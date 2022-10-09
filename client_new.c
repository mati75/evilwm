/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Client management: manage new client.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#ifdef SHAPE
#include <X11/extensions/shape.h>
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

static void init_geometry(struct client *c);
static void reparent(struct client *c);

// client_manage_new is called when a map request event for an unmanaged window
// is handled, and on startup for all windows found.

void client_manage_new(Window w, struct screen *s) {
	struct client *c;
	char *name;
	XClassHint *class;
	unsigned window_type;

	LOG_ENTER("client_manage_new(window=%lx)", (unsigned long)w);

	XGrabServer(display.dpy);

	// First a bit of interaction with the error handler due to X's
	// tendency to batch event notifications.  We set a global variable to
	// the id of the window we're initialising then do simple X call on
	// that window.  If an error is raised by this (and nothing else should
	// do so as we've grabbed the server), the error handler resets the
	// variable indicating the window has already disappeared, so we stop
	// trying to manage it.

	initialising = w;
	XFetchName(display.dpy, w, &name);
	XSync(display.dpy, False);

	// If 'initialising' is now set to None, that means doing the
	// XFetchName raised BadWindow - the window has been removed before
	// we got a chance to grab the server. */

	if (initialising == None) {
		LOG_DEBUG("XError occurred for initialising window - aborting...\n");
		XUngrabServer(display.dpy);
		LOG_LEAVE();
		return;
	}
	initialising = None;
	LOG_DEBUG("screen=%d\n", s->screen);
	LOG_DEBUG("name=%s\n", name ? name : "Untitled");
	if (name)
		XFree(name);

	window_type = ewmh_get_net_wm_window_type(w);
	// Don't manage DESKTOP type windows
	if (window_type & EWMH_WINDOW_TYPE_DESKTOP) {
		XMapWindow(display.dpy, w);
		XUngrabServer(display.dpy);
		return;
	}

	// If allocation fails, don't crash the window manager.  Just don't
	// manage the window.
	c = malloc(sizeof(struct client));
	if (!c) {
		LOG_ERROR("out of memory allocating new client\n");
		XMapWindow(display.dpy, w);
		XUngrabServer(display.dpy);
		LOG_LEAVE();
		return;
	}
	clients_tab_order = list_prepend(clients_tab_order, c);
	clients_mapping_order = list_append(clients_mapping_order, c);
	clients_stacking_order = list_append(clients_stacking_order, c);

	c->screen = s;
	c->window = w;
	c->ignore_unmap = 0;
	c->remove = 0;

	// Ungrab the X server as soon as possible. Now that the client is
	// malloc()ed and attached to the list, it is safe for any subsequent
	// X calls to raise an X error and thus flag it for removal.

	XUngrabServer(display.dpy);

	update_window_type_flags(c, window_type);
	init_geometry(c);

#ifdef DEBUG
	{
		int i = 0;
		for (struct list *iter = clients_tab_order; iter; iter = iter->next)
			i++;
		LOG_DEBUG("new window %dx%d+%d+%d, wincount=%d\n", c->width, c->height, c->x, c->y, i);
	}
#endif

	XSelectInput(display.dpy, c->window, ColormapChangeMask | EnterWindowMask | PropertyChangeMask);

	reparent(c);

#ifdef SHAPE
	if (display.have_shape) {
		XShapeSelectInput(display.dpy, c->window, ShapeNotifyMask);
		set_shape(c);
	}
#endif

	// Read name/class information for client and check against list built
	// with -app options.
	class = XAllocClassHint();
	if (class) {
		XGetClassHint(display.dpy, w, class);
		for (struct list *iter = applications; iter; iter = iter->next) {
			struct application *a = iter->data;
			// Does resource name and class match?
			if ((!a->res_name || (class->res_name && !strcmp(class->res_name, a->res_name)))
					&& (!a->res_class || (class->res_class && !strcmp(class->res_class, a->res_class)))) {

				// Override width or height?
				if (a->geometry_mask & WidthValue)
					c->width = a->width * c->width_inc;
				if (a->geometry_mask & HeightValue)
					c->height = a->height * c->height_inc;

				// Override X or Y?
				if (a->geometry_mask & XValue) {
					if (a->geometry_mask & XNegative)
						c->x = a->x + DisplayWidth(display.dpy, s->screen)-c->width-c->border;
					else
						c->x = a->x + c->border;
				}
				if (a->geometry_mask & YValue) {
					if (a->geometry_mask & YNegative)
						c->y = a->y + DisplayHeight(display.dpy, s->screen)-c->height-c->border;
					else
						c->y = a->y + c->border;
				}

				// XXX better way of updating window geometry?
				client_moveresizeraise(c);

				// Force treating this app as a dock?
				if (a->is_dock)
					c->is_dock = 1;

				// Force app to specific vdesk?
				if (a->vdesk != VDESK_NONE)
					c->vdesk = a->vdesk;
			}
		}
		XFree(class->res_name);
		XFree(class->res_class);
		XFree(class);
	}

	// Set EWMH property on client advertising WM features
	ewmh_set_allowed_actions(c);

	// Update EWMH client list hints for screen
	ewmh_set_net_client_list(c->screen);
	ewmh_set_net_client_list_stacking(c->screen);

	// Only map the window frame (and thus the window) if it's supposed
	// to be visible on this virtual desktop.  Otherwise, set it to
	// IconicState (hidden).
	if (is_fixed(c) || c->vdesk == s->vdesk) {
		client_show(c);
		client_raise(c);
		// Don't focus windows that aren't on the same display as the
		// pointer.
		if (get_pointer_root_xy(c->window, NULL, NULL) &&
		    !(window_type & (EWMH_WINDOW_TYPE_DOCK|EWMH_WINDOW_TYPE_NOTIFICATION))) {
			select_client(c);
#ifdef WARP_POINTER
			setmouse(c->window, c->width + c->border - 1,
				 c->height + c->border - 1);
#endif
			discard_enter_events(c);
		}
	} else {
		set_wm_state(c, IconicState);
	}

	// Ensure whichever vdesk it ended up on is reflected in the EWMH hints
	ewmh_set_net_wm_desktop(c);

	LOG_LEAVE();
}

// Fetches various hints to determine a window's initial geometry.

static void init_geometry(struct client *c) {
	unsigned long nitems;
	XWindowAttributes attr;

	// Normal border size from MWM hints
	c->normal_border = window_normal_border(c->window);

	// Possible get a value for initial virtual desktop from EWMH hint
	unsigned long *lprop;
	c->vdesk = c->screen->vdesk;
	if ( (lprop = get_property(c->window, X_ATOM(_NET_WM_DESKTOP), XA_CARDINAL, &nitems)) ) {
		// NB, Xlib not only returns a 32bit value in a long (which may
		// not be 32bits), it also sign extends the 32bit value
		if (nitems && valid_vdesk(lprop[0] & UINT32_MAX)) {
			c->vdesk = lprop[0] & UINT32_MAX;
		}
		XFree(lprop);
	}

	// Get current window attributes
	LOG_XENTER("XGetWindowAttributes(window=%lx)", (unsigned long)c->window);
	XGetWindowAttributes(display.dpy, c->window, &attr);
	debug_window_attributes(&attr);
	LOG_XLEAVE();
	// We remove any client border, so preserve its old value to restore on
	// emulator quit.
	c->old_border = attr.border_width;
	c->cmap = attr.colormap;

	// Default to no unmaximised width/height.
	c->oldw = c->oldh = 0;

	// If the _EVILWM_UNMAXIMISED_HORZ is present, it was previously
	// managed by evilwm and this property contains the unmaximised X
	// coordinate and width.  These are unrepresented in EWMH hints, so
	// would otherwise not survive window manager restart.
	unsigned long *eprop;
	if ( (eprop = get_property(c->window, X_ATOM(_EVILWM_UNMAXIMISED_HORZ), XA_CARDINAL, &nitems)) ) {
		if (nitems == 2) {
			c->oldx = eprop[0];
			c->oldw = eprop[1];
		}
		XFree(eprop);
	}

	// Similarly _EVILWM_UNMAXIMISED_VERT will contain the unmaximised Y
	// coordinate and height.
	if ( (eprop = get_property(c->window, X_ATOM(_EVILWM_UNMAXIMISED_VERT), XA_CARDINAL, &nitems)) ) {
		if (nitems == 2) {
			c->oldy = eprop[0];
			c->oldh = eprop[1];
		}
		XFree(eprop);
	}

	c->border = (c->oldw && c->oldh) ? 0 : c->normal_border;

	// Update some client info from the WM_NORMAL_HINTS property.  The
	// flags returned will indicate whether certain values were user- or
	// program-specified.
	long size_flags = get_wm_normal_hints(c);

	_Bool need_send_config = 0;

	// If the current window dimensions conform to the minimums specified
	// in WM_NORMAL_HINTS, use them.  Otherwise, use the mimimums.
	if ((attr.width >= c->min_width) && (attr.height >= c->min_height)) {
		c->width = attr.width;
		c->height = attr.height;
	} else {
		c->width = c->min_width;
		c->height = c->min_height;
		need_send_config = 1;
	}

	// If the window was already visible (as we manage existing windows on
	// startup), or if its screen position was user-specified, use its
	// current position.  Otherwise (new window, post startup), calculate a
	// position for it based on where the pointer is.

	// XXX: if an existing window would be mapped off the screen, would it
	// be sensible to move it somewhere visible?

	if ((attr.map_state == IsViewable) || (size_flags & USPosition)) {
		c->x = attr.x;
		c->y = attr.y;
	} else {
		int xmax = DisplayWidth(display.dpy, c->screen->screen);
		int ymax = DisplayHeight(display.dpy, c->screen->screen);
		int x, y;
		get_pointer_root_xy(c->screen->root, &x, &y);
		c->x = (x * (xmax - c->border - c->width)) / xmax;
		c->y = (y * (ymax - c->border - c->height)) / ymax;
		need_send_config = 1;
	}

	if (need_send_config)
		send_config(c);

	ewmh_set_net_frame_extents(c->window, c->border);

	LOG_DEBUG("window started as %dx%d +%d+%d\n", c->width, c->height, c->x, c->y);

	// If the window was already viewable (existed while window manager
	// starts), that means the reparent to come would send an unmap request
	// to the root window.  Set a flag to ignore this.
	if (attr.map_state == IsViewable) {
		c->ignore_unmap++;
	}

	// Account for removed old_border
	c->x += c->old_border;
	c->y += c->old_border;
	client_gravitate(c, -c->old_border);
	client_gravitate(c, c->border);
}

// Create parent window for a client and reparent.

static void reparent(struct client *c) {
	XSetWindowAttributes p_attr;

	// Default border is unselected (bg)
	p_attr.border_pixel = c->screen->bg.pixel;
	// We want to handle events for this parent window
	p_attr.override_redirect = True;
	// The events we need to manage the window
	p_attr.event_mask = SubstructureRedirectMask | SubstructureNotifyMask
	                    | ButtonPressMask | EnterWindowMask;

	// Create parent window, accounting for border width
	c->parent = XCreateWindow(display.dpy, c->screen->root, c->x-c->border, c->y-c->border,
		c->width, c->height, c->border,
		DefaultDepth(display.dpy, c->screen->screen), CopyFromParent,
		DefaultVisual(display.dpy, c->screen->screen),
		CWOverrideRedirect | CWBorderPixel | CWEventMask, &p_attr);

	// Adding the original window to our "save set" means that if we die
	// unexpectedly, the window will be reparented back to the root.
	XAddToSaveSet(display.dpy, c->window);

	// Kill any internal border on the application window
	XSetWindowBorderWidth(display.dpy, c->window, 0);

	// Reparent into our new parent window
	XReparentWindow(display.dpy, c->window, c->parent, 0, 0);

	// Map the window (shows up within parent)
	XMapWindow(display.dpy, c->window);

	// Grab mouse button actions on the parent window
	bind_grab_for_client(c);
}
