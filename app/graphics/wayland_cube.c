/*
 * wayland_cube — Toyc-native Wayland software 3D milestone.
 *
 * wl_shm double buffering, xdg-shell resize/close, frame callbacks, keyboard
 * and pointer input are supplied by wayland_min.  Rendering is a CPU triangle
 * rasterizer with a floating-point Z buffer; no host libc, EGL or OpenGL.
 *
 * Controls: Esc quits, arrows rotate, mouse position changes the view.
 */

#include "core.h"
#include "string.h"
#include "wayland_min.h"

#define KEY_ESC   1
#define KEY_LEFT  105
#define KEY_RIGHT 106
#define KEY_UP    103
#define KEY_DOWN  108

struct screen_v { int x, y, z; };
struct rotation { int sy, cy, sx, cx; };

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

static long edge(const struct screen_v *a, const struct screen_v *b,
                 int px, int py)
{
    return (px - a->x) * (b->y - a->y) - (py - a->y) * (b->x - a->x);
}

static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void clear_frame(struct toywl_frame *f, int *depth)
{
    for (int y = 0; y < f->height; y++) {
        uint32_t *row = (uint32_t *)((unsigned char *)f->pixels + y * f->stride);
        uint32_t shade = (uint32_t)(18 + y * 18 / (f->height ? f->height : 1));
        uint32_t color = (shade << 16) | (shade << 8) | (shade + 8);
        for (int x = 0; x < f->width; x++) {
            row[x] = color;
            depth[y * f->width + x] = 0x7fffffff;
        }
    }
}

static int raster_triangle(struct toywl_frame *f, int *depth,
                           const struct screen_v *a, const struct screen_v *b,
                           const struct screen_v *c, uint32_t color)
{
    long area = edge(a, b, c->x, c->y);
    int minx, maxx, miny, maxy;
    int drawn = 0;
    if (area >= 0) return 0;
    minx = clampi(a->x < b->x ? (a->x < c->x ? a->x : c->x) :
                               (b->x < c->x ? b->x : c->x), 0, f->width - 1);
    maxx = clampi((a->x > b->x ? (a->x > c->x ? a->x : c->x) :
                                (b->x > c->x ? b->x : c->x)) + 1, 0, f->width - 1);
    miny = clampi(a->y < b->y ? (a->y < c->y ? a->y : c->y) :
                               (b->y < c->y ? b->y : c->y), 0, f->height - 1);
    maxy = clampi((a->y > b->y ? (a->y > c->y ? a->y : c->y) :
                                (b->y > c->y ? b->y : c->y)) + 1, 0, f->height - 1);
    for (int y = miny; y <= maxy; y++) {
        uint32_t *row = (uint32_t *)((unsigned char *)f->pixels + y * f->stride);
        for (int x = minx; x <= maxx; x++) {
            long w0 = edge(b, c, x, y);
            long w1 = edge(c, a, x, y);
            long w2 = edge(a, b, x, y);
            if (w0 <= 0 && w1 <= 0 && w2 <= 0) {
                int z = (int)((w0 * a->z + w1 * b->z + w2 * c->z) / area);
                int at = y * f->width + x;
                if (z < depth[at]) { depth[at] = z; row[x] = color; drawn++; }
            }
        }
    }
    return drawn;
}

static int render_cube(struct toywl_frame *f, int *depth, const struct rotation *rot,
                       int pointer_x, int pointer_y)
{
    struct screen_v projected[8];
    int min_dim = f->width;
    int scale_i;
    if (f->height < min_dim) min_dim = f->height;
    scale_i = min_dim * 85 / 100;
    int drawn = 0;
    clear_frame(f, depth);
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
        projected[i].x = f->width / 2 + vx * scale_i / vz;
        projected[i].y = f->height / 2 - vy * scale_i / vz;
        projected[i].z = vz;
    }
    for (int i = 0; i < 12; i++) {
        drawn += raster_triangle(f, depth,
            &projected[cube_faces[i][0]], &projected[cube_faces[i][1]],
            &projected[cube_faces[i][2]], face_colors[i]);
    }
    /* Crosshair and pointer marker also make input/configure errors visible. */
    int cx = f->width / 2, cy = f->height / 2;
    for (int d = -8; d <= 8; d++) {
        if (cx + d >= 0 && cx + d < f->width) f->pixels[cy * f->width + cx + d] = 0xFFFFFF;
        if (cy + d >= 0 && cy + d < f->height) f->pixels[(cy + d) * f->width + cx] = 0xFFFFFF;
    }
    if (pointer_x >= 0 && pointer_x < f->width && pointer_y >= 0 && pointer_y < f->height) {
        for (int d = -3; d <= 3; d++) {
            int x = pointer_x + d, y = pointer_y + d;
            if (x >= 0 && x < f->width) f->pixels[pointer_y * f->width + x] = 0xFFCC33;
            if (y >= 0 && y < f->height) f->pixels[y * f->width + pointer_x] = 0xFFCC33;
        }
    }
    return drawn;
}

int main(int argc, char **argv)
{
    struct toywl *wl;
    struct toywl_input input;
    struct toywl_frame frame;
    int *depth = NULL;
    size_t depth_size = 0;
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

    wl = toywl_open("Toyc Wayland Software 3D", 640, 360);
    if (!wl) {
        __fprintf(2, "wayland_cube: cannot connect or create xdg-shell window\n");
        __fprintf(2, "check XDG_RUNTIME_DIR and WAYLAND_DISPLAY\n");
        return 1;
    }
    while (running) {
        if (toywl_dispatch(wl, &input, 1000) < 0) break;
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
        int ready = toywl_begin_frame(wl, &frame);
        if (ready < 0) break;
        if (ready > 0) {
            size_t need = (size_t)frame.width * frame.height * sizeof(int);
            if (need != depth_size) {
                if (depth) tlibc_free(depth);
                depth = tlibc_malloc(need);
                depth_size = need;
                if (!depth) break;
            }
            {
                int old_sy = rot.sy, old_sx = rot.sx;
                rot.sy = (old_sy * 1024 + rot.cy * 13) / 1024;
                rot.cy = (rot.cy * 1024 - old_sy * 13) / 1024;
                rot.sx = (old_sx * 1024 + rot.cx * 8) / 1024;
                rot.cx = (rot.cx * 1024 - old_sx * 8) / 1024;
            }
            cube_pixels = render_cube(&frame, depth, &rot,
                                      pointer_x, pointer_y);
            if (toywl_present(wl) < 0) break;
            rendered_frames++;
            if (frame_limit > 0 && rendered_frames >= frame_limit) running = 0;
        }
    }
    if (depth) tlibc_free(depth);
    toywl_close(wl);
    __printf("wayland_cube: %d frames, %d cube pixels\n", rendered_frames, cube_pixels);
    return rendered_frames > 0 && cube_pixels == 0 ? 2 : 0;
}
