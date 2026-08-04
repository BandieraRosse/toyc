/*
 * wayland_cube — Toyc-native Wayland software 3D milestone.
 *
 * wl_shm double buffering, xdg-shell resize/close, frame callbacks, keyboard
 * and pointer input are supplied by the platform window layer.  Rendering uses
 * the shared CPU triangle rasterizer; no host libc, EGL or OpenGL.
 *
 * Controls: Esc quits, arrows rotate, mouse position changes the view.
 */

#include "core.h"
#include "string.h"
#include "toy_window.h"
#include "toy_renderer.h"

#define KEY_ESC   1
#define KEY_LEFT  105
#define KEY_RIGHT 106
#define KEY_UP    103
#define KEY_DOWN  108

struct rotation { int sy, cy, sx, cx; };

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
                       int pointer_x, int pointer_y)
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
    for (int d = -8; d <= 8; d++) {
        if (cx + d >= 0 && cx + d < surface->width)
            put_pixel(surface, cx + d, cy, 0xFFFFFF);
        if (cy + d >= 0 && cy + d < surface->height)
            put_pixel(surface, cx, cy + d, 0xFFFFFF);
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
    struct toy_window_events input;
    struct toy_surface surface;
    struct toy_renderer renderer;
    struct rotation rot;
    int pointer_x = 0, pointer_y = 0;
    int running = 1;
    int rendered_frames = 0;
    int frame_limit = 0;
    int cube_pixels = 0;

    if (argc == 3 && strcmp(argv[1], "--frames") == 0) {
        const char *p = argv[2];
        while (*p >= '0' && *p <= '9') frame_limit = frame_limit * 10 + (*p++ - '0');
    }
    rot.sy = 445; rot.cy = 922;
    rot.sx = 253; rot.cx = 992;
    toy_renderer_init(&renderer);

    window = toy_window_open("Toyc Wayland Software 3D", 640, 360);
    if (!window) {
        __fprintf(2, "wayland_cube: cannot connect or create xdg-shell window\n");
        __fprintf(2, "check XDG_RUNTIME_DIR and WAYLAND_DISPLAY\n");
        return 1;
    }
    while (running) {
        if (toy_window_poll(window, &input, 1000) < 0) break;
        if (input.close_requested) running = 0;
        if (input.pointer_moved) { pointer_x = input.pointer_x; pointer_y = input.pointer_y; }
        if (input.key_pressed) {
            if (input.key == KEY_ESC) running = 0;
            else if (input.key == KEY_LEFT) rot.sy -= 82;
            else if (input.key == KEY_RIGHT) rot.sy += 82;
            else if (input.key == KEY_UP) rot.sx -= 82;
            else if (input.key == KEY_DOWN) rot.sx += 82;
        }
        if (!running) break;
        int ready = toy_window_begin_frame(window, &surface);
        if (ready < 0) break;
        if (ready > 0) {
            if (toy_renderer_begin(&renderer, &surface, 0) < 0) break;
            {
                int old_sy = rot.sy, old_sx = rot.sx;
                rot.sy = (old_sy * 1024 + rot.cy * 13) / 1024;
                rot.cy = (rot.cy * 1024 - old_sy * 13) / 1024;
                rot.sx = (old_sx * 1024 + rot.cx * 8) / 1024;
                rot.cx = (rot.cx * 1024 - old_sx * 8) / 1024;
            }
            cube_pixels = render_cube(&renderer, &rot,
                                      pointer_x, pointer_y);
            if (toy_window_present(window) < 0) break;
            rendered_frames++;
            if (frame_limit > 0 && rendered_frames >= frame_limit) running = 0;
        }
    }
    toy_renderer_destroy(&renderer);
    toy_window_close(window);
    __printf("wayland_cube: %d frames, %d cube pixels\n", rendered_frames, cube_pixels);
    return rendered_frames > 0 && cube_pixels == 0 ? 2 : 0;
}
