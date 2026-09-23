#ifndef RF_GPU_SCENE_FRAME_H
#define RF_GPU_SCENE_FRAME_H

#include "rf_gpu_scene_identity.h"

#define RF_GPU_SCENE_SNAPSHOT_ABI_V2 2U
#define RF_GPU_SCENE_WORLD_ID_CAP 64U
#define RF_GPU_SCENE_WORLD_KIND_CAP 32U
#define RF_GPU_SCENE_MAX_WORLD_V2 128U
#define RF_GPU_SCENE_MAX_TRANSIENT_V2 2048U

/* World inputs follow the Runtime Map projection order, including its
 * legacy_index ordering. ID is the authored render ID, never the array slot. */
struct rf_gpu_scene_world_input_v2 {
    char id[RF_GPU_SCENE_WORLD_ID_CAP];
    char kind[RF_GPU_SCENE_WORLD_KIND_CAP];
    uint32_t submission_ordinal;
    int visible, x, y, z, alpha;
};

/* local_id is unique within source in this frame. A one-frame effect may use
 * its submission index; a longer-lived producer may supply its own ID. */
struct rf_gpu_scene_transient_input_v2 {
    uint32_t source, local_id, kind, submission_ordinal;
    int visible, x, y, z, alpha, age_ms;
};

struct rf_gpu_scene_snapshot_v2 {
    uint32_t abi_version, byte_size;
    uint64_t frame_id, world_generation, map_generation;
    uint32_t width, height, actor_first, actor_count;
    uint32_t world_first, world_count, transient_first, transient_count;
    int air_walls_enabled;
    struct rf_gpu_scene_camera_v1 camera;
    struct rf_gpu_scene_actor_v1 actors[TOY_GAME_MAX_ACTORS];
    uint32_t actor_submission_ordinals[TOY_GAME_MAX_ACTORS];
    struct rf_gpu_scene_world_input_v2 world[RF_GPU_SCENE_MAX_WORLD_V2];
    struct rf_gpu_scene_transient_input_v2 transient[RF_GPU_SCENE_MAX_TRANSIENT_V2];
};

/* The caller freezes one presentation time and supplies already projected
 * value inputs. Failure leaves both tracker and output unchanged. */
int rf_gpu_scene_snapshot_build_v2(
    struct rf_gpu_scene_identity_tracker *tracker,
    const struct rf_gpu_scene_actor_input_v1 actors[TOY_GAME_MAX_ACTORS],
    const uint32_t actor_submission_ordinals[TOY_GAME_MAX_ACTORS],
    const struct camera *camera, uint64_t frame_id, uint64_t world_generation,
    uint64_t map_generation, uint32_t width, uint32_t height,
    int air_walls_enabled,
    const struct rf_gpu_scene_world_input_v2 *world, uint32_t world_count,
    const struct rf_gpu_scene_transient_input_v2 *transient,
    uint32_t transient_count, struct rf_gpu_scene_snapshot_v2 *out);

#endif
