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

struct toywl_input {
    int close_requested;
    int resized;
    int width;
    int height;
    int pointer_x;
    int pointer_y;
    int pointer_moved;
    unsigned int button;
    int button_pressed;
    unsigned int key;
    int key_pressed;
};

/* A deliberately small, freestanding Wayland client for software-rendered
 * xdg-toplevel windows.  It implements only the protocol used by Toyc apps. */
struct toywl *toywl_open(const char *title, int width, int height);
int toywl_dispatch(struct toywl *wl, struct toywl_input *input, int timeout_ms);
int toywl_begin_frame(struct toywl *wl, struct toywl_frame *frame);
int toywl_present(struct toywl *wl);
void toywl_close(struct toywl *wl);

#endif
