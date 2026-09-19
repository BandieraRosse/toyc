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
int rf_gpu_vulkan_raster_segment(struct rf_gpu_vulkan_context *context,
    void *raster, const void *stream, unsigned long stream_size,
    const void *texture_descs, unsigned int texture_count,
    const void *texture_texels, unsigned long texture_bytes,
    unsigned int first, unsigned int end, enum rf_gpu_raster_load load,
    int final, unsigned int *color, int *depth,
    unsigned int width, unsigned int height,
    unsigned int color_stride, unsigned int depth_stride,
    char *message, unsigned long message_capacity);

#endif
