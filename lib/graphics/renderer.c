/*
 * Small software renderer shared by Toyc windowed games.
 *
 * Rasterization is deferred into a command list, then split across a worker
 * pool by horizontal scanline bands.  Each worker owns disjoint rows, so
 * pixel and depth writes need no locking; per-pixel math is identical to a
 * plain single-threaded rasterizer.  Synchronization uses volatile flags +
 * futex (the same pattern as the audio thread); there is no mutex/cond in
 * tlibc.  If worker creation fails the renderer degrades to inline
 * single-threaded rasterization.
 */

#include "toy_renderer.h"
#include "core.h"
#include "string.h"
#include "pthread.h"
#include "tlibc_compat.h"
#include "atomic.h"
#if defined(TOYC_WINDOWS)
#include <windows.h>
#endif

#define TOY_UV_ONE 65536L
#define TOY_INV_Z_SCALE 1048576L
#define TOY_RENDER_MAX_WORKERS 8
#define TOY_RENDER_CMD_INIT 4096
/* A detailed PMX character can submit roughly 25k body triangles plus its
 * outline shell after the world has already populated the deferred list.
 * Near-plane clipping can also split one input triangle into two commands.
 * Keep the existing grow-on-demand policy, but leave enough headroom so a
 * late material is not silently truncated while its earlier Edge survives. */
/* Detailed PMX characters routinely submit more than 65k visible body/edge
 * triangles.  Keep the small initial allocation, but allow the command pool
 * to grow far enough for the developer character lineup without truncation. */
#define TOY_RENDER_CMD_MAX 524288
#define TOY_FUTEX_WAIT 0
#define TOY_FUTEX_WAKE 1
#define TOY_MATERIAL_TOON   1
#define TOY_MATERIAL_SPHERE 2

struct toy_raster_sampler {
    const unsigned char *data;
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    int valid;
};

static long long edge(const struct toy_screen_vertex *a,
                 const struct toy_screen_vertex *b, int px, int py)
{
    return ((long long)px - a->x) * ((long long)b->y - a->y) -
           ((long long)py - a->y) * ((long long)b->x - a->x);
}

static int clampi(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

/* tlibc 的 __clock_gettime 是裸系统调用（无 vdso），逐命令计时会显著
 * 污染测量本身，因此路径耗时只按“路径段切换”取钟：场景按类型成组提交，
 * 每帧仅数次调用。 */
static long renderer_monotonic_us(void)
{
    struct timespec now;
    if (__clock_gettime(CLOCK_MONOTONIC, &now) < 0) return 0;
    return now.tv_sec * 1000000L + now.tv_nsec / 1000;
}

static long renderer_thread_cpu_us(void)
{
    struct timespec now;
    if (__clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) < 0) return 0;
    return now.tv_sec * 1000000L + now.tv_nsec / 1000;
}

/* 收拢当前路径段的计时（worker_rasterize 与单线程降级路径在命令循环
 * 结束后调用）。 */
static void close_runs(struct toy_render_worker *worker)
{
    if (worker->last_path >= 0) {
        long dt = renderer_monotonic_us() - worker->path_start;
        if (worker->last_path) worker->tex_us += dt;
        else worker->flat_us += dt;
        worker->last_path = -1;
    }
}

void toy_renderer_init(struct toy_renderer *renderer)
{
    if (renderer) memset(renderer, 0, sizeof(struct toy_renderer));
}

/* ── 条带化逐像素光栅化（y 范围由调用方给定，数学与单线程版完全一致） ── */

static long raster_flat(struct toy_renderer *renderer,
                        struct toy_render_worker *worker,
                        const struct toy_screen_vertex *a,
                        const struct toy_screen_vertex *b,
                        const struct toy_screen_vertex *c,
                        long long area, int minx, int maxx,
                        int y0, int y1, uint32_t color, int overlay)
{
    struct toy_surface *surface = &renderer->surface;
    int *depth = renderer->depth;
    int width = surface->width;
    int y, x, drawn = 0;
    unsigned long inside = 0;
    /* 增量边函数：E(x+1,y)=E(x,y)+dEx、E(x,y+1)=E(x,y)+dEy，每像素
     * 由 3 次完整边函数（-O0 下是 3 次调用 + 6 次乘法）退化为 3 次加法；
     * 行首边值只按行增量更新。整数加法与逐像素重算完全一致。 */
    long dEx0 = c->y - b->y, dEx1 = a->y - c->y, dEx2 = b->y - a->y;
    long dEy0 = b->x - c->x, dEy1 = c->x - a->x, dEy2 = a->x - b->x;
    long w0 = edge(b, c, minx, y0);
    long w1 = edge(c, a, minx, y0);
    long w2 = edge(a, b, minx, y0);
    for (y = y0; y <= y1; y++) {
        uint32_t *row = (uint32_t *)((unsigned char *)surface->pixels +
                                     y * surface->stride);
        int base = y * width;
        long e0 = w0, e1 = w1, e2 = w2;
        for (x = minx; x <= maxx; x++) {
            if (e0 <= 0 && e1 <= 0 && e2 <= 0) {
                inside++;
                /* 逆深度（1/z）在屏幕空间精确线性插值；仿射插值 z 在
                 * 掠射角下有显著误差，会让近共面（墙脚与地板）判错深度。
                 * 除 area 归一化到 ≤ max(inv_z)，防止大三角形（近平
                 * 面裁剪产生的巨屏外三角形）加权和在 int 截断时溢出。 */
                long long inv = ((long long)e0 * a->inv_z +
                                 (long long)e1 * b->inv_z +
                                 (long long)e2 * c->inv_z) / area;
                int at = base + x;
                /* 覆盖层不做深度比较与深度写入；深度保持底层值，
                 * 后画的更近面仍能正常通过测试。 */
                /* 平局（≥）判给后画者：共面/共边像素深度相等（墙脚与地板、
                 * 盒体相邻棱边），先画者赢会在棱边透出隐藏面颜色；后画者
                 * 赢与"可见面在后、按绘制顺序叠加"的意图一致。调用方顶点
                 * 交换必须携带 inv_z，否则深度插值错配。 */
                if (overlay || inv >= depth[at]) {
                    worker->depth_pass_px++;
                    worker->shaded_px++;
                    if (!overlay) depth[at] = (int)inv;
                    row[x] = color;
                    worker->written_px++;
                    worker->flat_pixels++;
                    drawn++;
                }
            }
            e0 += dEx0; e1 += dEx1; e2 += dEx2;
        }
        w0 += dEy0; w1 += dEy1; w2 += dEy2;
    }
    worker->inside_px += inside;
    return drawn;
}

static int texture_valid(const struct toy_texture_view *t)
{
    unsigned long pixels, channels;
    if (!t || !t->data || !t->width || !t->height) return 0;
    if (t->width > 8192 || t->height > 8192) return 0;
    pixels = (unsigned long)t->width * t->height;
    channels = t->channels ? t->channels : 3;
    return (channels == 3 || channels == 4) &&
           pixels <= 0xffffffffUL / channels &&
           t->data_size == pixels * channels;
}

static uint32_t texture_sample(const struct toy_texture_view *t,
                               long u, long v, int repeat,
                               uint32_t fallback, int *used_fallback)
{
    long x, y, at;
    const unsigned char *p;
    if (!texture_valid(t)) {
        long cx = (u / TOY_UV_ONE) & 1;
        long cy = (v / TOY_UV_ONE) & 1;
        *used_fallback = 1;
        return ((cx ^ cy) ? 0xFFFF00FFU : fallback);
    }
    /* UVs are expressed in tile units: 1.0 spans the whole image, so the
     * address period is one Q16 tile, independent of pixel dimensions. */
    /* Rasterfall 的贴图坐标以一个 Q16 tile 为周期。用位掩码完成重复
     * 归一化，避免自托管版在极端透视插值下执行长整型取模；非重复路径
     * 仍明确限制到合法的一个 tile。最终 u/v 始终落在 [0, 65535]。 */
    if (repeat) {
        u &= TOY_UV_ONE - 1;
        v &= TOY_UV_ONE - 1;
    } else {
        if (u < 0) u = 0;
        if (v < 0) v = 0;
        if (u >= TOY_UV_ONE) u = TOY_UV_ONE - 1;
        if (v >= TOY_UV_ONE) v = TOY_UV_ONE - 1;
    }
    x = (u * t->width) / TOY_UV_ONE;
    y = (v * t->height) / TOY_UV_ONE;
    {
    long channels = t->channels ? t->channels : 3;
    at = (y * t->width + x) * channels;
    p = t->data + at;
    return (channels == 4 ? (uint32_t)p[3] << 24 : 0xFF000000U) |
           ((uint32_t)p[0] << 16) |
           ((uint32_t)p[1] << 8) | p[2];
    }
}

/* Benchmark-only addressing specialization: callers guarantee a valid TTEX
 * view, so this isolates validation/fallback and generic call overhead while
 * retaining the same Q16 clamp/wrap and texel selection. */
static uint32_t texture_sample_simple(const struct toy_texture_view *t,
                                      long u, long v, int repeat)
{
    long x, y, at, channels = t->channels ? t->channels : 3;
    const unsigned char *p;
    if (repeat) {
        u &= TOY_UV_ONE - 1;
        v &= TOY_UV_ONE - 1;
    } else {
        if (u < 0) u = 0;
        if (v < 0) v = 0;
        if (u >= TOY_UV_ONE) u = TOY_UV_ONE - 1;
        if (v >= TOY_UV_ONE) v = TOY_UV_ONE - 1;
    }
    x = (u * t->width) / TOY_UV_ONE;
    y = (v * t->height) / TOY_UV_ONE;
    at = (y * t->width + x) * channels;
    p = t->data + at;
    return (channels == 4 ? (uint32_t)p[3] << 24 : 0xFF000000U) |
           ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static void prepare_sampler(struct toy_raster_sampler *out,
                            const struct toy_texture_view *texture,
                            int valid)
{
    out->data = texture ? texture->data : 0;
    out->width = texture ? texture->width : 0;
    out->height = texture ? texture->height : 0;
    out->channels = texture && texture->channels ? texture->channels : 3;
    out->valid = valid;
}

static uint32_t texture_sample_prepared(const struct toy_raster_sampler *s,
                                        long u, long v, int repeat,
                                        uint32_t fallback,
                                        int *used_fallback)
{
    long x, y, at;
    const unsigned char *p;
    if (!s->valid) {
        long cx = (u / TOY_UV_ONE) & 1;
        long cy = (v / TOY_UV_ONE) & 1;
        *used_fallback = 1;
        return (cx ^ cy) ? 0xFFFF00FFU : fallback;
    }
    if (repeat) {
        u &= TOY_UV_ONE - 1;
        v &= TOY_UV_ONE - 1;
    } else {
        if (u < 0) u = 0;
        if (v < 0) v = 0;
        if (u >= TOY_UV_ONE) u = TOY_UV_ONE - 1;
        if (v >= TOY_UV_ONE) v = TOY_UV_ONE - 1;
    }
    x = (u * s->width) / TOY_UV_ONE;
    y = (v * s->height) / TOY_UV_ONE;
    at = (y * s->width + x) * s->channels;
    p = s->data + at;
    return (s->channels == 4 ? (uint32_t)p[3] << 24 : 0xFF000000U) |
           ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static uint32_t shade_color(uint32_t color, int light, int fog)
{
    int r = (int)((color >> 16) & 255), g = (int)((color >> 8) & 255);
    int b = (int)(color & 255);
    int fr = 28, fg = 33, fb = 40;
    if (light < 0) light = 0;
    if (light > 384) light = 384;
    r = r * light / 256; g = g * light / 256; b = b * light / 256;
    /* Overbright materials (for example Bomb's emissive warning light) may
     * legitimately use a light factor above 256.  Clamp each channel before
     * packing it; otherwise values above 255 spill into adjacent channels
     * and turn bright red into a darker magenta. */
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    if (fog < 0) fog = 0;
    if (fog > 256) fog = 256;
    r = (r * (256 - fog) + fr * fog) / 256;
    g = (g * (256 - fog) + fg * fog) / 256;
    b = (b * (256 - fog) + fb * fog) / 256;
    return (uint32_t)(r << 16 | g << 8 | b);
}

static long raster_tex(struct toy_renderer *renderer,
                       struct toy_render_worker *worker,
                       const struct toy_screen_vertex *a,
                       const struct toy_screen_vertex *b,
                       const struct toy_screen_vertex *c,
                       long long area, int minx, int maxx,
                       int y0, int y1,
                       const struct toy_texture_view *texture,
                       const struct toy_texture_view *texture2,
                       int blend_mode,
                       int has_toon, uint32_t toon_multiplier,
                       int material_features,
                       int base_texture_valid, int sphere_texture_valid,
                       int material_alpha,
                       uint32_t material_ambient,
                       uint32_t material_specular,
                       int material_specular_level,
                       int repeat, uint32_t fallback_color,
                       int light_factor, int fog_factor,
                       unsigned long *tex_pixels,
                       unsigned long *fallback_pixels)
{
    struct toy_surface *surface = &renderer->surface;
    int *depth = renderer->depth;
    int width = surface->width;
    int y, x, drawn = 0;
    int diagnostic = renderer->texture_diagnostic_flags;
    int has_sphere = (material_features & TOY_MATERIAL_SPHERE) != 0;
    struct toy_raster_sampler base_sampler, sphere_sampler;
    prepare_sampler(&base_sampler, texture, base_texture_valid);
    prepare_sampler(&sphere_sampler, has_sphere ? texture2 : 0,
                    sphere_texture_valid);
    unsigned long inside = 0;
    /* 与 raster_flat 相同的增量边函数（行首边值按行增量更新）。 */
    long dEx0 = c->y - b->y, dEx1 = a->y - c->y, dEx2 = b->y - a->y;
    long dEy0 = b->x - c->x, dEy1 = c->x - a->x, dEy2 = a->x - b->x;
    long w0 = edge(b, c, minx, y0);
    long w1 = edge(c, a, minx, y0);
    long w2 = edge(a, b, minx, y0);
    long affine_u_row = 0, affine_v_row = 0;
    long affine_u2_row = 0, affine_v2_row = 0;
    long affine_du_dx = 0, affine_dv_dx = 0;
    long affine_du2_dx = 0, affine_dv2_dx = 0;
    long affine_du_dy = 0, affine_dv_dy = 0;
    long affine_du2_dy = 0, affine_dv2_dy = 0;
    if (diagnostic & TOY_RENDER_DIAG_AFFINE_UV) {
        affine_u_row = (long)(((long long)w0 * a->u +
                               (long long)w1 * b->u +
                               (long long)w2 * c->u) / area);
        affine_v_row = (long)(((long long)w0 * a->v +
                               (long long)w1 * b->v +
                               (long long)w2 * c->v) / area);
        if (has_sphere) affine_u2_row = (long)(((long long)w0 * a->u2 +
                                (long long)w1 * b->u2 +
                                (long long)w2 * c->u2) / area);
        if (has_sphere) affine_v2_row = (long)(((long long)w0 * a->v2 +
                                (long long)w1 * b->v2 +
                                (long long)w2 * c->v2) / area);
        affine_du_dx = (long)(((long long)dEx0 * a->u +
                               (long long)dEx1 * b->u +
                               (long long)dEx2 * c->u) / area);
        affine_dv_dx = (long)(((long long)dEx0 * a->v +
                               (long long)dEx1 * b->v +
                               (long long)dEx2 * c->v) / area);
        if (has_sphere) affine_du2_dx = (long)(((long long)dEx0 * a->u2 +
                                (long long)dEx1 * b->u2 +
                                (long long)dEx2 * c->u2) / area);
        if (has_sphere) affine_dv2_dx = (long)(((long long)dEx0 * a->v2 +
                                (long long)dEx1 * b->v2 +
                                (long long)dEx2 * c->v2) / area);
        affine_du_dy = (long)(((long long)dEy0 * a->u +
                               (long long)dEy1 * b->u +
                               (long long)dEy2 * c->u) / area);
        affine_dv_dy = (long)(((long long)dEy0 * a->v +
                               (long long)dEy1 * b->v +
                               (long long)dEy2 * c->v) / area);
        if (has_sphere) affine_du2_dy = (long)(((long long)dEy0 * a->u2 +
                                (long long)dEy1 * b->u2 +
                                (long long)dEy2 * c->u2) / area);
        if (has_sphere) affine_dv2_dy = (long)(((long long)dEy0 * a->v2 +
                                (long long)dEy1 * b->v2 +
                                (long long)dEy2 * c->v2) / area);
    }
    for (y = y0; y <= y1; y++) {
        uint32_t *row = (uint32_t *)((unsigned char *)surface->pixels +
                                     y * surface->stride);
        int base = y * width;
        long e0 = w0, e1 = w1, e2 = w2;
        long affine_u = affine_u_row, affine_v = affine_v_row;
        long affine_u2 = affine_u2_row, affine_v2 = affine_v2_row;
        for (x = minx; x <= maxx; x++) {
            if (e0 <= 0 && e1 <= 0 && e2 <= 0) {
                inside++;
                worker->depth_divisions++;
                /* 与 flat 路径相同的逆深度比较（透视校正，无仿射误差）；
                 * inv 未归一化仅用于 UV 比值，深度用归一化后的 inv_norm
                 * 避免大三角形加权和溢出 int 截断。 */
                long long inv64 = (long long)e0 * a->inv_z +
                                  (long long)e1 * b->inv_z +
                                  (long long)e2 * c->inv_z;
                long long inv_norm = inv64 / area;
                int at = base + x;
                /* 与 flat 路径相同：平局（≥）判给后画者。 */
                if (inv_norm >= depth[at]) {
                    int path = material_features &
                               (TOY_MATERIAL_TOON | TOY_MATERIAL_SPHERE);
                    unsigned long pixel_divisions = 0;
                    worker->depth_pass_px++;
                    worker->material_path_pixels[path]++;
                    long long uoz = 0, voz = 0, u2oz = 0, v2oz = 0;
                    if (!(diagnostic & TOY_RENDER_DIAG_AFFINE_UV)) {
                        uoz = (long long)e0 * a->u_over_z +
                              (long long)e1 * b->u_over_z +
                              (long long)e2 * c->u_over_z;
                        voz = (long long)e0 * a->v_over_z +
                              (long long)e1 * b->v_over_z +
                              (long long)e2 * c->v_over_z;
                        if (has_sphere) {
                            u2oz = (long long)e0 * a->u2_over_z +
                                   (long long)e1 * b->u2_over_z +
                                   (long long)e2 * c->u2_over_z;
                            v2oz = (long long)e0 * a->v2_over_z +
                                   (long long)e1 * b->v2_over_z +
                                   (long long)e2 * c->v2_over_z;
                        }
                    }
                    int used_fallback = 0;
                    long u = diagnostic & TOY_RENDER_DIAG_AFFINE_UV ?
                             affine_u : inv64 ? (long)(uoz / inv64) : 0;
                    long v = diagnostic & TOY_RENDER_DIAG_AFFINE_UV ?
                             affine_v : inv64 ? (long)(voz / inv64) : 0;
                    if (!(diagnostic & TOY_RENDER_DIAG_AFFINE_UV))
                    {
                        pixel_divisions += 2;
                        worker->base_perspective_divisions += 2;
                    }
                    pixel_divisions += 2; /* base Q16 address conversion */
                    worker->texture_address_divisions += 2;
                    uint32_t color;
                    if ((diagnostic & TOY_RENDER_DIAG_SIMPLE_ADDRESS) &&
                        texture_valid(texture))
                        color = texture_sample_simple(texture, u, v, repeat);
                    else
                        color = texture_sample_prepared(
                            &base_sampler, u, v, repeat, fallback_color,
                            &used_fallback);
                    if (has_sphere) {
                        int sphere_fallback = 0;
                        long u2, v2;
                        u2 = diagnostic & TOY_RENDER_DIAG_AFFINE_UV ?
                             affine_u2 : inv64 ? (long)(u2oz / inv64) : 0;
                        v2 = diagnostic & TOY_RENDER_DIAG_AFFINE_UV ?
                             affine_v2 : inv64 ? (long)(v2oz / inv64) : 0;
                        if (!(diagnostic & TOY_RENDER_DIAG_AFFINE_UV))
                        {
                            pixel_divisions += 2;
                            worker->sphere_perspective_divisions += 2;
                        }
                        pixel_divisions += 2; /* sphere address conversion */
                        worker->texture_address_divisions += 2;
                        uint32_t sphere = texture_sample_prepared(
                            &sphere_sampler, u2, v2, repeat, 0xffffffffU,
                            &sphere_fallback);
                        int r = (color >> 16) & 255, g = (color >> 8) & 255, b = color & 255;
                        int sr = (sphere >> 16) & 255, sg = (sphere >> 8) & 255, sb = sphere & 255;
                        if (blend_mode == 2) {
                            r += sr; g += sg; b += sb;
                        } else {
                            pixel_divisions += 3;
                            worker->material_color_divisions += 3;
                            /* PMX mode 1 is multiplicative.  Mode 2 is an
                             * additive sphere map, handled above. */
                            r = r * sr / 255; g = g * sg / 255; b = b * sb / 255;
                        }
                        if (r < 0) r = 0;
                        if (r > 255) r = 255;
                        if (g < 0) g = 0;
                        if (g > 255) g = 255;
                        if (b < 0) b = 0;
                        if (b > 255) b = 255;
                        color = (color & 0xff000000U) | (uint32_t)r << 16 |
                                (uint32_t)g << 8 | (uint32_t)b;
                    }
                    if (has_toon) {
                        pixel_divisions += 3;
                        worker->material_color_divisions += 3;
                        int tr = (toon_multiplier >> 16) & 255;
                        int tg = (toon_multiplier >> 8) & 255;
                        int tb = toon_multiplier & 255;
                        color = (color & 0xff000000U) |
                            (uint32_t)(((color >> 16) & 255) * tr / 255) << 16 |
                            (uint32_t)(((color >> 8) & 255) * tg / 255) << 8 |
                            (uint32_t)((color & 255) * tb / 255);
                    }
                    long light = light_factor >= 0 ? light_factor :
                        (e0 * a->light + e1 * b->light + e2 * c->light) / area;
                    long fog = fog_factor >= 0 ? fog_factor :
                        (e0 * a->fog + e1 * b->fog + e2 * c->fog) / area;
                    if (light_factor < 0) pixel_divisions++;
                    if (fog_factor < 0) pixel_divisions++;
                    if (light_factor < 0) worker->material_color_divisions++;
                    if (fog_factor < 0) worker->material_color_divisions++;
                    {
                    int alpha = diagnostic & TOY_RENDER_DIAG_FORCE_OPAQUE ?
                                255 :
                                (int)(color >> 24) * material_alpha / 255;
                    if (!(diagnostic & TOY_RENDER_DIAG_FORCE_OPAQUE))
                    {
                        pixel_divisions++;
                        worker->alpha_divisions++;
                    }
                    if (alpha > 0) {
                        pixel_divisions += 12; /* material 6 + light/fog 6 */
                        worker->material_color_divisions += 12;
                        worker->shaded_px++;
                        {
                            int r = (color >> 16) & 255;
                            int g = (color >> 8) & 255;
                            int b = color & 255;
                            r += ((material_ambient >> 16) & 255) * 16 / 255 +
                                 ((material_specular >> 16) & 255) * material_specular_level / 255;
                            g += ((material_ambient >> 8) & 255) * 16 / 255 +
                                 ((material_specular >> 8) & 255) * material_specular_level / 255;
                            b += (material_ambient & 255) * 16 / 255 +
                                 (material_specular & 255) * material_specular_level / 255;
                            color = (color & 0xff000000U) |
                                (uint32_t)clampi(r, 0, 255) << 16 |
                                (uint32_t)clampi(g, 0, 255) << 8 |
                                (uint32_t)clampi(b, 0, 255);
                        }
                        color = shade_color(color, (int)light, (int)fog);
                        if (alpha == 255) {
                            depth[at] = (int)inv_norm;
                            row[x] = color;
                        } else {
                            pixel_divisions += 3;
                            worker->blend_divisions += 3;
                            worker->alpha_blended_pixels++;
                            uint32_t under = row[x];
                            int ur = (under >> 16) & 255;
                            int ug = (under >> 8) & 255;
                            int ub = under & 255;
                            int sr = (color >> 16) & 255;
                            int sg = (color >> 8) & 255;
                            int sb = color & 255;
                            row[x] = (uint32_t)((sr * alpha + ur * (255 - alpha)) / 255) << 16 |
                                     (uint32_t)((sg * alpha + ug * (255 - alpha)) / 255) << 8 |
                                     (uint32_t)((sb * alpha + ub * (255 - alpha)) / 255);
                        }
                        (*tex_pixels)++;
                        worker->written_px++;
                        if (used_fallback) (*fallback_pixels)++;
                        drawn++;
                    }
                    else worker->alpha_zero_pixels++;
                    worker->material_path_divisions[path] += pixel_divisions;
                    }
                }
            }
            e0 += dEx0; e1 += dEx1; e2 += dEx2;
            if (diagnostic & TOY_RENDER_DIAG_AFFINE_UV) {
                affine_u += affine_du_dx; affine_v += affine_dv_dx;
                affine_u2 += affine_du2_dx; affine_v2 += affine_dv2_dx;
            }
        }
        w0 += dEy0; w1 += dEy1; w2 += dEy2;
        if (diagnostic & TOY_RENDER_DIAG_AFFINE_UV) {
            affine_u_row += affine_du_dy; affine_v_row += affine_dv_dy;
            affine_u2_row += affine_du2_dy; affine_v2_row += affine_dv2_dy;
        }
    }
    worker->inside_px += inside;
    return drawn;
}

static void copy_vertex(struct toy_screen_vertex *out,
                        const struct toy_screen_vertex *in)
{
    out->x = in->x; out->y = in->y; out->z = in->z;
    out->u = in->u; out->v = in->v;
    out->inv_z = in->inv_z;
    out->u_over_z = in->u_over_z;
    out->v_over_z = in->v_over_z;
    out->u2 = in->u2; out->v2 = in->v2;
    out->u2_over_z = in->u2_over_z;
    out->v2_over_z = in->v2_over_z;
    out->light = in->light;
    out->fog = in->fog;
}

/* 一条命令，跨两个条带（worker 的 id 带和 id+worker_count 带）光栅化；
 * 像素统计进 worker 本地字段，job 结束后由主线程汇总。 */
static void rasterize_cmd(struct toy_renderer *renderer,
                          const struct toy_raster_cmd *cmd,
                          int y0, int y1,
                          struct toy_render_worker *worker)
{
    if (cmd->area >= 0) return;
    /* 路径段计时：命令类型翻转时才取一次钟（见 renderer_monotonic_us
     * 注释）；同段内两条带的光栅化都累计进该路径。 */
    if (cmd->textured != worker->last_path) {
        if (worker->last_path >= 0) {
            long dt = renderer_monotonic_us() - worker->path_start;
            if (worker->last_path) worker->tex_us += dt;
            else worker->flat_us += dt;
        }
        worker->last_path = cmd->textured;
        worker->path_start = renderer_monotonic_us();
    }
    /* 包围盒扫描像素：内层循环的精确迭代数（x 全宽 × 本带裁剪后行数） */
    worker->bbox_px += (unsigned long)(cmd->bbox_maxx - cmd->bbox_minx + 1) *
                       (unsigned long)(y1 - y0 + 1);
    if (cmd->textured)
        worker->pixels += raster_tex(renderer, worker, &cmd->a, &cmd->b, &cmd->c,
                                     cmd->area, cmd->bbox_minx, cmd->bbox_maxx,
                                     y0, y1, cmd->texture, cmd->texture2,
                                     cmd->blend_mode, cmd->has_toon,
                                     cmd->toon_multiplier,
                                     cmd->material_features,
                                     cmd->base_texture_valid,
                                     cmd->sphere_texture_valid,
                                     cmd->material_alpha,
                                     cmd->material_ambient,
                                     cmd->material_specular,
                                     cmd->material_specular_level,
                                     cmd->repeat,
                                     cmd->fallback, cmd->light, cmd->fog,
                                     &worker->textured_pixels,
                                     &worker->texture_fallback_pixels);
    else
        worker->pixels += raster_flat(renderer, worker, &cmd->a, &cmd->b, &cmd->c,
                                      cmd->area, cmd->bbox_minx,
                                      cmd->bbox_maxx, y0, y1,
                                      shade_color(cmd->color, cmd->light, cmd->fog),
                                      cmd->overlay);
}

static int grow_cmds(struct toy_renderer *renderer)
{
    int new_cap = renderer->cmd_cap > 0 ? renderer->cmd_cap * 2
                                        : TOY_RENDER_CMD_INIT;
    struct toy_raster_cmd *new_cmds, *new_sort_cmds;
    if (new_cap > TOY_RENDER_CMD_MAX) return 0;
    new_cmds = tlibc_malloc((size_t)new_cap * sizeof(struct toy_raster_cmd));
    if (!new_cmds) return 0;
    new_sort_cmds = tlibc_malloc((size_t)new_cap * sizeof(struct toy_raster_cmd));
    if (!new_sort_cmds) { tlibc_free(new_cmds); return 0; }
    if (renderer->cmd_count > 0)
        memcpy(new_cmds, renderer->cmds,
               (size_t)renderer->cmd_count * sizeof(struct toy_raster_cmd));
    if (renderer->cmds) tlibc_free(renderer->cmds);
    if (renderer->sort_cmds) tlibc_free(renderer->sort_cmds);
    renderer->cmds = new_cmds;
    renderer->sort_cmds = new_sort_cmds;
    renderer->cmd_cap = new_cap;
    renderer->sort_cmd_cap = new_cap;
    return 1;
}

static int record_cmd(struct toy_renderer *renderer, int textured,
                      const struct toy_screen_vertex *a,
                      const struct toy_screen_vertex *b,
                      const struct toy_screen_vertex *c,
                      long long area, uint32_t color,
                      const struct toy_texture_view *texture,
                      int repeat, uint32_t fallback, int light, int fog,
                      int overlay)
{
    struct toy_raster_cmd *cmd;
    if (renderer->cmd_count >= renderer->cmd_cap) {
        if (!grow_cmds(renderer)) {
            renderer->cmd_overflow++;
            return 0;
        }
    }
    cmd = &renderer->cmds[renderer->cmd_count++];
    cmd->textured = textured;
    cmd->overlay = overlay;
    cmd->repeat = repeat;
    cmd->color = color;
    cmd->fallback = fallback;
    cmd->light = light;
    cmd->fog = fog;
    cmd->texture = texture;
    cmd->texture2 = 0;
    cmd->texture3 = 0;
    cmd->blend_mode = 0;
    cmd->toon_shared = -1;
    cmd->toon_level = 255;
    cmd->has_toon = 0;
    cmd->toon_multiplier = 0xffffffffU;
    cmd->material_features = 0;
    cmd->base_texture_valid = texture_valid(texture);
    cmd->sphere_texture_valid = 0;
    cmd->material_alpha = 255;
    cmd->material_ambient = 0;
    cmd->material_specular = 0;
    cmd->material_specular_level = 0;
    cmd->transparent = textured && texture && texture->has_transparency;
    cmd->edge = renderer->recording_edge;
    cmd->area = area;
    /* 投影坐标已被调用方裁剪过；包围盒缓存进命令，工作线程按带直接跳过。 */
    cmd->bbox_minx = clampi(a->x < b->x ? (a->x < c->x ? a->x : c->x) :
                                             (b->x < c->x ? b->x : c->x),
                            0, renderer->surface.width - 1);
    cmd->bbox_maxx = clampi((a->x > b->x ? (a->x > c->x ? a->x : c->x) :
                                              (b->x > c->x ? b->x : c->x)) + 1,
                            0, renderer->surface.width - 1);
    cmd->bbox_miny = clampi(a->y < b->y ? (a->y < c->y ? a->y : c->y) :
                                             (b->y < c->y ? b->y : c->y),
                            0, renderer->surface.height - 1);
    cmd->bbox_maxy = clampi((a->y > b->y ? (a->y > c->y ? a->y : c->y) :
                                              (b->y > c->y ? b->y : c->y)) + 1,
                            0, renderer->surface.height - 1);
    copy_vertex(&cmd->a, a);
    copy_vertex(&cmd->b, b);
    copy_vertex(&cmd->c, c);
    return 1;
}

int toy_renderer_triangle(struct toy_renderer *renderer,
                          const struct toy_screen_vertex *a,
                          const struct toy_screen_vertex *b,
                          const struct toy_screen_vertex *c,
                          uint32_t color)
{
    return toy_renderer_triangle_lit(renderer, a, b, c, color, 256, 0);
}

int toy_renderer_triangle_lit(struct toy_renderer *renderer,
                              const struct toy_screen_vertex *a,
                              const struct toy_screen_vertex *b,
                              const struct toy_screen_vertex *c,
                              uint32_t color, int light, int fog)
{
    long long area;
    if (!renderer || !renderer->depth || !a || !b || !c) return 0;
    area = edge(a, b, c->x, c->y);
    if (area >= 0) return 0;
    if (record_cmd(renderer, 0, a, b, c, area, color, NULL, 0, 0,
                   light, fog, 0)) {
        renderer->submitted_triangles++;
        renderer->submitted_vertices += 3;
    }
    return 0;
}

int toy_renderer_triangle_lit_overlay(struct toy_renderer *renderer,
                                      const struct toy_screen_vertex *a,
                                      const struct toy_screen_vertex *b,
                                      const struct toy_screen_vertex *c,
                                      uint32_t color, int light, int fog)
{
    long long area;
    if (!renderer || !renderer->depth || !a || !b || !c) return 0;
    area = edge(a, b, c->x, c->y);
    if (area >= 0) return 0;
    if (record_cmd(renderer, 0, a, b, c, area, color, NULL, 0, 0,
                   light, fog, 1)) {
        renderer->submitted_triangles++;
        renderer->submitted_vertices += 3;
    }
    return 0;
}

int toy_renderer_triangle_textured(struct toy_renderer *renderer,
                                   const struct toy_screen_vertex *a,
                                   const struct toy_screen_vertex *b,
                                   const struct toy_screen_vertex *c,
                                   const struct toy_texture_view *texture,
                                   int repeat, uint32_t fallback_color)
{
    return toy_renderer_triangle_textured_lit(renderer, a, b, c, texture,
                                              repeat, fallback_color, 256, 0);
}

int toy_renderer_triangle_textured_lit(struct toy_renderer *renderer,
                                       const struct toy_screen_vertex *a,
                                       const struct toy_screen_vertex *b,
                                       const struct toy_screen_vertex *c,
                                       const struct toy_texture_view *texture,
                                       int repeat, uint32_t fallback_color,
                                       int light, int fog)
{
    long long area;
    if (!renderer || !renderer->depth || !a || !b || !c) return 0;
    area = edge(a, b, c->x, c->y);
    if (area >= 0) return 0;
    renderer->textured_triangles++;
    if (record_cmd(renderer, 1, a, b, c, area, 0, texture, repeat,
                   fallback_color, light, fog, 0)) {
        renderer->submitted_triangles++;
        renderer->submitted_vertices += 3;
    }
    return 0;
}

int toy_renderer_triangle_textured_dual_lit(struct toy_renderer *renderer,
                                            const struct toy_screen_vertex *a,
                                            const struct toy_screen_vertex *b,
                                            const struct toy_screen_vertex *c,
                                            const struct toy_texture_view *texture,
                                            const struct toy_texture_view *texture2,
                                            int blend_mode, int repeat,
                                            uint32_t fallback_color,
                                            int light, int fog)
{
    long long area;
    struct toy_raster_cmd *cmd;
    if (!renderer || !renderer->depth || !a || !b || !c) return 0;
    area = edge(a, b, c->x, c->y);
    if (area >= 0) return 0;
    renderer->textured_triangles++;
    if (!record_cmd(renderer, 1, a, b, c, area, 0, texture, repeat,
                    fallback_color, light, fog, 0)) return 0;
    cmd = &renderer->cmds[renderer->cmd_count - 1];
    cmd->texture2 = texture2;
    cmd->blend_mode = blend_mode;
    if (texture2) cmd->material_features |= TOY_MATERIAL_SPHERE;
    cmd->sphere_texture_valid = texture_valid(texture2);
    renderer->submitted_triangles++;
    renderer->submitted_vertices += 3;
    return 0;
}

int toy_renderer_triangle_textured_material_lit(
    struct toy_renderer *renderer,
    const struct toy_screen_vertex *a,
    const struct toy_screen_vertex *b,
    const struct toy_screen_vertex *c,
    const struct toy_texture_view *texture,
    const struct toy_texture_view *sphere_texture,
    int sphere_mode,
    const struct toy_texture_view *toon_texture,
    int toon_shared, int toon_level, int material_alpha,
    uint32_t material_ambient, uint32_t material_specular,
    int material_specular_level,
    int repeat, uint32_t fallback_color, int light, int fog)
{
    long long area;
    struct toy_raster_cmd *cmd;
    if (!renderer || !renderer->depth || !a || !b || !c) return 0;
    area = edge(a, b, c->x, c->y);
    if (area >= 0) return 0;
    renderer->textured_triangles++;
    if (!record_cmd(renderer, 1, a, b, c, area, 0, texture, repeat,
                    fallback_color, light, fog, 0)) return 0;
    cmd = &renderer->cmds[renderer->cmd_count - 1];
    cmd->texture2 = sphere_texture;
    cmd->blend_mode = sphere_mode;
    cmd->texture3 = toon_texture;
    cmd->toon_shared = toon_shared;
    cmd->toon_level = toon_level;
    cmd->has_toon = toon_texture != 0 || toon_shared >= 0;
    if (cmd->has_toon) cmd->material_features |= TOY_MATERIAL_TOON;
    if (sphere_texture) cmd->material_features |= TOY_MATERIAL_SPHERE;
    cmd->sphere_texture_valid = texture_valid(sphere_texture);
    if (cmd->has_toon) {
        int level = clampi(toon_level, 0, 255);
        if (renderer->toon_cache_valid &&
            renderer->toon_cache_texture == toon_texture &&
            renderer->toon_cache_shared == toon_shared &&
            renderer->toon_cache_level == level) {
            cmd->toon_multiplier = renderer->toon_cache_multiplier;
        } else if (toon_texture) {
            int toon_fallback = 0;
            cmd->toon_multiplier = texture_sample(
                toon_texture, 32768, (255 - level) * 257L,
                0, 0xffffffffU, &toon_fallback);
        } else {
            int dark = 150 + (toon_shared & 7) * 6;
            int value = level >= 144 ? 255 : dark;
            cmd->toon_multiplier = 0xff000000U | (uint32_t)value << 16 |
                                   (uint32_t)value << 8 | (uint32_t)value;
        }
        renderer->toon_cache_texture = toon_texture;
        renderer->toon_cache_shared = toon_shared;
        renderer->toon_cache_level = level;
        renderer->toon_cache_multiplier = cmd->toon_multiplier;
        renderer->toon_cache_valid = 1;
    }
    if (material_alpha < 0) material_alpha = 0;
    if (material_alpha > 255) material_alpha = 255;
    cmd->material_alpha = material_alpha;
    cmd->material_ambient = material_ambient;
    cmd->material_specular = material_specular;
    cmd->material_specular_level = clampi(material_specular_level, 0, 255);
    cmd->transparent = material_alpha < 255 ||
                       (texture && texture->has_transparency);
    renderer->submitted_triangles++;
    renderer->submitted_vertices += 3;
    return 0;
}

void toy_renderer_set_recording_edge(struct toy_renderer *renderer, int edge)
{
    if (renderer) renderer->recording_edge = edge != 0;
}

void toy_renderer_set_worker_count(struct toy_renderer *renderer, int count)
{
    if (!renderer || renderer->workers) return;
    if (count < 0) count = 0;
    if (count > TOY_RENDER_MAX_WORKERS) count = TOY_RENDER_MAX_WORKERS;
    renderer->requested_worker_count = count;
}

void toy_renderer_set_texture_diagnostics(struct toy_renderer *renderer,
                                          int flags)
{
    if (renderer) renderer->texture_diagnostic_flags = flags;
}

/* ── 工作线程池：futex 等待 job_generation，主线程分发后自旋等 done ── */

#if !defined(TOYC_WINDOWS)
static int parse_cpu_online(const char *text, int length)
{
    int at = 0, count = 0;
    while (at < length) {
        int first = 0, last;
        if (text[at] < '0' || text[at] > '9') break;
        while (at < length && text[at] >= '0' && text[at] <= '9')
            first = first * 10 + text[at++] - '0';
        last = first;
        if (at < length && text[at] == '-') {
            at++;
            if (at >= length || text[at] < '0' || text[at] > '9') return 0;
            last = 0;
            while (at < length && text[at] >= '0' && text[at] <= '9')
                last = last * 10 + text[at++] - '0';
        }
        if (last < first || last - first > 4096) return 0;
        count += last - first + 1;
        if (at >= length || text[at] == '\n') break;
        if (text[at++] != ',') return 0;
    }
    return count;
}
#endif

static int count_processors(void)
{
#if defined(TOYC_WINDOWS)
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors > 0 ? (int)info.dwNumberOfProcessors : 4;
#else
    static const char processor[] = "processor";
    char buf[4096];
    int fd, n, count = 0, match = 0;
    /* This compact Linux list (for example 0-15,32-47) avoids a libc
     * sysconf dependency and cannot be truncated by a large cpuinfo file. */
    fd = __openat(AT_FDCWD, "/sys/devices/system/cpu/online", O_RDONLY, 0);
    if (fd >= 0) {
        n = (int)__read(fd, buf, (int)sizeof(buf));
        __close(fd);
        if (n > 0) count = parse_cpu_online(buf, n);
    }
    /* Container/sysfs fallback: parse every cpuinfo chunk and retain line
     * matching state across read boundaries. */
    if (count < 1) {
        fd = __openat(AT_FDCWD, "/proc/cpuinfo", O_RDONLY, 0);
        if (fd >= 0) {
            count = 0;
            for (;;) {
                n = (int)__read(fd, buf, (int)sizeof(buf));
                if (n <= 0) break;
                for (int i = 0; i < n; i++) {
                    if (buf[i] == '\n') match = 0;
                    else if (match >= 0 && match < 9 &&
                             buf[i] == processor[match]) {
                        match++;
                        if (match == 9) count++;
                    } else match = -1;
                }
            }
            __close(fd);
        }
    }
    if (count < 1) count = 4;
    return count;
#endif
}

static void worker_clear(struct toy_renderer *renderer, int id)
{
    int band_count = renderer->worker_count * 2;
    int height = renderer->surface.height;
    int bands[2];
    bands[0] = id;
    bands[1] = id + renderer->worker_count;
    for (int k = 0; k < 2; k++) {
        int band = bands[k];
        int top = (int)((long)band * height / band_count);
        int bottom = (int)((long)(band + 1) * height / band_count) - 1;
        int rows = bottom - top + 1;
        uint32_t *pixels;
        int *depth;
        if (rows <= 0) continue;
        pixels = (uint32_t *)((unsigned char *)renderer->surface.pixels +
                              top * renderer->surface.stride);
        depth = renderer->depth + top * renderer->surface.width;
        for (int y = 0; y < rows; y++) {
            for (int x = 0; x < renderer->surface.width; x++) {
                pixels[x] = renderer->job_clear_color;
                depth[x] = 0;
            }
            pixels = (uint32_t *)((unsigned char *)pixels +
                                  renderer->surface.stride);
            depth += renderer->surface.width;
        }
    }
}

static void worker_rasterize(struct toy_renderer *renderer, int id,
                             struct toy_render_worker *worker)
{
    int band_count = renderer->worker_count * 2;
    int height = renderer->surface.height;
    /* 镜像带配对 (id, band_count-1-id)：本游戏三角形集中在屏幕下半部
     * （天空是直接填充不进命令列表），固定 (id, id+N) 会让下半部 worker
     * 各拿到两条密集带。镜像后每个 worker 恰好一条上半带 + 一条下半带。 */
    int band_a = id;
    int band_b = band_count - 1 - id;
    int top0 = (int)((long)band_a * height / band_count);
    int bot0 = (int)((long)(band_a + 1) * height / band_count) - 1;
    int top1 = (int)((long)band_b * height / band_count);
    int bot1 = (int)((long)(band_b + 1) * height / band_count) - 1;
    worker->commands = (unsigned long)renderer->cmd_count;
    for (int i = 0; i < renderer->cmd_count; i++) {
        const struct toy_raster_cmd *cmd = &renderer->cmds[i];
        int hit0 = cmd->bbox_maxy >= top0 && cmd->bbox_miny <= bot0;
        int hit1 = cmd->bbox_maxy >= top1 && cmd->bbox_miny <= bot1;
        if (hit0 || hit1) worker->triangles++;
        if (hit0) {
            int y0 = cmd->bbox_miny > top0 ? cmd->bbox_miny : top0;
            int y1 = cmd->bbox_maxy < bot0 ? cmd->bbox_maxy : bot0;
            if (y0 <= y1) rasterize_cmd(renderer, cmd, y0, y1, worker);
        }
        if (hit1) {
            int y0 = cmd->bbox_miny > top1 ? cmd->bbox_miny : top1;
            int y1 = cmd->bbox_maxy < bot1 ? cmd->bbox_maxy : bot1;
            if (y0 <= y1) rasterize_cmd(renderer, cmd, y0, y1, worker);
        }
    }
    close_runs(worker);
}

static void *render_worker_main(void *arg)
{
    struct toy_render_worker *worker = (struct toy_render_worker *)arg;
    struct toy_renderer *renderer = worker->renderer;
    int seen = renderer->job_generation;
    /* 停放确认：pthread_create 返回时子线程可能还没进入 futex 等待，
     * 若此时主线程已推进 generation，唤醒会丢失、worker 永久休眠。
     * 先快照 generation 并累加 done_count，主线程等全部确认后才分发。 */
    __sync_synchronize();
    atomic_fetch_add_u32((volatile uint32_t *)&renderer->job_done_count, 1);
    for (;;) {
        __sync_synchronize();
        while (renderer->job_generation == seen && !renderer->quit) {
            /* FUTEX_WAIT：值未变则休眠；主线程分发时 FUTEX_WAKE。 */
            __futex((unsigned int *)&renderer->job_generation,
                    TOY_FUTEX_WAIT, (unsigned int)renderer->job_generation,
                    NULL, NULL, 0);
            __sync_synchronize();
        }
        if (renderer->quit) break;
        seen = renderer->job_generation;
        worker->pixels = 0;
        worker->textured_pixels = 0;
        worker->texture_fallback_pixels = 0;
        worker->commands = 0;
        worker->triangles = 0;
        worker->depth_pass_px = 0;
        worker->shaded_px = 0;
        worker->written_px = 0;
        worker->flat_pixels = 0;
        worker->alpha_blended_pixels = 0;
        worker->alpha_zero_pixels = 0;
        worker->depth_divisions = 0;
        worker->base_perspective_divisions = 0;
        worker->sphere_perspective_divisions = 0;
        worker->texture_address_divisions = 0;
        worker->material_color_divisions = 0;
        worker->alpha_divisions = 0;
        worker->blend_divisions = 0;
        for (int path = 0; path < 4; path++) {
            worker->material_path_pixels[path] = 0;
            worker->material_path_divisions[path] = 0;
        }
        worker->bbox_px = 0;
        worker->inside_px = 0;
        worker->flat_us = 0;
        worker->tex_us = 0;
        worker->active_us = 0;
        worker->cpu_us = 0;
        worker->last_path = -1;
        worker->path_start = 0;
        long active_start = renderer_monotonic_us();
        long cpu_start = renderer_thread_cpu_us();
        if (renderer->job_is_parallel) {
            if (worker->id < renderer->job_parallel_workers)
                for (;;) {
                    int task = (int)atomic_fetch_add_u32(
                        (volatile uint32_t *)&renderer->job_parallel_next, 1);
                    if (task >= renderer->job_parallel_count) break;
                    renderer->job_parallel_fn(worker->id, task,
                                              renderer->job_parallel_context);
                }
        } else if (renderer->job_is_clear)
            worker_clear(renderer, worker->id);
        else
            worker_rasterize(renderer, worker->id, worker);
        worker->cpu_us = renderer_thread_cpu_us() - cpu_start;
        worker->active_us = renderer_monotonic_us() - active_start;
        __sync_synchronize();
        atomic_fetch_add_u32((volatile uint32_t *)&renderer->job_done_count, 1);
    }
    return NULL;
}

static int ensure_workers(struct toy_renderer *renderer)
{
    int n, i;
    if (renderer->workers) return 0;
    renderer->detected_cpu_count = count_processors();
    n = renderer->requested_worker_count > 0 ?
        renderer->requested_worker_count : renderer->detected_cpu_count;
    if (n > TOY_RENDER_MAX_WORKERS) n = TOY_RENDER_MAX_WORKERS;
    if (n < 1) return -1;
    renderer->workers = tlibc_malloc((size_t)n *
                                     sizeof(struct toy_render_worker));
    if (!renderer->workers) return -1;
    memset(renderer->workers, 0,
           (size_t)n * sizeof(struct toy_render_worker));
    for (i = 0; i < n; i++) {
        renderer->workers[i].id = i;
        renderer->workers[i].renderer = renderer;
        if (pthread_create(&renderer->workers[i].thread, NULL,
                           render_worker_main, &renderer->workers[i]) != 0)
            break;
    }
    renderer->worker_count = i;
    if (i == 0) {
        tlibc_free(renderer->workers);
        renderer->workers = NULL;
        return -1;
    }
    /* 等全部 worker 完成停放确认（generation 快照已取），此后分发
     * 的 generation 推进必然被每个 worker 观察到。 */
    while (renderer->job_done_count != renderer->worker_count)
        __sync_synchronize();
    return 0;
}

/* 分发一个 job 并等待全部 worker 完成。任务字段只在上一 job 全部结束后
 * 才被改写，worker 随后都在 futex 上休眠，因此无并发写竞争。 */
static void renderer_dispatch(struct toy_renderer *renderer, int is_clear,
                              uint32_t clear_color)
{
    long wait_start = renderer_monotonic_us();
    renderer->job_is_clear = is_clear;
    renderer->job_is_parallel = 0;
    renderer->job_clear_color = clear_color;
    renderer->job_done_count = 0;
    __sync_synchronize();
    renderer->job_generation++;
    __sync_synchronize();
    __futex((unsigned int *)&renderer->job_generation, TOY_FUTEX_WAKE,
            0x7fffffff, NULL, NULL, 0);
    while (renderer->job_done_count != renderer->worker_count)
        __sync_synchronize();
    renderer->last_worker_wait_us = renderer_monotonic_us() - wait_start;
}

int toy_renderer_parallel_for(struct toy_renderer *renderer, int task_count,
                              int worker_limit,
                              toy_renderer_parallel_fn function,
                              void *context)
{
    if (!renderer || !function || task_count < 1) return -1;
    if (ensure_workers(renderer) < 0) return -1;
    if (worker_limit < 1 || worker_limit > renderer->worker_count)
        worker_limit = renderer->worker_count;
    renderer->job_parallel_fn = function;
    renderer->job_parallel_context = context;
    renderer->job_parallel_count = task_count;
    renderer->job_parallel_workers = worker_limit;
    renderer->job_parallel_next = 0;
    renderer->job_is_clear = 0;
    renderer->job_is_parallel = 1;
    renderer->job_done_count = 0;
    __sync_synchronize();
    renderer->job_generation++;
    __sync_synchronize();
    __futex((unsigned int *)&renderer->job_generation, TOY_FUTEX_WAKE,
            0x7fffffff, NULL, NULL, 0);
    while (renderer->job_done_count != renderer->worker_count)
        __sync_synchronize();
    renderer->job_is_parallel = 0;
    return 0;
}

int toy_renderer_merge_commands(struct toy_renderer *renderer,
                                const struct toy_renderer *source)
{
    int required;
    if (!renderer || !source || source->cmd_count < 0) return -1;
    required = renderer->cmd_count + source->cmd_count;
    if (required > TOY_RENDER_CMD_MAX) return -1;
    while (renderer->cmd_cap < required)
        if (!grow_cmds(renderer)) return -1;
    memcpy(renderer->cmds + renderer->cmd_count, source->cmds,
           (size_t)source->cmd_count * sizeof(*source->cmds));
    renderer->cmd_count = required;
    renderer->submitted_triangles += source->submitted_triangles;
    renderer->submitted_vertices += source->submitted_vertices;
    renderer->textured_triangles += source->textured_triangles;
    renderer->textured_pixels += source->textured_pixels;
    renderer->texture_fallback_pixels += source->texture_fallback_pixels;
    renderer->cmd_overflow += source->cmd_overflow;
    return 0;
}

static void clear_single(struct toy_renderer *renderer, uint32_t clear_color)
{
    for (int y = 0; y < renderer->surface.height; y++) {
        uint32_t *row = (uint32_t *)((unsigned char *)renderer->surface.pixels +
                                     y * renderer->surface.stride);
        int *depth = renderer->depth + y * renderer->surface.width;
        for (int x = 0; x < renderer->surface.width; x++) {
            row[x] = clear_color;
            depth[x] = 0;
        }
    }
}

int toy_renderer_begin(struct toy_renderer *renderer,
                       const struct toy_surface *surface, uint32_t clear_color)
{
    size_t required;
    if (!renderer || !surface || !surface->pixels ||
        surface->width <= 0 || surface->height <= 0) return -1;
    required = (size_t)surface->width * surface->height * sizeof(int);
    if (required != renderer->depth_size) {
        int *new_depth;
        if (renderer->depth) tlibc_free(renderer->depth);
        renderer->depth = NULL;
        renderer->depth_size = 0;
        new_depth = tlibc_malloc(required);
        if (!new_depth) return -1;
        renderer->depth = new_depth;
        renderer->depth_size = required;
    }
    if (!renderer->cmds && !grow_cmds(renderer)) return -1;
    renderer->cmd_count = 0;
    renderer->cmd_overflow = 0;
    renderer->textured_pixels = 0;
    renderer->textured_triangles = 0;
    renderer->texture_fallback_pixels = 0;
    renderer->submitted_triangles = 0;
    renderer->submitted_vertices = 0;
    renderer->last_bbox_px = 0;
    renderer->last_inside_px = 0;
    renderer->last_tex_px = 0;
    renderer->last_tex_tris = 0;
    renderer->tex_tris_mark = 0;
    renderer->last_flat_us = 0;
    renderer->last_tex_us = 0;
    renderer->last_sort_us = 0;
    renderer->last_classify_us = 0;
    renderer->last_merge_copy_us = 0;
    renderer->last_actual_sort_us = 0;
    renderer->last_opaque_cmds = 0;
    renderer->last_transparent_cmds = 0;
    renderer->last_edge_cmds = 0;
    renderer->last_sorted_cmds = 0;
    renderer->last_worker_wait_us = 0;
    renderer->recording_edge = 0;
    renderer->toon_cache_valid = 0;
    /* Keep this self-host friendly: avoid a whole-structure assignment here. */
    renderer->surface.pixels = surface->pixels;
    renderer->surface.width = surface->width;
    renderer->surface.height = surface->height;
    renderer->surface.stride = surface->stride;
    /* Tiny in-memory probes gain nothing from the worker pool and should not
     * require thread/TLS setup merely to exercise rasterization math. */
    if ((long)surface->width * surface->height >= 320 * 180 &&
        ensure_workers(renderer) == 0)
        renderer_dispatch(renderer, 1, clear_color);
    else
        clear_single(renderer, clear_color);
    return 0;
}

static long transparent_depth_key(const struct toy_raster_cmd *cmd)
{
    return cmd->a.inv_z + cmd->b.inv_z + cmd->c.inv_z;
}

static void sort_transparent_commands(struct toy_raster_cmd *commands,
                                      struct toy_raster_cmd *temporary,
                                      int begin, int end)
{
    int middle, left, right, out;
    if (end - begin < 2) return;
    middle = begin + (end - begin) / 2;
    sort_transparent_commands(commands, temporary, begin, middle);
    sort_transparent_commands(commands, temporary, middle, end);
    left = begin; right = middle; out = begin;
    while (left < middle && right < end) {
        if (transparent_depth_key(&commands[left]) <=
            transparent_depth_key(&commands[right]))
            temporary[out++] = commands[left++];
        else temporary[out++] = commands[right++];
    }
    while (left < middle) temporary[out++] = commands[left++];
    while (right < end) temporary[out++] = commands[right++];
    for (out = begin; out < end; out++) commands[out] = temporary[out];
}

int toy_renderer_flush(struct toy_renderer *renderer)
{
    long total = 0;
    unsigned long tex = 0, fallback = 0, bbox = 0, inside = 0;
    long flat_us = 0, tex_us = 0;
    long sort_start, phase_start;
    if (!renderer) return 0;
    renderer->last_classify_us = 0;
    renderer->last_merge_copy_us = 0;
    renderer->last_actual_sort_us = 0;
    renderer->last_opaque_cmds = 0;
    renderer->last_transparent_cmds = 0;
    renderer->last_edge_cmds = 0;
    renderer->last_sorted_cmds = 0;
    sort_start = renderer_monotonic_us();
    phase_start = sort_start;
    if (renderer->cmd_count > 0) {
        int i;
        for (i = 0; i < renderer->cmd_count; i++) {
            if (renderer->cmds[i].transparent)
                renderer->last_transparent_cmds++;
            else
                renderer->last_opaque_cmds++;
            if (renderer->cmds[i].edge) renderer->last_edge_cmds++;
        }
    }
    renderer->last_classify_us = renderer_monotonic_us() - phase_start;
    /* 常见的全不透明场景直接保持记录顺序，避免为了空透明列表复制整个
     * 大型命令池。确有透明命令时才沿用稳定的 opaque + transparent 布局。 */
    if (renderer->last_transparent_cmds > 0 && renderer->cmd_count > 1 &&
        renderer->sort_cmds) {
        int opaque = 0, transparent = 0, i;
        phase_start = renderer_monotonic_us();
        /* Stable-partition without copying the opaque pool out and back:
         * compact opaque commands in place and collect only transparent ones
         * in sort_cmds. Edge commands are opaque and therefore never sorted. */
        for (i = 0; i < renderer->cmd_count; i++) {
            if (renderer->cmds[i].transparent)
                renderer->sort_cmds[transparent++] = renderer->cmds[i];
            else {
                if (opaque != i) renderer->cmds[opaque] = renderer->cmds[i];
                opaque++;
            }
        }
        renderer->last_merge_copy_us += renderer_monotonic_us() - phase_start;
        /* Transparent commands use stable painter order. inv_z is larger
         * when nearer, so ascending average inverse depth is back-to-front. */
        phase_start = renderer_monotonic_us();
        sort_transparent_commands(renderer->sort_cmds, renderer->cmds + opaque,
                                  0, transparent);
        renderer->last_actual_sort_us = renderer_monotonic_us() - phase_start;
        renderer->last_sorted_cmds = (unsigned long)transparent;
        phase_start = renderer_monotonic_us();
        memcpy(renderer->cmds + opaque, renderer->sort_cmds,
               (size_t)transparent * sizeof(struct toy_raster_cmd));
        renderer->last_merge_copy_us += renderer_monotonic_us() - phase_start;
    }
    renderer->last_sort_us = renderer_monotonic_us() - sort_start;
    if (renderer->worker_count > 0) {
        renderer_dispatch(renderer, 0, 0);
        /* 命令已被本次 flush 消费：清零后下一条记录从空列表开始，
         * 每帧第二次 flush 不会重复光栅化整个场景。 */
        renderer->cmd_count = 0;
        for (int i = 0; i < renderer->worker_count; i++) {
            total += renderer->workers[i].pixels;
            tex += renderer->workers[i].textured_pixels;
            fallback += renderer->workers[i].texture_fallback_pixels;
            bbox += renderer->workers[i].bbox_px;
            inside += renderer->workers[i].inside_px;
            flat_us += renderer->workers[i].flat_us;
            tex_us += renderer->workers[i].tex_us;
        }
    } else {
        /* 单线程降级：整屏一条带，统计进栈上 worker 壳。 */
        struct toy_render_worker local;
        memset(&local, 0, sizeof(local));
        local.renderer = renderer;
        local.last_path = -1;
        for (int i = 0; i < renderer->cmd_count; i++) {
            const struct toy_raster_cmd *cmd = &renderer->cmds[i];
            rasterize_cmd(renderer, cmd, cmd->bbox_miny, cmd->bbox_maxy,
                          &local);
        }
        close_runs(&local);
        total = local.pixels;
        tex = local.textured_pixels;
        fallback = local.texture_fallback_pixels;
        bbox = local.bbox_px;
        inside = local.inside_px;
        flat_us = local.flat_us;
        tex_us = local.tex_us;
        renderer->cmd_count = 0;
    }
    renderer->textured_pixels += tex;
    renderer->texture_fallback_pixels += fallback;
    /* 本 flush 的漏斗与路径快照（覆盖式，begin 清零；flat 三角形/像素
     * 由调用方用 total 与 last_tex_* 相减获得） */
    renderer->last_bbox_px = bbox;
    renderer->last_inside_px = inside;
    renderer->last_tex_px = tex;
    renderer->last_tex_tris = renderer->textured_triangles -
                              renderer->tex_tris_mark;
    renderer->tex_tris_mark = renderer->textured_triangles;
    renderer->last_flat_us = flat_us;
    renderer->last_tex_us = tex_us;
    return (int)total;
}

void toy_renderer_destroy(struct toy_renderer *renderer)
{
    if (!renderer) return;
    if (renderer->workers) {
        renderer->quit = 1;
        __sync_synchronize();
        renderer->job_generation++;
        __sync_synchronize();
        __futex((unsigned int *)&renderer->job_generation, TOY_FUTEX_WAKE,
                0x7fffffff, NULL, NULL, 0);
        for (int i = 0; i < renderer->worker_count; i++)
            pthread_join(renderer->workers[i].thread, NULL);
        tlibc_free(renderer->workers);
        renderer->workers = NULL;
        renderer->worker_count = 0;
    }
    if (renderer->cmds) tlibc_free(renderer->cmds);
    renderer->cmds = NULL;
    if (renderer->sort_cmds) tlibc_free(renderer->sort_cmds);
    renderer->sort_cmds = NULL;
    if (renderer->depth) tlibc_free(renderer->depth);
    memset(renderer, 0, sizeof(struct toy_renderer));
}
