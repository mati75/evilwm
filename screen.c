/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Screen management.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#ifdef RANDR
#include <X11/extensions/Xrandr.h>
#endif

#include "bind.h"
#include "client.h"
#include "display.h"
#include "evilwm.h"
#include "ewmh.h"
#include "list.h"
#include "log.h"
#include "screen.h"
#include "util.h"
#include "xalloc.h"

// Set up DISPLAY environment variable to use

static char *screen_to_display_str(int i) {
	char *ds = DisplayString(display.dpy);
	char *dpy_str = xmalloc(20 + strlen(ds));
	strcpy(dpy_str, "DISPLAY=");
	strcat(dpy_str, ds);
	char *colon = strrchr(dpy_str, ':');
	if (!colon || display.nscreens < 2)
		return dpy_str;

	char *dot = strchr(colon, '.');
	if (!dot)
		dot = colon + strlen(colon);
	snprintf(dot, 12, ".%d", i);

	return dpy_str;
}

// Called once per screen when display is being initialised.

void screen_init(struct screen *s) {
	int i = s->screen;

	// Used to set the DISPLAY environment variable to something like
	// ":0.x" depending on which screen a terminal is launched from.
	s->display = screen_to_display_str(i);

	s->root = RootWindow(display.dpy, i);
#ifdef RANDR
	s->nmonitors = 0;
	s->monitors = NULL;
        if (display.have_randr) {
		XRRSelectInput(display.dpy, s->root, RRScreenChangeNotifyMask);
	}
#endif
	screen_probe_monitors(s);

	// Default to first virtual desktop.  TODO: consider checking the
	// _NET_WM_DESKTOP property of the window with focus when we start to
	// change this default?
	s->vdesk = 0;

	// In case the visual for this screen uses a colourmap, ensure our
	// border colours are in it.
	XColor dummy;
	XAllocNamedColor(display.dpy, DefaultColormap(display.dpy, i), option.fg, &s->fg, &dummy);
	XAllocNamedColor(display.dpy, DefaultColormap(display.dpy, i), option.bg, &s->bg, &dummy);
	XAllocNamedColor(display.dpy, DefaultColormap(display.dpy, i), option.fc, &s->fc, &dummy);

	// When dragging an outline, we use an inverting graphics context
	// (GCFunction + GXinvert) so that simply drawing it again will erase
	// it from the screen.

	XGCValues gv;
	gv.function = GXinvert;
	gv.subwindow_mode = IncludeInferiors;
	gv.line_width = 1;  // option.bw
	gv.font = display.font->fid;
	s->invert_gc = XCreateGC(display.dpy, s->root,
				 GCFunction | GCSubwindowMode | GCLineWidth | GCFont, &gv);

	// We handle events to the root window:
	// SubstructureRedirectMask - create, destroy, configure window notifications
	// SubstructureNotifyMask - configure window requests
	// EnterWindowMask - enter events
	// ColormapChangeMask - when a new colourmap is needed

	XSetWindowAttributes attr;
	attr.event_mask = SubstructureRedirectMask | SubstructureNotifyMask
	                  | EnterWindowMask | ColormapChangeMask;
	XChangeWindowAttributes(display.dpy, s->root, CWEventMask, &attr);

	// Grab the various keyboard shortcuts
	bind_grab_for_screen(s);

	s->active = None;
	s->docks_visible = 1;

	Atom supported[] = {
		X_ATOM(_NET_CLIENT_LIST),
		X_ATOM(_NET_CLIENT_LIST_STACKING),
		X_ATOM(_NET_NUMBER_OF_DESKTOPS),
		X_ATOM(_NET_DESKTOP_GEOMETRY),
		X_ATOM(_NET_DESKTOP_VIEWPORT),
		X_ATOM(_NET_CURRENT_DESKTOP),
		X_ATOM(_NET_ACTIVE_WINDOW),
		X_ATOM(_NET_WORKAREA),
		X_ATOM(_NET_SUPPORTING_WM_CHECK),

		X_ATOM(_NET_CLOSE_WINDOW),
		X_ATOM(_NET_MOVERESIZE_WINDOW),
		X_ATOM(_NET_RESTACK_WINDOW),
		X_ATOM(_NET_REQUEST_FRAME_EXTENTS),

		X_ATOM(_NET_WM_DESKTOP),
		X_ATOM(_NET_WM_WINDOW_TYPE),
		X_ATOM(_NET_WM_WINDOW_TYPE_DESKTOP),
		X_ATOM(_NET_WM_WINDOW_TYPE_DOCK),
		X_ATOM(_NET_WM_STATE),
		X_ATOM(_NET_WM_STATE_MAXIMIZED_VERT),
		X_ATOM(_NET_WM_STATE_MAXIMIZED_HORZ),
		X_ATOM(_NET_WM_STATE_HIDDEN),
		X_ATOM(_NET_WM_STATE_FULLSCREEN),
		X_ATOM(_NET_WM_STATE_FOCUSED),
		X_ATOM(_NET_WM_ALLOWED_ACTIONS),

		// Not sure if it makes any sense including every action here
		// as they'll already be listed per-client in the
		// _NET_WM_ALLOWED_ACTIONS property, but EWMH spec is unclear.
		X_ATOM(_NET_WM_ACTION_MOVE),
		X_ATOM(_NET_WM_ACTION_RESIZE),
		X_ATOM(_NET_WM_ACTION_MAXIMIZE_HORZ),
		X_ATOM(_NET_WM_ACTION_MAXIMIZE_VERT),
		X_ATOM(_NET_WM_ACTION_FULLSCREEN),
		X_ATOM(_NET_WM_ACTION_CHANGE_DESKTOP),
		X_ATOM(_NET_WM_ACTION_CLOSE),
		X_ATOM(_NET_FRAME_EXTENTS),
	};

	unsigned long num_desktops = option.vdesks;
	unsigned long vdesk = s->vdesk;
	unsigned long pid = getpid();

	s->supporting = XCreateSimpleWindow(display.dpy, s->root, 0, 0, 1, 1, 0, 0, 0);
	XChangeProperty(display.dpy, s->root, X_ATOM(_NET_SUPPORTED),
			XA_ATOM, 32, PropModeReplace,
			(unsigned char *)&supported,
			sizeof(supported) / sizeof(Atom));
	XChangeProperty(display.dpy, s->root, X_ATOM(_NET_NUMBER_OF_DESKTOPS),
			XA_CARDINAL, 32, PropModeReplace,
			(unsigned char *)&num_desktops, 1);
	XChangeProperty(display.dpy, s->root, X_ATOM(_NET_CURRENT_DESKTOP),
			XA_CARDINAL, 32, PropModeReplace,
			(unsigned char *)&vdesk, 1);
	XChangeProperty(display.dpy, s->root, X_ATOM(_NET_ACTIVE_WINDOW),
	                XA_WINDOW, 32, PropModeReplace,
	                (unsigned char *)&s->active, 1);
	XChangeProperty(display.dpy, s->root, X_ATOM(_NET_SUPPORTING_WM_CHECK),
			XA_WINDOW, 32, PropModeReplace,
			(unsigned char *)&s->supporting, 1);
	XChangeProperty(display.dpy, s->supporting, X_ATOM(_NET_SUPPORTING_WM_CHECK),
			XA_WINDOW, 32, PropModeReplace,
			(unsigned char *)&s->supporting, 1);
	XChangeProperty(display.dpy, s->supporting, X_ATOM(_NET_WM_NAME),
			XA_STRING, 8, PropModeReplace,
			(const unsigned char *)"evilwm", 6);
	XChangeProperty(display.dpy, s->supporting, X_ATOM(_NET_WM_PID),
			XA_CARDINAL, 32, PropModeReplace,
			(unsigned char *)&pid, 1);

	ewmh_set_screen_workarea(s);
}

void screen_deinit(struct screen *s) {
	XDeleteProperty(display.dpy, s->root, X_ATOM(_NET_SUPPORTED));
	XDeleteProperty(display.dpy, s->root, X_ATOM(_NET_CLIENT_LIST));
	XDeleteProperty(display.dpy, s->root, X_ATOM(_NET_CLIENT_LIST_STACKING));
	XDeleteProperty(display.dpy, s->root, X_ATOM(_NET_NUMBER_OF_DESKTOPS));
	XDeleteProperty(display.dpy, s->root, X_ATOM(_NET_DESKTOP_GEOMETRY));
	XDeleteProperty(display.dpy, s->root, X_ATOM(_NET_DESKTOP_VIEWPORT));
	XDeleteProperty(display.dpy, s->root, X_ATOM(_NET_CURRENT_DESKTOP));
	XDeleteProperty(display.dpy, s->root, X_ATOM(_NET_ACTIVE_WINDOW));
	XDeleteProperty(display.dpy, s->root, X_ATOM(_NET_WORKAREA));
	XDeleteProperty(display.dpy, s->root, X_ATOM(_NET_SUPPORTING_WM_CHECK));
	XDestroyWindow(display.dpy, s->supporting);
	free(s->monitors);
}

// Get a list of monitors for the screen.  If Randr >= 1.5 is unavailable, or
// the "wholescreen" option has been specified, assume a single monitor
// covering the whole screen.

void screen_probe_monitors(struct screen *s) {
#if defined(RANDR) && (RANDR_MAJOR == 1) && (RANDR_MINOR >= 5)
        if (display.have_randr && !option.wholescreen) {
		int nmonitors;
		XRRMonitorInfo *monitors;
		// Populate list of active monitors
		LOG_XENTER("XRRGetMonitors(screen=%d)", s->screen);
		monitors = XRRGetMonitors(display.dpy, s->root, True, &nmonitors);
		if (monitors) {
			struct monitor *new_monitors = s->monitors;
			if (nmonitors != s->nmonitors) {
				// allocating in multiple of 4 should stop us
				// having to reallocate at all in the most
				// common uses
				int n = (nmonitors | 3) + 1;
				new_monitors = realloc(s->monitors, n * sizeof(struct monitor));
			}
			if (new_monitors) {
				s->monitors = new_monitors;
				for (int i = 0; i < nmonitors; i++) {
					LOG_XDEBUG("monitor %d: %dx%d+%d+%d\n", i, monitors[i].width, monitors[i].height, monitors[i].x, monitors[i].y);
					s->monitors[i].x = monitors[i].x;
					s->monitors[i].y = monitors[i].y;
					s->monitors[i].width = monitors[i].width;
					s->monitors[i].height = monitors[i].height;
					s->monitors[i].area = monitors[i].width * monitors[i].height;
				}
				s->nmonitors = nmonitors;
			}
			LOG_XLEAVE();
			XRRFreeMonitors(monitors);
			return;
		}
		LOG_XLEAVE();
	}
#endif

	s->nmonitors = 1;
	if (!s->monitors) {
		s->monitors = xmalloc(sizeof(struct monitor));
	}
	s->monitors[0].x = 0;
	s->monitors[0].y = 0;
	s->monitors[0].width = DisplayWidth(display.dpy, s->screen);
	s->monitors[0].height = DisplayHeight(display.dpy, s->screen);
	s->monitors[0].area = s->monitors[0].width * s->monitors[0].height;
}

// Switch virtual desktop.  Hides clients on different vdesks, shows clients on
// the selected one.  Docks are always shown (unless user has hidden them
// explicitly).  Fixed clients are always shown.

void switch_vdesk(struct screen *s, unsigned v) {
#ifdef DEBUG
	int nhidden = 0, nraised = 0;
#endif

	// Sanity check vdesk number.
	if (!valid_vdesk(v))
		return;

	// Selected == current?  Do nothing.
	if (v == s->vdesk)
		return;

	LOG_ENTER("switch_vdesk(screen=%d, from=%d, to=%d)", s->screen, s->vdesk, v);

	// If current client is not fixed, deselect it.  An enter event from
	// mapping clients may select a new one.
	if (current && !is_fixed(current)) {
		select_client(NULL);
	}

	for (struct list *iter = clients_tab_order; iter; iter = iter->next) {
		struct client *c = iter->data;
		if (c->screen != s)
			continue;
		if (c->vdesk == s->vdesk) {
			client_hide(c);
#ifdef DEBUG
			nhidden++;
#endif
		} else if (c->vdesk == v) {
			if (!c->is_dock || s->docks_visible)
				client_show(c);
#ifdef DEBUG
			nraised++;
#endif
		}
	}

	// Store previous vdesk, so that user may toggle back to it
	s->old_vdesk = s->vdesk;

	// Update current vdesk (including EWMH properties)
	s->vdesk = v;
	ewmh_set_net_current_desktop(s);

	LOG_DEBUG("%d hidden, %d raised\n", nhidden, nraised);
	LOG_LEAVE();
}

// Set whether docks are visible on the current screen.

void set_docks_visible(struct screen *s, int is_visible) {
	LOG_ENTER("set_docks_visible(screen=%d, is_visible=%d)", s->screen, is_visible);

	s->docks_visible = is_visible;

	// Traverse client list and hide or show any docks on this screen as
	// appropriate.

	for (struct list *iter = clients_tab_order; iter; iter = iter->next) {
		struct client *c = iter->data;
		if (c->screen != s)
			continue;
		if (c->is_dock) {
			if (is_visible) {
				// XXX I've assumed that if you want to see
				// them, you also want them raised...
				if (is_fixed(c) || (c->vdesk == s->vdesk)) {
					client_show(c);
					client_raise(c);
				}
			} else {
				client_hide(c);
			}
		}
	}

	LOG_LEAVE();
}

#ifdef RANDR

// If a screen has been resized (due to RandR), some windows have the
// possibility of:
//
//   a) not being visible
//   b) being vertically/horizontally maximised to the wrong extent
//
// Our approach is now to:
//
//   1) record client positions as proportions of offset into their current
//      monitor
//   2) apply screen geometry changes and rescan list of monitors
//   3) move any client that no longer intersects a monitor to the same
//      proportional position within its nearest monitor
//   4) adjust geometry of maximised clients to any "new" monitor

// Record old monitor offset for each client before resize.

void scan_clients_before_resize(struct screen *s) {
	for (struct list *iter = clients_tab_order; iter; iter = iter->next) {
		struct client *c = iter->data;
		// only handle clients on the screen being resized
		if (c->screen != s)
			continue;

		struct monitor *m = client_monitor(c, NULL);

		int mw = m->width;
		int mh = m->height;
		int cx = c->oldw ? c->oldx : c->x;
		int cy = c->oldh ? c->oldy : c->y;

		c->mon_offx = (double)(cx - m->x) / (double)mw;
		c->mon_offy = (double)(cy - m->y) / (double)mh;
	}
}

// Fix up maximised and non-intersecting clients after resize.

void fix_screen_after_resize(struct screen *s) {
	for (struct list *iter = clients_tab_order; iter; iter = iter->next) {
		struct client *c = iter->data;
		// only handle clients on the screen being resized
		if (c->screen != s)
			continue;
		Bool intersects;
		struct monitor *m = client_monitor(c, &intersects);

		if (c->oldw) {
			// horiz maximised: update width, update old x pos
			c->x = m->x - c->border;
			c->width = m->width;
			c->oldx = m->x + c->mon_offx * m->width;
		} else {
			// horiz normal: update x pos
			if (!intersects)
				c->x = m->x + c->mon_offx * m->width;
		}

		if (c->oldh) {
			// vert maximised: update height, update old y pos
			c->y = m->y - c->border;
			c->height = m->height;
			c->oldy = m->y + c->mon_offy * m->height;
		} else {
			// vert normal: update y pos
			if (!intersects)
				c->y = m->y + c->mon_offy * m->height;
		}
		client_moveresize(c);
	}
}

#endif

// Find screen corresponding to specified root window.

struct screen *find_screen(Window root) {
	for (int i = 0; i < display.nscreens; i++) {
		if (display.screens[i].root == root)
			return &display.screens[i];
	}
	return NULL;
}

// Find screen corresponding to the root window the pointer is currently on.

struct screen *find_current_screen(void) {
	Window cur_root;
	Window dw;  // dummy
	int di;  // dummy
	unsigned dui;  // dummy

	// XQueryPointer is useful for getting the current pointer root
	XQueryPointer(display.dpy, display.screens[0].root, &cur_root, &dw, &di, &di, &di, &di, &dui);
	return find_screen(cur_root);
}
