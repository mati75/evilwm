/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Extended Window Manager Hints

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <unistd.h>

#include <X11/X.h>
#include <X11/Xlib.h>

#include "client.h"
#include "display.h"
#include "ewmh.h"
#include "list.h"
#include "log.h"
#include "screen.h"
#include "util.h"

// Maintain a reasonably sized allocated block of memory for lists
// of windows (for feeding to XChangeProperty in one hit).
static Window *window_array = NULL;
static Window *alloc_window_array(void);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Update various properties that reflect the screen geometry.

void ewmh_set_screen_workarea(struct screen *s) {
	unsigned long workarea[4] = {
		0, 0,
		DisplayWidth(display.dpy, s->screen), DisplayHeight(display.dpy, s->screen)
	};
	XChangeProperty(display.dpy, s->root, X_ATOM(_NET_DESKTOP_GEOMETRY),
			XA_CARDINAL, 32, PropModeReplace,
			(unsigned char *)&workarea[2], 2);
	XChangeProperty(display.dpy, s->root, X_ATOM(_NET_DESKTOP_VIEWPORT),
			XA_CARDINAL, 32, PropModeReplace,
			(unsigned char *)&workarea[0], 2);
	XChangeProperty(display.dpy, s->root, X_ATOM(_NET_WORKAREA),
			XA_CARDINAL, 32, PropModeReplace,
			(unsigned char *)&workarea, 4);
}

// Update the _NET_CLIENT_LIST property for a screen.  This is a simple list of
// all client windows in the order they were mapped.

void ewmh_set_net_client_list(struct screen *s) {
	Window *windows = alloc_window_array();
	int i = 0;
	if (windows) {
		for (struct list *iter = clients_mapping_order; iter; iter = iter->next) {
			struct client *c = iter->data;
			if (c->screen == s) {
				windows[i++] = c->window;
			}
		}
	}
	XChangeProperty(display.dpy, s->root, X_ATOM(_NET_CLIENT_LIST),
			XA_WINDOW, 32, PropModeReplace,
			(unsigned char *)windows, i);
}

// Update the _NET_CLIENT_LIST_STACKING property for a screen.  Similar to
// _NET_CLIENT_LIST, but in stacking order (bottom to top).

void ewmh_set_net_client_list_stacking(struct screen *s) {
	Window *windows = alloc_window_array();
	int i = 0;
	if (windows) {
		for (struct list *iter = clients_stacking_order; iter; iter = iter->next) {
			struct client *c = iter->data;
			if (c->screen == s) {
				windows[i++] = c->window;
			}
		}
	}
	XChangeProperty(display.dpy, s->root, X_ATOM(_NET_CLIENT_LIST_STACKING),
			XA_WINDOW, 32, PropModeReplace,
			(unsigned char *)windows, i);
}

// Update _NET_CURRENT_DESKTOP for screen to currently selected vdesk.

void ewmh_set_net_current_desktop(struct screen *s) {
	unsigned long vdesk = s->vdesk;
	XChangeProperty(display.dpy, s->root, X_ATOM(_NET_CURRENT_DESKTOP),
			XA_CARDINAL, 32, PropModeReplace,
			(unsigned char *)&vdesk, 1);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Set the _NET_WM_ALLOWED_ACTIONS on a client advertising what we support.

void ewmh_set_allowed_actions(struct client *c) {
	Atom allowed_actions[] = {
		X_ATOM(_NET_WM_ACTION_MOVE),
		X_ATOM(_NET_WM_ACTION_MAXIMIZE_HORZ),
		X_ATOM(_NET_WM_ACTION_MAXIMIZE_VERT),
		X_ATOM(_NET_WM_ACTION_FULLSCREEN),
		X_ATOM(_NET_WM_ACTION_CHANGE_DESKTOP),
		X_ATOM(_NET_WM_ACTION_CLOSE),
		// nelements reduced to omit this if not possible:
		X_ATOM(_NET_WM_ACTION_RESIZE),
	};
	int nelements = sizeof(allowed_actions) / sizeof(Atom);
	// Omit resize element if resizing not possible:
	if (c->max_width && c->max_width == c->min_width
			&& c->max_height && c->max_height == c->min_height)
		nelements--;
	XChangeProperty(display.dpy, c->window, X_ATOM(_NET_WM_ALLOWED_ACTIONS),
			XA_ATOM, 32, PropModeReplace,
			(unsigned char *)&allowed_actions,
			nelements);
}

// When window manager is shutting down, the _NET_WM_ALLOWED_ACTIONS property
// is removed from all clients (as no WM now exists to service these actions).

void ewmh_remove_allowed_actions(struct client *c) {
	XDeleteProperty(display.dpy, c->window, X_ATOM(_NET_WM_ALLOWED_ACTIONS));
}

// These properties are removed when a window is "withdrawn".

void ewmh_withdraw_client(struct client *c) {
	XDeleteProperty(display.dpy, c->window, X_ATOM(_NET_WM_DESKTOP));
	XDeleteProperty(display.dpy, c->window, X_ATOM(_NET_WM_STATE));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Update _NET_WM_DESKTOP to reflect virtual desktop of client (including
// fixed, 0xffffffff).

void ewmh_set_net_wm_desktop(struct client *c) {
	unsigned long vdesk = c->vdesk;
	XChangeProperty(display.dpy, c->window, X_ATOM(_NET_WM_DESKTOP),
			XA_CARDINAL, 32, PropModeReplace,
			(unsigned char *)&vdesk, 1);
}

// Check _NET_WM_WINDOW_TYPE property and build a bitmask of EWMH_WINDOW_TYPE_*

unsigned ewmh_get_net_wm_window_type(Window w) {
	Atom *aprop;
	unsigned long nitems, i;
	unsigned type = 0;
	if ( (aprop = get_property(w, X_ATOM(_NET_WM_WINDOW_TYPE), XA_ATOM, &nitems)) ) {
		for (i = 0; i < nitems; i++) {
			if (aprop[i] == X_ATOM(_NET_WM_WINDOW_TYPE_DESKTOP))
				type |= EWMH_WINDOW_TYPE_DESKTOP;
			if (aprop[i] == X_ATOM(_NET_WM_WINDOW_TYPE_DOCK))
				type |= EWMH_WINDOW_TYPE_DOCK;
			if (aprop[i] == X_ATOM(_NET_WM_WINDOW_TYPE_NOTIFICATION))
				type |= EWMH_WINDOW_TYPE_NOTIFICATION;
		}
		XFree(aprop);
	}
	return type;
}

// Update _NET_WM_STATE_* properties on a window.  Also updates
// _NET_ACTIVE_WINDOW on the client's screen if necessary.

void ewmh_set_net_wm_state(struct client *c) {
	Atom state[4];
	int i = 0;
	if (c->oldh)
		state[i++] = X_ATOM(_NET_WM_STATE_MAXIMIZED_VERT);
	if (c->oldw)
		state[i++] = X_ATOM(_NET_WM_STATE_MAXIMIZED_HORZ);
	if (c->oldh && c->oldw)
		state[i++] = X_ATOM(_NET_WM_STATE_FULLSCREEN);
	if (c == current) {
		state[i++] = X_ATOM(_NET_WM_STATE_FOCUSED);
		if (c->screen->active != c->window) {
			XChangeProperty(display.dpy, c->screen->root,
			                X_ATOM(_NET_ACTIVE_WINDOW),
			                XA_WINDOW, 32, PropModeReplace,
			                (unsigned char *)&c->window, 1);
			c->screen->active = c->window;
		}
	} else if (c->screen->active == c->window) {
		Window w = None;
		XChangeProperty(display.dpy, c->screen->root, X_ATOM(_NET_ACTIVE_WINDOW),
		                XA_WINDOW, 32, PropModeReplace,
		                (unsigned char *)&w, 1);
		c->screen->active = None;
	}
	XChangeProperty(display.dpy, c->window, X_ATOM(_NET_WM_STATE),
			XA_ATOM, 32, PropModeReplace,
			(unsigned char *)&state, i);
}

// When we receive _NET_REQUEST_FRAME_EXTENTS from an unmapped window, we are
// to set _NET_FRAME_EXTENTS on that window even before it becomes managed.
// TODO: set this property on a client once the border size for it is known
// during client_manage_new().

void ewmh_set_net_frame_extents(Window w, unsigned long border) {
	unsigned long extents[4];
	extents[0] = extents[1] = extents[2] = extents[3] = border;
	XChangeProperty(display.dpy, w, X_ATOM(_NET_FRAME_EXTENTS),
			XA_CARDINAL, 32, PropModeReplace,
			(unsigned char *)&extents, 4);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Allocate/resize an array suitable to hold all client window ids.
//
// XXX should test that this can be allocated before we commit to managing a
// window, in the same way that we test the client structure allocation.

static Window *alloc_window_array(void) {
	unsigned count = 0;
	for (struct list *iter = clients_mapping_order; iter; iter = iter->next) {
		count++;
	}
	if (count == 0) count++;
	// Round up to next block of 128
	count = (count + 127) & ~127;
	window_array = realloc(window_array, count * sizeof(Window));
	return window_array;
}
