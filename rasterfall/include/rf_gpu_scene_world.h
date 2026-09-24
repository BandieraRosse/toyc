#ifndef RF_GPU_SCENE_WORLD_H
#define RF_GPU_SCENE_WORLD_H

#include "rf_gpu_scene_frame.h"
#include "rasterfall_map.h"
#include "rasterfall_render_resources.h"
#include "rasterfall_prop.h"
#include "rasterfall_world_light.h"

#define RF_GPU_SCENE_WORLD_RENDER_ABI_V1 1U
enum rf_gpu_scene_world_opaque_class {
    RF_GPU_SCENE_WORLD_WALL,
    RF_GPU_SCENE_WORLD_BOX,
    RF_GPU_SCENE_WORLD_RAMP,
    RF_GPU_SCENE_WORLD_PLATFORM,
    RF_GPU_SCENE_WORLD_FLOOR,
    RF_GPU_SCENE_WORLD_BOUNDARY,
    RF_GPU_SCENE_WORLD_MODEL_BOX,
    RF_GPU_SCENE_WORLD_SIGN,
    RF_GPU_SCENE_WORLD_MODEL_LEGACY,
    RF_GPU_SCENE_WORLD_MODEL_SPECIAL,
    RF_GPU_SCENE_WORLD_MODEL_INFECTED,
    RF_GPU_SCENE_WORLD_OPAQUE_CLASS_COUNT
};
struct rasterfall_model_asset;
struct rf_gpu_scene_world_render_item_v1 {
    char id[RF_GPU_SCENE_WORLD_ID_CAP];
    uint32_t submission_ordinal;
    int visible, alpha;
    struct toy_map_draw draw;
};
struct rf_gpu_scene_world_render_frame_v1 {
    uint32_t abi_version, byte_size;
    uint64_t frame_id, world_generation, map_generation;
    uint32_t count;
    struct rf_gpu_scene_world_render_item_v1 items[RF_GPU_SCENE_MAX_WORLD_V2];
};
struct rf_gpu_scene_world_floor_frame_v1 {
    uint64_t frame_id, world_generation, map_generation;
    int minx, maxx, minz, maxz, authored_ground;
    uint32_t spawn_count;
    struct toy_map_zone spawn_zones[TOY_MAP_MAX_ZONES];
    struct rasterfall_diagnostic_world_lighting_v1 model_light_v1;
};
struct rf_gpu_scene_world_prop_item_v1 {
    char id[RF_MAP_RUNTIME_ID_CAP];
    uint32_t submission_ordinal;
    int scene_light_q8;
    struct toy_map_prop prop;
};
struct rf_gpu_scene_world_prop_frame_v1 {
    uint64_t frame_id, world_generation, map_generation;
    uint32_t count;
    struct rf_gpu_scene_world_prop_item_v1 items[TOY_MAP_MAX_PROPS];
};
struct rasterfall_session;
#define RF_GPU_SCENE_FLAG_CAP 8
struct rf_gpu_scene_flag_item_v1 {
    int active, x, z, color, selected;
    char label[5];
};
struct rf_gpu_scene_flag_frame_v1 {
    uint64_t frame_id, world_generation;
    uint32_t count;
    struct rf_gpu_scene_flag_item_v1 items[RF_GPU_SCENE_FLAG_CAP];
};
int rf_gpu_scene_flag_freeze(const struct rasterfall_session *session,
    uint64_t frame_id,uint64_t world_generation,
    struct rf_gpu_scene_flag_frame_v1 *flags);

#define RF_GPU_SCENE_PROJECTILE_CAP 16
struct rf_gpu_scene_projectile_item_v1 {
    uint32_t source_slot;
    int kind, x, y, z, age_ms, flash_ms, scene_light_q8;
};
struct rf_gpu_scene_projectile_frame_v1 {
    uint64_t frame_id, world_generation;
    uint32_t count;
    struct rf_gpu_scene_projectile_item_v1 items[RF_GPU_SCENE_PROJECTILE_CAP];
};
struct toy_game;
int rf_gpu_scene_projectile_freeze(const struct toy_game *game,
    const struct rasterfall_world_lighting *lighting,
    uint64_t frame_id,uint64_t world_generation,
    struct rf_gpu_scene_projectile_frame_v1 *projectiles);

struct rasterfall_effects;
struct rf_gpu_scene_interactable_item_v1 {
    uint32_t source_slot;
    int kind, weapon, x, y, z, highlight_on, scene_light_q8;
};
struct rf_gpu_scene_interactable_frame_v1 {
    uint64_t frame_id, world_generation;
    uint32_t count;
    struct rf_gpu_scene_interactable_item_v1 items[TOY_MAP_MAX_PICKUPS];
};
int rf_gpu_scene_interactable_freeze(const struct rasterfall_session *session,
    const struct rasterfall_effects *effects,
    const struct rasterfall_world_lighting *lighting,int visible,
    uint64_t frame_id,uint64_t world_generation,
    struct rf_gpu_scene_interactable_frame_v1 *interactables);

/* Copy authored render IDs in the exact order used by the compatibility
 * projection. Values are frozen before Scene extraction; no map pointers
 * survive into the snapshot. */
int rf_gpu_scene_world_capture(const struct rasterfall_map_state *map,
    int air_walls_enabled,struct rf_gpu_scene_world_input_v2 *out,
    uint32_t capacity,uint32_t *count);
int rf_gpu_scene_world_render_freeze(const struct rasterfall_map_state *map,
    int air_walls_enabled,uint64_t frame_id,uint64_t world_generation,
    struct rf_gpu_scene_world_input_v2 *world,uint32_t capacity,
    uint32_t *count,struct rf_gpu_scene_world_render_frame_v1 *render);
int rf_gpu_scene_world_render_validate(
    const struct rf_gpu_scene_snapshot_v2 *snapshot,
    const struct rf_gpu_scene_world_render_frame_v1 *render);
int rf_gpu_scene_world_floor_freeze(const struct rasterfall_map_state *map,
    int authored_ground,uint64_t frame_id,uint64_t world_generation,
    struct rf_gpu_scene_world_floor_frame_v1 *floor);
int rf_gpu_scene_world_prop_freeze(const struct rasterfall_map_state *map,
    const struct rasterfall_world_lighting *lighting,
    uint64_t frame_id,uint64_t world_generation,
    struct rf_gpu_scene_world_prop_frame_v1 *props);
/* Builds four persistent map classes, partitioned floor, boundary walls,
 * legacy model cuboids, world signs, and legacy/special display silhouettes from
 * frozen values. V2 lighting still samples the active world-light state
 * during build; cuboids and display silhouettes use frozen V1.
 * Call before the active map generation changes. The caller owns each returned model
 * and must unload/free it. Unhandled visible content is counted explicitly. */
int rf_gpu_scene_world_opaque_mesh_build(
    const struct rf_gpu_scene_snapshot_v2 *snapshot,
    const struct rf_gpu_scene_world_render_frame_v1 *render,
    const struct rf_gpu_scene_world_floor_frame_v1 *floor,
    const struct rf_gpu_scene_world_prop_frame_v1 *props,
    struct rasterfall_model_asset *models[RF_GPU_SCENE_WORLD_OPAQUE_CLASS_COUNT],
    uint32_t *accepted,uint32_t *deferred,uint32_t *transparent,
    uint32_t *prop_accepted);
int rf_gpu_scene_world_floor_mesh_build(
    const struct rf_gpu_scene_world_render_frame_v1 *render,
    const struct rf_gpu_scene_world_floor_frame_v1 *floor,
    struct rasterfall_model_asset **model);
int rf_gpu_scene_world_boundary_mesh_build(
    const struct rf_gpu_scene_world_prop_frame_v1 *props,
    struct rasterfall_model_asset **model,uint32_t *accepted);

/* Dedicated world resource owner. Prepare before frame pin; a changed world
 * retires old handles while an in-flight frame may still hold its pins. */
struct rf_gpu_scene_world_resources {
    struct rasterfall_resource_registry registry;
    struct rasterfall_resource_handle opaque[RF_GPU_SCENE_WORLD_OPAQUE_CLASS_COUNT];
    struct rasterfall_resource_handle prop_asset[RASTERFALL_PROP_ASSET_COUNT+1];
    uint64_t world_generation, map_generation, light_generation;
    struct rf_gpu_scene_world_floor_frame_v1 floor;
    struct rf_gpu_scene_world_prop_item_v1 prop_items[TOY_MAP_MAX_PROPS];
    struct rf_gpu_scene_world_render_item_v1 render_items[RF_GPU_SCENE_MAX_WORLD_V2];
    uint32_t render_count, prop_count;
    uint32_t accepted, deferred, transparent, prop_accepted, prop_asset_count;
    uint32_t prop_draws, prop_deferred, prop_culled;
    uint32_t prop_numeric_deferred, prop_material_deferred, prop_transparent_deferred;
};
int rf_gpu_scene_world_resources_prepare(struct rf_gpu_scene_world_resources *owner,
    const struct rf_gpu_scene_snapshot_v2 *snapshot,
    const struct rf_gpu_scene_world_render_frame_v1 *render,
    const struct rf_gpu_scene_world_floor_frame_v1 *floor,
    const struct rf_gpu_scene_world_prop_frame_v1 *props,
    uint64_t light_generation);
void rf_gpu_scene_world_resources_invalidate(struct rf_gpu_scene_world_resources *owner);

#endif
