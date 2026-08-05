/*
 * wayland_fps — Toyc software-rendered safe-room-to-safe-room zombie shooter.
 *
 * Controls: left click captures the pointer, mouse or arrows look (pitch is
 * limited to 75 degrees up/down), WASD moves, left click / Space fire (Space
 * held = SMG full auto), R reloads, 1/2 switch weapon slots, E interacts with
 * highlighted pickups, Esc pauses/releases the pointer or quits. A first
 * person weapon viewmodel sits bottom-right; tracers originate from the gun
 * muzzle. Rendering and input are freestanding;
 * game rules live in lib/game.
 */

#include "core.h"
#include "string.h"
#include "tlibc_everything.h"
#include "toy_window.h"
#include "toy_renderer.h"
#include "toy_assets.h"
#include "toy_input.h"
#include "toy_game.h"
#include "fb_draw.h"
#include "fb_font.h"
#include "linux_audio.h"
#include "toy_audio.h"
#include "toy_map.h"
#include "pthread.h"
#include "errno.h"
#include "math.h"

#define KEY_ESC   1
#define KEY_1     2
#define KEY_2     3
#define KEY_E     18
#define KEY_R     19
#define KEY_ENTER 28
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
#define PLAYER_RADIUS 180
#define MOVE_STEP 76
#define UV_ONE 65536
#define PITCH_LIMIT_SY 989        /* sin(75°) * 1024 */
#define PITCH_LIMIT_CY 265        /* cos(75°) * 1024 */

struct vec3 { int x, y, z; };
/* 朝向：sy/cy = 偏航 sin/cos，pitch_sy/pitch_cy = 俯仰 sin/cos（均 1024 定点）。 */
struct camera { int x, z, sy, cy, pitch_sy, pitch_cy; };
struct box { int minx, maxx, minz, maxz, height; uint32_t color; };
struct control_settings { int mouse_level, keyboard_level; };
struct pause_menu { int selected; };

#define PAUSE_ITEM_RESUME   0
#define PAUSE_ITEM_MOUSE    1
#define PAUSE_ITEM_KEYBOARD 2
#define PAUSE_ITEM_QUIT     3
#define PAUSE_ITEM_COUNT    4

#if 0
static const struct box obstacles[OBSTACLE_COUNT] = {
    {-1700, -700,  300, 1700, 1000, 0x755A47},
    {  600, 1800, -900,  100, 1450, 0x49677D},
    { 2300, 3200, 1800, 3000,  750, 0x64704B},
    /* start room */
    {-2000, -1800, -5700, -3800, 1300, 0x3F6751},
    { 1800,  2000, -5700, -3800, 1300, 0x3F6751},
    {-1800,  -600, -4000, -3800, 1300, 0x3F6751},
    {  600,  1800, -4000, -3800, 1300, 0x3F6751},
    /* exit room */
    {-2000, -1800,  3800,  5700, 1300, 0x477A58},
    { 1800,  2000,  3800,  5700, 1300, 0x477A58},
    {-1800,  -600,  3800,  4000, 1300, 0x477A58},
    {  600,  1800,  3800,  4000, 1300, 0x477A58}
};

/* 游戏世界（与 obstacles 同 xz 范围，交给 lib/game 做碰撞与遮挡） */
static const struct toy_game_box bounds[OBSTACLE_COUNT] = {
    {-1700, -700,  300, 1700},
    {  600, 1800, -900,  100},
    { 2300, 3200, 1800, 3000},
    {-2000, -1800, -5700, -3800},
    { 1800,  2000, -5700, -3800},
    {-1800,  -600, -4000, -3800},
    {  600,  1800, -4000, -3800},
    {-2000, -1800,  3800,  5700},
    { 1800,  2000,  3800,  5700},
    {-1800,  -600,  3800,  4000},
    {  600,  1800,  3800,  4000}
};

/* 经典闯关路线：南侧起点安全室，穿过主体区域后抵达北侧终点安全室。 */
static const struct toy_game_box safe_rooms[2] = {
    {-1800, 1800, -5700, -4000},
    {-1800, 1800,  4000,  5700}
};

/* 固定刷怪区域；渲染为红色地面，便于玩家识别危险区。 */
static const struct toy_game_box spawn_zones[3] = {
    {-5200, -3500, -2600, -800},
    { 3500,  5200,  -500, 1400},
    {-3300, -1500,  2100, 3600}
};

/* 踏入黄框后，右侧 spawn_zones[1] 在有限配额的警报尸潮中开启。 */
static const struct toy_game_box alarm_zone = {
    3300, 5200, -3000, -1500
};

#endif
static struct toy_map level_map;
static struct toy_game_box map_bounds[TOY_MAP_MAX_BOXES];
static struct toy_game_box map_safe_rooms[TOY_MAP_MAX_ZONES];
static struct toy_game_box map_spawn_zones[TOY_MAP_MAX_ZONES];
static int map_spawn_count;
static struct toy_game game;   /* 游戏规则状态（main / logic-test 共用） */
static struct toy_texture_asset scene_texture;
static struct toy_texture_view scene_texture_view;
static struct toy_texture_view wall_texture_view;
static struct toy_texture_view *active_texture_view;
static int textures_enabled = 1;

static void prepare_map_rules(void)
{
    int i;
    for (i=0; i<level_map.box_count; i++) {
        map_bounds[i].minx=level_map.boxes[i].minx; map_bounds[i].maxx=level_map.boxes[i].maxx;
        map_bounds[i].minz=level_map.boxes[i].minz; map_bounds[i].maxz=level_map.boxes[i].maxz;
    }
    for (i=0; i<level_map.safe_count; i++) map_safe_rooms[i]=level_map.safe_rooms[i];
    map_spawn_count=level_map.spawn_count;
    for (i=0; i<map_spawn_count; i++) map_spawn_zones[i]=level_map.spawn_zones[i].box;
}

/* Only the distant room boundary uses the stylized wall texture. Gameplay
 * cover remains flat-shaded so texture sampling does not dominate the
 * software rasterizer. */

static void reset_interactables(void);

/* 新局和死亡重开必须走同一路径，避免 toy_game_init 清空世界配置。 */
static void reset_game(struct camera *camera, uint64_t seed)
{
    camera->x = level_map.start_x;
    camera->z = level_map.start_z;
    camera->sy = 0;
    camera->cy = 1024;
    camera->pitch_sy = 0;
    camera->pitch_cy = 1024;
    toy_game_init(&game, seed);
    toy_game_set_world(&game, map_bounds, level_map.box_count, level_map.room_limit);
    toy_game_set_campaign(&game, map_safe_rooms, level_map.safe_count,
                          map_spawn_zones, map_spawn_count);
    toy_game_set_alarm(&game, &level_map.alarm_zone, level_map.has_alarm);
    game.px = camera->x;
    game.pz = camera->z;
    reset_interactables();
}

/* ── 固定拾取点（主武器/弹药盒）：E 互动，准星对准时高亮。
 * 拾取物永远在场不消失；同武器再互动 = 补充弹药。 ─────────────── */

#define INTERACT_RANGE 1000

struct interactable { int kind; int x, z, y; };
static struct interactable interactables[TOY_MAP_MAX_PICKUPS];
static int interactable_count;
static int highlighted = -1;   /* 本帧准星对准的拾取物索引，-1=无 */

static void reset_interactables(void)
{
    int i;
    interactable_count = level_map.pickup_count;
    for (i = 0; i < interactable_count; i++) {
        interactables[i].kind = level_map.pickups[i].kind;
        interactables[i].x = level_map.pickups[i].x;
        interactables[i].z = level_map.pickups[i].z;
        interactables[i].y = level_map.pickups[i].y;
    }
}

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

/* 偏航（绕 Y 轴）后再做俯仰（绕 X 轴）的真实 3D 旋转：
 * pitch 为正 = 上仰，视平线以下的点投影到屏幕下方。 */
static void world_to_view(const struct camera *camera, const struct vec3 *world,
                          struct vec3 *view)
{
    int dx = world->x - camera->x;
    int dz = world->z - camera->z;
    int wx = (dx * camera->cy - dz * camera->sy) / 1024;
    int wz = (dx * camera->sy + dz * camera->cy) / 1024;
    view->x = wx;
    view->y = (world->y * camera->pitch_cy - wz * camera->pitch_sy) / 1024;
    view->z = (world->y * camera->pitch_sy + wz * camera->pitch_cy) / 1024;
}

static void near_intersection(const struct vec3 *a, const struct vec3 *b,
                              struct vec3 *out)
{
    long numerator = NEAR_Z - a->z;
    long denominator = b->z - a->z;
    /* Toyc-safe widening: cast operands before subtraction so a negative
     * signed-int delta is sign-extended instead of zero-extended. */
    out->x = a->x + (int)(((long)b->x - (long)a->x) *
                          numerator / denominator);
    out->y = a->y + (int)(((long)b->y - (long)a->y) *
                          numerator / denominator);
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

struct world_uv_vertex { struct vec3 p; int u, v; };

static void copy_world_uv(struct world_uv_vertex *out,
                          const struct world_uv_vertex *in)
{
    copy_vec3(&out->p, &in->p);
    out->u = in->u; out->v = in->v;
}

static void near_intersection_uv(const struct world_uv_vertex *a,
                                 const struct world_uv_vertex *b,
                                 struct world_uv_vertex *out)
{
    long numerator = NEAR_Z - a->p.z;
    long denominator = b->p.z - a->p.z;
    near_intersection(&a->p, &b->p, &out->p);
    out->u = a->u + (int)(((long)b->u - a->u) * numerator / denominator);
    out->v = a->v + (int)(((long)b->v - a->v) * numerator / denominator);
}

static int clip_near_uv(const struct world_uv_vertex *input, int count,
                        struct world_uv_vertex *output)
{
    int out_count = 0;
    struct world_uv_vertex previous;
    copy_world_uv(&previous, &input[count - 1]);
    int previous_inside = previous.p.z >= NEAR_Z;
    for (int i = 0; i < count; i++) {
        struct world_uv_vertex current;
        copy_world_uv(&current, &input[i]);
        int current_inside = current.p.z >= NEAR_Z;
        if (current_inside != previous_inside)
            near_intersection_uv(&previous, &current, &output[out_count++]);
        if (current_inside) copy_world_uv(&output[out_count++], &current);
        copy_world_uv(&previous, &current);
        previous_inside = current_inside;
    }
    return out_count;
}

static void project_vertex(const struct toy_surface *surface,
                           const struct vec3 *view,
                           struct toy_screen_vertex *screen)
{
    int focal = surface->width * 3 / 4;
    screen->x = surface->width / 2 + view->x * focal / view->z;
    screen->y = surface->height / 2 - view->y * focal / view->z;
    screen->z = view->z;
}

static void project_uv_vertex(const struct toy_surface *surface,
                              const struct vec3 *view,
                              int u, int v,
                              struct toy_screen_vertex *screen)
{
    project_vertex(surface, view, screen);
    screen->u = u; screen->v = v;
    screen->inv_z = (long)1048576 / view->z;
    screen->u_over_z = (long)u * 1048576L / view->z;
    screen->v_over_z = (long)v * 1048576L / view->z;
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
        int area;
        project_vertex(&renderer->surface, &clipped[0], &sa);
        project_vertex(&renderer->surface, &clipped[i], &sb);
        project_vertex(&renderer->surface, &clipped[i + 1], &sc);
        /* Projected coordinates are bounded here, so 32-bit area is safe and
         * avoids Toyc's signed int-to-long promotion bug. */
        area = (sc.x - sa.x) * (sb.y - sa.y) -
               (sc.y - sa.y) * (sb.x - sa.x);
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

static int draw_world_triangle_tex(struct toy_renderer *renderer,
                                    const struct camera *camera,
                                    const struct world_uv_vertex *a,
                                    const struct world_uv_vertex *b,
                                    const struct world_uv_vertex *c)
{
    struct world_uv_vertex input[3], clipped[4];
    int count, drawn = 0;
    world_to_view(camera, &a->p, &input[0].p);
    world_to_view(camera, &b->p, &input[1].p);
    world_to_view(camera, &c->p, &input[2].p);
    input[0].u = a->u; input[0].v = a->v;
    input[1].u = b->u; input[1].v = b->v;
    input[2].u = c->u; input[2].v = c->v;
    count = clip_near_uv(input, 3, clipped);
    for (int i = 1; i + 1 < count; i++) {
        struct toy_screen_vertex sa, sb, sc;
        int area;
        project_uv_vertex(&renderer->surface, &clipped[0].p,
                          clipped[0].u, clipped[0].v, &sa);
        project_uv_vertex(&renderer->surface, &clipped[i].p,
                          clipped[i].u, clipped[i].v, &sb);
        project_uv_vertex(&renderer->surface, &clipped[i + 1].p,
                          clipped[i + 1].u, clipped[i + 1].v, &sc);
        area = (sc.x - sa.x) * (sb.y - sa.y) -
               (sc.y - sa.y) * (sb.x - sa.x);
        if (area >= 0) {
            struct toy_screen_vertex swap;
            swap.x=sb.x; swap.y=sb.y; swap.z=sb.z;
            swap.u=sb.u; swap.v=sb.v; swap.inv_z=sb.inv_z;
            swap.u_over_z=sb.u_over_z; swap.v_over_z=sb.v_over_z;
            sb.x=sc.x; sb.y=sc.y; sb.z=sc.z;
            sb.u=sc.u; sb.v=sc.v; sb.inv_z=sc.inv_z;
            sb.u_over_z=sc.u_over_z; sb.v_over_z=sc.v_over_z;
            sc.x=swap.x; sc.y=swap.y; sc.z=swap.z;
            sc.u=swap.u; sc.v=swap.v; sc.inv_z=swap.inv_z;
            sc.u_over_z=swap.u_over_z; sc.v_over_z=swap.v_over_z;
        }
        drawn += toy_renderer_triangle_textured(renderer, &sa, &sb, &sc,
                                                 active_texture_view, 1,
                                                 0xFF202020U);
    }
    return drawn;
}

static int draw_quad_tex(struct toy_renderer *renderer,
                         const struct camera *camera,
                         const struct world_uv_vertex *a,
                         const struct world_uv_vertex *b,
                         const struct world_uv_vertex *c,
                         const struct world_uv_vertex *d,
                         uint32_t color)
{
    if (!textures_enabled)
        return draw_world_triangle(renderer, camera, &a->p, &b->p, &c->p, color) +
               draw_world_triangle(renderer, camera, &a->p, &c->p, &d->p, color);
    return draw_world_triangle_tex(renderer, camera, a, b, c) +
           draw_world_triangle_tex(renderer, camera, a, c, d);
}

static int draw_position_quad_tex(struct toy_renderer *renderer,
                                  const struct camera *camera,
                                  const struct vec3 *a, const struct vec3 *b,
                                  const struct vec3 *c, const struct vec3 *d,
                                  int u1, int v1, uint32_t color)
{
    struct world_uv_vertex wa, wb, wc, wd;
    copy_vec3(&wa.p, a); copy_vec3(&wb.p, b);
    copy_vec3(&wc.p, c); copy_vec3(&wd.p, d);
    wa.u=0; wa.v=0; wb.u=u1; wb.v=0;
    wc.u=u1; wc.v=v1; wd.u=0; wd.v=v1;
    return draw_quad_tex(renderer, camera, &wa, &wb, &wc, &wd, color);
}

static int draw_quad(struct toy_renderer *renderer, const struct camera *camera,
                     const struct vec3 *a, const struct vec3 *b,
                     const struct vec3 *c, const struct vec3 *d,
                     uint32_t color)
{
    return draw_world_triangle(renderer, camera, a, b, c, color) +
           draw_world_triangle(renderer, camera, a, c, d, color);
}

static int draw_floor_zone(struct toy_renderer *renderer,
                           const struct camera *camera,
                           const struct toy_game_box *zone, uint32_t color)
{
    struct vec3 a, b, c, d;
    a.x = zone->minx; a.y = -894; a.z = zone->minz;
    b.x = zone->maxx; b.y = -894; b.z = zone->minz;
    c.x = zone->maxx; c.y = -894; c.z = zone->maxz;
    d.x = zone->minx; d.y = -894; d.z = zone->maxz;
    return draw_quad(renderer, camera, &a, &b, &c, &d, color);
}

static int draw_floor_border(struct toy_renderer *renderer,
                             const struct camera *camera,
                             const struct toy_game_box *zone, int width,
                             uint32_t color)
{
    struct toy_game_box edge;
    int pixels = 0;
    edge.minx = zone->minx; edge.maxx = zone->maxx;
    edge.minz = zone->minz; edge.maxz = zone->minz + width;
    pixels += draw_floor_zone(renderer, camera, &edge, color);
    edge.minz = zone->maxz - width; edge.maxz = zone->maxz;
    pixels += draw_floor_zone(renderer, camera, &edge, color);
    edge.minx = zone->minx; edge.maxx = zone->minx + width;
    edge.minz = zone->minz + width; edge.maxz = zone->maxz - width;
    pixels += draw_floor_zone(renderer, camera, &edge, color);
    edge.minx = zone->maxx - width; edge.maxx = zone->maxx;
    pixels += draw_floor_zone(renderer, camera, &edge, color);
    return pixels;
}

static void draw_world_label(struct toy_renderer *renderer,
                             const struct camera *camera,
                             const struct toy_game_box *zone,
                             const char *label, uint32_t color)
{
    struct vec3 world, view;
    struct toy_screen_vertex screen;
    int width = (int)strlen(label) * FB_FONT_W;
    world.x = (zone->minx + zone->maxx) / 2;
    world.y = -650;
    world.z = (zone->minz + zone->maxz) / 2;
    world_to_view(camera, &world, &view);
    if (view.z < NEAR_Z) return;
    project_vertex(&renderer->surface, &view, &screen);
    screen.x -= width / 2;
    if (screen.x < 0 || screen.x + width >= renderer->surface.width ||
        screen.y < 0 || screen.y + FB_FONT_H >= renderer->surface.height) return;
    fb_draw_string((unsigned char *)renderer->surface.pixels,
                   screen.x, screen.y, label, color, renderer->surface.stride);
}

static int render_block_enemy(struct toy_renderer *, const struct camera *,
                              const struct toy_game_enemy *, int, uint32_t);
static int render_round_enemy(struct toy_renderer *, const struct camera *,
                              const struct toy_game_enemy *, int, uint32_t);

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
    pixels += draw_quad(renderer,camera,&a,&b,&f,&e,box->color);
    pixels += draw_quad(renderer,camera,&b,&c,&g,&f,box->color + 0x080808);
    pixels += draw_quad(renderer,camera,&c,&d,&h,&g,box->color);
    pixels += draw_quad(renderer,camera,&d,&a,&e,&h,box->color + 0x080808);
    pixels += draw_quad(renderer,camera,&e,&f,&g,&h,box->color + 0x181818);
    return pixels;
}

/* 敌人模型使用任意底面高度的盒体，而场景障碍物仍走上面的落地盒体。 */
static int draw_cuboid(struct toy_renderer *renderer, const struct camera *camera,
                       int minx, int maxx, int miny, int maxy,
                       int minz, int maxz, uint32_t color)
{
    struct vec3 a, b, c, d, e, f, g, h;
    int pixels = 0;
    a.x = minx; a.y = miny; a.z = minz;
    b.x = maxx; b.y = miny; b.z = minz;
    c.x = maxx; c.y = miny; c.z = maxz;
    d.x = minx; d.y = miny; d.z = maxz;
    e.x = minx; e.y = maxy; e.z = minz;
    f.x = maxx; f.y = maxy; f.z = minz;
    g.x = maxx; g.y = maxy; g.z = maxz;
    h.x = minx; h.y = maxy; h.z = maxz;
    pixels += draw_quad(renderer, camera, &a, &b, &f, &e, color);
    pixels += draw_quad(renderer, camera, &b, &c, &g, &f, color + 0x080808);
    pixels += draw_quad(renderer, camera, &c, &d, &h, &g, color);
    pixels += draw_quad(renderer, camera, &d, &a, &e, &h, color + 0x080808);
    pixels += draw_quad(renderer, camera, &e, &f, &g, &h, color + 0x181818);
    return pixels;
}

static const int circle_x[8] = {1024, 724, 0, -724, -1024, -724, 0, 724};
static const int circle_z[8] = {0, 724, 1024, 724, 0, -724, -1024, -724};

static int draw_cylinder(struct toy_renderer *renderer,
                         const struct camera *camera, int x, int z,
                         int radius, int bottom, int top, uint32_t color)
{
    int pixels = 0;
    struct vec3 lo[8], hi[8], cap;
    cap.x = x; cap.y = top; cap.z = z;
    for (int i = 0; i < 8; i++) {
        lo[i].x = x + circle_x[i] * radius / 1024;
        lo[i].y = bottom;
        lo[i].z = z + circle_z[i] * radius / 1024;
        hi[i].x = lo[i].x; hi[i].y = top; hi[i].z = lo[i].z;
    }
    for (int i = 0; i < 8; i++) {
        int next = (i + 1) & 7;
        uint32_t shade = color + ((i & 3) * 0x030303);
        pixels += draw_quad(renderer, camera, &lo[i], &lo[next],
                            &hi[next], &hi[i], shade);
        pixels += draw_world_triangle(renderer, camera, &cap,
                                      &hi[i], &hi[next], color + 0x181818);
    }
    return pixels;
}

/* 三圈八边面组成的低多边形椭圆头。 */
static int draw_ellipsoid_head(struct toy_renderer *renderer,
                               const struct camera *camera, int x, int z,
                               int center_y, int rx, int ry, uint32_t color)
{
    static const int ring_r[5] = {0, 724, 1024, 724, 0};
    static const int ring_y[5] = {-1024, -724, 0, 724, 1024};
    struct vec3 ring[5][8];
    int pixels = 0;
    for (int r = 0; r < 5; r++) {
        for (int i = 0; i < 8; i++) {
            ring[r][i].x = x + circle_x[i] * rx * ring_r[r] / 1048576;
            ring[r][i].y = center_y + ring_y[r] * ry / 1024;
            ring[r][i].z = z + circle_z[i] * rx * ring_r[r] / 1048576;
        }
    }
    for (int r = 0; r < 4; r++) {
        for (int i = 0; i < 8; i++) {
            int next = (i + 1) & 7;
            pixels += draw_quad(renderer, camera, &ring[r][i], &ring[r][next],
                                &ring[r + 1][next], &ring[r + 1][i],
                                color + ((i & 3) * 0x030303));
        }
    }
    return pixels;
}

/* 沿敌人当前朝向在头部平面画小矩形，转身过程因此清晰可见。 */
static int draw_face_rect(struct toy_renderer *renderer,
                          const struct camera *camera, int x, int z, int radius,
                          int face_x, int face_z, int h0, int h1,
                          int y0, int y1, uint32_t color)
{
    struct vec3 a, b, c, d;
    int cx = x + face_x * (radius + 3) / 1024;
    int cz = z + face_z * (radius + 3) / 1024;
    a.x = cx - face_z * h0 / 1024; a.y = y0;
    a.z = cz + face_x * h0 / 1024;
    b.x = cx - face_z * h1 / 1024; b.y = y0;
    b.z = cz + face_x * h1 / 1024;
    c.x = b.x; c.y = y1; c.z = b.z;
    d.x = a.x; d.y = y1; d.z = a.z;
    return draw_quad(renderer, camera, &a, &b, &c, &d, color);
}

/* ── 可交互拾取物渲染：桌上武器与弹药盒（准星对准时整体提亮） ── */

static uint32_t highlight_tint(uint32_t color, int on)
{
    return on ? color + 0x383838 : color;
}

/* 平放的 SMG：机匣 + 枪管 + 弹匣 + 木托 + 照门，枪口朝 +z */
static int render_smg(struct toy_renderer *renderer, const struct camera *camera,
                      int x, int y, int z, int on)
{
    int pixels = 0;
    pixels += draw_cuboid(renderer, camera, x - 35, x + 35, y - 13, y + 13,
                          z - 60, z + 60, highlight_tint(0x3B4148, on));
    pixels += draw_cuboid(renderer, camera, x - 14, x + 14, y - 2, y + 10,
                          z + 60, z + 150, highlight_tint(0x2F343B, on));
    pixels += draw_cuboid(renderer, camera, x - 20, x + 20, y - 48, y - 16,
                          z - 20, z + 10, highlight_tint(0x4A4438, on));
    pixels += draw_cuboid(renderer, camera, x - 24, x + 24, y - 9, y + 7,
                          z - 118, z - 60, highlight_tint(0x6B4A30, on));
    pixels += draw_cuboid(renderer, camera, x - 3, x + 3, y + 13, y + 21,
                          z - 15, z + 15, highlight_tint(0x2A2E34, on));
    return pixels;
}

/* 平放的霰弹枪：机匣 + 长枪管 + 护木 + 木托，枪口朝 +z */
static int render_shotgun(struct toy_renderer *renderer, const struct camera *camera,
                          int x, int y, int z, int on)
{
    int pixels = 0;
    pixels += draw_cuboid(renderer, camera, x - 32, x + 32, y - 13, y + 13,
                          z - 45, z + 45, highlight_tint(0x46505A, on));
    pixels += draw_cuboid(renderer, camera, x - 11, x + 11, y - 2, y + 10,
                          z + 45, z + 150, highlight_tint(0x3A434D, on));
    pixels += draw_cuboid(renderer, camera, x - 17, x + 17, y - 19, y - 4,
                          z + 45, z + 100, highlight_tint(0x2C3138, on));
    pixels += draw_cuboid(renderer, camera, x - 24, x + 24, y - 8, y + 8,
                          z - 100, z - 45, highlight_tint(0x6B4A30, on));
    return pixels;
}

/* 弹药盒：橄榄绿箱体 + 浅色箱盖 */
static int render_ammo_box(struct toy_renderer *renderer, const struct camera *camera,
                           int x, int y, int z, int on)
{
    int pixels = 0;
    pixels += draw_cuboid(renderer, camera, x - 60, x + 60, y - 40, y + 40,
                          z - 80, z + 80, highlight_tint(0x555F3F, on));
    pixels += draw_cuboid(renderer, camera, x - 60, x + 60, y + 16, y + 40,
                          z - 80, z + 80, highlight_tint(0x6A7550, on));
    return pixels;
}

/* 距离 + 朝向锥判定高亮目标。拾取物（桌上武器/地面弹药盒）都在视平线
 * 以下，屏幕投影天然偏低，不能用中心像素窗口；改在世界空间按水平朝向
 * 放宽到约 40° 半角，垂直方向不限，取范围内最近者。 */
#define INTERACT_AIM_CONE 784   /* cos(≈38°)，1024 定点 */

static int compute_highlight(const struct camera *camera)
{
    int i, best = -1;
    long best_d2 = 0;
    for (i = 0; i < interactable_count; i++) {
        const struct interactable *it = &interactables[i];
        long dx, dz, d2, dist, dot;
        dx = it->x - camera->x;
        dz = it->z - camera->z;
        d2 = dx * dx + dz * dz;
        if (d2 > (long)INTERACT_RANGE * INTERACT_RANGE || d2 == 0) continue;
        dist = (long)isqrt(d2);
        if (dist <= 0) continue;
        dot = dx * camera->sy + dz * camera->cy;
        if (dot < dist * INTERACT_AIM_CONE) continue;
        if (best < 0 || d2 < best_d2) {
            best = i;
            best_d2 = d2;
        }
    }
    return best;
}

static int render_interactables(struct toy_renderer *renderer,
                                const struct camera *camera)
{
    int i, pixels = 0;
    for (i = 0; i < interactable_count; i++) {
        const struct interactable *it = &interactables[i];
        int on = i == highlighted;
        if (it->kind == TOY_MAP_PICKUP_SMG)
            pixels += render_smg(renderer, camera, it->x, it->y, it->z, on);
        else if (it->kind == TOY_MAP_PICKUP_SHOTGUN)
            pixels += render_shotgun(renderer, camera, it->x, it->y, it->z, on);
        else
            pixels += render_ammo_box(renderer, camera, it->x, it->y, it->z, on);
    }
    return pixels;
}

/* ── 第一人称武器模型：视图空间盒体固定在镜头右下方，开火后坐后移 ── */

#define VIEWMODEL_KICK_MAX 110

static int weapon_kick;   /* 开火瞬间置最大，随逻辑步衰减 */

static int viewmodel_weapon(void)
{
    int slot = game.current_slot;
    if (slot < 0 || slot >= TOY_GAME_WEAPON_SLOTS) return -1;
    return game.slots[slot].weapon;
}

/* 视图空间逆变换：枪口偏移 → 世界坐标（供弹道起点与枪口粒子） */
static void view_to_world(const struct camera *camera, const struct vec3 *view,
                          struct vec3 *world)
{
    int vx = view->x, vy = view->y, vz = view->z;
    int wy = (vy * camera->pitch_cy + vz * camera->pitch_sy) / 1024;
    int wz = (-vy * camera->pitch_sy + vz * camera->pitch_cy) / 1024;
    /* 调用方会原地逆变换，先缓存三个分量，避免写入 x 后破坏 z 计算。 */
    world->x = camera->x + (vx * camera->cy + wz * camera->sy) / 1024;
    world->z = camera->z + (-vx * camera->sy + wz * camera->cy) / 1024;
    world->y = wy;
}

/* 当前武器的枪口视图偏移（含后坐力位移：后移、下沉、略右偏） */
static void viewmodel_muzzle_offset(int weapon, int kick, struct vec3 *muzzle)
{
    if (weapon == TOY_GAME_WEAPON_SMG) {
        muzzle->x = 105; muzzle->y = -65; muzzle->z = 450;
    } else if (weapon == TOY_GAME_WEAPON_SHOTGUN) {
        muzzle->x = 100; muzzle->y = -65; muzzle->z = 560;
    } else {
        muzzle->x = 102; muzzle->y = -70; muzzle->z = 420;
    }
    muzzle->x += kick / 3;
    muzzle->y -= kick / 2;
    muzzle->z += kick;
}

/* 视图模型专用 2D 三角形填充：直接写表面、不参与深度测试 ——
 * 站在墙边/低头看地时枪永远不会被近处几何裁掉。 */
static int fill_triangle_2d(struct toy_surface *surface,
                            int x0, int y0, int x1, int y1, int x2, int y2,
                            uint32_t color)
{
    int y, tmp, xa, xb, ymin, ymax, drawn = 0;
    long dx01 = 0, dx02, dx12 = 0, xl, xr, lt;
    if (y0 > y1) { tmp = x0; x0 = x1; x1 = tmp; tmp = y0; y0 = y1; y1 = tmp; }
    if (y1 > y2) { tmp = x1; x1 = x2; x2 = tmp; tmp = y1; y1 = y2; y2 = tmp; }
    if (y0 > y1) { tmp = x0; x0 = x1; x1 = tmp; tmp = y0; y0 = y1; y1 = tmp; }
    if (y0 == y2 || y2 < 0 || y0 >= surface->height) return 0;
    if (y1 > y0) dx01 = (long)(x1 - x0) * 65536 / (y1 - y0);
    dx02 = (long)(x2 - x0) * 65536 / (y2 - y0);
    if (y2 > y1) dx12 = (long)(x2 - x1) * 65536 / (y2 - y1);
    ymin = y0 < 0 ? 0 : y0;
    ymax = y2 >= surface->height ? surface->height - 1 : y2;
    for (y = ymin; y <= ymax; y++) {
        if (y <= y1) {
            xl = (long)x0 * 65536 + dx01 * (y - y0);
            xr = (long)x0 * 65536 + dx02 * (y - y0);
        } else {
            xl = (long)x1 * 65536 + dx12 * (y - y1);
            xr = (long)x0 * 65536 + dx02 * (y - y0);
        }
        if (xl > xr) { lt = xl; xl = xr; xr = lt; }
        xa = (int)(xl >> 16);
        xb = (int)(xr >> 16);
        if (xa < 0) xa = 0;
        if (xb >= surface->width) xb = surface->width - 1;
        if (xa <= xb) {
            uint32_t *row = (uint32_t *)((unsigned char *)surface->pixels +
                                         y * surface->stride);
            for (int x = xa; x <= xb; x++) row[x] = color;
            drawn += xb - xa + 1;
        }
    }
    return drawn;
}

/* 视图空间四边形 → 两个 2D 三角形（枪体 z 恒 > 近平面，无需裁剪） */
static int draw_view_quad(struct toy_surface *surface,
                          const struct vec3 *p1, const struct vec3 *p2,
                          const struct vec3 *p3, const struct vec3 *p4,
                          uint32_t color)
{
    struct toy_screen_vertex s1, s2, s3, s4;
    project_vertex(surface, p1, &s1);
    project_vertex(surface, p2, &s2);
    project_vertex(surface, p3, &s3);
    project_vertex(surface, p4, &s4);
    if (s1.z < NEAR_Z || s2.z < NEAR_Z ||
        s3.z < NEAR_Z || s4.z < NEAR_Z) return 0;
    return fill_triangle_2d(surface, s1.x, s1.y, s2.x, s2.y, s3.x, s3.y,
                            color) +
           fill_triangle_2d(surface, s1.x, s1.y, s3.x, s3.y, s4.x, s4.y,
                            color);
}

/* 视图空间盒体：skip_back/skip_top 用于起始端埋进相邻盒内的部件，
 * 避免隐藏的内部面在屏幕上开出颜色补丁。 */
static int draw_view_box(struct toy_surface *surface,
                         int minx, int maxx, int miny, int maxy,
                         int minz, int maxz, uint32_t color,
                         int skip_back, int skip_top)
{
    struct vec3 a, b, c, d, e, f, g, h;
    int pixels = 0;
    a.x = minx; a.y = miny; a.z = minz;
    b.x = maxx; b.y = miny; b.z = minz;
    c.x = maxx; c.y = miny; c.z = maxz;
    d.x = minx; d.y = miny; d.z = maxz;
    e.x = minx; e.y = maxy; e.z = minz;
    f.x = maxx; f.y = maxy; f.z = minz;
    g.x = maxx; g.y = maxy; g.z = maxz;
    h.x = minx; h.y = maxy; h.z = maxz;
    /* 视图模型不走深度缓冲，盒体必须从远到近绘制。否则近端面会
     * 被侧面/顶面覆盖，部件交叠处就会出现错误的颜色层。 */
    pixels += draw_view_quad(surface, &d, &c, &g, &h, color);
    pixels += draw_view_quad(surface, &b, &c, &g, &f, color + 0x0A0A0A);
    pixels += draw_view_quad(surface, &d, &a, &e, &h, color - 0x0A0A0A);
    if (!skip_top)
        pixels += draw_view_quad(surface, &e, &f, &g, &h, color + 0x141414);
    if (!skip_back)
        pixels += draw_view_quad(surface, &a, &b, &f, &e, color - 0x141414);
    pixels += draw_view_quad(surface, &a, &b, &c, &d, color - 0x0C0C0C);
    return pixels;
}

/* 视图空间盒体 + 后坐力位移 */
static int draw_view_box_kick(struct toy_surface *surface,
                              int minx, int maxx, int miny, int maxy,
                              int minz, int maxz, uint32_t color,
                              int skip_back, int skip_top)
{
    int kx = weapon_kick / 3;
    int ky = -weapon_kick / 2;
    int kz = weapon_kick;
    return draw_view_box(surface, minx + kx, maxx + kx,
                         miny + ky, maxy + ky,
                         minz + kz, maxz + kz, color,
                         skip_back, skip_top);
}

/* 手枪：握把 → 套筒 → 枪管。后画者在前，部件大幅互嵌成一体。 */
static int render_view_pistol(struct toy_renderer *renderer)
{
    struct toy_surface *s = &renderer->surface;
    int pixels = 0;
    pixels += draw_view_box_kick(s, 90, 150, -180, -95, 255, 305,
                                 0x5A4630, 1, 1);
    pixels += draw_view_box_kick(s, 85, 160, -115, -45, 255, 340,
                                 0x3E4652, 0, 0);
    pixels += draw_view_box_kick(s, 75, 130, -90, -50, 300, 420,
                                 0x2E343D, 1, 0);
    return pixels;
}

/* SMG：弹匣 → 机匣 → 枪管 */
static int render_view_smg(struct toy_renderer *renderer)
{
    struct toy_surface *s = &renderer->surface;
    int pixels = 0;
    pixels += draw_view_box_kick(s, 95, 155, -170, -95, 275, 320,
                                 0x4A4438, 1, 1);
    pixels += draw_view_box_kick(s, 90, 170, -115, -40, 255, 345,
                                 0x3B4148, 0, 0);
    pixels += draw_view_box_kick(s, 70, 140, -85, -40, 310, 450,
                                 0x2F343B, 1, 0);
    return pixels;
}

/* 霰弹枪：机匣 → 长枪管 → 护木（护木包在枪管中段） */
static int render_view_shotgun(struct toy_renderer *renderer)
{
    struct toy_surface *s = &renderer->surface;
    int pixels = 0;
    pixels += draw_view_box_kick(s, 95, 175, -110, -40, 270, 355,
                                 0x46505A, 0, 0);
    pixels += draw_view_box_kick(s, 70, 130, -90, -40, 335, 560,
                                 0x3A434D, 1, 0);
    pixels += draw_view_box_kick(s, 75, 140, -100, -45, 390, 460,
                                 0x2C3138, 0, 0);
    return pixels;
}

/* 跟随当前武器出枪；空槽不显示 */
static int render_viewmodel(struct toy_renderer *renderer)
{
    int weapon = viewmodel_weapon();
    if (weapon == TOY_GAME_WEAPON_SMG) return render_view_smg(renderer);
    if (weapon == TOY_GAME_WEAPON_SHOTGUN) return render_view_shotgun(renderer);
    if (weapon == TOY_GAME_WEAPON_PISTOL) return render_view_pistol(renderer);
    return 0;
}

static void fill_rect(struct toy_surface *surface, int x, int y,
                      int width, int height, uint32_t color);
static uint32_t mix_color(uint32_t from, uint32_t to, int num, int den);
static void draw_sky_ground(struct toy_surface *surface,
                            const struct camera *camera);
static void draw_sky_features(struct toy_surface *surface,
                              const struct camera *camera);

/* E 互动提示固定在屏幕底部中央，高亮时可见；已持有的武器显示 REFILL */
static void draw_interact_prompt(struct toy_renderer *renderer)
{
    const struct interactable *it;
    char label[24];
    int text_w, x, y;
    if (highlighted < 0) return;
    it = &interactables[highlighted];
    if (it->kind == TOY_MAP_PICKUP_AMMO) {
        snprintf(label, sizeof(label), "E TAKE AMMO");
    } else {
        int weapon = it->kind == TOY_MAP_PICKUP_SMG ?
                     TOY_GAME_WEAPON_SMG : TOY_GAME_WEAPON_SHOTGUN;
        const char *name = it->kind == TOY_MAP_PICKUP_SMG ? "SMG" : "SHOTGUN";
        snprintf(label, sizeof(label), "E %s %s",
                 game.slots[0].weapon == weapon ? "REFILL" : "PICK UP", name);
    }
    text_w = (int)strlen(label) * FB_FONT_W;
    x = (renderer->surface.width - text_w) / 2;
    y = renderer->surface.height - FB_FONT_H - 18;
    fill_rect(&renderer->surface, x - 5, y - 3, text_w + 10, FB_FONT_H + 6,
              0x171B24);
    fb_draw_string((unsigned char *)renderer->surface.pixels, x, y,
                   label, 0xFFD060, renderer->surface.stride);
}

/* E 键互动：拾取主武器（替换槽 0 并切出），同武器 = 补充弹药；
 * 弹药盒补满备弹。拾取点是固定的，互动后保留在场。 */
static void interact_current(struct interactable *it)
{
    if (it->kind == TOY_MAP_PICKUP_AMMO) {
        toy_game_refill_ammo(&game);
        return;
    }
    toy_game_equip_weapon(&game, it->kind == TOY_MAP_PICKUP_SMG ?
                          TOY_GAME_WEAPON_SMG : TOY_GAME_WEAPON_SHOTGUN);
}

static int render_scene(struct toy_renderer *renderer, const struct camera *camera)
{
    int pixels = 0;
    struct vec3 a, b, c, d;
    /* 自由俯仰下先铺天空/地面：地平线由俯仰角决定，墙面与地板随后覆盖 */
    draw_sky_ground(&renderer->surface, camera);
    for (int z = level_map.minz; z < level_map.maxz; z += 1000) {
        for (int x = level_map.minx; x < level_map.maxx; x += 1000) {
            uint32_t color = (((x + z) / 1000) & 1) ? 0x30343A : 0x272B31;
            a.x = x;        a.y = -900; a.z = z;
            b.x = x + 1000; b.y = -900; b.z = z;
            c.x = x + 1000; c.y = -900; c.z = z + 1000;
            d.x = x;        d.y = -900; d.z = z + 1000;
            pixels += draw_quad(renderer, camera, &a, &b, &c, &d, color);
        }
    }
    active_texture_view = &wall_texture_view;
    for (int i=0; i<level_map.draw_count; i++) {
        struct toy_map_draw *x=&level_map.draw[i];
        if (x->type==TOY_MAP_DRAW_FLOOR) {
            struct toy_game_box zone={x->a,x->b,x->c,x->d}; pixels+=draw_floor_zone(renderer,camera,&zone,x->color);
        } else if (x->type==TOY_MAP_DRAW_BORDER) {
            struct toy_game_box zone={x->a,x->b,x->c,x->d}; pixels+=draw_floor_border(renderer,camera,&zone,x->e,x->color);
        } else if (x->type==TOY_MAP_DRAW_WALL) {
            if (x->c==x->d) { a.x=x->a;a.y=-900;a.z=x->c;b.x=x->b;b.y=-900;b.z=x->c;c.x=x->b;c.y=x->e;c.z=x->c;d.x=x->a;d.y=x->e;d.z=x->c; }
            else { a.x=x->a;a.y=-900;a.z=x->c;b.x=x->a;b.y=-900;b.z=x->d;c.x=x->a;c.y=x->e;c.z=x->d;d.x=x->a;d.y=x->e;d.z=x->c; }
            pixels+=draw_quad(renderer,camera,&a,&b,&c,&d,x->color);
        } else if (x->type==TOY_MAP_DRAW_MODEL) {
            if (x->style) {
                struct toy_game_enemy model;
                memset(&model,0,sizeof(model)); model.active=1;
                model.x=(x->a+x->b)/2; model.z=(x->c+x->d)/2; model.dir_z=-1024;
                if (x->style==1) pixels+=render_block_enemy(renderer,camera,&model,1000,x->color);
                else pixels+=render_round_enemy(renderer,camera,&model,1000,x->color);
            } else {
                struct box model={x->a,x->b,x->c,x->d,x->f,x->color};
                pixels+=draw_cuboid(renderer,camera,model.minx,model.maxx,x->e,x->f,model.minz,model.maxz,model.color);
            }
        } else if (x->type==TOY_MAP_DRAW_TEXTURE) {
            a.x=x->a;a.y=-900;a.z=x->c;b.x=x->b;b.y=-900;b.z=x->c;c.x=x->b;c.y=x->e;c.z=x->c;d.x=x->a;d.y=x->e;d.z=x->c;
            pixels+=draw_position_quad_tex(renderer,camera,&a,&b,&c,&d,x->texture_u*UV_ONE,x->texture_v*UV_ONE,x->color);
        } else if (x->type==TOY_MAP_DRAW_LABEL) {
            struct toy_game_box zone={x->a,x->b,x->c,x->d}; draw_world_label(renderer,camera,&zone,x->text,x->color);
        }
    }
    for (int i=0; i<level_map.box_count; i++) if (!level_map.boxes[i].air) {
        struct box obstacle={level_map.boxes[i].minx,level_map.boxes[i].maxx,level_map.boxes[i].minz,level_map.boxes[i].maxz,level_map.boxes[i].height,level_map.boxes[i].color};
        pixels+=draw_box(renderer,camera,&obstacle);
    }
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
    int old_psy = camera->pitch_sy;
    long long length;
    camera->sy = (old_sy * 1024 + camera->cy * turn) / 1024;
    camera->cy = (camera->cy * 1024 - old_sy * turn) / 1024;
    length = isqrt((long long)camera->sy * camera->sy +
                   (long long)camera->cy * camera->cy);
    if (length > 0) {
        camera->sy = (int)((long long)camera->sy * 1024 / length);
        camera->cy = (int)((long long)camera->cy * 1024 / length);
    }
    /* 俯仰先按角度增量旋转，再钳制到 ±75°，避免镜头翻过头。 */
    camera->pitch_sy = (old_psy * 1024 + camera->pitch_cy * pitch) / 1024;
    camera->pitch_cy = (camera->pitch_cy * 1024 - old_psy * pitch) / 1024;
    length = isqrt((long long)camera->pitch_sy * camera->pitch_sy +
                   (long long)camera->pitch_cy * camera->pitch_cy);
    if (length > 0) {
        camera->pitch_sy = (int)((long long)camera->pitch_sy * 1024 / length);
        camera->pitch_cy = (int)((long long)camera->pitch_cy * 1024 / length);
    }
    /* 不能只看 sin：一次较大的鼠标输入可能直接越过 90°，此时
     * sin 会重新变小但 cos 已为负，仍必须钳回边界。 */
    if (camera->pitch_cy < PITCH_LIMIT_CY) {
        if (camera->pitch_sy < 0) {
            camera->pitch_sy = -PITCH_LIMIT_SY;
        } else {
            camera->pitch_sy = PITCH_LIMIT_SY;
        }
        camera->pitch_cy = PITCH_LIMIT_CY;
    } else if (camera->pitch_sy > PITCH_LIMIT_SY) {
        camera->pitch_sy = PITCH_LIMIT_SY;
        camera->pitch_cy = PITCH_LIMIT_CY;
    } else if (camera->pitch_sy < -PITCH_LIMIT_SY) {
        camera->pitch_sy = -PITCH_LIMIT_SY;
        camera->pitch_cy = PITCH_LIMIT_CY;
    }
}

static int sensitivity_percent(int level)
{
    return 50 + clampi(level, 0, 15) * 10;
}

static void update_mouse(struct camera *camera, int relative_x, int relative_y,
                         const struct control_settings *settings)
{
    int percent = sensitivity_percent(settings->mouse_level);
    /* 水平/垂直同一倍率，避免方向手感不一致 */
    int turn = relative_x * 3 * percent / 100;
    int pitch = -relative_y * 3 * percent / 100;
    rotate_camera(camera, clampi(turn, -256, 256), pitch);
}

static void update_keyboard_look(struct camera *camera,
                                 const struct toy_input *input,
                                 const struct control_settings *settings)
{
    int turn = toy_input_down(input, KEY_RIGHT) -
               toy_input_down(input, KEY_LEFT);
    int pitch = toy_input_down(input, KEY_UP) -
                toy_input_down(input, KEY_DOWN);
    int percent = sensitivity_percent(settings->keyboard_level);
    /* 左右/上下同一倍率 */
    if (turn || pitch)
        rotate_camera(camera, turn * 16 * percent / 100,
                      pitch * 16 * percent / 100);
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

static void draw_pause_overlay(struct toy_surface *surface,
                               const struct pause_menu *menu,
                               const struct control_settings *settings)
{
    char line[64];
    int panel_w = surface->width * 3 / 5;
    int panel_h = surface->height * 2 / 3;
    int x = (surface->width - panel_w) / 2;
    int y = (surface->height - panel_h) / 2;
    int row_y = y + 58;
    fill_rect(surface, x - 3, y - 3, panel_w + 6, panel_h + 6, 0xD88A32);
    fill_rect(surface, x, y, panel_w, panel_h, 0x171B24);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * 6) / 2, y + 28,
                   "PAUSED", 0xE7E9EC, surface->stride);
    for (int item = 0; item < PAUSE_ITEM_COUNT; item++) {
        uint32_t color = item == menu->selected ? 0xFFD060 : 0xE7E9EC;
        if (item == menu->selected)
            fill_rect(surface, x + 30, row_y - 3, panel_w - 60,
                      FB_FONT_H + 6, 0x343B49);
        if (item == PAUSE_ITEM_RESUME)
            snprintf(line, sizeof(line), "%c RESUME", item == menu->selected ? '>' : ' ');
        else if (item == PAUSE_ITEM_MOUSE)
            snprintf(line, sizeof(line), "%c MOUSE SENS  < %d%% >",
                     item == menu->selected ? '>' : ' ',
                     sensitivity_percent(settings->mouse_level));
        else if (item == PAUSE_ITEM_KEYBOARD)
            snprintf(line, sizeof(line), "%c KEYBOARD SENS < %d%% >",
                     item == menu->selected ? '>' : ' ',
                     sensitivity_percent(settings->keyboard_level));
        else
            snprintf(line, sizeof(line), "%c QUIT", item == menu->selected ? '>' : ' ');
        fb_draw_string((unsigned char *)surface->pixels, x + 42, row_y,
                       line, color, surface->stride);
        row_y += 30;
    }
    fb_draw_string((unsigned char *)surface->pixels, x + 42, y + panel_h - 62,
                   "UP DOWN SELECT  LEFT RIGHT CHANGE", 0xAEB6C2,
                   surface->stride);
    fb_draw_string((unsigned char *)surface->pixels, x + 42, y + panel_h - 38,
                   "ENTER CONFIRM  ESC RESUME", 0xD88A32, surface->stride);
}

static int enemy_y(int y, int scale)
{
    return -900 + (y + 900) * scale / 1000;
}

/* 偶数槽位：方块人。分离的靴子、腿、躯干和头保持 Minecraft 式轮廓。 */
static int render_block_enemy(struct toy_renderer *renderer,
                              const struct camera *camera,
                              const struct toy_game_enemy *e,
                              int scale, uint32_t color)
{
    int x = e->x, z = e->z, pixels = 0;
    pixels += draw_cuboid(renderer, camera, x - 105, x - 15,
                          enemy_y(-900, scale), enemy_y(-760, scale),
                          z - 105, z + 105, 0x252A30);
    pixels += draw_cuboid(renderer, camera, x + 15, x + 105,
                          enemy_y(-900, scale), enemy_y(-760, scale),
                          z - 105, z + 105, 0x252A30);
    pixels += draw_cuboid(renderer, camera, x - 100, x - 10,
                          enemy_y(-760, scale), enemy_y(-450, scale),
                          z - 85, z + 85, color - 0x101008);
    pixels += draw_cuboid(renderer, camera, x + 10, x + 100,
                          enemy_y(-760, scale), enemy_y(-450, scale),
                          z - 85, z + 85, color - 0x101008);
    pixels += draw_cuboid(renderer, camera, x - 175, x + 175,
                          enemy_y(-470, scale), enemy_y(20, scale),
                          z - 105, z + 105, color);
    pixels += draw_cuboid(renderer, camera, x - 155, x + 155,
                          enemy_y(0, scale), enemy_y(320, scale),
                          z - 155, z + 155, color + 0x202010);
    /* 像素化愤怒脸：下压的双眉、亮眼和紧闭嘴。 */
    pixels += draw_face_rect(renderer, camera, x, z, 155, e->dir_x, e->dir_z,
                             -105, -18, enemy_y(210, scale),
                             enemy_y(235, scale), 0x4A1010);
    pixels += draw_face_rect(renderer, camera, x, z, 155, e->dir_x, e->dir_z,
                             18, 105, enemy_y(210, scale),
                             enemy_y(235, scale), 0x4A1010);
    pixels += draw_face_rect(renderer, camera, x, z, 155, e->dir_x, e->dir_z,
                             -82, -28, enemy_y(160, scale),
                             enemy_y(200, scale), 0xFFF0A0);
    pixels += draw_face_rect(renderer, camera, x, z, 155, e->dir_x, e->dir_z,
                             28, 82, enemy_y(160, scale),
                             enemy_y(200, scale), 0xFFF0A0);
    pixels += draw_face_rect(renderer, camera, x, z, 155, e->dir_x, e->dir_z,
                             -70, 70, enemy_y(80, scale),
                             enemy_y(105, scale), 0x4A1010);
    return pixels;
}

/* 奇数槽位：Madness 风格无臂人，圆柱躯干、椭圆头和厚底短靴。 */
static int render_round_enemy(struct toy_renderer *renderer,
                              const struct camera *camera,
                              const struct toy_game_enemy *e,
                              int scale, uint32_t color)
{
    int x = e->x, z = e->z, pixels = 0;
    pixels += draw_cuboid(renderer, camera, x - 145, x - 12,
                          enemy_y(-900, scale), enemy_y(-760, scale),
                          z - 180, z + 95, 0x202328);
    pixels += draw_cuboid(renderer, camera, x + 12, x + 145,
                          enemy_y(-900, scale), enemy_y(-760, scale),
                          z - 180, z + 95, 0x202328);
    pixels += draw_cylinder(renderer, camera, x, z, 180,
                            enemy_y(-770, scale), enemy_y(20, scale), color);
    pixels += draw_ellipsoid_head(renderer, camera, x, z,
                                  enemy_y(155, scale), 205,
                                  (enemy_y(350, scale) - enemy_y(-40, scale)) / 2,
                                  color + 0x181810);
    /* 普通敌人面部只有一个醒目的十字。 */
    pixels += draw_face_rect(renderer, camera, x, z, 205, e->dir_x, e->dir_z,
                             -25, 25, enemy_y(35, scale),
                             enemy_y(275, scale), 0x251F20);
    pixels += draw_face_rect(renderer, camera, x, z, 205, e->dir_x, e->dir_z,
                             -120, 120, enemy_y(135, scale),
                             enemy_y(175, scale), 0x251F20);
    return pixels;
}

static void render_enemy_alert(struct toy_renderer *renderer,
                               const struct camera *camera,
                               const struct toy_game_enemy *e, int scale)
{
    struct vec3 world, view;
    struct toy_screen_vertex screen;
    int x, y;
    if (e->ai_state != TOY_GAME_ENEMY_ALERT) return;
    world.x = e->x;
    world.y = enemy_y(500, scale);
    world.z = e->z;
    world_to_view(camera, &world, &view);
    if (view.z < NEAR_Z) return;
    project_vertex(&renderer->surface, &view, &screen);
    x = screen.x - 5;
    y = screen.y;
    if (x < 1 || x + 11 >= renderer->surface.width ||
        y < 1 || y + 27 >= renderer->surface.height)
        return;
    fill_rect(&renderer->surface, x - 1, y - 1, 11, 18, 0x301010);
    fill_rect(&renderer->surface, x + 1, y + 1, 7, 14, 0xFF3030);
    fill_rect(&renderer->surface, x - 1, y + 19, 11, 8, 0x301010);
    fill_rect(&renderer->surface, x + 1, y + 21, 7, 4, 0xFF3030);
}

/* 两种低多边形敌人；受击闪红/命中闪白，倒地时整体纵向压扁。 */
static int render_enemies(struct toy_renderer *renderer,
                          const struct camera *camera)
{
    int pixels = 0;
    for (int i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        const struct toy_game_enemy *e = &game.enemies[i];
        struct vec3 center, view;
        uint32_t color;
        int scale = 1000;
        if (e->active == 0) continue;
        center.x = e->x;
        center.y = 0;
        center.z = e->z;
        world_to_view(camera, &center, &view);
        if (view.z > 8000) continue;
        if (e->active == 2) {
            scale = e->dying_ms * 1000 / TOY_GAME_DYING_MS;
            color = 0x5A1A1A;
        } else {
            if (e->hurt > 0) color = 0xBB3333;
            else if (e->flash > 0) color = 0xDFDFDF;
            else color = 0x4A5D3A;
        }
        if ((i & 1) == 0)
            pixels += render_block_enemy(renderer, camera, e, scale, color);
        else
            pixels += render_round_enemy(renderer, camera, e, scale, color);
        render_enemy_alert(renderer, camera, e, scale);
    }
    return pixels;
}

static const char *campaign_phase_name(int phase)
{
    if (phase == TOY_GAME_PHASE_BUILDUP) return "BUILDUP";
    if (phase == TOY_GAME_PHASE_HORDE) return "HORDE";
    if (phase == TOY_GAME_PHASE_RELAX) return "RELAX";
    return "CALM";
}

static int draw_hud_value(struct toy_surface *surface, int x,
                          const char *label, const char *value, uint32_t color)
{
    int label_w = (int)strlen(label) * FB_FONT_W;
    int value_w = (int)strlen(value) * FB_FONT_W;
    fb_draw_string((unsigned char *)surface->pixels, x, 8,
                   label, 0xAAB4C0, surface->stride);
    x += label_w;
    fill_rect(surface, x - 1, 6, value_w + 2, FB_FONT_H + 4, 0x26384C);
    fb_draw_string((unsigned char *)surface->pixels, x, 8,
                   value, color, surface->stride);
    return x + value_w + FB_FONT_W * 2;
}

static const char *weapon_names[TOY_GAME_WEAPON_COUNT] = {
    "PISTOL", "SMG", "SHOTGUN"
};

/* 武器栏：1[主武器] 弹匣/备弹  2[手枪] 弹匣/备弹，当前槽位高亮 */
static void render_weapon_hud(struct toy_surface *surface, int x, int y)
{
    char line[48];
    int i;
    for (i = 0; i < TOY_GAME_WEAPON_SLOTS; i++) {
        const struct toy_game_slot *s = &game.slots[i];
        uint32_t color = i == game.current_slot ? 0xFFD060 : 0x9AA6B4;
        if (s->weapon < 0)
            snprintf(line, sizeof(line), "%d[-] ", i + 1);
        else if (s->reserve == TOY_GAME_AMMO_INFINITE)
            snprintf(line, sizeof(line), "%d[%s] %d/INF ", i + 1,
                     weapon_names[s->weapon], s->mag);
        else
            snprintf(line, sizeof(line), "%d[%s] %d/%d ", i + 1,
                     weapon_names[s->weapon], s->mag, s->reserve);
        fb_draw_string((unsigned char *)surface->pixels, x, y, line, color,
                       surface->stride);
        x += (int)strlen(line) * FB_FONT_W;
    }
}

static void render_hud(struct toy_surface *surface, int fps)
{
    char line[96];
    int n, x = 8, hint_y;
    uint32_t phase_color = game.campaign_phase == TOY_GAME_PHASE_HORDE ?
                           0xFFD040 : 0x80E0C0;
    x = draw_hud_value(surface, x, "DIR ",
                       campaign_phase_name(game.campaign_phase), phase_color);
    snprintf(line, sizeof(line), "%d", game.spawn_budget);
    x = draw_hud_value(surface, x, "BUD ", line, 0xFFD070);
    snprintf(line, sizeof(line), "%d", game.enemies_alive);
    x = draw_hud_value(surface, x, "LIVE ", line, 0xF0F0F0);
    snprintf(line, sizeof(line), "%d", game.active_attackers);
    x = draw_hud_value(surface, x, "ACT ", line,
                       game.active_attackers > 0 ? 0xFF8060 : 0x80E080);
    snprintf(line, sizeof(line), "%dS",
             game.phase_timer_ms > 0 ? (game.phase_timer_ms + 999) / 1000 : 0);
    x = draw_hud_value(surface, x, "NEXT ", line, 0x80C8FF);
    snprintf(line, sizeof(line), "%d", game.director_encounters);
    x = draw_hud_value(surface, x, "RUN ", line, 0xC0A0FF);
    snprintf(line, sizeof(line), "%d", fps);
    draw_hud_value(surface, x, "FPS ", line, 0x90F090);
    n = snprintf(line, sizeof(line), "HP %d  KILLS %d", game.hp, game.kills);
    if (n > 0)
        fb_draw_string((unsigned char *)surface->pixels, 8, 8 + FB_FONT_H,
                       line, 0xE7E9EC, surface->stride);
    render_weapon_hud(surface, 8, 8 + FB_FONT_H * 2);
    hint_y = 8 + FB_FONT_H * (game.reloading ? 4 : 3);
    if (game.reloading)
        fb_draw_string((unsigned char *)surface->pixels, 8, 8 + FB_FONT_H * 3,
                       "RELOADING...", 0xD88A32, surface->stride);
    if (level_map.safe_count > 1 && toy_game_point_in_box(game.px, game.pz, &map_safe_rooms[1])) {
        snprintf(line, sizeof(line), "EXIT SECURE %d%%",
                 game.goal_hold_ms * 100 / TOY_GAME_GOAL_HOLD_MS);
        fb_draw_string((unsigned char *)surface->pixels, 8, hint_y,
                       line, 0x80E080, surface->stride);
    } else if (level_map.safe_count > 0 && toy_game_point_in_box(game.px, game.pz, &map_safe_rooms[0])) {
        fb_draw_string((unsigned char *)surface->pixels, 8, hint_y,
                       "START SAFE ROOM - REACH GREEN EXIT", 0x80E080,
                       surface->stride);
    } else if (game.alarm_timer_ms > 0) {
        snprintf(line, sizeof(line), "ALARM HORDE %d SEC",
                 (game.alarm_timer_ms + 999) / 1000);
        fb_draw_string((unsigned char *)surface->pixels, 8, hint_y,
                       line, 0xFFD040, surface->stride);
    } else if (game.campaign_phase == TOY_GAME_PHASE_RELAX) {
        snprintf(line, sizeof(line), "HORDE CLEARED - RELAX %d SEC",
                 (game.phase_timer_ms + 999) / 1000);
        fb_draw_string((unsigned char *)surface->pixels, 8, hint_y,
                       line, 0x80E080, surface->stride);
    } else if (!game.alarm_triggered) {
        fb_draw_string((unsigned char *)surface->pixels, 8, hint_y,
                       "YELLOW BORDER = HORDE TRIGGER", 0xFFD040,
                       surface->stride);
    } else {
        fb_draw_string((unsigned char *)surface->pixels, 8, hint_y,
                       "RED FLOOR = SPAWN ZONE", 0xE08080, surface->stride);
    }
}

/* --dump-frame：把最后一帧写为 PPM（XRGB8888 → RGB，调试渲染用） */
static void dump_frame_ppm(const char *path, const struct toy_surface *surface)
{
    char header[48];
    unsigned char *rgb;
    int hlen, fd, i;
    if (!surface || !surface->pixels || surface->width <= 0 ||
        surface->height <= 0) return;
    hlen = snprintf(header, sizeof(header), "P6\n%d %d\n255\n",
                    surface->width, surface->height);
    rgb = tlibc_malloc((size_t)surface->width * surface->height * 3);
    if (!rgb) return;
    for (i = 0; i < surface->width * surface->height; i++) {
        uint32_t p = ((uint32_t *)surface->pixels)[i];
        rgb[i * 3 + 0] = (unsigned char)(p & 0xFF);
        rgb[i * 3 + 1] = (unsigned char)((p >> 8) & 0xFF);
        rgb[i * 3 + 2] = (unsigned char)((p >> 16) & 0xFF);
    }
    fd = __creat(path, 0644);
    if (fd >= 0) {
        __write(fd, header, hlen);
        __write(fd, rgb, surface->width * surface->height * 3);
        __close(fd);
        __printf("wayland_fps: dumped frame to %s\n", path);
    }
    tlibc_free(rgb);
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

/* ── 子弹轨迹与命中粒子（纯视觉；逻辑步进 16ms 推进） ──────────── */

#define TRACER_SLOTS 32
#define TRACER_LIFE_MS 160
#define TRACER_Y (-350)         /* 弹道高度：躯干/头部之间，与命中判定同平面 */

struct tracer {
    int active;
    int sx, sy, sz;     /* 起点：枪口世界坐标（开火瞬间采样） */
    int ex, ey, ez;     /* 视觉终点：沿准心方向；命中点另由火花表示 */
    int life_ms;
};

#define PARTICLE_SLOTS 96
#define PARTICLE_LIFE_MS 240
#define PARTICLE_GRAVITY 4      /* 每 16ms 步重力加速度（世界单位） */

struct particle {
    int active;
    int x, y, z;
    int vx, vy, vz;
    int life_ms;
};

static struct tracer tracers[TRACER_SLOTS];
static int tracer_next;
static struct particle particles[PARTICLE_SLOTS];
static int particle_next;
static unsigned int last_fire_seq;

/* 纯视觉随机源（xorshift32），不占用游戏 PRNG，无需与逻辑同步 */
static uint32_t effect_rng = 0x243F6A88;

static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int effect_rand(int lo, int hi)
{
    uint32_t raw = xorshift32(&effect_rng);
    int span = hi - lo + 1;
    return lo + (int)(raw % (uint32_t)span);
}

/* num/den 线性插值两色（num=den 时取 from，0 时取 to） */
static uint32_t mix_color(uint32_t from, uint32_t to, int num, int den)
{
    unsigned long fr = (from >> 16) & 0xFF, fg = (from >> 8) & 0xFF, fb = from & 0xFF;
    unsigned long tr = (to >> 16) & 0xFF, tg = (to >> 8) & 0xFF, tb = to & 0xFF;
    return (uint32_t)(((fr * num + tr * (den - num)) / den) << 16 |
                      ((fg * num + tg * (den - num)) / den) << 8 |
                      ((fb * num + tb * (den - num)) / den));
}

/* 视平线以上画渐变天空、以下铺地面色。地平线屏幕位置
 * y = h/2 + focal·tan(pitch)，tan(pitch) = pitch_sy/pitch_cy；
 * 俯仰接近 ±90° 时地平线越出屏幕，整屏填天空或地面。 */
static void draw_sky_ground(struct toy_surface *surface,
                            const struct camera *camera)
{
    int focal = surface->width * 3 / 4;
    int horizon = surface->height / 2;
    int pitch_cy = camera->pitch_cy;
    int sky_bottom, ground_top;
    if (pitch_cy < 0) pitch_cy = -pitch_cy;
    if (pitch_cy >= 64) {
        long long offset = (long long)focal * camera->pitch_sy / camera->pitch_cy;
        if (offset > 2LL * surface->height) offset = 2LL * surface->height;
        if (offset < -2LL * surface->height) offset = -2LL * surface->height;
        horizon += (int)offset;
    } else if (camera->pitch_sy > 0) {
        horizon = -surface->height;      /* 看向正上方：全屏天空 */
    } else {
        horizon = surface->height * 2;   /* 看向正下方：全屏地面 */
    }
    sky_bottom = horizon < surface->height ? horizon : surface->height;
    ground_top = horizon > 0 ? horizon : 0;
    if (sky_bottom > 0) {
        int band_h = sky_bottom / 8 + 1;
        int y;
        for (y = 0; y < sky_bottom; y += band_h) {
            int band = y / band_h;
            uint32_t color = mix_color(0x3B82C4, 0xB9E3FF,
                                       8 - 1 - band, 8);
            fill_rect(surface, 0, y, surface->width, band_h, color);
        }
    }
    if (ground_top < surface->height)
        fill_rect(surface, 0, ground_top, surface->width,
                  surface->height - ground_top, 0x0F1218);
    /* 固定在天空盒方向上的像素立方体白云。 */
    draw_sky_features(surface, camera);
}

/* 把世界方向（1024 定点单位向量）投影到屏幕；在身后返回 0 */
static int project_sky_dir(const struct camera *camera, const struct toy_surface *surface,
                           int dx, int dy, int dz, int *sx, int *sy)
{
    int vx = (dx * camera->cy - dz * camera->sy) / 1024;
    int vz0 = (dx * camera->sy + dz * camera->cy) / 1024;
    int vy2 = (dy * camera->pitch_cy - vz0 * camera->pitch_sy) / 1024;
    int vz2 = (dy * camera->pitch_sy + vz0 * camera->pitch_cy) / 1024;
    int focal = surface->width * 3 / 4;
    if (vz2 <= 64) return 0;
    *sx = surface->width / 2 + vx * focal / vz2;
    *sy = surface->height / 2 - vy2 * focal / vz2;
    return 1;
}

static void draw_sky_cloud(struct toy_surface *surface, int x, int y, int scale)
{
    int unit = scale / 4;
    if (unit < 2) unit = 2;
    /* 方块轮廓、顶部高光和底部阴影，保持 Minecraft 式立方体云朵。 */
    fill_rect(surface, x - unit * 6, y, unit * 12, unit * 3, 0xEAF7FF);
    fill_rect(surface, x - unit * 3, y - unit * 2, unit * 6, unit * 2,
              0xFFFFFF);
    fill_rect(surface, x - unit * 7, y + unit * 2, unit * 14, unit,
              0xB9D9EC);
    fill_rect(surface, x - unit * 5, y - unit, unit * 2, unit,
              0xFFFFFF);
    fill_rect(surface, x + unit * 3, y - unit, unit * 2, unit,
              0xFFFFFF);
}

/* 三朵云的方向固定在天空盒中，转动镜头时只改变观察位置，不随时间漂移。 */
static void draw_sky_features(struct toy_surface *surface,
                              const struct camera *camera)
{
    static const int cloud_dir[3][3] = {
        {340, 248, -934},
        {-872, 172, -512},
        {-488, 380, -816},
    };
    static const int cloud_scale[3] = {16, 12, 10};
    int cx, cy, i;
    for (i = 0; i < 3; i++) {
        if (!project_sky_dir(camera, surface, cloud_dir[i][0],
                             cloud_dir[i][1], cloud_dir[i][2], &cx, &cy))
            continue;
        draw_sky_cloud(surface, cx, cy, cloud_scale[i]);
    }
}

/* 命中统一火花：弹着点 7 颗，沿弹道方向喷射，随后受重力回落 */
static void spawn_hit_particles(int x, int y, int z, int sy, int cy)
{
    int i;
    for (i = 0; i < 7; i++) {
        struct particle *p = &particles[particle_next];
        particle_next = (particle_next + 1) % PARTICLE_SLOTS;
        p->active = 1;
        p->x = x;
        p->y = y + effect_rand(-10, 10);
        p->z = z;
        p->vx = sy * 22 / 1024 + effect_rand(-24, 24);
        p->vy = effect_rand(8, 30);
        p->vz = cy * 22 / 1024 + effect_rand(-24, 24);
        p->life_ms = PARTICLE_LIFE_MS + effect_rand(-40, 40);
    }
}

static void update_effects(int dt_ms)
{
    int i;
    weapon_kick -= dt_ms * 2;
    if (weapon_kick < 0) weapon_kick = 0;
    for (i = 0; i < TRACER_SLOTS; i++) {
        struct tracer *t = &tracers[i];
        if (!t->active) continue;
        t->life_ms -= dt_ms;
        if (t->life_ms <= 0) t->active = 0;
    }
    for (i = 0; i < PARTICLE_SLOTS; i++) {
        struct particle *p = &particles[i];
        if (!p->active) continue;
        p->x += p->vx;
        p->y += p->vy;
        p->z += p->vz;
        p->vy -= PARTICLE_GRAVITY;
        p->life_ms -= dt_ms;
        if (p->life_ms <= 0 || p->y < -880) p->active = 0;
    }
}

/* 把游戏层的水平射线转成从枪口指向屏幕准心的 3D 视觉终点。
 * 游戏命中仍使用水平平面，而 tracer 必须补偿枪口在右下方造成的视差，
 * 否则它会从枪口斜着飞向准心旁边。 */
static void tracer_aim_endpoint(const struct camera *camera,
                                const struct toy_game_ray *ray,
                                int ex, int ez, int *out_x, int *out_y,
                                int *out_z)
{
    struct vec3 view_end;
    int dx = ex - game.px;
    int dz = ez - game.pz;
    int distance = isqrt((long long)dx * dx + (long long)dz * dz);
    int view_x, view_y, view_z;
    if (distance < 1) distance = 1;
    view_x = (dx * camera->cy - dz * camera->sy) / 1024;
    view_z = (dx * camera->sy + dz * camera->cy) / 1024;
    if (view_z < 1) view_z = 1;
    view_y = ray->vy * distance / 1024;
    /* 保留游戏层的实际 x/z 命中点，只补上同一颗弹丸的垂直扩散，
     * 射线和命中粒子因此使用完全一致的落点。 */
    view_end.x = view_x;
    view_end.y = view_y;
    view_end.z = view_z;
    view_to_world(camera, &view_end, &view_end);
    *out_x = view_end.x;
    *out_y = view_end.y;
    *out_z = view_end.z;
}

/* 每次实际开火后同步：把 game 里最新一枪的射线搬进 tracer 环，
 * 起点统一取枪口世界坐标；命中（敌人或墙体）的弹丸在弹着点生成
 * 火花，枪口火花则由独立的粒子与闪光绘制。 */
static void sync_fire_effects(const struct camera *camera)
{
    int i, ray_count;
    struct vec3 muzzle;
    struct vec3 muzzle_view;
    if (game.fire_seq < last_fire_seq) {
        /* 新局（R 重开/死亡重开）：丢弃旧弹道与粒子 */
        memset(tracers, 0, sizeof(tracers));
        memset(particles, 0, sizeof(particles));
        tracer_next = 0;
        particle_next = 0;
        last_fire_seq = 0;
        weapon_kick = 0;
        return;
    }
    if (game.fire_seq == last_fire_seq) return;
    last_fire_seq = game.fire_seq;
    ray_count = game.ray_count;
    if (ray_count < 0) ray_count = 0;
    if (ray_count > TOY_GAME_MAX_RAYS) ray_count = TOY_GAME_MAX_RAYS;
    /* 后坐力 + 枪口世界坐标（开火瞬间采样，含后坐位移） */
    weapon_kick = VIEWMODEL_KICK_MAX;
    viewmodel_muzzle_offset(viewmodel_weapon(), weapon_kick, &muzzle_view);
    muzzle.x = muzzle_view.x;
    muzzle.y = muzzle_view.y;
    muzzle.z = muzzle_view.z;
    view_to_world(camera, &muzzle, &muzzle);
    for (i = 0; i < ray_count; i++) {
        const struct toy_game_ray *r = &game.rays[i];
        struct tracer *t = &tracers[tracer_next];
        tracer_next = (tracer_next + 1) % TRACER_SLOTS;
        t->active = 1;
        t->sx = muzzle.x;
        t->sy = muzzle.y;
        t->sz = muzzle.z;
        tracer_aim_endpoint(camera, r, r->ex, r->ez,
                            &t->ex, &t->ey, &t->ez);
        t->life_ms = TRACER_LIFE_MS;
        if (r->hit_enemy || r->hit_world)
            spawn_hit_particles(r->ex, t->ey, r->ez, r->sy, r->cy);
    }
}

/* 一个带最终边界检查的 Bresenham。投影裁剪是为了避免远处端点导致
 * 巨量迭代，像素检查则是最后一道防线：fb_draw_line 不检查坐标。 */
static void draw_effect_line(struct toy_surface *surface,
                             int x0, int y0, int x1, int y1,
                             uint32_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < surface->width &&
            y0 >= 0 && y0 < surface->height)
            put_pixel(surface, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        {
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}

/* 弹道投影为屏幕线段：起点（枪口）与终点（命中点）都从世界空间投影，
 * 先裁剪到近平面，再 Liang-Barsky 裁剪到屏幕。 */
static void draw_tracer_line(struct toy_surface *surface, const struct camera *camera,
                             int sx, int sy, int sz, int ex, int ey, int ez,
                             uint32_t color)
{
    struct vec3 a, b, clipped;
    struct toy_screen_vertex pa, pb;
    int x0, y0, x1, y1, t0, t1, t_in, t_out, tmp;
    a.x = sx; a.y = sy; a.z = sz;
    b.x = ex; b.y = ey; b.z = ez;
    world_to_view(camera, &a, &a);
    world_to_view(camera, &b, &b);
    if (a.z < NEAR_Z && b.z < NEAR_Z) return;
    if (a.z < NEAR_Z) {
        near_intersection(&a, &b, &clipped);
        a.x = clipped.x;
        a.y = clipped.y;
        a.z = clipped.z;
    } else if (b.z < NEAR_Z) {
        near_intersection(&b, &a, &clipped);
        b.x = clipped.x;
        b.y = clipped.y;
        b.z = clipped.z;
    }
    project_vertex(surface, &a, &pa);
    project_vertex(surface, &b, &pb);
    x0 = pa.x; y0 = pa.y; x1 = pb.x; y1 = pb.y;
    /* 16.16 定点 Liang-Barsky；投影坐标有界但可能远超屏幕。
     * 裁剪到 [0, w-2]×[0, h-2]：第二条偏移线 (+1,+1) 也必须在界内。 */
    t0 = 0;
    t1 = 1 << 16;
    if (x0 != x1) {
        t_in = (int)((long long)(0 - x0) * 65536 / (x1 - x0));
        t_out = (int)((long long)(surface->width - 2 - x0) * 65536 / (x1 - x0));
        if (t_in > t_out) { tmp = t_in; t_in = t_out; t_out = tmp; }
        if (t_in > t0) t0 = t_in;
        if (t_out < t1) t1 = t_out;
        if (t0 > t1) return;
    } else if (x0 < 0 || x0 >= surface->width - 1) {
        return;   /* 竖直段整体在屏幕外 */
    }
    if (y0 != y1) {
        t_in = (int)((long long)(0 - y0) * 65536 / (y1 - y0));
        t_out = (int)((long long)(surface->height - 2 - y0) * 65536 / (y1 - y0));
        if (t_in > t_out) { tmp = t_in; t_in = t_out; t_out = tmp; }
        if (t_in > t0) t0 = t_in;
        if (t_out < t1) t1 = t_out;
        if (t0 > t1) return;
    } else if (y0 < 0 || y0 >= surface->height - 1) {
        return;   /* 水平段整体在屏幕外 */
    }
    if (t0 < 0) t0 = 0;
    if (t1 > (1 << 16)) t1 = 1 << 16;
    if (t0 > t1) return;
    if (t0 != 0 || t1 != (1 << 16)) {
        int nx0 = x0 + (int)((long long)(x1 - x0) * t0 / 65536);
        int ny0 = y0 + (int)((long long)(y1 - y0) * t0 / 65536);
        x1 = nx0 + (int)((long long)(x1 - x0) * (t1 - t0) / 65536);
        y1 = ny0 + (int)((long long)(y1 - y0) * (t1 - t0) / 65536);
        x0 = nx0;
        y0 = ny0;
    }
    /* 双线 = 2px 粗的射线；偏移像素同色，无需额外混合 */
    draw_effect_line(surface, x0, y0, x1, y1, color);
    draw_effect_line(surface, x0 + 1, y0 + 1, x1 + 1, y1 + 1, color);
}

static int render_tracers(struct toy_renderer *renderer, const struct camera *camera)
{
    int i, pixels = 0;
    for (i = 0; i < TRACER_SLOTS; i++) {
        const struct tracer *t = &tracers[i];
        uint32_t color;
        if (!t->active) continue;
        color = mix_color(0xFFE060, 0x3A2C14,
                          t->life_ms * 256 / TRACER_LIFE_MS, 256);
        draw_tracer_line(&renderer->surface, camera, t->sx, t->sy, t->sz,
                         t->ex, t->ey, t->ez, color);
        pixels++;
    }
    return pixels;
}

static int render_particles(struct toy_renderer *renderer, const struct camera *camera)
{
    int i, pixels = 0;
    for (i = 0; i < PARTICLE_SLOTS; i++) {
        const struct particle *p = &particles[i];
        struct vec3 world, view;
        struct toy_screen_vertex screen;
        int k;
        if (!p->active) continue;
        world.x = p->x;
        world.y = p->y;
        world.z = p->z;
        world_to_view(camera, &world, &view);
        if (view.z < NEAR_Z) continue;
        project_vertex(&renderer->surface, &view, &screen);
        if (screen.x < 0 || screen.x + 2 >= renderer->surface.width ||
            screen.y < 0 || screen.y + 2 >= renderer->surface.height) continue;
        k = p->life_ms * 256 / PARTICLE_LIFE_MS;
        if (k > 256) k = 256;   /* 寿命随机 +40ms 可能超出基准，钳制插值比例 */
        fill_rect(&renderer->surface, screen.x, screen.y, 2, 2,
                  mix_color(0xFFC860, 0x4A2008, k, 256));
        pixels++;
    }
    return pixels;
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

static void draw_level_won_panel(struct toy_surface *surface)
{
    char line[96];
    int panel_w = surface->width / 3;
    int panel_h = surface->height / 3;
    int x = (surface->width - panel_w) / 2;
    int y = (surface->height - panel_h) / 2;
    fill_rect(surface, x - 3, y - 3, panel_w + 6, panel_h + 6, 0x56B878);
    fill_rect(surface, x, y, panel_w, panel_h, 0x171B24);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * 17) / 2, y + 28,
                   "SAFE ROOM REACHED", 0x80E080, surface->stride);
    snprintf(line, sizeof(line), "KILLS %d", game.kills);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * (int)strlen(line)) / 2, y + 60,
                   line, 0xE7E9EC, surface->stride);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * 21) / 2, y + 104,
                   "R restart   Esc quit", 0xD88A32, surface->stride);
}

static void draw_input_debug(struct toy_surface *surface,
                             const struct toy_input *input,
                             unsigned int last_key, int last_pressed,
                             int event_count)
{
    char line[96];
    int x = 8;
    int y = surface->height - FB_FONT_H * 4 - 12;
    unsigned int color = input->keyboard_focused ? 0x80E080 : 0xE08080;
    fill_rect(surface, x - 4, y - 4, 390, FB_FONT_H * 4 + 8, 0x171B24);
    snprintf(line, sizeof(line), "KEYBOARD %s  EVENTS %d",
             input->keyboard_focused ? "FOCUSED" : "UNFOCUSED", event_count);
    fb_draw_string((unsigned char *)surface->pixels, x, y,
                   line, color, surface->stride);
    snprintf(line, sizeof(line), "LAST %u %s",
             last_key, last_pressed ? "PRESSED" : "RELEASED");
    fb_draw_string((unsigned char *)surface->pixels, x, y + FB_FONT_H,
                   line, 0xE7E9EC, surface->stride);
    snprintf(line, sizeof(line), "DOWN W%d A%d S%d D%d SPC%d ENT%d",
             toy_input_down(input, KEY_W), toy_input_down(input, KEY_A),
             toy_input_down(input, KEY_S), toy_input_down(input, KEY_D),
             toy_input_down(input, KEY_SPACE), toy_input_down(input, KEY_ENTER));
    fb_draw_string((unsigned char *)surface->pixels, x, y + FB_FONT_H * 2,
                   line, 0xE7E9EC, surface->stride);
    snprintf(line, sizeof(line), "EDGE SPC P%d R%d  R P%d R%d",
             toy_input_pressed(input, KEY_SPACE),
             toy_input_released(input, KEY_SPACE),
             toy_input_pressed(input, KEY_R),
             toy_input_released(input, KEY_R));
    fb_draw_string((unsigned char *)surface->pixels, x, y + FB_FONT_H * 3,
                   line, 0xD8B060, surface->stride);
}

/* ── 音效输出：独立音频线程持续混音，游戏线程只提交事件 ──────── */

#define SFX_BLOCK_FRAMES 512
#define AUDIO_EVENT_RING 32

struct audio_ctx {
    struct toy_audio output;
    struct toy_sfx sfx;
    unsigned char events[AUDIO_EVENT_RING];
    volatile unsigned int event_wpos;
    volatile unsigned int event_rpos;
    pthread_t thread;
    volatile int quit;
    int running;
};

/* 8 种核心音效的 TSND 资产（tools/gen_sfx.c 生成）；加载失败的 kind 回退
 * 程序合成。资产为 44100Hz 单声道，与音频线程输出率一致，无需重采样。 */
static struct toy_sound_asset sfx_assets[TOY_SFX_PLAYER_DEATH + 1];

static const char *sfx_asset_names[TOY_SFX_PLAYER_DEATH + 1] = {
    "gunshot", "dry_fire", "reload_start", "reload_done",
    "hit_marker", "kill", "bite", "death",
};

static void load_sfx_assets(void)
{
    int kind, loaded = 0;
    for (kind = 0; kind <= TOY_SFX_PLAYER_DEATH; kind++) {
        char path[96];
        snprintf(path, sizeof(path), "assets/generated/sfx_%s.tsnd",
                 sfx_asset_names[kind]);
        if (toy_sound_load(path, &sfx_assets[kind]) == 0) loaded++;
    }
    __printf("wayland_fps: sound assets %d/%d loaded\n",
             loaded, TOY_SFX_PLAYER_DEATH + 1);
}

/* 单生产者/单消费者事件环；满时丢弃新事件，避免阻塞游戏线程。 */
static void audio_post_event(struct audio_ctx *ctx, int kind)
{
    unsigned int wp = ctx->event_wpos;
    if (wp - ctx->event_rpos >= AUDIO_EVENT_RING) return;
    ctx->events[wp & (AUDIO_EVENT_RING - 1)] = (unsigned char)kind;
    __sync_synchronize();
    ctx->event_wpos = wp + 1;
}

static void audio_drain_events(struct audio_ctx *ctx)
{
    while (ctx->event_rpos != ctx->event_wpos) {
        unsigned int rp = ctx->event_rpos;
        int kind;
        __sync_synchronize();
        kind = ctx->events[rp & (AUDIO_EVENT_RING - 1)];
        ctx->event_rpos = rp + 1;
        toy_sfx_play(&ctx->sfx, kind);
    }
}

static void *audio_thread_func(void *arg)
{
    struct audio_ctx *ctx = (struct audio_ctx *)arg;
    short play_buf[SFX_BLOCK_FRAMES * 2];
    while (!ctx->quit) {
        long ret;
        audio_drain_events(ctx);
        toy_sfx_render(&ctx->sfx, play_buf, SFX_BLOCK_FRAMES);
        ret = toy_audio_write(&ctx->output, play_buf, SFX_BLOCK_FRAMES);
        if (ret == -EPIPE && ctx->output.backend == TOY_AUDIO_ALSA) {
            /* XRUN：恢复设备后继续 */
            __ioctl(ctx->output.alsa.fd, SNDRV_PCM_IOCTL_PREPARE, 0);
            continue;
        }
        if (ret < 0) break;   /* 设备级错误：静默停声 */
    }
    return NULL;
}

static int audio_start(struct audio_ctx *ctx)
{
    int kind;
    memset(ctx, 0, sizeof(struct audio_ctx));
    if (toy_audio_open(&ctx->output, TOY_SFX_RATE, 2) < 0) return -1;
    toy_sfx_init(&ctx->sfx, TOY_SFX_RATE);
    /* 优先样本音色（TSND 资产），未加载成功的 kind 保持程序合成 */
    for (kind = 0; kind <= TOY_SFX_PLAYER_DEATH; kind++)
        if (sfx_assets[kind].blob)
            toy_sfx_set_sample(&ctx->sfx, kind,
                               (const short *)sfx_assets[kind].data,
                               sfx_assets[kind].frames);
    toy_sfx_music(&ctx->sfx, 1);
    if (pthread_create(&ctx->thread, NULL, audio_thread_func, ctx) != 0) {
        toy_audio_close(&ctx->output);
        return -1;
    }
    ctx->running = 1;
    __printf("wayland_fps: audio backend: %s\n",
             toy_audio_backend_name(&ctx->output));
    return 0;
}

static void audio_stop(struct audio_ctx *ctx)
{
    if (!ctx->running) return;
    ctx->quit = 1;
    pthread_join(ctx->thread, NULL);
    toy_audio_close(&ctx->output);
    ctx->running = 0;
}

/* 事件 → 音效（每帧上限 4 个，防同帧爆发撑爆 voice） */
static void play_game_events(struct audio_ctx *audio)
{
    unsigned char evs[TOY_GAME_MAX_EVENTS];
    int ne = toy_game_drain_events(&game, evs, TOY_GAME_MAX_EVENTS);
    int i;
    if (ne > 4) ne = 4;
    for (i = 0; i < ne; i++) {
        switch (evs[i]) {
        case TOY_GAME_EV_SHOOT: audio_post_event(audio, TOY_SFX_GUNSHOT); break;
        case TOY_GAME_EV_DRY_FIRE: audio_post_event(audio, TOY_SFX_DRY_FIRE); break;
        case TOY_GAME_EV_RELOAD_START: audio_post_event(audio, TOY_SFX_RELOAD_START); break;
        case TOY_GAME_EV_RELOAD_DONE: audio_post_event(audio, TOY_SFX_RELOAD_DONE); break;
        case TOY_GAME_EV_KILL: audio_post_event(audio, TOY_SFX_KILL); break;
        case TOY_GAME_EV_BITE: audio_post_event(audio, TOY_SFX_BITE); break;
        case TOY_GAME_EV_PLAYER_DEATH: audio_post_event(audio, TOY_SFX_PLAYER_DEATH); break;
        default: break;
        }
    }
}

static int run_logic_test(void)
{
    struct camera camera;
    struct control_settings settings;
    struct toy_input input;
    struct vec3 triangle[3], clipped[4];
    struct toy_surface surface;
    struct toy_renderer renderer;
    uint32_t *pixels;
    int count;
    long long direction_length;
    settings.mouse_level = 5;
    settings.keyboard_level = 5;
    toy_input_init(&input);
    reset_game(&camera, 1);
    input.key_down[KEY_W] = 1;
    for (int i = 0; i < 10; i++) update_player(&camera, &input);
    if (camera.x != 0 || camera.z != -4240) return 1;

    /* 死亡重开仍须恢复世界碰撞配置并能推进首波。 */
    game.state = TOY_GAME_OVER;
    reset_game(&camera, 2);
    if (game.world != map_bounds || game.world_count != level_map.box_count ||
        game.room_limit != level_map.room_limit ||
        toy_game_position_blocked(&game, camera.x, camera.z,
                                  PLAYER_RADIUS)) return 13;
    for (int i = 0; i < 170; i++)
        toy_game_update(&game, NULL, 0, camera.sy, camera.cy, 16);
    if (game.enemies_alive == 0) return 14;

    camera.x = -1200; camera.z = 0;
    for (int i = 0; i < 10; i++) update_player(&camera, &input);
    if (camera.z >= level_map.boxes[0].minz - PLAYER_RADIUS) return 2;

    input.key_down[KEY_W] = 0;
    input.key_down[KEY_RIGHT] = 1;
    update_keyboard_look(&camera, &input, &settings);
    /* 首次转向时 old_sy=0，定点旋转 cy 恰为 1024 不变，只断言转向生效 */
    if (camera.sy <= 0) return 3;
    input.key_down[KEY_RIGHT] = 0;
    input.key_down[KEY_UP] = 1;
    update_keyboard_look(&camera, &input, &settings);
    if (camera.pitch_sy <= 0) return 4;
    camera.sy = 0;
    camera.cy = 1024;
    for (int i = 0; i < 10000; i++) rotate_camera(&camera, 37, 0);
    direction_length = isqrt((long long)camera.sy * camera.sy +
                             (long long)camera.cy * camera.cy);
    if (direction_length < 1022 || direction_length > 1026) return 16;
    camera.sy = 0;
    camera.cy = 1024;
    /* 俯仰持续旋转时保持归一化，并钳制在 ±75°，不能跨过 ±90°。 */
    camera.pitch_sy = 0;
    camera.pitch_cy = 1024;
    for (int i = 0; i < 10000; i++) rotate_camera(&camera, 0, 37);
    direction_length = isqrt((long long)camera.pitch_sy * camera.pitch_sy +
                             (long long)camera.pitch_cy * camera.pitch_cy);
    if (direction_length < 1022 || direction_length > 1026) return 41;
    if (camera.pitch_cy != PITCH_LIMIT_CY ||
        camera.pitch_sy != PITCH_LIMIT_SY) return 43;
    camera.pitch_sy = -1024;
    camera.pitch_cy = 0;
    for (int i = 0; i < 1000; i++) rotate_camera(&camera, 0, -31);
    direction_length = isqrt((long long)camera.pitch_sy * camera.pitch_sy +
                             (long long)camera.pitch_cy * camera.pitch_cy);
    if (direction_length < 1022 || direction_length > 1026) return 42;
    if (camera.pitch_cy != PITCH_LIMIT_CY ||
        camera.pitch_sy != -PITCH_LIMIT_SY) return 44;
    camera.pitch_sy = 0;
    camera.pitch_cy = 1024;

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
    if (toy_renderer_begin(&renderer, &surface, 0x151922) < 0) {
        toy_renderer_destroy(&renderer);
        tlibc_free(pixels);
        return 8;
    }
    count = render_scene(&renderer, &camera);
    /* 破碎画面曾仍返回正数；完整 320x180 场景应稳定超过此下限。 */
    if (count < 50000) {
        toy_renderer_destroy(&renderer);
        tlibc_free(pixels);
        return 8;
    }
    /* 两个槽位分别覆盖方块人和圆柱人，确保模型几何进入光栅器。 */
    memset(game.enemies, 0, sizeof(game.enemies));
    game.enemies[0].active = 1;
    game.enemies[0].x = -350;
    game.enemies[0].z = -2500;
    game.enemies[0].dir_z = -1024;
    game.enemies[1].active = 1;
    game.enemies[1].x = 350;
    game.enemies[1].z = -2500;
    game.enemies[1].dir_z = -1024;
    game.enemies[1].ai_state = TOY_GAME_ENEMY_ALERT;
    if (render_enemies(&renderer, &camera) < 1000) {
        toy_renderer_destroy(&renderer);
        tlibc_free(pixels);
        return 15;
    }
    /* 弹道/粒子渲染冒烟：轴向弹道 + 终点越出右屏的弹道（屏幕裁剪与
     * 偏移线边界） + 远处粒子 */
    {
        struct tracer *t = &tracers[0];
        struct particle *p = &particles[0];
        t->active = 1;
        t->sx = camera.x;
        t->sy = TRACER_Y;
        t->sz = camera.z;
        t->ex = camera.x;
        t->ez = camera.z - 3000;
        t->life_ms = 100;
        tracers[1].active = 1;
        tracers[1].sx = camera.x;
        tracers[1].sy = TRACER_Y;
        tracers[1].sz = camera.z;
        tracers[1].ex = camera.x + 4000;
        tracers[1].ez = camera.z - 3000;
        tracers[1].life_ms = 100;
        p->active = 1;
        p->x = camera.x + 200;
        p->y = -350;
        p->z = camera.z - 2000;
        p->life_ms = 100;
        render_tracers(&renderer, &camera);
        render_particles(&renderer, &camera);
        memset(tracers, 0, sizeof(tracers));
        memset(particles, 0, sizeof(particles));
    }
    /* 开发者区域场景：纹理墙/模型台座渲染 + 真实开火产生弹道与命中
     * 火花（向北穿门洞直达最大射程长弹道，向西命中空气墙 1800）。
     * 弹道起点取枪口世界坐标；同帧附加枪口粒子。 */
    {
        camera.x = 0;
        camera.z = -6400;
        game.px = 0;
        game.pz = -6400;
        if (toy_game_equip_weapon(&game, TOY_GAME_WEAPON_SMG) != 1) return 40;
        render_scene(&renderer, &camera);
        last_fire_seq = game.fire_seq;
        toy_game_fire(&game, 0, 1024);
        sync_fire_effects(&camera);
        toy_game_fire(&game, -1024, 0);
        sync_fire_effects(&camera);
        update_effects(16);
        render_tracers(&renderer, &camera);
        render_particles(&renderer, &camera);
        /* 第一人称武器模型冒烟：SMG 几何进入光栅器 */
        render_viewmodel(&renderer);
        memset(tracers, 0, sizeof(tracers));
        memset(particles, 0, sizeof(particles));
    }
    toy_renderer_destroy(&renderer);
    tlibc_free(pixels);

    /* 输入状态回归：组合键、重复 press 边沿、释放和失焦清理。 */
    {
        struct toy_window_events ev;
        toy_input_init(&input);
        memset(&ev, 0, sizeof(ev));
        ev.keyboard_focus_changed = 1;
        ev.keyboard_focused = 1;
        ev.key_event_count = 2;
        ev.key_events[0].key = KEY_W;
        ev.key_events[0].pressed = 1;
        ev.key_events[1].key = KEY_D;
        ev.key_events[1].pressed = 1;
        toy_input_apply(&input, &ev);
        if (!toy_input_down(&input, KEY_W) || !toy_input_down(&input, KEY_D) ||
            !toy_input_pressed(&input, KEY_W) ||
            !toy_input_pressed(&input, KEY_D)) return 10;
        toy_input_begin_frame(&input);
        memset(&ev, 0, sizeof(ev));
        ev.key_event_count = 1;
        ev.key_events[0].key = KEY_W;
        ev.key_events[0].pressed = 1; /* compositor repeat */
        toy_input_apply(&input, &ev);
        if (!toy_input_down(&input, KEY_W) ||
            toy_input_pressed(&input, KEY_W)) return 11;
        toy_input_begin_frame(&input);
        memset(&ev, 0, sizeof(ev));
        ev.keyboard_focus_changed = 1;
        ev.keyboard_focused = 0;
        toy_input_apply(&input, &ev);
        if (toy_input_down(&input, KEY_W) || toy_input_down(&input, KEY_D) ||
            !toy_input_released(&input, KEY_W) ||
            !toy_input_released(&input, KEY_D)) return 12;
    }

    /* 游戏规则冒烟：同 seed 推进 500 步，两遍运行状态一致 */
    {
        struct toy_game smoke;
        int pass, i;
        int first_wave = 0, first_kills = 0, first_hp = 0;
        for (pass = 0; pass < 2; pass++) {
            toy_game_init(&smoke, 1234);
            toy_game_set_world(&smoke, map_bounds, level_map.box_count, level_map.room_limit);
            smoke.px = 0;
            smoke.pz = -5000;
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
    /* 武器槽：默认手枪出枪（槽 2），主武器槽为空；切空槽无效 */
    {
        int i;
        unsigned char keys[TOY_INPUT_KEY_COUNT];
        reset_game(&camera, 7);
        if (game.current_slot != 1 ||
            game.slots[1].weapon != TOY_GAME_WEAPON_PISTOL ||
            game.slots[1].mag != 30 ||
            game.slots[1].reserve != TOY_GAME_AMMO_INFINITE ||
            game.slots[0].weapon != -1) return 17;
        if (toy_game_switch_weapon(&game, 0)) return 18;
        /* 拾取 SMG：50/650 并自动切出；同武器再拾取 = 补充弹药 */
        if (toy_game_equip_weapon(&game, TOY_GAME_WEAPON_SMG) != 1) return 19;
        if (game.current_slot != 0 || game.slots[0].mag != 50 ||
            game.slots[0].reserve != 650) return 20;
        game.slots[0].mag = 7;
        game.slots[0].reserve = 120;
        if (toy_game_equip_weapon(&game, TOY_GAME_WEAPON_SMG) != 0 ||
            game.slots[0].mag != 50 || game.slots[0].reserve != 650) return 21;
        /* 切枪保留各自弹药状态 */
        if (!toy_game_switch_weapon(&game, 1) || game.slots[1].mag != 30)
            return 22;
        if (!toy_game_switch_weapon(&game, 0) ||
            game.slots[0].mag != 50 || game.slots[0].reserve != 650) return 23;
        /* 弹药盒补满备弹，不碰弹匣 */
        game.slots[0].mag = 3;
        game.slots[0].reserve = 5;
        if (!toy_game_refill_ammo(&game) || game.slots[0].reserve != 650 ||
            game.slots[0].mag != 3) return 24;
        /* SMG 全自动：按住 10 步（160ms，100ms 冷却）消耗 2 发 */
        game.slots[0].mag = 50;
        for (i = 0; i < 10; i++)
            toy_game_update_held(&game, NULL, 0, 1, 0, 1024, 16);
        if (game.slots[0].mag != 48) return 25;
        /* 手枪半自动：按住不连发 */
        toy_game_switch_weapon(&game, 1);
        for (i = 0; i < 5; i++)
            toy_game_update_held(&game, NULL, 0, 1, 0, 1024, 16);
        if (game.slots[1].mag != 30) return 26;
        /* 有限备弹换弹：SMG 2000ms，消耗备弹 */
        memset(keys, 0, sizeof(keys));
        keys[TOY_GAME_KEY_RELOAD] = 1;
        toy_game_switch_weapon(&game, 0);
        toy_game_update(&game, keys, 0, 0, 1024, 16);
        if (!game.reloading) return 27;
        for (i = 0; i < 125; i++)
            toy_game_update(&game, NULL, 0, 0, 1024, 16);
        if (game.reloading || game.slots[0].mag != 50 ||
            game.slots[0].reserve != 648) return 28;
        /* 霰弹枪 8/64：10 弹丸随机散射，整匣内应放倒并排两敌 */
        if (toy_game_equip_weapon(&game, TOY_GAME_WEAPON_SHOTGUN) != 1 ||
            game.slots[0].mag != 8 || game.slots[0].reserve != 64) return 29;
        memset(game.enemies, 0, sizeof(game.enemies));
        game.enemies[0].active = 1;
        game.enemies[0].x = game.px + 120;
        game.enemies[0].z = game.pz + 900;
        game.enemies[1].active = 1;
        game.enemies[1].x = game.px - 120;
        game.enemies[1].z = game.pz + 900;
        game.enemies_alive = 2;
        game.kills = 0;
        {
            unsigned int seq_before = game.fire_seq;
            for (i = 0; i < 8 && game.kills < 2; i++)
                toy_game_fire(&game, 0, 1024);
            if (game.kills != 2) return 30;
            if (game.slots[0].mag != 8 - i) return 31;
            /* 弹道记录：每枪 10 条射线，方向已归一化，终点不越最大射程 */
            if (game.ray_count != 10 ||
                game.fire_seq != seq_before + (unsigned int)i) return 38;
            for (i = 0; i < game.ray_count; i++) {
                const struct toy_game_ray *r = &game.rays[i];
                long long len_sq = (long long)r->sy * r->sy +
                                   (long long)r->cy * r->cy;
                if (len_sq < 1022 * 1022 || len_sq > 1026 * 1026) return 38;
                if (r->ex < game.px - TOY_GAME_MAX_RANGE ||
                    r->ex > game.px + TOY_GAME_MAX_RANGE ||
                    r->ez < game.pz - TOY_GAME_MAX_RANGE ||
                    r->ez > game.pz + TOY_GAME_MAX_RANGE) return 38;
            }
        }
        /* 无敌人时射线止于墙：向西命中起点安全室西墙（x=-1800 面）。
         * 弹丸有随机偏转，但终点都应落在该墙面范围内。 */
        game.reloading = 0;
        game.slots[0].mag = 8;
        memset(game.enemies, 0, sizeof(game.enemies));
        game.enemies_alive = 0;
        toy_game_fire(&game, -1024, 0);
        if (game.ray_count != 10) return 39;
        for (i = 0; i < game.ray_count; i++) {
            const struct toy_game_ray *r = &game.rays[i];
            if (r->hit_enemy || !r->hit_world) return 39;
            if (r->ex < game.px - 1803 || r->ex > game.px - 1797) return 39;
            if (r->ez < -5700 || r->ez > -3800) return 39;
        }
    }
    /* 地图拾取物解析：2 把主武器 + 1 个弹药盒 */
    if (interactable_count != 3 ||
        interactables[0].kind != TOY_MAP_PICKUP_SMG ||
        interactables[2].kind != TOY_MAP_PICKUP_AMMO) return 32;
    /* 固定拾取点：拾取后武器仍在场，同武器再互动 = 补充弹药 */
    highlighted = 0;
    interact_current(&interactables[0]);
    if (game.slots[0].weapon != TOY_GAME_WEAPON_SMG ||
        game.slots[0].mag != 50 || game.slots[0].reserve != 650) return 33;
    game.slots[0].mag = 3;
    game.slots[0].reserve = 5;
    highlighted = 0;
    interact_current(&interactables[0]);
    if (game.slots[0].mag != 50 || game.slots[0].reserve != 650) return 33;
    /* 拾取不影响高亮：SMG 固定点仍可被选中 */
    {
        struct camera aim_cam;
        aim_cam.x = 0;
        aim_cam.z = -7000;
        aim_cam.sy = 0;
        aim_cam.cy = -1024;
        aim_cam.pitch_sy = 0;
        aim_cam.pitch_cy = 1024;
        if (compute_highlight(&aim_cam) != 0) return 33;
    }
    /* 高亮判定：世界空间朝向锥。面向桌子（武器低于视平线）可选中最远
     * 的 SMG；背对或超出距离均不可选。 */
    {
        struct camera aim_cam;
        aim_cam.x = 0;
        aim_cam.z = -7000;
        aim_cam.sy = 0;
        aim_cam.cy = -1024;
        aim_cam.pitch_sy = 0;
        aim_cam.pitch_cy = 1024;
        highlighted = compute_highlight(&aim_cam);
        if (highlighted != 0) return 34;
        aim_cam.sy = 1024;
        aim_cam.cy = 0;
        if (compute_highlight(&aim_cam) != -1) return 35;
        aim_cam.sy = 0;
        aim_cam.cy = -1024;
        aim_cam.z = -5200;
        if (compute_highlight(&aim_cam) != -1) return 36;
        /* 斜向面向桌子东侧：只有弹药盒在锥内（两把枪横向偏角过大） */
        aim_cam.x = 1000;
        aim_cam.z = -7000;
        aim_cam.sy = 0;
        aim_cam.cy = -1024;
        if (compute_highlight(&aim_cam) != 2) return 37;
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
    struct control_settings settings;
    struct pause_menu pause_menu;
    long last_time, accumulator = 0, fps_window_start, fps_elapsed;
    int running = 1, pointer_lock_requested = 0, paused = 1;
    int last_pointer_x = 0, last_pointer_y = 0, have_pointer_position = 0;
    int frame_limit = 0, rendered_frames = 0, scene_pixels = 0;
    int display_fps = 0, fps_window_frames = 0;
    int fire_edge = 0;
    int interact_consumed = 0;   /* 每帧 E 互动只消费一次 */
    int input_debug = 0, input_event_count = 0, have_last_key = 0;
    int texture_stats = 0;
    unsigned int last_key = 0;
    int last_key_pressed = 0;
    struct audio_ctx audio;
    uint64_t seed;

    int logic_test = 0;
    int auto_mode = 0;
    const char *dump_path = 0;
    for (int arg = 1; arg < argc; arg++) {
        if (strcmp(argv[arg], "--input-test") == 0) input_debug = 1;
        else if (strcmp(argv[arg], "--logic-test") == 0) logic_test = 1;
        else if (strcmp(argv[arg], "--auto") == 0) auto_mode = 1;
        else if (strcmp(argv[arg], "--no-textures") == 0) textures_enabled = 0;
        else if (strcmp(argv[arg], "--texture-stats") == 0) texture_stats = 1;
        else if (strcmp(argv[arg], "--dump-frame") == 0 && arg + 1 < argc)
            dump_path = argv[++arg];
        else if (strcmp(argv[arg], "--frames") == 0 && arg + 1 < argc) {
            const char *p = argv[++arg];
            while (*p >= '0' && *p <= '9')
                frame_limit = frame_limit * 10 + (*p++ - '0');
        }
    }
    if (toy_map_load("assets/maps/wayland_fps.map", &level_map) < 0) {
        __fprintf(2, "wayland_fps: cannot load map assets/maps/wayland_fps.map\n");
        return 1;
    }
    prepare_map_rules();
    memset(&scene_texture, 0, sizeof(scene_texture));
    memset(&scene_texture_view, 0, sizeof(scene_texture_view));
    if (textures_enabled && toy_texture_load("assets/generated/wall.ttex",
                                              &scene_texture) == 0) {
        scene_texture_view.data = scene_texture.data;
        scene_texture_view.width = scene_texture.width;
        scene_texture_view.height = scene_texture.height;
        scene_texture_view.data_size = scene_texture.data_size;
        wall_texture_view.data = scene_texture_view.data;
        wall_texture_view.width = scene_texture_view.width;
        wall_texture_view.height = scene_texture_view.height;
        wall_texture_view.data_size = scene_texture_view.data_size;
        __printf("wayland_fps: UV texture loaded (%u x %u)\n",
                 scene_texture.width, scene_texture.height);
    } else if (textures_enabled) {
        __printf("wayland_fps: UV texture unavailable, using checkerboard fallback\n");
    } else {
        __printf("wayland_fps: textures disabled, using pure colors\n");
    }
    active_texture_view = &scene_texture_view;
    if (logic_test) {
        int result = run_logic_test();
        if (scene_texture.blob) toy_texture_unload(&scene_texture);
        toy_map_unload(&level_map);
        return result;
    }
    /* 服务器断开（WSLg 组合器/音频服务重启）时 socket 写会触发 SIGPIPE
     * 并默认杀死进程；忽略后写返回 EPIPE，由既有错误路径接管（音频线程
     * 静默停声、wayland 发送失败则主循环干净退出）。SIG_IGN 值为 1。 */
    tlibc_sigaction(SIGPIPE, (void (*)(int))1);
    toy_input_init(&input);
    toy_renderer_init(&renderer);
    settings.mouse_level = 5;
    settings.keyboard_level = 5;
    pause_menu.selected = PAUSE_ITEM_RESUME;
    if (__getrandom(&seed, sizeof(seed), 0) < 0)
        seed = (uint64_t)monotonic_us();
    if (seed == 0) seed = 1;
    reset_game(&camera, seed);
    window = toy_window_open("Toyc FPS Zombies", 800, 450);
    if (!window) {
        __fprintf(2, "wayland_fps: cannot create Wayland window\n");
        if (scene_texture.blob) toy_texture_unload(&scene_texture);
        toy_map_unload(&level_map);
        toy_renderer_destroy(&renderer);
        return 1;
    }
    __printf("wayland_fps: pause menu uses arrows + Enter; mouse/arrows look, "
             "WASD moves, click/Space fire (hold for SMG), R reload, "
             "1/2 weapons, E interact, Esc pauses/resumes\n");
    if (input_debug)
        __printf("wayland_fps: input debug HUD enabled; test chords and focus changes\n");
    load_sfx_assets();
    if (audio_start(&audio) < 0) {
        __printf("wayland_fps: audio unavailable, playing silent\n");
    }
    last_time = monotonic_us();
    fps_window_start = last_time;
    while (running) {
        long now, elapsed;
        int logic_steps = 0;
        int resumed = 0;
        toy_input_begin_frame(&input);
        if (toy_window_poll(window, &events, 1000) < 0) break;
        toy_input_apply(&input, &events);
        if (events.key_event_count > 0) {
            int at = events.key_event_count - 1;
            last_key = events.key_events[at].key;
            last_key_pressed = events.key_events[at].pressed;
            have_last_key = 1;
            input_event_count += events.key_event_count;
        }
        if (events.pointer_lock_changed) {
            if (events.pointer_locked)
                __printf("wayland_fps: pointer constraint activated\n");
            else {
                __printf("wayland_fps: pointer constraint released\n");
                pointer_lock_requested = 0;
            }
        }
        if (paused && game.state == TOY_GAME_PLAYING) {
            int resume_requested = 0;
            if (toy_input_pressed(&input, KEY_UP)) {
                pause_menu.selected--;
                if (pause_menu.selected < 0)
                    pause_menu.selected = PAUSE_ITEM_COUNT - 1;
            }
            if (toy_input_pressed(&input, KEY_DOWN)) {
                pause_menu.selected++;
                if (pause_menu.selected >= PAUSE_ITEM_COUNT)
                    pause_menu.selected = 0;
            }
            if (toy_input_pressed(&input, KEY_LEFT) ||
                toy_input_pressed(&input, KEY_RIGHT)) {
                int change = toy_input_pressed(&input, KEY_RIGHT) ? 1 : -1;
                if (pause_menu.selected == PAUSE_ITEM_MOUSE)
                    settings.mouse_level = clampi(settings.mouse_level + change, 0, 15);
                else if (pause_menu.selected == PAUSE_ITEM_KEYBOARD)
                    settings.keyboard_level = clampi(settings.keyboard_level + change, 0, 15);
            }
            if (toy_input_pressed(&input, KEY_ENTER)) {
                if (pause_menu.selected == PAUSE_ITEM_RESUME)
                    resume_requested = 1;
                else if (pause_menu.selected == PAUSE_ITEM_QUIT)
                    running = 0;
            }
            if (toy_input_pressed(&input, KEY_ESC)) resume_requested = 1;
            if (resume_requested) {
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
        }
        if (!paused && !resumed && toy_input_pressed(&input, KEY_ESC)) {
            if (game.state == TOY_GAME_OVER || game.state == TOY_GAME_WON)
                running = 0;
            else {
                toy_window_set_pointer_lock(window, 0);
                pointer_lock_requested = 0;
                paused = 1;
                pause_menu.selected = PAUSE_ITEM_RESUME;
                __printf("wayland_fps: paused, pointer released\n");
            }
        }
        /* 射击输入：每帧只取一次边沿（恢复点击帧不开火） */
        if (!paused && !resumed && events.button_pressed && events.button == BTN_LEFT)
            fire_edge = 1;
        if (!paused && !resumed && toy_input_pressed(&input, KEY_SPACE))
            fire_edge = 1;
        if (events.close_requested) running = 0;
        if (!running) break;
        /* --auto：炮弹幕压测（复现崩溃用）。瞬移到关键区域（起点室/
         * 开发者区/中心/刷怪区），快速转枪口持续轰击：弹道终点大量落
         * 在屏幕边缘/屏外（裁剪路径）、穿门洞长弹道（最大射程）、
         * 近距离墙面（命中火花）。 */
        if (auto_mode) {
            static const int spots[][2] = {
                {0, -5000}, {0, -6400}, {0, -9000}, {0, 0},
                {-5500, -1500}, {4500, 500}, {-6000, -2000}, {2000, 2500},
            };
            int idx;
            if (rendered_frames == 60 && paused) {
                paused = 0;
                __printf("wayland_fps: auto barrage started\n");
            }
            if (rendered_frames > 60) {
                if (game.state != TOY_GAME_PLAYING)
                    input.key_pressed[KEY_R] = 1;   /* 死亡重开 */
                fire_edge = 1;
                rotate_camera(&camera, 37, 5);      /* 快速扫射 */
                idx = rendered_frames / 60;
                if (rendered_frames % 60 == 0 && idx < 32) {
                    static const int wslot[3] = {TOY_GAME_WEAPON_PISTOL,
                                                 TOY_GAME_WEAPON_SMG,
                                                 TOY_GAME_WEAPON_SHOTGUN};
                    camera.x = spots[idx % 8][0];
                    camera.z = spots[idx % 8][1];
                    game.px = camera.x;
                    game.pz = camera.z;
                    toy_game_equip_weapon(&game, wslot[idx % 3]);
                    __printf("wayland_fps: auto teleport %d to (%d,%d) w=%d\n",
                             idx, camera.x, camera.z, wslot[idx % 3]);
                }
            }
        }
        /* Some compositors acknowledge locked asynchronously. Relative
         * events received after our accepted request are already valid. */
        if (!paused && (input.pointer_locked || pointer_lock_requested) &&
            events.relative_moved) {
            update_mouse(&camera, input.relative_x, input.relative_y, &settings);
        } else if (!paused && pointer_lock_requested && input.pointer_moved) {
            if (have_pointer_position)
                update_mouse(&camera, input.pointer_x - last_pointer_x,
                              input.pointer_y - last_pointer_y, &settings);
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
        interact_consumed = 0;
        while (accumulator >= FIXED_STEP_US && logic_steps < MAX_LOGIC_STEPS) {
            if (!paused) {
                update_effects(FIXED_STEP_US / 1000);
                if (game.state == TOY_GAME_PLAYING) {
                    update_player(&camera, &input);
                    update_keyboard_look(&camera, &input, &settings);
                    game.px = camera.x;
                    game.pz = camera.z;
                    highlighted = compute_highlight(&camera);
                    if (highlighted >= 0 && !interact_consumed &&
                        toy_input_pressed(&input, KEY_E)) {
                        interact_current(&interactables[highlighted]);
                        interact_consumed = 1;
                    }
                    toy_game_update_held(&game, input.key_pressed, fire_edge,
                                         toy_input_down(&input, KEY_SPACE),
                                         camera.sy, camera.cy,
                                         FIXED_STEP_US / 1000);
                    fire_edge = 0;
                } else if (toy_input_pressed(&input, KEY_R)) {
                    /* 死亡或通关结算：R 重开 */
                    reset_game(&camera, seed);
                    fire_edge = 0;
                }
            }
            accumulator -= FIXED_STEP_US;
            logic_steps++;
        }
        if (!paused) sync_fire_effects(&camera);
        if (accumulator >= FIXED_STEP_US) accumulator %= FIXED_STEP_US;
        int ready = toy_window_begin_frame(window, &surface);
        if (ready < 0) break;
        if (ready > 0) {
            if (toy_renderer_begin(&renderer, &surface, 0x151922) < 0) break;
            scene_pixels = render_scene(&renderer, &camera);
            scene_pixels += render_enemies(&renderer, &camera);
            scene_pixels += render_tracers(&renderer, &camera);
            scene_pixels += render_particles(&renderer, &camera);
            /* 第一人称武器：最后画，叠加在世界之上 */
            scene_pixels += render_viewmodel(&renderer);
            if (game.state == TOY_GAME_OVER) {
                draw_game_over_panel(&surface);
            } else if (game.state == TOY_GAME_WON) {
                draw_level_won_panel(&surface);
            } else if (paused) {
                draw_pause_overlay(&surface, &pause_menu, &settings);
            } else {
                draw_crosshair(&surface);
                render_hud(&surface, display_fps);
            }
            if (game.state == TOY_GAME_PLAYING && !paused) {
                scene_pixels += render_interactables(&renderer, &camera);
                draw_interact_prompt(&renderer);
            }
            render_damage_flash(&surface);
            if (input_debug)
                draw_input_debug(&surface, &input,
                                 have_last_key ? last_key : 0,
                                 have_last_key ? last_key_pressed : 0,
                                 input_event_count);
            /* 游戏线程只投递事件，音乐与 SFX 由音频线程持续混音。 */
            if (audio.running) play_game_events(&audio);
            if (toy_window_present(window) < 0) break;
            rendered_frames++;
            fps_window_frames++;
            now = monotonic_us();
            fps_elapsed = now - fps_window_start;
            if (fps_elapsed >= 1000000) {
                display_fps = (int)((long long)fps_window_frames * 1000000 /
                                    fps_elapsed);
                fps_window_frames = 0;
                fps_window_start = now;
            }
            if (frame_limit > 0 && rendered_frames >= frame_limit) running = 0;
        }
    }
    audio_stop(&audio);
    for (int kind = 0; kind <= TOY_SFX_PLAYER_DEATH; kind++)
        if (sfx_assets[kind].blob) toy_sound_unload(&sfx_assets[kind]);
    if (scene_texture.blob) toy_texture_unload(&scene_texture);
    if (dump_path) dump_frame_ppm(dump_path, &surface);
    toy_map_unload(&level_map);
    toy_window_close(window);
    __printf("wayland_fps: %d frames, %d scene pixels, position=(%d,%d)\n",
             rendered_frames, scene_pixels, camera.x, camera.z);
    if (texture_stats)
        __printf("wayland_fps: texture stats triangles=%lu pixels=%lu fallback=%lu\n",
                 renderer.textured_triangles, renderer.textured_pixels,
                 renderer.texture_fallback_pixels);
    toy_renderer_destroy(&renderer);
    return rendered_frames > 0 && scene_pixels == 0 ? 2 : 0;
}
