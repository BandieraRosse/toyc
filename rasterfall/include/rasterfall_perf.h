#ifndef RASTERFALL_PERF_H
#define RASTERFALL_PERF_H

#include "toy_renderer.h"

#define RASTERFALL_STATS_WINDOW_US 5000000L
#define RASTERFALL_STATS_RING_SIZE 1024
#define RASTERFALL_STATS_STAGE_MAX 7

enum rasterfall_stats_stage {
    RASTERFALL_STATS_LOGIC = 0,
    RASTERFALL_STATS_BEGIN,
    RASTERFALL_STATS_SCENE,
    RASTERFALL_STATS_ENEMIES,
    RASTERFALL_STATS_RASTER,
    RASTERFALL_STATS_OVERLAY,
    RASTERFALL_STATS_PRESENT
};

struct rasterfall_perf_stats {
    long window_start;
    int frames;
    long stall_us;
    long wall_us;
    long stage_us[RASTERFALL_STATS_STAGE_MAX];
    unsigned long stage_tris[RASTERFALL_STATS_STAGE_MAX];
    unsigned long stage_pixels[RASTERFALL_STATS_STAGE_MAX];
    int ring[RASTERFALL_STATS_RING_SIZE];
    int ring_count;
    int ring_head;
    long frame_sum;
    long frame_max;
    unsigned long raster_bbox_px;
    unsigned long raster_inside_px;
    unsigned long raster_flat_tris;
    unsigned long raster_tex_tris;
    unsigned long raster_flat_px;
    unsigned long raster_tex_px;
    long raster_flat_us;
    long raster_tex_us;
};

void rasterfall_perf_init(struct rasterfall_perf_stats *stats);
void rasterfall_perf_end_stage(struct rasterfall_perf_stats *window,
                               struct rasterfall_perf_stats *total,
                               int stage, long *stage_start,
                               unsigned long tris, unsigned long pixels);
void rasterfall_perf_record_frame(struct rasterfall_perf_stats *window,
                                  struct rasterfall_perf_stats *total, long us);
void rasterfall_perf_add_stall(struct rasterfall_perf_stats *window,
                               struct rasterfall_perf_stats *total, long us);
void rasterfall_perf_add_interval(struct rasterfall_perf_stats *window,
                                  struct rasterfall_perf_stats *total,
                                  long wall_us);
void rasterfall_perf_add_raster(struct rasterfall_perf_stats *window,
                                struct rasterfall_perf_stats *total,
                                const struct toy_renderer *renderer,
                                unsigned long tris, unsigned long pixels);
void rasterfall_perf_dump(const struct rasterfall_perf_stats *stats,
                          const char *label);

#endif
