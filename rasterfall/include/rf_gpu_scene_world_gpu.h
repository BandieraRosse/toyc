#ifndef RF_GPU_SCENE_WORLD_GPU_H
#define RF_GPU_SCENE_WORLD_GPU_H

#include "rf_gpu_scene_world.h"
#include "rf_gpu_scene_enemy.h"
#include "rasterfall_camera.h"
#include "rasterfall_hud.h"
#include "rasterfall_prop.h"
#include "rf_mesh_weaver_presentation.h"
struct rasterfall_effects;
enum rf_gpu_scene_aux_kind { RF_GPU_AUX_WORLD, RF_GPU_AUX_WEAPON };
enum rf_gpu_scene_aux_state {
    RF_GPU_AUX_HIDDEN, RF_GPU_AUX_CONNECTING, RF_GPU_AUX_LIVE, RF_GPU_AUX_UNAVAILABLE
};
/* Runtime selects the visible views. Layout is in final framebuffer pixels;
 * render extent and cadence are independent of UI scale. The WORLD transform
 * comes from the real camera entity, never from the main player's camera. */
struct rf_gpu_scene_aux_view {
    int visible,kind,shell_visible;
    uint64_t stable_id,generation,now_us;
    struct camera camera;
    int x,y,width,height;
    unsigned render_width,render_height,refresh_hz;
    int weapon,rotation_degrees;
};
struct rf_gpu_scene_aux_status {
    int state;
    uint64_t frames,last_update_us;
    int64_t last_cpu_us;
    uint64_t last_upload_bytes;
    double last_gpu_ms;
    int gpu_time_valid;
};
/* Consumed synchronously into immutable geometry before GPU target writes. */
struct rf_gpu_scene_layers_input {
    const struct rf_gpu_scene_world_prop_frame_v1 *props;
    unsigned host_time_ms,lighting_time_ms;
    struct rasterfall_electronics_frame electronics;
    struct rf_mesh_weaver_frame weaver;
    const struct toy_game *source_game;
    const struct rasterfall_effects *source_effects;
    const struct rf_gpu_scene_enemy_frame_v1 *actor_presentations;
    const struct rf_gpu_scene_world_render_frame_v1 *map;
    struct rasterfall_hud_state hud;
    int fps,paused,pause_selected,viewmodel_light,show_viewmodel;
    int flashlight,lighting_lab,fixed_lighting;
    /* Explicit session hints; renderer never infers or changes mission truth. */
    int frontier_actor_prewarm,frontier_actor_warm_activate;
    float cutaway_bounds[4],cutaway_height[4];
    void *ui_context;
    void (*ui_layout)(void *, struct rasterfall_canvas *);
    const struct rf_gpu_scene_aux_view *aux_view;
    const struct rf_gpu_scene_aux_view *unit_view;
    int world_only; /* Camera texture: no HUD, names or viewmodel. */
    const struct camera *light_camera;
};

struct rf_gpu_resource_cache;
struct rf_gpu_graphics;
struct rf_gpu_graphics_batch_item;
struct rf_gpu_vulkan_context;
struct rf_gpu_scene_pose_v1;
struct rf_gpu_scene_actor_gpu;
struct toy_texture_view;
struct scene_layer_workspace;
struct scene_enemy_mesh;
struct scene_enemy_cached;
struct scene_actor_warm_pool;

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

#define RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES 16384U
#define RF_GPU_SCENE_LAYER_CHUNKS 16U
struct rf_gpu_scene_aux_slot {
    struct rf_gpu_scene_world_gpu_probe *owner;
    struct rf_gpu_scene_aux_status status;
    uint64_t id,generation,world_generation,next_us;
    unsigned attempts;
    int visible,kind;
};
struct rf_gpu_scene_world_gpu_probe {
    /* Stage 3 preview: synchronous native Scene instead of audit readback. */
    int native_present;
    int quiet;
    int slow_profile_state; /* 0: unchecked, 1: off, 2: bounded slow profile. */
    int offscreen_only;
    /* Synchronous child borrows the parent frame; never owns its resources. */
    struct rf_gpu_scene_world_gpu_probe *shared_parent;
    uint32_t shared_actor_first,shared_actor_count;
    uint64_t prewarmed_generation;
    uint64_t lighting_world_generation,lighting_map_generation;
    /* Transient first-frame observer; negative return cancels startup. */
    int (*startup_event)(void *, const char *, int, int64_t);
    void *startup_event_context;
    struct rf_gpu_scene_aux_slot aux[2]; /* 0: story/device, 1: RTS unit. */
    struct rf_gpu_graphics_resource *lighting_lab_sphere;
    struct rf_mesh_weaver_gpu *weaver;
    const struct rf_gpu_scene_layers_input *layers;
    struct rf_gpu_graphics_resource *layer_resource[RF_GPU_SCENE_LAYER_CHUNKS][2];
    struct scene_layer_workspace *layer_workspace;
    struct scene_enemy_mesh *enemy_workspace;
    struct scene_enemy_cached *enemy_cached;
    int enemy_vertex_color;
    struct rf_gpu_graphics_batch_item *batch;
    uint32_t batch_capacity;
    struct rf_gpu_graphics_resource *enemy[RF_GPU_SCENE_ENEMY_CAPACITY+TOY_GAME_MAX_ACTORS];
    struct rf_gpu_graphics *graphics;
    struct rf_gpu_resource_cache *cache;
    struct rf_gpu_scene_actor_gpu *actor[TOY_GAME_MAX_ACTORS];
    struct scene_actor_warm_pool *actor_warm_pool;
    struct rf_gpu_graphics_resource *flag_pole;
    struct rf_gpu_graphics_resource *flag_label[RF_GPU_SCENE_FLAG_CAP];
    char flag_label_text[RF_GPU_SCENE_FLAG_CAP][5];
    uint32_t flag_label_indices[RF_GPU_SCENE_FLAG_CAP];
    uint32_t flag_panel_color[RF_GPU_SCENE_FLAG_CAP];
    int flag_panel_facing[RF_GPU_SCENE_FLAG_CAP];
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
    uint32_t lights, shadow_maps, shadow_draws, present_mode;
    uint32_t draws, actor_draws, flag_draws, flag_text_draws;
    uint32_t layer_draws[6];
    uint32_t enemy_draws, enemy_items, enemy_deferred, enemy_culled;
    uint32_t enemy_prepare_culled;
    uint32_t enemy_geometry_reused,skin_reused;
    uint32_t procedural_draws, procedural_items;
    uint32_t dynamic_reused,dynamic_created,enemy_triangles;
    uint32_t layer_reused,layer_created;
    uint32_t projectile_draws, pickup_model_draws, pickup_model_items;
    uint32_t pickup_procedural_draws, pickup_procedural_items;
    uint32_t pickup_procedural_deferred, covered_pixels;
    uint32_t prop_draws, prop_deferred, prop_culled;
    uint32_t prop_numeric_deferred, prop_material_deferred, prop_transparent_deferred;
    uint64_t uploads, hits;
    int64_t prepare_us,geometry_extract_us;
    int64_t enemy_upload_us,enemy_draw_prepare_us;
    int64_t world_prepare_us,actor_prepare_us,enemy_prepare_us,layer_prepare_us,submit_retire_us;
    int64_t submit_present_us,retire_us;
    int64_t record_us,acquire_us,queue_submit_us,present_us;
    int64_t actor_batch_us,misc_prepare_us;
    int64_t weaver_prepare_us;
    /* Opt-in slow profile: only completed fresh child frames have timings. */
    int64_t aux_prepare_us;
    uint32_t aux_refresh_mask,aux_gpu_valid_mask;
    uint64_t gpu_query_frame; /* Existing main retired query, slow profile only. */
    uint64_t aux_frames_before[2],aux_frames_after[2];
    int64_t aux_cpu_us[2],aux_gpu_us[2];
    int64_t layer_extract_us,layer_clip_us,layer_pack_us,layer_upload_us,layer_batch_us;
    uint32_t layer_triangles,layer_culled;
    uint64_t upload_bytes,bridge_transfers;
    double gpu_draw_ms;
    double gpu_sky_ms;
    uint32_t native_draws;
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
