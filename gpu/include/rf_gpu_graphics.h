#ifndef RF_GPU_GRAPHICS_H
#define RF_GPU_GRAPHICS_H

#include <stdint.h>
#include "rf_gpu_vulkan_backend.h"

/* GPU Scene geometry ABI. Corner vertices carry all three source normals
 * for smooth interpolation after rotation. Expansion happens once at upload. */
struct rf_gpu_graphics_vertex {
    int32_t position[3], uv[2], normals[9];
};
struct rf_gpu_scene_color_vertex {
    int32_t position[3];
    int32_t light_q8; /* Legacy light; flat opacity 0..255 when draw alpha=256. */
    uint32_t rgb24;
};
_Static_assert(sizeof(struct rf_gpu_scene_color_vertex)==20,"Scene color vertex ABI");

/* Eight 16-byte push-constant lanes. Scene converts fixed storage to float
 * before transforms. Legacy integer compatibility is rejected.
 * Q10 directions, milli scale, unsigned Q16 UV.
 * The first eight lanes form the 128-byte shader push-constant block.
 * integer_depth is reserved and must be zero. */
struct rf_gpu_graphics_draw {
    int32_t translation_scale[4];
    int32_t rotation[4]; /* sin, cos, bottom pivot y, reserved */
    int32_t camera[4];
    int32_t view[4]; /* direction x,z; pitch sin,cos */
    int32_t projection[4]; /* extent x,y; near=64; focal=width*3/4 */
    uint32_t material[4]; /* RGB, ignored legacy multiplier, textured, vertex format (2=color) */
    int32_t texture[4]; /* width,height, alpha (0=opaque,256=color-vertex alpha), screen mode (0/1/2) */
    /* Scene precision/material controls. Zero keeps ordinary defaults.
     * flags: public callers must pass zero; internal layer flags are owner-only.
     * units: local units/metre (0=512); shading: 0 flat, 1 smooth, 2 soft,
     * 3 unlit; filter: 0 nearest, 1 bilinear/repeat, 2 texture-set clamp/mip.
     * Filter 2 uses material[2]=one-based texture-set index (0 untextured),
     * and multiplies the sRGB base color by the texture in linear light. */
    int32_t quality[4];
    uint32_t first_index, index_count, double_sided;
    uint32_t integer_depth; /* Retired; nonzero is rejected. */
    /* Scene-only ordered pass. Zero preserves existing WORLD callers. */
    uint32_t scene_layer;
    /* Linear-light material; emissive is cd/m^2 times the linear base colour.
     * Zero roughness selects the neutral 0.65 default. */
    float roughness, metallic, emissive;
    /* Host-only: caller guarantees this entire draw is represented in the
     * current architecture input. Zero keeps ordinary shadow-map casting. */
    uint32_t architecture_occluder;
};

#define RF_GPU_LIGHT_CAP 160 /* 128 persistent fixtures plus 32 transient lights */
#define RF_GPU_LIGHT_WORDS 5
#define RF_GPU_SHADOW_CASCADES 3
#define RF_GPU_SHADOW_MAPS 5
#define RF_GPU_SHADOW_SIZE 1024
struct rf_gpu_light {
    float position_radius[4]; /* RFU position and finite influence radius */
    float color_intensity[4]; /* linear chromaticity (normalized to Y=1 on submission), peak cd */
    float direction_outer[4]; /* spot direction and outer cosine; -1 point */
    /* Inner cone cosine; owner-assigned shadow index; 1 = also in the stable
     * indirect source list (prevents duplicate injection); reserved. */
    float inner_shadow[4];
};
struct rf_gpu_lighting {
    float sun_direction[4];
    float sun_color[4]; /* linear chromaticity, normal-plane illuminance in lux */
    float environment[4]; /* RGB equivalent diffuse-fill lux; display exposure in w */
    uint32_t count;
    struct rf_gpu_light lights[RF_GPU_LIGHT_CAP];
    /* Presentation-only sky: coverage, density, base/thickness in km;
     * wind offset x/z in km, fixed seed, haze. Zero density disables clouds. */
    float sky_cloud[4],sky_weather[4];
    /* Per-view XZ cutaway rectangle, world ceiling and dissolve amount.
     * Shadow geometry is never clipped by this presentation operation. */
    float cutaway_bounds[4],cutaway_height[4];
    /* Presentation-only suppression for isolated diagnostic/model views. */
    uint32_t disable_indirect;
    /* Unoccluded horizontal sky illuminance (lux); zero retains legacy fill.
     * The procedural sky supplies direction/color, without its solar disc. */
    float sky_illuminance;
};
enum rf_gpu_graphics_scene_layer {
    RF_GPU_SCENE_WORLD, RF_GPU_SCENE_SKY, RF_GPU_SCENE_TRANSPARENT,
    RF_GPU_SCENE_EFFECTS, RF_GPU_SCENE_VIEWMODEL, RF_GPU_SCENE_OVERLAY,
    RF_GPU_SCENE_LAYER_COUNT
};
enum rf_gpu_graphics_submit_kind {
    RF_GPU_SUBMIT_UPLOAD, RF_GPU_SUBMIT_VERTEX_DIFF, RF_GPU_SUBMIT_SKIN_INPUT,
    RF_GPU_SUBMIT_SKINNING, RF_GPU_SUBMIT_DRAW,
    RF_GPU_SUBMIT_READBACK, RF_GPU_SUBMIT_KIND_COUNT
};
struct rf_gpu_graphics_stats {
    /* Diagnostic invocations (including overdraw), last completed frame.
     * Separate shader; never enabled for ordinary performance evidence. */
    uint32_t light_profile,light_ablation,light_tiles;
    uint64_t light_counts[7]; /* shaded, candidates, roof, sun, local, visible, PCF */
    uint64_t shadow_draws;
    uint32_t lights, shadow_maps, present_mode;
    uint64_t mesh_upload_bytes, texture_upload_bytes;
    uint64_t skin_reused;
    uint64_t instance_upload_bytes, indexed_draws, frames, target_builds;
    uint64_t bridge_roundtrips, bridge_transfer_bytes, raster_bridge_transfers;
    uint64_t queue_submits, fence_waits;
    double submit_wall_ms, fence_wait_wall_ms, bridge_wall_ms;
    uint64_t submits_by_kind[RF_GPU_SUBMIT_KIND_COUNT];
    double wait_ms_by_kind[RF_GPU_SUBMIT_KIND_COUNT];
    /* Unconfirmed predecessor at submit time, not a measured GPU duration. */
    uint64_t wait_frame, wait_predecessor_frame;
    /* Optional RF_GPU_PROFILE_TRIANGLE_UPDATE CPU walls, successful updates only. */
    double triangle_validate_ms, triangle_map_ms, triangle_copy_ms;
    double triangle_flush_ms, triangle_transfer_ms;
    uint64_t triangle_updates, triangle_update_bytes, triangle_flush_bytes;
    uint64_t triangle_staging_updates, triangle_direct_flags, triangle_staging_flags;
};
struct rf_gpu_graphics;
struct rf_gpu_graphics_resource;
/* Static, opaque architectural primitives in world RFU. Copies synchronously;
 * rebuild only on world changes, never from collision or camera visibility.
 * Empty input clears the previous world. In-flight replacement is rejected. */
struct rf_gpu_occlusion_primitive {
    float p[3][3];
    /* Nonzero: exact authored opaque cuboid, p[0]/p[1] = min/max.
     * This is not a mesh's approximate bounding box or a collision proxy. */
    uint32_t solid_box;
    uint32_t diffuse_rgb; /* sRGB architectural reflectance for probe GI. */
    uint32_t bottom_rgb; /* BOX -Y override, bit 24 present; zero inherits diffuse_rgb. */
};
int rf_gpu_graphics_set_architecture(struct rf_gpu_graphics *g,
    const struct rf_gpu_occlusion_primitive *triangles,uint32_t count);
/* DDGI volume placement and persistent fixture sources. GPU updates diffuse
 * irradiance/distance moments in batches; transient direct lights also inject.
 * Sources are camera-independent. Empty input clears the volume. */
#define RF_GPU_INDIRECT_LIGHT_CAP 128
int rf_gpu_graphics_set_indirect_lights(struct rf_gpu_graphics *g,
    const struct rf_gpu_light *lights,uint32_t count);
/* Geometry-based daylight coverage, independent of fixture locations.
 * Call after installing architecture; replacing architecture clears both fields. */
int rf_gpu_graphics_prepare_daylight(struct rf_gpu_graphics *g);
#define RF_GPU_GRAPHICS_TEXTURES 8
struct rf_gpu_graphics_texture_image {
    const uint32_t *rgb;
    uint32_t width, height;
};
struct rf_gpu_graphics_batch_item {
    struct rf_gpu_graphics_resource *resource;
    struct rf_gpu_graphics_draw draw;
};
struct rf_gpu_scene_timing {
    uint64_t frame_id;
    int supported, valid;
    double world_draw_ms, present_blit_ms;
    double sky_compute_ms; /* Included in world_draw_ms; excludes HDR composite. */
    /* Together with sky_compute_ms and detail_ms[0,6,7] partition world_draw_ms. Shadow includes
     * depth copies; main includes WORLD shading, HDR composite, tonemap/HUD. */
    double shadow_ms, main_scene_ms;
    /* tiles; WORLD+sky composite; transparent/effects; viewmodel; post; HUD. */
    double detail_ms[8]; /* final entries: DDGI probe update, receiver cache */
    /* CPU walls within native submit; separate from completed GPU queries. */
    double record_ms,acquire_ms,queue_submit_ms,present_ms;
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
/* Immutable opaque sRGB images; <=8 images of <=1024 square, UV clamp and
 * linear-light mips. Copies inputs synchronously. The owner may update only
 * after all borrowing views retire; in-flight mutation is rejected. Chunks can
 * share one set without copying images. Lifetime is reference-counted by the
 * graphics owner, independent of the resource used to create the set. */
int rf_gpu_graphics_resource_texture_set(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource *resource,
    const struct rf_gpu_graphics_texture_image *images,uint32_t count);
int rf_gpu_graphics_resource_share_textures(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource *resource,
    const struct rf_gpu_graphics_resource *source);
/* Update an owned, retired triangle-list resource created with sequential
 * indices (0,1,...). Retains texture, indices and descriptors. Returns 1 if
 * capacity must grow, 0 on success, -1 on invalid input/failure. Caller must
 * retire every borrowing view before updating; skinned resources reject. */
int rf_gpu_graphics_triangle_resource_update(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource *resource,
    const struct rf_gpu_graphics_vertex *vertices, uint32_t vertex_count);
/* Untextured Scene-only 20-byte triangle color: Q8 light in [0,384],
 * RGB24 equal at all three indexed corners. Draw material[3]=2;
 * no integer-depth, screen mode or form lighting. */
struct rf_gpu_graphics_resource *rf_gpu_graphics_scene_color_resource_create(
    struct rf_gpu_graphics *g,
    const struct rf_gpu_scene_color_vertex *vertices,uint32_t vertex_count,
    const uint32_t *indices,uint32_t index_count);
int rf_gpu_graphics_scene_color_resource_update(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource *resource,
    const struct rf_gpu_scene_color_vertex *vertices,uint32_t vertex_count);
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
 * NULL bind with zero bind words retains the resource's previously uploaded
 * immutable bind data; the caller must prove the asset and bind policy match.
 * Returns 0 on success, 1 when capacity must grow, and -1 on failure. */
int rf_gpu_graphics_skinned_resource_update(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource *resource, uint32_t vertex_count,
    const uint32_t *bind_words, uint32_t bind_word_count,
    const uint32_t *palette_words, uint32_t palette_word_count);
/* Owner updates only, after all borrowing views retire. Updates copy input immediately but defer
 * dispatch until end, which submits and waits once. Cold creates remain
 * synchronous. No draw/readback or second update of a queued resource before
 * end. Cancel discards unsubmitted dispatches; caller must update again before
 * using those resources. Failed end requires owner teardown. */
int rf_gpu_graphics_skin_batch_begin(struct rf_gpu_graphics *g);
int rf_gpu_graphics_skin_batch_end(struct rf_gpu_graphics *g);
void rf_gpu_graphics_skin_batch_cancel(struct rf_gpu_graphics *g);
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

/* Caller shuts graphics down before its shared backend context. Calls are
 * synchronous except updates inside an explicit skin batch (end waits), and
 * the explicitly retired Scene submission below.
 * Failure never invokes CPU lowering. */
struct rf_gpu_graphics *rf_gpu_graphics_create(struct rf_gpu_vulkan_context *ctx);
/* Smoothstep spot flux integral; a negative outer cosine denotes a point. */
float rf_gpu_light_peak_candela(float lumens,float outer_cosine,float inner_cosine);
/* Read the last explicit capture's linear RGBA16F values as floats. RGB is
 * cd/m^2 / 100, or direct/GI/fill lux / 100 with RF_GPU_GI_DEBUG=3. */
int rf_gpu_graphics_capture_hdr(struct rf_gpu_graphics *g,float *rgba,uint32_t capacity);
void rf_gpu_lighting_default(struct rf_gpu_lighting *lighting);
int rf_gpu_graphics_set_lighting(struct rf_gpu_graphics *g,
    const struct rf_gpu_lighting *lighting);
/* Linear HDR clear color for isolated model previews. Default is black;
 * world SKY geometry remains authoritative wherever it is present. */
int rf_gpu_graphics_scene_background(struct rf_gpu_graphics *g,float red,float green,float blue);
int rf_gpu_graphics_lighting_regression(struct rf_gpu_graphics *g);
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
/* A persistent auxiliary camera target. This draws and retires synchronously
 * without acquiring the swapchain or reading pixels to the CPU. */
int rf_gpu_graphics_scene_offscreen(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_batch_item *items,uint32_t count,uint64_t frame_id);
/* One opaque device-local video image, composed after tonemapping and before
 * HUD geometry. Source must have completed its draw and outlive the receiver's
 * submission. NULL clears the binding. Both owners must share a device. */
int rf_gpu_graphics_scene_video(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics *source,int x,int y,int width,int height);
/* Independent sources composite before HUD. Clearing one slot preserves others. */
#define RF_GPU_GRAPHICS_VIDEO_SLOTS 2
int rf_gpu_graphics_scene_video_at(struct rf_gpu_graphics *g,unsigned slot,
    struct rf_gpu_graphics *source,int x,int y,int width,int height);
void rf_gpu_graphics_scene_timing(const struct rf_gpu_graphics *g,
    struct rf_gpu_scene_timing *timing);
/* Explicit diagnostic only: direct attachment readback, no Raster conversion. */
int rf_gpu_graphics_scene_capture(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_batch_item *items, uint32_t count,
    uint32_t *rgba, float *depth, uint32_t capacity);

/* Draw-pass timestamps only; excludes upload/readback and uses caller frame ID. */
int rf_gpu_graphics_scene_capture_at(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_batch_item *items,uint32_t count,
    uint32_t *rgba,float *depth,uint32_t capacity,uint64_t frame_id);
#endif
