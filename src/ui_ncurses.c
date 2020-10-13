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

#include "list.h"
#include "ui.h"
#include "xmpp.h"

/* Include strnlen() and others. */
#ifdef _XOPEN_SOURCE
#undef _XOPEN_SOURCE
#endif
#define _XOPEN_SOURCE 700

#include <assert.h>
#include <curses.h>
#include <errno.h>
#include <locale.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strophe.h>
#include <wchar.h>
#include <wctype.h>

struct ui_ncurses_line {
	char *line;
	size_t chars_nr;
	uint32_t magic;
	struct xc_list_link link;
};

struct xc_ui_ncurses {
	WINDOW *win_log;
	WINDOW *win_sep;
	WINDOW *win_inp;
	size_t win_inp_offset;
	size_t win_inp_pos;
	size_t lines_nr;
	const char *last_status;
	struct xc_list lines;
	struct ui_ncurses_line *line_current;
	bool paged;
};

#define UI_NCURSES_INPUT_TIMEOUT 1
#define UI_NCURSES_LINES_MAX 1024
#define UI_NCURSES_LINE_MAGIC 0xdeadbeef
#define UI_NCURSES_TERMINAL_TITLE "xmppconsole"

#define XC_LINE_TO_ROWS(line) (((line)->chars_nr + COLS - 1) / COLS)
/* Number of visible rows. The last row is always empty because of '\n'. */
#define XC_LOG_ROWS ((int)LINES - 3)

static struct xc_list_descr ui_ncurses_lines_descr =
	XC_LIST_DESCR("log lines", struct ui_ncurses_line, link, magic,
		      UI_NCURSES_LINE_MAGIC);

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
static int            g_sep_color = A_STANDOUT;
static int            g_ctl_color = A_STANDOUT;

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
 * This is a helper function which processes maximum 'len_max' bytes of the
 * null-terminated string 's' and 'wlen_max' in width.
 *
 * In 'wlen_out' returns the total width (in columns) of the characters in the
 * prefix of the null-terminated multibyte string 's'.
 *
 * In 'len_out' returns the prefix size in bytes.
 *
 * If both limits 'wlen_max' and 'len_max' are larger than 's', returns the
 * total width and size of the string respectively.
 *
 * Tries to emulate how readline prints some special characters. Makes a guess
 * for malformed strings.
 *
 * 'offset' is the current horizontal offset within the line. This is used to
 * get tab stops right.
 */
static void ui_ncurses_strnwidth_helper(const char *s,
					size_t      wlen_max,
					size_t      len_max,
					size_t      offset,
					size_t     *wlen_out,
					size_t     *len_out)
{
	mbstate_t shift_state;
	wchar_t wc;
	size_t wc_len;
	size_t width = 0;
	size_t i;

	/* Start in the initial shift state */
	memset(&shift_state, '\0', sizeof shift_state);

	for (i = 0; i < len_max && width < wlen_max; i += wc_len) {
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
			wc_len = strnlen(s + i, len_max - i);
			width += wc_len;
			if (width > wlen_max) {
				wc_len -= width - wlen_max;
				width = wlen_max;
			}
			i += wc_len;
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
	if (wlen_out != NULL)
		*wlen_out = width;
	if (len_out != NULL)
		*len_out = i;
}

static size_t ui_ncurses_strnwidth(const char *s, size_t n, size_t offset)
{
	size_t wlen;

	ui_ncurses_strnwidth_helper(s, SIZE_MAX, n, offset, &wlen, NULL);

	return wlen;
}

static size_t ui_ncurses_strwidth(const char *s, size_t offset)
{
	return ui_ncurses_strnwidth(s, SIZE_MAX, offset);
}

static size_t ui_ncurses_strnlen(const char *s, size_t wlen_max, size_t offset)
{
	size_t len;

	ui_ncurses_strnwidth_helper(s, wlen_max, SIZE_MAX, offset, NULL, &len);

	return len;
}

static void ui_ncurses_line_destroy_first(struct xc_ui_ncurses *priv)
{
	struct ui_ncurses_line *item = xc_list_dequeue(&priv->lines);

	if (item != NULL) {
		if (priv->line_current == item)
			priv->line_current = xc_list_head(&priv->lines);
		free(item->line);
		free(item);
	}
}

static void ui_ncurses_status_set(struct xc_ui_ncurses *priv, const char *status)
{
	const char *jid = NULL;
	const char *secure = "";
	char buf[64];
	size_t len;

	priv->last_status = status;
	werase(priv->win_sep);
	if (*status == '[') {
		/* Status update branch. */
		if (g_ctx != NULL && g_ctx->c_conn != NULL) {
			jid = xmpp_conn_get_bound_jid(g_ctx->c_conn) ?:
			      xmpp_conn_get_jid(g_ctx->c_conn);
			if (xmpp_conn_is_connected(g_ctx->c_conn)) {
				secure = xmpp_conn_is_secured(g_ctx->c_conn) ?
					 "[TLS] " : "[PLAIN] ";
			}
		}
		len = strlen(status) + strlen(secure) +
		      (jid != NULL ? strlen(jid) : 0);
		if (len + 2 > COLS)
			secure = "";
		snprintf(buf, sizeof(buf), "%s%s", secure, status);
		len = strlen(buf);

		mvwaddstr(priv->win_sep, 0, 1, jid != NULL ? jid : "");
		if (len < COLS)
			mvwaddstr(priv->win_sep, 0, COLS - len - 1, buf);
	} else if (*status != '\0') {
		/* If not a status update, just print the message. */
		mvwaddstr(priv->win_sep, 0, 0, status);
	}
	wrefresh(priv->win_sep);
}

static size_t ui_ncurses_cursor_pos(struct xc_ui_ncurses *priv)
{
	return ui_ncurses_strnwidth(rl_line_buffer, rl_point, 0);
}

static void ui_ncurses_move_cursor(struct xc_ui_ncurses *priv, size_t pos)
{
	wmove(priv->win_inp, 0, pos);
}

static void ui_ncurses_redisplay_cursor(struct xc_ui_ncurses *priv)
{
	ui_ncurses_move_cursor(priv, priv->win_inp_pos);
	wrefresh(priv->win_inp);
}

static void ui_ncurses_redisplay_cb(void)
{
	struct xc_ui_ncurses *priv = g_ui->ui_priv;
	size_t pos = ui_ncurses_cursor_pos(priv);
	size_t width = ui_ncurses_strwidth(rl_line_buffer, 0);
	size_t len;
	const char *s;

	/*
	 * Adjust offset when input is larger than the window width.
	 * When the window is scrolled horizontally, we print symbols '<' and
	 * '>' to indicate that only a part of the input is displayed. Take
	 * this into account, so cursor doesn't overlap with the symbols.
	 */
	if (priv->win_inp_offset > 0 && width < COLS) {
		priv->win_inp_offset = 0;
	} else if (priv->win_inp_offset > 0 && pos == priv->win_inp_offset + 1) {
		priv->win_inp_offset -= COLS / 2;
	} else if (priv->win_inp_offset > 0 && pos <= priv->win_inp_offset) {
		priv->win_inp_offset = pos > COLS - 1 ? pos - COLS + 1 : 0;
	} else if (pos - priv->win_inp_offset >= COLS - 1 &&
		   width - priv->win_inp_offset >= COLS) {
		priv->win_inp_offset += COLS / 2;
	}

	werase(priv->win_inp);
	s = rl_line_buffer +
		ui_ncurses_strnlen(rl_line_buffer, priv->win_inp_offset, 0);
	len = ui_ncurses_strnlen(s, COLS, 0);
	mvwaddnstr(priv->win_inp, 0, 0, s, len);
	if (priv->win_inp_offset > 0) {
		wattron(priv->win_inp, g_ctl_color);
		mvwaddstr(priv->win_inp, 0, 0, "<");
		wattroff(priv->win_inp, g_ctl_color);
	}
	if (width - priv->win_inp_offset > COLS) {
		wattron(priv->win_inp, g_ctl_color);
		mvwaddstr(priv->win_inp, 0, COLS - 1, ">");
		wattroff(priv->win_inp, g_ctl_color);
	}
	priv->win_inp_pos = pos - priv->win_inp_offset;
	ui_ncurses_move_cursor(priv, priv->win_inp_pos);
	wrefresh(priv->win_inp);
}

static void ui_ncurses_redisplay_log(struct xc_ui_ncurses *priv)
{
	struct ui_ncurses_line *p = NULL;
	struct ui_ncurses_line *tmp;
	size_t rows;
	size_t i;

	rows = XC_LOG_ROWS < 0 ? 0 : (size_t)XC_LOG_ROWS;

	if (!priv->paged) {
		tmp = xc_list_tail(&priv->lines);
		i = 0;
		while (tmp != NULL && i < rows) {
			p = tmp;
			i += XC_LINE_TO_ROWS(p);
			tmp = xc_list_prev(&priv->lines, tmp);
		}
		/*
		 * This is a hack to print all the required lines. Otherwise,
		 * it may happen that the last line will never be printed.
		 */
		rows = i;
	} else {
		p = priv->line_current;
	}

	werase(priv->win_log);
	i = 0;
	while (p != NULL && i < rows) {
		waddstr(priv->win_log, p->line);
		waddstr(priv->win_log, "\n");
		i += XC_LINE_TO_ROWS(p);
		p = xc_list_next(&priv->lines, p);
	}

	wrefresh(priv->win_log);
	ui_ncurses_redisplay_cursor(priv);
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

static void ui_ncurses_scrollup(size_t nr)
{
	struct xc_ui_ncurses *priv = g_ui->ui_priv;
	size_t rows;
	size_t i;

	rows = XC_LOG_ROWS < 0 ? 0 : (size_t)XC_LOG_ROWS;

	if (!priv->paged) {
		/* Find the 1st visible line. */
		priv->line_current = xc_list_tail(&priv->lines);
		for (i = 0; priv->line_current != NULL;) {
			i += XC_LINE_TO_ROWS(priv->line_current);
			if (i >= rows)
				break;
			priv->line_current =
				xc_list_prev(&priv->lines, priv->line_current);
		}
		priv->paged = priv->line_current != NULL;
	}

	for (i = 0; i < nr && priv->line_current != NULL;) {
		priv->line_current =
				xc_list_prev(&priv->lines, priv->line_current);
		if (priv->line_current != NULL)
			i += XC_LINE_TO_ROWS(priv->line_current);
	}
	if (priv->line_current == NULL && priv->paged) {
		priv->line_current = xc_list_head(&priv->lines);
	}

	ui_ncurses_redisplay_log(priv);
}

static void ui_ncurses_scrolldown(size_t nr)
{
	struct xc_ui_ncurses *priv = g_ui->ui_priv;
	struct ui_ncurses_line *p;
	size_t rows;
	size_t i;

	rows = XC_LOG_ROWS < 0 ? 0 : (size_t)XC_LOG_ROWS;

	if (priv->paged) {
		for (i = 0; i < nr && priv->line_current != NULL;) {
			i += XC_LINE_TO_ROWS(priv->line_current);
			priv->line_current =
				xc_list_next(&priv->lines, priv->line_current);
		}
		/* Check whether we have enough lines to fill the window. */
		for (i = 0, p = priv->line_current; p != NULL;) {
			i += XC_LINE_TO_ROWS(p);
			if (i >= rows)
				break;
			p = xc_list_next(&priv->lines, p);
		}
		priv->paged = p != NULL;
		if (!priv->paged)
			priv->line_current = NULL;
	}

	ui_ncurses_redisplay_log(priv);
}

static int ui_ncurses_pageup_cb()
{
	size_t nr;

	/* How many lines to scroll. Scroll by 1 for very small window. */
	nr = XC_LOG_ROWS <= 0 ? 1 : (size_t)XC_LOG_ROWS;
	/* Scroll by a half of the window. */
	nr = (nr + 1) / 2;
	ui_ncurses_scrollup(nr);

	return 0;
}

static int ui_ncurses_pagedown_cb()
{
	size_t nr;

	/* How many lines to scroll. Scroll by 1 for very small window. */
	nr = XC_LOG_ROWS <= 0 ? 1 : (size_t)XC_LOG_ROWS;
	/* Scroll by a half of the window. */
	nr = (nr + 1) / 2;
	ui_ncurses_scrolldown(nr);

	return 0;
}

static int ui_ncurses_up_cb()
{
	ui_ncurses_scrollup(1);
	return 0;
}

static int ui_ncurses_down_cb()
{
	ui_ncurses_scrolldown(1);
	return 0;
}

static void ui_ncurses_rl_init(void)
{
	rl_bind_key ('\t', rl_insert);
	/* PageUp and PageDown. Doesn't work... */
	rl_bind_keyseq("\\e[5~", ui_ncurses_pageup_cb);
	rl_bind_keyseq("\\e[6~", ui_ncurses_pagedown_cb);
#if 0
	/*
	 * These bindings are for UP and DOWN arrows, enabling them
	 * will break history scrolling.
	 */
	rl_bind_keyseq("\\e[A", ui_ncurses_up_cb);
	rl_bind_keyseq("\\e[B", ui_ncurses_down_cb);
#endif
	/* Mouse wheel in some terminals. */
	rl_bind_keyseq("\\eOA", ui_ncurses_up_cb);
	rl_bind_keyseq("\\eOB", ui_ncurses_down_cb);
	/* Ctrl + arrows */
	rl_bind_keyseq("\\e[1;5A", ui_ncurses_up_cb);
	rl_bind_keyseq("\\e[1;5B", ui_ncurses_down_cb);

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

	printf("\033]0;" UI_NCURSES_TERMINAL_TITLE "\007");

	result = initscr();
	if (result == NULL)
		return -ENODEV;

	cbreak();
	noecho();
	nonl();
	intrflush(NULL, FALSE);

	if (has_colors()) {
		/* XXX These functions may fail. */
		start_color();
		use_default_colors();
		init_pair(1, COLOR_WHITE, COLOR_BLUE);
		init_pair(2, COLOR_CYAN, COLOR_BLUE);
		g_sep_color = COLOR_PAIR(1);
		g_ctl_color = COLOR_PAIR(2);
	}

	priv->win_log = newwin(LINES - 2, COLS, 0, 0);
	priv->win_sep = newwin(1, COLS, LINES - 2, 0);
	priv->win_inp = newwin(1, COLS, LINES - 1, 0);

	scrollok(priv->win_log, TRUE);
	wtimeout(priv->win_inp, UI_NCURSES_INPUT_TIMEOUT);
	wbkgd(priv->win_sep, g_sep_color);

	xc_list_init(&priv->lines, &ui_ncurses_lines_descr);
	priv->win_inp_offset = 0;
	priv->win_inp_pos = 0;
	priv->lines_nr = 0;
	priv->line_current = NULL;
	priv->paged = false;
	priv->last_status = "";
	ui->ui_priv = priv;

	/* We need a global pointer to access it from readline callbacks. */
	g_ui = ui;

	wrefresh(priv->win_log);
	ui_ncurses_redisplay_sep(priv);
	wrefresh(priv->win_inp);

	return 0;
}

static void ui_ncurses_fini(struct xc_ui *ui)
{
	struct xc_ui_ncurses *priv = ui->ui_priv;

	while (!xc_list_is_empty(&priv->lines)) {
		ui_ncurses_line_destroy_first(priv);
	}
	xc_list_fini(&priv->lines);
	delwin(priv->win_inp);
	delwin(priv->win_sep);
	delwin(priv->win_log);
	endwin();
	free(priv);
}

static int ui_ncurses_get_passwd(struct xc_ui *ui, char **out)
{
	struct xc_ui_ncurses *priv = ui->ui_priv;
	bool                  ready = false;
	char                  passwd[256];
	int                   i = 0;
	int                   c;

	ui_ncurses_status_set(priv, "Enter password: ");

	keypad(priv->win_sep, TRUE);
	notimeout(priv->win_sep, TRUE);
	while (!ready) {
		c = wgetch(priv->win_sep);
		switch (c) {
		case ERR:
			assert(0);
		case 13:
			passwd[i] = '\0';
			ready = true;
			break;
		case 27:
			i = 0;
			ready = true;
			break;
		case 127:
			if (i > 0)
				--i;
			break;
		case KEY_RESIZE:
			ui_ncurses_resize(priv);
			break;
		default:
			if (c < 256 && i + 1 < (int)sizeof(passwd))
				passwd[i++] = (char)c;
		}
	}

	ui_ncurses_status_set(priv, "");
	notimeout(priv->win_sep, FALSE);
	keypad(priv->win_sep, FALSE);
	*out = i > 0 ? strdup(passwd) : NULL;
	memset(passwd, 0, sizeof(passwd));

	return 0;
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
		ui_ncurses_status_set(priv, "");
		ui_ncurses_rl_init();
		break;
	case XC_UI_CONNECTING:
		ui_ncurses_status_set(priv, "[connecting...]");
		break;
	case XC_UI_CONNECTED:
		ui_ncurses_status_set(priv, "[online]");
		break;
	case XC_UI_DISCONNECTING:
		ui_ncurses_status_set(priv, "[disconnecting...]");
		break;
	case XC_UI_DISCONNECTED:
		ui_ncurses_status_set(priv, "[offline]");
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
	struct ui_ncurses_line *item;
	const char *s = msg;
	const char *p;

	do {
		if (*s == '\0')
			break;

		item = malloc(sizeof *item);
		if (item == NULL)
			break;

		p = strchr(s, '\n');
		if (p == NULL) {
			item->line = strdup(s);
		} else {
			item->line = strndup(s, p - s);
			s = p + 1;
			while (*s != '\0' && *s == '\r')
				++s;
		}
		if (item->line == NULL) {
			free(item);
			break;
		}

		item->chars_nr = ui_ncurses_strwidth(s, 0);
		xc_list_insert_tail(&priv->lines, item);
		if (priv->lines_nr >= UI_NCURSES_LINES_MAX) {
			ui_ncurses_line_destroy_first(priv);
		} else {
			++priv->lines_nr;
		}
	} while (p != NULL);

	if (!priv->paged) {
		waddstr(priv->win_log, msg);
		waddstr(priv->win_log, "\n");
		wrefresh(priv->win_log);
		ui_ncurses_redisplay_cursor(priv);
	}
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
	.uio_init       = ui_ncurses_init,
	.uio_fini       = ui_ncurses_fini,
	.uio_get_passwd = ui_ncurses_get_passwd,
	.uio_state_set  = ui_ncurses_state_set,
	.uio_run        = ui_ncurses_run,
	.uio_print      = ui_ncurses_print,
	.uio_is_done    = ui_ncurses_is_done,
	.uio_quit       = ui_ncurses_quit,
};

#undef XC_LOG_ROWS
#undef XC_LINE_TO_ROWS

#endif /* BUILD_UI_NCURSES */
