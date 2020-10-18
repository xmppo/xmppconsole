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

#include "misc.h"
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
	char *xo_jid;
	char *xo_passwd;
	char *xo_host;
	const char *xo_ui;
	xc_ui_type_t xo_ui_type;
	bool xo_help;
	bool xo_version;
	bool xo_auth_legacy;
	bool xo_raw_mode;
	bool xo_tls_disable;
	bool xo_tls_legacy;
	bool xo_tls_trust;
};

#define XC_RECONNECT_TRIES 5
#define XC_RECONNECT_TIMER 5000
#define XC_CONN_RAW_FEATURES_TIMEOUT 5000

static bool verbose_level = false;

#ifdef PACKAGE_NAME
static const char *xc_name = PACKAGE_NAME;
#else
static const char *xc_name = "xmppconsole";
#endif

#ifdef PACKAGE_VERSION
static const char *xc_version = PACKAGE_VERSION;
#elif defined(VERSION)
static const char *xc_version = VERSION;
#else
static const char *xc_version = "unknown";
#endif

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

	if (xc_streq(xmpp_stanza_get_name(stanza), "proceed")) {
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

	/* Establish TLS session if it is supported and not disabled */
	child = xmpp_stanza_get_child_by_name(stanza, "starttls");
	if (!ctx->c_tls_disable && !secured && child != NULL &&
	    xc_streq(xmpp_stanza_get_ns(child), XMPP_NS_TLS)) {
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

static int xc_reconnect_cb(xmpp_ctx_t *xmpp_ctx, void *userdata)
{
	struct xc_ctx *ctx = userdata;
	int            rc = 0;

	++ctx->c_attempts;
	if (ctx->c_attempts <= XC_RECONNECT_TRIES)
		rc = xc_connect(ctx, NULL, false);

	/* Don't remove timed handler if connection fails, reconnect later */
	return rc == 0 ? 0 : 1;
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
	 * In case of an authentication error, ask user to type new password
	 * in UI.
	 */

	switch (status) {
	case XMPP_CONN_CONNECT:
		ctx->c_attempts = 0;
		if (ctx->c_is_raw) {
			/* Special case for raw mode. */
			xc_handle_connect_raw(conn, ctx);
			break;
		}
		xc_ui_connected(ctx->c_ui);
		if (xc_ui_is_done(ctx->c_ui))
			xmpp_disconnect(conn);
		break;
	case XMPP_CONN_RAW_CONNECT:
		assert(ctx->c_is_raw);
		if (ctx->c_tls_legacy && !ctx->c_tls_disable) {
			int rc;

			rc = xmpp_conn_tls_start(conn);
			if (rc != 0) {
				xmpp_disconnect(conn);
				break;
			}
		}
		xmpp_conn_open_stream_default(conn);
		break;
	default:
		xc_ui_disconnected(ctx->c_ui);
		if (ctx->c_is_done || xc_ui_is_done(ctx->c_ui))
			xc_ui_quit(ctx->c_ui);
		else {
			xmpp_global_timed_handler_add(ctx->c_ctx,
						      xc_reconnect_cb,
						      XC_RECONNECT_TIMER, ctx);
		}
	}
}

static void xc_configure(struct xc_ctx *ctx, struct xc_options *opts)
{
	long xmpp_flags;

	assert(opts->xo_jid != NULL);

	xmpp_flags = opts->xo_tls_disable ?  XMPP_CONN_FLAG_DISABLE_TLS :
		     opts->xo_tls_trust ?    XMPP_CONN_FLAG_TRUST_TLS :
					     XMPP_CONN_FLAG_MANDATORY_TLS;
	xmpp_flags |= opts->xo_tls_legacy ?  XMPP_CONN_FLAG_LEGACY_SSL : 0;
	xmpp_flags |= opts->xo_auth_legacy ? XMPP_CONN_FLAG_LEGACY_AUTH : 0;
	xmpp_conn_set_flags(ctx->c_conn, xmpp_flags);
	xmpp_conn_set_jid(ctx->c_conn, opts->xo_jid);
	if (opts->xo_passwd != NULL) {
		xmpp_conn_set_pass(ctx->c_conn, opts->xo_passwd);
	}
	ctx->c_host        = opts->xo_host;
	ctx->c_port        = opts->xo_port;
	ctx->c_attempts    = 0;
	ctx->c_is_raw      = opts->xo_raw_mode;
	ctx->c_tls_disable = opts->xo_tls_disable;
	ctx->c_tls_legacy  = opts->xo_tls_legacy;
}

int xc_connect(struct xc_ctx *ctx, struct xc_options *opts, bool reconnect)
{
	int rc;

	if (opts != NULL) {
		if (ctx->c_conn != NULL)
			xmpp_conn_release(ctx->c_conn);
		ctx->c_conn = xmpp_conn_new(ctx->c_ctx);
		assert(ctx->c_conn != NULL);
		xc_configure(ctx, opts);
	}

	assert(ctx->c_conn != NULL);

	rc = ctx->c_is_raw ?
		xmpp_connect_raw(ctx->c_conn, ctx->c_host, ctx->c_port,
				 xc_conn_handler, ctx) :
		xmpp_connect_client(ctx->c_conn, ctx->c_host, ctx->c_port,
				    xc_conn_handler, ctx);
	if (rc == XMPP_EOK)
		xc_ui_connecting(ctx->c_ui);
	else if (reconnect) {
		xmpp_global_timed_handler_add(ctx->c_ctx, xc_reconnect_cb,
					      XC_RECONNECT_TIMER, ctx);
	}

	return (rc == XMPP_EOK || reconnect) ? 0 : -1;
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
	const char *tag_stream;
	const char *tag_xml;
	const char *ptr;
	char       *buf;
	size_t      len;

	tag_stream = strstr(msg, "<stream:stream");
	if (tag_stream != NULL) {
		/*
		 * Re-open a stream. We have to reset libstrophe's parser with
		 * a xmpp_conn_open_stream-like function.
		 */
		tag_xml = strstr(msg, "<?");
		ptr = tag_xml != NULL && tag_xml < tag_stream ? tag_xml : tag_stream;
		if (msg < ptr) {
			len = (size_t)(ptr - msg);
			buf = malloc(len + 1);
			if (buf != NULL) {
				strncpy(buf, msg, len);
				buf[len] = '\0';
				xmpp_send_raw_string(ctx->c_conn, "%s", buf);
				free(buf);
			}
		}
		/* TODO Don't ignore attributes in the users tag. */
		xmpp_conn_open_stream_default(ctx->c_conn);
		ptr = strstr(tag_stream, ">");
		if (ptr != NULL && *(ptr + 1) != '\0') {
			xmpp_send_raw_string(ctx->c_conn, "%s", ptr + 1);
		}
	} else {
		xmpp_send_raw_string(ctx->c_conn, "%s", msg);
	}
}

void xc_quit(struct xc_ctx *ctx)
{
	ctx->c_is_done = true;
	if (xmpp_conn_is_connected(ctx->c_conn))
		xmpp_disconnect(ctx->c_conn);
	else
		xc_ui_quit(ctx->c_ui);
}

static void xc_usage(FILE *stream, const char *name)
{
	fprintf(stream, "Usage: %s [OPTIONS] <JID> [PASSWORD]\n", name);
	fprintf(stream, "OPTIONS:\n"
			"  --help\t\tPrint this help\n"
			"  --host, -h <HOST>\tConnect to the host instead of "
								"domain\n"
			"  --noauth, -n\t\tConnect to the server without "
						"performing authentication\n"
			"  --port, -p <PORT>\tOverride default port number\n"
			"  --trust-tls-cert, -t\tTrust invalid TLS certificates\n"
			"  --disable-tls\t\tDon't establish TLS session\n"
			"  --legacy-ssl\t\tLegacy SSL mode (without STARTTLS "
								"support)\n"
			"  --legacy-auth\t\tAllow insecure legacy authentication\n"
			"  --ui, -u <NAME>\tUse specified UI. Available: any, "
#ifdef BUILD_UI_GTK
			"gtk, "
#endif
#ifdef BUILD_UI_NCURSES
			"ncurses, "
#endif
			"console.\n"
			"  --verbose, -v\t\tPrint debug messages\n"
			"  --version\t\tPrint version and exit\n"
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
	const char *name;

	static struct option long_opts[] = {
		{ "disable-tls", no_argument, 0, 0 },
		{ "help", no_argument, 0, 0 },
		{ "host", no_argument, 0, 'h' },
		{ "legacy-auth", no_argument, 0, 0 },
		{ "legacy-ssl", no_argument, 0, 0 },
		{ "noauth", no_argument, 0, 'n' },
		{ "port", required_argument, 0, 'p' },
		{ "trust-tls-cert", no_argument, 0, 't' },
		{ "ui", required_argument, 0, 'u' },
		{ "verbose", no_argument, 0, 'v' },
		{ "version", no_argument, 0, 0 },
		{ 0, 0, 0, 0 }
	};
	const char *short_opts = "h:np:tu:v";

	memset(opts, 0, sizeof(*opts));

	while (1) {
		int index = 0;
		c = getopt_long(argc, argv, short_opts, long_opts, &index);
		if (c == -1)
			break;
		switch (c) {
		case 'h':
			opts->xo_host = strdup(optarg);
			break;
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
			opts->xo_tls_trust = true;
			break;
		case 'u':
			opts->xo_ui = optarg;
			break;
		case 'v':
			verbose_level = true;
			break;
		case 0:
			/* Long only options */
			assert(index < ARRAY_SIZE(long_opts));
			name = long_opts[index].name;
			if (xc_streq(name, "help")) {
				opts->xo_help = true;
				return true;
			} else if (xc_streq(name, "disable-tls")) {
				opts->xo_tls_disable = true;
			} else if (xc_streq(name, "legacy-ssl")) {
				opts->xo_tls_legacy = true;
			} else if (xc_streq(name, "legacy-auth")) {
				opts->xo_auth_legacy = true;
			} else if (xc_streq(name, "version")) {
				opts->xo_version = true;
				return true;
			}
			break;
		default:
			return false;
		}
	}

	arg_nr = argc - optind;
	if (arg_nr < 1 || arg_nr > 2)
		return false;

	opts->xo_jid = strdup(argv[optind]);
	if (arg_nr > 1)
		opts->xo_passwd = strdup(argv[optind + 1]);

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

static void xc_options_fini(struct xc_options *opts)
{
	free(opts->xo_jid);
	free(opts->xo_host);
	if (opts->xo_passwd != NULL) {
		memset(opts->xo_passwd, 0, strlen(opts->xo_passwd));
		free(opts->xo_passwd);
	}
}

/* Global pointer for signal handler. */
static struct xc_ctx *g_ctx;

static void xc_sighandler(int signo)
{
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
	bool              result;
	int               rc;

	memset(&ctx, 0, sizeof(ctx));

	result = xc_options_parse(argc, argv, &opts);
	if (!result || opts.xo_help) {
		xc_usage(result ? stdout : stderr,
			 argc > 0 ? argv[0] : xc_name);
		exit(result ? EXIT_SUCCESS : EXIT_FAILURE);
	}
	if (opts.xo_version) {
		printf("%s version %s\n", xc_name, xc_version);
		exit(EXIT_SUCCESS);
	}

	rc = xc_ui_init(&ui, opts.xo_ui_type);
	assert(rc == 0);
	if (xc_ui_type(&ui) != XC_UI_GTK) {
		/* Debug logs can break a text UI. */
		verbose_level = false;
	}

	log = (xmpp_log_t){
		.handler = &xc_log_cb,
		.userdata = &ctx,
	};
	xmpp_initialize();
	ctx.c_ctx = xmpp_ctx_new(NULL, &log);
	assert(ctx.c_ctx != NULL);

	/* Check password. */
	if (opts.xo_passwd == NULL) {
		char *node = xmpp_jid_node(ctx.c_ctx, opts.xo_jid);

		if (node != NULL && !opts.xo_raw_mode) {
			(void)xc_ui_get_passwd(&ui, &opts.xo_passwd);
			xmpp_free(ctx.c_ctx, node);
		}
	}

	ctx.c_ui = &ui;
	xc_ui_ctx_set(&ui, &ctx);
	rc = xc_connect(&ctx, &opts, true);
	assert(rc == 0);

	g_ctx = &ctx;
	rc = sigaction(SIGTERM, &xc_sigaction, NULL)
	  ?: sigaction(SIGINT, &xc_sigaction, NULL);
	assert(rc == 0);
	rc = signal(SIGPIPE, SIG_IGN) == SIG_ERR ? -1 : 0;
	assert(rc == 0);

	/* Run main event loops */
	xc_ui_run(&ui);

	xmpp_conn_release(ctx.c_conn);
	xmpp_ctx_free(ctx.c_ctx);
	xmpp_shutdown();

	xc_ui_fini(&ui);
	xc_options_fini(&opts);

	return 0;
}
