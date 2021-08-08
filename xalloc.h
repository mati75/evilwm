/*

Memory allocation with checking

Copyright 2014-2018 Ciaran Anscomb

A small set of convenience functions that wrap standard system calls and
provide out of memory checking.  See Gnulib for a far more complete set.

*/

#ifndef EVILWM_XALLOC_H_
#define EVILWM_XALLOC_H_

#include <stddef.h>

void *xmalloc(size_t s);
void *xzalloc(size_t s);
void *xrealloc(void *p, size_t s);

void *xmemdup(const void *p, size_t s);
char *xstrdup(const char *str);

#endif
