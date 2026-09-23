#include "rf_gpu_scene_extract.h"
#include <stdlib.h>
#include <string.h>

static int compare_item_ordinal(const void *a, const void *b)
{
    const struct rf_gpu_scene_item_v1 *left = a, *right = b;
    if (left->submission_ordinal < right->submission_ordinal) return -1;
    if (left->submission_ordinal > right->submission_ordinal) return 1;
    return 0;
}

int rf_gpu_scene_extract_v1(const struct rf_gpu_scene_snapshot_v2 *snapshot,
                            struct rf_gpu_scene_frozen_v1 *out)
{
    struct rf_gpu_scene_frozen_v1 next;
    uint32_t i;
    if (!snapshot || !out ||
        snapshot->abi_version != RF_GPU_SCENE_SNAPSHOT_ABI_V2 ||
        snapshot->byte_size != sizeof(*snapshot) ||
        !snapshot->frame_id || !snapshot->world_generation ||
        !snapshot->map_generation || !snapshot->width || !snapshot->height ||
        snapshot->actor_first || snapshot->world_first ||
        snapshot->transient_first ||
        snapshot->actor_count > TOY_GAME_MAX_ACTORS ||
        snapshot->world_count > RF_GPU_SCENE_MAX_WORLD_V2 ||
        snapshot->transient_count > RF_GPU_SCENE_MAX_TRANSIENT_V2) return -1;
    memset(&next, 0, sizeof(next));
    next.abi_version = RF_GPU_SCENE_FROZEN_ABI_V1;
    next.byte_size = (uint32_t)sizeof(next);
    next.frame_id = snapshot->frame_id;
    next.world_generation = snapshot->world_generation;
    next.map_generation = snapshot->map_generation;
    next.width = snapshot->width;
    next.height = snapshot->height;
    for (i = 0; i < snapshot->world_count; ++i) {
        struct rf_gpu_scene_item_v1 *item = &next.items[next.item_count++];
        const struct rf_gpu_scene_world_input_v2 *world = &snapshot->world[i];
        if (!world->id[0] || !world->kind[0] ||
            !memchr(world->id, 0, sizeof(world->id)) ||
            !memchr(world->kind, 0, sizeof(world->kind))) return -1;
        item->source_kind = RF_GPU_SCENE_ITEM_WORLD;
        item->source_index = i;
        item->submission_ordinal = world->submission_ordinal;
        item->visible = world->visible != 0;
        item->x = world->x; item->y = world->y; item->z = world->z;
        item->alpha = world->alpha;
        strcpy(item->world_id, world->id);
        strcpy(item->world_kind, world->kind);
    }
    for (i = 0; i < snapshot->actor_count; ++i) {
        struct rf_gpu_scene_item_v1 *item = &next.items[next.item_count++];
        const struct rf_gpu_scene_actor_v1 *actor = &snapshot->actors[i];
        if (!actor->identity.source || !actor->identity.generation) return -1;
        item->source_kind = RF_GPU_SCENE_ITEM_ACTOR;
        item->source_index = i;
        item->submission_ordinal = snapshot->actor_submission_ordinals[i];
        item->visible = actor->visible != 0;
        item->x = actor->x; item->y = actor->y; item->z = actor->z;
        item->alpha = 255;
        item->actor = actor->identity;
    }
    for (i = 0; i < snapshot->transient_count; ++i) {
        struct rf_gpu_scene_item_v1 *item = &next.items[next.item_count++];
        const struct rf_gpu_scene_transient_input_v2 *transient =
            &snapshot->transient[i];
        if (!transient->source || !transient->kind) return -1;
        item->source_kind = RF_GPU_SCENE_ITEM_TRANSIENT;
        item->source_index = i;
        item->submission_ordinal = transient->submission_ordinal;
        item->kind = transient->kind;
        item->visible = transient->visible != 0;
        item->x = transient->x; item->y = transient->y;
        item->z = transient->z; item->alpha = transient->alpha;
        item->transient_source = transient->source;
        item->transient_local_id = transient->local_id;
    }
    qsort(next.items, next.item_count, sizeof(next.items[0]),
          compare_item_ordinal);
    for (i = 1; i < next.item_count; ++i)
        if (next.items[i - 1].submission_ordinal ==
            next.items[i].submission_ordinal) return -1;
    *out = next;
    return 0;
}
