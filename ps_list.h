/***
 * Copyright 2009-2017 by Gabriel Parmer.  All rights reserved.
 * Redistribution of this file is permitted under the BSD 2 clause license.
 * Author: Gabriel Parmer, gparmer@gwu.edu, 2017
 *
 * History:
 * - Initial implementation, ~2009
 * - Adapted for parsec and relicensed, 2016
 * - Made a bit more verbose for us mere mortals (SPM), 2019
 */

/*
 * Intrusive doubly-linked circular list.
 *
 * Embed a `struct ps_list` in your own struct, then use the macros below to
 * manipulate it.  `struct ps_list_head` is a separate sentinel type so the
 * compiler can catch accidental misuse.
 *
 * Example:
 *   struct ps_list_head h;
 *   struct foo { struct ps_list l; void *d; } node, *i, *tmp;
 *
 *   ps_list_head_init(&h);
 *   ps_list_init(&node, l);
 *   ps_list_head_add(&h, &node, l);
 *
 *   ps_list_foreach(&h, i, l) { ... }
 *   ps_list_foreach_del(&h, i, tmp, l) { ps_list_rem(i, l); free(i); }
 */

#ifndef PS_LIST_H
#define PS_LIST_H

struct ps_list {
	struct ps_list *next, *previous;
};

struct ps_list_head {
	struct ps_list list;
};

#define PS_LIST_DEF_NAME list

static inline void
ps_list_ll_init(struct ps_list *list)
{ list->next = list->previous = list; }

static inline void
ps_list_head_init(struct ps_list_head *list_head)
{ ps_list_ll_init(&list_head->list); }

static inline int
ps_list_ll_empty(struct ps_list *list)
{ return list->next == list; }

static inline int
ps_list_head_empty(struct ps_list_head *list_head)
{ return ps_list_ll_empty(&list_head->list); }

/* Adds new_node after list */
static inline void
ps_list_ll_add(struct ps_list *list, struct ps_list *new_node)
{
	new_node->next           = list->next;
	new_node->previous       = list;
	list->next               = new_node;
	new_node->next->previous = new_node;
}

/* Removes list from its list */
static inline void
ps_list_ll_rem(struct ps_list *list)
{
	list->next->previous = list->previous;
	list->previous->next = list->next;
	list->previous = list->next = list;
}

#define ps_offsetof(s, field) __builtin_offsetof(s, field)

#define ps_container(intern, type, field) \
	((type *)((char *)(intern) - ps_offsetof(type, field)))

#define ps_list_obj_get(l, o, lname) \
	ps_container(l, __typeof__(*(o)), lname)

#define ps_list_is_head(list_head, o, lname) \
	(ps_list_obj_get((list_head), (o), lname) == (o))

#define ps_list_singleton(o, lname)  ps_list_ll_empty(&(o)->lname)
#define ps_list_init(o, lname)       ps_list_ll_init(&(o)->lname)
#define ps_list_next(o, lname)       ps_list_obj_get((o)->lname.next, (o), lname)
#define ps_list_prev(o, lname)       ps_list_obj_get((o)->lname.previous, (o), lname)
#define ps_list_add(o, n, lname)     ps_list_ll_add(&(o)->lname, &(n)->lname)
#define ps_list_append(o, n, lname)  ps_list_add(ps_list_prev((o), lname), n, lname)
#define ps_list_rem(o, lname)        ps_list_ll_rem(&(o)->lname)

#define ps_list_head_add(list_head, o, lname) \
	ps_list_ll_add((&(list_head)->list), &(o)->lname)
#define ps_list_head_append(list_head, o, lname) \
	ps_list_ll_add((&(list_head)->list)->previous, &(o)->lname)

#define ps_list_head_first(list_head, type, lname) \
	ps_container(((list_head)->list.next), type, lname)
#define ps_list_head_last(list_head, type, lname) \
	ps_container(((list_head)->list.previous), type, lname)

/* _d variants assume the list field is named "list" (PS_LIST_DEF_NAME) */
#define ps_list_is_head_d(list_head, o)      ps_list_is_head(list_head, o, PS_LIST_DEF_NAME)
#define ps_list_singleton_d(o)               ps_list_singleton(o, PS_LIST_DEF_NAME)
#define ps_list_init_d(o)                    ps_list_init(o, PS_LIST_DEF_NAME)
#define ps_list_next_d(o)                    ps_list_next(o, PS_LIST_DEF_NAME)
#define ps_list_prev_d(o)                    ps_list_prev(o, PS_LIST_DEF_NAME)
#define ps_list_add_d(o, n)                  ps_list_add(o, n, PS_LIST_DEF_NAME)
#define ps_list_append_d(o, n)               ps_list_append(o, n, PS_LIST_DEF_NAME)
#define ps_list_rem_d(o)                     ps_list_rem(o, PS_LIST_DEF_NAME)
#define ps_list_head_last_d(list_head, o)    ps_list_head_last(list_head, o, PS_LIST_DEF_NAME)
#define ps_list_head_first_d(list_head, type) ps_list_head_first(list_head, type, PS_LIST_DEF_NAME)
#define ps_list_head_add_d(list_head, o)     ps_list_head_add(list_head, o, PS_LIST_DEF_NAME)
#define ps_list_head_append_d(list_head, o)  ps_list_head_append(list_head, o, PS_LIST_DEF_NAME)

#define ps_list_foreach(head, iter, lname) \
	for (iter = ps_list_head_first((head), __typeof__(*iter), lname); \
	     !ps_list_is_head((head), iter, lname); \
	     (iter) = ps_list_next(iter, lname))

#define ps_list_foreach_d(head, iter) \
	ps_list_foreach(head, iter, PS_LIST_DEF_NAME)

#define ps_list_foreach_del(head, iter, tmp, lname) \
	for (iter = ps_list_head_first((head), __typeof__(*iter), lname), \
	         (tmp) = ps_list_next((iter), lname); \
	     !ps_list_is_head((head), iter, lname); \
	     (iter) = (tmp), (tmp) = ps_list_next((tmp), lname))

#define ps_list_foreach_del_d(head, iter, tmp) \
	ps_list_foreach_del(head, iter, tmp, PS_LIST_DEF_NAME)

#endif /* PS_LIST_H */
