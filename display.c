/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Display management

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <X11/cursorfont.h>
#include <X11/keysymdef.h>

#ifdef SHAPE
#include <X11/extensions/shape.h>
#endif
#ifdef RANDR
#include <X11/extensions/Xrandr.h>
#endif

#include "client.h"
#include "display.h"
#include "evilwm.h"
#include "ewmh.h"
#include "list.h"
#include "log.h"
#include "screen.h"
#include "util.h"
#include "xalloc.h"

// evilwm only supports one display at a time; this variable is global:
struct display display = { 0 };

// List of atom names.  Reflect any changes here in the enum in display.h.
static const char *atom_list[] = {
	// Standard X protocol atoms
	"WM_STATE",
	"WM_PROTOCOLS",
	"WM_DELETE_WINDOW",
	"WM_COLORMAP_WINDOWS",

	// Motif atoms
	"_MOTIF_WM_HINTS",

	// evilwm atoms
	"_EVILWM_UNMAXIMISED_HORZ",
	"_EVILWM_UNMAXIMISED_VERT",

	// EWMH: Root Window Properties (and Related Messages)
	"_NET_SUPPORTED",
	"_NET_CLIENT_LIST",
	"_NET_CLIENT_LIST_STACKING",
	"_NET_NUMBER_OF_DESKTOPS",
	"_NET_DESKTOP_GEOMETRY",
	"_NET_DESKTOP_VIEWPORT",
	"_NET_CURRENT_DESKTOP",
	"_NET_ACTIVE_WINDOW",
	"_NET_WORKAREA",
	"_NET_SUPPORTING_WM_CHECK",

	// EWMH: Other Root Window Messages
	"_NET_CLOSE_WINDOW",
	"_NET_MOVERESIZE_WINDOW",
	"_NET_RESTACK_WINDOW",
	"_NET_REQUEST_FRAME_EXTENTS",

	// EWMH: Application Window Properties
	"_NET_WM_NAME",
	"_NET_WM_DESKTOP",
	"_NET_WM_WINDOW_TYPE",
	"_NET_WM_WINDOW_TYPE_DESKTOP",
	"_NET_WM_WINDOW_TYPE_DOCK",
	"_NET_WM_WINDOW_TYPE_NOTIFICATION",
	"_NET_WM_STATE",
	"_NET_WM_STATE_MAXIMIZED_VERT",
	"_NET_WM_STATE_MAXIMIZED_HORZ",
	"_NET_WM_STATE_HIDDEN",
	"_NET_WM_STATE_FULLSCREEN",
	"_NET_WM_STATE_FOCUSED",
	"_NET_WM_ALLOWED_ACTIONS",
	"_NET_WM_ACTION_MOVE",
	"_NET_WM_ACTION_RESIZE",
	"_NET_WM_ACTION_MAXIMIZE_HORZ",
	"_NET_WM_ACTION_MAXIMIZE_VERT",
	"_NET_WM_ACTION_FULLSCREEN",
	"_NET_WM_ACTION_CHANGE_DESKTOP",
	"_NET_WM_ACTION_CLOSE",
	"_NET_WM_PID",
	"_NET_FRAME_EXTENTS",
};

// Open and initialise display.  Exits the process on failure.
void display_open(void) {
	LOG_ENTER("display_open()");

	display.dpy = XOpenDisplay(option.display);
	if (!display.dpy) {
		LOG_ERROR("can't open display %s\n", option.display);
		exit(1);
	}
	display.info_window = None;

	XSetErrorHandler(handle_xerror);

	// While debugging, synchronous behaviour may be desirable:
	//XSynchronize(display.dpy, True);

	// Sanity check that there's a name for each atom in the enum:
	assert(NUM_ATOMS == (sizeof(atom_list)/sizeof(atom_list[0])));

	// Get or create all required atom IDs
	for (int i = 0; i < NUM_ATOMS; i++) {
		display.atom[i] = XInternAtom(display.dpy, atom_list[i], False);
	}

	// Get the font used for window info
	display.font = XLoadQueryFont(display.dpy, option.font);
	if (!display.font) {
		LOG_DEBUG("failed to load specified font, trying default: %s\n", DEF_FONT);
		display.font = XLoadQueryFont(display.dpy, DEF_FONT);
	}
	if (!display.font) {
		LOG_ERROR("couldn't find a font to use: try starting with -fn fontname\n");
		exit(1);
	}

	// Cursors used for different actions
	display.move_curs = XCreateFontCursor(display.dpy, XC_fleur);
	display.resize_curs = XCreateFontCursor(display.dpy, XC_plus);

	// Find out which modifier is NumLock - for every grab, we need to also
	// grab the combination where this is set.
	XModifierKeymap *modmap = XGetModifierMapping(display.dpy);
	for (unsigned i = 0; i < 8; i++) {
		for (unsigned j = 0; j < (unsigned)modmap->max_keypermod; j++) {
			if (modmap->modifiermap[i*modmap->max_keypermod+j] == XKeysymToKeycode(display.dpy, XK_Num_Lock)) {
				numlockmask = (1<<i);
				LOG_DEBUG("XK_Num_Lock is (1<<0x%02x)\n", i);
			}
		}
	}
	XFreeModifiermap(modmap);

	// SHAPE extension?
#ifdef SHAPE
	{
		int e_dummy;
		display.have_shape = XShapeQueryExtension(display.dpy, &display.shape_event, &e_dummy);
	}
#endif
	// XRandR extension?
#ifdef RANDR
	{
		int e_dummy;
		display.have_randr = XRRQueryExtension(display.dpy, &display.randr_event_base, &e_dummy);
		if (!display.have_randr) {
			LOG_DEBUG("XRandR is not supported on this display.\n");
		}
	}
#endif

	// Initialise screens
	display.nscreens = ScreenCount(display.dpy);
	if (display.nscreens < 0) {
		LOG_ERROR("Can't count screens\n");
		exit(1);
	}
	display.screens = xmalloc(display.nscreens * sizeof(struct screen));
	for (int i = 0; i < display.nscreens; i++) {
		display.screens[i].screen = i;
		screen_init(&display.screens[i]);
	}

	LOG_LEAVE();
}

// Close display.  Unmanages all windows cleanly.  While managing, windows will
// have been offset to account for borders, gravity, etc.  This will undo those
// offsets so that repeatedly restarting window managers doesn't result in
// moved windows.

void display_close(void) {
	XSetInputFocus(display.dpy, PointerRoot, RevertToPointerRoot, CurrentTime);

	if (display.font)
		XFreeFont(display.dpy, display.font);

	for (int i = 0; i < display.nscreens; i++) {
		screen_deinit(&display.screens[i]);
		XFreeGC(display.dpy, display.screens[i].invert_gc);
		XInstallColormap(display.dpy, DefaultColormap(display.dpy, i));
		free(display.screens[i].display);
	}

	free(display.screens);

	XCloseDisplay(display.dpy);
	display.dpy = 0;
}

void display_manage_clients(void) {
	for (int i = 0; i < display.nscreens; i++) {
		struct screen *s = &display.screens[i];

		LOG_XENTER("XQueryTree(screen=%d)", i);
		unsigned nwins;
		Window dw1, dw2, *wins;
		XQueryTree(display.dpy, s->root, &dw1, &dw2, &wins, &nwins);
		LOG_XDEBUG("%u windows\n", nwins);
		LOG_XLEAVE();

		// Manage all relevant windows
		for (unsigned j = 0; j < nwins; j++) {
			XWindowAttributes winattr;
			XGetWindowAttributes(display.dpy, wins[j], &winattr);
			// Override redirect implies a pop-up that we should ignore.
			// If map_state is not IsViewable, it shouldn't be shown right
			// now, so don't try to manage it.
			if (!winattr.override_redirect && winattr.map_state == IsViewable)
				client_manage_new(wins[j], s);
		}
		XFree(wins);
	}
}

void display_unmanage_clients(void) {
	while (clients_stacking_order)
		remove_client(clients_stacking_order->data);
}
