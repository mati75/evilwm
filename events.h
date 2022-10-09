/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// X11 event processing

#ifndef EVILWM_EVENTS_H_
#define EVILWM_EVENTS_H_

#include <X11/X.h>
#include <X11/Xdefs.h>

// Event loop will run until this flag is set
extern _Bool end_event_loop;

// Flags that the client list should be scanned and marked clients removed.
// Set by unhandled X errors and unmap requests.
extern int need_client_tidy;

// The main event loop - this will run until something signals the window
// manager to quit.

void event_main_loop(void);

#endif
