#ifndef RF_GPU_GRAPHICS_H
#define RF_GPU_GRAPHICS_H

#include <stdint.h>
#include "rf_gpu_vulkan_backend.h"

/* HG-2A hosted diagnostic only. No normal-frame/fallback integration.
 * Corner vertices carry all three source normals to preserve integer
 * per-primitive lighting after rotation. Expansion happens once at upload. */
struct rf_gpu_graphics_vertex {
    int32_t position[3], uv[2], normals[9];
};

/* Seven 16-byte push-constant lanes; all transforms use C/GLSL integer
 * division (toward zero). Q10 directions, milli scale, unsigned Q16 UV.
 * Deliberately bounded proof contract; not a replacement for Draw V0. */
struct rf_gpu_graphics_draw {
    int32_t translation_scale[4];
    int32_t rotation[4]; /* sin, cos, bottom pivot y, form-light enabled */
    int32_t camera[4];
    int32_t view[4]; /* direction x,z; pitch sin,cos */
    int32_t projection[4]; /* extent x,y; near=64; focal=width*3/4 */
    uint32_t material[4]; /* RGB, scene Q8, textured, reserved */
    int32_t texture[4]; /* width,height, reserved,reserved */
    uint32_t first_index, index_count, double_sided;
};
struct rf_gpu_graphics_stats {
    uint64_t mesh_upload_bytes, texture_upload_bytes;
    uint64_t instance_upload_bytes, indexed_draws, frames, target_builds;
};
struct rf_gpu_graphics;

/* Caller shuts graphics down before its shared backend context. All calls
 * are synchronous, one frame in flight. Failure never invokes CPU lowering. */
struct rf_gpu_graphics *rf_gpu_graphics_create(struct rf_gpu_vulkan_context *ctx);
int rf_gpu_graphics_upload(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_vertex *vertices, uint32_t vertex_count,
    const uint32_t *indices, uint32_t index_count,
    const uint32_t *rgb_texels, uint32_t texture_width, uint32_t texture_height);
int rf_gpu_graphics_resize(struct rf_gpu_graphics *g, uint32_t width, uint32_t height);
int rf_gpu_graphics_render(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_draw *draws, uint32_t count,
    uint32_t *rgba, float *depth, uint32_t pixel_capacity);
void rf_gpu_graphics_get_stats(const struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_stats *stats);
void rf_gpu_graphics_destroy(struct rf_gpu_graphics *g);

#endif
