/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Basic linked list handling code.  Operations that modify the list
// return the new list head.

#ifndef EVILWM_LIST_H__
#define EVILWM_LIST_H__

// Each list element is of this deliberately transparent type:
struct list {
	struct list *next;
	void *data;
};

// Each of these return the new pointer to the head of the list:
struct list *list_insert_before(struct list *list, struct list *before, void *data);
struct list *list_prepend(struct list *list, void *data);
struct list *list_append(struct list *list, void *data);
struct list *list_delete(struct list *list, void *data);
struct list *list_to_head(struct list *list, void *data);
struct list *list_to_tail(struct list *list, void *data);

// Returns element in list:
struct list *list_find(struct list *list, void *data);

#endif
