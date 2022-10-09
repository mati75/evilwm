/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Maps keyboard and button controls to window manager functions.  Handles
// keypress and buttonpress events.

#ifndef EVILWM_BIND_H_
#define EVILWM_BIND_H_

#include <X11/X.h>
#include <X11/Xlib.h>

struct client;
struct screen;

// Modifier binds are kept in an array of mappings:

struct name_to_modifier {
	const char *name;
	unsigned value;
};

extern struct name_to_modifier name_to_modifier[];

// These modifiers are currently used explicitly in button-based controls, and
// are not reconfigurable beyond changing the modifier bind.

#define grabmask2 (name_to_modifier[1].value)
#define altmask   (name_to_modifier[2].value)

// Reset list of binds to the built-ins
void bind_reset(void);

// Alter modifier by name - only used for mask1, mask2, altmask
void bind_modifier(const char *modname, const char *modspec);

// Bind a control to a function + flags
void bind_control(const char *ctlspec, const char *funcspec);

// Apply grabs relevant to screen
void bind_grab_for_screen(struct screen *s);

// Apply grabs relevant to client
void bind_grab_for_client(struct client *c);

void bind_handle_key(XKeyEvent *e);
void bind_handle_button(XButtonEvent *e);

#endif
