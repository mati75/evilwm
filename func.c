/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Bindable functions

// All functions present the same call interface, so they can be mapped
// reasonably arbitrarily to user inputs.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "client.h"
#include "display.h"
#include "evilwm.h"
#include "func.h"
#include "list.h"
#include "log.h"
#include "screen.h"
#include "util.h"

static void do_client_move(struct client *c) {
	if (abs(c->x) == c->border && c->oldw != 0)
		c->x = 0;
	if (abs(c->y) == c->border && c->oldh != 0)
		c->y = 0;
	if (c->width < c->min_width) {
		c->width = c->min_width;
	}
	if (c->max_width && c->width > c->max_width) {
		c->width = c->max_width;
	}
	if (c->height < c->min_height) {
		c->height = c->min_height;
	}
	if (c->max_height && c->height > c->max_height) {
		c->height = c->max_height;
	}
	client_moveresizeraise(c);
#ifdef WARP_POINTER
	setmouse(c->window, c->width + c->border - 1, c->height + c->border - 1);
#endif
	discard_enter_events(c);
}

void func_delete(void *sptr, XEvent *e, unsigned flags) {
	if (!(flags & FL_CLIENT))
		return;
	struct client *c = sptr;
	(void)e;
	send_wm_delete(c, flags & FL_VALUEMASK);
}

void func_dock(void *sptr, XEvent *e, unsigned flags) {
	if (!(flags & FL_SCREEN))
		return;
	struct screen *current_screen = sptr;
	(void)e;
	if (flags & FL_TOGGLE) {
		set_docks_visible(current_screen, !current_screen->docks_visible);
	}
}

void func_info(void *sptr, XEvent *e, unsigned flags) {
	if (!(flags & FL_CLIENT))
		return;
	struct client *c = sptr;
	client_show_info(c, e);
}

void func_lower(void *sptr, XEvent *e, unsigned flags) {
	struct client *c = sptr;
	if (!(flags & FL_CLIENT) || !c)
		return;
	(void)e;
	client_lower(c);
}

void func_move(void *sptr, XEvent *e, unsigned flags) {
	struct client *c = sptr;
	if (!(flags & FL_CLIENT) || !c)
		return;

	struct monitor *monitor = client_monitor(c, NULL);
	int width_inc = (c->width_inc > 1) ? c->width_inc : 16;
	int height_inc = (c->height_inc > 1) ? c->height_inc : 16;

	if (e->type == ButtonPress) {
		XButtonEvent *xbutton = (XButtonEvent *)e;
		client_move_drag(c, xbutton->button);
		return;
	}

	if (flags & FL_RELATIVE) {
		if (flags & FL_RIGHT) {
			c->x += width_inc;
		}
		if (flags & FL_LEFT) {
			c->x -= width_inc;
		}
		if (flags & FL_DOWN) {
			c->y += height_inc;
		}
		if (flags & FL_UP) {
			c->y -= height_inc;
		}
	} else {
		if (flags & FL_RIGHT) {
			c->x = monitor->x + monitor->width - c->width-c->border;
		}
		if (flags & FL_LEFT) {
			c->x = monitor->x + c->border;
		}
		if (flags & FL_BOTTOM) {
			c->y = monitor->y + monitor->height - c->height-c->border;
		}
		if (flags & FL_TOP) {
			c->y = monitor->y + c->border;
		}
	}

	do_client_move(c);
}

void func_next(void *sptr, XEvent *e, unsigned flags) {
	(void)sptr;
	(void)flags;
	if (e->type != KeyPress)
		return;
	XKeyEvent *xkey = (XKeyEvent *)e;
	client_select_next();
	if (XGrabKeyboard(display.dpy, xkey->root, False, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess) {
		XEvent ev;
		do {
			XMaskEvent(display.dpy, KeyPressMask|KeyReleaseMask, &ev);
			if (ev.type == KeyPress && ev.xkey.keycode == xkey->keycode)
				client_select_next();
		} while (ev.type == KeyPress || ev.xkey.keycode == xkey->keycode);
		XUngrabKeyboard(display.dpy, CurrentTime);
	}
	clients_tab_order = list_to_head(clients_tab_order, current);
}

void func_raise(void *sptr, XEvent *e, unsigned flags) {
	struct client *c = sptr;
	if (!(flags & FL_CLIENT) || !c)
		return;
	(void)e;
	client_raise(c);
}

void func_resize(void *sptr, XEvent *e, unsigned flags) {
	struct client *c = sptr;
	if (!(flags & FL_CLIENT) || !c)
		return;

	int width_inc = (c->width_inc > 1) ? c->width_inc : 16;
	int height_inc = (c->height_inc > 1) ? c->height_inc : 16;

	if (e->type == ButtonPress) {
		XButtonEvent *xbutton = (XButtonEvent *)e;
		client_resize_sweep(c, xbutton->button);
		return;
	}

	if (flags & FL_RELATIVE) {
		if (flags & FL_RIGHT) {
			c->width += width_inc;
		}
		if (flags & FL_LEFT) {
			c->width -= width_inc;
		}
		if (flags & FL_DOWN) {
			c->height += height_inc;
		}
		if (flags & FL_UP) {
			c->height -= height_inc;
		}
	} else if (flags & FL_TOGGLE) {
		int hv = 0;
		if ((flags & FL_HORZ) == FL_HORZ) {
			hv |= MAXIMISE_HORZ;
		}
		if ((flags & FL_VERT) == FL_VERT) {
			hv |= MAXIMISE_VERT;
		}
		client_maximise(c, NET_WM_STATE_TOGGLE, hv);
		return;
	}

	do_client_move(c);
}

void func_spawn(void *sptr, XEvent *e, unsigned flags) {
	// TODO: configurable array of commands to run indexed by FL_VALUEMASK?
	(void)sptr;
	(void)e;
	(void)flags;
	spawn((const char *const *)option.term);
}

void func_vdesk(void *sptr, XEvent *e, unsigned flags) {
	(void)e;
	if (flags & FL_CLIENT) {
		struct client *c = sptr;
		if (flags & FL_TOGGLE) {
			if (is_fixed(c)) {
				client_to_vdesk(c, c->screen->vdesk);
			} else {
				client_to_vdesk(c, VDESK_FIXED);
			}
		}
	} else if (flags & FL_SCREEN) {
		struct screen *current_screen = sptr;
		if (flags & FL_TOGGLE) {
			switch_vdesk(current_screen, current_screen->old_vdesk);
		} else if (flags & FL_RELATIVE) {
			if (flags & FL_DOWN) {
				if (current_screen->vdesk > 0) {
					switch_vdesk(current_screen, current_screen->vdesk - 1);
				}
			}
			if (flags & FL_UP) {
				if (current_screen->vdesk < VDESK_MAX) {
					switch_vdesk(current_screen, current_screen->vdesk + 1);
				}
			}
		} else {
			int v = flags & FL_VALUEMASK;
			switch_vdesk(current_screen, v);
		}
	}
}
