/*
 * wayland_fps — Toyc software-rendered first-person zombie-horde shooter.
 *
 * Controls: left click captures the pointer, mouse or arrows look, WASD moves,
 * left click / Space fire, R reloads, Esc pauses/releases the pointer or quits.
 * Rendering and input are freestanding; game rules live in lib/game.
 */

#include "core.h"
#include "string.h"
#include "tlibc_everything.h"
#include "toy_window.h"
#include "toy_renderer.h"
#include "toy_input.h"
#include "toy_game.h"
#include "fb_draw.h"
#include "fb_font.h"
#include "linux_audio.h"
#include "pthread.h"
#include "errno.h"

#define KEY_ESC   1
#define KEY_R     19
#define KEY_W     17
#define KEY_A     30
#define KEY_S     31
#define KEY_D     32
#define KEY_SPACE 57
#define KEY_UP    103
#define KEY_LEFT  105
#define KEY_RIGHT 106
#define KEY_DOWN  108
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

/* 游戏世界（与 obstacles 同 xz 范围，交给 lib/game 做碰撞与遮挡） */
static const struct toy_game_box bounds[3] = {
    {-1700, -700,  300, 1700},
    {  600, 1800, -900,  100},
    { 2300, 3200, 1800, 3000}
};

static struct toy_game game;   /* 游戏规则状态（main / logic-test 共用） */

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
    return toy_game_position_blocked(&game, x, z, PLAYER_RADIUS);
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

static void rotate_camera(struct camera *camera, int turn, int pitch)
{
    int old_sy = camera->sy;
    camera->sy = (old_sy * 1024 + camera->cy * turn) / 1024;
    camera->cy = (camera->cy * 1024 - old_sy * turn) / 1024;
    camera->pitch = clampi(camera->pitch + pitch, -240, 240);
}

static void update_mouse(struct camera *camera, int relative_x, int relative_y)
{
    rotate_camera(camera, clampi(relative_x * 3, -128, 128), -relative_y * 2);
}

static void update_keyboard_look(struct camera *camera,
                                 const struct toy_input *input)
{
    int turn = toy_input_down(input, KEY_RIGHT) -
               toy_input_down(input, KEY_LEFT);
    int pitch = toy_input_down(input, KEY_UP) -
                toy_input_down(input, KEY_DOWN);
    if (turn || pitch) rotate_camera(camera, turn * 24, pitch * 5);
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

static void fill_rect(struct toy_surface *surface, int x, int y,
                      int width, int height, uint32_t color)
{
    int right = x + width, bottom = y + height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > surface->width) right = surface->width;
    if (bottom > surface->height) bottom = surface->height;
    for (int py = y; py < bottom; py++)
        for (int px = x; px < right; px++) put_pixel(surface, px, py, color);
}

static void draw_pause_overlay(struct toy_surface *surface)
{
    int panel_w = surface->width / 3;
    int panel_h = surface->height / 3;
    int x = (surface->width - panel_w) / 2;
    int y = (surface->height - panel_h) / 2;
    int bar_w = panel_w / 10;
    int bar_h = panel_h / 2;
    fill_rect(surface, x - 3, y - 3, panel_w + 6, panel_h + 6, 0xD88A32);
    fill_rect(surface, x, y, panel_w, panel_h, 0x171B24);
    fill_rect(surface, surface->width / 2 - bar_w - bar_w / 2,
              surface->height / 2 - bar_h / 2, bar_w, bar_h, 0xE7E9EC);
    fill_rect(surface, surface->width / 2 + bar_w / 2,
              surface->height / 2 - bar_h / 2, bar_w, bar_h, 0xE7E9EC);
}

/* 敌人彩盒：存活灰绿（受击闪红/命中闪白），倒地压扁 */
static int render_enemies(struct toy_renderer *renderer,
                          const struct camera *camera)
{
    int pixels = 0;
    for (int i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        const struct toy_game_enemy *e = &game.enemies[i];
        struct box b;
        struct vec3 center, view;
        if (e->active == 0) continue;
        center.x = e->x;
        center.y = 0;
        center.z = e->z;
        world_to_view(camera, &center, &view);
        if (view.z > 3500) continue;   /* 远距剔除：该距离下盒高不足 11px */
        b.minx = e->x - TOY_GAME_ENEMY_HALF;
        b.maxx = e->x + TOY_GAME_ENEMY_HALF;
        b.minz = e->z - TOY_GAME_ENEMY_HALF;
        b.maxz = e->z + TOY_GAME_ENEMY_HALF;
        if (e->active == 2) {
            b.height = TOY_GAME_ENEMY_HEIGHT * e->dying_ms / TOY_GAME_DYING_MS;
            b.color = 0x5A1A1A;
        } else {
            b.height = TOY_GAME_ENEMY_HEIGHT;
            if (e->hurt > 0) b.color = 0xBB3333;
            else if (e->flash > 0) b.color = 0xFFFFFF;
            else b.color = 0x4A5D3A;
        }
        pixels += draw_box(renderer, camera, &b);
    }
    return pixels;
}

static void render_hud(struct toy_surface *surface)
{
    char line[96];
    int n;
    n = snprintf(line, sizeof(line), "HP %d  MAG %d/%d  KILLS %d  WAVE %d",
                 game.hp, game.ammo_mag, game.ammo_reserve,
                 game.kills, game.wave);
    if (n > 0)
        fb_draw_string((unsigned char *)surface->pixels, 8, 8,
                       line, 0xE7E9EC, surface->stride);
    if (game.reloading)
        fb_draw_string((unsigned char *)surface->pixels, 8, 8 + FB_FONT_H,
                       "RELOADING...", 0xD88A32, surface->stride);
}

/* 受击红屏：棋盘隔像素填充（省一半写入量） */
static void render_damage_flash(struct toy_surface *surface)
{
    if (game.damage_flash_ms <= 0) return;
    for (int y = 0; y < surface->height; y++) {
        uint32_t *row = (uint32_t *)((unsigned char *)surface->pixels +
                                     y * surface->stride);
        for (int x = y & 1; x < surface->width; x += 2) row[x] = 0xAA0000;
    }
}

static void render_muzzle_flash(struct toy_surface *surface)
{
    if (game.muzzle_flash_ms <= 0) return;
    fb_fill_circle((unsigned char *)surface->pixels,
                   surface->width / 2, surface->height / 2, 8,
                   0xFFCC80, surface->stride);
}

static void draw_game_over_panel(struct toy_surface *surface)
{
    char line[96];
    int panel_w = surface->width / 3;
    int panel_h = surface->height / 3;
    int x = (surface->width - panel_w) / 2;
    int y = (surface->height - panel_h) / 2;
    fill_rect(surface, x - 3, y - 3, panel_w + 6, panel_h + 6, 0xD88A32);
    fill_rect(surface, x, y, panel_w, panel_h, 0x171B24);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * 8) / 2, y + 28,
                   "YOU DIED", 0xE7E9EC, surface->stride);
    snprintf(line, sizeof(line), "WAVE %d  KILLS %d", game.wave, game.kills);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * (int)strlen(line)) / 2, y + 60,
                   line, 0xE7E9EC, surface->stride);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * 21) / 2, y + 104,
                   "R restart   Esc quit", 0xD88A32, surface->stride);
}

/* ── 音效输出：音频线程 + futex 环形缓冲（mp3_player 范式） ────── */

#define SFX_RING_SHIFT 13                /* 8192 帧 ≈ 186ms@44.1k */
#define SFX_RING_SIZE (1 << SFX_RING_SHIFT)
#define SFX_RING_MASK (SFX_RING_SIZE - 1)
#define SFX_BLOCK_FRAMES 2048

struct sfx_ring {
    short buf[SFX_RING_SIZE * 2];
    volatile unsigned int wpos;   /* 生产者写入（单调帧计数） */
    volatile unsigned int rpos;   /* 消费者读取 */
};

struct audio_ctx {
    struct sfx_ring ring;
    struct tlibc_pcm pcm;
    pthread_t thread;
    volatile int quit;
    int running;
};

/* 生产者非阻塞：空间不足丢弃整块（SFX 最佳努力，丢无感知） */
static void ring_push_drop(struct sfx_ring *r, const short *data, int frames)
{
    int avail;
    unsigned int wp;
    if (frames <= 0 || frames >= SFX_RING_SIZE) return;
    avail = (int)(r->wpos - r->rpos);
    if (avail + frames > SFX_RING_SIZE) return;
    wp = r->wpos & SFX_RING_MASK;
    for (int i = 0; i < frames; i++) {
        r->buf[wp * 2] = data[2 * i];
        r->buf[wp * 2 + 1] = data[2 * i + 1];
        wp = (wp + 1) & SFX_RING_MASK;
    }
    __sync_synchronize();
    r->wpos += (unsigned int)frames;
    __futex((unsigned int *)&r->wpos, 1 /* FUTEX_WAKE */, -1, NULL, NULL, 0);
}

/* 消费者：等待数据并读取最多 max_frames 帧 */
static int ring_wait_read(struct sfx_ring *r, short *dst, int max_frames)
{
    for (;;) {
        int avail = (int)(r->wpos - r->rpos);
        if (avail > 0) {
            unsigned int rp;
            if (avail > max_frames) avail = max_frames;
            rp = r->rpos & SFX_RING_MASK;
            for (int i = 0; i < avail; i++) {
                dst[2 * i] = r->buf[rp * 2];
                dst[2 * i + 1] = r->buf[rp * 2 + 1];
                rp = (rp + 1) & SFX_RING_MASK;
            }
            __sync_synchronize();
            r->rpos += (unsigned int)avail;
            __futex((unsigned int *)&r->rpos, 1 /* FUTEX_WAKE */, -1,
                    NULL, NULL, 0);
            return avail;
        }
        __futex((unsigned int *)&r->wpos, 0 /* FUTEX_WAIT */, r->wpos,
                NULL, NULL, 0);
    }
}

static void *audio_thread_func(void *arg)
{
    struct audio_ctx *ctx = (struct audio_ctx *)arg;
    short play_buf[SFX_BLOCK_FRAMES * 2];
    for (;;) {
        int frames;
        long ret;
        if (ctx->quit) break;
        frames = ring_wait_read(&ctx->ring, play_buf, SFX_BLOCK_FRAMES);
        if (ctx->quit) break;
        ret = tlibc_pcm_write(&ctx->pcm, play_buf, (unsigned long)frames);
        if (ret == -EPIPE) {
            /* XRUN：恢复设备后继续 */
            __ioctl(ctx->pcm.fd, SNDRV_PCM_IOCTL_PREPARE, 0);
            continue;
        }
        if (ret < 0) break;   /* 设备级错误：静默停声 */
    }
    return NULL;
}

static int audio_start(struct audio_ctx *ctx)
{
    memset(ctx, 0, sizeof(struct audio_ctx));
    if (tlibc_pcm_open(&ctx->pcm, NULL, 2, TOY_SFX_RATE, 16) < 0) return -1;
    if (tlibc_pcm_configure(&ctx->pcm) < 0) {
        tlibc_pcm_close(&ctx->pcm);
        return -1;
    }
    if (pthread_create(&ctx->thread, NULL, audio_thread_func, ctx) != 0) {
        tlibc_pcm_close(&ctx->pcm);
        return -1;
    }
    ctx->running = 1;
    return 0;
}

static void audio_stop(struct audio_ctx *ctx)
{
    if (!ctx->running) return;
    ctx->quit = 1;
    __futex((unsigned int *)&ctx->ring.wpos, 1 /* FUTEX_WAKE */, -1,
            NULL, NULL, 0);
    pthread_join(ctx->thread, NULL);
    tlibc_pcm_close(&ctx->pcm);
    ctx->running = 0;
}

/* 事件 → 音效（每帧上限 4 个，防同帧爆发撑爆 voice） */
static void play_game_events(struct toy_sfx *sfx)
{
    unsigned char evs[TOY_GAME_MAX_EVENTS];
    int ne = toy_game_drain_events(&game, evs, TOY_GAME_MAX_EVENTS);
    int i;
    if (ne > 4) ne = 4;
    for (i = 0; i < ne; i++) {
        switch (evs[i]) {
        case TOY_GAME_EV_SHOOT: toy_sfx_play(sfx, TOY_SFX_GUNSHOT); break;
        case TOY_GAME_EV_DRY_FIRE: toy_sfx_play(sfx, TOY_SFX_DRY_FIRE); break;
        case TOY_GAME_EV_RELOAD_START: toy_sfx_play(sfx, TOY_SFX_RELOAD_START); break;
        case TOY_GAME_EV_RELOAD_DONE: toy_sfx_play(sfx, TOY_SFX_RELOAD_DONE); break;
        case TOY_GAME_EV_KILL: toy_sfx_play(sfx, TOY_SFX_KILL); break;
        case TOY_GAME_EV_BITE: toy_sfx_play(sfx, TOY_SFX_BITE); break;
        case TOY_GAME_EV_PLAYER_DEATH: toy_sfx_play(sfx, TOY_SFX_PLAYER_DEATH); break;
        default: break;
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
    toy_game_init(&game, 1);
    toy_game_set_world(&game, bounds, 3, ROOM_LIMIT);
    toy_input_init(&input);
    camera.x = 0; camera.z = -4200;
    camera.sy = 0; camera.cy = 1024; camera.pitch = 0;
    input.key_down[KEY_W] = 1;
    for (int i = 0; i < 10; i++) update_player(&camera, &input);
    if (camera.x != 0 || camera.z != -3480) return 1;

    camera.x = -1200; camera.z = 0;
    for (int i = 0; i < 10; i++) update_player(&camera, &input);
    if (camera.z >= obstacles[0].minz - PLAYER_RADIUS) return 2;

    input.key_down[KEY_W] = 0;
    input.key_down[KEY_RIGHT] = 1;
    update_keyboard_look(&camera, &input);
    /* 首次转向时 old_sy=0，定点旋转 cy 恰为 1024 不变，只断言转向生效 */
    if (camera.sy <= 0) return 3;
    input.key_down[KEY_RIGHT] = 0;
    input.key_down[KEY_UP] = 1;
    update_keyboard_look(&camera, &input);
    if (camera.pitch <= 0) return 4;

    triangle[0].x = -100; triangle[0].y = 0; triangle[0].z = 100;
    triangle[1].x =  100; triangle[1].y = 0; triangle[1].z = 400;
    triangle[2].x =    0; triangle[2].y = 100; triangle[2].z = 400;
    count = clip_near(triangle, 3, clipped);
    if (count != 4) return 5;
    for (int i = 0; i < count; i++)
        if (clipped[i].z < NEAR_Z) return 6;
    pixels = tlibc_malloc(320 * 180 * sizeof(uint32_t));
    if (!pixels) return 7;
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
        return 8;
    }
    toy_renderer_destroy(&renderer);
    tlibc_free(pixels);

    /* 游戏规则冒烟：同 seed 推进 500 步，两遍运行状态一致 */
    {
        struct toy_game smoke;
        int pass, i;
        int first_wave = 0, first_kills = 0, first_hp = 0;
        for (pass = 0; pass < 2; pass++) {
            toy_game_init(&smoke, 1234);
            toy_game_set_world(&smoke, bounds, 3, ROOM_LIMIT);
            smoke.px = 0;
            smoke.pz = -4200;
            for (i = 0; i < 500; i++)
                toy_game_update(&smoke, NULL, 0, 0, 1024, 16);
            if (pass == 0) {
                first_wave = smoke.wave;
                first_kills = smoke.kills;
                first_hp = smoke.hp;
            } else if (smoke.wave != first_wave || smoke.kills != first_kills ||
                       smoke.hp != first_hp) {
                return 9;
            }
        }
    }
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
    int running = 1, pointer_lock_requested = 0, paused = 1;
    int last_pointer_x = 0, last_pointer_y = 0, have_pointer_position = 0;
    int frame_limit = 0, rendered_frames = 0, scene_pixels = 0;
    int fire_edge = 0;
    struct audio_ctx audio;
    struct toy_sfx sfx;
    short block[SFX_BLOCK_FRAMES * 2];
    uint64_t seed;

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
    if (__getrandom(&seed, sizeof(seed), 0) < 0)
        seed = (uint64_t)monotonic_us();
    if (seed == 0) seed = 1;
    toy_game_init(&game, seed);
    toy_game_set_world(&game, bounds, 3, ROOM_LIMIT);
    game.px = camera.x;
    game.pz = camera.z;
    window = toy_window_open("Toyc FPS Zombies", 800, 450);
    if (!window) {
        __fprintf(2, "wayland_fps: cannot create Wayland window\n");
        return 1;
    }
    __printf("wayland_fps: click to play; mouse/arrows look, WASD moves, "
             "click/Space fire, R reload, Esc pauses/exits\n");
    toy_sfx_init(&sfx, TOY_SFX_RATE);
    if (audio_start(&audio) < 0) {
        __printf("wayland_fps: audio unavailable, playing silent\n");
        sfx.enabled = 0;
    }
    last_time = monotonic_us();
    while (running) {
        long now, elapsed;
        int logic_steps = 0;
        int resumed = 0;
        toy_input_begin_frame(&input);
        if (toy_window_poll(window, &events, 1000) < 0) break;
        toy_input_apply(&input, &events);
        if (events.pointer_lock_changed) {
            if (events.pointer_locked)
                __printf("wayland_fps: pointer constraint activated\n");
            else {
                __printf("wayland_fps: pointer constraint released\n");
                pointer_lock_requested = 0;
            }
        }
        if (paused && events.button_pressed && events.button == BTN_LEFT) {
            int capture_result = toy_window_set_pointer_lock(window, 1);
            pointer_lock_requested = capture_result > 0;
            paused = 0;
            last_pointer_x = input.pointer_x;
            last_pointer_y = input.pointer_y;
            have_pointer_position = 1;
            resumed = 1;
            __printf("wayland_fps: resumed, pointer constraint %s\n",
                     pointer_lock_requested ? "requested" : "unavailable");
        }
        if (toy_input_pressed(&input, KEY_ESC)) {
            if (game.state == TOY_GAME_OVER) running = 0;
            else if (!paused) {
                toy_window_set_pointer_lock(window, 0);
                pointer_lock_requested = 0;
                paused = 1;
                __printf("wayland_fps: paused, pointer released\n");
            } else running = 0;
        }
        /* 射击输入：每帧只取一次边沿（恢复点击帧不开火） */
        if (!paused && !resumed && events.button_pressed && events.button == BTN_LEFT)
            fire_edge = 1;
        if (!paused && !resumed && toy_input_pressed(&input, KEY_SPACE))
            fire_edge = 1;
        if (events.close_requested) running = 0;
        if (!running) break;
        /* Some compositors acknowledge locked asynchronously. Relative
         * events received after our accepted request are already valid. */
        if (!paused && (input.pointer_locked || pointer_lock_requested) &&
            events.relative_moved) {
            update_mouse(&camera, input.relative_x, input.relative_y);
        } else if (!paused && pointer_lock_requested && input.pointer_moved) {
            if (have_pointer_position)
                update_mouse(&camera, input.pointer_x - last_pointer_x,
                              input.pointer_y - last_pointer_y);
            last_pointer_x = input.pointer_x;
            last_pointer_y = input.pointer_y;
            have_pointer_position = 1;
        }
        now = monotonic_us();
        elapsed = now - last_time;
        last_time = now;
        if (elapsed < 0) elapsed = 0;
        if (elapsed > MAX_FRAME_US) elapsed = MAX_FRAME_US;
        accumulator += elapsed;
        while (accumulator >= FIXED_STEP_US && logic_steps < MAX_LOGIC_STEPS) {
            if (!paused) {
                if (game.state == TOY_GAME_PLAYING) {
                    update_player(&camera, &input);
                    update_keyboard_look(&camera, &input);
                    game.px = camera.x;
                    game.pz = camera.z;
                    toy_game_update(&game, input.key_pressed, fire_edge,
                                    camera.sy, camera.cy, FIXED_STEP_US / 1000);
                    fire_edge = 0;
                } else if (toy_input_pressed(&input, KEY_R)) {
                    /* 死亡结算：R 重开 */
                    camera.x = 0; camera.z = -4200;
                    camera.sy = 0; camera.cy = 1024; camera.pitch = 0;
                    toy_game_init(&game, seed);
                    game.px = camera.x;
                    game.pz = camera.z;
                    fire_edge = 0;
                }
            }
            accumulator -= FIXED_STEP_US;
            logic_steps++;
        }
        if (accumulator >= FIXED_STEP_US) accumulator %= FIXED_STEP_US;
        int ready = toy_window_begin_frame(window, &surface);
        if (ready < 0) break;
        if (ready > 0) {
            if (toy_renderer_begin(&renderer, &surface, 0x151922) < 0) break;
            scene_pixels = render_scene(&renderer, &camera);
            scene_pixels += render_enemies(&renderer, &camera);
            if (game.state == TOY_GAME_OVER) {
                draw_game_over_panel(&surface);
            } else if (paused) {
                draw_pause_overlay(&surface);
            } else {
                draw_crosshair(&surface);
                render_hud(&surface);
                render_muzzle_flash(&surface);
            }
            render_damage_flash(&surface);
            /* 音效：事件 → voice；按真实流逝时间合成样本块推给音频线程 */
            play_game_events(&sfx);
            if (sfx.enabled) {
                int sfx_frames = (int)((long long)elapsed * TOY_SFX_RATE / 1000000);
                if (sfx_frames < 1) sfx_frames = 1;
                if (sfx_frames > SFX_BLOCK_FRAMES) sfx_frames = SFX_BLOCK_FRAMES;
                toy_sfx_render(&sfx, block, sfx_frames);
                ring_push_drop(&audio.ring, block, sfx_frames);
            }
            if (toy_window_present(window) < 0) break;
            rendered_frames++;
            if (frame_limit > 0 && rendered_frames >= frame_limit) running = 0;
        }
    }
    audio_stop(&audio);
    toy_renderer_destroy(&renderer);
    toy_window_close(window);
    __printf("wayland_fps: %d frames, %d scene pixels, position=(%d,%d)\n",
             rendered_frames, scene_pixels, camera.x, camera.z);
    return rendered_frames > 0 && scene_pixels == 0 ? 2 : 0;
}
