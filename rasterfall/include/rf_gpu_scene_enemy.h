#ifndef RF_GPU_SCENE_ENEMY_H
#define RF_GPU_SCENE_ENEMY_H
#include "toy_game.h"
#include "rasterfall_enemy_rig.h"
#include "rasterfall_world_light.h"
#include "rasterfall_render.h"
#include "rf_gpu_scene_pose.h"
struct rf_gpu_scene_procedural_item_v1 {
    struct rasterfall_procedural_humanoid_state state;
    unsigned body_color, leg_color, skin_color, hair_color;
    int scene_light_q8, vertex_lighting, double_sided;
};
struct rf_gpu_scene_enemy_point {
    int x, y, z;
    int form_light_q8, light_min_q8, light_max_q8, double_sided;
    int light_override_plus_one;
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
    int squash, transformed, pivot_x, pivot_y, pivot_z, roll_sin, roll_cos;
    int transparent;
    int legacy_kind;
    int double_sided;
    int shadow, shadow_y, shadow_rx, shadow_rz;
    int tongue, mouth_x, mouth_y, mouth_z, target_x, target_y, target_z;
};
struct rf_gpu_scene_enemy_frame_v1 {
    uint64_t frame_id, world_generation;
    unsigned count, deferred, culled, transparent;
    int vertex_lighting;
    struct rasterfall_world_lighting lighting;
    struct rf_gpu_scene_enemy_item_v1 items[TOY_GAME_MAX_ENEMIES];
    unsigned procedural_count, failed;
    struct rf_gpu_scene_procedural_item_v1 procedural[TOY_GAME_MAX_ACTORS];
    /* Supplemental modular draws have frame-local diagnostic identities.
     * They do not claim persistent actor generations or network epochs. */
    unsigned modular_count;
    struct rf_gpu_scene_pose_v1 modular[TOY_GAME_MAX_ACTORS];
};
/* Capture the actual sampled living enemy body pose during normal render;
 * begin clears even frames where WORLD is not rendered. Freeze ends capture. */
void rf_gpu_scene_enemy_begin(uint64_t frame_id, uint64_t world_generation);
int rf_gpu_scene_enemy_freeze(struct rf_gpu_scene_enemy_frame_v1 *out);
typedef int (*rf_gpu_scene_enemy_triangle_fn)(void *context,
    const struct rf_gpu_scene_enemy_point *a,const struct rf_gpu_scene_enemy_point *b,const struct rf_gpu_scene_enemy_point *c,unsigned color);
int rf_gpu_scene_enemy_triangles(const struct rf_gpu_scene_enemy_item_v1 *item,
    rf_gpu_scene_enemy_triangle_fn emit,void *context);
int rf_gpu_scene_procedural_triangles(const struct rf_gpu_scene_procedural_item_v1 *item,
    rf_gpu_scene_enemy_triangle_fn emit,void *context);
#endif
