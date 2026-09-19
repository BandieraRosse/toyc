#include "tlibc_everything.h"
#include "rasterfall_render_resources.h"
#include <limits.h>

static struct rasterfall_resource_registry render_resources;

struct rasterfall_resource_registry *rasterfall_render_resources(void)
{
    return &render_resources;
}

static void resource_collect(struct rasterfall_resource_registry *registry)
{
    unsigned int i;
    for (i = 0; i < RASTERFALL_RESOURCE_CAPACITY; ++i) {
        struct rasterfall_resource_slot *slot = &registry->slots[i];
        if (!slot->active && !slot->pinned && slot->model) {
            rasterfall_model_unload(slot->model);
            tlibc_free(slot->model);
            slot->model = NULL;
            registry->releases++;
        }
    }
}

static int resource_load(struct rasterfall_resource_registry *registry,
    const char *path, struct rasterfall_resource_handle *handle,
    int (*load)(struct rasterfall_model_asset *, const char *))
{
    unsigned int i, available = RASTERFALL_RESOURCE_CAPACITY;
    struct rasterfall_resource_slot *slot;
    if (!handle) return -1;
    memset(handle, 0, sizeof(*handle));
    if (!registry || !path || !*path ||
        strlen(path) >= RASTERFALL_RESOURCE_PATH_BYTES) return -1;
    for (i = 0; i < RASTERFALL_RESOURCE_CAPACITY; ++i) {
        slot = &registry->slots[i];
        if (slot->active && !strcmp(slot->path, path)) {
            if (slot->failed) return -1;
            handle->slot = i; handle->generation = slot->generation;
            return 0;
        }
        /* A saturated generation permanently retires the slot: no ABA. */
        if (!slot->active && !slot->model && !slot->pinned &&
            slot->generation != UINT_MAX && available == RASTERFALL_RESOURCE_CAPACITY)
            available = i;
    }
    if (available == RASTERFALL_RESOURCE_CAPACITY) return -1;
    slot = &registry->slots[available];
    slot->model = tlibc_malloc(sizeof(*slot->model));
    if (!slot->model) return -1;
    memset(slot->model, 0, sizeof(*slot->model));
    slot->generation++;
    strcpy(slot->path, path);
    slot->active = 1;
    slot->failed = 0;
    registry->loads++;
    if (load(slot->model, path) < 0) {
        rasterfall_model_unload(slot->model);
        tlibc_free(slot->model);
        slot->model = NULL;
        slot->failed = 1;
        return -1;
    }
    handle->slot = available; handle->generation = slot->generation;
    return 0;
}

int rasterfall_resources_load(struct rasterfall_resource_registry *registry,
    const char *path, struct rasterfall_resource_handle *handle)
{
    return resource_load(registry, path, handle, rasterfall_model_load);
}

const struct rasterfall_model_asset *rasterfall_resources_resolve(
    const struct rasterfall_resource_registry *registry,
    struct rasterfall_resource_handle handle)
{
    const struct rasterfall_resource_slot *slot;
    if (!registry || !handle.generation || handle.slot >= RASTERFALL_RESOURCE_CAPACITY)
        return NULL;
    slot = &registry->slots[handle.slot];
    if (slot->generation != handle.generation || (!slot->active && !slot->pinned))
        return NULL;
    return slot->model;
}

int rasterfall_resources_pin(struct rasterfall_resource_registry *registry,
    struct rasterfall_resource_handle handle)
{
    if (!registry || !registry->frame_active ||
        !rasterfall_resources_resolve(registry, handle) ||
        !registry->slots[handle.slot].active) return -1;
    registry->slots[handle.slot].pinned = 1;
    return 0;
}

int rasterfall_resources_frame_begin(struct rasterfall_resource_registry *registry)
{
    if (!registry || registry->frame_active) return -1;
    registry->frame_active = 1;
    return 0;
}

void rasterfall_resources_frame_complete(struct rasterfall_resource_registry *registry)
{
    unsigned int i;
    if (!registry) return;
    for (i = 0; i < RASTERFALL_RESOURCE_CAPACITY; ++i) registry->slots[i].pinned = 0;
    registry->frame_active = 0;
    resource_collect(registry);
}

void rasterfall_resources_invalidate(struct rasterfall_resource_registry *registry)
{
    unsigned int i;
    if (!registry) return;
    for (i = 0; i < RASTERFALL_RESOURCE_CAPACITY; ++i) {
        registry->slots[i].active = 0;
        registry->slots[i].failed = 0;
    }
    resource_collect(registry);
}

void rasterfall_resources_stats(const struct rasterfall_resource_registry *registry,
    struct rasterfall_resource_stats *stats)
{
    unsigned int i;
    if (!stats) return;
    memset(stats, 0, sizeof(*stats));
    if (!registry) return;
    stats->loads = registry->loads; stats->releases = registry->releases;
    for (i = 0; i < RASTERFALL_RESOURCE_CAPACITY; ++i) {
        const struct rasterfall_resource_slot *slot = &registry->slots[i];
        stats->live += slot->active && slot->model;
        stats->retired += !slot->active && slot->model;
        stats->pinned += slot->pinned != 0;
        stats->failed += slot->failed != 0;
    }
}

#include "../dev-tests/rasterfall_render_resources_test.inc"
