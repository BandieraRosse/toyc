#ifndef RF_GPU_MIXED_EXECUTOR_H
#define RF_GPU_MIXED_EXECUTOR_H
#include "rf_core_mixed_frame.h"
#include "rf_gpu_resource_cache.h"

/* Hosted synchronous HG-2B consumer. Owns graphics/cache/raster targets, not
 * the service or registry. Destroy before either borrowed owner. Diagnostic
 * final diagnostic readback or native present; no normal producer or CPU lowering. */
struct rf_gpu_mixed_executor;
struct rf_gpu_mixed_output {
    uint32_t clear_color;
    struct rf_gpu_post_params_v1 post;
    unsigned int *color;
    int *depth;
    unsigned int color_stride, depth_stride;
    /* Native output uses the existing GPU overlay/composite/presenter. */
    const unsigned int *overlay_color;
    const unsigned char *overlay_coverage;
    unsigned int overlay_stride, coverage_stride;
    struct rf_gpu_native_present_timing *present_timing;
    /* Optional explicit diagnostic readback of final Post/overlay color. */
    unsigned int *capture_color;
    /* Require a native GPU-only finish; reject diagnostic readback. */
    unsigned int strict_native;
};
struct rf_gpu_mixed_stats {
    uint64_t clears, raster_segments, draw_spans, draws, finishes, readback_bytes;
    double cache_collect_ms, preflight_ms, texture_measure_ms, pack_ms;
    double draw_encode_ms, draw_batch_prepare_ms, graphics_draw_ms;
    double raster_segment_ms;
    struct rf_gpu_mixed_gpu_timing gpu_timing;
    struct rf_gpu_graphics_stats graphics;
};
struct rf_gpu_mixed_executor *rf_gpu_mixed_create(struct rf_gpu *gpu,
    struct rf_gpu_vulkan_context *context,
    struct rasterfall_resource_registry *registry);
int rf_gpu_mixed_render(struct rf_gpu_mixed_executor *executor,
    struct rf_core_mixed_frame *frame, const struct rf_gpu_mixed_output *output);
void rf_gpu_mixed_get_stats(struct rf_gpu_mixed_executor *executor,
    struct rf_gpu_mixed_stats *stats);
void rf_gpu_mixed_destroy(struct rf_gpu_mixed_executor *executor);
#endif
