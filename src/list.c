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

#include "list.h"

#include <assert.h>

static bool list_invariant(struct xc_list *list)
{
	const struct xc_list_descr *descr = list->l_descr;
	struct xc_list_link        *head  = &list->l_head;

	return descr != NULL &&
	       list->l_magic == descr->ld_magic &&
	       ((head->ll_next == head && head->ll_prev == head) ||
		(head->ll_next != head && head->ll_prev != head));
}

void xc_list_init(struct xc_list *list, const struct xc_list_descr *descr)
{
	assert(descr->ld_name != NULL);

	list->l_descr = descr;
	list->l_head.ll_next = &list->l_head;
	list->l_head.ll_prev = &list->l_head;
	list->l_magic = descr->ld_magic;

	assert(list_invariant(list));
	assert(xc_list_is_empty(list));
}

void xc_list_fini(struct xc_list *list)
{
	assert(list_invariant(list));
	assert(xc_list_is_empty(list));

	/* TODO Poisoning in debug mode */
}

static uint32_t *list_obj_magic(struct xc_list *list, void *obj)
{
	return (uint32_t *)((char *)obj + list->l_descr->ld_magic_off);
}

static void list_obj_magic_set(struct xc_list *list, void *obj)
{
	*list_obj_magic(list, obj) = list->l_descr->ld_magic;
}

static bool list_obj_magic_is_correct(struct xc_list *list, void *obj)
{
	uint32_t magic = *list_obj_magic(list, obj);

	return magic == list->l_descr->ld_magic;
}

static struct xc_list_link *list_obj_link(struct xc_list *list, void *obj)
{
	return (struct xc_list_link *)((char *)obj +
					list->l_descr->ld_link_off);
}

static void *list_link2obj(struct xc_list *list, struct xc_list_link *link)
{
	void *obj = (char *)link - list->l_descr->ld_link_off;

	assert(list_obj_magic_is_correct(list, obj));
	return obj;
}

static void list_insert_before_link(struct xc_list      *list,
				    void                *obj,
				    struct xc_list_link *before)
{
	struct xc_list_link *link = list_obj_link(list, obj);

	assert(list_invariant(list));

	link->ll_next = before;
	link->ll_prev = before->ll_prev;
	link->ll_prev->ll_next = link;
	before->ll_prev = link;

	list_obj_magic_set(list, obj);
}

static void list_insert_after_link(struct xc_list      *list,
				   void                *obj,
				   struct xc_list_link *after)
{
	struct xc_list_link *link = list_obj_link(list, obj);

	assert(list_invariant(list));

	link->ll_next = after->ll_next;
	link->ll_prev = after;
	link->ll_next->ll_prev = link;
	after->ll_next = link;

	list_obj_magic_set(list, obj);
}

void xc_list_insert(struct xc_list *list, void *obj)
{
	xc_list_insert_head(list, obj);
}

void xc_list_insert_head(struct xc_list *list, void *obj)
{
	list_insert_after_link(list, obj, &list->l_head);
}

void xc_list_insert_tail(struct xc_list *list, void *obj)
{
	list_insert_before_link(list, obj, &list->l_head);
}

void xc_list_insert_before(struct xc_list *list, void *obj, void *before)
{
	struct xc_list_link *link_before = list_obj_link(list, before);

	assert(list_invariant(list));
	assert(list_obj_magic_is_correct(list, before));

	list_insert_before_link(list, obj, link_before);
}

void xc_list_insert_after(struct xc_list *list, void *obj, void *after)
{
	struct xc_list_link *link_after = list_obj_link(list, after);

	assert(list_invariant(list));
	assert(list_obj_magic_is_correct(list, after));

	list_insert_after_link(list, obj, link_after);
}

void xc_list_del(struct xc_list *list, void *obj)
{
	struct xc_list_link *link = list_obj_link(list, obj);

	assert(list_invariant(list));
	assert(list_obj_magic_is_correct(list, obj));
	assert(link->ll_prev->ll_next == link);
	assert(link->ll_next->ll_prev == link);

	link->ll_prev->ll_next = link->ll_next;
	link->ll_next->ll_prev = link->ll_prev;

	link->ll_next = link;
	link->ll_prev = link;
}

void xc_list_push(struct xc_list *list, void *obj)
{
	xc_list_insert_head(list, obj);
}

void *xc_list_pop(struct xc_list *list)
{
	void *obj = xc_list_head(list);

	if (obj != NULL)
		xc_list_del(list, obj);

	return obj;
}

void xc_list_enqueue(struct xc_list *list, void *obj)
{
	xc_list_insert_tail(list, obj);
}

void *xc_list_dequeue(struct xc_list *list)
{
	void *obj = xc_list_head(list);

	if (obj != NULL)
		xc_list_del(list, obj);

	return obj;
}

void *xc_list_dequeue_last(struct xc_list *list)
{
	void *obj = xc_list_tail(list);

	if (obj != NULL)
		xc_list_del(list, obj);

	return obj;
}

void *xc_list_head(struct xc_list *list)
{
	return xc_list_is_empty(list) ? NULL :
	       list_link2obj(list, list->l_head.ll_next);
}

void *xc_list_tail(struct xc_list *list)
{
	return xc_list_is_empty(list) ? NULL :
	       list_link2obj(list, list->l_head.ll_prev);
}

void *xc_list_next(struct xc_list *list, void *obj)
{
	struct xc_list_link *link = list_obj_link(list, obj);

	assert(list_invariant(list));
	assert(list_obj_magic_is_correct(list, obj));
	link = link->ll_next;

	return link == &list->l_head ? NULL : list_link2obj(list, link);
}

void *xc_list_prev(struct xc_list *list, void *obj)
{
	struct xc_list_link *link = list_obj_link(list, obj);

	assert(list_invariant(list));
	assert(list_obj_magic_is_correct(list, obj));
	link = link->ll_prev;

	return link == &list->l_head ? NULL : list_link2obj(list, link);
}

bool xc_list_is_empty(struct xc_list *list)
{
	struct xc_list_link *head = &list->l_head;
	assert(list_invariant(list));

	return head->ll_next == head && head->ll_prev == head;
}

int xc_list_count(struct xc_list *list)
{
	struct xc_list_link *link = &list->l_head;
	int count;

	assert(list_invariant(list));
	for (count = 0; link->ll_next != &list->l_head; ++count)
		link = link->ll_next;

	return count;
}
