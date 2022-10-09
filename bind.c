/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Keyboard and button function definitions
//
// Static lists of binds for now, but the point of having them like this is to
// enable configuration of key and button binds.

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

#include "bind.h"
#include "client.h"
#include "display.h"
#include "evilwm.h"
#include "func.h"
#include "list.h"
#include "log.h"
#include "screen.h"
#include "util.h"
#include "xalloc.h"

// Configurable modifier bits.  For client operations, grabmask2 is always
// allowed for button presses.
#define KEY_STATE_MASK ( ShiftMask | ControlMask | Mod1Mask | \
                         Mod2Mask | Mod3Mask | Mod4Mask | Mod5Mask )
#define BUTTON_STATE_MASK ( KEY_STATE_MASK & ~grabmask2 )

// Map modifier name to mask

struct name_to_modifier name_to_modifier[] = {
	// The order of the first three entries here is important, as they are
	// referenced directly:
	{ "mask1",      ControlMask|Mod1Mask },
	{ "mask2",      Mod1Mask },
	{ "altmask",    ShiftMask },
	{ "shift",      ShiftMask },
	{ "control",    ControlMask },
	{ "ctrl",       ControlMask },
	{ "alt",        Mod1Mask },
	{ "mod1",       Mod1Mask },
	{ "mod2",       Mod2Mask },
	{ "mod3",       Mod3Mask },
	{ "mod4",       Mod4Mask },
	{ "mod5",       Mod5Mask }
};
#define NUM_NAME_TO_MODIFIER (int)(sizeof(name_to_modifier) / sizeof(name_to_modifier[0]))

// Map button name to identifier

struct name_to_button {
	const char *name;
	unsigned button;
};

static struct name_to_button name_to_button[] = {
	{ "button1",    Button1 },
	{ "button2",    Button2 },
	{ "button3",    Button3 },
	{ "button4",    Button4 },
	{ "button5",    Button5 },
};
#define NUM_NAME_TO_BUTTON (int)(sizeof(name_to_button) / sizeof(name_to_button[0]))

// All bindable functions present the same call interface.  'sptr' should point
// to a relevant data structure (controlled by flags FL_SCREEN or FL_CLIENT).

typedef void (*func_dispatch)(void *sptr, XEvent *e, unsigned flags);

// Maps a user-specifiable function name to internal function and the base
// flags required to call it, including whether 'sptr' should be (struct screen
// *) or (struct client *).  Futher flags may be ORed in.

struct function_def {
        const char *name;
        func_dispatch func;
        unsigned flags;
};

static struct function_def name_to_func[] = {
	{ "delete", func_delete,    FL_CLIENT|0 },
	{ "kill",   func_delete,    FL_CLIENT|1 },
	{ "dock",   func_dock,      FL_SCREEN },
	{ "info",   func_info,      FL_CLIENT },
	{ "lower",  func_lower,     FL_CLIENT },
	{ "move",   func_move,      FL_CLIENT },
	{ "next",   func_next,      0 },
	{ "resize", func_resize,    FL_CLIENT },
	{ "spawn",  func_spawn,     0 },
	{ "vdesk",  func_vdesk,     FL_SCREEN },
	{ "fix",    func_vdesk,     FL_CLIENT },
};
#define NUM_NAME_TO_FUNC (int)(sizeof(name_to_func) / sizeof(name_to_func[0]))

// Map flag name to flag

struct name_to_flags {
	const char *name;
	unsigned flags;
};

static struct name_to_flags name_to_flags[] = {
	{ "up",             FL_UP },
	{ "down",           FL_DOWN },
	{ "left",           FL_LEFT },
	{ "right",          FL_RIGHT },
	{ "top",            FL_TOP },
	{ "bottom",         FL_BOTTOM },
	{ "relative",       FL_RELATIVE },
	{ "rel",            FL_RELATIVE },
	{ "toggle",         FL_TOGGLE },
	{ "vertical",       FL_VERT },
	{ "v",              FL_VERT },
	{ "horizontal",     FL_HORZ },
	{ "h",              FL_HORZ },
};
#define NUM_NAME_TO_FLAGS (int)(sizeof(name_to_flags) / sizeof(name_to_flags[0]))

// Lists of built-in binds.

static struct {
	const char *ctl;
	const char *func;
} control_builtins[] = {
	// Move client
	{ "mask1+k",                "move,relative+up" },
	{ "mask1+j",                "move,relative+down" },
	{ "mask1+h",                "move,relative+left" },
	{ "mask1+l",                "move,relative+right" },
#ifndef QWERTZ_KEYMAP
	{ "mask1+y",                "move,top+left" },
#else
	{ "mask1+z",                "move,top+left" },
#endif
	{ "mask1+u",                "move,top+right" },
	{ "mask1+b",                "move,bottom+left" },
	{ "mask1+n",                "move,bottom+right" },

	// Resize client
	{ "mask1+altmask+k",        "resize,relative+up" },
	{ "mask1+altmask+j",        "resize,relative+down" },
	{ "mask1+altmask+h",        "resize,relative+left" },
	{ "mask1+altmask+l",        "resize,relative+right" },
	{ "mask1+x",                "resize,toggle+v+h" },
	{ "mask1+equal",            "resize,toggle+v" },
	{ "mask1+altmask+equal",    "resize,toggle+h" },

	// Client misc
	{ "mask1+Escape",           "delete" },
	{ "mask1+altmask+Escape",   "kill" },
	{ "mask1+i",                "info" },
	{ "mask1+Insert",           "lower" },
	{ "mask1+KP_Insert",        "lower" },
	{ "mask2+Tab",              "next" },
	{ "mask1+Return",           "spawn" },
	{ "mask1+f",                "fix,toggle" },

	// Virtual desktops
	{ "mask1+1",                "vdesk,0" },
	{ "mask1+2",                "vdesk,1" },
	{ "mask1+3",                "vdesk,2" },
	{ "mask1+4",                "vdesk,3" },
	{ "mask1+5",                "vdesk,4" },
	{ "mask1+6",                "vdesk,5" },
	{ "mask1+7",                "vdesk,6" },
	{ "mask1+8",                "vdesk,7" },
	{ "mask1+a",                "vdesk,toggle" },
	{ "mask1+Left",             "vdesk,relative+down" },
	{ "mask1+Right",            "vdesk,relative+up" },

	// Screen misc
	{ "mask1+d",                "dock,toggle" },

	// Button controls
	{ "button1",                "move" },
	{ "button2",                "resize" },
	{ "button3",                "lower" },
};
#define NUM_CONTROL_BUILTINS (int)(sizeof(control_builtins) / sizeof(control_builtins[0]))

struct bind {
	// KeyPress or ButtonPress
	int type;
	// Bound key or button
	union {
		KeySym key;
		unsigned button;
	} control;
	// Modifier state
	unsigned state;
	// Dispatch function
	func_dispatch func;
	// Control flags
	unsigned flags;
};

static struct list *controls = NULL;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// String-to-value mapping helper functions

static struct name_to_modifier *modifier_by_name(const char *name) {
	for (int i = 0; i < NUM_NAME_TO_MODIFIER; i++) {
		if (!strcmp(name_to_modifier[i].name, name)) {
			return &name_to_modifier[i];
		}
	}
	return NULL;
}

static unsigned button_by_name(const char *name) {
	for (int i = 0; i < NUM_NAME_TO_BUTTON; i++) {
		if (!strcmp(name_to_button[i].name, name)) {
			return name_to_button[i].button;
		}
	}
	return 0;
}

static struct function_def *func_by_name(const char *name) {
	for (int i = 0; i < NUM_NAME_TO_FUNC; i++) {
		if (!strcmp(name_to_func[i].name, name)) {
			return &name_to_func[i];
		}
	}
	return NULL;
}

static unsigned flags_by_name(const char *name) {
	for (int i = 0; i < NUM_NAME_TO_FLAGS; i++) {
		if (!strcmp(name_to_flags[i].name, name)) {
			return name_to_flags[i].flags;
		}
	}
	return 0;
}

// Manage list of binds

void bind_reset(void) {
	// unbind _all_ controls
	while (controls) {
		struct bind *b = controls->data;
		controls = list_delete(controls, b);
		free(b);
	}

	// then rebind what's configured
	for (int i = 0; i < NUM_CONTROL_BUILTINS; i++) {
		bind_control(control_builtins[i].ctl, control_builtins[i].func);
	}
}

void bind_modifier(const char *modname, const char *modstr) {
	if (!modstr)
		return;
	struct name_to_modifier *destm = modifier_by_name(modname);
	if (!destm)
		return;
	destm->value = 0;

	// parse modstr
	char *moddup = xstrdup(modstr);
	for (char *tmp = strtok(moddup, ",+"); tmp; tmp = strtok(NULL, ",+")) {
		// is this a modifier?
		struct name_to_modifier *m = modifier_by_name(tmp);
		if (m) {
			destm->value |= m->value;
			continue;
		}
	}
	free(moddup);
	if (!strcmp(modname, "altmask"))
		altmask = destm->value;
}

void bind_control(const char *ctlname, const char *func) {
	// Parse control string
	char *ctldup = xstrdup(ctlname);
	if (!ctldup)
		return;
	struct bind *newbind = xmalloc(sizeof(*newbind));
	*newbind = (struct bind){0};

	for (char *tmp = strtok(ctldup, ",+"); tmp; tmp = strtok(NULL, ",+")) {
		// is this a modifier?
		struct name_to_modifier *m = modifier_by_name(tmp);
		if (m) {
			newbind->state |= m->value;
			continue;
		}

		// only consider first key or button listed
		if (newbind->type)
			continue;

		// maybe it's a button?
		unsigned b = button_by_name(tmp);
		if (b) {
			newbind->type = ButtonPress;
			newbind->control.button = b;
			continue;
		}

		// ok, see if it's recognised as a key name
		KeySym k = XStringToKeysym(tmp);
		if (k != NoSymbol) {
			newbind->type = KeyPress;
			newbind->control.key = k;
			continue;
		}
	}
	free(ctldup);

	// No known control type?  Abort.
	if (!newbind->type) {
		free(newbind);
		return;
	}

	// always unbind any existing matching control
	for (struct list *l = controls; l; l = l->next) {
		struct bind *b = l->data;
		if (newbind->type == KeyPress && b->state == newbind->state && b->control.key == newbind->control.key) {
			controls = list_delete(controls, b);
			free(b);
			break;
		}
		if (newbind->type == ButtonPress && b->state == newbind->state && b->control.button == newbind->control.button) {
			controls = list_delete(controls, b);
			free(b);
			break;
		}
	}

	// empty function definition implies unbind.  already done, so return.
	if (!func || *func == 0) {
		free(newbind);
		return;
	}

	// parse the second string for function & flags

	char *funcdup = xstrdup(func);
	if (!funcdup)
		return;

	for (char *tmp = strtok(funcdup, ",+"); tmp; tmp = strtok(NULL, ",+")) {
		// function name?
		struct function_def *fn = func_by_name(tmp);
		if (fn) {
			newbind->func = fn->func;
			newbind->flags = fn->flags;
			continue;
		}

		// a simple number?
		if (*tmp >= '0' && *tmp <= '9') {
			newbind->flags &= ~FL_VALUEMASK;
			newbind->flags |= strtol(tmp, NULL, 0) & FL_VALUEMASK;
			continue;
		}

		// treat it as a flag name then
		newbind->flags |= flags_by_name(tmp);
	}

	if (newbind->func) {
		controls = list_prepend(controls, newbind);
	} else {
		free(newbind);
	}

	free(funcdup);
}

static void grab_keysym(KeySym keysym, unsigned modmask, Window w) {
	KeyCode keycode = XKeysymToKeycode(display.dpy, keysym);
	XGrabKey(display.dpy, keycode, modmask, w, True,
		 GrabModeAsync, GrabModeAsync);
	XGrabKey(display.dpy, keycode, modmask|LockMask, w, True,
		 GrabModeAsync, GrabModeAsync);
	if (numlockmask) {
		XGrabKey(display.dpy, keycode, modmask|numlockmask, w, True,
			 GrabModeAsync, GrabModeAsync);
		XGrabKey(display.dpy, keycode, modmask|numlockmask|LockMask, w, True,
			 GrabModeAsync, GrabModeAsync);
	}
}

static void grab_button(unsigned button, unsigned modifiers, Window w) {
	XGrabButton(display.dpy, button, modifiers, w,
		    False, ButtonPressMask | ButtonReleaseMask,
		    GrabModeAsync, GrabModeSync, None, None);
	XGrabButton(display.dpy, button, modifiers|LockMask, w,
		    False, ButtonPressMask | ButtonReleaseMask,
		    GrabModeAsync, GrabModeSync, None, None);
	if (numlockmask) {
		XGrabButton(display.dpy, button, modifiers|numlockmask, w,
			    False, ButtonPressMask | ButtonReleaseMask,
			    GrabModeAsync, GrabModeSync, None, None);
		XGrabButton(display.dpy, button, modifiers|numlockmask|LockMask, w,
			    False, ButtonPressMask | ButtonReleaseMask,
			    GrabModeAsync, GrabModeSync, None, None);
	}
}

void bind_grab_for_screen(struct screen *s) {
	// Release any previous grabs
	XUngrabKey(display.dpy, AnyKey, AnyModifier, s->root);

	for (struct list *l = controls; l; l = l->next) {
		struct bind *b = l->data;
		if (b->type == KeyPress) {
			grab_keysym(b->control.key, b->state, s->root);
		}
	}
}

void bind_grab_for_client(struct client *c) {
	// Button binds way less configurable than key binds for now.
	// Modifiers in the bind are ignored, and we ONLY use 'mask2' and
	// 'mask2+altmask'.

	for (struct list *l = controls; l; l = l->next) {
		struct bind *b = l->data;
		if (b->type == ButtonPress) {
			grab_button(b->control.button, grabmask2, c->parent);
			grab_button(b->control.button, grabmask2|altmask, c->parent);
		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Handle keyboard events.

void bind_handle_key(XKeyEvent *e) {
	KeySym key = XkbKeycodeToKeysym(display.dpy, e->keycode, 0, 0);

	for (struct list *l = controls; l; l = l->next) {
		struct bind *bind = l->data;
		if (bind->type == KeyPress && key == bind->control.key && (e->state & KEY_STATE_MASK) == bind->state) {
			void *sptr = NULL;
			if (bind->flags & FL_CLIENT) {
				sptr = current;
				if (!sptr)
					return;
			} else if (bind->flags & FL_SCREEN) {
				sptr = find_current_screen();
				if (!sptr)
					return;
			}
			bind->func(sptr, (XEvent *)e, bind->flags);
			break;
		}
	}
}

// Handle mousebutton events.

void bind_handle_button(XButtonEvent *e) {
	for (struct list *l = controls; l; l = l->next) {
		struct bind *bind = l->data;
		if (bind->type == ButtonPress && e->button == bind->control.button && (e->state & BUTTON_STATE_MASK) == bind->state) {
			void *sptr = NULL;
			if (bind->flags & FL_CLIENT) {
				sptr = find_client(e->window);
			} else if (bind->flags & FL_SCREEN) {
				sptr = find_current_screen();
			}
			if (!sptr)
				return;
			bind->func(sptr, (XEvent *)e, bind->flags);
			break;
		}
	}
}
