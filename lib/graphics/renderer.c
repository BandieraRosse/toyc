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

#define TOY_UV_ONE 65536L
#define TOY_INV_Z_SCALE 1048576L
#define TOY_RENDER_MAX_WORKERS 8
#define TOY_RENDER_CMD_INIT 4096
#define TOY_RENDER_CMD_MAX 32768
#define TOY_FUTEX_WAIT 0
#define TOY_FUTEX_WAKE 1

static long edge(const struct toy_screen_vertex *a,
                 const struct toy_screen_vertex *b, int px, int py)
{
    return (px - a->x) * (b->y - a->y) -
           (py - a->y) * (b->x - a->x);
}

static int clampi(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

void toy_renderer_init(struct toy_renderer *renderer)
{
    if (renderer) memset(renderer, 0, sizeof(struct toy_renderer));
}

/* ── 条带化逐像素光栅化（y 范围由调用方给定，数学与单线程版完全一致） ── */

static long raster_flat(struct toy_renderer *renderer,
                        const struct toy_screen_vertex *a,
                        const struct toy_screen_vertex *b,
                        const struct toy_screen_vertex *c,
                        long area, int minx, int maxx,
                        int y0, int y1, uint32_t color)
{
    struct toy_surface *surface = &renderer->surface;
    int y, x, drawn = 0;
    for (y = y0; y <= y1; y++) {
        uint32_t *row = (uint32_t *)((unsigned char *)surface->pixels +
                                     y * surface->stride);
        for (x = minx; x <= maxx; x++) {
            long w0 = edge(b, c, x, y);
            long w1 = edge(c, a, x, y);
            long w2 = edge(a, b, x, y);
            if (w0 <= 0 && w1 <= 0 && w2 <= 0) {
                int z = (int)((w0 * a->z + w1 * b->z + w2 * c->z) / area);
                int at = y * surface->width + x;
                if (z < renderer->depth[at]) {
                    renderer->depth[at] = z;
                    row[x] = color;
                    drawn++;
                }
            }
        }
    }
    return drawn;
}

static int texture_valid(const struct toy_texture_view *t)
{
    unsigned long pixels;
    if (!t || !t->data || !t->width || !t->height) return 0;
    if (t->width > 8192 || t->height > 8192) return 0;
    pixels = (unsigned long)t->width * t->height;
    return pixels <= 0xffffffffUL / 3UL && t->data_size == pixels * 3UL;
}

static long wrap_coord(long value, long limit, int repeat)
{
    if (repeat) {
        if (limit <= 0) return 0;
        value %= limit;
        if (value < 0) value += limit;
        return value;
    }
    if (value < 0) return 0;
    if (value >= limit) return limit - 1;
    return value;
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
    u = wrap_coord(u, TOY_UV_ONE, repeat);
    v = wrap_coord(v, TOY_UV_ONE, repeat);
    x = (u * t->width) / TOY_UV_ONE;
    y = (v * t->height) / TOY_UV_ONE;
    at = (y * t->width + x) * 3;
    p = t->data + at;
    return 0xFF000000U | ((uint32_t)p[0] << 16) |
           ((uint32_t)p[1] << 8) | p[2];
}

static long raster_tex(struct toy_renderer *renderer,
                       const struct toy_screen_vertex *a,
                       const struct toy_screen_vertex *b,
                       const struct toy_screen_vertex *c,
                       long area, int minx, int maxx,
                       int y0, int y1,
                       const struct toy_texture_view *texture,
                       int repeat, uint32_t fallback_color,
                       unsigned long *tex_pixels,
                       unsigned long *fallback_pixels)
{
    struct toy_surface *surface = &renderer->surface;
    int y, x, drawn = 0;
    for (y = y0; y <= y1; y++) {
        uint32_t *row = (uint32_t *)((unsigned char *)surface->pixels +
                                     y * surface->stride);
        for (x = minx; x <= maxx; x++) {
            long w0 = edge(b, c, x, y);
            long w1 = edge(c, a, x, y);
            long w2 = edge(a, b, x, y);
            if (w0 <= 0 && w1 <= 0 && w2 <= 0) {
                long z = (w0 * a->z + w1 * b->z + w2 * c->z) / area;
                int at = y * surface->width + x;
                if (z < renderer->depth[at]) {
                    long inv = w0 * a->inv_z + w1 * b->inv_z + w2 * c->inv_z;
                    long uoz = w0 * a->u_over_z + w1 * b->u_over_z + w2 * c->u_over_z;
                    long voz = w0 * a->v_over_z + w1 * b->v_over_z + w2 * c->v_over_z;
                    int used_fallback = 0;
                    long u = inv ? (uoz / inv) : 0;
                    long v = inv ? (voz / inv) : 0;
                    uint32_t color = texture_sample(texture, u, v, repeat,
                                                     fallback_color, &used_fallback);
                    renderer->depth[at] = (int)z;
                    row[x] = color;
                    (*tex_pixels)++;
                    if (used_fallback) (*fallback_pixels)++;
                    drawn++;
                }
            }
        }
    }
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
}

/* 一条命令，跨两个条带（worker 的 id 带和 id+worker_count 带）光栅化；
 * 像素统计进 worker 本地字段，job 结束后由主线程汇总。 */
static void rasterize_cmd(struct toy_renderer *renderer,
                          const struct toy_raster_cmd *cmd,
                          int y0, int y1,
                          struct toy_render_worker *worker)
{
    if (cmd->textured)
        worker->pixels += raster_tex(renderer, &cmd->a, &cmd->b, &cmd->c,
                                     cmd->area, cmd->bbox_minx, cmd->bbox_maxx,
                                     y0, y1, cmd->texture, cmd->repeat,
                                     cmd->fallback, &worker->textured_pixels,
                                     &worker->texture_fallback_pixels);
    else
        worker->pixels += raster_flat(renderer, &cmd->a, &cmd->b, &cmd->c,
                                      cmd->area, cmd->bbox_minx,
                                      cmd->bbox_maxx, y0, y1, cmd->color);
}

static int grow_cmds(struct toy_renderer *renderer)
{
    int new_cap = renderer->cmd_cap > 0 ? renderer->cmd_cap * 2
                                        : TOY_RENDER_CMD_INIT;
    struct toy_raster_cmd *new_cmds;
    if (new_cap > TOY_RENDER_CMD_MAX) return 0;
    new_cmds = tlibc_malloc((size_t)new_cap * sizeof(struct toy_raster_cmd));
    if (!new_cmds) return 0;
    if (renderer->cmd_count > 0)
        memcpy(new_cmds, renderer->cmds,
               (size_t)renderer->cmd_count * sizeof(struct toy_raster_cmd));
    if (renderer->cmds) tlibc_free(renderer->cmds);
    renderer->cmds = new_cmds;
    renderer->cmd_cap = new_cap;
    return 1;
}

static int record_cmd(struct toy_renderer *renderer, int textured,
                      const struct toy_screen_vertex *a,
                      const struct toy_screen_vertex *b,
                      const struct toy_screen_vertex *c,
                      long area, uint32_t color,
                      const struct toy_texture_view *texture,
                      int repeat, uint32_t fallback)
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
    cmd->repeat = repeat;
    cmd->color = color;
    cmd->fallback = fallback;
    cmd->texture = texture;
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
    long area;
    if (!renderer || !renderer->depth || !a || !b || !c) return 0;
    area = edge(a, b, c->x, c->y);
    if (area >= 0) return 0;
    if (record_cmd(renderer, 0, a, b, c, area, color, NULL, 0, 0)) {
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
    long area;
    if (!renderer || !renderer->depth || !a || !b || !c) return 0;
    area = edge(a, b, c->x, c->y);
    if (area >= 0) return 0;
    renderer->textured_triangles++;
    if (record_cmd(renderer, 1, a, b, c, area, 0, texture, repeat,
                   fallback_color)) {
        renderer->submitted_triangles++;
        renderer->submitted_vertices += 3;
    }
    return 0;
}

/* ── 工作线程池：futex 等待 job_generation，主线程分发后自旋等 done ── */

static int count_processors(void)
{
    char buf[16384];
    int fd, n, count = 0, i;
    fd = __openat(AT_FDCWD, "/proc/cpuinfo", O_RDONLY, 0);
    if (fd < 0) return 0;
    n = (int)__read(fd, buf, (int)sizeof(buf));
    __close(fd);
    if (n <= 0) return 0;
    for (i = 0; i + 8 < n; i++)
        if (buf[i] == 'p' && buf[i + 1] == 'r' && buf[i + 2] == 'o' &&
            buf[i + 3] == 'c' && buf[i + 4] == 'e' && buf[i + 5] == 's' &&
            buf[i + 6] == 's' && buf[i + 7] == 'o' && buf[i + 8] == 'r')
            count++;
    if (count < 1) count = 4;   /* 解析失败按 4 核降级 */
    if (count > TOY_RENDER_MAX_WORKERS) count = TOY_RENDER_MAX_WORKERS;
    return count;
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
                depth[x] = 0x7fffffff;
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
    for (int i = 0; i < renderer->cmd_count; i++) {
        const struct toy_raster_cmd *cmd = &renderer->cmds[i];
        if (cmd->bbox_maxy >= top0 && cmd->bbox_miny <= bot0) {
            int y0 = cmd->bbox_miny > top0 ? cmd->bbox_miny : top0;
            int y1 = cmd->bbox_maxy < bot0 ? cmd->bbox_maxy : bot0;
            if (y0 <= y1) rasterize_cmd(renderer, cmd, y0, y1, worker);
        }
        if (cmd->bbox_maxy >= top1 && cmd->bbox_miny <= bot1) {
            int y0 = cmd->bbox_miny > top1 ? cmd->bbox_miny : top1;
            int y1 = cmd->bbox_maxy < bot1 ? cmd->bbox_maxy : bot1;
            if (y0 <= y1) rasterize_cmd(renderer, cmd, y0, y1, worker);
        }
    }
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
    __sync_fetch_and_add((int *)&renderer->job_done_count, 1);
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
        if (renderer->job_is_clear)
            worker_clear(renderer, worker->id);
        else
            worker_rasterize(renderer, worker->id, worker);
        __sync_synchronize();
        __sync_fetch_and_add((int *)&renderer->job_done_count, 1);
    }
    return NULL;
}

static int ensure_workers(struct toy_renderer *renderer)
{
    int n, i;
    if (renderer->workers) return 0;
    n = count_processors();
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
    renderer->job_is_clear = is_clear;
    renderer->job_clear_color = clear_color;
    renderer->job_done_count = 0;
    __sync_synchronize();
    renderer->job_generation++;
    __sync_synchronize();
    __futex((unsigned int *)&renderer->job_generation, TOY_FUTEX_WAKE,
            0x7fffffff, NULL, NULL, 0);
    while (renderer->job_done_count != renderer->worker_count)
        __sync_synchronize();
}

static void clear_single(struct toy_renderer *renderer, uint32_t clear_color)
{
    for (int y = 0; y < renderer->surface.height; y++) {
        uint32_t *row = (uint32_t *)((unsigned char *)renderer->surface.pixels +
                                     y * renderer->surface.stride);
        int *depth = renderer->depth + y * renderer->surface.width;
        for (int x = 0; x < renderer->surface.width; x++) {
            row[x] = clear_color;
            depth[x] = 0x7fffffff;
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
    /* Keep this self-host friendly: avoid a whole-structure assignment here. */
    renderer->surface.pixels = surface->pixels;
    renderer->surface.width = surface->width;
    renderer->surface.height = surface->height;
    renderer->surface.stride = surface->stride;
    if (ensure_workers(renderer) == 0)
        renderer_dispatch(renderer, 1, clear_color);
    else
        clear_single(renderer, clear_color);
    return 0;
}

int toy_renderer_flush(struct toy_renderer *renderer)
{
    long total = 0;
    unsigned long tex = 0, fallback = 0;
    if (!renderer) return 0;
    if (renderer->worker_count > 0) {
        renderer_dispatch(renderer, 0, 0);
        /* 命令已被本次 flush 消费：清零后下一条记录从空列表开始，
         * 每帧第二次 flush 不会重复光栅化整个场景。 */
        renderer->cmd_count = 0;
        for (int i = 0; i < renderer->worker_count; i++) {
            total += renderer->workers[i].pixels;
            tex += renderer->workers[i].textured_pixels;
            fallback += renderer->workers[i].texture_fallback_pixels;
        }
    } else {
        /* 单线程降级：整屏一条带，统计进栈上 worker 壳。 */
        struct toy_render_worker local;
        memset(&local, 0, sizeof(local));
        local.renderer = renderer;
        for (int i = 0; i < renderer->cmd_count; i++) {
            const struct toy_raster_cmd *cmd = &renderer->cmds[i];
            rasterize_cmd(renderer, cmd, cmd->bbox_miny, cmd->bbox_maxy,
                          &local);
        }
        total = local.pixels;
        tex = local.textured_pixels;
        fallback = local.texture_fallback_pixels;
        renderer->cmd_count = 0;
    }
    renderer->textured_pixels += tex;
    renderer->texture_fallback_pixels += fallback;
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
    if (renderer->depth) tlibc_free(renderer->depth);
    memset(renderer, 0, sizeof(struct toy_renderer));
}
