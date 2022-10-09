/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Logging support functions

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "display.h"
#include "log.h"

#if defined(DEBUG) || defined(XDEBUG)
int log_indent = 0;
#endif

#ifdef DEBUG

const char *debug_atom_name(Atom a) {
	static char buf[48];
	char *atom_name = XGetAtomName(display.dpy, a);
	strncpy(buf, atom_name, sizeof(buf));
	buf[sizeof(buf)-1] = 0;
	return buf;
}

#endif

#ifdef XDEBUG

static const char *gravity_string(int gravity) {
	const char *gravities[12] = {
		"ForgetGravity",
		"NorthWestGravity",
		"NorthGravity",
		"NorthEastGravity",
		"WestGravity",
		"CenterGravity",
		"EastGravity",
		"SouthWestGravity",
		"SouthGravity",
		"SouthEastGravity",
		"StaticGravity",
		"Unknown"
	};
	return ((unsigned)gravity < 11) ? gravities[gravity] : gravities[11];
}

void debug_wm_normal_hints(XSizeHints *size) {
	if (size->flags & 15) {
		LOG_INDENT();
		if (size->flags & USPosition) {
			LOG_XDEBUG_("USPosition ");
		}
		if (size->flags & USSize) {
			LOG_XDEBUG_("USSize ");
		}
		if (size->flags & PPosition) {
			LOG_XDEBUG_("PPosition ");
		}
		if (size->flags & PSize) {
			LOG_XDEBUG_("PSize");
		}
		LOG_XDEBUG_("\n");
	}
	if (size->flags & PMinSize) {
		LOG_XDEBUG("PMinSize: min_width = %d, min_height = %d\n", size->min_width, size->min_height);
	}
	if (size->flags & PMaxSize) {
		LOG_XDEBUG("PMaxSize: max_width = %d, max_height = %d\n", size->max_width, size->max_height);
	}
	if (size->flags & PResizeInc) {
		LOG_XDEBUG("PResizeInc: width_inc = %d, height_inc = %d\n",
			   size->width_inc, size->height_inc);
	}
	if (size->flags & PAspect) {
		LOG_XDEBUG("PAspect: min_aspect = %d/%d, max_aspect = %d/%d\n",
			   size->min_aspect.x, size->min_aspect.y,
			   size->max_aspect.x, size->max_aspect.y);
	}
	if (size->flags & PBaseSize) {
		LOG_XDEBUG("PBaseSize: base_width = %d, base_height = %d\n",
			   size->base_width, size->base_height);
	}
	if (size->flags & PWinGravity) {
		LOG_XDEBUG("PWinGravity: %s\n", gravity_string(size->win_gravity));
	}
}

static const char *map_state_string(int map_state) {
	const char *map_states[4] = {
		"IsUnmapped",
		"IsUnviewable",
		"IsViewable",
		"Unknown"
	};
	return ((unsigned)map_state < 3)
	       ? map_states[map_state]
	       : map_states[3];
}

void debug_window_attributes(XWindowAttributes *attr) {
	LOG_XDEBUG("(%s) %dx%d+%d+%d, bw = %d\n", map_state_string(attr->map_state),
	           attr->width, attr->height, attr->x, attr->y, attr->border_width);
}

#endif
