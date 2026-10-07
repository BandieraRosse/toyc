#ifndef RF_GPU_CHARACTER_H
#define RF_GPU_CHARACTER_H
#include "rf_gpu_scene_enemy.h"

/* Producer contract: immutable topology plus an independently owned palette.
 * Flags describe surfaces; neither gameplay nor GPU ownership crosses here. */
#define RF_GPU_CHARACTER_BONES (RF_GPU_SCENE_POSE_BONES+2)
#define RF_GPU_CHARACTER_FLASH 1
#define RF_GPU_CHARACTER_DOUBLE_SIDED 2
#define RF_GPU_CHARACTER_NORMALS 4
#define RF_GPU_CHARACTER_FORM_LIGHT 8
struct rf_gpu_character_vertex {
    int position[3],normal[3];
    unsigned bone0,bone1,weight,type;
};
typedef int (*rf_gpu_character_triangle_fn)(void *,
    const struct rf_gpu_character_vertex *,unsigned color,int flags);
struct rf_gpu_character_pose {
    unsigned bone_count;
    int origin[3],flash;
    struct rasterfall_model_skin_palette_bone palette[RF_GPU_CHARACTER_BONES];
};
int rf_gpu_scene_infected_supported(const struct rf_gpu_scene_enemy_item_v1 *item);
int rf_gpu_scene_infected_bind(const struct rf_gpu_scene_enemy_item_v1 *item,
    rf_gpu_character_triangle_fn emit,void *context);
int rf_gpu_scene_infected_sample(const struct rf_gpu_scene_enemy_item_v1 *item,
    struct rf_gpu_character_pose *out);
#endif
