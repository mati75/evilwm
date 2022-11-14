/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// main() function parses options and kicks off the main event loop.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include <X11/X.h>
#include <X11/Xlib.h>

#include "bind.h"
#include "client.h"
#include "display.h"
#include "events.h"
#include "evilwm.h"
#include "list.h"
#include "log.h"
#include "xalloc.h"
#include "xconfig.h"

#define CONFIG_FILE ".evilwmrc"

#define xstr(s) str(s)
#define str(s) #s

// WM will manage/process events/unmanage until this flag is set
static _Bool wm_exit;

struct options option;

static struct list *opt_bind = NULL;
static char *opt_grabmask1 = NULL;
static char *opt_grabmask2 = NULL;
static char *opt_altmask = NULL;

unsigned numlockmask = 0;

struct list *applications = NULL;

static void set_bind(const char *arg);
static void set_app(const char *arg);
static void set_app_geometry(const char *arg);
static void set_app_dock(void);
static void set_app_vdesk(const char *arg);
static void set_app_fixed(void);

static struct xconfig_option evilwm_options[] = {
	{ XCONFIG_STRING,   "fn",           { .s = &option.font } },
	{ XCONFIG_STRING,   "display",      { .s = &option.display } },
	{ XCONFIG_UINT,     "numvdesks",    { .u = &option.vdesks } },
	{ XCONFIG_STRING,   "fg",           { .s = &option.fg } },
	{ XCONFIG_STRING,   "bg",           { .s = &option.bg } },
	{ XCONFIG_STRING,   "fc",           { .s = &option.fc } },
	{ XCONFIG_INT,      "bw",           { .i = &option.bw } },
	{ XCONFIG_STR_LIST, "term",         { .sl = &option.term } },
	{ XCONFIG_INT,      "snap",         { .i = &option.snap } },
	{ XCONFIG_BOOL,     "wholescreen",  { .i = &option.wholescreen } },
	{ XCONFIG_STRING,   "mask1",        { .s = &opt_grabmask1 } },
	{ XCONFIG_STRING,   "mask2",        { .s = &opt_grabmask2 } },
	{ XCONFIG_STRING,   "altmask",      { .s = &opt_altmask } },
	{ XCONFIG_CALL_1,   "bind",         { .c1 = &set_bind } },
	{ XCONFIG_CALL_1,   "app",          { .c1 = &set_app } },
	{ XCONFIG_CALL_1,   "geometry",     { .c1 = &set_app_geometry } },
	{ XCONFIG_CALL_1,   "g",            { .c1 = &set_app_geometry } },
	{ XCONFIG_CALL_0,   "dock",         { .c0 = &set_app_dock } },
	{ XCONFIG_CALL_1,   "vdesk",        { .c1 = &set_app_vdesk } },
	{ XCONFIG_CALL_1,   "v",            { .c1 = &set_app_vdesk } },
	{ XCONFIG_CALL_0,   "fixed",        { .c0 = &set_app_fixed } },
	{ XCONFIG_CALL_0,   "f",            { .c0 = &set_app_fixed } },
	{ XCONFIG_CALL_0,   "s",            { .c0 = &set_app_fixed } },
#ifdef SOLIDDRAG
	{ XCONFIG_BOOL,     "nosoliddrag",  { .i = &option.no_solid_drag } },
#endif
	{ XCONFIG_END, NULL, { .i = NULL } }
};

static void handle_signal(int signo);

static void helptext(void) {
	puts(
"Usage: evilwm [OPTION]...\n"
"evilwm is a minimalist window manager for X11.\n"
"\n Options:\n"
"  --display DISPLAY   X display [from environment]\n"
"  --term PROGRAM      binary used to spawn terminal [" DEF_TERM "]\n"
"  --fn FONTNAME       font used to display text [" DEF_FONT "]\n"
"  --fg COLOUR         colour of active window frames [" DEF_FG "]\n"
"  --fc COLOUR         colour of fixed window frames [" DEF_FC "]\n"
"  --bg COLOUR         colour of inactive window frames [" DEF_BG "]\n"
"  --bw PIXELS         window border width [" xstr(DEF_BW) "]\n"
"  --snap PIXELS       snap distance when dragging windows [0; disabled]\n"
"  --wholescreen       ignore monitor geometries when maximising\n"
"  --numvdesks N       total number of virtual desktops [8]\n"
#ifdef SOLIDDRAG
"  --nosoliddrag       draw outline when moving or resizing\n"
#endif
"  --mask1 MASK        modifiers for most keyboard controls [control+alt]\n"
"  --mask2 MASK        modifiers for mouse button controls [alt]\n"
"  --altmask MASK      modifiers selecting alternate control behaviour\n"
"  --bind CTL[=FUNC]   bind (or unbind) input to window manager function\n"

"\n Application matching options:\n"
"  --app NAME/CLASS      match application by instance name & class\n"
"    -g, --geometry GEOM   apply X geometry to matched application\n"
"        --dock            treat matched app as a dock\n"
"    -v, --vdesk VDESK     move app to numbered vdesk (indexed from 0)\n"
"    -f, --fixed           matched app should start fixed\n"

"\n Other options:\n"
"  -h, --help      display this help and exit\n"
"  -V, --version   output version information and exit\n"

"\nWhen binding a control, CTL contains a (case-sensitive) list of modifiers,\n"
"buttons or keys (using the X11 keysym name) and FUNC lists a function\n"
"name and optional extra flags.  List entries can be separated with ','\n"
"or '+'.  If FUNC is missing or empty, the control is unbound.  Modifiers are\n"
"ignored when binding buttons.\n"

"\nModifiers: mask1, mask2, altmask, shift, control, alt, mod1..mod5\n"
"Buttons: button1..button5\n"
"Functions: delete, dock, fix, info, kill, lower, move, next, resize,\n"
"           spawn, vdesk\n"
"Flags: up, down, left, right, top, bottom, relative (rel), drag, toggle,\n"
"       vertical (v), horizontal (h)\n"

	);
}

static const char *default_options[] = {
	"display",
	"term " DEF_TERM,
	"fn " DEF_FONT,
	"fg " DEF_FG,
	"bg " DEF_BG,
	"bw " xstr(DEF_BW),
	"fc " DEF_FC,
	"numvdesks 8",
};
#define NUM_DEFAULT_OPTIONS (sizeof(default_options)/sizeof(default_options[0]))

int main(int argc, char *argv[]) {
	struct sigaction act;
	int argn = 1, ret;
	Window old_current_window = None;

	act.sa_handler = handle_signal;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGHUP, &act, NULL);

	// Run until something signals to quit.
	wm_exit = 0;
	while (!wm_exit) {

		// Default options
		option = (struct options){0};
		for (unsigned i = 0; i < NUM_DEFAULT_OPTIONS; i++)
			xconfig_parse_line(evilwm_options, default_options[i]);

		// Read configuration file
		const char *home = getenv("HOME");
		if (home) {
			char *conffile = xmalloc(strlen(home) + sizeof(CONFIG_FILE) + 2);
			strcpy(conffile, home);
			strcat(conffile, "/" CONFIG_FILE);
			xconfig_parse_file(evilwm_options, conffile);
			free(conffile);
		}

		// Parse CLI options
		ret = xconfig_parse_cli(evilwm_options, argc, argv, &argn);
		if (ret == XCONFIG_MISSING_ARG) {
			fprintf(stderr, "%s: missing argument to `%s'\n", argv[0], argv[argn]);
			fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
			exit(1);
		} else if (ret == XCONFIG_BAD_OPTION) {
			if (0 == strcmp(argv[argn], "-h")
			    || 0 == strcmp(argv[argn], "--help")) {
				helptext();
				exit(0);
			} else if (0 == strcmp(argv[argn], "-V")
				   || 0 == strcmp(argv[argn], "--version")) {
				LOG_INFO("evilwm version " VERSION "\n");
				exit(0);
			} else {
				fprintf(stderr, "%s: unrecognised option '%s'\n", argv[0], argv[argn]);
				fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
				exit(1);
			}
		}

		bind_modifier("mask1", opt_grabmask1);
		bind_modifier("mask2", opt_grabmask2);
		bind_modifier("altmask", opt_altmask);

		bind_reset();
		while (opt_bind) {
			char *arg = opt_bind->data;
			opt_bind = list_delete(opt_bind, arg);
			char *ctlstr = strtok(arg, "=");
			if (!ctlstr) {
				free(arg);
				continue;
			}
			char *funcstr = strtok(NULL, "");
			bind_control(ctlstr, funcstr);
			free(arg);
		}

		// Open display only if not already open
		if (!display.dpy) {
			display_open();
		}

		// Manage all eligible clients across all screens
		display_manage_clients();

		// Restore "old current window", if known
		if (old_current_window != None) {
			struct client *c = find_client(old_current_window);
			if (c)
				select_client(c);
		}

		// Event loop will run until interrupted
		end_event_loop = 0;
		event_main_loop();

		// Record "old current window" across SIGHUPs
		old_current_window = current ? current->window : None;

		// Important to clean up anything now that might be reallocated
		// after rereading config, re-managing windows, etc.

		// Free any allocated strings in parsed options
		xconfig_free(evilwm_options);

		// Free application configuration
		while (applications) {
			struct application *app = applications->data;
			applications = list_delete(applications, app);
			if (app->res_name)
				free(app->res_name);
			if (app->res_class)
				free(app->res_class);
			free(app);
		}

		display_unmanage_clients();
		XSync(display.dpy, True);
	}

	// Close display
	display_close();

	return 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Option parsing callbacks

static void set_bind(const char *arg) {
	char *argdup = xstrdup(arg);
	if (!argdup)
		return;
	opt_bind = list_prepend(opt_bind, argdup);
}

static void set_app(const char *arg) {
	struct application *new = xmalloc(sizeof(struct application));
	char *tmp;
	new->res_name = new->res_class = NULL;
	new->geometry_mask = 0;
	new->is_dock = 0;
	new->vdesk = VDESK_NONE;
	if ((tmp = strchr(arg, '/'))) {
		*(tmp++) = 0;
	}
	if (*arg) {
		new->res_name = xstrdup(arg);
	}
	if (tmp && *tmp) {
		new->res_class = xstrdup(tmp);
	}
	applications = list_prepend(applications, new);
}

static void set_app_geometry(const char *arg) {
	if (applications) {
		struct application *app = applications->data;
		app->geometry_mask = XParseGeometry(arg,
				&app->x, &app->y, &app->width, &app->height);
	}
}

static void set_app_dock(void) {
	if (applications) {
		struct application *app = applications->data;
		app->is_dock = 1;
	}
}

static void set_app_vdesk(const char *arg) {
	unsigned v = strtoul(arg, NULL, 0);
	if (applications && valid_vdesk(v)) {
		struct application *app = applications->data;
		app->vdesk = v;
	}
}

static void set_app_fixed(void) {
	if (applications) {
		struct application *app = applications->data;
		app->vdesk = VDESK_FIXED;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Signals configured in main() trigger a clean shutdown

static void handle_signal(int signo) {
	if (signo != SIGHUP) {
		wm_exit = 1;
	}
	end_event_loop = 1;
}
