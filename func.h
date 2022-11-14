/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

#ifndef EVILWM_FUNC_H_
#define EVILWM_FUNC_H_

// Function flags

#define FL_VALUEMASK  (0xff)  // 8 bits of value
#define FL_UP       (1 << 8)
#define FL_DOWN     (1 << 9)
#define FL_LEFT     (1 << 10)
#define FL_RIGHT    (1 << 11)
#define FL_TOP      (1 << 12)
#define FL_BOTTOM   (1 << 13)
#define FL_RELATIVE (1 << 14)
#define FL_SCREEN   (1 << 15)
#define FL_CLIENT   (1 << 16)
#define FL_TOGGLE   (1 << 18)

#define FL_CORNERMASK  (FL_LEFT|FL_RIGHT|FL_TOP|FL_BOTTOM)
#define FL_TOPLEFT     (FL_LEFT|FL_TOP)
#define FL_TOPRIGHT    (FL_RIGHT|FL_TOP)
#define FL_BOTTOMLEFT  (FL_LEFT|FL_BOTTOM)
#define FL_BOTTOMRIGHT (FL_RIGHT|FL_BOTTOM)
#define FL_MAX         (FL_LEFT|FL_RIGHT|FL_TOP|FL_BOTTOM)
#define FL_VERT        (FL_TOP|FL_BOTTOM)
#define FL_HORZ        (FL_LEFT|FL_RIGHT)

// Bindable functions

void func_delete(void *, XEvent *, unsigned);
void func_dock(void *, XEvent *, unsigned);
void func_info(void *, XEvent *, unsigned);
void func_lower(void *, XEvent *, unsigned);
void func_move(void *, XEvent *, unsigned);
void func_next(void *, XEvent *, unsigned);
void func_raise(void *, XEvent *, unsigned);
void func_resize(void *, XEvent *, unsigned);
void func_spawn(void *, XEvent *, unsigned);
void func_vdesk(void *, XEvent *, unsigned);

#endif
