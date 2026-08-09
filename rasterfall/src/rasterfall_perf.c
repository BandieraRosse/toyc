#include "tlibc_everything.h"
#include "core.h"
#include "rasterfall_perf.h"

static long rasterfall_perf_monotonic_us(void)
{
    struct timespec now;
    if (__clock_gettime(CLOCK_MONOTONIC, &now) < 0) return 0;
    return now.tv_sec * 1000000L + now.tv_nsec / 1000;
}

/* ── 性能统计：分阶段三角形/顶点/耗时 + 帧时间分布 ─────────────
 * 每个统计窗口（默认 5 秒）向终端输出一次：窗口平均帧率、帧渲染
 * 时间均值/p95/p99/最长，以及 logic/begin/scene/enemies/raster/
 * overlay/present 各阶段平均每帧的三角形、顶点（提交三角形×3）、
 * 像素与耗时（及耗时占比）；退出时再输出一次全量汇总。
 * 帧渲染时间从申请缓冲（begin_frame）计到 present 提交结束；双缓冲
 * 被组合器占用时的等待单独计入 stall，不算渲染帧。--no-stats 关闭
 * 终端输出（采集仍然进行，开销可忽略）。 */

static const char *stats_stage_names[RASTERFALL_STATS_STAGE_MAX] = {
    "logic", "begin", "scene", "enemies", "raster", "overlay", "present"
};

void rasterfall_perf_init(struct rasterfall_perf_stats *s)
{
    memset(s, 0, sizeof(*s));
    s->window_start = rasterfall_perf_monotonic_us();
}

/* 阶段结束：把阶段耗时与三角形/像素计数同时累积进窗口与全量两个
 * 结构，stage_start 推进到当前时刻。 */
void rasterfall_perf_end_stage(struct rasterfall_perf_stats *window, struct rasterfall_perf_stats *total,
                           int stage, long *stage_start,
                           unsigned long tris, unsigned long pixels)
{
    long now = rasterfall_perf_monotonic_us();
    window->stage_us[stage] += now - *stage_start;
    total->stage_us[stage] += now - *stage_start;
    window->stage_tris[stage] += tris;
    total->stage_tris[stage] += tris;
    window->stage_pixels[stage] += pixels;
    total->stage_pixels[stage] += pixels;
    *stage_start = now;
}

static void ring_add(struct rasterfall_perf_stats *s, long us)
{
    s->ring[s->ring_head] = (int)us;
    s->ring_head = (s->ring_head + 1) % RASTERFALL_STATS_RING_SIZE;
    if (s->ring_count < RASTERFALL_STATS_RING_SIZE) s->ring_count++;
    s->frame_sum += us;
    if (us > s->frame_max) s->frame_max = us;
    s->frames++;
}

void rasterfall_perf_record_frame(struct rasterfall_perf_stats *window,
                              struct rasterfall_perf_stats *total, long us)
{
    ring_add(window, us);
    ring_add(total, us);
}

void rasterfall_perf_add_stall(struct rasterfall_perf_stats *window,
                           struct rasterfall_perf_stats *total, long us)
{
    window->stall_us += us;
    total->stall_us += us;
}

/* 帧间隔：wall 从一次 begin_frame 到下一次 begin_frame（含双缓冲等待、
 * 事件轮询、逻辑与调度）。按循环迭代累计（stall 迭代也算），除以渲染帧
 * 数即每渲染帧的均值（应≈1e6/fps）。wait 在 dump 中用 wall − 活跃帧
 * 时间推导，保证恒等对消。 */
void rasterfall_perf_add_interval(struct rasterfall_perf_stats *window, struct rasterfall_perf_stats *total,
                              long wall_us)
{
    window->wall_us += wall_us;
    total->wall_us += wall_us;
}

/* RASTER 阶段结束后读取渲染器最近一次 flush 的漏斗与路径快照。flat
 * 三角形/像素 = 本 flush 总数 − 纹理路径（last_tex_* 为覆盖式快照，
 * overlay 的第二次 flush 不重复计入）。 */
void rasterfall_perf_add_raster(struct rasterfall_perf_stats *window, struct rasterfall_perf_stats *total,
                            const struct toy_renderer *r,
                            unsigned long tris, unsigned long pixels)
{
    unsigned long tex_tris = r->last_tex_tris;
    unsigned long tex_px = r->last_tex_px;
    unsigned long flat_tris = tris > tex_tris ? tris - tex_tris : 0;
    unsigned long flat_px = pixels > tex_px ? pixels - tex_px : 0;
    window->raster_bbox_px += r->last_bbox_px;
    total->raster_bbox_px += r->last_bbox_px;
    window->raster_inside_px += r->last_inside_px;
    total->raster_inside_px += r->last_inside_px;
    window->raster_flat_tris += flat_tris;
    total->raster_flat_tris += flat_tris;
    window->raster_tex_tris += tex_tris;
    total->raster_tex_tris += tex_tris;
    window->raster_flat_px += flat_px;
    total->raster_flat_px += flat_px;
    window->raster_tex_px += tex_px;
    total->raster_tex_px += tex_px;
    window->raster_flat_us += r->last_flat_us;
    total->raster_flat_us += r->last_flat_us;
    window->raster_tex_us += r->last_tex_us;
    total->raster_tex_us += r->last_tex_us;
}

/* 排序副本上的最近秩百分位（us）：p95 即第 ceil(0.95*n) 个样本。 */
static long perf_percentile(const struct rasterfall_perf_stats *s, int pct)
{
    int tmp[RASTERFALL_STATS_RING_SIZE];
    int i, j, n = s->ring_count, idx;
    long v;
    if (n <= 0) return 0;
    for (i = 0; i < n; i++) tmp[i] = s->ring[i];
    for (i = 1; i < n; i++) {
        v = tmp[i];
        for (j = i - 1; j >= 0 && tmp[j] > v; j--) tmp[j + 1] = tmp[j];
        tmp[j + 1] = (int)v;
    }
    idx = n * pct / 100;
    if (idx >= n) idx = n - 1;
    return tmp[idx];
}

void rasterfall_perf_dump(const struct rasterfall_perf_stats *s, const char *label)
{
    long elapsed = rasterfall_perf_monotonic_us() - s->window_start;
    long fps10, avg_us, total_us, p95, p99;
    unsigned long tris_all = 0, pixels_all = 0;
    int i;
    if (s->frames <= 0 || elapsed <= 0) return;
    fps10 = (long)((long long)s->frames * 10 * 1000000 / elapsed);
    avg_us = s->frame_sum / s->frames;
    p95 = s->ring_count > 0 ? perf_percentile(s, 95) : 0;
    p99 = s->ring_count > 0 ? perf_percentile(s, 99) : 0;
    /* wait = wall − 活跃帧时间：present 到下一次 begin 的间隔（轮询/
     * 逻辑/调度/组合器背压），由对消恒等式推导，与各阶段统计严格一致 */
    {
        long wall_avg = s->wall_us / s->frames;
        long wait_avg = wall_avg - avg_us;
        if (wait_avg < 0) wait_avg = 0;
        __printf("[stats:%s] window=%ld.%03lds frames=%d fps=%ld.%ld "
                 "frame_us avg=%ld p95=%ld p99=%ld max=%ld "
                 "wall_us avg=%ld wait_us avg=%ld stall_ms=%ld\n",
                 label, elapsed / 1000000L, (elapsed % 1000000L) / 1000L,
                 s->frames, fps10 / 10, fps10 % 10,
                 avg_us, p95, p99, s->frame_max, wall_avg, wait_avg,
                 s->stall_us / 1000);
    }
    total_us = 0;
    for (i = 0; i < RASTERFALL_STATS_STAGE_MAX; i++) total_us += s->stage_us[i];
    if (total_us <= 0) total_us = 1;
    __printf("[stats:%s] %-7s %8s %8s %9s %10s %5s\n", label,
             "stage", "tris/f", "verts/f", "px/f", "us/f", "%time");
    for (i = 0; i < RASTERFALL_STATS_STAGE_MAX; i++) {
        unsigned long tris = s->stage_tris[i] / (unsigned long)s->frames;
        unsigned long px = s->stage_pixels[i] / (unsigned long)s->frames;
        long pct = s->stage_us[i] * 100 / total_us;
        tris_all += tris;
        pixels_all += px;
        __printf("[stats:%s] %-7s %8lu %8lu %9lu %10ld %4ld%%\n", label,
                 stats_stage_names[i], tris, tris * 3UL, px,
                 s->stage_us[i] / s->frames, pct);
    }
    __printf("[stats:%s] per-frame total tris=%lu verts=%lu pixels=%lu\n",
             label, tris_all, tris_all * 3UL, pixels_all);
    {
        /* 像素漏斗：bbox=包围盒扫描 → inside=边函数覆盖 → depth/shade/
         * write=深度通过/着色/写入。当前实现三者相等（=raster 阶段实际
         * 写入像素）；引入提前深度裁剪或增量扫描后会分叉。 */
        unsigned long bbox = s->raster_bbox_px / (unsigned long)s->frames;
        unsigned long inside = s->raster_inside_px / (unsigned long)s->frames;
        unsigned long written =
            s->stage_pixels[RASTERFALL_STATS_RASTER] / (unsigned long)s->frames;
        long ib10 = bbox > 0 ? (long)(inside * 1000 / bbox) : 0;
        long di10 = inside > 0 ? (long)(written * 1000 / inside) : 0;
        __printf("[stats:%s] funnel px/f bbox=%lu inside=%lu depth=%lu "
                 "shade=%lu write=%lu inside/bbox=%ld.%ld%% "
                 "depth/inside=%ld.%ld%%\n",
                 label, bbox, inside, written, written, written,
                 ib10 / 10, ib10 % 10, di10 / 10, di10 % 10);
    }
    {
        /* 纯色/纹理路径拆分：像素与三角形来自本帧主 flush 快照，
         * 耗时为各 worker 路径段计时之和（并行近似，与 stage 墙钟
         * 不同口径）。 */
        unsigned long flat_tris = s->raster_flat_tris / (unsigned long)s->frames;
        unsigned long tex_tris = s->raster_tex_tris / (unsigned long)s->frames;
        unsigned long flat_px = s->raster_flat_px / (unsigned long)s->frames;
        unsigned long tex_px = s->raster_tex_px / (unsigned long)s->frames;
        long flat_us = s->raster_flat_us / s->frames;
        long tex_us = s->raster_tex_us / s->frames;
        long path_us = flat_us + tex_us;
        long fpct = path_us > 0 ? flat_us * 100 / path_us : 0;
        __printf("[stats:%s] path tris/f flat=%lu tex=%lu px/f flat=%lu "
                 "tex=%lu us/f flat=%ld tex=%ld time flat=%ld%% tex=%ld%%\n",
                 label, flat_tris, tex_tris, flat_px, tex_px,
                 flat_us, tex_us, fpct, 100 - fpct);
    }
}
