/* Wayland implementation of the platform-neutral Toyc window interface. */

#include "toy_window.h"
#include "wayland_min.h"
#include "core.h"
#include "string.h"
#include "tlibc_compat.h"

struct toy_window {
    struct toywl *wayland;
};

struct toy_window *toy_window_open(const char *title, int width, int height)
{
    struct toy_window *window = tlibc_malloc(sizeof(struct toy_window));
    if (!window) return NULL;
    window->wayland = toywl_open(title, width, height);
    if (!window->wayland) {
        tlibc_free(window);
        return NULL;
    }
    return window;
}

int toy_window_poll(struct toy_window *window, struct toy_window_events *events,
                    int timeout_ms)
{
    struct toywl_input input;
    int result;
    if (!window) return -1;
    memset(&input, 0, sizeof(input));
    result = toywl_dispatch(window->wayland, &input, timeout_ms);
    if (events) {
        events->close_requested = input.close_requested;
        events->resized = input.resized;
        events->width = input.width;
        events->height = input.height;
        events->pointer_x = input.pointer_x;
        events->pointer_y = input.pointer_y;
        events->pointer_moved = input.pointer_moved;
        events->relative_x = input.relative_x;
        events->relative_y = input.relative_y;
        events->relative_moved = input.relative_moved;
        events->pointer_lock_changed = input.pointer_lock_changed;
        events->pointer_locked = input.pointer_locked;
        events->button = input.button;
        events->button_pressed = input.button_pressed;
        events->button_serial = input.button_serial;
        events->keyboard_focus_changed = input.keyboard_focus_changed;
        events->keyboard_focused = input.keyboard_focused;
        events->key_event_count = input.key_event_count;
        for (int i = 0; i < input.key_event_count; i++) {
            events->key_events[i].key = input.key_events[i].key;
            events->key_events[i].pressed = input.key_events[i].pressed;
        }
    }
    return result;
}

int toy_window_begin_frame(struct toy_window *window, struct toy_surface *surface)
{
    struct toywl_frame frame;
    int result;
    if (!window || !surface) return -1;
    result = toywl_begin_frame(window->wayland, &frame);
    if (result > 0) {
        surface->pixels = frame.pixels;
        surface->width = frame.width;
        surface->height = frame.height;
        surface->stride = frame.stride;
    }
    return result;
}

int toy_window_present(struct toy_window *window)
{
    if (!window) return -1;
    return toywl_present(window->wayland);
}

int toy_window_pointer_lock_supported(struct toy_window *window)
{
    return window && toywl_pointer_lock_supported(window->wayland);
}

int toy_window_set_pointer_lock(struct toy_window *window, int locked)
{
    if (!window) return -1;
    return toywl_set_pointer_lock(window->wayland, locked);
}

int toy_window_set_pointer_confine(struct toy_window *window, int confined)
{
    if (!window) return -1;
    return toywl_set_pointer_confine(window->wayland, confined);
}

int toy_window_move(struct toy_window *window, uint32_t serial)
{
    if (!window) return -1;
    return toywl_move(window->wayland, serial);
}

void toy_window_close(struct toy_window *window)
{
    if (!window) return;
    toywl_close(window->wayland);
    tlibc_free(window);
}
