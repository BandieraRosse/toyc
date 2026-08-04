/*
 * wayland_cube — Toyc-native Wayland software 3D milestone.
 *
 * wl_shm double buffering, xdg-shell resize/close, frame callbacks, keyboard
 * and pointer input are supplied by the platform window layer.  Rendering uses
 * the shared CPU triangle rasterizer; no host libc, EGL or OpenGL.
 *
 * Controls: arrows rotate, left click captures the pointer, mouse look rotates,
 * Esc releases a captured pointer or quits when the pointer is free.
 */

#include "core.h"
#include "string.h"
#include "toy_window.h"
#include "toy_renderer.h"
#include "toy_input.h"

#define KEY_ESC   1
#define KEY_LEFT  105
#define KEY_RIGHT 106
#define KEY_UP    103
#define KEY_DOWN  108
#define BTN_LEFT  0x110

struct rotation { int sy, cy, sx, cx; };

#define FIXED_STEP_US 16667
#define MAX_FRAME_US  250000
#define MAX_LOGIC_STEPS 4

static long monotonic_us(void)
{
    struct timespec now;
    if (__clock_gettime(CLOCK_MONOTONIC, &now) < 0) return 0;
    return now.tv_sec * 1000000L + now.tv_nsec / 1000;
}

static void update_rotation(struct rotation *rot)
{
    int old_sy = rot->sy, old_sx = rot->sx;
    rot->sy = (old_sy * 1024 + rot->cy * 13) / 1024;
    rot->cy = (rot->cy * 1024 - old_sy * 13) / 1024;
    rot->sx = (old_sx * 1024 + rot->cx * 8) / 1024;
    rot->cx = (rot->cx * 1024 - old_sx * 8) / 1024;
}

static void put_pixel(struct toy_surface *surface, int x, int y, uint32_t color)
{
    uint32_t *row = (uint32_t *)((unsigned char *)surface->pixels +
                                 y * surface->stride);
    row[x] = color;
}

static const signed char cube_vertices[8][3] = {
    {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
    {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}
};

static const unsigned char cube_faces[12][3] = {
    {0,2,1}, {0,3,2}, {4,5,6}, {4,6,7},
    {0,1,5}, {0,5,4}, {2,3,7}, {2,7,6},
    {1,2,6}, {1,6,5}, {3,0,4}, {3,4,7}
};

static const uint32_t face_colors[12] = {
    0xD95F59, 0xC94F49, 0x65C779, 0x55B769,
    0x5B8DEF, 0x4B7DDF, 0xE6B94F, 0xD6A93F,
    0xB46FE3, 0xA45FD3, 0x49C8C2, 0x39B8B2
};

static void draw_background(struct toy_surface *surface)
{
    for (int y = 0; y < surface->height; y++) {
        uint32_t *row = (uint32_t *)((unsigned char *)surface->pixels +
                                     y * surface->stride);
        uint32_t shade = (uint32_t)(18 + y * 18 /
                                    (surface->height ? surface->height : 1));
        uint32_t color = (shade << 16) | (shade << 8) | (shade + 8);
        for (int x = 0; x < surface->width; x++) row[x] = color;
    }
}

static int render_cube(struct toy_renderer *renderer, const struct rotation *rot,
                       int pointer_x, int pointer_y, int shot_flash)
{
    struct toy_surface *surface = &renderer->surface;
    struct toy_screen_vertex projected[8];
    int min_dim = surface->width;
    int scale_i;
    if (surface->height < min_dim) min_dim = surface->height;
    scale_i = min_dim * 85 / 100;
    int drawn = 0;
    draw_background(surface);
    for (int i = 0; i < 8; i++) {
        int mx = cube_vertices[i][0] * 1024;
        int my = cube_vertices[i][1] * 1024;
        int mz = cube_vertices[i][2] * 1024;
        int rx = (mx * rot->cy + mz * rot->sy) / 1024;
        int ry = my;
        int rz = (-mx * rot->sy + mz * rot->cy) / 1024;
        int vx = rx;
        int vy = (ry * rot->cx - rz * rot->sx) / 1024;
        int vz = (ry * rot->sx + rz * rot->cx) / 1024 + 4608;
        projected[i].x = surface->width / 2 + vx * scale_i / vz;
        projected[i].y = surface->height / 2 - vy * scale_i / vz;
        projected[i].z = vz;
    }
    for (int i = 0; i < 12; i++) {
        drawn += toy_renderer_triangle(renderer,
            &projected[cube_faces[i][0]], &projected[cube_faces[i][1]],
            &projected[cube_faces[i][2]], face_colors[i]);
    }
    /* Crosshair and pointer marker also make input/configure errors visible. */
    int cx = surface->width / 2, cy = surface->height / 2;
    uint32_t crosshair_color = shot_flash > 0 ? 0xFFD040 : 0xFFFFFF;
    for (int d = -8; d <= 8; d++) {
        if (cx + d >= 0 && cx + d < surface->width)
            put_pixel(surface, cx + d, cy, crosshair_color);
        if (cy + d >= 0 && cy + d < surface->height)
            put_pixel(surface, cx, cy + d, crosshair_color);
    }
    if (shot_flash > 0) {
        int radius = 12 + shot_flash * 2;
        for (int d = -radius; d <= radius; d++) {
            if (cx + d >= 0 && cx + d < surface->width) {
                if (cy - radius >= 0) put_pixel(surface, cx + d, cy - radius, 0xFF8A20);
                if (cy + radius < surface->height) put_pixel(surface, cx + d, cy + radius, 0xFF8A20);
            }
            if (cy + d >= 0 && cy + d < surface->height) {
                if (cx - radius >= 0) put_pixel(surface, cx - radius, cy + d, 0xFF8A20);
                if (cx + radius < surface->width) put_pixel(surface, cx + radius, cy + d, 0xFF8A20);
            }
        }
    }
    if (pointer_x >= 0 && pointer_x < surface->width &&
        pointer_y >= 0 && pointer_y < surface->height) {
        for (int d = -3; d <= 3; d++) {
            int x = pointer_x + d, y = pointer_y + d;
            if (x >= 0 && x < surface->width)
                put_pixel(surface, x, pointer_y, 0xFFCC33);
            if (y >= 0 && y < surface->height)
                put_pixel(surface, pointer_x, y, 0xFFCC33);
        }
    }
    return drawn;
}

int main(int argc, char **argv)
{
    struct toy_window *window;
    struct toy_window_events events;
    struct toy_input input;
    struct toy_surface surface;
    struct toy_renderer renderer;
    struct rotation rot;
    int running = 1;
    int rendered_frames = 0;
    int frame_limit = 0;
    int cube_pixels = 0;
    int pointer_lock_requested = 0;
    int lock_test = 0;
    int shot_count = 0, shot_flash = 0;
    int relative_reports = 0;
    int last_pointer_x = 0, last_pointer_y = 0, have_pointer_position = 0;
    long last_time, accumulator = 0;

    if (argc == 3 && strcmp(argv[1], "--frames") == 0) {
        const char *p = argv[2];
        while (*p >= '0' && *p <= '9') frame_limit = frame_limit * 10 + (*p++ - '0');
    } else if (argc == 2 && strcmp(argv[1], "--lock-test") == 0) {
        frame_limit = 3;
        lock_test = 1;
    }
    rot.sy = 445; rot.cy = 922;
    rot.sx = 253; rot.cx = 992;
    toy_renderer_init(&renderer);
    toy_input_init(&input);

    window = toy_window_open("Toyc Wayland Software 3D", 640, 360);
    if (!window) {
        __fprintf(2, "wayland_cube: cannot connect or create xdg-shell window\n");
        __fprintf(2, "check XDG_RUNTIME_DIR and WAYLAND_DISPLAY\n");
        return 1;
    }
    __printf("wayland_cube: relative pointer %s\n",
             toy_window_pointer_lock_supported(window) ? "available" : "unavailable");
    if (lock_test && toy_window_set_pointer_lock(window, 1) > 0)
        pointer_lock_requested = 1;
    last_time = monotonic_us();
    while (running) {
        long now, elapsed;
        int logic_steps = 0;
        toy_input_begin_frame(&input);
        if (toy_window_poll(window, &events, 1000) < 0) break;
        toy_input_apply(&input, &events);
        if (events.pointer_lock_changed) {
            __printf("wayland_cube: pointer %s\n",
                     events.pointer_locked ? "locked" : "unlocked");
            if (!events.pointer_locked) pointer_lock_requested = 0;
        }
        if (events.relative_moved && relative_reports < 12) {
            __printf("wayland_cube: relative dx=%d dy=%d locked=%d requested=%d\n",
                     events.relative_x, events.relative_y,
                     input.pointer_locked, pointer_lock_requested);
            relative_reports++;
        }
        if (events.button_pressed && events.button == BTN_LEFT) {
            shot_count++;
            shot_flash = 6;
            last_pointer_x = input.pointer_x;
            last_pointer_y = input.pointer_y;
            have_pointer_position = 1;
            __printf("wayland_cube: shot %d\n", shot_count);
            if (toy_window_pointer_lock_supported(window)) {
                int lock_result = toy_window_set_pointer_lock(window, 1);
                if (lock_result > 0) pointer_lock_requested = 1;
            }
        }
        if (toy_input_pressed(&input, KEY_ESC)) {
            if (input.pointer_locked || pointer_lock_requested) {
                toy_window_set_pointer_lock(window, 0);
                pointer_lock_requested = 0;
            } else {
                running = 0;
            }
        }
        if (events.close_requested) running = 0;
        if (toy_input_pressed(&input, KEY_LEFT)) rot.sy -= 82;
        if (toy_input_pressed(&input, KEY_RIGHT)) rot.sy += 82;
        if (toy_input_pressed(&input, KEY_UP)) rot.sx -= 82;
        if (toy_input_pressed(&input, KEY_DOWN)) rot.sx += 82;
        if ((input.pointer_locked || pointer_lock_requested) &&
            events.relative_moved) {
            rot.sy += input.relative_x * 4;
            rot.sx += input.relative_y * 4;
        } else if (pointer_lock_requested && input.pointer_moved) {
            /* Absolute-motion fallback keeps WSLg usable until the
             * compositor confirms pointer constraints. */
            if (have_pointer_position) {
                rot.sy += (input.pointer_x - last_pointer_x) * 4;
                rot.sx += (input.pointer_y - last_pointer_y) * 4;
            }
            last_pointer_x = input.pointer_x;
            last_pointer_y = input.pointer_y;
            have_pointer_position = 1;
        }
        if (!running) break;
        now = monotonic_us();
        elapsed = now - last_time;
        last_time = now;
        if (elapsed < 0) elapsed = 0;
        if (elapsed > MAX_FRAME_US) elapsed = MAX_FRAME_US;
        accumulator += elapsed;
        while (accumulator >= FIXED_STEP_US && logic_steps < MAX_LOGIC_STEPS) {
            update_rotation(&rot);
            accumulator -= FIXED_STEP_US;
            logic_steps++;
        }
        if (accumulator >= FIXED_STEP_US) accumulator %= FIXED_STEP_US;
        int ready = toy_window_begin_frame(window, &surface);
        if (ready < 0) break;
        if (ready > 0) {
            if (toy_renderer_begin(&renderer, &surface, 0) < 0) break;
            cube_pixels = render_cube(&renderer, &rot,
                                      input.pointer_x, input.pointer_y,
                                      shot_flash);
            if (toy_window_present(window) < 0) break;
            if (shot_flash > 0) shot_flash--;
            rendered_frames++;
            if (frame_limit > 0 && rendered_frames >= frame_limit) running = 0;
        }
    }
    toy_renderer_destroy(&renderer);
    toy_window_close(window);
    __printf("wayland_cube: %d frames, %d cube pixels, %d shots\n",
             rendered_frames, cube_pixels, shot_count);
    return rendered_frames > 0 && cube_pixels == 0 ? 2 : 0;
}
