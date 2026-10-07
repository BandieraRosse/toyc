#ifndef RF_GPU_SCENE_BLOCK_H
#define RF_GPU_SCENE_BLOCK_H
#include "rf_gpu_scene_enemy.h"

/* Immutable topology and sampled pose have separate lifetimes. No GPU handles,
 * mutable instances or gameplay pointers cross this producer boundary. */
#define RF_GPU_SCENE_BLOCK_BONES (RF_GPU_SCENE_POSE_BONES+2)
struct rf_gpu_scene_block_vertex {
    int position[3];
    unsigned bone0,bone1,weight,type;
};
typedef int (*rf_gpu_scene_block_triangle_fn)(void *,
    const struct rf_gpu_scene_block_vertex *,unsigned color,int flash);
struct rf_gpu_scene_block_pose {
    unsigned bone_count;
    int origin[3],flash;
    struct rasterfall_model_skin_palette_bone palette[RF_GPU_SCENE_BLOCK_BONES];
};
/* Returns 1 for the standard Block carrier, 0 for planar/CPU-lit diagnostics. */
int rf_gpu_scene_block_supported(const struct rf_gpu_scene_procedural_item_v1 *item);
int rf_gpu_scene_block_bind(const struct rf_gpu_scene_procedural_item_v1 *item,
    rf_gpu_scene_block_triangle_fn emit,void *context);
int rf_gpu_scene_block_sample(const struct rf_gpu_scene_procedural_item_v1 *item,
    struct rf_gpu_scene_block_pose *out);
#endif
