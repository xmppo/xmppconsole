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

#include "misc.h"
#include "ui.h"
#include "xmpp.h"

/* Headers for UI implementations */
#include "ui_console.h"
#include "ui_gtk.h"
#include "ui_ncurses.h"

#include <string.h>

static const struct {
	const char *name;
	xc_ui_type_t type;
} xc_ui_names[] = {
	{ "any", XC_UI_ANY, },
	{ "console", XC_UI_CONSOLE, },
#ifdef BUILD_UI_GTK
	{ "gtk", XC_UI_GTK, },
#endif
#ifdef BUILD_UI_NCURSES
	{ "ncurses", XC_UI_NCURSES, },
#endif
};
static const size_t xc_ui_names_nr = ARRAY_SIZE(xc_ui_names);

xc_ui_type_t xc_ui_name_to_type(const char *name)
{
	xc_ui_type_t result = XC_UI_ERROR;
	size_t i;

	if (name == NULL) {
		result = XC_UI_ANY;
	} else {
		for (i = 0; i < xc_ui_names_nr; ++i) {
			if (xc_streq(xc_ui_names[i].name, name)) {
				result = xc_ui_names[i].type;
				break;
			}
		}
	}
	return result;
}

xc_ui_type_t xc_ui_type(struct xc_ui *ui)
{
	/*
	 * TODO Make an array of all UI modules with descriptions like
	 * name, type, pointer to the ops structure. Then rewrite xc_ui_type()
	 * and xc_ui_init() to work with the array without hardcoding
	 * knowledge of existing modules.
	 */
	if (ui->ui_ops == &xc_ui_ops_gtk)
		return XC_UI_GTK;
	else if (ui->ui_ops == &xc_ui_ops_ncurses)
		return XC_UI_NCURSES;
	else if (ui->ui_ops == &xc_ui_ops_console)
		return XC_UI_CONSOLE;
	else
		return XC_UI_ERROR;
}

int xc_ui_init(struct xc_ui *ui, xc_ui_type_t type)
{
	int rc = -1;

#ifdef BUILD_UI_GTK
	if (type == XC_UI_GTK || type == XC_UI_ANY) {
		ui->ui_ops = &xc_ui_ops_gtk;
		rc = ui->ui_ops->uio_init(ui);
	}
#endif /* BUILD_UI_GTK */

#ifdef BUILD_UI_NCURSES
	if (type == XC_UI_NCURSES || (type == XC_UI_ANY && rc != 0)) {
		ui->ui_ops = &xc_ui_ops_ncurses;
		rc = ui->ui_ops->uio_init(ui);
	}
#endif /* BUILD_UI_NCURSES */

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

int xc_ui_get_passwd(struct xc_ui *ui, char **out)
{
	return ui->ui_ops->uio_get_passwd(ui, out);
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
