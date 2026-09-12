#ifndef RASTERFALL_MAP_RUNTIME_H
#define RASTERFALL_MAP_RUNTIME_H

/* Runtime-facing view of a V1 map.  This header deliberately does not expose
 * the parser IR or any toy_game/rendering type. */

#define RF_MAP_RUNTIME_ID_CAP 64
#define RF_MAP_RUNTIME_KIND_CAP 32
#define RF_MAP_RUNTIME_ACTION_CAP 32
#define RF_MAP_RUNTIME_VALUE_CAP 96
#define RF_MAP_RUNTIME_MAX_ATTRIBUTES 8

struct rf_map_runtime_bounds {
    int min_x, max_x, min_z, max_z;
};

struct rf_map_runtime_world {
    struct rf_map_runtime_bounds bounds;
    int room_limit;
    int has_room_limit;
};

struct rf_map_runtime_collision {
    char id[RF_MAP_RUNTIME_ID_CAP];
    char shape[RF_MAP_RUNTIME_KIND_CAP];
    struct rf_map_runtime_bounds bounds;
    int height;
    int height2;
    int has_height2;
    int collision;
    int visible;
    int walkable;
    int blocks_airborne;
    char color[RF_MAP_RUNTIME_KIND_CAP];
    int has_color;
    char role[RF_MAP_RUNTIME_KIND_CAP];
    int has_role;
    int legacy_index;
    int has_legacy_index;
    int line;
};

struct rf_map_runtime_surface {
    char id[RF_MAP_RUNTIME_ID_CAP];
    char kind[RF_MAP_RUNTIME_KIND_CAP];
    struct rf_map_runtime_bounds bounds;
    int height;
    int height2;
    int has_height2;
    char axis[RF_MAP_RUNTIME_KIND_CAP];
    int has_axis;
    char material[RF_MAP_RUNTIME_KIND_CAP];
    int has_material;
    int legacy_index;
    int has_legacy_index;
    int line;
};

struct rf_map_runtime_region {
    char id[RF_MAP_RUNTIME_ID_CAP];
    char kind[RF_MAP_RUNTIME_KIND_CAP];
    struct rf_map_runtime_bounds bounds;
    int legacy_index;
    int has_legacy_index;
    int line;
};

struct rf_map_runtime_interaction {
    char id[RF_MAP_RUNTIME_ID_CAP];
    char action[RF_MAP_RUNTIME_ACTION_CAP];
    int action_id;
    int legacy_index;
    int has_legacy_index;
    int x, y, z;
    int line;
};

struct rf_map_runtime_actor_spawn {
    char id[RF_MAP_RUNTIME_ID_CAP];
    char class_name[RF_MAP_RUNTIME_KIND_CAP];
    char weapon[RF_MAP_RUNTIME_KIND_CAP];
    int base_id;
    int x, y, z;
    int downed;
    int has_weapon;
    int legacy_index;
    int has_legacy_index;
    int line;
};

struct rf_map_runtime_pickup {
    char id[RF_MAP_RUNTIME_ID_CAP];
    char kind[RF_MAP_RUNTIME_KIND_CAP];
    int x, y, z;
    int legacy_index;
    int has_legacy_index;
    int line;
};

struct rf_map_runtime_object {
    char id[RF_MAP_RUNTIME_ID_CAP];
    char kind[RF_MAP_RUNTIME_KIND_CAP];
    int x, y, z;
    int yaw;
    int scale;
    int legacy_index;
    int has_legacy_index;
    int line;
};

struct rf_map_runtime_attribute {
    char key[RF_MAP_RUNTIME_KIND_CAP];
    char value[RF_MAP_RUNTIME_VALUE_CAP];
};

struct rf_map_runtime_render {
    char id[RF_MAP_RUNTIME_ID_CAP];
    char kind[RF_MAP_RUNTIME_KIND_CAP];
    struct rf_map_runtime_bounds bounds;
    int x, y, z;
    int has_position;
    char asset[RF_MAP_RUNTIME_VALUE_CAP];
    int has_asset;
    int height;
    int has_height;
    char color[RF_MAP_RUNTIME_VALUE_CAP];
    int has_color;
    struct rf_map_runtime_attribute attributes[RF_MAP_RUNTIME_MAX_ATTRIBUTES];
    int attribute_count;
    int legacy_index;
    int has_legacy_index;
    int line;
};

enum rf_map_runtime_action {
    RF_MAP_ACTION_UNKNOWN = -1,
    RF_MAP_ACTION_WAVE_SKIP,
    RF_MAP_ACTION_ENEMY_DEATH_TEST,
    RF_MAP_ACTION_HUMANOID_ACTIONS,
    RF_MAP_ACTION_HORDE,
    RF_MAP_ACTION_AIR,
    RF_MAP_ACTION_ALARM,
    RF_MAP_ACTION_HEAVY,
    RF_MAP_ACTION_FAST,
    RF_MAP_ACTION_BASE1,
    RF_MAP_ACTION_BASE2,
    RF_MAP_ACTION_SMOKER,
    RF_MAP_ACTION_CHARGER,
    RF_MAP_ACTION_TANK,
    RF_MAP_ACTION_MONEY,
    RF_MAP_ACTION_CLEAR_HIRED,
    RF_MAP_ACTION_ATTACK_X2,
    RF_MAP_ACTION_ATTACK_X3,
    RF_MAP_ACTION_ATTACK_X4,
    RF_MAP_ACTION_POSE,
    RF_MAP_ACTION_POSE_PREV_BONE,
    RF_MAP_ACTION_POSE_NEXT_BONE,
    RF_MAP_ACTION_POSE_AXIS_X,
    RF_MAP_ACTION_POSE_AXIS_Y,
    RF_MAP_ACTION_POSE_AXIS_Z,
    RF_MAP_ACTION_POSE_DECREASE,
    RF_MAP_ACTION_POSE_INCREASE,
    RF_MAP_ACTION_POSE_EXPORT,
    RF_MAP_ACTION_POSE_TOGGLE_LAYER,
    RF_MAP_ACTION_PICKUP_SMG,
    RF_MAP_ACTION_PICKUP_SHOTGUN,
    RF_MAP_ACTION_PICKUP_AMMO,
    RF_MAP_ACTION_PICKUP_SHOP,
    RF_MAP_ACTION_PICKUP_AK,
    RF_MAP_ACTION_PICKUP_AWP,
    RF_MAP_ACTION_PICKUP_AXE,
    RF_MAP_ACTION_PICKUP_BOMB,
    RF_MAP_ACTION_PICKUP_MOLOTOV,
    RF_MAP_ACTION_PICKUP_PILL,
    RF_MAP_ACTION_ANIM_IDLE,
    RF_MAP_ACTION_ANIM_WALK,
    RF_MAP_ACTION_ANIM_JOG,
    RF_MAP_ACTION_GLB_IDLE,
    RF_MAP_ACTION_GLB_WALK,
    RF_MAP_ACTION_GLB_JOG,
    RF_MAP_ACTION_VMD_WALK,
    RF_MAP_ACTION_VMD_MANJUSAKA,
    RF_MAP_ACTION_ANIMATION_COMPOSITION,
    RF_MAP_ACTION_HUMANOID_POSE_DEBUG,
    RF_MAP_ACTION_WEST_CORRIDOR,
    RF_MAP_ACTION_WEST_CORRIDOR_NO_TANK,
    RF_MAP_ACTION_STATION_TERMINAL,
    RF_MAP_ACTION_OPERATIONS_TERMINAL,
    RF_MAP_ACTION_SUPER_TERMINAL,
    RF_MAP_ACTION_RETURN_OUTPOST,
    RF_MAP_ACTION_COUNT
};

/* The implementation owns the parser IR behind this opaque handle. */
struct rf_map_runtime {
    void *impl;
    int error_line;
    char error[192];
};

int rf_map_runtime_load(struct rf_map_runtime *runtime, const char *path);
void rf_map_runtime_unload(struct rf_map_runtime *runtime);
const struct rf_map_runtime_world *rf_map_runtime_world_info(
    const struct rf_map_runtime *runtime);
int rf_map_runtime_region_count(const struct rf_map_runtime *runtime);
int rf_map_runtime_collision_count(const struct rf_map_runtime *runtime);
int rf_map_runtime_surface_count(const struct rf_map_runtime *runtime);
const struct rf_map_runtime_surface *rf_map_runtime_surface_at(
    const struct rf_map_runtime *runtime, int index);
const struct rf_map_runtime_surface *rf_map_runtime_find_surface(
    const struct rf_map_runtime *runtime, const char *id);
const struct rf_map_runtime_collision *rf_map_runtime_collision_at(
    const struct rf_map_runtime *runtime, int index);
const struct rf_map_runtime_collision *rf_map_runtime_find_collision(
    const struct rf_map_runtime *runtime, const char *id);
const struct rf_map_runtime_region *rf_map_runtime_region_at(
    const struct rf_map_runtime *runtime, int index);
const struct rf_map_runtime_region *rf_map_runtime_find_region(
    const struct rf_map_runtime *runtime, const char *id);
int rf_map_runtime_find_regions_by_kind(
    const struct rf_map_runtime *runtime, const char *kind,
    const struct rf_map_runtime_region **out, int capacity);
int rf_map_runtime_interaction_count(const struct rf_map_runtime *runtime);
const struct rf_map_runtime_interaction *rf_map_runtime_interaction_at(
    const struct rf_map_runtime *runtime, int index);
const struct rf_map_runtime_interaction *rf_map_runtime_find_interaction(
    const struct rf_map_runtime *runtime, const char *id);
int rf_map_runtime_actor_spawn_count(const struct rf_map_runtime *runtime);
const struct rf_map_runtime_actor_spawn *rf_map_runtime_actor_spawn_at(
    const struct rf_map_runtime *runtime, int index);
const struct rf_map_runtime_actor_spawn *rf_map_runtime_find_spawn(
    const struct rf_map_runtime *runtime, const char *id);
int rf_map_runtime_pickup_count(const struct rf_map_runtime *runtime);
const struct rf_map_runtime_pickup *rf_map_runtime_pickup_at(
    const struct rf_map_runtime *runtime, int index);
const struct rf_map_runtime_pickup *rf_map_runtime_find_pickup(
    const struct rf_map_runtime *runtime, const char *id);
int rf_map_runtime_object_count(const struct rf_map_runtime *runtime);
const struct rf_map_runtime_object *rf_map_runtime_object_at(
    const struct rf_map_runtime *runtime, int index);
const struct rf_map_runtime_object *rf_map_runtime_find_object(
    const struct rf_map_runtime *runtime, const char *id);
int rf_map_runtime_render_count(const struct rf_map_runtime *runtime);
const struct rf_map_runtime_render *rf_map_runtime_render_at(
    const struct rf_map_runtime *runtime, int index);
const struct rf_map_runtime_render *rf_map_runtime_find_render(
    const struct rf_map_runtime *runtime, const char *id);
const char *rf_map_runtime_render_attribute(
    const struct rf_map_runtime_render *render, const char *key);
int rf_map_runtime_action_from_name(const char *name);
const char *rf_map_runtime_action_name(int action_id);

#endif
