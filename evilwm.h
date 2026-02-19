/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2025 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Default option values

#define DEF_FONT        "variable"
#define DEF_FG          "goldenrod"
#define DEF_BG          "grey50"
#define DEF_BW          1
#define DEF_FC          "blue"
#ifdef DEBIAN
#define DEF_TERM        "x-terminal-emulator"
#else
#define DEF_TERM        "xterm"
#endif

// Options

struct options {
	// Display string (e.g., ":0")
	char *display;

	// Text font
	char *font;

	// Border colours
	char *fg;  // selected (foreground)
	char *bg;  // unselected (background)
	char *fc;  // fixed & selected

	// Border width
	int bw;

	// Number of rows and columns of virtual desktops
	unsigned vdeskrows;
	unsigned vdeskcolumns;

	// Snap to border flag
	int snap;

	// Whole screen flag (ignore monitor information)
	int wholescreen;

#ifdef SOLIDDRAG
	// Solid drag disabled flag
	int no_solid_drag;
#endif

	// NULL-terminated array passed to execvp() to launch terminal
	char **term;
};

extern struct options option;

#ifdef SOLIDDRAG
# define OPTION_NO_SOLID_DRAG (option.no_solid_drag)
#else
# define OPTION_NO_SOLID_DRAG (1)
#endif

extern unsigned numlockmask;

// Application matching

struct application {
	char *res_name;
	char *res_class;
	int geometry_mask;
	_Bool ignore_position;
	_Bool ignore_border;
	int x, y;
	unsigned width, height;
	int is_dock;
	char *vdesk;
};

extern struct list *applications;
