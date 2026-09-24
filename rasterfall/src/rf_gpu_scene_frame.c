#include "rf_gpu_scene_frame.h"
#include "tlibc_everything.h"
#include <string.h>

static int scene_has_nul(const char *value,uint32_t capacity)
{
    for(uint32_t i=0;i<capacity;++i) if (!value[i]) return 1;
    return 0;
}

static void scene_sift_down(uint32_t *values,uint32_t root,uint32_t end)
{
    while(root<end && root<=(end-1)/2) {
        uint32_t child=root*2+1;
        if (child<end && values[child]<values[child+1]) child++;
        if (values[root]>=values[child]) return;
        uint32_t swap=values[root];values[root]=values[child];values[child]=swap;
        root=child;
    }
}

static void scene_sort_ordinals(uint32_t *values,uint32_t count)
{
    /* Bounded, in-place order validation also works in the freestanding build. */
    if (count<2) return;
    for(uint32_t start=count/2;;--start) {
        scene_sift_down(values,start,count-1);
        if (!start) break;
    }
    for(uint32_t end=count-1;end>0;--end) {
        uint32_t swap=values[0];values[0]=values[end];values[end]=swap;
        scene_sift_down(values,0,end-1);
    }
}

int rf_gpu_scene_snapshot_build_v2(
    struct rf_gpu_scene_identity_tracker *tracker,
    const struct rf_gpu_scene_actor_input_v1 actors[TOY_GAME_MAX_ACTORS],
    const uint32_t actor_submission_ordinals[TOY_GAME_MAX_ACTORS],
    const struct camera *camera, uint64_t frame_id, uint64_t world_generation,
    uint64_t map_generation, uint32_t width, uint32_t height,
    int air_walls_enabled,
    const struct rf_gpu_scene_world_input_v2 *world, uint32_t world_count,
    const struct rf_gpu_scene_transient_input_v2 *transient,
    uint32_t transient_count, struct rf_gpu_scene_snapshot_v2 *out)
{
    struct rf_gpu_scene_identity_tracker next_tracker;
    struct rf_gpu_scene_snapshot_v1 actor_snapshot;
    struct rf_gpu_scene_snapshot_v2 next;
    uint32_t ordinals[TOY_GAME_MAX_ACTORS + RF_GPU_SCENE_MAX_WORLD_V2 +
                      RF_GPU_SCENE_MAX_TRANSIENT_V2];
    uint32_t i, j, ordinal_count = 0;
    if (!tracker || !actors || !actor_submission_ordinals || !camera || !out || !map_generation ||
        world_count > RF_GPU_SCENE_MAX_WORLD_V2 ||
        transient_count > RF_GPU_SCENE_MAX_TRANSIENT_V2 ||
        (world_count && !world) || (transient_count && !transient)) return -1;
    for (i = 0; i < world_count; ++i) {
        if (!world[i].id[0] || !world[i].kind[0] ||
            !scene_has_nul(world[i].id, RF_GPU_SCENE_WORLD_ID_CAP) ||
            !scene_has_nul(world[i].kind, RF_GPU_SCENE_WORLD_KIND_CAP) ||
            world[i].alpha < 0 || world[i].alpha > 255)
            return -1;
        for (j = 0; j < i; ++j)
            if (!strcmp(world[i].id, world[j].id)) return -1;
        ordinals[ordinal_count++] = world[i].submission_ordinal;
    }
    for (i = 0; i < transient_count; ++i) {
        if (!transient[i].source || !transient[i].kind ||
            transient[i].alpha < 0 || transient[i].alpha > 255 ||
            transient[i].age_ms < 0) return -1;
        for (j = 0; j < i; ++j)
            if (transient[i].source == transient[j].source &&
                transient[i].local_id == transient[j].local_id) return -1;
        ordinals[ordinal_count++] = transient[i].submission_ordinal;
    }
    for (i = 0; i < TOY_GAME_MAX_ACTORS; ++i)
        if (actors[i].active)
            ordinals[ordinal_count++] = actor_submission_ordinals[i];
    scene_sort_ordinals(ordinals,ordinal_count);
    for (i = 1; i < ordinal_count; ++i)
        if (ordinals[i - 1] == ordinals[i]) return -1;
    next_tracker = *tracker;
    if (rf_gpu_scene_snapshot_build_v1(&next_tracker, actors, camera,
        frame_id, world_generation, width, height, &actor_snapshot) < 0)
        return -1;
    memset(&next, 0, sizeof(next));
    next.abi_version = RF_GPU_SCENE_SNAPSHOT_ABI_V2;
    next.byte_size = (uint32_t)sizeof(next);
    next.frame_id = frame_id;
    next.world_generation = world_generation;
    next.map_generation = map_generation;
    next.width = width;
    next.height = height;
    next.actor_count = actor_snapshot.actor_count;
    next.world_count = world_count;
    next.transient_count = transient_count;
    next.air_walls_enabled = air_walls_enabled != 0;
    next.camera = actor_snapshot.camera;
    memcpy(next.actors, actor_snapshot.actors, sizeof(next.actors));
    for (i = 0; i < next.actor_count; ++i)
        next.actor_submission_ordinals[i] =
            actor_submission_ordinals[next.actors[i].source_slot];
    for (i = 0; i < world_count; ++i) {
        strcpy(next.world[i].id, world[i].id);
        strcpy(next.world[i].kind, world[i].kind);
        next.world[i].submission_ordinal = world[i].submission_ordinal;
        next.world[i].visible = world[i].visible != 0;
        next.world[i].x = world[i].x;
        next.world[i].y = world[i].y;
        next.world[i].z = world[i].z;
        next.world[i].alpha = world[i].alpha;
    }
    for (i = 0; i < transient_count; ++i) {
        next.transient[i].source = transient[i].source;
        next.transient[i].local_id = transient[i].local_id;
        next.transient[i].kind = transient[i].kind;
        next.transient[i].submission_ordinal = transient[i].submission_ordinal;
        next.transient[i].visible = transient[i].visible != 0;
        next.transient[i].x = transient[i].x;
        next.transient[i].y = transient[i].y;
        next.transient[i].z = transient[i].z;
        next.transient[i].alpha = transient[i].alpha;
        next.transient[i].age_ms = transient[i].age_ms;
    }
    *tracker = next_tracker;
    *out = next;
    return 0;
}
