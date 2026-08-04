#ifndef TOYC_TOY_WINDOW_H
#define TOYC_TOY_WINDOW_H

#include "tlibc_types.h"

struct toy_window;

struct toy_surface {
    uint32_t *pixels;
    int width;
    int height;
    int stride;
};

#define TOY_WINDOW_MAX_KEY_EVENTS 16
#define TOY_INPUT_KEY_COUNT 256

struct toy_key_event {
    unsigned int key;
    int pressed;
};

struct toy_window_events {
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
    struct toy_key_event key_events[TOY_WINDOW_MAX_KEY_EVENTS];
};

/* Platform-neutral window surface used by software-rendered Toyc apps. */
struct toy_window *toy_window_open(const char *title, int width, int height);
int toy_window_poll(struct toy_window *window, struct toy_window_events *events,
                    int timeout_ms);
int toy_window_begin_frame(struct toy_window *window, struct toy_surface *surface);
int toy_window_present(struct toy_window *window);
int toy_window_pointer_lock_supported(struct toy_window *window);
int toy_window_set_pointer_lock(struct toy_window *window, int locked);
int toy_window_set_pointer_confine(struct toy_window *window, int confined);
void toy_window_close(struct toy_window *window);

#endif
