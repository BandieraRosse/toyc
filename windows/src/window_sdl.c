/* SDL2 implementation of the public software-window interface.
 *
 * This file is intentionally not part of the Linux build. It is the first
 * Windows platform unit: Rasterfall keeps rendering into toy_surface and SDL
 * only presents that buffer and translates native events.
 */
#include <SDL2/SDL.h>
#include <windows.h>

#include "toy_window.h"

struct toy_window {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint32_t *pixels;
    int width;
    int height;
    int pointer_locked;
};

/* Linux input numbers are part of the existing game-facing key contract. Keep
 * the values local here so this Windows file does not include Linux headers. */
enum {
    KEY_ESC = 1, KEY_1 = 2, KEY_2 = 3, KEY_BACKSPACE = 14, KEY_TAB = 15,
    KEY_Q = 16, KEY_W = 17, KEY_E = 18, KEY_R = 19,
    KEY_A = 30, KEY_S = 31, KEY_D = 32, KEY_ENTER = 28,
    KEY_LEFTSHIFT = 42, KEY_COMMA = 51, KEY_DOT = 52,
    KEY_SLASH = 53, KEY_SPACE = 57,
    KEY_UP = 103, KEY_LEFT = 105, KEY_RIGHT = 106, KEY_DOWN = 108
};

void toy_window_close(struct toy_window *window);

static void clear_events(struct toy_window_events *events)
{
    if (!events) return;
    for (unsigned int i = 0; i < sizeof(*events); i++)
        ((unsigned char *)events)[i] = 0;
}

static int has_key_event(const struct toy_window_events *events,
                         unsigned int key, int pressed)
{
    for (int i = 0; i < events->key_event_count; i++) {
        if (events->key_events[i].key == key &&
            events->key_events[i].pressed == pressed)
            return 1;
    }
    return 0;
}

static void poll_windows_key_edges(const struct toy_window_events *events,
                                   unsigned int key, int vk, int *previous)
{
    int pressed = (GetAsyncKeyState(vk) & 0x8000) != 0;
    struct toy_window_events *mutable_events = (struct toy_window_events *)events;
    if (pressed == *previous) return;
    *previous = pressed;
    if (has_key_event(events, key, pressed) ||
        mutable_events->key_event_count >= TOY_WINDOW_MAX_KEY_EVENTS)
        return;
    mutable_events->key_events[mutable_events->key_event_count].key = key;
    mutable_events->key_events[mutable_events->key_event_count].pressed = pressed;
    mutable_events->key_event_count++;
}

static void poll_windows_keys(struct toy_window_events *events)
{
    static const struct {
        unsigned int key;
        int vk;
        int *previous;
    } keys[] = {
        {KEY_W, 'W', NULL}, {KEY_A, 'A', NULL}, {KEY_S, 'S', NULL},
        {KEY_D, 'D', NULL}, {KEY_SPACE, VK_SPACE, NULL},
        {KEY_LEFTSHIFT, VK_LSHIFT, NULL}, {KEY_TAB, VK_TAB, NULL},
        {KEY_R, 'R', NULL}, {KEY_E, 'E', NULL},
        {KEY_1, '1', NULL}, {KEY_2, '2', NULL},
        {KEY_LEFT, VK_LEFT, NULL}, {KEY_RIGHT, VK_RIGHT, NULL},
        {KEY_SLASH, VK_OEM_2, NULL},
    };
    static int previous[sizeof(keys) / sizeof(keys[0])];
    size_t i;
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        poll_windows_key_edges(events, keys[i].key, keys[i].vk, &previous[i]);
}

static unsigned int key_code(SDL_Scancode code, SDL_Keycode sym)
{
    /* Prefer SDL keycodes: these are layout-aware on Windows and avoid
     * exposing SDL's physical scancode values to the game. */
    switch (sym) {
    case SDLK_w: return KEY_W;
    case SDLK_a: return KEY_A;
    case SDLK_s: return KEY_S;
    case SDLK_d: return KEY_D;
    case SDLK_SPACE: return KEY_SPACE;
    case SDLK_ESCAPE: return KEY_ESC;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return KEY_ENTER;
    case SDLK_LEFT: return KEY_LEFT;
    case SDLK_RIGHT: return KEY_RIGHT;
    case SDLK_UP: return KEY_UP;
    case SDLK_DOWN: return KEY_DOWN;
    case SDLK_LSHIFT: return KEY_LEFTSHIFT;
    case SDLK_SLASH: return KEY_SLASH;
    case SDLK_COMMA: return KEY_COMMA;
    case SDLK_PERIOD: return KEY_DOT;
    case SDLK_r: return KEY_R;
    case SDLK_e: return KEY_E;
    case SDLK_TAB: return KEY_TAB;
    case SDLK_BACKSPACE: return KEY_BACKSPACE;
    case SDLK_1: return KEY_1;
    case SDLK_2: return KEY_2;
    case SDLK_3: return 4;
    case SDLK_4: return 5;
    case SDLK_5: return 6;
    case SDLK_6: return 7;
    case SDLK_7: return 8;
    case SDLK_8: return 9;
    case SDLK_9: return 10;
    case SDLK_0: return 11;
    case SDLK_KP_1: return KEY_1;
    case SDLK_KP_2: return KEY_2;
    case SDLK_KP_3: return 4;
    case SDLK_KP_4: return 5;
    case SDLK_KP_5: return 6;
    case SDLK_KP_6: return 7;
    case SDLK_KP_7: return 8;
    case SDLK_KP_8: return 9;
    case SDLK_KP_9: return 10;
    case SDLK_KP_0: return 11;
    default: break;
    }

    /* Scancode fallback covers unusual layouts and keyboards without a
     * usable translated keycode. */
    switch (code) {
    case SDL_SCANCODE_W: return KEY_W;
    case SDL_SCANCODE_A: return KEY_A;
    case SDL_SCANCODE_S: return KEY_S;
    case SDL_SCANCODE_D: return KEY_D;
    case SDL_SCANCODE_SPACE: return KEY_SPACE;
    case SDL_SCANCODE_ESCAPE: return KEY_ESC;
    case SDL_SCANCODE_RETURN: return KEY_ENTER;
    case SDL_SCANCODE_KP_ENTER: return KEY_ENTER;
    case SDL_SCANCODE_LEFT: return KEY_LEFT;
    case SDL_SCANCODE_RIGHT: return KEY_RIGHT;
    case SDL_SCANCODE_UP: return KEY_UP;
    case SDL_SCANCODE_DOWN: return KEY_DOWN;
    case SDL_SCANCODE_LSHIFT: return KEY_LEFTSHIFT;
    case SDL_SCANCODE_SLASH: return KEY_SLASH;
    case SDL_SCANCODE_COMMA: return KEY_COMMA;
    case SDL_SCANCODE_PERIOD: return KEY_DOT;
    case SDL_SCANCODE_R: return KEY_R;
    case SDL_SCANCODE_E: return KEY_E;
    case SDL_SCANCODE_TAB: return KEY_TAB;
    case SDL_SCANCODE_BACKSPACE: return KEY_BACKSPACE;
    case SDL_SCANCODE_1: return KEY_1;
    case SDL_SCANCODE_2: return KEY_2;
    case SDL_SCANCODE_3: return 4;
    case SDL_SCANCODE_4: return 5;
    case SDL_SCANCODE_5: return 6;
    case SDL_SCANCODE_6: return 7;
    case SDL_SCANCODE_7: return 8;
    case SDL_SCANCODE_8: return 9;
    case SDL_SCANCODE_9: return 10;
    case SDL_SCANCODE_0: return 11;
    case SDL_SCANCODE_KP_1: return KEY_1;
    case SDL_SCANCODE_KP_2: return KEY_2;
    case SDL_SCANCODE_KP_3: return 4;
    case SDL_SCANCODE_KP_4: return 5;
    case SDL_SCANCODE_KP_5: return 6;
    case SDL_SCANCODE_KP_6: return 7;
    case SDL_SCANCODE_KP_7: return 8;
    case SDL_SCANCODE_KP_8: return 9;
    case SDL_SCANCODE_KP_9: return 10;
    case SDL_SCANCODE_KP_0: return 11;
    default: return TOY_INPUT_KEY_COUNT;
    }
}

struct toy_window *toy_window_open(const char *title, int width, int height)
{
    struct toy_window *out;
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0 && SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
        return NULL;
    out = (struct toy_window *)SDL_calloc(1, sizeof(*out));
    if (!out) return NULL;
    out->width = width;
    out->height = height;
    out->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED, width, height,
                                   SDL_WINDOW_RESIZABLE);
    out->renderer = out->window ? SDL_CreateRenderer(out->window, -1,
                                                      SDL_RENDERER_PRESENTVSYNC) : NULL;
    out->texture = out->renderer ? SDL_CreateTexture(out->renderer,
                                                     SDL_PIXELFORMAT_ARGB8888,
                                                     SDL_TEXTUREACCESS_STREAMING,
                                                     width, height) : NULL;
    out->pixels = out->texture ? (uint32_t *)SDL_calloc((size_t)width * height,
                                                        sizeof(uint32_t)) : NULL;
    if (!out->window || !out->renderer || !out->texture || !out->pixels) {
        toy_window_close(out);
        return NULL;
    }
    SDL_SetRelativeMouseMode(SDL_FALSE);
    return out;
}

int toy_window_poll(struct toy_window *window, struct toy_window_events *events,
                    int timeout_ms)
{
    SDL_Event event;
    int have_event = 0;
    static int previous_up, previous_down, previous_enter, previous_escape;
    clear_events(events);
    if (!window) return -1;
    if (timeout_ms > 0 && !SDL_WaitEventTimeout(&event, timeout_ms)) return 0;
    if (timeout_ms > 0) {
        have_event = 1;
        goto dispatch;
    }
    while (SDL_PollEvent(&event)) {
        have_event = 1;
dispatch:
        if (!events) continue;
        switch (event.type) {
        case SDL_QUIT: events->close_requested = 1; break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                events->resized = 1;
                events->width = event.window.data1;
                events->height = event.window.data2;
            }
            if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED ||
                event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                events->keyboard_focus_changed = 1;
                events->keyboard_focused =
                    event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED;
            }
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            unsigned int key = key_code(event.key.keysym.scancode,
                                        event.key.keysym.sym);
            if (key < TOY_INPUT_KEY_COUNT &&
                events->key_event_count < TOY_WINDOW_MAX_KEY_EVENTS) {
                events->key_events[events->key_event_count].key = key;
                events->key_events[events->key_event_count].pressed =
                    event.type == SDL_KEYDOWN;
                events->key_event_count++;
            }
            break;
        }
        case SDL_MOUSEMOTION:
            events->pointer_moved = 1;
            events->pointer_x = event.motion.x;
            events->pointer_y = event.motion.y;
            if (event.motion.xrel || event.motion.yrel) {
                events->relative_moved = 1;
                events->relative_x = event.motion.xrel;
                events->relative_y = event.motion.yrel;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT ||
                event.button.button == SDL_BUTTON_RIGHT) {
                events->button = event.button.button == SDL_BUTTON_LEFT ?
                    0x110 : 0x111;
                events->button_pressed = event.type == SDL_MOUSEBUTTONDOWN;
            }
            break;
        default: break;
        }
        if (timeout_ms > 0) break;
    }
    if (events) {
        /* SDL normally supplies these events. The Win32 fallback handles
         * keyboard layouts/drivers that expose only virtual-key state. */
        poll_windows_key_edges(events, KEY_UP, VK_UP, &previous_up);
        poll_windows_key_edges(events, KEY_DOWN, VK_DOWN, &previous_down);
        poll_windows_key_edges(events, KEY_ENTER, VK_RETURN, &previous_enter);
        poll_windows_key_edges(events, KEY_ESC, VK_ESCAPE, &previous_escape);
        poll_windows_keys(events);
        {
            Uint32 buttons = SDL_GetMouseState(NULL, NULL);
            events->mouse_buttons = 0;
            if (buttons & SDL_BUTTON_LMASK) events->mouse_buttons |= 1;
            if (buttons & SDL_BUTTON_RMASK) events->mouse_buttons |= 2;
        }
    }
    return have_event;
}

int toy_window_begin_frame(struct toy_window *window, struct toy_surface *surface)
{
    if (!window || !surface) return -1;
    surface->pixels = window->pixels;
    surface->width = window->width;
    surface->height = window->height;
    surface->stride = window->width * (int)sizeof(uint32_t);
    return 1;
}

int toy_window_present(struct toy_window *window)
{
    if (!window) return -1;
    if (SDL_UpdateTexture(window->texture, NULL, window->pixels,
                          window->width * (int)sizeof(uint32_t)) < 0) return -1;
    if (SDL_RenderClear(window->renderer) < 0) return -1;
    if (SDL_RenderCopy(window->renderer, window->texture, NULL, NULL) < 0) return -1;
    SDL_RenderPresent(window->renderer);
    return 0;
}

int toy_window_pointer_lock_supported(struct toy_window *window)
{
    return window != NULL;
}

int toy_window_set_pointer_lock(struct toy_window *window, int locked)
{
    if (!window) return -1;
    window->pointer_locked = locked != 0;
    SDL_SetRelativeMouseMode(window->pointer_locked ? SDL_TRUE : SDL_FALSE);
    SDL_SetWindowGrab(window->window, window->pointer_locked ? SDL_TRUE : SDL_FALSE);
    SDL_WarpMouseInWindow(window->window, window->width / 2, window->height / 2);
    return 1;
}

int toy_window_set_pointer_confine(struct toy_window *window, int confined)
{
    (void)confined;
    return window ? 0 : -1;
}

int toy_window_move(struct toy_window *window, uint32_t serial)
{
    (void)window;
    (void)serial;
    return 0;
}

void toy_window_close(struct toy_window *window)
{
    if (!window) return;
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_SetWindowGrab(window->window, SDL_FALSE);
    SDL_free(window->pixels);
    SDL_DestroyTexture(window->texture);
    SDL_DestroyRenderer(window->renderer);
    SDL_DestroyWindow(window->window);
    SDL_free(window);
}
