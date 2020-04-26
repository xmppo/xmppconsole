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

/*
 * This is the most simple UI module that reads user's stanzas from STDIN and
 * prints logs to STDOUT. Work with multi-line input may be inconvenient.
 *
 * We use select(2) in order not to block the event loop. Also we rely on
 * terminal's buffering ability, so STDIN receives data when user presses
 * ENTER or terminal receives "\n" in other way.
 */

#include "ui.h"
#include "xmpp.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <strophe.h>
#include <sys/select.h>

#define UI_CONSOLE_INPUT_PERIOD 10

static bool is_done = false;

static int ui_console_init(struct xc_ui *ui)
{
	return 0;
}

static void ui_console_fini(struct xc_ui *ui)
{
}

static void ui_console_state_set(struct xc_ui *ui, xc_ui_state_t state)
{
	switch (state) {
	case XC_UI_UNKNOWN:
		/* Must not happen. */
		break;
	case XC_UI_INITED:
		break;
	case XC_UI_CONNECTING:
		break;
	case XC_UI_CONNECTED:
		printf("*** Connected ***\n");
		break;
	case XC_UI_DISCONNECTING:
		break;
	case XC_UI_DISCONNECTED:
		printf("*** Disconnected ***\n");
		is_done = true;
		break;
	}
}

static int ui_console_timed_cb(xmpp_conn_t *conn, void *userdata)
{
	struct xc_ui   *ui = userdata;
	char           *line = NULL;
	size_t          len = 0;
	ssize_t         rlen;
	struct timeval  tv;
	fd_set          rfds;
	int             rc;

	FD_ZERO(&rfds);
	FD_SET(0, &rfds);
	tv.tv_sec = 0;
	tv.tv_usec = 1;
	rc = select(1, &rfds, NULL, NULL, &tv);
	if (rc <= 0) {
		/* Nothing in STDIN right now. */
		return 1;
	}

	rlen = getline(&line, &len, stdin);
	if (rlen == -1 && errno == 0) {
		/* Consider this scenario as Ctrl+D. */
		xc_quit(ui->ui_ctx);
		free(line);
		return 0;
	}
	if (rlen > 0) {
		if (line[rlen - 1] == '\n')
			line[rlen - 1] = '\0';
		if (*line != '\0')
			xc_send(ui->ui_ctx, line);
	}
	/* line should be freed even if getline() fails. */
	free(line);

	return 1;
}

static void ui_console_run(struct xc_ui *ui)
{
	xmpp_ctx_t  *ctx = ui->ui_ctx->c_ctx;
	xmpp_conn_t *conn = ui->ui_ctx->c_conn;

	xmpp_timed_handler_add(conn, ui_console_timed_cb,
			       UI_CONSOLE_INPUT_PERIOD, ui);
	xmpp_run(ctx);
}

static void ui_console_print(struct xc_ui *ui, const char *msg)
{
	printf("%s\n", msg);
}

static bool ui_console_is_done(struct xc_ui *ui)
{
	return is_done;
}

static void ui_console_quit(struct xc_ui *ui)
{
	xmpp_ctx_t *ctx = ui->ui_ctx->c_ctx;

	xmpp_stop(ctx);
}

struct xc_ui_ops xc_ui_ops_console = {
	.uio_init      = ui_console_init,
	.uio_fini      = ui_console_fini,
	.uio_state_set = ui_console_state_set,
	.uio_run       = ui_console_run,
	.uio_print     = ui_console_print,
	.uio_is_done   = ui_console_is_done,
	.uio_quit      = ui_console_quit,
};
