/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Miscellaneous utility functions

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>

#include "client.h"
#include "display.h"
#include "events.h"
#include "evilwm.h"
#include "log.h"
#include "screen.h"
#include "util.h"

// For get_property()
#define MAXIMUM_PROPERTY_LENGTH 4096

// Error handler interaction
int ignore_xerror = 0;
volatile Window initialising = None;

// Spawn a subprocess by fork()ing twice so we don't have to worry about
// SIGCHLDs.

void spawn(const char *const cmd[]) {
	struct screen *current_screen = find_current_screen();
	pid_t pid;

	if (current_screen && current_screen->display)
		putenv(current_screen->display);
	if (!(pid = fork())) {
		// Put first fork in a new session
		setsid();
		switch (fork()) {
			// execvp()'s prototype is (char *const *) suggesting that it
			// modifies the contents of the strings.  The prototype is this
			// way due to SUS maintaining compatability with older code.
			// However, execvp guarantees not to modify argv, so the following
			// cast is valid.
			case 0: execvp(cmd[0], (char *const *)cmd); break;
			default: _exit(0);
		}
	}
	if (pid > 0)
		wait(NULL);
}

// When something we do raises an X error, we get sent here.  There are several
// specific types of error that we know we want to ignore, or that indicate a
// fatal error.  For the rest, cease managing the client, as it should indicate
// that the window has disappeared.

int handle_xerror(Display *dsply, XErrorEvent *e) {
	struct client *c;
	(void)dsply;  // unused

	LOG_ENTER("handle_xerror(error=%d, request=%d/%d, resourceid=%lx)", e->error_code, e->request_code, e->minor_code, e->resourceid);

	// Some parts of the code deliberately disable error checking.

	if (ignore_xerror) {
		LOG_DEBUG("ignoring...\n");
		LOG_LEAVE();
		return 0;
	}

	// client_manage_new() sets initialising to non-None to test if a
	// window still exists.  If we end up here, the test failed, so
	// indicate that by setting it back to None.

	if (initialising != None && e->resourceid == initialising) {
		LOG_DEBUG("error caught while initialising window=%lx\n", (unsigned long)initialising);
		initialising = None;
		LOG_LEAVE();
		return 0;
	}

	// This error is generally raised when trying to start evilwm while
	// another window manager is running.

	if (e->error_code == BadAccess && e->request_code == X_ChangeWindowAttributes) {
		LOG_ERROR("root window unavailable (maybe another wm is running?)\n");
		exit(1);
	}

	// Batching of events sometimes leads to us calling XSetInputFocus()
	// for enter events to windows that were subsequently deleted.  Ignore
	// these errors.

	if (e->request_code == X_SetInputFocus) {
		LOG_DEBUG("ignoring harmless error caused by possible race\n");
		LOG_LEAVE();
		return 0;
	}

	// For any other raised error, remove the client that triggered it from
	// management, as it's probably gone.

	c = find_client(e->resourceid);
	if (c) {
		LOG_DEBUG("flagging client for removal\n");
		c->remove = 1;
		need_client_tidy = 1;
	} else {
		LOG_DEBUG("unknown error: not handling\n");
	}

	LOG_LEAVE();
	return 0;
}

// Simplify calls to XQueryPointer(), and make destination pointers optional

Bool get_pointer_root_xy(Window w, int *x, int *y) {
	Window root_r, child_r;
	int root_x_r, root_y_r;
	int win_x_r, win_y_r;
	unsigned mask_r;
	if (!x)
		x = &root_x_r;
	if (!y)
		y = &root_y_r;
	return XQueryPointer(display.dpy, w, &root_r, &child_r, x, y, &win_x_r, &win_y_r, &mask_r);
}

// Wraps XGetWindowProperty()

void *get_property(Window w, Atom property, Atom req_type,
		   unsigned long *nitems_return) {
	Atom actual_type;
	int actual_format;
	unsigned long bytes_after;
	unsigned char *prop;
	if (XGetWindowProperty(display.dpy, w, property,
			       0L, MAXIMUM_PROPERTY_LENGTH / 4, False,
			       req_type, &actual_type, &actual_format,
			       nitems_return, &bytes_after, &prop) == Success) {
		if (actual_type == req_type)
			return (void *)prop;
		XFree(prop);
	}
	return NULL;
}

// Determine the normal border size for a window.  MWM hints seem to be the
// only way clients can signal they don't want a border.

int window_normal_border(Window w) {
	int bw = option.bw;
	PropMwmHints *mprop;
	unsigned long nitems;
	if ( (mprop = get_property(w, X_ATOM(_MOTIF_WM_HINTS), X_ATOM(_MOTIF_WM_HINTS), &nitems)) ) {
		if (nitems >= PROP_MWM_HINTS_ELEMENTS
		    && (mprop->flags & MWM_HINTS_DECORATIONS)
		    && !(mprop->decorations & MWM_DECOR_ALL)
		    && !(mprop->decorations & MWM_DECOR_BORDER)) {
			bw = 0;
		}
		XFree(mprop);
	}
	return bw;
}


// interruptibleXNextEvent() is taken from the Blender source and comes with
// the following copyright notice:
//
// Copyright (c) Mark J. Kilgard, 1994, 1995, 1996.
//
// This program is freely distributable without licensing fees and is provided
// without guarantee or warrantee expressed or implied. This program is -not-
// in the public domain.

// Unlike XNextEvent, if a signal arrives, interruptibleXNextEvent will return
// zero.

int interruptibleXNextEvent(XEvent *event) {
	fd_set fds;
	int rc;
	int dpy_fd = ConnectionNumber(display.dpy);
	for (;;) {
		if (XPending(display.dpy)) {
			XNextEvent(display.dpy, event);
			return 1;
		}
		FD_ZERO(&fds);
		FD_SET(dpy_fd, &fds);
		rc = select(dpy_fd + 1, &fds, NULL, NULL, NULL);
		if (rc < 0) {
			if (errno == EINTR) {
				return 0;
			} else {
				LOG_ERROR("interruptibleXNextEvent(): select()\n");
			}
		}
	}
}

// Remove enter events from the queue, preserving only the last one
// corresponding to "except"s parent.

void discard_enter_events(struct client *except) {
	XEvent tmp, putback_ev;
	int putback = 0;
	XSync(display.dpy, False);
	while (XCheckMaskEvent(display.dpy, EnterWindowMask, &tmp)) {
		if (tmp.xcrossing.window == except->parent) {
			memcpy(&putback_ev, &tmp, sizeof(XEvent));
			putback = 1;
		}
	}
	if (putback) {
		XPutBackEvent(display.dpy, &putback_ev);
	}
}
