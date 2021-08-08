/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2021 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// X11 event processing.  This is the core of the window manager and processes
// events from clients and user interaction in a loop until signalled to exit.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <X11/XKBlib.h>
#include <X11/keysymdef.h>

#ifdef SHAPE
#include <X11/extensions/shape.h>
#endif
#ifdef RANDR
#include <X11/extensions/Xrandr.h>
#endif

#include "client.h"
#include "display.h"
#include "events.h"
#include "evilwm.h"
#include "ewmh.h"
#include "keymap.h"
#include "list.h"
#include "log.h"
#include "screen.h"
#include "util.h"

// Event loop will run until this flag is set
int wm_exit;

// Flags that the client list should be scanned and marked clients removed.
// Set by unhandled X errors and unmap requests.
int need_client_tidy = 0;

// Process keyboard events.

static void handle_key_event(XKeyEvent *e) {
	KeySym key = XkbKeycodeToKeysym(display.dpy, e->keycode, 0, 0);
	struct screen *current_screen = find_current_screen();

	switch (key) {
		case KEY_NEW:
			spawn((const char *const *)option.term);
			break;
		case KEY_NEXT:
			client_select_next();
			if (XGrabKeyboard(display.dpy, e->root, False, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess) {
				XEvent ev;
				do {
					XMaskEvent(display.dpy, KeyPressMask|KeyReleaseMask, &ev);
					if (ev.type == KeyPress && XkbKeycodeToKeysym(display.dpy, ev.xkey.keycode, 0, 0) == KEY_NEXT)
						client_select_next();
				} while (ev.type == KeyPress || XkbKeycodeToKeysym(display.dpy, ev.xkey.keycode, 0, 0) == KEY_NEXT);
				XUngrabKeyboard(display.dpy, CurrentTime);
			}
			clients_tab_order = list_to_head(clients_tab_order, current);
			break;
		case KEY_DOCK_TOGGLE:
			set_docks_visible(current_screen, !current_screen->docks_visible);
			break;
		case XK_1: case XK_2: case XK_3: case XK_4:
		case XK_5: case XK_6: case XK_7: case XK_8:
			switch_vdesk(current_screen, KEY_TO_VDESK(key));
			break;
		case KEY_PREVDESK:
			if (current_screen->vdesk > 0) {
				switch_vdesk(current_screen,
						current_screen->vdesk - 1);
			}
			break;
		case KEY_NEXTDESK:
			if (current_screen->vdesk < VDESK_MAX) {
				switch_vdesk(current_screen,
						current_screen->vdesk + 1);
			}
			break;
		case KEY_TOGGLEDESK:
			switch_vdesk(current_screen, current_screen->old_vdesk);
			break;
		default:
			break;
	}

	struct client *c = current;
	if (c == NULL) return;

	struct monitor *monitor = client_monitor(c, NULL);
	int width_inc = (c->width_inc > 1) ? c->width_inc : 16;
	int height_inc = (c->height_inc > 1) ? c->height_inc : 16;

	switch (key) {
		case KEY_LEFT:
			if (e->state & altmask) {
				if ((c->width - width_inc) >= c->min_width)
					c->width -= width_inc;
			} else {
				c->x -= 16;
			}
			goto move_client;
		case KEY_DOWN:
			if (e->state & altmask) {
				if (!c->max_height || (c->height + height_inc) <= c->max_height)
					c->height += height_inc;
			} else {
				c->y += 16;
			}
			goto move_client;
		case KEY_UP:
			if (e->state & altmask) {
				if ((c->height - height_inc) >= c->min_height)
					c->height -= height_inc;
			} else {
				c->y -= 16;
			}
			goto move_client;
		case KEY_RIGHT:
			if (e->state & altmask) {
				if (!c->max_width || (c->width + width_inc) <= c->max_width)
					c->width += width_inc;
			} else {
				c->x += 16;
			}
			goto move_client;
		case KEY_TOPLEFT:
			c->x = monitor->x + c->border;
			c->y = monitor->y + c->border;
			goto move_client;
		case KEY_TOPRIGHT:
			c->x = monitor->x + monitor->width - c->width-c->border;
			c->y = monitor->y + c->border;
			goto move_client;
		case KEY_BOTTOMLEFT:
			c->x = monitor->x + c->border;
			c->y = monitor->y + monitor->height - c->height-c->border;
			goto move_client;
		case KEY_BOTTOMRIGHT:
			c->x = monitor->x + monitor->width - c->width-c->border;
			c->y = monitor->y + monitor->height - c->height-c->border;
			goto move_client;
		case KEY_KILL:
			send_wm_delete(c, e->state & altmask);
			break;
		case KEY_LOWER: case KEY_ALTLOWER:
			client_lower(c);
			break;
		case KEY_INFO:
			client_show_info(c, e->keycode);
			break;
		case KEY_MAX:
			client_maximise(c, NET_WM_STATE_TOGGLE, MAXIMISE_HORZ|MAXIMISE_VERT);
			break;
		case KEY_MAXVERT:
			if (e->state & altmask) {
				client_maximise(c, NET_WM_STATE_TOGGLE, MAXIMISE_HORZ);
			} else {
				client_maximise(c, NET_WM_STATE_TOGGLE, MAXIMISE_VERT);
			}
			break;
		case KEY_FIX:
			if (is_fixed(c)) {
				client_to_vdesk(c, current_screen->vdesk);
			} else {
				client_to_vdesk(c, VDESK_FIXED);
			}
			break;
		default:
			break;
	}
	return;

move_client:
	if (abs(c->x) == c->border && c->oldw != 0)
		c->x = 0;
	if (abs(c->y) == c->border && c->oldh != 0)
		c->y = 0;
	client_moveresizeraise(c);
#ifdef WARP_POINTER
	setmouse(c->window, c->width + c->border - 1, c->height + c->border - 1);
#endif
	discard_enter_events(c);
	return;
}

// Handle mousebutton events.

static void handle_button_event(XButtonEvent *e) {
	struct client *c = find_client(e->window);

	if (c) {
		switch (e->button) {
			case Button1:
				client_move_drag(c, e->button);
				break;
			case Button2:
				client_resize_sweep(c, e->button);
				break;
			case Button3:
				client_lower(c);
				break;
			default:
				break;
		}
	}
}

// Apply the changes from an XWindowChanges struct to a client.

static void do_window_changes(int value_mask, XWindowChanges *wc, struct client *c,
		int gravity) {
	LOG_XENTER("do_window_changes(window=%lx), was: %dx%d+%d+%d", (unsigned long)c->window, c->width, c->height, c->x, c->y);
	if (gravity == 0)
		gravity = c->win_gravity_hint;
	c->win_gravity = gravity;
	if (value_mask & CWX) {
		c->x = wc->x;
		LOG_XDEBUG("CWX      x=%d\n", wc->x);
	}
	if (value_mask & CWY) {
		c->y = wc->y;
		LOG_XDEBUG("CWY      y=%d\n", wc->y);
	}
	if (value_mask & (CWWidth|CWHeight)) {
		int dw = 0, dh = 0;
		if (!(value_mask & (CWX|CWY))) {
			client_gravitate(c, -c->border);
		}
		if (value_mask & CWWidth) {
			LOG_XDEBUG("CWWidth  width=%d\n", wc->width);
			int neww = wc->width;
			if (neww < c->min_width)
				neww = c->min_width;
			if (c->max_width && neww > c->max_width)
				neww = c->max_width;
			dw = neww - c->width;
			c->width = neww;
		}
		if (value_mask & CWHeight) {
			LOG_XDEBUG("CWHeight height=%d\n", wc->height);
			int newh = wc->height;
			if (newh < c->min_height)
				newh = c->min_height;
			if (c->max_height && newh > c->max_height)
				newh = c->max_height;
			dh = newh - c->height;
			c->height = newh;
		}

		// only apply position fixes if not being explicitly moved
		if (!(value_mask & (CWX|CWY))) {
			switch (gravity) {
			default:
			case NorthWestGravity:
				break;
			case NorthGravity:
				c->x -= (dw / 2);
				break;
			case NorthEastGravity:
				c->x -= dw;
				break;
			case WestGravity:
				c->y -= (dh / 2);
				break;
			case CenterGravity:
				c->x -= (dw / 2);
				c->y -= (dh / 2);
				break;
			case EastGravity:
				c->x -= dw;
				c->y -= (dh / 2);
				break;
			case SouthWestGravity:
				c->y -= dh;
				break;
			case SouthGravity:
				c->x -= (dw / 2);
				c->y -= dh;
				break;
			case SouthEastGravity:
				c->x -= dw;
				c->y -= dh;
				break;
			}
			value_mask |= CWX|CWY;
			client_gravitate(c, c->border);
		}
	}

	wc->x = c->x - c->border;
	wc->y = c->y - c->border;
	wc->border_width = c->border;
	XConfigureWindow(display.dpy, c->parent, value_mask, wc);
	XMoveResizeWindow(display.dpy, c->window, 0, 0, c->width, c->height);
	if ((value_mask & (CWX|CWY)) && !(value_mask & (CWWidth|CWHeight))) {
		send_config(c);
	}
	LOG_XLEAVE();
}

static void handle_configure_request(XConfigureRequestEvent *e) {
	struct client *c = find_client(e->window);

	XWindowChanges wc;
	wc.x = e->x;
	wc.y = e->y;
	wc.width = e->width;
	wc.height = e->height;
	wc.border_width = 0;
	wc.sibling = e->above;
	wc.stack_mode = e->detail;

	if (c) {
		if (e->value_mask & CWStackMode && e->value_mask & CWSibling) {
			struct client *sibling = find_client(e->above);
			if (sibling) {
				wc.sibling = sibling->parent;
			}
		}
		do_window_changes(e->value_mask, &wc, c, 0);
		if (c == current) {
			discard_enter_events(c);
		}
	} else {
		LOG_XENTER("XConfigureWindow(window=%lx, value_mask=%lx)", (unsigned long)e->window, e->value_mask);
		XConfigureWindow(display.dpy, e->window, e->value_mask, &wc);
		LOG_XLEAVE();
	}
}

static void handle_map_request(XMapRequestEvent *e) {
	struct client *c = find_client(e->window);

	LOG_ENTER("handle_map_request(window=%lx)", (unsigned long)e->window);
	if (c) {
		if (!is_fixed(c) && c->vdesk != c->screen->vdesk)
			switch_vdesk(c->screen, c->vdesk);
		client_show(c);
		client_raise(c);
	} else {
		XWindowAttributes attr;
		XGetWindowAttributes(display.dpy, e->window, &attr);
		client_manage_new(e->window, find_screen(attr.root));
	}
	LOG_LEAVE();
}

static void handle_unmap_event(XUnmapEvent *e) {
	struct client *c = find_client(e->window);

	LOG_ENTER("handle_unmap_event(window=%lx)", (unsigned long)e->window);
	if (c) {
		if (c->ignore_unmap) {
			c->ignore_unmap--;
			LOG_DEBUG("ignored (%d ignores remaining)\n", c->ignore_unmap);
		} else {
			LOG_DEBUG("flagging client for removal\n");
			c->remove = 1;
			need_client_tidy = 1;
		}
	} else {
		LOG_DEBUG("unknown client!\n");
	}
	LOG_LEAVE();
}

static void handle_colormap_change(XColormapEvent *e) {
	struct client *c = find_client(e->window);

	if (c && e->new) {
		c->cmap = e->colormap;
		XInstallColormap(display.dpy, c->cmap);
	}
}

static void handle_property_change(XPropertyEvent *e) {
	struct client *c = find_client(e->window);

	if (c) {
		LOG_ENTER("handle_property_change(window=%lx, atom=%s)", (unsigned long)e->window, debug_atom_name(e->atom));
		if (e->atom == XA_WM_NORMAL_HINTS) {
			get_wm_normal_hints(c);
			LOG_DEBUG("geometry=%dx%d+%d+%d\n", c->width, c->height, c->x, c->y);
		} else if (e->atom == X_ATOM(_NET_WM_WINDOW_TYPE)) {
			get_window_type(c);
			if (!c->is_dock && (is_fixed(c) || (c->vdesk == c->screen->vdesk))) {
				client_show(c);
			}
		}
		LOG_LEAVE();
	}
}

static void handle_enter_event(XCrossingEvent *e) {
	struct client *c;

	if ((c = find_client(e->window))) {
		if (!is_fixed(c) && c->vdesk != c->screen->vdesk)
			return;
		select_client(c);
		clients_tab_order = list_to_head(clients_tab_order, c);
	}
}

static void handle_mappingnotify_event(XMappingEvent *e) {
	XRefreshKeyboardMapping(e);
	if (e->request == MappingKeyboard) {
		int i;
		for (i = 0; i < display.nscreens; i++) {
			grab_keys_for_screen(&display.screens[i]);
		}
	}
}

#ifdef SHAPE
static void handle_shape_event(XShapeEvent *e) {
	struct client *c = find_client(e->window);
	if (c)
		set_shape(c);
}
#endif

#ifdef RANDR
static void handle_randr_event(XRRScreenChangeNotifyEvent *e) {
	struct screen *s = find_screen(e->root);
	// Record geometries of clients relative to monitor
	scan_clients_before_resize(s);
	// Update Xlib's idea of screen size
	XRRUpdateConfiguration((XEvent*)e);
	// Scan new monitor list
	screen_probe_monitors(s);
	// Fix any clients that are now not visible on any monitor.  Also
	// adjusts maximised geometries where appropriate.
	fix_screen_after_resize(s);
	// Update various EWMH properties that reflect screen geometry
	ewmh_set_screen_workarea(s);
}
#endif

// Events sent to clients

static void handle_client_message(XClientMessageEvent *e) {
	struct screen *s = find_current_screen();
	struct client *c;

	LOG_ENTER("handle_client_message(window=%lx, format=%d, type=%s)", (unsigned long)e->window, e->format, debug_atom_name(e->message_type));

	if (e->message_type == X_ATOM(_NET_CURRENT_DESKTOP)) {
		switch_vdesk(s, e->data.l[0]);
		LOG_LEAVE();
		return;
	}

	c = find_client(e->window);
	if (!c) {

		// _NET_REQUEST_FRAME_EXTENTS is intended to be sent from
		// unmapped windows.  The reply is just an _estimate_ of the
		// border widths, so for the moment, just guess that it'll be
		// the default (it may not be if MWM flags suggest no border).
		//
		// XXX: would a client expect a reply if this was sent while
		// mapped?
		//
		// TODO: calculate this according to the logic in
		// client_manage_new() instead of offering up this "estimate".

		if (e->message_type == X_ATOM(_NET_REQUEST_FRAME_EXTENTS)) {
			ewmh_set_net_frame_extents(e->window, option.bw);
		}

		LOG_LEAVE();
		return;
	}

	if (e->message_type == X_ATOM(_NET_ACTIVE_WINDOW)) {
		// Only do this if it came from direct user action
		if (e->data.l[0] == 2) {
			if (c->screen == s)
				select_client(c);
		}
		LOG_LEAVE();
		return;
	}

	if (e->message_type == X_ATOM(_NET_CLOSE_WINDOW)) {
		// Only do this if it came from direct user action
		if (e->data.l[1] == 2) {
			send_wm_delete(c, 0);
		}
		LOG_LEAVE();
		return;
	}

	if (e->message_type == X_ATOM(_NET_MOVERESIZE_WINDOW)) {
		// Only do this if it came from direct user action
		int source_indication = (e->data.l[0] >> 12) & 3;
		if (source_indication == 2) {
			int value_mask = (e->data.l[0] >> 8) & 0x0f;
			int gravity = e->data.l[0] & 0xff;
			XWindowChanges wc;

			wc.x = e->data.l[1];
			wc.y = e->data.l[2];
			wc.width = e->data.l[3];
			wc.height = e->data.l[4];
			do_window_changes(value_mask, &wc, c, gravity);
		}
		LOG_LEAVE();
		return;
	}

	if (e->message_type == X_ATOM(_NET_RESTACK_WINDOW)) {
		// Only do this if it came from direct user action
		if (e->data.l[0] == 2) {
			XWindowChanges wc;

			wc.sibling = e->data.l[1];
			wc.stack_mode = e->data.l[2];
			do_window_changes(CWSibling | CWStackMode, &wc, c, c->win_gravity);
		}
		LOG_LEAVE();
		return;
	}

	if (e->message_type == X_ATOM(_NET_WM_DESKTOP)) {
		// Only do this if it came from direct user action
		if (e->data.l[1] == 2) {
			client_to_vdesk(c, e->data.l[0]);
		}
		LOG_LEAVE();
		return;
	}

	if (e->message_type == X_ATOM(_NET_WM_STATE)) {
		int i, maximise_hv = 0;
		// Message can contain up to two state changes:
		for (i = 1; i <= 2; i++) {
			if ((Atom)e->data.l[i] == X_ATOM(_NET_WM_STATE_MAXIMIZED_VERT)) {
				maximise_hv |= MAXIMISE_VERT;
			} else if ((Atom)e->data.l[i] == X_ATOM(_NET_WM_STATE_MAXIMIZED_HORZ)) {
				maximise_hv |= MAXIMISE_HORZ;
			} else if ((Atom)e->data.l[i] == X_ATOM(_NET_WM_STATE_FULLSCREEN)) {
				maximise_hv |= MAXIMISE_VERT|MAXIMISE_HORZ;
			}
		}
		if (maximise_hv) {
			client_maximise(c, e->data.l[0], maximise_hv);
		}
		LOG_LEAVE();
		return;
	}

	LOG_LEAVE();
}

// Run the main event loop.  This will run until something tells us to quit
// (generally, a signal).

void event_main_loop(void) {
	// XEvent is a big union of all the core event types, but we also need
	// to handle events about extensions, so make a union of the union...
	union {
		XEvent xevent;
#ifdef SHAPE
		XShapeEvent xshape;
#endif
#ifdef RANDR
		XRRScreenChangeNotifyEvent xrandr;
#endif
	} ev;

	// Main event loop
	while (!wm_exit) {
		if (interruptibleXNextEvent(&ev.xevent)) {
			switch (ev.xevent.type) {
			case KeyPress:
				handle_key_event(&ev.xevent.xkey);
				break;
			case ButtonPress:
				handle_button_event(&ev.xevent.xbutton);
				break;
			case ConfigureRequest:
				handle_configure_request(&ev.xevent.xconfigurerequest);
				break;
			case MapRequest:
				handle_map_request(&ev.xevent.xmaprequest);
				break;
			case ColormapNotify:
				handle_colormap_change(&ev.xevent.xcolormap);
				break;
			case EnterNotify:
				handle_enter_event(&ev.xevent.xcrossing);
				break;
			case PropertyNotify:
				handle_property_change(&ev.xevent.xproperty);
				break;
			case UnmapNotify:
				handle_unmap_event(&ev.xevent.xunmap);
				break;
			case MappingNotify:
				handle_mappingnotify_event(&ev.xevent.xmapping);
				break;
			case ClientMessage:
				handle_client_message(&ev.xevent.xclient);
				break;
			default:
#ifdef SHAPE
				if (display.have_shape
				    && ev.xevent.type == display.shape_event) {
					handle_shape_event(&ev.xshape);
				}
#endif
#ifdef RANDR
				if (display.have_randr && ev.xevent.type == display.randr_event_base + RRScreenChangeNotify) {
					handle_randr_event(&ev.xrandr);
				}
#endif
				break;
			}
		}

		// Scan list for clients flagged to be removed
		if (need_client_tidy) {
			struct list *iter, *niter;
			need_client_tidy = 0;
			for (iter = clients_tab_order; iter; iter = niter) {
				struct client *c = iter->data;
				niter = iter->next;
				if (c->remove)
					remove_client(c);
			}
		}
	}
}
