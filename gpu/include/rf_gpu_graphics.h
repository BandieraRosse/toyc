#ifndef RF_GPU_GRAPHICS_H
#define RF_GPU_GRAPHICS_H

#include <stdint.h>
#include "rf_gpu_vulkan_backend.h"

/* HG-2A/2B hosted diagnostic only. No normal-frame/fallback integration.
 * Corner vertices carry all three source normals to preserve integer
 * per-primitive lighting after rotation. Expansion happens once at upload. */
struct rf_gpu_graphics_vertex {
    int32_t position[3], uv[2], normals[9];
};

/* Seven 16-byte push-constant lanes; all transforms use C/GLSL integer
 * division (toward zero). Q10 directions, milli scale, unsigned Q16 UV.
 * Deliberately bounded proof contract; not a replacement for Draw V0.
 * integer_depth requires shaderInt64 and rejects conservative projected
 * mesh bounds outside [-16384,16384] before submitting any draw. */
struct rf_gpu_graphics_draw {
    int32_t translation_scale[4];
    int32_t rotation[4]; /* sin, cos, bottom pivot y, form-light enabled */
    int32_t camera[4];
    int32_t view[4]; /* direction x,z; pitch sin,cos */
    int32_t projection[4]; /* extent x,y; near=64; focal=width*3/4 */
    uint32_t material[4]; /* RGB, scene Q8, textured, reserved */
    int32_t texture[4]; /* width,height, material alpha (0=opaque),reserved */
    uint32_t first_index, index_count, double_sided;
    uint32_t integer_depth; /* HG-2B GPU clip/project + exact integer depth */
};
enum rf_gpu_graphics_submit_kind {
    RF_GPU_SUBMIT_UPLOAD, RF_GPU_SUBMIT_VERTEX_DIFF, RF_GPU_SUBMIT_SKIN_INPUT,
    RF_GPU_SUBMIT_SKINNING, RF_GPU_SUBMIT_BRIDGE, RF_GPU_SUBMIT_DRAW,
    RF_GPU_SUBMIT_READBACK, RF_GPU_SUBMIT_KIND_COUNT
};
struct rf_gpu_graphics_stats {
    uint64_t mesh_upload_bytes, texture_upload_bytes;
    uint64_t instance_upload_bytes, indexed_draws, frames, target_builds;
    uint64_t bridge_roundtrips, bridge_transfer_bytes, raster_bridge_transfers;
    uint64_t queue_submits, fence_waits;
    double submit_wall_ms, fence_wait_wall_ms, bridge_wall_ms;
    uint64_t submits_by_kind[RF_GPU_SUBMIT_KIND_COUNT];
    double wait_ms_by_kind[RF_GPU_SUBMIT_KIND_COUNT];
    /* Unconfirmed predecessor at submit time, not a measured GPU duration. */
    uint64_t wait_frame, wait_predecessor_frame;
};
struct rf_gpu_graphics;
struct rf_gpu_graphics_resource;
struct rf_gpu_graphics_batch_item {
    struct rf_gpu_graphics_resource *resource;
    struct rf_gpu_graphics_draw draw;
};
struct rf_gpu_scene_timing {
    uint64_t frame_id;
    int supported, valid;
    double world_draw_ms, present_blit_ms;
};

/* Persistent immutable submesh/texture bundles, independent of target extent.
 * Resources belong to their creating graphics owner and device, and are
 * destroyed automatically with that owner. Another graphics frame slot on the
 * same backend device may bind the immutable resource through its own descriptor
 * set; target resize never duplicates the mesh/texture upload. Bind/upload/
 * destroy are synchronous.
 * Creating a resource does not change the current binding or target contents.
 * Caller must not use a resource pointer after releasing it. */
struct rf_gpu_graphics_resource *rf_gpu_graphics_resource_create(
    struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_vertex *vertices, uint32_t vertex_count,
    const uint32_t *indices, uint32_t index_count,
    const uint32_t *rgb_texels, uint32_t texture_width, uint32_t texture_height);
/* HG-5B: create the ordinary indexed resource, then fill its vertex buffer
 * from packed bind/palette words. Reference may be NULL on normal frames;
 * explicit diff supplies it as a CPU oracle. */
struct rf_gpu_graphics_resource *rf_gpu_graphics_skinned_resource_create(
    struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_vertex *reference, uint32_t vertex_count,
    const uint32_t *indices, uint32_t index_count,
    const uint32_t *bind_words, uint32_t bind_word_count,
    const uint32_t *palette_words, uint32_t palette_word_count,
    const uint32_t *rgb_texels, uint32_t texture_width, uint32_t texture_height);
/* Reuse a completed frame-slot skin resource when the new input fits.
 * Returns 0 on success, 1 when capacity must grow, and -1 on failure. */
int rf_gpu_graphics_skinned_resource_update(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource *resource, uint32_t vertex_count,
    const uint32_t *bind_words, uint32_t bind_word_count,
    const uint32_t *palette_words, uint32_t palette_word_count);
int rf_gpu_graphics_resource_bind(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource *resource);
int rf_gpu_graphics_resource_set_frame_dynamic(
    struct rf_gpu_graphics_resource *resource);
int rf_gpu_graphics_resource_destroy(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource *resource);
/* Diagnostic proof for dynamic geometry: copy the device-local vertex buffer
 * back through the GPU transfer path and compare the bytes consumed by the
 * configured vertex-input ABI with the CPU reference. */
int rf_gpu_graphics_resource_diff_vertices(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource *resource,
    const struct rf_gpu_graphics_vertex *reference, uint32_t vertex_count,
    uint64_t *position_mismatches, uint64_t *normal_mismatches,
    uint64_t *uv_mismatches, uint32_t *max_position_delta,
    uint32_t *max_normal_delta);

/* Caller shuts graphics down before its shared backend context. All calls
 * are synchronous except the explicitly retired Scene submission below.
 * Failure never invokes CPU lowering. */
struct rf_gpu_graphics *rf_gpu_graphics_create(struct rf_gpu_vulkan_context *ctx);
/* Validate the currently bound resource/draw without touching target contents. */
int rf_gpu_graphics_validate_draw(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_draw *draw);
int rf_gpu_graphics_validate_dynamic_draw(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_draw *draw);
int rf_gpu_graphics_upload(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_vertex *vertices, uint32_t vertex_count,
    const uint32_t *indices, uint32_t index_count,
    const uint32_t *rgb_texels, uint32_t texture_width, uint32_t texture_height);
int rf_gpu_graphics_resize(struct rf_gpu_graphics *g, uint32_t width, uint32_t height);
/* Bind the normal mixed Raster target to this graphics owner's color image.
 * Resize discards unfinished borrower recordings and rebinds matching extents;
 * a new CLEAR is required. Destruction detaches borrowers. Submitted borrowers
 * must complete before the old attachment is released. */
int rf_gpu_graphics_share_color(struct rf_gpu_graphics *g, void *raster);
int rf_gpu_graphics_render(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_draw *draws, uint32_t count,
    uint32_t *rgba, float *depth, uint32_t pixel_capacity);
/* Diagnostic target interop: GPU export RGBA8/D32 -> RGBA8/inverse-Z
 * buffers -> GPU import, then attachment LOAD and later indexed draws.
 * No host framebuffer upload; final readback is diagnostic only. Requires
 * a successfully rendered target at this extent; resize invalidates it.
 * This is not yet the normal Raster ABI segmented-frame consumer. */
int rf_gpu_graphics_continue(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_draw *draws, uint32_t count,
    uint32_t *rgba, float *depth, uint32_t pixel_capacity);
/* HG-2B hosted interop: import an unfinished Raster ABI target, LOAD indexed
 * draws, export back to that target. GPU-only; no Post/present/readback.
 * Same device/extent, integer_depth draws, and WORLD depths in [0,16384]
 * required. Preflight rejection preserves raster continuation; execution
 * failure invalidates it. Caller owns frame ordering and lifetime. */
int rf_gpu_graphics_raster_draw(struct rf_gpu_graphics *g, void *raster,
    const struct rf_gpu_graphics_draw *draws, uint32_t count);
int rf_gpu_graphics_raster_batch(struct rf_gpu_graphics *g, void *raster,
    const struct rf_gpu_graphics_batch_item *items, uint32_t count);
/* Inspect the last diagnostic bridge's exported compute encoding, never used as input
 * to drawing/import. Diagnostic readback validates the conversion itself. */
int rf_gpu_graphics_read_bridge(struct rf_gpu_graphics *g,
    uint32_t *rgba, int32_t *inverse_depth, uint32_t pixel_capacity);
void rf_gpu_graphics_get_stats(const struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_stats *stats);
void rf_gpu_graphics_destroy(struct rf_gpu_graphics *g);

/* Isolated Scene slot: no Raster stream, bridge, or CPU framebuffer. All
 * items must be preflighted and pinned before this call. Successful submission
 * retains resources until scene_retire; failure is not completion. Destroying
 * the graphics owner drains pending work before releasing device resources. */
int rf_gpu_graphics_scene_present(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_batch_item *items, uint32_t count,uint64_t frame_id);
int rf_gpu_graphics_scene_retire(struct rf_gpu_graphics *g);
void rf_gpu_graphics_scene_timing(const struct rf_gpu_graphics *g,
    struct rf_gpu_scene_timing *timing);
/* Explicit diagnostic only: direct attachment readback, no Raster conversion. */
int rf_gpu_graphics_scene_capture(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_batch_item *items, uint32_t count,
    uint32_t *rgba, float *depth, uint32_t capacity);

#endif
