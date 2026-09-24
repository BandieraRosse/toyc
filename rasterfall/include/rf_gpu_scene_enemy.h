#ifndef RF_GPU_SCENE_ENEMY_H
#define RF_GPU_SCENE_ENEMY_H
#include "toy_game.h"
#include "rasterfall_enemy_rig.h"
#include "rasterfall_world_light.h"
struct rf_gpu_scene_enemy_point {
    int x, y, z;
    int form_light_q8, light_min_q8, light_max_q8, double_sided;
};
#define RF_GPU_SCENE_ENEMY_MAX_TRIANGLES 2048U

/* Diagnostic presentation values, scoped to one frame. A source slot is an
 * ordinal, not a persistent enemy identity. No gameplay/cache pointers escape. */
struct rf_gpu_scene_enemy_item_v1 {
    unsigned source_slot;
    int type, x, z, dir_x, dir_z, lift, scene_light_q8;
    unsigned feedback;
    struct enemy_rig_pose pose;
    /* Zero selects rigid special; 1..6 selects the immutable infected recipe.
     * Freeze the sampled stride, never query motion history during extraction. */
    int infected_recipe, infected_swing, infected_bind;
};
struct rf_gpu_scene_enemy_frame_v1 {
    uint64_t frame_id, world_generation;
    unsigned count, deferred, culled;
    int vertex_lighting;
    struct rasterfall_world_lighting lighting;
    struct rf_gpu_scene_enemy_item_v1 items[TOY_GAME_MAX_ENEMIES];
};
/* Capture the actual sampled living enemy body pose during normal render;
 * begin clears even frames where WORLD is not rendered. Freeze ends capture. */
void rf_gpu_scene_enemy_begin(uint64_t frame_id, uint64_t world_generation);
int rf_gpu_scene_enemy_freeze(struct rf_gpu_scene_enemy_frame_v1 *out);
typedef int (*rf_gpu_scene_enemy_triangle_fn)(void *context,
    const struct rf_gpu_scene_enemy_point *a,const struct rf_gpu_scene_enemy_point *b,const struct rf_gpu_scene_enemy_point *c,unsigned color);
int rf_gpu_scene_enemy_triangles(const struct rf_gpu_scene_enemy_item_v1 *item,
    rf_gpu_scene_enemy_triangle_fn emit,void *context);
#endif
