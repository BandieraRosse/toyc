#include <assert.h>
#include <string.h>
#include "rf_gpu_scene_identity.h"

int main(void)
{
    struct rf_gpu_scene_identity_tracker tracker;
    struct rf_gpu_scene_identity_tracker before, replay_tracker;
    struct rf_gpu_scene_actor_input_v1 input[TOY_GAME_MAX_ACTORS];
    struct rf_gpu_scene_snapshot_v1 snapshot, replay;
    struct camera camera;
    memset(&tracker, 0, sizeof(tracker));
    memset(input, 0, sizeof(input));
    memset(&camera, 0, sizeof(camera));
    camera.x = 120; camera.z = -80; camera.cy = 1024;
    input[3].active = input[3].visible = 1;
    input[3].source = RF_GPU_SCENE_ACTOR_GAME;
    input[3].source_id = 4;
    input[3].source_epoch = 1;
    input[3].x = 300;
    input[8].active = 1;
    input[8].source = RF_GPU_SCENE_ACTOR_NETWORK;
    input[8].source_id = 2;
    input[8].source_epoch = 1;
    before = tracker;
    assert(rf_gpu_scene_snapshot_build_v1(&tracker, input, &camera,
        10, 1, 1280, 720, &snapshot) == 0);
    assert(snapshot.abi_version == RF_GPU_SCENE_SNAPSHOT_ABI_V1);
    assert(snapshot.byte_size == sizeof(snapshot) &&
        snapshot.actor_first == 0 && snapshot.actor_count == 2);
    assert(snapshot.actors[0].source_slot == 3 &&
        snapshot.actors[0].identity.generation == 1 &&
        snapshot.actors[0].x == 300 && snapshot.camera.x == 120);
    assert(snapshot.actors[1].source_slot == 8 &&
        snapshot.actors[1].identity.source == RF_GPU_SCENE_ACTOR_NETWORK &&
        snapshot.actors[1].identity.generation == 2);
    replay_tracker = before;
    assert(rf_gpu_scene_snapshot_build_v1(&replay_tracker, input, &camera,
        10, 1, 1280, 720, &replay) == 0);
    assert(memcmp(&snapshot, &replay, sizeof(snapshot)) == 0);

    before = tracker;
    replay = snapshot;
    input[8].source = RF_GPU_SCENE_ACTOR_GAME;
    input[8].source_id = 4;
    assert(rf_gpu_scene_snapshot_build_v1(&tracker, input, &camera,
        11, 1, 1280, 720, &snapshot) == -1);
    assert(memcmp(&before, &tracker, sizeof(tracker)) == 0);
    assert(memcmp(&replay, &snapshot, sizeof(snapshot)) == 0);
    input[8].source = RF_GPU_SCENE_ACTOR_NETWORK;
    input[8].source_id = 2;
    assert(rf_gpu_scene_snapshot_build_v1(&tracker, input, &camera,
        11, 1, 1280, 720, &snapshot) == 0);
    assert(snapshot.actors[0].identity.generation == 1);
    /* The same source ID can be destroyed and reused between two freezes. */
    input[3].source_epoch = 2;
    assert(rf_gpu_scene_snapshot_build_v1(&tracker, input, &camera,
        12, 1, 1280, 720, &snapshot) == 0);
    assert(snapshot.actors[0].identity.generation == 3);
    assert(snapshot.actors[1].identity.generation == 2);
    /* Moving into an occupied slot, including a swap, preserves both IDs. */
    input[3].source = RF_GPU_SCENE_ACTOR_NETWORK;
    input[3].source_id = 2;
    input[3].source_epoch = 1;
    input[8].source = RF_GPU_SCENE_ACTOR_GAME;
    input[8].source_id = 4;
    input[8].source_epoch = 2;
    assert(rf_gpu_scene_snapshot_build_v1(&tracker, input, &camera,
        13, 1, 1280, 720, &snapshot) == 0);
    assert(snapshot.actors[0].identity.generation == 2);
    assert(snapshot.actors[1].identity.generation == 3);
    input[3].active = 0;
    input[6] = input[8];
    input[8].active = 0;
    assert(rf_gpu_scene_snapshot_build_v1(&tracker, input, &camera,
        14, 1, 1280, 720, &snapshot) == 0);
    assert(snapshot.actors[0].source_slot == 6 &&
        snapshot.actors[0].identity.generation == 3);
    /* A source that disappears and later returns gets a new generation. */
    input[6].active = 0;
    assert(rf_gpu_scene_snapshot_build_v1(&tracker, input, &camera,
        15, 1, 1280, 720, &snapshot) == 0 && snapshot.actor_count == 0);
    input[8].active = 1;
    assert(rf_gpu_scene_snapshot_build_v1(&tracker, input, &camera,
        16, 1, 1280, 720, &snapshot) == 0);
    assert(snapshot.actors[0].identity.generation == 4);
    assert(rf_gpu_scene_snapshot_build_v1(&tracker, input, &camera,
        17, 1, 1280, 720, &snapshot) == 0 && snapshot.actor_count == 1);
    input[3].active = 1;
    input[3].source = RF_GPU_SCENE_ACTOR_NETWORK;
    input[3].source_id = 2;
    assert(rf_gpu_scene_snapshot_build_v1(&tracker, input, &camera,
        18, 1, 1280, 720, &snapshot) == 0);
    assert(snapshot.actors[0].identity.generation == 5);
    assert(rf_gpu_scene_snapshot_build_v1(&tracker, input, &camera,
        19, 2, 1280, 720, &snapshot) == 0);
    assert(snapshot.actors[0].identity.generation == 6);
    assert(snapshot.actors[1].identity.generation == 7);
    assert(rf_gpu_scene_snapshot_build_v1(&tracker, input, &camera,
        19, 2, 1280, 720, &snapshot) == -1);
    replay = snapshot;
    before = tracker;
    input[3].source_epoch = 0;
    assert(rf_gpu_scene_snapshot_build_v1(&tracker, input, &camera,
        20, 2, 1280, 720, &snapshot) == -1);
    assert(memcmp(&before, &tracker, sizeof(tracker)) == 0);
    assert(memcmp(&replay, &snapshot, sizeof(snapshot)) == 0);
    input[3].source_epoch = 1;
    tracker.last_generation = UINT32_MAX;
    before = tracker;
    input[5].active = 1;
    input[5].source = RF_GPU_SCENE_ACTOR_MANAGED;
    input[5].source_id = 9;
    input[5].source_epoch = 1;
    assert(rf_gpu_scene_snapshot_build_v1(&tracker, input, &camera,
        20, 2, 1280, 720, &snapshot) == -1);
    assert(memcmp(&before, &tracker, sizeof(tracker)) == 0);
    assert(memcmp(&replay, &snapshot, sizeof(snapshot)) == 0);
    return 0;
}
