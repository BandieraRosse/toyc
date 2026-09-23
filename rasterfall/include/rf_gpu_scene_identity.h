#ifndef RF_GPU_SCENE_IDENTITY_H
#define RF_GPU_SCENE_IDENTITY_H

#include <stdint.h>
#include "toy_game.h"
#include "rasterfall_camera.h"

#define RF_GPU_SCENE_SNAPSHOT_ABI_V1 1U

/* Presentation identity only. Slot indexes are lookup positions, never
 * instance IDs; gameplay and network state retain their existing owners. */
enum rf_gpu_scene_actor_source {
    RF_GPU_SCENE_ACTOR_GAME = 1,
    RF_GPU_SCENE_ACTOR_NETWORK = 2,
    RF_GPU_SCENE_ACTOR_MANAGED = 3
};

struct rf_gpu_scene_actor_identity {
    uint32_t source;
    uint32_t source_id;
    uint32_t generation;
};

struct rf_gpu_scene_actor_identity_slot {
    uint32_t source, source_id, generation;
    unsigned char active;
};

struct rf_gpu_scene_identity_tracker {
    struct rf_gpu_scene_actor_identity_slot slots[TOY_GAME_MAX_ACTORS];
    uint64_t last_frame_id, last_world_generation;
    uint32_t last_generation;
};

/* The caller supplies the complete slot view after presentation interpolation.
 * Inactive entries are required so disappearance advances identity state. */
struct rf_gpu_scene_actor_input_v1 {
    uint32_t source, source_id;
    int active, visible;
    int x, y, z, sy, cy;
    int animation_id, animation_time_ms, weapon;
};

struct rf_gpu_scene_actor_v1 {
    struct rf_gpu_scene_actor_identity identity;
    uint32_t source_slot;
    int visible;
    int x, y, z, sy, cy;
    int animation_id, animation_time_ms, weapon;
};

struct rf_gpu_scene_camera_v1 {
    int x, y, z, sy, cy, pitch_sy, pitch_cy;
};

/* First value-only slice of PresentationSnapshot V1. No game, CPU model,
 * resource or GPU pointers survive the build. Future fields require a new
 * ABI version and byte_size check at the consumer. */
struct rf_gpu_scene_snapshot_v1 {
    uint32_t abi_version, byte_size;
    uint64_t frame_id, world_generation;
    uint32_t width, height, actor_first, actor_count;
    struct rf_gpu_scene_camera_v1 camera;
    /* V1's frame-owned arena is embedded at fixed capacity. Consumers use
     * actor_first/actor_count and never infer live actors from capacity. */
    struct rf_gpu_scene_actor_v1 actors[TOY_GAME_MAX_ACTORS];
};

/* Transactional: failure changes neither tracker nor output. Actors retain
 * source slot order. Supply every slot, including inactive ones. A source
 * identity present in consecutive frames retains its generation across slot
 * moves; disappearance or a new world allocates a fresh generation. Zero-init
 * the tracker once. The same frame_id cannot be frozen twice. */
int rf_gpu_scene_snapshot_build_v1(
    struct rf_gpu_scene_identity_tracker *tracker,
    const struct rf_gpu_scene_actor_input_v1 input[TOY_GAME_MAX_ACTORS],
    const struct camera *camera, uint64_t frame_id, uint64_t world_generation,
    uint32_t width, uint32_t height, struct rf_gpu_scene_snapshot_v1 *out);

#endif
