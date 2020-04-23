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

/* Headers for UI implementations */
#include "ui_console.h"
#include "ui_gtk.h"

#include <string.h>

int xc_ui_init(struct xc_ui *ui, xc_ui_type_t type)
{
	int rc = -1;

	if (type == XC_UI_GTK || type == XC_UI_ANY) {
		ui->ui_ops = &xc_ui_ops_gtk;
		rc = ui->ui_ops->uio_init(ui);
	}
#ifdef HAVE_NCURSES
	if (type == XC_UI_NCURSES || (type == XC_UI_ANY && rc != 0)) {
		ui->ui_ops = &xc_ui_ops_ncurses;
		rc = ui->ui_ops->uio_init(ui);
	}
#endif
	if (type == XC_UI_CONSOLE || (type == XC_UI_ANY && rc != 0)) {
		ui->ui_ops = &xc_ui_ops_console;
		rc = ui->ui_ops->uio_init(ui);
	}
	return rc;
}

void xc_ui_fini(struct xc_ui *ui)
{
	ui->ui_ops->uio_fini(ui);
}

void xc_ui_ctx_set(struct xc_ui *ui, struct xc_ctx *ctx)
{
	ui->ui_ctx = ctx;
	ui->ui_ops->uio_state_set(ui, XC_UI_INITED);
}

void xc_ui_connecting(struct xc_ui *ui)
{
	ui->ui_ops->uio_state_set(ui, XC_UI_CONNECTING);
}

void xc_ui_connected(struct xc_ui *ui)
{
	ui->ui_ops->uio_state_set(ui, XC_UI_CONNECTED);
}

void xc_ui_disconnecting(struct xc_ui *ui)
{
	ui->ui_ops->uio_state_set(ui, XC_UI_DISCONNECTING);
}

void xc_ui_disconnected(struct xc_ui *ui)
{
	ui->ui_ops->uio_state_set(ui, XC_UI_DISCONNECTED);
}

void xc_ui_run(struct xc_ui *ui)
{
	ui->ui_ops->uio_run(ui);
}

void xc_ui_print(struct xc_ui *ui, const char *msg)
{
	ui->ui_ops->uio_print(ui, msg);
}

bool xc_ui_is_done(struct xc_ui *ui)
{
	return ui->ui_ops->uio_is_done(ui);
}

void xc_ui_quit(struct xc_ui *ui)
{
	ui->ui_ops->uio_quit(ui);
}
