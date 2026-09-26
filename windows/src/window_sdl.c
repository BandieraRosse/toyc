/* SDL2 implementation of the public software-window interface.
 *
 * This file is intentionally not part of the Linux build. It is the first
 * Windows platform unit: Rasterfall keeps rendering into toy_surface and SDL
 * only presents that buffer and translates native events.
 */
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
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
    int reported_pointer_locked;
    int minimized;
};

/* Linux input numbers are part of the existing game-facing key contract. Keep
 * the values local here so this Windows file does not include Linux headers. */
enum {
    KEY_ESC = 1, KEY_1 = 2, KEY_2 = 3, KEY_3 = 4, KEY_4 = 5,
    KEY_BACKSPACE = 14, KEY_TAB = 15,
    KEY_Q = 16, KEY_W = 17, KEY_E = 18, KEY_R = 19,
    KEY_T = 20, KEY_Y = 21, KEY_U = 22, KEY_I = 23, KEY_O = 24,
    KEY_P = 25, KEY_F = 33, KEY_G = 34, KEY_H = 35,
    KEY_J = 36, KEY_K = 37, KEY_L = 38, KEY_Z = 44, KEY_X = 45,
    KEY_C = 46, KEY_V = 47, KEY_B = 48, KEY_N = 49,
    KEY_A = 30, KEY_S = 31, KEY_D = 32, KEY_M = 50, KEY_ENTER = 28,
    KEY_GRAVE = 41, KEY_LEFTSHIFT = 42, KEY_COMMA = 51, KEY_DOT = 52,
    KEY_SLASH = 53, KEY_SPACE = 57,
    KEY_MINUS = 12, KEY_EQUAL = 13, KEY_F2 = 60,
    KEY_F12 = 88, KEY_UP = 103, KEY_LEFT = 105, KEY_RIGHT = 106, KEY_DOWN = 108
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
    for (int i = 0; i < events->key_event_count; i++)
        if (events->key_events[i].key == key &&
            events->key_events[i].pressed == pressed)
            return 1;
    return 0;
}

/* SDL supplies the normal event stream.  Keep a native snapshot as an edge
 * fallback for Windows focus/message-queue paths, without turning held keys
 * into repeated presses. */
static void poll_windows_key_edge(struct toy_window_events *events,
                                  unsigned int key, int vk, int *previous)
{
    int pressed = (GetAsyncKeyState(vk) & 0x8000) != 0;
    if (pressed == *previous) return;
    *previous = pressed;
    if (has_key_event(events, key, pressed) ||
        events->key_event_count >= TOY_WINDOW_MAX_KEY_EVENTS)
        return;
    events->key_events[events->key_event_count].key = key;
    events->key_events[events->key_event_count].pressed = pressed;
    events->key_event_count++;
}

static void poll_windows_keys(struct toy_window_events *events, int focused)
{
    static const struct { unsigned int key; int vk; } keys[] = {
        {KEY_W, 'W'}, {KEY_A, 'A'}, {KEY_S, 'S'}, {KEY_D, 'D'},
        {KEY_SPACE, VK_SPACE}, {KEY_LEFTSHIFT, VK_SHIFT},
        {KEY_TAB, VK_TAB}, {KEY_R, 'R'}, {KEY_E, 'E'}, {KEY_F, 'F'},
        {KEY_Q, 'Q'}, {KEY_T, 'T'}, {KEY_Y, 'Y'}, {KEY_U, 'U'},
        {KEY_I, 'I'}, {KEY_O, 'O'}, {KEY_P, 'P'}, {KEY_G, 'G'},
        {KEY_H, 'H'}, {KEY_J, 'J'}, {KEY_K, 'K'}, {KEY_L, 'L'},
        {KEY_Z, 'Z'}, {KEY_X, 'X'}, {KEY_C, 'C'}, {KEY_V, 'V'},
        {KEY_B, 'B'}, {KEY_N, 'N'}, {KEY_M, 'M'},
        {KEY_1, '1'}, {KEY_2, '2'}, {KEY_3, '3'}, {KEY_4, '4'},
        {6, '5'}, {7, '6'}, {8, '7'}, {9, '8'}, {10, '9'}, {11, '0'},
        {KEY_MINUS, VK_OEM_MINUS}, {KEY_EQUAL, VK_OEM_PLUS},
        {KEY_F2, VK_F2}, {KEY_F12, VK_F12},
        {KEY_ESC, VK_ESCAPE}, {KEY_ENTER, VK_RETURN},
        {KEY_BACKSPACE, VK_BACK}, {KEY_GRAVE, VK_OEM_3},
        {KEY_COMMA, VK_OEM_COMMA}, {KEY_DOT, VK_OEM_PERIOD},
        {KEY_UP, VK_UP}, {KEY_DOWN, VK_DOWN},
        {KEY_LEFT, VK_LEFT}, {KEY_RIGHT, VK_RIGHT}, {KEY_SLASH, VK_OEM_2}
    };
    static int previous[sizeof(keys) / sizeof(keys[0])];
    if (!focused) {
        for (unsigned int i = 0; i < sizeof(previous) / sizeof(previous[0]); i++)
            previous[i] = 0;
        return;
    }
    for (unsigned int i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        poll_windows_key_edge(events, keys[i].key, keys[i].vk, &previous[i]);
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
    case SDLK_q: return KEY_Q;
    case SDLK_t: return KEY_T;
    case SDLK_y: return KEY_Y;
    case SDLK_u: return KEY_U;
    case SDLK_i: return KEY_I;
    case SDLK_o: return KEY_O;
    case SDLK_p: return KEY_P;
    case SDLK_g: return KEY_G;
    case SDLK_h: return KEY_H;
    case SDLK_j: return KEY_J;
    case SDLK_k: return KEY_K;
    case SDLK_l: return KEY_L;
    case SDLK_z: return KEY_Z;
    case SDLK_x: return KEY_X;
    case SDLK_c: return KEY_C;
    case SDLK_v: return KEY_V;
    case SDLK_b: return KEY_B;
    case SDLK_n: return KEY_N;
    case SDLK_MINUS: return KEY_MINUS;
    case SDLK_EQUALS: return KEY_EQUAL;
    case SDLK_F2: return KEY_F2;
    case SDLK_KP_MINUS: return KEY_MINUS;
    case SDLK_KP_PLUS: return KEY_EQUAL;
    case SDLK_SPACE: return KEY_SPACE;
    case SDLK_ESCAPE: return KEY_ESC;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return KEY_ENTER;
    case SDLK_LEFT: return KEY_LEFT;
    case SDLK_RIGHT: return KEY_RIGHT;
    case SDLK_UP: return KEY_UP;
    case SDLK_DOWN: return KEY_DOWN;
    case SDLK_LSHIFT: return KEY_LEFTSHIFT;
    case SDLK_RSHIFT: return KEY_LEFTSHIFT;
    case SDLK_SLASH: return KEY_SLASH;
    case SDLK_COMMA: return KEY_COMMA;
    case SDLK_PERIOD: return KEY_DOT;
    case SDLK_r: return KEY_R;
    case SDLK_e: return KEY_E;
    case SDLK_f: return KEY_F;
    case SDLK_m: return KEY_M;
    case SDLK_TAB: return KEY_TAB;
    case SDLK_BACKSPACE: return KEY_BACKSPACE;
    case SDLK_BACKQUOTE: return KEY_GRAVE;
    case SDLK_F12: return KEY_F12;
    case SDLK_1: return KEY_1;
    case SDLK_2: return KEY_2;
    case SDLK_3: return KEY_3;
    case SDLK_4: return KEY_4;
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
    case SDL_SCANCODE_Q: return KEY_Q;
    case SDL_SCANCODE_T: return KEY_T;
    case SDL_SCANCODE_Y: return KEY_Y;
    case SDL_SCANCODE_U: return KEY_U;
    case SDL_SCANCODE_I: return KEY_I;
    case SDL_SCANCODE_O: return KEY_O;
    case SDL_SCANCODE_P: return KEY_P;
    case SDL_SCANCODE_G: return KEY_G;
    case SDL_SCANCODE_H: return KEY_H;
    case SDL_SCANCODE_J: return KEY_J;
    case SDL_SCANCODE_K: return KEY_K;
    case SDL_SCANCODE_L: return KEY_L;
    case SDL_SCANCODE_Z: return KEY_Z;
    case SDL_SCANCODE_X: return KEY_X;
    case SDL_SCANCODE_C: return KEY_C;
    case SDL_SCANCODE_V: return KEY_V;
    case SDL_SCANCODE_B: return KEY_B;
    case SDL_SCANCODE_N: return KEY_N;
    case SDL_SCANCODE_MINUS: return KEY_MINUS;
    case SDL_SCANCODE_EQUALS: return KEY_EQUAL;
    case SDL_SCANCODE_F2: return KEY_F2;
    case SDL_SCANCODE_KP_MINUS: return KEY_MINUS;
    case SDL_SCANCODE_KP_PLUS: return KEY_EQUAL;
    case SDL_SCANCODE_SPACE: return KEY_SPACE;
    case SDL_SCANCODE_ESCAPE: return KEY_ESC;
    case SDL_SCANCODE_RETURN: return KEY_ENTER;
    case SDL_SCANCODE_KP_ENTER: return KEY_ENTER;
    case SDL_SCANCODE_LEFT: return KEY_LEFT;
    case SDL_SCANCODE_RIGHT: return KEY_RIGHT;
    case SDL_SCANCODE_UP: return KEY_UP;
    case SDL_SCANCODE_DOWN: return KEY_DOWN;
    case SDL_SCANCODE_LSHIFT: return KEY_LEFTSHIFT;
    case SDL_SCANCODE_RSHIFT: return KEY_LEFTSHIFT;
    case SDL_SCANCODE_SLASH: return KEY_SLASH;
    case SDL_SCANCODE_COMMA: return KEY_COMMA;
    case SDL_SCANCODE_PERIOD: return KEY_DOT;
    case SDL_SCANCODE_R: return KEY_R;
    case SDL_SCANCODE_E: return KEY_E;
    case SDL_SCANCODE_F: return KEY_F;
    case SDL_SCANCODE_M: return KEY_M;
    case SDL_SCANCODE_TAB: return KEY_TAB;
    case SDL_SCANCODE_BACKSPACE: return KEY_BACKSPACE;
    case SDL_SCANCODE_GRAVE: return KEY_GRAVE;
    case SDL_SCANCODE_F12: return KEY_F12;
    case SDL_SCANCODE_1: return KEY_1;
    case SDL_SCANCODE_2: return KEY_2;
    case SDL_SCANCODE_3: return KEY_3;
    case SDL_SCANCODE_4: return KEY_4;
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

int toy_window_windows_key_mapping_logic_test(void)
{
    return key_code(SDL_SCANCODE_M, SDLK_m) == KEY_M &&
           key_code(SDL_SCANCODE_M, SDLK_UNKNOWN) == KEY_M &&
           key_code(SDL_SCANCODE_J, SDLK_j) == KEY_J &&
           key_code(SDL_SCANCODE_F2, SDLK_UNKNOWN) == KEY_F2 &&
           key_code(SDL_SCANCODE_MINUS, SDLK_MINUS) == KEY_MINUS &&
           key_code(SDL_SCANCODE_0, SDLK_0) == 11 ? 0 : -1;
}

static struct toy_window *toy_window_open_impl(const char *title, int width,
                                               int height, int native_present)
{
    struct toy_window *out;
    size_t pixels;
    if (width <= 0 || height <= 0 ||
        (size_t)width > (size_t)-1 / (size_t)height)
        return NULL;
    pixels = (size_t)width * (size_t)height;
    if (pixels > (size_t)-1 / sizeof(uint32_t)) return NULL;
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
        native_present ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_PRESENTVSYNC) : NULL;
    out->texture = out->renderer ? SDL_CreateTexture(out->renderer,
                                                     SDL_PIXELFORMAT_ARGB8888,
                                                     SDL_TEXTUREACCESS_STREAMING,
                                                     width, height) : NULL;
    out->pixels = out->texture ? (uint32_t *)SDL_calloc(pixels,
                                                        sizeof(uint32_t)) : NULL;
    if (!out->window || !out->renderer || !out->texture || !out->pixels) {
        toy_window_close(out);
        return NULL;
    }
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    return out;
}

struct toy_window *toy_window_open(const char *title, int width, int height)
{
    return toy_window_open_impl(title, width, height, 0);
}

struct toy_window *toy_window_open_native(const char *title, int width, int height)
{
    return toy_window_open_impl(title, width, height, 1);
}

int toy_window_poll(struct toy_window *window, struct toy_window_events *events,
                    int timeout_ms)
{
    SDL_Event event;
    int have_event = 0;
    clear_events(events);
    if (!window) return -1;
    /* Pump SDL first, then merge the native snapshot only when it contributes
     * an edge SDL did not deliver.  has_key_event() prevents duplicates. */
    SDL_PumpEvents();
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
            if (event.window.event == SDL_WINDOWEVENT_MINIMIZED)
                window->minimized = 1;
            if (event.window.event == SDL_WINDOWEVENT_RESTORED ||
                event.window.event == SDL_WINDOWEVENT_MAXIMIZED)
                window->minimized = 0;
            if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                int new_width = event.window.data1, new_height = event.window.data2;
                if (new_width > 0 && new_height > 0) {
                    size_t count = (size_t)new_width * (size_t)new_height;
                    SDL_Texture *texture = SDL_CreateTexture(window->renderer,
                        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                        new_width, new_height);
                    uint32_t *pixels = texture ? (uint32_t *)SDL_calloc(
                        count, sizeof(*pixels)) : NULL;
                    if (texture && pixels) {
                        SDL_DestroyTexture(window->texture);
                        SDL_free(window->pixels);
                        window->texture = texture; window->pixels = pixels;
                        window->width = new_width; window->height = new_height;
                        events->resized = 1;
                        events->width = new_width; events->height = new_height;
                    } else {
                        SDL_DestroyTexture(texture); SDL_free(pixels);
                    }
                }
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
            if (event.key.repeat) break;
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
            events->pointer_moved = 1;
            events->pointer_x = event.button.x;
            events->pointer_y = event.button.y;
            if (event.button.button >= SDL_BUTTON_LEFT &&
                event.button.button <= SDL_BUTTON_X2) {
                events->button = event.button.button == SDL_BUTTON_LEFT ? 0x110 :
                    event.button.button == SDL_BUTTON_RIGHT ? 0x111 :
                    event.button.button == SDL_BUTTON_MIDDLE ? 0x112 :
                    event.button.button == SDL_BUTTON_X1 ? 0x113 : 0x114;
                events->button_pressed = event.type == SDL_MOUSEBUTTONDOWN;
            }
            break;
        case SDL_MOUSEWHEEL:
            events->wheel_y += event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ?
                -event.wheel.y : event.wheel.y;
            break;
        default: break;
        }
        if (timeout_ms > 0) break;
    }
    if (events) {
        poll_windows_keys(events, SDL_GetKeyboardFocus() == window->window);
        {
            Uint32 buttons = SDL_GetMouseState(NULL, NULL);
            events->mouse_buttons = 0;
            if (buttons & SDL_BUTTON_LMASK) events->mouse_buttons |= 1;
            if (buttons & SDL_BUTTON_RMASK) events->mouse_buttons |= 2;
            if (buttons & SDL_BUTTON_MMASK) events->mouse_buttons |= 4;
            if (buttons & SDL_BUTTON_X1MASK) events->mouse_buttons |= 8;
            if (buttons & SDL_BUTTON_X2MASK) events->mouse_buttons |= 16;
        }
        window->pointer_locked = SDL_GetRelativeMouseMode() == SDL_TRUE;
        if (window->reported_pointer_locked != window->pointer_locked) {
            events->pointer_lock_changed = 1;
            events->pointer_locked = window->pointer_locked;
            window->reported_pointer_locked = window->pointer_locked;
        }
    }
    return have_event;
}

int toy_window_begin_frame(struct toy_window *window, struct toy_surface *surface)
{
    if (!window || !surface) return -1;
    if (window->minimized) return 0;
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

int toy_window_get_native_handle(struct toy_window *window,
                                 struct toy_native_window_handle *handle)
{
    SDL_SysWMinfo info;
    if (!window || !handle) return -1;
    memset(handle, 0, sizeof(*handle));
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window->window, &info) ||
        info.subsystem != SDL_SYSWM_WINDOWS || !info.info.win.window)
        return 0;
    handle->type = TOY_NATIVE_WINDOW_WIN32;
    handle->window = (unsigned long long)(uintptr_t)info.info.win.window;
    handle->instance = (unsigned long long)(uintptr_t)GetModuleHandleW(NULL);
    return 1;
}

int toy_window_pointer_lock_supported(struct toy_window *window)
{
    return window != NULL;
}

int toy_window_set_pointer_lock(struct toy_window *window, int locked)
{
    if (!window) return -1;
    if (SDL_SetRelativeMouseMode(locked ? SDL_TRUE : SDL_FALSE) != 0)
        return -1;
    window->pointer_locked = locked != 0;
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
