#ifndef RASTERFALL_RENDER_RESOURCES_H
#define RASTERFALL_RENDER_RESOURCES_H

#include "rasterfall_model.h"

/* One renderer, one frame in flight. A mesh owns its material table and
 * texture backing as one immutable bundle; subresource identity is the
 * mesh handle plus table index. Zero generation is never a valid handle. */
#define RASTERFALL_RESOURCE_CAPACITY 256
#define RASTERFALL_RESOURCE_PATH_BYTES 256
struct rasterfall_resource_handle { unsigned int slot, generation; };
struct rasterfall_resource_slot {
    struct rasterfall_model_asset *model;
    unsigned int generation;
    int active, pinned, failed;
    char path[RASTERFALL_RESOURCE_PATH_BYTES];
};
struct rasterfall_resource_registry {
    struct rasterfall_resource_slot slots[RASTERFALL_RESOURCE_CAPACITY];
    int frame_active;
    unsigned int loads, releases;
};
struct rasterfall_resource_stats {
    unsigned int live, retired, pinned, failed, loads, releases;
};

/* Zero-initialize once. Never memset a registry to reload it: generations
 * survive unload, shutdown and device/target recreation. Exact asset paths
 * are canonical identities supplied by the prop profile table. */
int rasterfall_resources_load(struct rasterfall_resource_registry *registry,
    const char *path, struct rasterfall_resource_handle *handle);
const struct rasterfall_model_asset *rasterfall_resources_resolve(
    const struct rasterfall_resource_registry *registry,
    struct rasterfall_resource_handle handle);
int rasterfall_resources_pin(struct rasterfall_resource_registry *registry,
    struct rasterfall_resource_handle handle);
int rasterfall_resources_frame_begin(struct rasterfall_resource_registry *registry);
/* Caller guarantees all GPU work and CPU replay using this frame are done,
 * or cancelled after backend teardown. Failure is NOT completion. */
void rasterfall_resources_frame_complete(struct rasterfall_resource_registry *registry);
void rasterfall_resources_invalidate(struct rasterfall_resource_registry *registry);
void rasterfall_resources_stats(const struct rasterfall_resource_registry *registry,
    struct rasterfall_resource_stats *stats);
struct rasterfall_resource_registry *rasterfall_render_resources(void);
int rasterfall_render_resources_logic_test(void);

#endif
