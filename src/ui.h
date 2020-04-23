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

#ifndef __XMPPCONSOLE_UI_H__
#define __XMPPCONSOLE_UI_H__

#include <stdbool.h>

/* Forward declarations */
struct xc_ctx;
struct xc_ui_ops;

typedef enum {
	XC_UI_UNKNOWN,
	XC_UI_INITED,
	XC_UI_CONNECTING,
	XC_UI_CONNECTED,
	XC_UI_DISCONNECTING,
	XC_UI_DISCONNECTED,
} xc_ui_state_t;

typedef enum {
	XC_UI_ANY,
	XC_UI_GTK,
	XC_UI_NCURSES,
	XC_UI_CONSOLE,
} xc_ui_type_t;

struct xc_ui {
	const char             *ui_name;
	struct xc_ctx          *ui_ctx;
	const struct xc_ui_ops *ui_ops;
	void                   *ui_priv;
};

struct xc_ui_ops {
	int  (*uio_init)(struct xc_ui *ui);
	void (*uio_fini)(struct xc_ui *ui);
	void (*uio_state_set)(struct xc_ui *ui, xc_ui_state_t state);
	void (*uio_run)(struct xc_ui *ui);
	void (*uio_print)(struct xc_ui *ui, const char *msg);
	bool (*uio_is_done)(struct xc_ui *ui);
	void (*uio_quit)(struct xc_ui *ui);
};

int  xc_ui_init(struct xc_ui *ui, xc_ui_type_t type);
void xc_ui_fini(struct xc_ui *ui);
void xc_ui_ctx_set(struct xc_ui *ui, struct xc_ctx *ctx);
void xc_ui_connecting(struct xc_ui *ui);
void xc_ui_connected(struct xc_ui *ui);
void xc_ui_disconnecting(struct xc_ui *ui);
void xc_ui_disconnected(struct xc_ui *ui);
void xc_ui_run(struct xc_ui *ui);
void xc_ui_print(struct xc_ui *ui, const char *msg);
bool xc_ui_is_done(struct xc_ui *ui);
void xc_ui_quit(struct xc_ui *ui);

#endif /* __XMPPCONSOLE_UI_H__ */
