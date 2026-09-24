#ifndef RF_GPU_SCENE_WORLD_GPU_H
#define RF_GPU_SCENE_WORLD_GPU_H

#include "rf_gpu_scene_world.h"
#include "rf_gpu_scene_enemy.h"
#include "rasterfall_camera.h"

struct rf_gpu_resource_cache;
struct rf_gpu_graphics;
struct rf_gpu_graphics_batch_item;
struct rf_gpu_vulkan_context;
struct rf_gpu_scene_pose_v1;
struct rf_gpu_scene_actor_gpu;
struct toy_texture_view;

#define RF_GPU_SCENE_PICKUP_MODEL_COUNT 7
#define RF_GPU_SCENE_PICKUP_MAX_PRIMITIVES 4
#define RF_GPU_SCENE_PICKUP_SHAPE_COUNT 14
struct rf_gpu_scene_pickup_asset {
    struct rf_gpu_graphics_resource *part[RF_GPU_SCENE_PICKUP_MAX_PRIMITIVES];
    uint32_t index_count[RF_GPU_SCENE_PICKUP_MAX_PRIMITIVES];
    uint32_t color[RF_GPU_SCENE_PICKUP_MAX_PRIMITIVES];
    uint32_t primitive_count, texture_width, texture_height;
    int scale, min_y, textured;
};

/* Call after registry frame_begin, before any Scene target write. Pins every
 * referenced world handle, prepares immutable GPU submeshes, and emits WORLD
 * opaque draws. The caller owns frame completion and cache collection. */
int rf_gpu_scene_world_gpu_prepare(struct rf_gpu_scene_world_resources *owner,
    struct rf_gpu_resource_cache *cache,struct rf_gpu_graphics *graphics,
    const struct camera *camera,uint32_t width,uint32_t height,
    struct rf_gpu_graphics_batch_item *items,uint32_t capacity,uint32_t *count);

struct rf_gpu_scene_world_gpu_probe {
    /* Stage 3 preview: synchronous native Scene instead of audit readback. */
    int native_present;
    struct rf_gpu_graphics_resource *enemy[TOY_GAME_MAX_ENEMIES+TOY_GAME_MAX_ACTORS];
    struct rf_gpu_graphics *graphics;
    struct rf_gpu_resource_cache *cache;
    struct rf_gpu_scene_actor_gpu *actor[TOY_GAME_MAX_ACTORS];
    struct rf_gpu_graphics_resource *flag_pole, *flag_cloth;
    struct rf_gpu_graphics_resource *flag_label[RF_GPU_SCENE_FLAG_CAP];
    char flag_label_text[RF_GPU_SCENE_FLAG_CAP][5];
    uint32_t flag_label_indices[RF_GPU_SCENE_FLAG_CAP];
    struct rf_gpu_graphics_resource *projectile_asset[2];
    uint32_t projectile_indices[2], projectile_color[2];
    int projectile_scale[2];
    uint32_t projectile_texture_width, projectile_texture_height;
    struct rf_gpu_scene_pickup_asset pickup[RF_GPU_SCENE_PICKUP_MODEL_COUNT];
    struct rf_gpu_graphics_resource *pickup_shape[RF_GPU_SCENE_PICKUP_SHAPE_COUNT];
    struct rf_gpu_graphics_resource *pickup_pedestal[TOY_MAP_MAX_PICKUPS];
    int pickup_pedestal_y[TOY_MAP_MAX_PICKUPS];
};
struct rf_gpu_scene_world_gpu_probe_stats {
    uint32_t draws, actor_draws, flag_draws, flag_text_draws;
    uint32_t enemy_draws, enemy_items, enemy_deferred, enemy_culled;
    uint32_t procedural_draws, procedural_items;
    uint32_t projectile_draws, pickup_model_draws, pickup_model_items;
    uint32_t pickup_procedural_draws, pickup_procedural_items;
    uint32_t pickup_procedural_deferred, covered_pixels;
    uint32_t prop_draws, prop_deferred, prop_culled;
    uint32_t prop_numeric_deferred, prop_material_deferred, prop_transparent_deferred;
    uint64_t uploads, hits;
    int64_t prepare_us,geometry_extract_us;
    uint64_t upload_bytes,bridge_transfers;
    double gpu_draw_ms;
    int gpu_time_valid;
};
/* Explicit normal-frame audit only: offscreen draw plus readback, with the
 * same frozen world handles and camera as that completed normal frame. */
int rf_gpu_scene_world_gpu_probe_frame(struct rf_gpu_scene_world_gpu_probe *probe,
    struct rf_gpu_vulkan_context *context,struct rf_gpu_scene_world_resources *owner,
    const struct camera *camera,uint32_t width,uint32_t height,
    const struct rf_gpu_scene_pose_v1 *poses,uint32_t pose_count,
    const struct rf_gpu_scene_flag_frame_v1 *flags,
    const struct rf_gpu_scene_projectile_frame_v1 *projectiles,
    const struct rf_gpu_scene_interactable_frame_v1 *interactables,
    const struct rf_gpu_scene_enemy_frame_v1 *enemies,
    const struct toy_texture_view *model_texture,
    struct rf_gpu_scene_world_gpu_probe_stats *stats,const char *capture_path);
void rf_gpu_scene_world_gpu_probe_close(struct rf_gpu_scene_world_gpu_probe *probe);

#endif
