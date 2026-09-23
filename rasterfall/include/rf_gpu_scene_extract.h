#ifndef RF_GPU_SCENE_EXTRACT_H
#define RF_GPU_SCENE_EXTRACT_H

#include "rf_gpu_scene_frame.h"

#define RF_GPU_SCENE_FROZEN_ABI_V1 1U
#define RF_GPU_SCENE_MAX_ITEMS_V1 \
    (TOY_GAME_MAX_ACTORS + RF_GPU_SCENE_MAX_WORLD_V2 + RF_GPU_SCENE_MAX_TRANSIENT_V2)

enum rf_gpu_scene_item_source_v1 {
    RF_GPU_SCENE_ITEM_WORLD = 1,
    RF_GPU_SCENE_ITEM_ACTOR = 2,
    RF_GPU_SCENE_ITEM_TRANSIENT = 3
};

/* This first frozen Scene slice carries stable semantic identity, position
 * and submission order. Resource binding, pose and pass classification are
 * later steps; this is not yet an executable GPU frame. */
struct rf_gpu_scene_item_v1 {
    uint32_t source_kind, submission_ordinal, source_index;
    uint32_t kind, visible;
    int x, y, z, alpha;
    struct rf_gpu_scene_actor_identity actor;
    char world_id[RF_GPU_SCENE_WORLD_ID_CAP];
    char world_kind[RF_GPU_SCENE_WORLD_KIND_CAP];
    uint32_t transient_source, transient_local_id;
};

struct rf_gpu_scene_frozen_v1 {
    uint32_t abi_version, byte_size;
    uint64_t frame_id, world_generation, map_generation;
    uint32_t width, height, item_first, item_count;
    struct rf_gpu_scene_item_v1 items[RF_GPU_SCENE_MAX_ITEMS_V1];
};

/* Transactional: invalid header or duplicate cross-source ordinals leave
 * output unchanged. Input content must come from snapshot_build_v2. */
int rf_gpu_scene_extract_v1(const struct rf_gpu_scene_snapshot_v2 *snapshot,
                            struct rf_gpu_scene_frozen_v1 *out);

#endif
