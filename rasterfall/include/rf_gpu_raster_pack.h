#ifndef RASTERFALL_RF_GPU_RASTER_PACK_H
#define RASTERFALL_RF_GPU_RASTER_PACK_H

#include "rf_gpu_raster_abi.h"

struct toy_renderer;

enum rf_gpu_raster_pack_result {
    RF_GPU_RASTER_PACK_OK = 0,
    RF_GPU_RASTER_PACK_INVALID = -1,
    RF_GPU_RASTER_PACK_CAPACITY = -2,
    RF_GPU_RASTER_PACK_UNSUPPORTED = -3
};

size_t rf_gpu_raster_stream_size_v1(uint32_t command_count);
int rf_gpu_raster_pack_toy_v1(const struct toy_renderer *renderer,
                              uint32_t clear_color, int32_t clear_depth,
                              void *destination, size_t destination_size,
                              size_t *written_size);
int rf_gpu_raster_validate_v1(const void *stream, size_t stream_size);

#endif
