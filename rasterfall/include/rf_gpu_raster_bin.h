#ifndef RASTERFALL_RF_GPU_RASTER_BIN_H
#define RASTERFALL_RF_GPU_RASTER_BIN_H

struct rf_gpu_raster_tile_stats {
    unsigned int command_count;
    unsigned int tile_count;
    unsigned long long total_refs;
    unsigned int max_refs_per_tile;
};

struct rf_gpu_raster_tile_lists {
    unsigned int *offsets;
    /* Each tile's [offsets[t], offsets[t+1]) slice is strictly increasing in
     * command index. Raster segment lower_bound/early-stop relies on this. */
    unsigned int *indices;
    unsigned long long offsets_capacity;
    unsigned long long indices_capacity;
    struct rf_gpu_raster_tile_stats stats;
};

int rf_gpu_raster_bin_v1(const void *stream, unsigned long stream_size,
                         unsigned int tile_width, unsigned int tile_height,
                         struct rf_gpu_raster_tile_lists *lists);
void rf_gpu_raster_tile_lists_destroy(struct rf_gpu_raster_tile_lists *lists);

#endif
