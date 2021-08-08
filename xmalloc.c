/*

Memory allocation with checking

Copyright 2014-2018 Ciaran Anscomb

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "xalloc.h"

void *xmalloc(size_t s) {
	void *mem = malloc(s);
	if (!mem) {
		perror(NULL);
		exit(EXIT_FAILURE);
	}
	return mem;
}

void *xzalloc(size_t s) {
	void *mem = xmalloc(s);
	memset(mem, 0, s);
	return mem;
}

void *xrealloc(void *p, size_t s) {
	void *mem = realloc(p, s);
	if (!mem && s != 0) {
		perror(NULL);
		exit(EXIT_FAILURE);
	}
	return mem;
}

void *xmemdup(const void *p, size_t s) {
	if (!p)
		return NULL;
	void *mem = xmalloc(s);
	memcpy(mem, p, s);
	return mem;
}

char *xstrdup(const char *str) {
	return xmemdup(str, strlen(str) + 1);
}
