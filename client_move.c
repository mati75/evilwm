/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Client management: user window manipulation

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/X.h>
#include <X11/Xlib.h>

#include "bind.h"
#include "client.h"
#include "display.h"
#include "evilwm.h"
#include "ewmh.h"
#include "list.h"
#include "screen.h"
#include "util.h"

#define SPACE 3

// Use the inverting graphics context to draw an outline for the client.
// Drawing it a second time will erase it.  If INFOBANNER_MOVERESIZE is
// defined, the information window is shown for the duration (but this can be
// slow on old X servers).

static void draw_outline(struct client *c) {
#ifndef INFOBANNER_MOVERESIZE
	char buf[27];
	int width_inc = c->width_inc, height_inc = c->height_inc;
#endif

	XDrawRectangle(display.dpy, c->screen->root, c->screen->invert_gc,
		c->x - c->border, c->y - c->border,
		c->width + 2*c->border-1, c->height + 2*c->border-1);

#ifndef INFOBANNER_MOVERESIZE
	snprintf(buf, sizeof(buf), "%dx%d+%d+%d", (c->width-c->base_width)/width_inc,
			(c->height-c->base_height)/height_inc, c->x, c->y);
	XDrawString(display.dpy, c->screen->root, c->screen->invert_gc,
		c->x + c->width - XTextWidth(display.font, buf, strlen(buf)) - SPACE,
		c->y + c->height - SPACE,
		buf, strlen(buf));
#endif
}

static int absmin(int a, int b) {
	if (abs(a) < abs(b))
		return a;
	return b;
}

// Snap a client to the edges of other clients (if on same screen, and visible)
// or to the screen border.

static void snap_client(struct client *c, struct monitor *monitor) {
	int dx, dy;
	int dpy_width = monitor->width;
	int dpy_height = monitor->height;

	// Snap to other windows

	dx = dy = option.snap;
	for (struct list *iter = clients_tab_order; iter; iter = iter->next) {
		struct client *ci = iter->data;
		if (ci == c)
			continue;
		if (ci->screen != c->screen)
			continue;
		if (!is_fixed(ci) && ci->vdesk != c->screen->vdesk)
			continue;
		if (ci->is_dock && !c->screen->docks_visible)
			continue;
		if (ci->y - ci->border - c->border - c->height - c->y <= option.snap && c->y - c->border - ci->border - ci->height - ci->y <= option.snap) {
			dx = absmin(dx, ci->x + ci->width - c->x + c->border + ci->border);
			dx = absmin(dx, ci->x + ci->width - c->x - c->width);
			dx = absmin(dx, ci->x - c->x - c->width - c->border - ci->border);
			dx = absmin(dx, ci->x - c->x);
		}
		if (ci->x - ci->border - c->border - c->width - c->x <= option.snap && c->x - c->border - ci->border - ci->width - ci->x <= option.snap) {
			dy = absmin(dy, ci->y + ci->height - c->y + c->border + ci->border);
			dy = absmin(dy, ci->y + ci->height - c->y - c->height);
			dy = absmin(dy, ci->y - c->y - c->height - c->border - ci->border);
			dy = absmin(dy, ci->y - c->y);
		}
	}
	if (abs(dx) < option.snap)
		c->x += dx;
	if (abs(dy) < option.snap)
		c->y += dy;

	// Snap to screen border

	if (abs(c->x - c->border - monitor->x) < option.snap)
		c->x = monitor->x + c->border;
	if (abs(c->y - c->border - monitor->y) < option.snap)
		c->y = monitor->y + c->border;
	if (abs(c->x + c->width + c->border - monitor->x - dpy_width) < option.snap)
		c->x = monitor->x + dpy_width - c->width - c->border;
	if (abs(c->y + c->height + c->border - monitor->y - dpy_height) < option.snap)
		c->y = monitor->y + dpy_height - c->height - c->border;

	if (abs(c->x) == monitor->x + c->border && c->width == dpy_width)
		c->x = monitor->x;
	if (abs(c->y) == monitor->y + c->border && c->height == dpy_height)
		c->y = monitor->y;
}

// During a sweep (resize interaction), recalculate new dimensions for a window
// based on mouse position relative to top-left corner.

static void recalculate_sweep(struct client *c, int x1, int y1, int x2, int y2, _Bool force) {
	if (force || c->oldw == 0) {
		c->oldw = 0;
		c->width = abs(x1 - x2);
		c->width -= (c->width - c->base_width) % c->width_inc;
		if (c->min_width && c->width < c->min_width)
			c->width = c->min_width;
		if (c->max_width && c->width > c->max_width)
			c->width = c->max_width;
		c->x = (x1 <= x2) ? x1 : x1 - c->width;
	}
	if (force || c->oldh == 0)  {
		c->oldh = 0;
		c->height = abs(y1 - y2);
		c->height -= (c->height - c->base_height) % c->height_inc;
		if (c->min_height && c->height < c->min_height)
			c->height = c->min_height;
		if (c->max_height && c->height > c->max_height)
			c->height = c->max_height;
		c->y = (y1 <= y2) ? y1 : y1 - c->height;
	}
}

// Handle user resizing a window with the mouse.  Takes over processing X
// motion events until the mouse button is released.
//
// Note that because of the way this draws an outline, other events are blocked
// until the mouse is moved.  TODO: consider using a SHAPEd window for this,
// where available.

void client_resize_sweep(struct client *c, unsigned button) {
	// Ensure we can grab pointer events.
	if (!grab_pointer(c->screen->root, display.resize_curs))
		return;

	// Sweeping always raises.
	client_raise(c);

	int old_cx = c->x;
	int old_cy = c->y;

#ifdef INFOBANNER_MOVERESIZE
	create_info_window(c);
#endif
	XGrabServer(display.dpy);
	draw_outline(c);  // draw

	// Warp pointer to the bottom-right of the client for resizing
	setmouse(c->window, c->width, c->height);

	for (;;) {
		XEvent ev;
		XMaskEvent(display.dpy, ButtonPressMask|ButtonReleaseMask|PointerMotionMask, &ev);
		switch (ev.type) {
			case MotionNotify:
				if (ev.xmotion.root != c->screen->root)
					break;
				draw_outline(c);  // erase
				XUngrabServer(display.dpy);
				recalculate_sweep(c, old_cx, old_cy, ev.xmotion.x, ev.xmotion.y, ev.xmotion.state & altmask);
#ifdef INFOBANNER_MOVERESIZE
				update_info_window(c);
#endif
				XSync(display.dpy, False);
				XGrabServer(display.dpy);
				draw_outline(c);  // draw
				break;

			case ButtonRelease:
				if (ev.xbutton.button != button)
					continue;
				draw_outline(c);  // erase
				XUngrabServer(display.dpy);
#ifdef INFOBANNER_MOVERESIZE
				remove_info_window();
#endif
				XUngrabPointer(display.dpy, CurrentTime);
				client_moveresizeraise(c);
				// In case maximise state has changed:
				ewmh_set_net_wm_state(c);
				return;

			default:
				break;
		}
	}
}

// Handle user moving a window with the mouse.  Takes over processing X motion
// events until the mouse button is released.
//
// If solid drag is disabled, an outline is drawn, which leads to the same
// limitations as in the sweep() function.

void client_move_drag(struct client *c, unsigned button) {
	// Ensure we can grab pointer events.
	if (!grab_pointer(c->screen->root, display.move_curs))
		return;

	// Dragging always raises.
	client_raise(c);

	// Initial pointer and window positions; new coordinates calculated
	// relative to these.
	int x1, y1;
	int old_cx = c->x;
	int old_cy = c->y;
	get_pointer_root_xy(c->screen->root, &x1, &y1);

	struct monitor *monitor = client_monitor(c, NULL);

#ifdef INFOBANNER_MOVERESIZE
	create_info_window(c);
#endif
	if (option.no_solid_drag) {
		XGrabServer(display.dpy);
		draw_outline(c);  // draw
	}

	for (;;) {
		XEvent ev;
		XMaskEvent(display.dpy, ButtonPressMask|ButtonReleaseMask|PointerMotionMask, &ev);
		switch (ev.type) {
			case MotionNotify:
				if (ev.xmotion.root != c->screen->root)
					break;
				if (option.no_solid_drag) {
					draw_outline(c);  // erase
					XUngrabServer(display.dpy);
				}
				c->x = old_cx + (ev.xmotion.x - x1);
				c->y = old_cy + (ev.xmotion.y - y1);
				if (option.snap && !(ev.xmotion.state & altmask))
					snap_client(c, monitor);

#ifdef INFOBANNER_MOVERESIZE
				update_info_window(c);
#endif
				if (option.no_solid_drag) {
					XSync(display.dpy, False);
					XGrabServer(display.dpy);
					draw_outline(c);  // draw
				} else {
					XMoveWindow(display.dpy, c->parent,
							c->x - c->border,
							c->y - c->border);
					send_config(c);
				}
				break;

			case ButtonRelease:
				if (ev.xbutton.button != button)
					continue;
				if (option.no_solid_drag) {
					draw_outline(c);  // erase
					XUngrabServer(display.dpy);
				}
#ifdef INFOBANNER_MOVERESIZE
				remove_info_window();
#endif
				XUngrabPointer(display.dpy, CurrentTime);
				if (option.no_solid_drag) {
					// For solid drags, the client was
					// moved with the mouse.  For non-solid
					// drags, we need a final move/raise:
					client_moveresizeraise(c);
				}
				return;

			default:
				break;
		}
	}
}

// Predicate function for use with XCheckIfEvent.
//
// This is used to detect when a keyrelease is followed by a keypress with the
// same keycode and timestamp, indicating autorepeat.

static Bool predicate_keyrepeatpress(Display *dummy, XEvent *ev, XPointer arg) {
	(void)dummy;
	XEvent *release_event = (XEvent *)arg;
	if (ev->type != KeyPress)
		return False;
	if (release_event->xkey.keycode != ev->xkey.keycode)
		return False;
	return release_event->xkey.time == ev->xkey.time;
}

// Show information window until the key used to activate (keycode) is
// released.
//
// This routine used to disable autorepeat for the duration, but modern X
// servers seem to only change the keyboard control after all keys have been
// physically released, which is not so useful here.  Instead, we detect when a
// key release is followed by a key press with the same code and timestamp,
// which indicates autorepeat.

void client_show_info(struct client *c, XEvent *e) {
	unsigned input;

	if (e->type == KeyPress) {
		input = e->xkey.keycode;
		if (XGrabKeyboard(display.dpy, c->screen->root, False, GrabModeAsync, GrabModeAsync, CurrentTime) != GrabSuccess)
			return;
	} else {
		input = e->xbutton.button;
		if (!grab_pointer(c->screen->root, None))
			return;
	}

#ifdef INFOBANNER
	create_info_window(c);
#else
	XGrabServer(display.dpy);
	draw_outline(c);
#endif

	for (;;) {
		XEvent ev;
		if (e->type == KeyPress) {
			XMaskEvent(display.dpy, KeyReleaseMask, &ev);
			if (ev.xkey.keycode != input)
				continue;
			if (XCheckIfEvent(display.dpy, &ev, predicate_keyrepeatpress, (XPointer)&ev)) {
				// Autorepeat keypress detected - ignore!
				continue;
			}
		} else {
			XMaskEvent(display.dpy, ButtonReleaseMask, &ev);
			if (ev.xbutton.button != input)
				continue;
		}
		break;
	}

#ifdef INFOBANNER
	remove_info_window();
#else
	draw_outline(c);
	XUngrabServer(display.dpy);
#endif

	if (e->type == KeyPress) {
		XUngrabKeyboard(display.dpy, CurrentTime);
	} else {
		XUngrabPointer(display.dpy, CurrentTime);
	}
}

// Move window to (potentially updated) client coordinates.

void client_moveresize(struct client *c) {
	XMoveResizeWindow(display.dpy, c->parent, c->x - c->border, c->y - c->border,
			c->width, c->height);
	XMoveResizeWindow(display.dpy, c->window, 0, 0, c->width, c->height);
	send_config(c);
}

// Same, but raise the client first.

void client_moveresizeraise(struct client *c) {
	client_raise(c);
	client_moveresize(c);
}

// Maximise (or de-maximise) horizontally, vertically, or both.
//
// Updates EWMH properties, but also stores old dimensions in evilwm-specific
// properties so that they persist across window manager restarts.

void client_maximise(struct client *c, int action, int hv) {
	int monitor_x, monitor_y;
	int monitor_width, monitor_height;

	// Maximising to monitor or screen?
	if (hv & MAXIMISE_SCREEN) {
		monitor_x = monitor_y = 0;
		monitor_width = DisplayWidth(display.dpy, c->screen->screen);
		monitor_height = DisplayHeight(display.dpy, c->screen->screen);
	} else {
		struct monitor *monitor = client_monitor(c, NULL);
		monitor_x = monitor->x;
		monitor_y = monitor->y;
		monitor_width = monitor->width;
		monitor_height = monitor->height;
	}

	if (hv & MAXIMISE_HORZ) {
		if (c->oldw) {
			if (action == NET_WM_STATE_REMOVE || action == NET_WM_STATE_TOGGLE) {
				c->x = c->oldx;
				c->width = c->oldw;
				c->oldw = 0;
				XDeleteProperty(display.dpy, c->window, X_ATOM(_EVILWM_UNMAXIMISED_HORZ));
			}
		} else {
			if (action == NET_WM_STATE_ADD || action == NET_WM_STATE_TOGGLE) {
				unsigned long props[2];
				c->oldx = c->x;
				c->oldw = c->width;
				c->x = monitor_x;
				c->width = monitor_width;
				props[0] = c->oldx;
				props[1] = c->oldw;
				XChangeProperty(display.dpy, c->window, X_ATOM(_EVILWM_UNMAXIMISED_HORZ),
						XA_CARDINAL, 32, PropModeReplace,
						(unsigned char *)&props, 2);
			}
		}
	}
	if (hv & MAXIMISE_VERT) {
		if (c->oldh) {
			if (action == NET_WM_STATE_REMOVE || action == NET_WM_STATE_TOGGLE) {
				c->y = c->oldy;
				c->height = c->oldh;
				c->oldh = 0;
				XDeleteProperty(display.dpy, c->window, X_ATOM(_EVILWM_UNMAXIMISED_VERT));
			}
		} else {
			if (action == NET_WM_STATE_ADD || action == NET_WM_STATE_TOGGLE) {
				unsigned long props[2];
				c->oldy = c->y;
				c->oldh = c->height;
				c->y = monitor_y;
				c->height = monitor_height;
				props[0] = c->oldy;
				props[1] = c->oldh;
				XChangeProperty(display.dpy, c->window, X_ATOM(_EVILWM_UNMAXIMISED_VERT),
						XA_CARDINAL, 32, PropModeReplace,
						(unsigned char *)&props, 2);
			}
		}
	}
	_Bool change_border = 0;
	if (c->oldw && c->oldh) {
		// maximised - remove border
		if (c->border) {
			c->border = 0;
			change_border = 1;
		}
	} else {
		// not maximised - add border
		if (!c->border && c->normal_border) {
			c->border = c->normal_border;
			change_border = 1;
		}
	}
	if (change_border) {
		XSetWindowBorderWidth(display.dpy, c->parent, c->border);
		ewmh_set_net_frame_extents(c->window, c->border);
	}
	ewmh_set_net_wm_state(c);
	client_moveresizeraise(c);
	discard_enter_events(c);
}

// Find and select the "next" client, relative to the currently selected one
// (basically, handle Alt+Tab).  Order is most-recently-used (maintained in the
// clients_tab_order list).

void client_select_next(void) {
	struct list *newl = list_find(clients_tab_order, current);
	struct client *newc;

	do {
		if (newl) {
			newl = newl->next;
			if (!newl && !current)
				return;
		}
		if (!newl)
			newl = clients_tab_order;
		if (!newl)
			return;
		newc = newl->data;
		if (newc == current)
			return;
	} while ((!is_fixed(newc) && (newc->vdesk != newc->screen->vdesk))
		 || (newc->is_dock && !newc->screen->docks_visible));

	if (!newc)
		return;

	client_show(newc);  // XXX why would it be hidden?
	client_raise(newc);
	select_client(newc);

	// Optionally force the pointer to jump to the newly-selected window.
	// I think this was the default behaviour in much earlier versions of
	// evilwm (possibly to generate the enter event and handle selecting
	// the client as a result of that), but I don't like it now.

#ifdef WARP_POINTER
	setmouse(newc->window, newc->width + newc->border - 1,
			newc->height + newc->border - 1);
#endif

	discard_enter_events(newc);
}
