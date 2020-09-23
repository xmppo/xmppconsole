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

#ifndef __XC_LIST_H__
#define __XC_LIST_H__

#include <stdbool.h>	/* bool */
#include <stddef.h>	/* offsetof */
#include <stdint.h>	/* uint32_t */

/*
 * List implementation has been copied from PPPoAT project. Refer to PPPoAT
 * sources for high level and detailed level designs.
 */

struct xc_list_descr {
	const char    *ld_name;
	unsigned long  ld_link_off;
	unsigned long  ld_magic_off;
	uint32_t       ld_magic;
};

struct xc_list_link {
	struct xc_list_link *ll_prev;
	struct xc_list_link *ll_next;
};

struct xc_list {
	const struct xc_list_descr *l_descr;
	struct xc_list_link         l_head;
	uint32_t                    l_magic;
};

void xc_list_init(struct xc_list *list, const struct xc_list_descr *descr);
void xc_list_fini(struct xc_list *list);

void xc_list_insert(struct xc_list *list, void *obj);
void xc_list_insert_head(struct xc_list *list, void *obj);
void xc_list_insert_tail(struct xc_list *list, void *obj);
void xc_list_insert_before(struct xc_list *list, void *obj, void *before);
void xc_list_insert_after(struct xc_list *list, void *obj, void *after);

void xc_list_del(struct xc_list *list, void *obj);

void xc_list_push(struct xc_list *list, void *obj);
void *xc_list_pop(struct xc_list *list);

void xc_list_enqueue(struct xc_list *list, void *obj);
void *xc_list_dequeue(struct xc_list *list);
void *xc_list_dequeue_last(struct xc_list *list);

void *xc_list_head(struct xc_list *list);
void *xc_list_tail(struct xc_list *list);
void *xc_list_next(struct xc_list *list, void *obj);
void *xc_list_prev(struct xc_list *list, void *obj);

bool xc_list_is_empty(struct xc_list *list);

int xc_list_count(struct xc_list *list);

#define XC_LIST_DESCR(name, type, link_field, magic_field, magic) \
{                                                                 \
	.ld_name      = name,                                     \
	.ld_link_off  = offsetof(type, link_field),               \
	.ld_magic_off = offsetof(type, magic_field),              \
	.ld_magic     = magic,                                    \
}

#endif /* __XC_LIST_H__ */
