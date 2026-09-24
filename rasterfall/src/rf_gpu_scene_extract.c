#include "rf_gpu_scene_extract.h"
#include "tlibc_everything.h"
#include <string.h>

static int scene_item_has_nul(const char *value,uint32_t capacity)
{
    for(uint32_t i=0;i<capacity;++i) if (!value[i]) return 1;
    return 0;
}

static void scene_item_sift_down(struct rf_gpu_scene_item_v1 *items,
    uint32_t root,uint32_t end)
{
    while(root<end && root<=(end-1)/2) {
        uint32_t child=root*2+1;
        if (child<end && items[child].submission_ordinal<
            items[child+1].submission_ordinal) child++;
        if (items[root].submission_ordinal>=
            items[child].submission_ordinal) return;
        struct rf_gpu_scene_item_v1 swap=items[root];
        items[root]=items[child];items[child]=swap;
        root=child;
    }
}

static void scene_sort_items(struct rf_gpu_scene_item_v1 *items,uint32_t count)
{
    if (count<2) return;
    for(uint32_t start=count/2;;--start) {
        scene_item_sift_down(items,start,count-1);
        if (!start) break;
    }
    for(uint32_t end=count-1;end>0;--end) {
        struct rf_gpu_scene_item_v1 swap=items[0];
        items[0]=items[end];items[end]=swap;
        scene_item_sift_down(items,0,end-1);
    }
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
            !scene_item_has_nul(world->id, sizeof(world->id)) ||
            !scene_item_has_nul(world->kind, sizeof(world->kind))) return -1;
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
    scene_sort_items(next.items,next.item_count);
    for (i = 1; i < next.item_count; ++i)
        if (next.items[i - 1].submission_ordinal ==
            next.items[i].submission_ordinal) return -1;
    *out = next;
    return 0;
}
