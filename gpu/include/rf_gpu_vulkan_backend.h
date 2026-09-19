#ifndef RF_GPU_VULKAN_BACKEND_H
#define RF_GPU_VULKAN_BACKEND_H

#include "rf_gpu.h"

/* Vulkan handles remain private to the hosted backend implementation. */
struct rf_gpu_vulkan_context {
    void *implementation;
    struct rf_gpu_native_window native_window;
    /* Hosted HG-2 proof requests a joint graphics/compute queue. */
    unsigned int require_graphics;
};

struct rf_gpu_mixed_gpu_timing {
    double raster_ms, bridge_import_ms, draw_ms, bridge_export_ms;
    double post_ms, overlay_ms, present_copy_ms;
    unsigned int supported, valid;
};

extern const struct rf_gpu_backend rf_gpu_vulkan_backend;

/* HG-2B hosted segmented consumer. Every call validates the COMPLETE Raster
 * ABI stream and texture table; [first,end) only selects execution. CLEAR
 * starts at zero and includes both clear commands. LOAD requires a successful
 * non-final segment on this target. VIEWMODEL must stay wholly in the final
 * segment. Intermediate segments do no Post, overlay, present or readback.
 * Final output is diagnostic readback; normal-frame integration is separate.
 * Rejection before submission preserves existing contents; execution failure
 * invalidates continuation. Resize creates a new, invalid target. */
enum rf_gpu_raster_load { RF_GPU_RASTER_CLEAR, RF_GPU_RASTER_LOAD_EXISTING };
/* Complete stream/texture/device limits and binning, no target writes. */
int rf_gpu_vulkan_raster_preflight(struct rf_gpu_vulkan_context *context,
    void *raster, const void *stream, unsigned long stream_size,
    const void *texture_descs, unsigned int texture_count,
    const void *texture_texels, unsigned long texture_bytes,
    unsigned int width, unsigned int height);
int rf_gpu_vulkan_raster_segment(struct rf_gpu_vulkan_context *context,
    void *raster, const void *stream, unsigned long stream_size,
    const void *texture_descs, unsigned int texture_count,
    const void *texture_texels, unsigned long texture_bytes,
    unsigned int first, unsigned int end, enum rf_gpu_raster_load load,
    int final, unsigned int *color, int *depth,
    unsigned int width, unsigned int height,
    unsigned int color_stride, unsigned int depth_stride,
    char *message, unsigned long message_capacity);
int rf_gpu_vulkan_raster_segment_present(struct rf_gpu_vulkan_context *context,
    void *raster, const void *stream, unsigned long stream_size,
    const void *texture_descs, unsigned int texture_count,
    const void *texture_texels, unsigned long texture_bytes,
    unsigned int first, unsigned int end, enum rf_gpu_raster_load load,
    const unsigned int *overlay_color, const unsigned char *overlay_coverage,
    unsigned int overlay_stride, unsigned int coverage_stride,
    unsigned int width, unsigned int height,
    struct rf_gpu_native_present_timing *timing,
    unsigned int *capture_color,
    char *message, unsigned long message_capacity);
void rf_gpu_vulkan_mixed_gpu_timing(void *raster,
    struct rf_gpu_mixed_gpu_timing *timing);

#endif
