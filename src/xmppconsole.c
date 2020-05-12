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
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <strophe.h>

/* #define HAVE_NEW_LIBSTROPHE 1 */

#define XC_RECONNECT_TIMER 3000

static int xc_reconnect_cb(xmpp_ctx_t *xmpp_ctx, void *userdata);

static bool connected = false;
static bool signalled = false;
static bool verbose_level = false;

static void xc_conn_handler(xmpp_conn_t         *conn,
			    xmpp_conn_event_t    status,
			    int                  error,
			    xmpp_stream_error_t *stream_error,
			    void                *userdata)
{
	struct xc_ctx *ctx = userdata;

	if (status == XMPP_CONN_CONNECT) {
		xc_ui_connected(ctx->c_ui);
		connected = true;
		if (xc_ui_is_done(ctx->c_ui))
			xmpp_disconnect(conn);
	} else {
		xc_ui_disconnected(ctx->c_ui);
		connected = false;
		if (signalled || xc_ui_is_done(ctx->c_ui))
			xc_ui_quit(ctx->c_ui);
		else {
#ifdef HAVE_NEW_LIBSTROPHE
			xmpp_ctx_timed_handler_add(ctx->c_ctx, xc_reconnect_cb,
						   XC_RECONNECT_TIMER, ctx);
#endif /* HAVE_NEW_LIBSTROPHE */
		}
	}
}

static int xc_connect(xmpp_conn_t *conn, struct xc_ctx *ctx)
{
	int rc;

	rc = xmpp_connect_client(conn, NULL, 0, xc_conn_handler, ctx);
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
	struct xc_ui   ui;
	struct xc_ctx  ctx;
	xmpp_log_t     log;
	xmpp_ctx_t    *xmpp_ctx;
	xmpp_conn_t   *xmpp_conn;
	const char    *jid;
	const char    *pass;
	int            rc;

	/*
	 * TODO Let user type JID/password in UI.
	 */
	if (argc < 3) {
		printf("Usage: xmppconsole <jid> <password>\n");
		return 1;
	}
	jid = argv[1];
	pass = argv[2];

	rc = xc_ui_init(&ui, XC_UI_ANY);
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
	xmpp_conn_set_flags(xmpp_conn, XMPP_CONN_FLAG_MANDATORY_TLS);
	xmpp_conn_set_jid(xmpp_conn, jid);
	xmpp_conn_set_pass(xmpp_conn, pass);
	ctx.c_ctx  = xmpp_ctx;
	ctx.c_conn = xmpp_conn;
	ctx.c_ui   = &ui;

	xc_ui_ctx_set(&ui, &ctx);
	rc = xc_connect(xmpp_conn, &ctx);
	if (rc != 0) {
#ifdef HAVE_NEW_LIBSTROPHE
		xmpp_ctx_timed_handler_add(xmpp_ctx, xc_reconnect_cb,
					   XC_RECONNECT_TIMER, &ctx);
#else
		(void)xc_reconnect_cb;
#endif /* HAVE_NEW_LIBSTROPHE */
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
