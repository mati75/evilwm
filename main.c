/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2021 Ciaran Anscomb <evilwm@6809.org.uk>
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

#include "client.h"
#include "display.h"
#include "events.h"
#include "evilwm.h"
#include "list.h"
#include "log.h"
#include "xalloc.h"
#include "xconfig.h"

#define CONFIG_FILE ".evilwmrc"

struct options option = {
	.bw = DEF_BW,

	.vdesks = 8,
	.snap = 0,
	.wholescreen = 0,

#ifdef SOLIDDRAG
	.no_solid_drag = 0,
#endif
};

static char *opt_grabmask1 = NULL;
static char *opt_grabmask2 = NULL;
static char *opt_altmask = NULL;

unsigned numlockmask = 0;
unsigned grabmask1 = ControlMask|Mod1Mask;
unsigned grabmask2 = Mod1Mask;
unsigned altmask = ShiftMask;

struct list *applications = NULL;

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

static unsigned parse_modifiers(char *s);
static void handle_signal(int signo);

static void helptext(void) {
	puts(
"usage: evilwm [-display display] [-term termprog] [-fn fontname]\n"
"              [-fg foreground] [-fc fixed] [-bg background] [-bw borderwidth]\n"
"              [-mask1 modifiers] [-mask2 modifiers] [-altmask modifiers]\n"
"              [-snap num] [-numvdesks num] [-wholescreen]\n"
"              [-app name/class] [-g geometry] [-dock] [-v vdesk] [-fixed]\n"
"             "
#ifdef SOLIDDRAG
" [-nosoliddrag]"
#endif
" [-V]"
	);
}

int main(int argc, char *argv[]) {
	struct sigaction act;
	int argn = 1, ret;

	act.sa_handler = handle_signal;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGHUP, &act, NULL);

	// Default options
	xconfig_set_option(evilwm_options, "display", "");
	xconfig_set_option(evilwm_options, "fn", DEF_FONT);
	xconfig_set_option(evilwm_options, "fg", DEF_FG);
	xconfig_set_option(evilwm_options, "bg", DEF_BG);
	xconfig_set_option(evilwm_options, "fc", DEF_FC);
	xconfig_set_option(evilwm_options, "term", DEF_TERM);

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
			helptext();
			exit(1);
		}
	}

	if (opt_grabmask1)
		grabmask1 = parse_modifiers(opt_grabmask1);
	if (opt_grabmask2)
		grabmask2 = parse_modifiers(opt_grabmask2);
	if (opt_altmask)
		altmask = parse_modifiers(opt_altmask);

	if (!display.dpy) {
		// Open display.  Manages all eligible clients across all screens.
		display_open();
	}

	// Run event look until something signals to quit.
	wm_exit = 0;
	event_main_loop();

	// Close display.  This will cleanly unmanage all windows.
	display_close();

	return 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Option parsing callbacks

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
	if (strlen(arg) > 0) {
		new->res_name = xmalloc(strlen(arg)+1);
		strcpy(new->res_name, arg);
	}
	if (tmp && strlen(tmp) > 0) {
		new->res_class = xmalloc(strlen(tmp)+1);
		strcpy(new->res_class, tmp);
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
	unsigned v = atoi(arg);
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

// Used for overriding the default key modifiers

static unsigned parse_modifiers(char *s) {
	static struct {
		const char *name;
		unsigned mask;
	} modifiers[9] = {
		{ "shift", ShiftMask },
		{ "lock", LockMask },
		{ "control", ControlMask },
		{ "alt", Mod1Mask },
		{ "mod1", Mod1Mask },
		{ "mod2", Mod2Mask },
		{ "mod3", Mod3Mask },
		{ "mod4", Mod4Mask },
		{ "mod5", Mod5Mask }
	};

	char *tmp = strtok(s, ",+");
	if (!tmp)
		return 0;

	unsigned ret = 0;
	do {
		for (int i = 0; i < 9; i++) {
			if (!strcmp(modifiers[i].name, tmp))
				ret |= modifiers[i].mask;
		}
		tmp = strtok(NULL, ",+");
	} while (tmp);

	return ret;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Signals configured in main() trigger a clean shutdown

static void handle_signal(int signo) {
	(void)signo;  // unused
	wm_exit = 1;
}
