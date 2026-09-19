#ifndef RF_GPU_RESOURCE_CACHE_H
#define RF_GPU_RESOURCE_CACHE_H

#include "rf_gpu_graphics.h"
#include "rasterfall_render_resources.h"

#define RF_GPU_CACHE_FLAT_TEXTURE UINT32_MAX
#define RF_GPU_CACHE_CAPACITY 1024
struct rf_gpu_resource_cache;
struct rf_gpu_cached_submesh {
    uint32_t index_count, texture_width, texture_height;
};
struct rf_gpu_resource_cache_stats {
    uint64_t uploads, hits, releases;
    uint32_t entries;
};

/* Hosted HG-2B adapter. One registry and one graphics/device lifetime per
 * cache; destroy the cache before either owner. No normal producer hookup.
 * Immutable key: slot/generation + primitive + texture table index (or FLAT).
 * Each submesh expands its triangle corners once to preserve source normals;
 * shared vertices/textures across submeshes are not deduplicated in V0.
 * Targets/pipelines remain shared in graphics; resize never invalidates cache.
 * This adapter validates backing, not Draw material/transform eligibility. */
struct rf_gpu_resource_cache *rf_gpu_resource_cache_create(
    struct rf_gpu_graphics *graphics,
    const struct rasterfall_resource_registry *registry);
/* Must run during whole-frame preflight, before CLEAR. May upload resources
 * but never changes graphics binding or target contents. Requires this exact
 * active frame epoch and an existing pin, including retired-but-pinned data. */
int rf_gpu_resource_cache_prepare(struct rf_gpu_resource_cache *cache,
    unsigned long long frame_epoch, struct rasterfall_resource_handle handle,
    uint32_t primitive, uint32_t texture, struct rf_gpu_cached_submesh *info);
/* Lookup only: never allocates, traverses source triangles, or uploads.
 * Revalidates epoch/pin/generation. Draw first_index is zero in this submesh. */
int rf_gpu_resource_cache_bind(struct rf_gpu_resource_cache *cache,
    unsigned long long frame_epoch, struct rasterfall_resource_handle handle,
    uint32_t primitive, uint32_t texture);
struct rf_gpu_graphics_resource *rf_gpu_resource_cache_resource(
    struct rf_gpu_resource_cache *cache, unsigned long long frame_epoch,
    struct rasterfall_resource_handle handle, uint32_t primitive, uint32_t texture);
/* Safe after synchronous consumers return. Retired pinned entries survive;
 * invalid generations are released without dereferencing old CPU pointers. */
void rf_gpu_resource_cache_collect(struct rf_gpu_resource_cache *cache);
void rf_gpu_resource_cache_get_stats(const struct rf_gpu_resource_cache *cache,
    struct rf_gpu_resource_cache_stats *stats);
void rf_gpu_resource_cache_destroy(struct rf_gpu_resource_cache *cache);

#endif
