/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Client management.
//
// For each managed window, a structure is held in memory collecting the
// window, another window into which the original is "reparented" (parent
// window), and various associated metadata.

#ifndef EVILWM_CLIENT_H_
#define EVILWM_CLIENT_H_

struct list;
struct screen;
struct monitor;

// Maximise flags
#define MAXIMISE_HORZ   (1<<0)
#define MAXIMISE_VERT   (1<<1)
#define MAXIMISE_SCREEN (1<<2)  // maximise to screen, not monitor

// Virtual desktop macros
#define VDESK_NONE  (0xfffffffe)
#define VDESK_FIXED (0xffffffff)
#define VDESK_MAX   (option.vdesks - 1)
#define valid_vdesk(v) ((v) == VDESK_FIXED || (v) < option.vdesks)

struct client {
	Window window;  // actual application window
	Window parent;  // parent window that we control
	struct screen *screen;  // screen this client is on
	Colormap cmap;  // colourmap to install when focussed

	// Virtual desktop
	unsigned vdesk;

	// Sometimes unmap events occur that we know aren't the client
	// disappearing, flagged here:
	int ignore_unmap;

	// Geometry
	int x, y, width, height;
	int normal_border;  // normal border when unmaximised
	int border;  // current border

	// Old geometry while maximising
	int oldx, oldy, oldw, oldh;

	// Old border width - only used to restore when quitting
	int old_border;

	// Old monitor offset as proportion of monitor geometry
	double mon_offx, mon_offy;

	// Flag set when we need to remove client from management
	int remove;

	// Various window metadata determined by examining properties
	int min_width, min_height;
	int max_width, max_height;
	int width_inc, height_inc;
	int base_width, base_height;
	int win_gravity_hint;
	int win_gravity;
	int is_dock;
};

// Client tracking information
extern struct list *clients_tab_order;
extern struct list *clients_mapping_order;
extern struct list *clients_stacking_order;
extern struct client *current;

#define is_fixed(c) (c->vdesk == VDESK_FIXED)

// client_new.c: newly manage a window

void client_manage_new(Window w, struct screen *s);
long get_wm_normal_hints(struct client *c);
void get_window_type(struct client *c);
void update_window_type_flags(struct client *c, unsigned type);

// client_move.c: user window manipulation

void client_resize_sweep(struct client *c, unsigned button);
void client_move_drag(struct client *c, unsigned button);
void client_show_info(struct client *c, XEvent *e);
void client_moveresize(struct client *c);
void client_moveresizeraise(struct client *c);
void client_maximise(struct client *c, int action, int hv);
void client_select_next(void);

// client.c: various other client functions

struct client *find_client(Window w);
struct monitor *client_monitor(struct client *c, Bool *intersects);
void client_hide(struct client *c);
void client_show(struct client *c);
void client_raise(struct client *c);
void client_lower(struct client *c);
void client_gravitate(struct client *c, int bw);
void select_client(struct client *c);
void client_to_vdesk(struct client *c, unsigned vdesk);
void remove_client(struct client *c);

void send_config(struct client *c);
void send_wm_delete(struct client *c, int kill_client);
void set_wm_state(struct client *c, int state);
void set_shape(struct client *c);

#ifdef INFOBANNER
void create_info_window(struct client *c);
void update_info_window(struct client *c);
void remove_info_window(void);
#endif

#endif
