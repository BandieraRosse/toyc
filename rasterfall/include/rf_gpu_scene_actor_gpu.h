#ifndef RF_GPU_SCENE_ACTOR_GPU_H
#define RF_GPU_SCENE_ACTOR_GPU_H

#include <stdint.h>

#define RF_GPU_SCENE_ACTOR_MAX_DRAWS 64U

struct camera;
struct rf_gpu_graphics;
struct rf_gpu_graphics_batch_item;
struct rf_gpu_scene_pose_v1;
struct rf_gpu_scene_actor_gpu;

/* Prepare one frozen rifleman body, passive gear and active weapon for a Scene WORLD batch.
 * The caller retires the synchronous diagnostic draw before finish. */
struct rf_gpu_scene_actor_gpu *rf_gpu_scene_actor_gpu_create(
    struct rf_gpu_graphics *graphics);
int rf_gpu_scene_actor_gpu_prepare(struct rf_gpu_scene_actor_gpu *actor,
    const struct rf_gpu_scene_pose_v1 *pose,const struct camera *camera,
    uint32_t width,uint32_t height,
    struct rf_gpu_graphics_batch_item *items,uint32_t capacity,uint32_t *count);
void rf_gpu_scene_actor_gpu_finish(struct rf_gpu_scene_actor_gpu *actor);
void rf_gpu_scene_actor_gpu_destroy(struct rf_gpu_scene_actor_gpu *actor);

#endif
