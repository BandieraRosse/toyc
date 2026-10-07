#ifndef RF_GPU_SCENE_ACTOR_GPU_H
#define RF_GPU_SCENE_ACTOR_GPU_H

#include <stdint.h>
#include "rasterfall_character.h"

#define RF_GPU_SCENE_ACTOR_MAX_DRAWS 64U

struct camera;
struct rf_gpu_graphics;
struct rf_gpu_graphics_batch_item;
struct rf_gpu_scene_pose_v1;
struct rf_gpu_scene_actor_gpu;
/* Canonical appearance only: no identity, frame, palette or world transform.
 * Every member is uint32_t; unused catalog lanes are zeroed by the builder. */
struct rf_gpu_scene_actor_appearance {
    uint32_t character,body,bones,attachments,clothing,hidden_materials;
    uint32_t shirt,pants,bind_normals,weapon,material,linear_filter;
    uint32_t gear[RASTERFALL_CHARACTER_RECIPE_ATTACHMENTS][2];
    uint32_t clothes[RASTERFALL_CHARACTER_RECIPE_CLOTHING];
};
int rf_gpu_scene_actor_gunner_appearance(const struct rf_gpu_scene_pose_v1 *pose,
    struct rf_gpu_scene_actor_appearance *key);
/* Presentation-only controls, applied at the next Scene preparation. */
int rf_gpu_scene_character_material_enabled(void);
void rf_gpu_scene_character_material_set(int enabled);
int rf_gpu_scene_linear_filter_enabled(void);
void rf_gpu_scene_linear_filter_set(int enabled);

/* Prepare one frozen rifleman body, passive gear and active weapon for a Scene WORLD batch.
 * The caller retires the synchronous diagnostic draw before finish. */
struct rasterfall_resource_pool;
struct rf_gpu_scene_actor_gpu *rf_gpu_scene_actor_gpu_create(
    struct rf_gpu_graphics *graphics,struct rasterfall_resource_pool *pool);
int rf_gpu_scene_actor_gpu_prepare(struct rf_gpu_scene_actor_gpu *actor,
    const struct rf_gpu_scene_pose_v1 *pose,const struct camera *camera,
    uint32_t width,uint32_t height,
    struct rf_gpu_graphics_batch_item *items,uint32_t capacity,uint32_t *count);
void rf_gpu_scene_actor_gpu_set_quiet(struct rf_gpu_scene_actor_gpu *actor,int quiet);
void rf_gpu_scene_actor_gpu_finish(struct rf_gpu_scene_actor_gpu *actor);
/* Retained CPU upload arrays plus actor bookkeeping, excludes catalog models,
 * allocator overhead and GPU/driver allocations. */
uint64_t rf_gpu_scene_actor_gpu_cpu_buffer_bytes(const struct rf_gpu_scene_actor_gpu *actor);
/* A cancelled skin batch may leave staging copies unsubmitted. */
void rf_gpu_scene_actor_gpu_invalidate_bind(struct rf_gpu_scene_actor_gpu *actor);
void rf_gpu_scene_actor_gpu_destroy(struct rf_gpu_scene_actor_gpu *actor);

#endif
