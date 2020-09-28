/*
 * XMPP Console - a tool for XMPP hackers
 *
 * Copyright (C) 2020 Dmitry Podgorny <pasis.ua@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * xmppconsole is an interactive tool that sends raw stanzas over an XMPP
 * connection and displays full XMPP streams.
 *
 * Main purpose of the tool is to study XEPs and debug servers behavior.
 *
 * For GTK UI, main priority is given to the GTK main loop, libstrophe
 * loop is executed in a timer callback. This is done in order to improve
 * responsiveness of the UI.
 */

#include "ui.h"
#include "xmpp.h"

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strophe.h>

struct xc_options {
	unsigned short xo_port;
	const char *xo_jid;
	const char *xo_passwd;
	const char *xo_host;
	const char *xo_ui;
	xc_ui_type_t xo_ui_type;
	bool xo_help;
	bool xo_raw_mode;
	bool xo_trust_tls;
};

#define XC_RECONNECT_TIMER 5000
#define XC_CONN_RAW_FEATURES_TIMEOUT 5000

static int xc_reconnect_cb(xmpp_ctx_t *xmpp_ctx, void *userdata);

static bool connected = false;
static bool signalled = false;
static bool verbose_level = false;

static int xc_conn_raw_error_handler(xmpp_conn_t *conn,
				     xmpp_stanza_t *stanza,
				     void *userdata)
{
	xmpp_disconnect(conn);
	return 0;
}

static int xc_conn_raw_proceedtls_handler(xmpp_conn_t *conn,
					  xmpp_stanza_t *stanza,
					  void *userdata)
{
	int rc = -1;

	if (strcmp(xmpp_stanza_get_name(stanza), "proceed") == 0) {
		rc = xmpp_conn_tls_start(conn);
		if (rc == 0) {
			xmpp_handler_delete(conn, xc_conn_raw_error_handler);
			xmpp_conn_open_stream_default(conn);
		}
	}

	/* Failed TLS spoils the connection, so disconnect */
	if (rc != 0)
		xmpp_disconnect(conn);

	return 0;
}

static int xc_conn_raw_missing_features_handler(xmpp_conn_t *conn,
					        void *userdata)
{
	fprintf(stderr, "Error: haven't received features\n");
	xmpp_disconnect(conn);
	return 0;
}

static int xc_conn_raw_features_handler(xmpp_conn_t *conn,
					xmpp_stanza_t *stanza,
					void *userdata)
{
	struct xc_ctx *ctx = userdata;
	bool secured = !!xmpp_conn_is_secured(conn);
	xmpp_stanza_t *child;

	xmpp_timed_handler_delete(conn, xc_conn_raw_missing_features_handler);

	child = xmpp_stanza_get_child_by_name(stanza, "starttls");
	if (!secured && child != NULL &&
	    strcmp(xmpp_stanza_get_ns(child), XMPP_NS_TLS) == 0) {
		child = xmpp_stanza_new(ctx->c_ctx);
		xmpp_stanza_set_name(child, "starttls");
		xmpp_stanza_set_ns(child, XMPP_NS_TLS);
		xmpp_handler_add(conn, xc_conn_raw_proceedtls_handler,
				 XMPP_NS_TLS, NULL, NULL, userdata);
		xmpp_send(conn, child);
		xmpp_stanza_release(child);
		return 0;
	}

	xc_ui_connected(ctx->c_ui);
	connected = true;
	if (xc_ui_is_done(ctx->c_ui))
		xmpp_disconnect(conn);

	return 0;
}

static void xc_handle_connect_raw(xmpp_conn_t *conn, struct xc_ctx *ctx)
{
	xmpp_handler_add(conn, xc_conn_raw_error_handler, XMPP_NS_STREAMS,
			 "error", NULL, ctx);
	xmpp_handler_add(conn, xc_conn_raw_features_handler, XMPP_NS_STREAMS,
			 "features", NULL, ctx);
	xmpp_timed_handler_add(conn, xc_conn_raw_missing_features_handler,
			       XC_CONN_RAW_FEATURES_TIMEOUT, NULL);
}

static void xc_conn_handler(xmpp_conn_t         *conn,
			    xmpp_conn_event_t    status,
			    int                  error,
			    xmpp_stream_error_t *stream_error,
			    void                *userdata)
{
	struct xc_ctx *ctx = userdata;

	/*
	 * TODO Distinguish between network issues and authentication error.
	 * When the later happens, don't reconnect.
	 */

	switch (status) {
	case XMPP_CONN_CONNECT:
		if (ctx->c_is_raw) {
			/* Special case for raw mode. */
			xc_handle_connect_raw(conn, ctx);
			break;
		}
		xc_ui_connected(ctx->c_ui);
		connected = true;
		if (xc_ui_is_done(ctx->c_ui))
			xmpp_disconnect(conn);
		break;
	case XMPP_CONN_RAW_CONNECT:
		assert(ctx->c_is_raw);
		xmpp_conn_open_stream_default(conn);
		break;
	default:
		xc_ui_disconnected(ctx->c_ui);
		connected = false;
		if (signalled || xc_ui_is_done(ctx->c_ui))
			xc_ui_quit(ctx->c_ui);
		else {
			xmpp_global_timed_handler_add(ctx->c_ctx,
						      xc_reconnect_cb,
						      XC_RECONNECT_TIMER, ctx);
		}
	}
}

static int xc_connect(xmpp_conn_t *conn, struct xc_ctx *ctx)
{
	int rc;

	rc = ctx->c_is_raw ?
		xmpp_connect_raw(conn, ctx->c_host, ctx->c_port,
				 xc_conn_handler, ctx) :
		xmpp_connect_client(conn, ctx->c_host, ctx->c_port,
				    xc_conn_handler, ctx);
	if (rc == XMPP_EOK)
		xc_ui_connecting(ctx->c_ui);

	return rc == XMPP_EOK ? 0 : -1;
}

static int xc_reconnect_cb(xmpp_ctx_t *xmpp_ctx, void *userdata)
{
	struct xc_ctx *ctx = userdata;
	int            rc;

	rc = xc_connect(ctx->c_conn, ctx);

	/* Don't remove timed handler if connection fails, reconnect later */
	return rc == 0 ? 0 : 1;
}

static bool should_display(const char *msg)
{
	/* Display only sent and received stanzas. */
	return strncmp(msg, "SENT:", 5) == 0 ||
	       strncmp(msg, "RECV:", 5) == 0;
}

static void xc_log_cb(void             *userdata,
		      xmpp_log_level_t  level,
		      const char       *area,
		      const char       *msg)
{
	struct xc_ctx *ctx = userdata;

	if (should_display(msg))
		xc_ui_print(ctx->c_ui, msg);

	/* Debug output */
	if (verbose_level)
		printf("[%d] %s: %s\n", level, area, msg);
}

void xc_send(struct xc_ctx *ctx, const char *msg)
{
	xmpp_send_raw_string(ctx->c_conn, "%s", msg);
}

void xc_quit(struct xc_ctx *ctx)
{
	/* TODO: Replace with xmpp_conn_is_connected() for libstrophe-0.10 */
	if (connected)
		xmpp_disconnect(ctx->c_conn);
	else
		xc_ui_quit(ctx->c_ui);
}

static void xc_usage(FILE *stream, const char *name)
{
	fprintf(stream, "Usage: %s [OPTIONS] <JID> <PASSWORD> [HOST]\n", name);
	fprintf(stream, "OPTIONS:\n"
			"  --help, -h\t\tPrint this help\n"
			"  --noauth, -n\t\tConnect to the server without "
						"performing authentication\n"
			"  --port, -p <PORT>\tOverride default port number\n"
			"  --trust-tls-cert, -t\tTrust TLS certificate "
						"(use at your own risk!)\n"
			"  --ui, -u <NAME>\tUse specified UI. Available: any, "
#ifdef BUILD_UI_GTK
			"gtk, "
#endif
#ifdef BUILD_UI_NCURSES
			"ncurses, "
#endif
			"console.\n"
		);
}

static bool xc_options_parse(int argc, char **argv, struct xc_options *opts)
{
	int c;
	int arg_nr;
	int base = 10;
	long tmp_long;
	char *endptr;
	const char *tmp_str;

	static struct option long_opts[] = {
		{ "help", no_argument, 0, 'h' },
		{ "noauth", no_argument, 0, 'n' },
		{ "port", required_argument, 0, 'p' },
		{ "trust-tls-cert", no_argument, 0, 't' },
		{ "ui", required_argument, 0, 'u' },
		{ 0, 0, 0, 0 }
	};
	const char *short_opts = "hnp:tu:";

	memset(opts, 0, sizeof(*opts));

	while (1) {
		int index = 0;
		c = getopt_long(argc, argv, short_opts, long_opts, &index);
		if (c == -1)
			break;
		switch (c) {
		case 'h':
			opts->xo_help = true;
			return true;
		case 'n':
			opts->xo_raw_mode = true;
			break;
		case 'p':
			base = strncmp(optarg, "0x", 2) == 0 ? 16 : 10;
			tmp_str = base == 16 ? optarg + 2 : optarg;
			errno = 0;
			tmp_long = strtol(tmp_str, &endptr, base);
			if ((errno != 0 && tmp_long == 0) || *endptr != '\0') {
				fprintf(stderr, "Invalid value for port: %s\n",
					optarg);
				return false;
			}
			if (tmp_long > 0xffff || tmp_long < 0) {
				fprintf(stderr, "Port number must be between "
					"0 and 65535\n");
				return false;
			}
			opts->xo_port = (unsigned short)tmp_long;
			break;
		case 't':
			opts->xo_trust_tls = true;
			break;
		case 'u':
			opts->xo_ui = optarg;
			break;
		default:
			return false;
		}
	}

	arg_nr = argc - optind;
	if (arg_nr < 2 || arg_nr > 3)
		return false;

	opts->xo_jid = argv[optind];
	opts->xo_passwd = argv[optind + 1];
	if (arg_nr > 2)
		opts->xo_host = argv[optind + 2];

	/* Parse UI string */
	opts->xo_ui_type = xc_ui_name_to_type(opts->xo_ui);
	if (opts->xo_ui_type == XC_UI_ERROR) {
		fprintf(stderr, "Unknown UI name: %s. Check spelling and "
			"whether the UI module is builtin (with --help).\n",
			opts->xo_ui ? opts->xo_ui : "<NULL>");
		return false;
	}

	return true;
}

/* Global pointer for signal handler. */
static struct xc_ctx *g_ctx;

static void xc_sighandler(int signo)
{
	signalled = true;
	xc_quit(g_ctx);
}

static struct sigaction xc_sigaction = {
	.sa_handler = xc_sighandler,
};

int main(int argc, char **argv)
{
	struct xc_options opts;
	struct xc_ui      ui;
	struct xc_ctx     ctx;
	xmpp_log_t        log;
	xmpp_ctx_t       *xmpp_ctx;
	xmpp_conn_t      *xmpp_conn;
	long              xmpp_flags;
	bool              result;
	int               rc;

	memset(&ctx, 0, sizeof(ctx));

	result = xc_options_parse(argc, argv, &opts);
	if (!result || opts.xo_help) {
		xc_usage(result ? stdout : stderr,
			 argc > 0 ? argv[0] : "xmppconsole");
		exit(result ? EXIT_SUCCESS : EXIT_FAILURE);
	}

	/*
	 * TODO Let user type JID/password in UI.
	 */

	rc = xc_ui_init(&ui, opts.xo_ui_type);
	assert(rc == 0);

	log = (xmpp_log_t){
		.handler = &xc_log_cb,
		.userdata = &ctx,
	};
	xmpp_initialize();
	xmpp_ctx = xmpp_ctx_new(NULL, &log);
	assert(xmpp_ctx != NULL);
	xmpp_conn = xmpp_conn_new(xmpp_ctx);
	assert(xmpp_conn != NULL);
	xmpp_flags = opts.xo_trust_tls ? XMPP_CONN_FLAG_TRUST_TLS :
					 XMPP_CONN_FLAG_MANDATORY_TLS;
	xmpp_conn_set_flags(xmpp_conn, xmpp_flags);
	xmpp_conn_set_jid(xmpp_conn, opts.xo_jid);
	xmpp_conn_set_pass(xmpp_conn, opts.xo_passwd);
	ctx.c_host   = opts.xo_host;
	ctx.c_port   = opts.xo_port;
	ctx.c_ctx    = xmpp_ctx;
	ctx.c_conn   = xmpp_conn;
	ctx.c_is_raw = opts.xo_raw_mode;
	ctx.c_ui     = &ui;

	xc_ui_ctx_set(&ui, &ctx);
	rc = xc_connect(xmpp_conn, &ctx);
	if (rc != 0) {
		xmpp_global_timed_handler_add(xmpp_ctx, xc_reconnect_cb,
					      XC_RECONNECT_TIMER, &ctx);
	}

	g_ctx = &ctx;
	rc = sigaction(SIGTERM, &xc_sigaction, NULL)
	  ?: sigaction(SIGINT, &xc_sigaction, NULL);
	assert(rc == 0);
	rc = signal(SIGPIPE, SIG_IGN) == SIG_ERR ? -1 : 0;
	assert(rc == 0);

	/* Run main event loops */
	xc_ui_run(&ui);

	xmpp_conn_release(xmpp_conn);
	xmpp_ctx_free(xmpp_ctx);
	xmpp_shutdown();

	xc_ui_fini(&ui);

	return 0;
}
