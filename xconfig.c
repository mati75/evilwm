/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb
 * see README for license and other details. */

// Configuration parsing.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "xalloc.h"
#include "xconfig.h"

// Break a space-separated string into an array of strings.
// Backslash escapes next character.

static char **split_string(const char *arg) {
	int nelem = 0, elem = 0;
	char **list = NULL;
	char *string, *head, *tail;

	head = tail = string = malloc(strlen(arg) + 1);
	if (string == NULL)
		return NULL;

	for (;;) {
		if (*arg == '\\' && *(arg+1) != 0) {
			arg++;
			*(tail++) = *(arg++);
		} else if (*arg == 0 || isspace(*arg)) {
			*tail = 0;
			if (*head) {
				if ((elem + 1) >= nelem) {
					char **nlist;
					nelem += 4;
					nlist = realloc(list, nelem * sizeof(char *));
					if (nlist == NULL) {
						if (list)
							free(list);
						free(string);
						return NULL;
					}
					list = nlist;
				}
				list[elem++] = head;
				tail++;
				head = tail;
			}
			while (isspace(*arg)) {
				arg++;
			}
			if (*arg == 0) {
				break;
			}
		} else {
			*(tail++) = *(arg++);
		}
	}
	if (elem == 0) {
		free(string);
		return NULL;
	}
	list[elem] = NULL;
	return list;
}

// Find option by name in list.  List terminated by XCONFIG_END entry.

static struct xconfig_option *find_option(struct xconfig_option *options,
		const char *opt) {
	for (int i = 0; options[i].type != XCONFIG_END; i++) {
		if (0 == strcmp(options[i].name, opt)) {
			return &options[i];
		}
	}
	return NULL;
}

static void unset_option(struct xconfig_option *option) {
	switch (option->type) {
	case XCONFIG_STRING:
		if (*(char **)option->dest.s) {
			free(*(char **)option->dest.s);
			*(char **)option->dest.s = NULL;
		}
		break;
	case XCONFIG_STR_LIST:
		if (*(char ***)option->dest.sl) {
			if ((*(char ***)option->dest.sl)[0]) {
				free((*(char ***)option->dest.sl)[0]);
			}
			free(*(char ***)option->dest.sl);
			*(char ***)option->dest.sl = NULL;
		}
		break;
	default:
		break;
	}
}

// Store option value in memory according to its type

static void set_option(struct xconfig_option *option, const char *arg) {
	unset_option(option);
	switch (option->type) {
		case XCONFIG_BOOL:
			*(int *)option->dest.i = 1;
			break;
		case XCONFIG_INT:
			*(int *)option->dest.i = strtol(arg, NULL, 0);
			break;
		case XCONFIG_UINT:
			*(unsigned *)option->dest.u = strtoul(arg, NULL, 0);
			break;
		case XCONFIG_STRING:
			*(char **)option->dest.s = xstrdup(arg);
			break;
		case XCONFIG_STR_LIST:
			*(char ***)option->dest.sl = split_string(arg);
			break;
		case XCONFIG_CALL_0:
			((void (*)(void))option->dest.c0)();
			break;
		case XCONFIG_CALL_1:
			((void (*)(const char *))option->dest.c1)(arg);
			break;
		default:
			break;
	}
}

void xconfig_parse_line(struct xconfig_option *options, const char *line) {
	// skip leading spaces
	while (isspace((int)*line))
		line++;

	// end of line or comment?
	if (*line == 0 || *line == '#')
		return;

	// from here on, work on a copy of the string
	char *linedup = xstrdup(line);

	// whitespace separates option from arguments
	char *optstr = strtok(linedup, "\t\n\v\f\r ");
	if (optstr == NULL) {
		goto done;
	}

	struct xconfig_option *opt = find_option(options, optstr);
	if (opt == NULL) {
		LOG_INFO("Ignoring unknown option `%s'\n", optstr);
		goto done;
	}

	char *arg;
	if (opt->type == XCONFIG_STR_LIST) {
		// special case: spaces here mean something
		arg = strtok(NULL, "\n\v\f\r");
		while (isspace(*arg)) {
			arg++;
		}
	} else {
		arg = strtok(NULL, "\t\n\v\f\r ");
	}

	set_option(opt, arg ? arg : "");
done:
	free(linedup);
	return;
}

// Simple parser: one directive per line, "option argument"

enum xconfig_result xconfig_parse_file(struct xconfig_option *options,
		const char *filename) {
	char buf[256];
	char *line;
	FILE *cfg;
	cfg = fopen(filename, "r");
	if (cfg == NULL) return XCONFIG_FILE_ERROR;

	while ((line = fgets(buf, sizeof(buf), cfg))) {
		xconfig_parse_line(options, line);
	}
	fclose(cfg);
	return XCONFIG_OK;
}

// Command line argument processing

enum xconfig_result xconfig_parse_cli(struct xconfig_option *options,
		int argc, char **argv, int *argn) {
	int _argn;
	const char *optstr;
	_argn = argn ? *argn : 1;

	while (_argn < argc) {
		// No leading '-' means end of options
		if (argv[_argn][0] != '-') {
			break;
		}

		// "--" ends options, per tradition
		if (0 == strcmp("--", argv[_argn])) {
			_argn++;
			break;
		}

		optstr = argv[_argn]+1;
		// Allow double '-' to introduce option
		if (*optstr == '-')
			optstr++;

		struct xconfig_option *opt = find_option(options, optstr);
		if (opt == NULL) {
			if (argn) *argn = _argn;
			return XCONFIG_BAD_OPTION;
		}

		// Option types with no argument
		if (opt->type == XCONFIG_BOOL || opt->type == XCONFIG_CALL_0) {
			set_option(opt, NULL);
			_argn++;
			continue;
		}

		// Option types with one argument
		if ((_argn + 1) >= argc) {
			if (argn)
				*argn = _argn;
			return XCONFIG_MISSING_ARG;
		}
		set_option(opt, argv[_argn+1]);
		_argn += 2;
	}

	if (argn) {
		*argn = _argn;
	}
	return XCONFIG_OK;
}

void xconfig_free(struct xconfig_option *options) {
	for (int i = 0; options[i].type != XCONFIG_END; i++) {
		unset_option(&options[i]);
	}
}
