/*
 * wayland_fps — Toyc software-rendered first-person graybox.
 *
 * Controls: left click captures the pointer, mouse looks, WASD moves,
 * Esc releases the pointer or quits.  Rendering and input are freestanding.
 */

#include "core.h"
#include "string.h"
#include "toy_window.h"
#include "toy_renderer.h"
#include "toy_input.h"

#define KEY_ESC 1
#define KEY_W   17
#define KEY_A   30
#define KEY_S   31
#define KEY_D   32
#define BTN_LEFT 0x110

#define FIXED_STEP_US 16667
#define MAX_FRAME_US 250000
#define MAX_LOGIC_STEPS 4
#define NEAR_Z 192
#define ROOM_LIMIT 5700
#define PLAYER_RADIUS 180
#define MOVE_STEP 72

struct vec3 { int x, y, z; };
struct camera { int x, z, sy, cy, pitch; };
struct box { int minx, maxx, minz, maxz, height; uint32_t color; };

static const struct box obstacles[3] = {
    {-1700, -700,  300, 1700, 1000, 0x755A47},
    {  600, 1800, -900,  100, 1450, 0x49677D},
    { 2300, 3200, 1800, 3000,  750, 0x64704B}
};

static long monotonic_us(void)
{
    struct timespec now;
    if (__clock_gettime(CLOCK_MONOTONIC, &now) < 0) return 0;
    return now.tv_sec * 1000000L + now.tv_nsec / 1000;
}

static int clampi(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static void put_pixel(struct toy_surface *surface, int x, int y, uint32_t color)
{
    uint32_t *row = (uint32_t *)((unsigned char *)surface->pixels +
                                 y * surface->stride);
    row[x] = color;
}

static void copy_vec3(struct vec3 *out, const struct vec3 *in)
{
    out->x = in->x; out->y = in->y; out->z = in->z;
}

static void world_to_view(const struct camera *camera, const struct vec3 *world,
                          struct vec3 *view)
{
    int dx = world->x - camera->x;
    int dz = world->z - camera->z;
    view->x = (dx * camera->cy - dz * camera->sy) / 1024;
    view->y = world->y;
    view->z = (dx * camera->sy + dz * camera->cy) / 1024;
}

static void near_intersection(const struct vec3 *a, const struct vec3 *b,
                              struct vec3 *out)
{
    long numerator = NEAR_Z - a->z;
    long denominator = b->z - a->z;
    out->x = a->x + (int)((long)(b->x - a->x) * numerator / denominator);
    out->y = a->y + (int)((long)(b->y - a->y) * numerator / denominator);
    out->z = NEAR_Z;
}

static int clip_near(const struct vec3 *input, int count, struct vec3 *output)
{
    int out_count = 0;
    struct vec3 previous;
    copy_vec3(&previous, &input[count - 1]);
    int previous_inside = previous.z >= NEAR_Z;
    for (int i = 0; i < count; i++) {
        struct vec3 current;
        copy_vec3(&current, &input[i]);
        int current_inside = current.z >= NEAR_Z;
        if (current_inside != previous_inside)
            near_intersection(&previous, &current, &output[out_count++]);
        if (current_inside) copy_vec3(&output[out_count++], &current);
        copy_vec3(&previous, &current);
        previous_inside = current_inside;
    }
    return out_count;
}

static void project_vertex(const struct toy_surface *surface,
                           const struct vec3 *view, int pitch,
                           struct toy_screen_vertex *screen)
{
    int focal = surface->width * 3 / 4;
    screen->x = surface->width / 2 + view->x * focal / view->z;
    screen->y = surface->height / 2 + pitch - view->y * focal / view->z;
    screen->z = view->z;
}

static int draw_world_triangle(struct toy_renderer *renderer,
                               const struct camera *camera,
                               const struct vec3 *a, const struct vec3 *b,
                               const struct vec3 *c,
                               uint32_t color)
{
    struct vec3 input[3], clipped[4];
    int count, drawn = 0;
    world_to_view(camera, a, &input[0]);
    world_to_view(camera, b, &input[1]);
    world_to_view(camera, c, &input[2]);
    count = clip_near(input, 3, clipped);
    for (int i = 1; i + 1 < count; i++) {
        struct toy_screen_vertex sa, sb, sc;
        long area;
        project_vertex(&renderer->surface, &clipped[0], camera->pitch, &sa);
        project_vertex(&renderer->surface, &clipped[i], camera->pitch, &sb);
        project_vertex(&renderer->surface, &clipped[i + 1], camera->pitch, &sc);
        area = (long)(sc.x - sa.x) * (sb.y - sa.y) -
               (long)(sc.y - sa.y) * (sb.x - sa.x);
        if (area >= 0) {
            struct toy_screen_vertex swap;
            swap.x = sb.x; swap.y = sb.y; swap.z = sb.z;
            sb.x = sc.x; sb.y = sc.y; sb.z = sc.z;
            sc.x = swap.x; sc.y = swap.y; sc.z = swap.z;
        }
        drawn += toy_renderer_triangle(renderer, &sa, &sb, &sc, color);
    }
    return drawn;
}

static int draw_quad(struct toy_renderer *renderer, const struct camera *camera,
                     const struct vec3 *a, const struct vec3 *b,
                     const struct vec3 *c, const struct vec3 *d,
                     uint32_t color)
{
    return draw_world_triangle(renderer, camera, a, b, c, color) +
           draw_world_triangle(renderer, camera, a, c, d, color);
}

static int draw_box(struct toy_renderer *renderer, const struct camera *camera,
                    const struct box *box)
{
    struct vec3 a, b, c, d, e, f, g, h;
    int pixels = 0;
    a.x = box->minx; a.y = -900; a.z = box->minz;
    b.x = box->maxx; b.y = -900; b.z = box->minz;
    c.x = box->maxx; c.y = -900; c.z = box->maxz;
    d.x = box->minx; d.y = -900; d.z = box->maxz;
    e.x = a.x; e.y = box->height; e.z = a.z;
    f.x = b.x; f.y = box->height; f.z = b.z;
    g.x = c.x; g.y = box->height; g.z = c.z;
    h.x = d.x; h.y = box->height; h.z = d.z;
    pixels += draw_quad(renderer, camera, &a, &b, &f, &e, box->color);
    pixels += draw_quad(renderer, camera, &b, &c, &g, &f, box->color + 0x080808);
    pixels += draw_quad(renderer, camera, &c, &d, &h, &g, box->color);
    pixels += draw_quad(renderer, camera, &d, &a, &e, &h, box->color + 0x080808);
    pixels += draw_quad(renderer, camera, &e, &f, &g, &h, box->color + 0x181818);
    return pixels;
}

static int render_scene(struct toy_renderer *renderer, const struct camera *camera)
{
    int pixels = 0;
    struct vec3 a, b, c, d;
    for (int z = -6000; z < 6000; z += 1000) {
        for (int x = -6000; x < 6000; x += 1000) {
            uint32_t color = (((x + z) / 1000) & 1) ? 0x30343A : 0x272B31;
            a.x = x;        a.y = -900; a.z = z;
            b.x = x + 1000; b.y = -900; b.z = z;
            c.x = x + 1000; c.y = -900; c.z = z + 1000;
            d.x = x;        d.y = -900; d.z = z + 1000;
            pixels += draw_quad(renderer, camera, &a, &b, &c, &d, color);
        }
    }
    a.x = -6000; a.y = -900; a.z = -6000;
    b.x =  6000; b.y = -900; b.z = -6000;
    c.x =  6000; c.y = 1800; c.z = -6000;
    d.x = -6000; d.y = 1800; d.z = -6000;
    pixels += draw_quad(renderer, camera, &a, &b, &c, &d, 0x555B68);
    a.z = 6000; b.z = 6000; c.z = 6000; d.z = 6000;
    pixels += draw_quad(renderer, camera, &a, &b, &c, &d, 0x4C5260);
    a.x = -6000; a.y = -900; a.z = -6000;
    b.x = -6000; b.y = -900; b.z =  6000;
    c.x = -6000; c.y = 1800; c.z =  6000;
    d.x = -6000; d.y = 1800; d.z = -6000;
    pixels += draw_quad(renderer, camera, &a, &b, &c, &d, 0x5E5965);
    a.x = 6000; b.x = 6000; c.x = 6000; d.x = 6000;
    pixels += draw_quad(renderer, camera, &a, &b, &c, &d, 0x56515D);
    for (int i = 0; i < 3; i++) pixels += draw_box(renderer, camera, &obstacles[i]);
    return pixels;
}

static int position_blocked(int x, int z)
{
    if (x < -ROOM_LIMIT || x > ROOM_LIMIT ||
        z < -ROOM_LIMIT || z > ROOM_LIMIT) return 1;
    for (int i = 0; i < 3; i++) {
        if (x + PLAYER_RADIUS > obstacles[i].minx &&
            x - PLAYER_RADIUS < obstacles[i].maxx &&
            z + PLAYER_RADIUS > obstacles[i].minz &&
            z - PLAYER_RADIUS < obstacles[i].maxz) return 1;
    }
    return 0;
}

static void update_player(struct camera *camera, const struct toy_input *input)
{
    int forward = toy_input_down(input, KEY_W) - toy_input_down(input, KEY_S);
    int strafe = toy_input_down(input, KEY_D) - toy_input_down(input, KEY_A);
    int dx = (camera->sy * forward + camera->cy * strafe) * MOVE_STEP / 1024;
    int dz = (camera->cy * forward - camera->sy * strafe) * MOVE_STEP / 1024;
    int next_x = camera->x + dx;
    int next_z = camera->z + dz;
    if (!position_blocked(next_x, camera->z)) camera->x = next_x;
    if (!position_blocked(camera->x, next_z)) camera->z = next_z;
}

static void update_mouse(struct camera *camera, const struct toy_input *input)
{
    int turn = clampi(input->relative_x * 3, -128, 128);
    int old_sy = camera->sy;
    camera->sy = (old_sy * 1024 + camera->cy * turn) / 1024;
    camera->cy = (camera->cy * 1024 - old_sy * turn) / 1024;
    camera->pitch = clampi(camera->pitch + input->relative_y * 2, -240, 240);
}

static void draw_crosshair(struct toy_surface *surface)
{
    int cx = surface->width / 2, cy = surface->height / 2;
    for (int d = -6; d <= 6; d++) {
        if (d < -2 || d > 2) {
            put_pixel(surface, cx + d, cy, 0xF0F0F0);
            put_pixel(surface, cx, cy + d, 0xF0F0F0);
        }
    }
}

static int run_logic_test(void)
{
    struct camera camera;
    struct toy_input input;
    struct vec3 triangle[3], clipped[4];
    struct toy_surface surface;
    struct toy_renderer renderer;
    uint32_t *pixels;
    int count;
    toy_input_init(&input);
    camera.x = 0; camera.z = -4200;
    camera.sy = 0; camera.cy = 1024; camera.pitch = 0;
    input.key_down[KEY_W] = 1;
    for (int i = 0; i < 10; i++) update_player(&camera, &input);
    if (camera.x != 0 || camera.z != -3480) return 1;

    camera.x = -1200; camera.z = 0;
    for (int i = 0; i < 10; i++) update_player(&camera, &input);
    if (camera.z >= obstacles[0].minz - PLAYER_RADIUS) return 2;

    triangle[0].x = -100; triangle[0].y = 0; triangle[0].z = 100;
    triangle[1].x =  100; triangle[1].y = 0; triangle[1].z = 400;
    triangle[2].x =    0; triangle[2].y = 100; triangle[2].z = 400;
    count = clip_near(triangle, 3, clipped);
    if (count != 4) return 3;
    for (int i = 0; i < count; i++)
        if (clipped[i].z < NEAR_Z) return 4;
    pixels = tlibc_malloc(320 * 180 * sizeof(uint32_t));
    if (!pixels) return 5;
    surface.pixels = pixels;
    surface.width = 320;
    surface.height = 180;
    surface.stride = 320 * sizeof(uint32_t);
    camera.x = 0; camera.z = -4200;
    toy_renderer_init(&renderer);
    if (toy_renderer_begin(&renderer, &surface, 0x151922) < 0 ||
        render_scene(&renderer, &camera) <= 0) {
        toy_renderer_destroy(&renderer);
        tlibc_free(pixels);
        return 6;
    }
    toy_renderer_destroy(&renderer);
    tlibc_free(pixels);
    __printf("wayland_fps: logic test passed\n");
    return 0;
}

int main(int argc, char **argv)
{
    struct toy_window *window;
    struct toy_window_events events;
    struct toy_input input;
    struct toy_surface surface;
    struct toy_renderer renderer;
    struct camera camera;
    long last_time, accumulator = 0;
    int running = 1, pointer_lock_requested = 0;
    int frame_limit = 0, rendered_frames = 0, scene_pixels = 0;

    if (argc == 2 && strcmp(argv[1], "--logic-test") == 0)
        return run_logic_test();
    if (argc == 3 && strcmp(argv[1], "--frames") == 0) {
        const char *p = argv[2];
        while (*p >= '0' && *p <= '9') frame_limit = frame_limit * 10 + (*p++ - '0');
    }
    camera.x = 0; camera.z = -4200;
    camera.sy = 0; camera.cy = 1024; camera.pitch = 0;
    toy_input_init(&input);
    toy_renderer_init(&renderer);
    window = toy_window_open("Toyc FPS Graybox", 800, 450);
    if (!window) {
        __fprintf(2, "wayland_fps: cannot create Wayland window\n");
        return 1;
    }
    __printf("wayland_fps: click to capture, WASD move, mouse look, Esc release/quit\n");
    last_time = monotonic_us();
    while (running) {
        long now, elapsed;
        int logic_steps = 0;
        toy_input_begin_frame(&input);
        if (toy_window_poll(window, &events, 1000) < 0) break;
        toy_input_apply(&input, &events);
        if (events.pointer_lock_changed && !events.pointer_locked)
            pointer_lock_requested = 0;
        if (events.button_pressed && events.button == BTN_LEFT &&
            toy_window_set_pointer_lock(window, 1) > 0)
            pointer_lock_requested = 1;
        if (toy_input_pressed(&input, KEY_ESC)) {
            if (input.pointer_locked || pointer_lock_requested) {
                toy_window_set_pointer_lock(window, 0);
                pointer_lock_requested = 0;
            } else running = 0;
        }
        if (events.close_requested) running = 0;
        if (!running) break;
        if (input.pointer_locked) update_mouse(&camera, &input);
        now = monotonic_us();
        elapsed = now - last_time;
        last_time = now;
        if (elapsed < 0) elapsed = 0;
        if (elapsed > MAX_FRAME_US) elapsed = MAX_FRAME_US;
        accumulator += elapsed;
        while (accumulator >= FIXED_STEP_US && logic_steps < MAX_LOGIC_STEPS) {
            update_player(&camera, &input);
            accumulator -= FIXED_STEP_US;
            logic_steps++;
        }
        if (accumulator >= FIXED_STEP_US) accumulator %= FIXED_STEP_US;
        int ready = toy_window_begin_frame(window, &surface);
        if (ready < 0) break;
        if (ready > 0) {
            if (toy_renderer_begin(&renderer, &surface, 0x151922) < 0) break;
            scene_pixels = render_scene(&renderer, &camera);
            draw_crosshair(&surface);
            if (toy_window_present(window) < 0) break;
            rendered_frames++;
            if (frame_limit > 0 && rendered_frames >= frame_limit) running = 0;
        }
    }
    toy_renderer_destroy(&renderer);
    toy_window_close(window);
    __printf("wayland_fps: %d frames, %d scene pixels, position=(%d,%d)\n",
             rendered_frames, scene_pixels, camera.x, camera.z);
    return rendered_frames > 0 && scene_pixels == 0 ? 2 : 0;
}
