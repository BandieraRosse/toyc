#include "core.h"
#include "string.h"
#include "tlibc_everything.h"
#include "toy_renderer.h"
#include "toy_assets.h"
#include "fb_draw.h"
#include "fb_font.h"
#include "toy_map.h"
#include "toy_game.h"
#include "rasterfall_session.h"
#include "rasterfall_effects.h"
#include "rasterfall_net.h"
#include "rasterfall_sky.h"
#include "rasterfall_render.h"
#include "math.h"

#define NEAR_Z 192
#define ENEMY_RENDER_DISTANCE 24000
#define UV_ONE 65536
#define BAKED_LM_W 32
#define BAKED_LM_H 24

struct vec3 { int x, y, z; };
struct box { int minx, maxx, minz, maxz, height; uint32_t color; };

static struct rasterfall_render_context *render_ctx;
static struct rasterfall_session *active_session;
static struct rasterfall_effects *active_effects;
static struct toy_texture_view *active_wall_texture;
static unsigned short *active_lightmap;
static int active_textures;
static int active_fixed_floor_lighting;
static int active_enemy_lift;
static int active_actor_lift;

#define level_map active_session->level
#define game active_session->game_state
#define interactables active_session->items
#define interactable_count active_session->item_count
#define highlighted active_session->highlight_index
#define baked_lightmap active_lightmap
#define baked_light_at_render(x, z) baked_light_at(x, z)
#define fixed_floor_lighting active_fixed_floor_lighting
#define textures_enabled active_textures
#define active_texture_view active_wall_texture
#define wall_texture_view active_wall_texture
#define effects (*active_effects)
typedef struct rasterfall_interactable interactable;

static int clampi(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
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
        for (int px = x; px < right; px++) {
            uint32_t *row = (uint32_t *)((unsigned char *)surface->pixels +
                                         py * surface->stride);
            row[px] = color;
        }
}

static uint32_t mix_color(uint32_t from, uint32_t to, int num, int den)
{
    unsigned long fr = (from >> 16) & 0xFF, fg = (from >> 8) & 0xFF, fb = from & 0xFF;
    unsigned long tr = (to >> 16) & 0xFF, tg = (to >> 8) & 0xFF, tb = to & 0xFF;
    return (uint32_t)(((fr * num + tr * (den - num)) / den) << 16 |
                      ((fg * num + tg * (den - num)) / den) << 8 |
                      ((fb * num + tb * (den - num)) / den));
}

static void bake_static_lightmap(void)
{
    int x, z, i;
    for (z = 0; z < BAKED_LM_H; z++) for (x = 0; x < BAKED_LM_W; x++) {
        int wx = level_map.minx + (level_map.maxx - level_map.minx) * (x * 2 + 1) /
                 (BAKED_LM_W * 2);
        int wz = level_map.minz + (level_map.maxz - level_map.minz) * (z * 2 + 1) /
                 (BAKED_LM_H * 2);
        int light = 270 + (wx - level_map.minx) * 4 /
                    (level_map.maxx - level_map.minx ? level_map.maxx - level_map.minx : 1);
        for (i = 0; i < level_map.box_count; i++) {
            const struct toy_map_box *b = &level_map.boxes[i];
            int dx = wx < b->minx ? b->minx - wx : wx > b->maxx ? wx - b->maxx : 0;
            int dz = wz < b->minz ? b->minz - wz : wz > b->maxz ? wz - b->maxz : 0;
            int dist = dx > dz ? dx : dz;
            if (!b->air && dist < 900)
                light -= (900 - dist) * 24 / 900;
        }
        /* Warm point light baked from the small lamp in the east corner. */
        {
            int ldx = wx - 4000, ldz = wz - 160;
            int ldist = (int)isqrt((long long)ldx * ldx +
                                   (long long)ldz * ldz);
            if (ldist < 2600)
                light += (2600 - ldist) * 26 / 2600;
        }
        if (light < 150) light = 150;
        if (light > 286) light = 286;
        baked_lightmap[z * BAKED_LM_W + x] = (unsigned short)light;
    }
}

static int baked_light_at(int x, int z)
{
    int ix = (x - level_map.minx) * BAKED_LM_W /
             (level_map.maxx - level_map.minx ? level_map.maxx - level_map.minx : 1);
    int iz = (z - level_map.minz) * BAKED_LM_H /
             (level_map.maxz - level_map.minz ? level_map.maxz - level_map.minz : 1);
    if (ix < 0) ix = 0;
    if (ix >= BAKED_LM_W) ix = BAKED_LM_W - 1;
    if (iz < 0) iz = 0;
    if (iz >= BAKED_LM_H) iz = BAKED_LM_H - 1;
    return baked_lightmap[iz * BAKED_LM_W + ix];
}

static int baked_fog_at(int distance)
{
    if (distance <= 12000) return 0;
    if (distance >= 24000) return 210;
    return (distance - 12000) * 210 / 12000;
}

static int world_distance(const struct camera *camera, int x, int z)
{
    long long dx = (long long)x - camera->x;
    long long dz = (long long)z - camera->z;
    return (int)isqrt(dx * dx + dz * dz);
}

/* Map floor paint is an authored area colour, not a lightmap preview. */
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
    {
        int dy = world->y - camera->y;
        view->y = (dy * camera->pitch_cy - wz * camera->pitch_sy) / 1024;
        view->z = (dy * camera->pitch_sy + wz * camera->pitch_cy) / 1024;
    }
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
    int z = view->z < NEAR_Z ? NEAR_Z : view->z;
    screen->x = surface->width / 2 +
                (int)((long)view->x * focal / z);
    screen->y = surface->height / 2 -
                (int)((long)view->y * focal / z);
    screen->z = z;
    /* 渲染器深度缓冲使用逆深度，纯色路径也读该字段；必须在投影时填好。 */
    screen->inv_z = (long)1048576 / z;
    screen->light = 256;
    screen->fog = 0;
}

static void project_uv_vertex(const struct toy_surface *surface,
                              const struct vec3 *view,
                              int u, int v,
                              struct toy_screen_vertex *screen)
{
    int z = view->z < NEAR_Z ? NEAR_Z : view->z;
    project_vertex(surface, view, screen);
    screen->u = u; screen->v = v;
    screen->inv_z = (long)1048576 / z;
    screen->u_over_z = (long)u * 1048576L / z;
    screen->v_over_z = (long)v * 1048576L / z;
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
        project_vertex(&renderer->surface, &clipped[0], &sa);
        project_vertex(&renderer->surface, &clipped[i], &sb);
        project_vertex(&renderer->surface, &clipped[i + 1], &sc);
        /* Projected coordinates are bounded here, so 32-bit area is safe and
         * avoids Toyc's signed int-to-long promotion bug. */
        area = ((long)sc.x - sa.x) * ((long)sb.y - sa.y) -
               ((long)sc.y - sa.y) * ((long)sb.x - sa.x);
        if (area >= 0) {
            struct toy_screen_vertex swap;
            swap.x = sb.x; swap.y = sb.y; swap.z = sb.z;
            swap.inv_z = sb.inv_z;
            sb.x = sc.x; sb.y = sc.y; sb.z = sc.z;
            sb.inv_z = sc.inv_z;
            sc.x = swap.x; sc.y = swap.y; sc.z = swap.z;
            sc.inv_z = swap.inv_z;
        }
        int center_x = (a->x + b->x + c->x) / 3;
        int center_z = (a->z + b->z + c->z) / 3;
        int light = fixed_floor_lighting ? 256 : baked_light_at(center_x, center_z);
        int fog = fixed_floor_lighting ? 0 :
                  baked_fog_at(world_distance(camera, center_x, center_z));
        /* 区域涂色（fixed_floor_lighting）与地砖仅差 6 个世界单位，掠射角下
         * 插值深度误差会盖过真实差值导致 z-fight；涂色按覆盖层绘制，
         * 依赖"地砖先画、墙后画"的记录顺序保证遮挡正确。 */
        if (fixed_floor_lighting)
            drawn += toy_renderer_triangle_lit(renderer, &sa, &sb, &sc,
                                               color, light, fog);
        else
            drawn += toy_renderer_triangle_lit(renderer, &sa, &sb, &sc,
                                               color, light, fog);
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
        long area;
        project_uv_vertex(&renderer->surface, &clipped[0].p,
                          clipped[0].u, clipped[0].v, &sa);
        project_uv_vertex(&renderer->surface, &clipped[i].p,
                          clipped[i].u, clipped[i].v, &sb);
        project_uv_vertex(&renderer->surface, &clipped[i + 1].p,
                          clipped[i + 1].u, clipped[i + 1].v, &sc);
        area = ((long)sc.x - sa.x) * ((long)sb.y - sa.y) -
               ((long)sc.y - sa.y) * ((long)sb.x - sa.x);
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
        int center_x = (a->p.x + b->p.x + c->p.x) / 3;
        int center_z = (a->p.z + b->p.z + c->p.z) / 3;
        int light = fixed_floor_lighting ? 256 : baked_light_at(center_x, center_z);
        int fog = fixed_floor_lighting ? 0 : baked_fog_at(world_distance(camera, center_x, center_z));
        sa.light = sb.light = sc.light = light;
        sa.fog = sb.fog = sc.fog = fog;
        drawn += toy_renderer_triangle_textured_lit(renderer, &sa, &sb, &sc,
                                                     active_texture_view, 1,
                                                     0xFF202020U, light, fog);
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

static int draw_cuboid(struct toy_renderer *renderer,
                       const struct camera *camera,
                       int x0, int x1, int y0, int y1,
                       int z0, int z1, uint32_t color);

static int render_world_sign(struct toy_renderer *renderer,
                             const struct camera *camera,
                             const struct toy_map_draw *sign)
{
    const int text_scale = 2;
    int x = (sign->a + sign->b) / 2;
    int z = (sign->c + sign->d) / 2;
    int pixels = 0;
    struct vec3 world, view;
    struct toy_screen_vertex screen;
    pixels += draw_cuboid(renderer, camera, x - 18, x + 18,
                          sign->e - 220, sign->e, z - 18, z + 18, 0x4B3526);
    pixels += draw_cuboid(renderer, camera, sign->a, sign->b,
                          sign->e, sign->f, sign->c, sign->d, sign->color);
    world.x = x; world.y = (sign->e + sign->f) / 2; world.z = z;
    world_to_view(camera, &world, &view);
    /* The board faces +Z; print the string only when viewed from that side. */
    if (view.z >= NEAR_Z && camera->z > z && sign->text[0]) {
        int width = (int)strlen(sign->text) * FB_FONT_W * text_scale;
        int height = FB_FONT_H * text_scale;
        project_vertex(&renderer->surface, &view, &screen);
        screen.x -= width / 2;
        screen.y -= height / 2;
        if (screen.x >= 0 && screen.x + width < renderer->surface.width &&
            screen.y >= 0 && screen.y + height < renderer->surface.height)
            fb_draw_string_scaled((unsigned char *)renderer->surface.pixels,
                                  screen.x, screen.y, sign->text, 0xFFF0C0,
                                  renderer->surface.stride, text_scale);
    }
    return pixels;
}

static int render_block_enemy(struct toy_renderer *, const struct camera *,
                              const struct toy_game_enemy *, int, uint32_t);
static int render_round_enemy(struct toy_renderer *, const struct camera *,
                              const struct toy_game_enemy *, int, uint32_t);
static int render_smoker_enemy(struct toy_renderer *, const struct camera *,
                               const struct toy_game_enemy *, int, uint32_t);
static int render_charger_enemy(struct toy_renderer *, const struct camera *,
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

static int draw_tongue_segment(struct toy_renderer *renderer,
                               const struct camera *camera,
                               int x0, int y0, int z0,
                               int x1, int y1, int z1)
{
    long dx = x1 - x0, dz = z1 - z0;
    long len = isqrt(dx * dx + dz * dz);
    int width = 18;
    struct vec3 a, b, c, d;
    if (len <= 0) return 0;
    a.x = x0 - (int)(dz * width / len); a.y = y0; a.z = z0 + (int)(dx * width / len);
    b.x = x0 + (int)(dz * width / len); b.y = y0; b.z = z0 - (int)(dx * width / len);
    c.x = x1 + (int)(dz * width / len); c.y = y1; c.z = z1 - (int)(dx * width / len);
    d.x = x1 - (int)(dz * width / len); d.y = y1; d.z = z1 + (int)(dx * width / len);
    return draw_quad(renderer, camera, &a, &b, &c, &d, 0xB98B62);
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

/* 召唤按钮：从墙面凸出的暗色面板 + 正面 LED。每次都可再次触发；
 * 准星对准时面板整体提亮。 */
static int render_button(struct toy_renderer *renderer, const struct camera *camera,
                         int x, int y, int z, int on, int side_wall)
{
    struct vec3 a, b, c, d;
    int pixels = 0;
    if (side_wall == 1) {
        pixels += draw_cuboid(renderer, camera, x - 55, x + 55, y - 55, y + 55,
                              z - 45, z + 55, highlight_tint(0x2E333B, on));
        pixels += draw_cuboid(renderer, camera, x + 55, x + 60, y - 45, y + 45,
                              z - 45, z + 45, highlight_tint(0x1E2229, on));
        /* 西侧墙按钮朝向房间（+x） */
        a.x = x + 61; a.y = y - 22; a.z = z - 24;
        b.x = x + 61; b.y = y + 22; b.z = z - 24;
        c.x = x + 61; c.y = y + 22; c.z = z + 24;
        d.x = x + 61; d.y = y - 22; d.z = z + 24;
    } else if (side_wall == 2) {
        pixels += draw_cuboid(renderer, camera, x - 55, x + 55, y - 55, y + 55,
                              z - 45, z + 55, highlight_tint(0x2E333B, on));
        pixels += draw_cuboid(renderer, camera, x - 60, x - 55, y - 45, y + 45,
                              z - 45, z + 45, highlight_tint(0x1E2229, on));
        /* 东侧墙按钮朝向房间（-x） */
        a.x = x - 61; a.y = y - 22; a.z = z + 24;
        b.x = x - 61; b.y = y + 22; b.z = z + 24;
        c.x = x - 61; c.y = y + 22; c.z = z - 24;
        d.x = x - 61; d.y = y - 22; d.z = z - 24;
    } else {
        pixels += draw_cuboid(renderer, camera, x - 55, x + 55, y - 55, y + 55,
                              z - 45, z + 55, highlight_tint(0x2E333B, on));
        pixels += draw_cuboid(renderer, camera, x - 45, x + 45, y - 45, y + 45,
                              z + 55, z + 60, highlight_tint(0x1E2229, on));
        /* 正面 LED 小方块，面向房间（+z） */
        a.x = x - 24; a.y = y - 22; a.z = z + 61;
        b.x = x + 24; b.y = y - 22; b.z = z + 61;
        c.x = x + 24; c.y = y + 22; c.z = z + 61;
        d.x = x - 24; d.y = y + 22; d.z = z + 61;
    }
    pixels += draw_quad(renderer, camera, &a, &b, &c, &d,
                        0xFF3030);
    return pixels;
}

/* 开发者区的特感测试按钮：矮圆柱底座和略低于视线的按键帽，便于连续
 * 添加新的特感而不再占用墙面按钮。 */
static int render_special_button(struct toy_renderer *renderer,
                                 const struct camera *camera,
                                 int x, int y, int z, int on, int charger)
{
    uint32_t color = charger ? 0x9B5528 : 0x3E7462;
    int pixels = draw_cylinder(renderer, camera, x, z, 190, -900, y, color);
    pixels += draw_cuboid(renderer, camera, x - 125, x + 125,
                          y, y + 35, z - 125, z + 125,
                          highlight_tint(0x252B31, on));
    pixels += draw_cylinder(renderer, camera, x, z, 62, y + 35, y + 78,
                            on ? 0xFFE080 : (charger ? 0xD43A28 : 0x38CFA0));
    return pixels;
}

static int render_interactables(struct toy_renderer *renderer,
                                const struct camera *camera)
{
    int i, pixels = 0;
    for (i = 0; i < interactable_count; i++) {
        const interactable *it = &interactables[i];
        int on = i == highlighted;
        if (it->kind == TOY_MAP_PICKUP_SMG)
            pixels += render_smg(renderer, camera, it->x, it->y, it->z, on);
        else if (it->kind == TOY_MAP_PICKUP_SHOTGUN)
            pixels += render_shotgun(renderer, camera, it->x, it->y, it->z, on);
        else if (it->kind == TOY_MAP_PICKUP_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_AIR_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_ALARM_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_HEAVY_HORDE_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_FAST_HORDE_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_BASE_1_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_BASE_2_BUTTON)
            pixels += render_button(renderer, camera, it->x, it->y, it->z, on,
                                    it->x < -10000 ? 1 : it->x > 10000 ? 2 : 0);
        else if (it->kind == TOY_MAP_PICKUP_SMOKER_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_CHARGER_BUTTON)
            pixels += render_special_button(renderer, camera, it->x, it->y,
                                             it->z, on,
                                             it->kind == TOY_MAP_PICKUP_CHARGER_BUTTON);
        else
            pixels += render_ammo_box(renderer, camera, it->x, it->y, it->z, on);
    }
    return pixels;
}

/* ── 第一人称武器模型：视图空间盒体固定在镜头右下方，开火后坐后移 ── */

static void fill_rect(struct toy_surface *surface, int x, int y,
                      int width, int height, uint32_t color);
static uint32_t mix_color(uint32_t from, uint32_t to, int num, int den);

static int render_scene(struct toy_renderer *renderer, const struct camera *camera)
{
    int pixels = 0;
    struct vec3 a, b, c, d;
    /* 自由俯仰下先铺天空/地面：地平线由俯仰角决定，墙面与地板随后覆盖 */
    rasterfall_sky_draw(&renderer->surface, camera);
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
    for (int i=0; i<level_map.draw_count; i++) {
        struct toy_map_draw *x=&level_map.draw[i];
        if (x->type==TOY_MAP_DRAW_FLOOR) {
            struct toy_game_box zone={x->a,x->b,x->c,x->d};
            fixed_floor_lighting = 1;
            pixels+=draw_floor_zone(renderer,camera,&zone,x->color);
            fixed_floor_lighting = 0;
        } else if (x->type==TOY_MAP_DRAW_BORDER) {
            struct toy_game_box zone={x->a,x->b,x->c,x->d};
            /* 边框与涂色同层：同样走无深度覆盖绘制，颜色也是作者指定的
             * 固定色（不参与烘焙光照/雾）。 */
            fixed_floor_lighting = 1;
            pixels+=draw_floor_border(renderer,camera,&zone,x->e,x->color);
            fixed_floor_lighting = 0;
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
                else if (x->style==2) pixels+=render_round_enemy(renderer,camera,&model,1000,x->color);
                else if (x->style==3) {
                    model.type = TOY_GAME_ENEMY_SMOKER;
                    pixels += render_smoker_enemy(renderer, camera, &model,
                                                  1000, x->color);
                } else if (x->style==4) {
                    model.type = TOY_GAME_ENEMY_CHARGER;
                    pixels += render_charger_enemy(renderer, camera, &model,
                                                   1120, x->color);
                }
            } else {
                struct box model={x->a,x->b,x->c,x->d,x->f,x->color};
                pixels+=draw_cuboid(renderer,camera,model.minx,model.maxx,x->e,x->f,model.minz,model.maxz,model.color);
            }
        } else if (x->type==TOY_MAP_DRAW_TEXTURE) {
            a.x=x->a;a.y=-900;a.z=x->c;b.x=x->b;b.y=-900;b.z=x->c;c.x=x->b;c.y=x->e;c.z=x->c;d.x=x->a;d.y=x->e;d.z=x->c;
            pixels+=draw_position_quad_tex(renderer,camera,&a,&b,&c,&d,x->texture_u*UV_ONE,x->texture_v*UV_ONE,x->color);
        } else if (x->type==TOY_MAP_DRAW_LABEL) {
            struct toy_game_box zone={x->a,x->b,x->c,x->d}; draw_world_label(renderer,camera,&zone,x->text,x->color);
        } else if (x->type==TOY_MAP_DRAW_SIGN) {
            pixels += render_world_sign(renderer, camera, x);
        }
    }
    for (int i=0; i<level_map.box_count; i++) if (level_map.boxes[i].visible) {
        struct box obstacle={level_map.boxes[i].minx,level_map.boxes[i].maxx,level_map.boxes[i].minz,level_map.boxes[i].maxz,level_map.boxes[i].height,level_map.boxes[i].color};
        pixels+=draw_box(renderer,camera,&obstacle);
    }
    return pixels;
}
static int enemy_y(int y, int scale)
{
    return -900 + (y + 900) * scale / 1000 + active_enemy_lift;
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

static int render_charger_enemy(struct toy_renderer *renderer,
                                const struct camera *camera,
                                const struct toy_game_enemy *e, int scale,
                                uint32_t color)
{
    int x = e->x, z = e->z, pixels = 0;
    int sx = scale * 100 / 1120;
    pixels += draw_cuboid(renderer, camera, x - 135 * sx / 100, x - 20 * sx / 100,
                          enemy_y(-900, scale), enemy_y(-760, scale),
                          z - 150 * sx / 100, z + 110 * sx / 100, 0x30261F);
    pixels += draw_cuboid(renderer, camera, x + 20 * sx / 100, x + 135 * sx / 100,
                          enemy_y(-900, scale), enemy_y(-760, scale),
                          z - 150 * sx / 100, z + 110 * sx / 100, 0x30261F);
    pixels += draw_cuboid(renderer, camera, x - 235 * sx / 100, x + 235 * sx / 100,
                          enemy_y(-760, scale), enemy_y(95, scale),
                          z - 135 * sx / 100, z + 135 * sx / 100, color);
    pixels += draw_cuboid(renderer, camera, x - 205 * sx / 100, x + 205 * sx / 100,
                          enemy_y(75, scale), enemy_y(360, scale),
                          z - 145 * sx / 100, z + 145 * sx / 100, color + 0x18100A);
    /* Charger 面部是一个粗像素 C。 */
    pixels += draw_face_rect(renderer, camera, x, z, 150, e->dir_x, e->dir_z,
                             -120, -82, enemy_y(105, scale), enemy_y(315, scale),
                             0x2A1710);
    pixels += draw_face_rect(renderer, camera, x, z, 150, e->dir_x, e->dir_z,
                             -82, 100, enemy_y(280, scale), enemy_y(315, scale),
                             0x2A1710);
    pixels += draw_face_rect(renderer, camera, x, z, 150, e->dir_x, e->dir_z,
                             -82, 100, enemy_y(105, scale), enemy_y(140, scale),
                             0x2A1710);
    return pixels;
}

static int render_smoker_enemy(struct toy_renderer *renderer,
                               const struct camera *camera,
                               const struct toy_game_enemy *e, int scale,
                               uint32_t color)
{
    int x = e->x, z = e->z, pixels = 0;
    pixels += draw_cuboid(renderer, camera, x - 105, x - 25,
                          enemy_y(-900, scale), enemy_y(-760, scale),
                          z - 105, z + 75, 0x30261F);
    pixels += draw_cuboid(renderer, camera, x + 25, x + 105,
                          enemy_y(-900, scale), enemy_y(-760, scale),
                          z - 105, z + 75, 0x30261F);
    pixels += draw_cylinder(renderer, camera, x, z, 145,
                            enemy_y(-770, scale), enemy_y(180, scale), color);
    pixels += draw_ellipsoid_head(renderer, camera, x, z,
                                  enemy_y(315, scale), 165,
                                  (enemy_y(560, scale) - enemy_y(130, scale)) / 2,
                                  color + 0x18100A);
    /* 5x7 像素字模，保证 SM 在正常体型的脸上仍完整可辨。 */
    {
        static const char *letters[2][7] = {
            { "11111", "10000", "10000", "11111", "00001", "00001", "11111" },
            { "10001", "11011", "10101", "10101", "10001", "10001", "10001" }
        };
        int letter, row, col;
        for (letter = 0; letter < 2; letter++)
            for (row = 0; row < 7; row++)
                for (col = 0; col < 5; col++)
                    if (letters[letter][row][col] == '1') {
                        /* 面部平面从外侧看会发生镜像，反向布置列才能让
                         * 玩家看到正常顺序的 S 和 M。 */
                        int h0 = -145 + (letter * 6 + col) * 24;
                        pixels += draw_face_rect(renderer, camera, x, z, 165,
                                                 e->dir_x, e->dir_z,
                                                 h0, h0 + 17,
                                                 enemy_y(170 + (6 - row) * 38, scale),
                                                 enemy_y(200 + (6 - row) * 38, scale),
                                                 0xFFD070);
                    }
    }
    return pixels;
}

static int render_smoker_tongue(struct toy_renderer *renderer,
                                const struct camera *camera,
                                const struct toy_game_enemy *e)
{
    int target_x, target_z, target_lift = 0, pixels;
    if (e->special_target_player == 1) {
        target_x = game.secondary_px;
        target_z = game.secondary_pz;
        target_lift = game.secondary_player_airborne_y;
    } else if (e->special_target_player == 2 &&
               e->special_target_actor_index >= 0 &&
               e->special_target_actor_index < TOY_GAME_MAX_ACTORS &&
               game.actors[e->special_target_actor_index].active) {
        const struct toy_game_actor *actor =
            &game.actors[e->special_target_actor_index];
        target_x = actor->x;
        target_z = actor->z;
        target_lift = actor->airborne_y;
    } else {
        target_x = game.px;
        target_z = game.pz;
        target_lift = game.player_airborne_y;
    }
    pixels = draw_tongue_segment(renderer, camera, e->x, 270, e->z,
                                 target_x, -360 + target_lift, target_z);
    /* 两个水平束缚圈表现舌头在目标身上的缠绕。 */
    pixels += draw_cylinder(renderer, camera, target_x, target_z, 225,
                            -430 + target_lift, -395 + target_lift, 0xB98B62);
    pixels += draw_cylinder(renderer, camera, target_x, target_z, 205,
                            -280 + target_lift, -245 + target_lift, 0xB98B62);
    return pixels;
}

static void render_enemy_alert(struct toy_renderer *renderer,
                               const struct camera *camera,
                               const struct toy_game_enemy *e, int scale)
{
    struct vec3 world, view;
    struct toy_screen_vertex screen;
    int x, y;
    /* 追踪尸潮也常显红色惊叹号：它们已知晓玩家位置，无需侦测。 */
    if (e->ai_state != TOY_GAME_ENEMY_NOTICE &&
        e->ai_state != TOY_GAME_ENEMY_ALERT &&
        e->ai_state != TOY_GAME_ENEMY_TRACKING) return;
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
static int render_blob_shadow(struct toy_renderer *renderer,
                              const struct camera *camera,
                              const struct toy_game_enemy *e, int scale)
{
    struct vec3 a, b, c, d;
    int rx = 230 * scale / 1000;
    int rz = 150 * scale / 1000;
    int y = -886; /* just above the baked floor */
    if (rx < 18) rx = 18;
    if (rz < 12) rz = 12;
    a.x = e->x - rx; a.y = y; a.z = e->z - rz;
    b.x = e->x + rx; b.y = y; b.z = e->z - rz;
    c.x = e->x + rx; c.y = y; c.z = e->z + rz;
    d.x = e->x - rx; d.y = y; d.z = e->z + rz;
    /* Two nested, opaque low-alpha-style tones approximate a soft penumbra. */
    return draw_quad(renderer, camera, &a, &b, &c, &d, 0x17151A);
}

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
        if (view.z > ENEMY_RENDER_DISTANCE) continue;
        if (e->active == 2) {
            scale = e->dying_ms * 1000 / TOY_GAME_DYING_MS;
            color = 0x5A1A1A;
        } else {
            const struct toy_game_enemy_info *info = toy_game_enemy_info(e->type);
            color = info->color;
            if (e->type == TOY_GAME_ENEMY_HEAVY ||
                e->type == TOY_GAME_ENEMY_PURSUIT_HEAVY) scale = 1350;
            if (e->type == TOY_GAME_ENEMY_SMOKER) color = 0x76513A;
            if (e->type == TOY_GAME_ENEMY_CHARGER) {
                color = e->charge_active ? 0xB06A36 : 0x8B5A35;
                scale = 1180;
            }
            if (e->type == TOY_GAME_ENEMY_SMOKER) scale = 1000;
            if (e->hurt > 0) color = 0xBB3333;
            else if (e->flash > 0) color = 0xDFDFDF;
            else if (e->type == TOY_GAME_ENEMY_PURSUIT_HEAVY)
                color = 0x7A4A2A;
            else if (e->type == TOY_GAME_ENEMY_PURSUIT_FAST)
                color = 0xB84A32;
            else if (e->ai_state == TOY_GAME_ENEMY_TRACKING)
                color = 0x8A2A2A;   /* 尸潮追踪者：红色，一眼可辨 */
        }
        pixels += render_blob_shadow(renderer, camera, e, scale);
        if (e->type == TOY_GAME_ENEMY_SMOKER && e->special_target_active)
            pixels += render_smoker_tongue(renderer, camera, e);
        active_enemy_lift = e->airborne_y;
        if (e->type == TOY_GAME_ENEMY_CHARGER)
            pixels += render_charger_enemy(renderer, camera, e, scale, color);
        else if (e->type == TOY_GAME_ENEMY_SMOKER)
            pixels += render_smoker_enemy(renderer, camera, e, scale, color);
        else if (e->type == TOY_GAME_ENEMY_HEAVY ||
            e->type == TOY_GAME_ENEMY_PURSUIT_HEAVY || (i & 1) == 0)
            pixels += render_block_enemy(renderer, camera, e, scale, color);
        else
            pixels += render_round_enemy(renderer, camera, e, scale, color);
        active_enemy_lift = 0;
        render_enemy_alert(renderer, camera, e, scale);
    }
    return pixels;
}

static void render_actor_name(struct toy_renderer *renderer,
                              const struct camera *camera, int x, int z,
                              int y, const char *name, uint32_t color)
{
    struct vec3 world, view;
    struct toy_screen_vertex screen;
    int width;
    if (!name || !*name) return;
    world.x = x; world.y = y; world.z = z;
    world_to_view(camera, &world, &view);
    if (view.z < NEAR_Z || view.z > ENEMY_RENDER_DISTANCE) return;
    project_vertex(&renderer->surface, &view, &screen);
    width = (int)strlen(name) * FB_FONT_W;
    screen.x -= width / 2;
    if (screen.x < 0 || screen.x + width >= renderer->surface.width ||
        screen.y < 0 || screen.y + FB_FONT_H >= renderer->surface.height) return;
    fb_draw_string((unsigned char *)renderer->surface.pixels, screen.x,
                   screen.y, name, color, renderer->surface.stride);
}

static void render_actor_status(struct toy_renderer *renderer,
                                const struct camera *camera, int x, int z,
                                int y, const char *name, int hp, int max_hp, int downed,
                                int revive_ms, uint32_t name_color)
{
    struct vec3 world, view;
    struct toy_screen_vertex screen;
    int width, bar_x, bar_y, fill;
    uint32_t hp_color;
    (void)revive_ms;
    world.x = x; world.y = y; world.z = z;
    world_to_view(camera, &world, &view);
    if (view.z < NEAR_Z || view.z > ENEMY_RENDER_DISTANCE) return;
    project_vertex(&renderer->surface, &view, &screen);
    width = (int)strlen(name) * FB_FONT_W;
    screen.x -= width / 2;
    if (screen.x < 0 || screen.x + width >= renderer->surface.width ||
        screen.y < 0 || screen.y + FB_FONT_H >= renderer->surface.height) return;
    render_actor_name(renderer, camera, x, z, y, name, name_color);
    bar_x = screen.x + (width - 64) / 2;
    bar_y = screen.y + FB_FONT_H + 2;
    if (bar_x < 2) bar_x = 2;
    if (bar_x + 64 >= renderer->surface.width) bar_x = renderer->surface.width - 66;
    fill_rect(&renderer->surface, bar_x - 2, bar_y - 2, 68, 7, 0x20252B);
    if (hp < 10) hp_color = 0xF03030;
    else if (hp < 40) hp_color = 0xF0C830;
    else hp_color = 0x40D060;
    if (max_hp <= 0) max_hp = 100;
    fill = hp * 64 / max_hp;
    if (fill < 0) fill = 0;
    if (fill > 64) fill = 64;
    if (fill > 0) fill_rect(&renderer->surface, bar_x, bar_y, fill, 3, hp_color);
    (void)downed;
}

static int render_player_avatar(struct toy_renderer *renderer,
                                const struct camera *camera, int x, int z,
                                int sy, int cy, int muzzle_flash,
                                uint32_t body_color, int downed);

static void render_ai_teammate_name(struct toy_renderer *renderer,
                                    const struct camera *camera)
{
    int i;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
        const struct toy_game_actor *actor = &game.actors[i];
        long dx, dz, d2, dist, dot;
        uint32_t color = actor->class_id == TOY_GAME_AI_LEVEL_3 ? 0xFFD060 :
                         actor->class_id == TOY_GAME_AI_LEVEL_2 ? 0x80E080 :
                         0x70D8FF;
        if (!actor->active || actor->kind != TOY_GAME_ACTOR_AI) continue;
        dx = (long)actor->x - camera->x;
        dz = (long)actor->z - camera->z;
        d2 = dx * dx + dz * dz;
        if (d2 > 10800L * 10800L) continue;
        dist = isqrt(d2);
        if (dist <= 0) continue;
        dot = dx * camera->sy + dz * camera->cy;
        if (dot < dist * 650) continue;
        render_actor_status(renderer, camera, actor->x, actor->z,
                            actor->state == TOY_GAME_ACTOR_DOWNED ? -350 : 700,
                            actor->name, actor->hp, actor->max_hp,
                            actor->state == TOY_GAME_ACTOR_DOWNED,
                            actor->revive_progress_ms, color);
    }
}

static int render_ai_teammate(struct toy_renderer *renderer,
                              const struct camera *camera)
{
    int i, pixels = 0;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
        const struct toy_game_actor *actor = &game.actors[i];
        struct vec3 center, view;
        uint32_t color;
        if (!actor->active || actor->kind != TOY_GAME_ACTOR_AI) continue;
        center.x = actor->x; center.y = 0; center.z = actor->z;
        world_to_view(camera, &center, &view);
        if (view.z < NEAR_Z || view.z > ENEMY_RENDER_DISTANCE) continue;
        color = actor->class_id == TOY_GAME_AI_LEVEL_3 ? 0x252A30 :
                actor->class_id == TOY_GAME_AI_LEVEL_2 ? 0x386B96 :
                0x596B3A;
        active_actor_lift = actor->airborne_y;
        pixels += render_player_avatar(renderer, camera, actor->x, actor->z,
                                       actor->sy, actor->cy,
                                       actor->muzzle_flash_ms, color,
                                       actor->state == TOY_GAME_ACTOR_DOWNED);
        active_actor_lift = 0;
    }
    return pixels;
}

static int render_player_avatar(struct toy_renderer *renderer,
                                const struct camera *camera, int x, int z,
                                int sy, int cy, int muzzle_flash,
                                uint32_t body_color, int downed)
{
    int pixels = 0, face_y0, face_y1;
    if (!renderer || !camera) return 0;
    if (downed) {
        pixels += draw_cuboid(renderer, camera, x - 170, x + 170,
                              -850 + active_actor_lift, -650 + active_actor_lift,
                              z - 100, z + 100, body_color);
        pixels += draw_ellipsoid_head(renderer, camera, x, z,
                                      -550 + active_actor_lift, 145, 100, 0xD2A878);
        face_y0 = -650; face_y1 = -470;
    } else {
        pixels += draw_cuboid(renderer, camera, x - 95, x - 10,
                              -900 + active_actor_lift, -610 + active_actor_lift,
                              z - 75, z + 75, 0x25354A);
        pixels += draw_cuboid(renderer, camera, x + 10, x + 95,
                              -900 + active_actor_lift, -610 + active_actor_lift,
                              z - 75, z + 75, 0x25354A);
        pixels += draw_cuboid(renderer, camera, x - 155, x + 155,
                              -620 + active_actor_lift, -100 + active_actor_lift,
                              z - 100, z + 100, body_color);
        pixels += draw_ellipsoid_head(renderer, camera, x, z,
                                      50 + active_actor_lift, 145, 150, 0xD2A878);
        face_y0 = -35; face_y1 = 185;
    }
    pixels += draw_face_rect(renderer, camera, x, z, 145, sy, cy,
                             -72, 72, face_y0 + active_actor_lift,
                             face_y1 + active_actor_lift, 0x252A30);
    pixels += draw_face_rect(renderer, camera, x, z, 145, sy, cy,
                             -16, 16, face_y0 + 40 + active_actor_lift,
                             face_y1 - 40 + active_actor_lift, 0xE8D2A8);
    pixels += draw_face_rect(renderer, camera, x, z, 145, sy, cy,
                             -72, 72, face_y0 + 90 + active_actor_lift,
                             face_y0 + 115 + active_actor_lift, 0xE8D2A8);
    if (muzzle_flash > 0)
        pixels += draw_cuboid(renderer, camera, x - 45, x + 45,
                              -560 + active_actor_lift, -430 + active_actor_lift,
                              z - 120, z + 120, 0xFFD060);
    return pixels;
}

static int render_network_teammate(struct toy_renderer *renderer,
                                   const struct camera *camera,
                                   const struct rasterfall_net *net)
{
    const struct camera *remote = NULL;
    int muzzle_flash = 0;
    int downed = 0;
    if (net->mode == RASTERFALL_NET_HOST && net->peer_known) {
        remote = &net->peer_camera;
        muzzle_flash = net->peer_muzzle_flash_ms;
        downed = net->peer_down;
    } else if (net->mode == RASTERFALL_NET_CLIENT && net->players[0].active) {
        remote = &net->players[0].camera;
        muzzle_flash = net->players[0].muzzle_flash_ms;
        downed = net->players[0].downed;
    }
    if (!remote) return 0;
    return render_player_avatar(renderer, camera, remote->x, remote->z,
                                remote->sy, remote->cy, muzzle_flash,
                                0x386B96, downed);
}

static void render_network_teammate_status(struct toy_renderer *renderer,
                                           const struct camera *camera,
                                           const struct rasterfall_net *net)
{
    const struct camera *remote = NULL;
    int hp = 0;
    int downed = 0, revive_ms = 0;
    if (net->mode == RASTERFALL_NET_HOST && net->peer_known) {
        remote = &net->peer_camera;
        hp = net->peer_hp;
        downed = net->peer_down;
        revive_ms = net->peer_revive_progress_ms;
    } else if (net->mode == RASTERFALL_NET_CLIENT && net->players[0].active) {
        remote = &net->players[0].camera;
        hp = net->players[0].hp;
        downed = net->players[0].downed;
        revive_ms = net->players[0].revive_progress_ms;
    }
    if (!remote) return;
    render_actor_status(renderer, camera, remote->x, remote->z, 700,
                        "PLAYER 2", hp, 100, downed, revive_ms, 0x70D8FF);
}

/* ── 子弹轨迹与命中粒子（纯视觉；逻辑步进 16ms 推进） ──────────── */

static void draw_effect_line(struct toy_surface *surface,
                             int x0, int y0, int x1, int y1,
                             uint32_t color)
{
    int bound_x = surface->width * 4;
    int bound_y = surface->height * 4;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int steps = 0;
    int max_steps = (surface->width + surface->height) * 4;
    x0 = clampi(x0, -bound_x, bound_x);
    x1 = clampi(x1, -bound_x, bound_x);
    y0 = clampi(y0, -bound_y, bound_y);
    y1 = clampi(y1, -bound_y, bound_y);
    dx = x1 > x0 ? x1 - x0 : x0 - x1;
    dy = y1 > y0 ? y0 - y1 : y1 - y0;
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    err = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < surface->width &&
            y0 >= 0 && y0 < surface->height)
            put_pixel(surface, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        if (++steps > max_steps) break;
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
    for (i = 0; i < RASTERFALL_TRACER_SLOTS; i++) {
        const struct rasterfall_tracer *t = &effects.tracers[i];
        uint32_t color;
        if (!t->active) continue;
        color = mix_color(0xFFE060, 0x3A2C14,
                          t->life_ms * 256 / RASTERFALL_TRACER_LIFE_MS, 256);
        draw_tracer_line(&renderer->surface, camera, t->sx, t->sy, t->sz,
                         t->ex, t->ey, t->ez, color);
        pixels++;
    }
    return pixels;
}

static int render_particles(struct toy_renderer *renderer, const struct camera *camera)
{
    int i, pixels = 0;
    for (i = 0; i < RASTERFALL_PARTICLE_SLOTS; i++) {
        const struct rasterfall_particle *p = &effects.particles[i];
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
        k = p->life_ms * 256 / RASTERFALL_PARTICLE_LIFE_MS;
        if (k > 256) k = 256;   /* 寿命随机 +40ms 可能超出基准，钳制插值比例 */
        fill_rect(&renderer->surface, screen.x, screen.y, 2, 2,
                  mix_color(0xFFC860, 0x4A2008, k, 256));
        pixels++;
    }
    return pixels;
}

#undef effects
#undef textures_enabled
#undef fixed_floor_lighting
void rasterfall_render_bind(struct rasterfall_render_context *ctx)
{
    render_ctx = ctx;
    active_session = ctx->session;
    active_effects = ctx->effects;
    active_wall_texture = ctx->wall_texture;
    active_lightmap = ctx->lightmap;
    active_textures = ctx->textures_enabled;
    active_fixed_floor_lighting = ctx->fixed_floor_lighting;
}

void rasterfall_render_bake_lightmap(void)
{
    bake_static_lightmap();
}

int rasterfall_render_scene(struct toy_renderer *renderer,
                            const struct camera *camera)
{
    return render_scene(renderer, camera);
}

int rasterfall_render_interactables(struct toy_renderer *renderer,
                                    const struct camera *camera)
{
    return render_interactables(renderer, camera);
}

int rasterfall_render_enemies(struct toy_renderer *renderer,
                              const struct camera *camera)
{
    return render_enemies(renderer, camera);
}

int rasterfall_render_ai_teammate(struct toy_renderer *renderer,
                                  const struct camera *camera)
{
    return render_ai_teammate(renderer, camera);
}

int rasterfall_render_network_teammate(struct toy_renderer *renderer,
                                       const struct camera *camera,
                                       const struct rasterfall_net *net)
{
    return render_network_teammate(renderer, camera, net);
}

void rasterfall_render_ai_teammate_name(struct toy_renderer *renderer,
                                        const struct camera *camera)
{
    render_ai_teammate_name(renderer, camera);
}

void rasterfall_render_network_teammate_status(
    struct toy_renderer *renderer, const struct camera *camera,
    const struct rasterfall_net *net)
{
    render_network_teammate_status(renderer, camera, net);
}

int rasterfall_render_tracers(struct toy_renderer *renderer,
                              const struct camera *camera)
{
    return render_tracers(renderer, camera);
}

int rasterfall_render_particles(struct toy_renderer *renderer,
                                const struct camera *camera)
{
    return render_particles(renderer, camera);
}
