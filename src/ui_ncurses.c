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

#include "ui.h"
#include "xmpp.h"

#include <assert.h>
#include <curses.h>
#include <errno.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <stdlib.h>
#include <strophe.h>

/*
 * TODO
 *  - Resize
 *  - Proper cursor position
 */

struct xc_ui_ncurses {
	WINDOW *win_log;
	WINDOW *win_sep;
	WINDOW *win_inp;
};

#define UI_NCURSES_INPUT_TIMEOUT 1

static bool           is_done = false;
static bool           is_stop = false;
static struct xc_ctx *g_ctx = NULL;
static struct xc_ui  *g_ui = NULL;
static int            g_input = 0;

static int ui_ncurses_input_avail_cb(void)
{
	return g_input != 0;
}

static int ui_ncurses_getc_cb(FILE *dummy)
{
	int input = g_input;

	g_input = 0;

	return input;
}

static void ui_ncurses_redisplay_cb(void)
{
	struct xc_ui_ncurses *priv = g_ui->ui_priv;

	werase(priv->win_inp);
	mvwaddstr(priv->win_inp, 0, 0, rl_line_buffer);
	wrefresh(priv->win_inp);
}

static void ui_ncurses_input_cb(char *line)
{
	if (line == NULL) {
		/* Consider this as Ctrl+D. */
		is_done = true;
		xc_quit(g_ctx);
	} else if (*line != '\0') {
		add_history(line);
		xc_send(g_ctx, line);
	}
}

static void ui_ncurses_rl_init(void)
{
	rl_catch_signals = 0;
	rl_catch_sigwinch = 0;
	rl_deprep_term_function = NULL;
	rl_prep_term_function = NULL;
	rl_change_environment = 0;

	rl_getc_function = ui_ncurses_getc_cb;
	rl_input_available_hook = ui_ncurses_input_avail_cb;
	rl_redisplay_function = ui_ncurses_redisplay_cb;
	rl_callback_handler_install("", ui_ncurses_input_cb);
}

static int ui_ncurses_init(struct xc_ui *ui)
{
	struct xc_ui_ncurses *priv;
	WINDOW               *result;

	priv = malloc(sizeof(*priv));
	assert(priv != NULL);

	result = initscr();
	if (result == NULL)
		return -ENODEV;

	noecho();

	priv->win_log = newwin(LINES - 2, COLS, 0, 0);
	priv->win_sep = newwin(1, COLS, LINES - 2, 0);
	priv->win_inp = newwin(1, COLS, LINES - 1, 0);

	scrollok(priv->win_log, TRUE);
	wtimeout(priv->win_inp, UI_NCURSES_INPUT_TIMEOUT);

	ui->ui_priv = priv;

	return 0;
}

static void ui_ncurses_fini(struct xc_ui *ui)
{
	struct xc_ui_ncurses *priv = ui->ui_priv;

	delwin(priv->win_inp);
	delwin(priv->win_sep);
	delwin(priv->win_log);
	endwin();
	free(priv);
}

static void ui_ncurses_status_set(struct xc_ui *ui, const char *status)
{
	struct xc_ui_ncurses *priv = ui->ui_priv;

	if (*status == '\0') {
		wbkgd(priv->win_sep, A_STANDOUT);
	} else {
		werase(priv->win_sep);
		mvwaddstr(priv->win_sep, 0, 0, status);
	}
	wrefresh(priv->win_sep);
}

static void ui_ncurses_state_set(struct xc_ui *ui, xc_ui_state_t state)
{
	switch (state) {
	case XC_UI_UNKNOWN:
		/* Must not happen. */
		break;
	case XC_UI_INITED:
		g_ctx = ui->ui_ctx;
		g_ui = ui;
		ui_ncurses_status_set(ui, "");
		ui_ncurses_rl_init();
		break;
	case XC_UI_CONNECTING:
		ui_ncurses_status_set(ui, "[Connecting...]");
		break;
	case XC_UI_CONNECTED:
		ui_ncurses_status_set(ui, "[Connected]");
		break;
	case XC_UI_DISCONNECTING:
		ui_ncurses_status_set(ui, "[Disconnecting...]");
		break;
	case XC_UI_DISCONNECTED:
		ui_ncurses_status_set(ui, "[Disconnected]");
		is_done = true;
		break;
	}
}

static void ui_ncurses_run(struct xc_ui *ui)
{
	struct xc_ui_ncurses *priv = ui->ui_priv;
	xmpp_ctx_t           *ctx = ui->ui_ctx->c_ctx;
	int                   c;

	while (!is_stop) {
		c = wgetch(priv->win_inp);
		if (c == ERR) {
			xmpp_run_once(ctx, 1);
		} else {
			g_input = c;
			rl_callback_read_char();
		}
	}
}

static void ui_ncurses_print(struct xc_ui *ui, const char *msg)
{
	struct xc_ui_ncurses *priv = ui->ui_priv;

	waddstr(priv->win_log, msg);
	waddstr(priv->win_log, "\n");
	wrefresh(priv->win_log);
}

static bool ui_ncurses_is_done(struct xc_ui *ui)
{
	return is_done;
}

static void ui_ncurses_quit(struct xc_ui *ui)
{
	is_stop = true;
}

struct xc_ui_ops xc_ui_ops_ncurses = {
	.uio_init      = ui_ncurses_init,
	.uio_fini      = ui_ncurses_fini,
	.uio_state_set = ui_ncurses_state_set,
	.uio_run       = ui_ncurses_run,
	.uio_print     = ui_ncurses_print,
	.uio_is_done   = ui_ncurses_is_done,
	.uio_quit      = ui_ncurses_quit,
};
