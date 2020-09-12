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
 *
 * Note, this file contains code from an ISC licensed example which is located
 * at https://github.com/ulfalizer/readline-and-ncurses. Its license:
 * Copyright (c) 2015-2019, Ulf Magnusson <ulfalizer@gmail.com>
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */

#ifdef BUILD_UI_NCURSES

#include "ui.h"
#include "xmpp.h"

#define _XOPEN_SOURCE 700

#include <assert.h>
#include <curses.h>
#include <errno.h>
#include <locale.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <stdlib.h>
#include <strophe.h>
#include <wctype.h>

/*
 * TODO
 *  - Resize
 */

struct xc_ui_ncurses {
	WINDOW *win_log;
	WINDOW *win_sep;
	WINDOW *win_inp;
	const char *last_status;
};

#define UI_NCURSES_INPUT_TIMEOUT 1

#define max(a, b)         \
  ({ typeof(a) _a = a;    \
     typeof(b) _b = b;    \
     _a > _b ? _a : _b; })

static bool           is_done = false;
static bool           is_stop = false;
static struct xc_ctx *g_ctx = NULL;
static struct xc_ui  *g_ui = NULL;
static int            g_input = 0;
static bool           g_input_avail = false;
static chtype         g_sep_color = A_STANDOUT;

static int ui_ncurses_input_avail_cb(void)
{
	return g_input_avail;
}

static int ui_ncurses_getc_cb(FILE *dummy)
{
	int input = g_input;

	g_input = 0;
	g_input_avail = false;

	return input;
}

/*
 * Calculates the cursor column for the readline window in a way that supports
 * multibyte, multi-column and combining characters. readline itself calculates
 * this as part of its default redisplay function and does not export the
 * cursor column.
 *
 * Returns the total width (in columns) of the characters in the 'n'-byte
 * prefix of the null-terminated multibyte string 's'. If 'n' is larger than
 * 's', returns the total width of the string. Tries to emulate how readline
 * prints some special characters.
 *
 * 'offset' is the current horizontal offset within the line. This is used to
 * get tab stops right.
 *
 * Makes a guess for malformed strings.
 */
static size_t ui_ncurses_strnwidth(const char *s, size_t n, size_t offset)
{
	mbstate_t shift_state;
	wchar_t wc;
	size_t wc_len;
	size_t width = 0;

	/* Start in the initial shift state */
	memset(&shift_state, '\0', sizeof shift_state);

	for (size_t i = 0; i < n; i += wc_len) {
		/* Extract the next multibyte character */
		wc_len = mbrtowc(&wc, s + i, MB_CUR_MAX, &shift_state);
		switch (wc_len) {
		case 0:
			/* Reached the end of the string */
			goto done;
		case (size_t)-1:
			/* Fallthrough */
		case (size_t)-2:
			/*
			 * Failed to extract character. Guess that each character is one
			 * byte/column wide each starting from the invalid character to
			 * keep things simple.
			 */
			width += strnlen(s + i, n - i);
			goto done;
		}

		if (wc == '\t')
			width = ((width + offset + 8) & ~7) - offset;
		else {
			/*
			 * TODO: readline also outputs ~<letter> and the like for some
			 * non-printable characters
			 */
			width += iswcntrl(wc) ? 2 : max(0, wcwidth(wc));
		}
	}
done:
	return width;
}

static void ui_ncurses_status_set(struct xc_ui_ncurses *priv, const char *status)
{
	priv->last_status = status;
	if (*status == '\0') {
		wbkgd(priv->win_sep, g_sep_color);
	} else {
		werase(priv->win_sep);
		mvwaddstr(priv->win_sep, 0, 0, status);
	}
	wrefresh(priv->win_sep);
}

static void ui_ncurses_move_cursor(struct xc_ui_ncurses *priv)
{
	size_t pos = ui_ncurses_strnwidth(rl_line_buffer, rl_point, 0);
	wmove(priv->win_inp, 0, pos);
}

static void ui_ncurses_redisplay_cursor(struct xc_ui_ncurses *priv)
{
	ui_ncurses_move_cursor(priv);
	wrefresh(priv->win_inp);
}

static void ui_ncurses_redisplay_cb(void)
{
	struct xc_ui_ncurses *priv = g_ui->ui_priv;

	werase(priv->win_inp);
	mvwaddstr(priv->win_inp, 0, 0, rl_line_buffer);
	ui_ncurses_move_cursor(priv);
	wrefresh(priv->win_inp);
}

static void ui_ncurses_redisplay_log(struct xc_ui_ncurses *priv)
{
	/*
	 * TODO Maintain a list of the lines and print required lines.
	 * This feature will require accurate handling of wrapped lines
	 * and multibyte characters.
	 * Scrolling feature will use this function to redraw the window
	 * after changing position in the list of lines.
	 */
	wrefresh(priv->win_log);
}

static void ui_ncurses_redisplay_sep(struct xc_ui_ncurses *priv)
{
	ui_ncurses_status_set(priv, priv->last_status);
}

static void ui_ncurses_resize(struct xc_ui_ncurses *priv)
{
	if (LINES >= 3) {
		wresize(priv->win_log, LINES - 2, COLS);
		wresize(priv->win_sep, 1, COLS);
		wresize(priv->win_inp, 1, COLS);
		mvwin(priv->win_sep, LINES - 2, 0);
		mvwin(priv->win_inp, LINES - 1, 0);
	}
	ui_ncurses_redisplay_log(priv);
	ui_ncurses_redisplay_sep(priv);
	ui_ncurses_redisplay_cb();
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
	char                 *loc;

	priv = malloc(sizeof(*priv));
	assert(priv != NULL);

	loc = setlocale(LC_ALL, "");
	if (loc == NULL)
		return -ENODEV;

	result = initscr();
	if (result == NULL)
		return -ENODEV;

	cbreak();
	noecho();
	nonl();
	intrflush(NULL, FALSE);

	priv->win_log = newwin(LINES - 2, COLS, 0, 0);
	priv->win_sep = newwin(1, COLS, LINES - 2, 0);
	priv->win_inp = newwin(1, COLS, LINES - 1, 0);

	scrollok(priv->win_log, TRUE);
	wtimeout(priv->win_inp, UI_NCURSES_INPUT_TIMEOUT);

	priv->last_status = "";
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

static void ui_ncurses_state_set(struct xc_ui *ui, xc_ui_state_t state)
{
	struct xc_ui_ncurses *priv = ui->ui_priv;

	switch (state) {
	case XC_UI_UNKNOWN:
		/* Must not happen. */
		break;
	case XC_UI_INITED:
		g_ctx = ui->ui_ctx;
		g_ui = ui;
		ui_ncurses_status_set(priv, "");
		ui_ncurses_rl_init();
		break;
	case XC_UI_CONNECTING:
		ui_ncurses_status_set(priv, "[Connecting...]");
		break;
	case XC_UI_CONNECTED:
		ui_ncurses_status_set(priv, "[Connected]");
		break;
	case XC_UI_DISCONNECTING:
		ui_ncurses_status_set(priv, "[Disconnecting...]");
		break;
	case XC_UI_DISCONNECTED:
		ui_ncurses_status_set(priv, "[Disconnected]");
		break;
	}
	ui_ncurses_redisplay_cursor(priv);
}

static void ui_ncurses_run(struct xc_ui *ui)
{
	struct xc_ui_ncurses *priv = ui->ui_priv;
	xmpp_ctx_t           *ctx = ui->ui_ctx->c_ctx;
	int                   c;

	while (!is_stop) {
		c = wgetch(priv->win_inp);
		switch (c) {
		case ERR:
			xmpp_run_once(ctx, 1);
			break;
		case KEY_RESIZE:
			ui_ncurses_resize(priv);
			break;
		case '\f':
			clearok(curscr, TRUE);
			ui_ncurses_resize(priv);
			break;
		default:
			g_input = c;
			g_input_avail = true;
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
	ui_ncurses_redisplay_cursor(priv);
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

#endif /* BUILD_UI_NCURSES */
