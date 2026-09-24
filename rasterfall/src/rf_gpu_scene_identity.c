#include "rf_gpu_scene_identity.h"
#include <string.h>
#include "tlibc_everything.h"

int rf_gpu_scene_snapshot_build_v1(
    struct rf_gpu_scene_identity_tracker *tracker,
    const struct rf_gpu_scene_actor_input_v1 input[TOY_GAME_MAX_ACTORS],
    const struct camera *camera, uint64_t frame_id, uint64_t world_generation,
    uint32_t width, uint32_t height, struct rf_gpu_scene_snapshot_v1 *out)
{
    struct rf_gpu_scene_identity_tracker next;
    struct rf_gpu_scene_snapshot_v1 snapshot;
    unsigned int i, j;
    if (!tracker || !input || !camera || !out || !frame_id ||
        !world_generation || !width || !height ||
        frame_id <= tracker->last_frame_id ||
        world_generation < tracker->last_world_generation) return -1;
    next = *tracker;
    memset(next.slots, 0, sizeof(next.slots));
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.abi_version = RF_GPU_SCENE_SNAPSHOT_ABI_V1;
    snapshot.byte_size = (uint32_t)sizeof(snapshot);
    snapshot.frame_id = frame_id;
    snapshot.world_generation = world_generation;
    snapshot.width = width;
    snapshot.height = height;
    snapshot.camera.x = camera->x;
    snapshot.camera.y = camera->y;
    snapshot.camera.z = camera->z;
    snapshot.camera.sy = camera->sy;
    snapshot.camera.cy = camera->cy;
    snapshot.camera.pitch_sy = camera->pitch_sy;
    snapshot.camera.pitch_cy = camera->pitch_cy;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; ++i) {
        struct rf_gpu_scene_actor_v1 *actor;
        if (!input[i].active) continue;
        if (input[i].source < RF_GPU_SCENE_ACTOR_GAME ||
            input[i].source > RF_GPU_SCENE_ACTOR_MANAGED ||
            !input[i].source_epoch) return -1;
        for (j = 0; j < snapshot.actor_count; ++j)
            if (snapshot.actors[j].identity.source == input[i].source &&
                snapshot.actors[j].identity.source_id == input[i].source_id)
                return -1;
        actor = &snapshot.actors[snapshot.actor_count++];
        actor->identity.source = input[i].source;
        actor->identity.source_id = input[i].source_id;
        if (world_generation == tracker->last_world_generation) {
            for (j = 0; j < TOY_GAME_MAX_ACTORS; ++j)
                if (tracker->slots[j].active &&
                    tracker->slots[j].source == input[i].source &&
                    tracker->slots[j].source_id == input[i].source_id &&
                    tracker->slots[j].source_epoch == input[i].source_epoch) {
                    actor->identity.generation = tracker->slots[j].generation;
                    break;
                }
        }
        if (!actor->identity.generation) {
            if (next.last_generation == UINT32_MAX) return -1;
            actor->identity.generation = ++next.last_generation;
        }
        next.slots[i].source = input[i].source;
        next.slots[i].source_id = input[i].source_id;
        next.slots[i].source_epoch = input[i].source_epoch;
        next.slots[i].generation = actor->identity.generation;
        next.slots[i].active = 1;
        actor->source_slot = i;
        actor->visible = input[i].visible != 0;
        actor->x = input[i].x;
        actor->y = input[i].y;
        actor->z = input[i].z;
        actor->sy = input[i].sy;
        actor->cy = input[i].cy;
        actor->animation_id = input[i].animation_id;
        actor->animation_time_ms = input[i].animation_time_ms;
        actor->weapon = input[i].weapon;
    }
    next.last_frame_id = frame_id;
    next.last_world_generation = world_generation;
    *tracker = next;
    *out = snapshot;
    return 0;
}
