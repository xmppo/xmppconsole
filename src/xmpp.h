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

#ifndef __XMPPCONSOLE_XMPP_H__
#define __XMPPCONSOLE_XMPP_H__

#include <strophe.h>

/* Forward declarations */
struct xc_ui;

struct xc_ctx {
	xmpp_ctx_t   *c_ctx;
	xmpp_conn_t  *c_conn;
	const char   *c_host;
	struct xc_ui *c_ui;
};

void xc_send(struct xc_ctx *ctx, const char *msg);
void xc_quit(struct xc_ctx *ctx);

#endif /* __XMPPCONSOLE_XMPP_H__ */
