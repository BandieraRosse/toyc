#ifndef RASTERFALL_RF_GPU_RASTER_CPU_REF_H
#define RASTERFALL_RF_GPU_RASTER_CPU_REF_H

#include "tlibc_types.h"

struct rf_gpu_cpu_reference_timing {
    double raster_ms;
};

/* Decode Raster Command ABI V1 and execute it through the production
 * toy_renderer flat-triangle path. */
int rf_gpu_raster_cpu_reference_v1(const void *stream, size_t stream_size,
                                   uint32_t *color, int32_t *depth,
                                   uint32_t color_stride,
                                   uint32_t depth_stride,
                                   struct rf_gpu_cpu_reference_timing *timing);

#endif
