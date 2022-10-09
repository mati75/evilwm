/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Display management.
//
// One evilwm process can manage one display.

#ifndef EVILWM_DISPLAY_H_
#define EVILWM_DISPLAY_H_

#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

struct screen;

// List of atoms.  Reflect any changes here in atom_list[] in display.c.
enum {
	// Standard X protocol atoms
	X_ATOM_WM_STATE,
	X_ATOM_WM_PROTOCOLS,
	X_ATOM_WM_DELETE_WINDOW,
	X_ATOM_WM_COLORMAP_WINDOWS,

	// Motif atoms
	X_ATOM__MOTIF_WM_HINTS,

	// evilwm atoms
	X_ATOM__EVILWM_UNMAXIMISED_HORZ,
	X_ATOM__EVILWM_UNMAXIMISED_VERT,

	// EWMH: Root Window Properties (and Related Messages)
	X_ATOM__NET_SUPPORTED,
	X_ATOM__NET_CLIENT_LIST,
	X_ATOM__NET_CLIENT_LIST_STACKING,
	X_ATOM__NET_NUMBER_OF_DESKTOPS,
	X_ATOM__NET_DESKTOP_GEOMETRY,
	X_ATOM__NET_DESKTOP_VIEWPORT,
	X_ATOM__NET_CURRENT_DESKTOP,
	X_ATOM__NET_ACTIVE_WINDOW,
	X_ATOM__NET_WORKAREA,
	X_ATOM__NET_SUPPORTING_WM_CHECK,

	// EWMH: Other Root Window Messages
	X_ATOM__NET_CLOSE_WINDOW,
	X_ATOM__NET_MOVERESIZE_WINDOW,
	X_ATOM__NET_RESTACK_WINDOW,
	X_ATOM__NET_REQUEST_FRAME_EXTENTS,

	// EWMH: Application Window Properties
	X_ATOM__NET_WM_NAME,
	X_ATOM__NET_WM_DESKTOP,
	X_ATOM__NET_WM_WINDOW_TYPE,
	X_ATOM__NET_WM_WINDOW_TYPE_DESKTOP,
	X_ATOM__NET_WM_WINDOW_TYPE_DOCK,
	X_ATOM__NET_WM_WINDOW_TYPE_NOTIFICATION,
	X_ATOM__NET_WM_STATE,
	X_ATOM__NET_WM_STATE_MAXIMIZED_VERT,
	X_ATOM__NET_WM_STATE_MAXIMIZED_HORZ,
	X_ATOM__NET_WM_STATE_HIDDEN,
	X_ATOM__NET_WM_STATE_FULLSCREEN,
	X_ATOM__NET_WM_STATE_FOCUSED,
	X_ATOM__NET_WM_ALLOWED_ACTIONS,
	X_ATOM__NET_WM_ACTION_MOVE,
	X_ATOM__NET_WM_ACTION_RESIZE,
	X_ATOM__NET_WM_ACTION_MAXIMIZE_HORZ,
	X_ATOM__NET_WM_ACTION_MAXIMIZE_VERT,
	X_ATOM__NET_WM_ACTION_FULLSCREEN,
	X_ATOM__NET_WM_ACTION_CHANGE_DESKTOP,
	X_ATOM__NET_WM_ACTION_CLOSE,
	X_ATOM__NET_WM_PID,
	X_ATOM__NET_FRAME_EXTENTS,

	NUM_ATOMS
};

#define X_ATOM(a) display.atom[X_ATOM_ ## a]

struct display {
	// The display handle
	Display *dpy;

	// Atoms
	Atom atom[NUM_ATOMS];

	// Font
	XFontStruct *font;

	// Cursors
	Cursor move_curs;
	Cursor resize_curs;

	// Extensions
#ifdef SHAPE
	Bool have_shape;
	int shape_event;
#endif
#ifdef RANDR
	Bool have_randr;
	int randr_event_base;
#endif

	// Screens
	int nscreens;
	struct screen *screens;

	// Information window
#ifdef INFOBANNER
	Window info_window;
#endif
};

// evilwm only supports one display at a time; this variable is global:
extern struct display display;

// Open and initialise display.  Exits the process on failure.
void display_open(void);

// Close display.
void display_close(void);

// Manage all relevant windows.
void display_manage_clients(void);

// Remove all windows from management.
void display_unmanage_clients(void);

#endif
