#include <assert.h>
#include <string.h>
#include "rf_gpu_scene_extract.h"

int main(void)
{
    struct rf_gpu_scene_identity_tracker tracker, before, replay_tracker;
    struct rf_gpu_scene_actor_input_v1 actors[TOY_GAME_MAX_ACTORS];
    uint32_t actor_ordinals[TOY_GAME_MAX_ACTORS];
    struct rf_gpu_scene_world_input_v2 world[2];
    struct rf_gpu_scene_transient_input_v2 transient[2];
    struct rf_gpu_scene_snapshot_v2 snapshot, previous, replay;
    struct rf_gpu_scene_frozen_v1 scene, scene_before;
    struct camera camera;
    memset(&tracker, 0, sizeof(tracker));
    memset(actors, 0, sizeof(actors));
    memset(actor_ordinals, 0, sizeof(actor_ordinals));
    memset(world, 0, sizeof(world));
    memset(transient, 0, sizeof(transient));
    memset(&camera, 0, sizeof(camera));
    actors[4].active = 1;
    actors[4].source = RF_GPU_SCENE_ACTOR_GAME;
    actors[4].source_id = 7;
    actor_ordinals[4] = 20;
    actor_ordinals[9] = 20;
    strcpy(world[0].id, "wall-b");
    strcpy(world[0].kind, "wall");
    world[0].submission_ordinal = 3;
    world[0].alpha = 255;
    strcpy(world[1].id, "wall-a");
    strcpy(world[1].kind, "box");
    world[1].submission_ordinal = 4;
    world[1].alpha = 48;
    transient[0].source = transient[1].source = 1;
    transient[0].local_id = 0;
    transient[1].local_id = 1;
    transient[0].kind = transient[1].kind = 2;
    transient[0].submission_ordinal = 30;
    transient[1].submission_ordinal = 31;
    transient[0].alpha = transient[1].alpha = 255;
    replay_tracker = tracker;
    assert(rf_gpu_scene_snapshot_build_v2(&tracker, actors, actor_ordinals, &camera,
        10, 1, 3, 1280, 720, 1, world, 2, transient, 2, &snapshot) == 0);
    assert(snapshot.abi_version == RF_GPU_SCENE_SNAPSHOT_ABI_V2 &&
        snapshot.byte_size == sizeof(snapshot));
    assert(snapshot.actor_count == 1 && snapshot.world_count == 2 &&
        snapshot.transient_count == 2 && snapshot.air_walls_enabled == 1);
    assert(!strcmp(snapshot.world[0].id, "wall-b") &&
        !strcmp(snapshot.world[1].id, "wall-a"));
    assert(snapshot.actors[0].identity.generation == 1);
    assert(rf_gpu_scene_extract_v1(&snapshot, &scene) == 0);
    assert(scene.abi_version == RF_GPU_SCENE_FROZEN_ABI_V1 &&
        scene.byte_size == sizeof(scene) && scene.item_count == 5);
    assert(scene.items[0].source_kind == RF_GPU_SCENE_ITEM_WORLD &&
        !strcmp(scene.items[0].world_id, "wall-b"));
    assert(scene.items[2].source_kind == RF_GPU_SCENE_ITEM_ACTOR &&
        scene.items[2].actor.generation == 1);
    assert(scene.items[3].source_kind == RF_GPU_SCENE_ITEM_TRANSIENT &&
        scene.items[3].submission_ordinal == 30);
    scene_before = scene;
    snapshot.transient[1].submission_ordinal = 20;
    assert(rf_gpu_scene_extract_v1(&snapshot, &scene) == -1);
    assert(memcmp(&scene, &scene_before, sizeof(scene)) == 0);
    snapshot.transient[1].submission_ordinal = 31;
    assert(rf_gpu_scene_snapshot_build_v2(&replay_tracker, actors, actor_ordinals, &camera,
        10, 1, 3, 1280, 720, 1, world, 2, transient, 2, &replay) == 0);
    assert(memcmp(&snapshot, &replay, sizeof(snapshot)) == 0);
    before = tracker;
    previous = snapshot;
    transient[1].local_id = 0;
    assert(rf_gpu_scene_snapshot_build_v2(&tracker, actors, actor_ordinals, &camera,
        11, 1, 3, 1280, 720, 1, world, 2, transient, 2, &snapshot) == -1);
    assert(memcmp(&tracker, &before, sizeof(tracker)) == 0);
    assert(memcmp(&snapshot, &previous, sizeof(snapshot)) == 0);
    transient[1].local_id = 1;
    strcpy(world[1].id, "wall-b");
    assert(rf_gpu_scene_snapshot_build_v2(&tracker, actors, actor_ordinals, &camera,
        11, 1, 3, 1280, 720, 1, world, 2, transient, 2, &snapshot) == -1);
    assert(memcmp(&tracker, &before, sizeof(tracker)) == 0);
    assert(memcmp(&snapshot, &previous, sizeof(snapshot)) == 0);
    strcpy(world[1].id, "wall-a");
    transient[1].submission_ordinal = actor_ordinals[4];
    assert(rf_gpu_scene_snapshot_build_v2(&tracker, actors, actor_ordinals, &camera,
        11, 1, 3, 1280, 720, 1, world, 2, transient, 2, &snapshot) == -1);
    assert(memcmp(&tracker, &before, sizeof(tracker)) == 0);
    assert(memcmp(&snapshot, &previous, sizeof(snapshot)) == 0);
    transient[1].submission_ordinal = 31;
    assert(rf_gpu_scene_snapshot_build_v2(&tracker, actors, actor_ordinals, &camera,
        11, 1, 3, 1280, 720, 1, world,
        RF_GPU_SCENE_MAX_WORLD_V2 + 1, transient, 2, &snapshot) == -1);
    actors[4].active = 0;
    actors[9] = actors[4];
    actors[9].active = 1;
    assert(rf_gpu_scene_snapshot_build_v2(&tracker, actors, actor_ordinals, &camera,
        11, 1, 3, 1280, 720, 0, world, 2, transient, 2, &snapshot) == 0);
    assert(snapshot.actors[0].source_slot == 9 &&
        snapshot.actors[0].identity.generation == 1);
    assert(snapshot.air_walls_enabled == 0);
    return 0;
}
