#ifndef TOYC_WAYLAND_MIN_H
#define TOYC_WAYLAND_MIN_H

#include "tlibc_types.h"

struct toywl;

struct toywl_frame {
    uint32_t *pixels;
    int width;
    int height;
    int stride;
};

#define TOYWL_MAX_KEY_EVENTS 16

struct toywl_key_event {
    unsigned int key;
    int pressed;
};

struct toywl_input {
    int close_requested;
    int resized;
    int width;
    int height;
    int pointer_x;
    int pointer_y;
    int pointer_moved;
    int relative_x;
    int relative_y;
    int relative_moved;
    int pointer_lock_changed;
    int pointer_locked;
    unsigned int button;
    int button_pressed;
    int keyboard_focus_changed;
    int keyboard_focused;
    int key_event_count;
    struct toywl_key_event key_events[TOYWL_MAX_KEY_EVENTS];
};

/* A deliberately small, freestanding Wayland client for software-rendered
 * xdg-toplevel windows.  It implements only the protocol used by Toyc apps. */
struct toywl *toywl_open(const char *title, int width, int height);
int toywl_dispatch(struct toywl *wl, struct toywl_input *input, int timeout_ms);
int toywl_begin_frame(struct toywl *wl, struct toywl_frame *frame);
int toywl_present(struct toywl *wl);
int toywl_pointer_lock_supported(struct toywl *wl);
int toywl_set_pointer_lock(struct toywl *wl, int locked);
int toywl_set_pointer_confine(struct toywl *wl, int confined);
void toywl_close(struct toywl *wl);

#endif
