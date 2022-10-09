/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

#ifndef EVILWM_EWMH_H_
#define EVILWM_EWMH_H_

// Extended Window Manager Hints

// https://specifications.freedesktop.org/wm-spec/latest/

#include <X11/X.h>

// EWMH window type bits
#define EWMH_WINDOW_TYPE_DESKTOP (1<<0)
#define EWMH_WINDOW_TYPE_DOCK    (1<<1)
#define EWMH_WINDOW_TYPE_NOTIFICATION (1<<2)

struct client;
struct screen;

void ewmh_set_screen_workarea(struct screen *s);
void ewmh_set_net_client_list(struct screen *s);
void ewmh_set_net_client_list_stacking(struct screen *s);
void ewmh_set_net_current_desktop(struct screen *s);

void ewmh_set_allowed_actions(struct client *c);
void ewmh_remove_allowed_actions(struct client *c);
void ewmh_withdraw_client(struct client *c);

void ewmh_set_net_wm_desktop(struct client *c);
unsigned ewmh_get_net_wm_window_type(Window w);
void ewmh_set_net_wm_state(struct client *c);
void ewmh_set_net_frame_extents(Window w, unsigned long border);

#endif
