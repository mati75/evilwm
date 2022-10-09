/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

#ifndef EVILWM_UTIL_H_
#define EVILWM_UTIL_H_

#include <X11/X.h>
#include <X11/Xdefs.h>

// Required for interpreting MWM hints

#define PROP_MWM_HINTS_ELEMENTS 3
#define MWM_HINTS_DECORATIONS   (1L << 1)
#define MWM_DECOR_ALL           (1L << 0)
#define MWM_DECOR_BORDER        (1L << 1)
typedef struct {
	unsigned long flags;
	unsigned long functions;
	unsigned long decorations;
} PropMwmHints;

// EWMH hints use these definitions, so for simplicity my functions will too:

#define NET_WM_STATE_REMOVE     0    /* remove/unset property */
#define NET_WM_STATE_ADD        1    /* add/set property */
#define NET_WM_STATE_TOGGLE     2    /* toggle property  */

// Grab pointer using specified cursor.  Report button press/release, and
// pointer motion events.

#define grab_pointer(w, curs) \
	(XGrabPointer(display.dpy, w, False, \
		      ButtonPressMask|ButtonReleaseMask|PointerMotionMask, \
		      GrabModeAsync, GrabModeAsync, \
		      None, curs, CurrentTime) == GrabSuccess)

// Move the mouse pointer.

#define setmouse(w, x, y) XWarpPointer(display.dpy, None, w, 0, 0, 0, 0, x, y)

// Error handler interaction
extern int ignore_xerror;
extern volatile Window initialising;

// Spawn a subprocess (usually xterm or similar)
void spawn(const char *const cmd[]);

// Global X11 error handler.  Various actions interact with this.
int handle_xerror(Display *dsply, XErrorEvent *e);

// Simplify calls to XQueryPointer(), and make destination pointers optional
Bool get_pointer_root_xy(Window w, int *x, int *y); // Wraps XQueryPointer()

// Wraps XGetWindowProperty()
void *get_property(Window w, Atom property, Atom req_type, unsigned long *nitems_return);

// Determine the normal border size for a window.
int window_normal_border(Window w);

// Alternative to XNextEvent().  Unlike XNextEvent, if a signal arrives,
// interruptibleXNextEvent will return zero.
int interruptibleXNextEvent(XEvent *event);

// Remove enter events from the queue, preserving only the last one
// corresponding to "except"s parent.
void discard_enter_events(struct client *except);

#endif
