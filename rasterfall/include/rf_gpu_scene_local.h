#ifndef RF_GPU_SCENE_LOCAL_H
#define RF_GPU_SCENE_LOCAL_H
#include "rf_gpu_scene_extract.h"

/* Session-owned roster source. Register at creation, retire at reset/unload.
 * Never infer a birth by comparing actor bytes or by observing slot reuse. */
struct rf_gpu_scene_local_source {
    uint64_t next_epoch, world_generation, frame_id;
    uint64_t epoch;
    int actor_id, active, failed;
    uint64_t lower_time_ms;
    int lower_walk, last_animation, last_time_ms, clock_character;
    struct rf_gpu_scene_identity_tracker tracker;
};

/* Value inputs for the existing action/pose/socket evaluator. A finalized
 * palette is deliberately not fabricated here; it remains extraction-owned. */
struct rf_gpu_scene_local_presentation {
    struct rf_gpu_scene_actor_identity identity;
    int character_id, state, moving, ground_y, airborne_y;
    int pitch_sy, pitch_cy, locomotion_blend_ms, muzzle_flash_ms;
    uint64_t lower_time_ms;
    int lower_walk;
};
struct rf_gpu_scene_local_frame {
    struct rf_gpu_scene_snapshot_v2 snapshot;
    struct rf_gpu_scene_local_presentation presentation;
};
int rf_gpu_scene_local_world(struct rf_gpu_scene_local_source *source);
int rf_gpu_scene_local_created(struct rf_gpu_scene_local_source *source,
                               const struct toy_game_actor *actor);
void rf_gpu_scene_local_destroyed(struct rf_gpu_scene_local_source *source);
int rf_gpu_scene_local_freeze(struct rf_gpu_scene_local_source *source,
    const struct toy_game *game, const struct camera *camera,
    uint32_t width, uint32_t height, int air_walls_enabled,
    struct rf_gpu_scene_local_frame *out);
#endif
