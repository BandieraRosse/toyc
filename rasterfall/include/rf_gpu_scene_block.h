#ifndef RF_GPU_SCENE_BLOCK_H
#define RF_GPU_SCENE_BLOCK_H
#include "rf_gpu_character.h"

/* Returns 1 for the standard Block carrier, 0 for planar/CPU-lit diagnostics. */
int rf_gpu_scene_block_supported(const struct rf_gpu_scene_procedural_item_v1 *item);
int rf_gpu_scene_block_bind(const struct rf_gpu_scene_procedural_item_v1 *item,
    rf_gpu_character_triangle_fn emit,void *context);
int rf_gpu_scene_block_sample(const struct rf_gpu_scene_procedural_item_v1 *item,
    struct rf_gpu_character_pose *out);
#endif
