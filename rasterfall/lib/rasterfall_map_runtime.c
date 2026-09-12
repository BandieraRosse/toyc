#include "tlibc_everything.h"
#include "rasterfall_map_runtime.h"
#include "rasterfall_map_parser.h"

struct rf_map_runtime_impl {
    struct rf_map_runtime_world world;
    struct rf_map_runtime_collision collisions[RASTERFALL_MAP_IR_MAX_COLLISIONS];
    int collision_count;
    struct rf_map_runtime_surface surfaces[RASTERFALL_MAP_IR_MAX_SURFACES];
    int surface_count;
    struct rf_map_runtime_region regions[RASTERFALL_MAP_IR_MAX_REGIONS];
    int region_count;
    struct rf_map_runtime_interaction interactions[
        RASTERFALL_MAP_IR_MAX_INTERACTIONS];
    int interaction_count;
    struct rf_map_runtime_actor_spawn actor_spawns[
        RASTERFALL_MAP_IR_MAX_ACTOR_SPAWNS];
    int actor_spawn_count;
    struct rf_map_runtime_pickup pickups[RASTERFALL_MAP_IR_MAX_PICKUPS];
    int pickup_count;
    struct rf_map_runtime_object objects[RASTERFALL_MAP_IR_MAX_OBJECTS];
    int object_count;
    struct rf_map_runtime_render renders[RASTERFALL_MAP_IR_MAX_RENDERS];
    int render_count;
};

static int text_equal(const char *a, const char *b)
{
    return a && b && !strcmp(a, b);
}

struct action_name {
    const char *name;
    int id;
};

static const struct action_name action_names[] = {
    {"wave_skip", RF_MAP_ACTION_WAVE_SKIP},
    {"enemy_death_test", RF_MAP_ACTION_ENEMY_DEATH_TEST},
    {"humanoid_actions", RF_MAP_ACTION_HUMANOID_ACTIONS},
    {"horde", RF_MAP_ACTION_HORDE}, {"air", RF_MAP_ACTION_AIR},
    {"alarm", RF_MAP_ACTION_ALARM}, {"heavy", RF_MAP_ACTION_HEAVY},
    {"fast", RF_MAP_ACTION_FAST}, {"base1", RF_MAP_ACTION_BASE1},
    {"base2", RF_MAP_ACTION_BASE2}, {"smoker", RF_MAP_ACTION_SMOKER},
    {"charger", RF_MAP_ACTION_CHARGER}, {"tank", RF_MAP_ACTION_TANK},
    {"money", RF_MAP_ACTION_MONEY}, {"clear_hired", RF_MAP_ACTION_CLEAR_HIRED},
    {"attack_x2", RF_MAP_ACTION_ATTACK_X2},
    {"attack_x3", RF_MAP_ACTION_ATTACK_X3},
    {"attack_x4", RF_MAP_ACTION_ATTACK_X4},
    {"pose", RF_MAP_ACTION_POSE},
    {"pose_prev_bone", RF_MAP_ACTION_POSE_PREV_BONE},
    {"pose_next_bone", RF_MAP_ACTION_POSE_NEXT_BONE},
    {"pose_axis_x", RF_MAP_ACTION_POSE_AXIS_X},
    {"pose_axis_y", RF_MAP_ACTION_POSE_AXIS_Y},
    {"pose_axis_z", RF_MAP_ACTION_POSE_AXIS_Z},
    {"pose_decrease", RF_MAP_ACTION_POSE_DECREASE},
    {"pose_increase", RF_MAP_ACTION_POSE_INCREASE},
    {"pose_export", RF_MAP_ACTION_POSE_EXPORT},
    {"pose_toggle_layer", RF_MAP_ACTION_POSE_TOGGLE_LAYER},
    {"pickup_smg", RF_MAP_ACTION_PICKUP_SMG},
    {"pickup_shotgun", RF_MAP_ACTION_PICKUP_SHOTGUN},
    {"pickup_ammo", RF_MAP_ACTION_PICKUP_AMMO},
    {"pickup_shop", RF_MAP_ACTION_PICKUP_SHOP},
    {"pickup_ak", RF_MAP_ACTION_PICKUP_AK},
    {"pickup_awp", RF_MAP_ACTION_PICKUP_AWP},
    {"pickup_axe", RF_MAP_ACTION_PICKUP_AXE},
    {"pickup_bomb", RF_MAP_ACTION_PICKUP_BOMB},
    {"pickup_molotov", RF_MAP_ACTION_PICKUP_MOLOTOV},
    {"pickup_pill", RF_MAP_ACTION_PICKUP_PILL},
    {"anim_idle", RF_MAP_ACTION_ANIM_IDLE},
    {"anim_walk", RF_MAP_ACTION_ANIM_WALK},
    {"anim_jog", RF_MAP_ACTION_ANIM_JOG},
    {"glb_idle", RF_MAP_ACTION_GLB_IDLE},
    {"glb_walk", RF_MAP_ACTION_GLB_WALK},
    {"glb_jog", RF_MAP_ACTION_GLB_JOG},
    {"vmd_walk", RF_MAP_ACTION_VMD_WALK},
    {"vmd_manjusaka", RF_MAP_ACTION_VMD_MANJUSAKA},
    {"animation_composition", RF_MAP_ACTION_ANIMATION_COMPOSITION},
    {"humanoid_pose_debug", RF_MAP_ACTION_HUMANOID_POSE_DEBUG},
    {"west_corridor", RF_MAP_ACTION_WEST_CORRIDOR},
    {"west_corridor_no_tank", RF_MAP_ACTION_WEST_CORRIDOR_NO_TANK},
    {"station_terminal", RF_MAP_ACTION_STATION_TERMINAL},
    {"operations_terminal", RF_MAP_ACTION_OPERATIONS_TERMINAL},
    {"super_terminal", RF_MAP_ACTION_SUPER_TERMINAL},
    {"return_outpost", RF_MAP_ACTION_RETURN_OUTPOST}
};

int rf_map_runtime_action_from_name(const char *name)
{
    int i;
    for (i = 0; i < (int)(sizeof(action_names) / sizeof(action_names[0])); i++)
        if (text_equal(name, action_names[i].name)) return action_names[i].id;
    return RF_MAP_ACTION_UNKNOWN;
}

const char *rf_map_runtime_action_name(int action_id)
{
    int i;
    for (i = 0; i < (int)(sizeof(action_names) / sizeof(action_names[0])); i++)
        if (action_names[i].id == action_id) return action_names[i].name;
    return "unknown";
}

static void copy_string(char *dst, int capacity, const char *src)
{
    strncpy(dst, src, capacity - 1);
    dst[capacity - 1] = '\0';
}

static int extension_int(const struct rasterfall_map_ir_attribute *attributes,
                         int count, const char *key, int *value)
{
    int i, sign, number;
    const char *text;
    for (i = 0; i < count; i++) {
        if (strcmp(attributes[i].key, key)) continue;
        text = attributes[i].value;
        sign = 1;
        number = 0;
        if (*text == '-') { sign = -1; text++; }
        if (!*text) return -1;
        while (*text) {
            if (*text < '0' || *text > '9') return -1;
            number = number * 10 + (*text++ - '0');
        }
        *value = number * sign;
        return 0;
    }
    return 1;
}

static void swap_regions(struct rf_map_runtime_region *a,
                         struct rf_map_runtime_region *b)
{
    struct rf_map_runtime_region tmp = *a;
    *a = *b;
    *b = tmp;
}

static void swap_collisions(struct rf_map_runtime_collision *a,
                            struct rf_map_runtime_collision *b)
{
    struct rf_map_runtime_collision tmp = *a; *a = *b; *b = tmp;
}

static void swap_surfaces(struct rf_map_runtime_surface *a,
                          struct rf_map_runtime_surface *b)
{
    struct rf_map_runtime_surface tmp = *a; *a = *b; *b = tmp;
}

static void swap_interactions(struct rf_map_runtime_interaction *a,
                              struct rf_map_runtime_interaction *b)
{
    struct rf_map_runtime_interaction tmp = *a;
    *a = *b;
    *b = tmp;
}

static void swap_actor_spawns(struct rf_map_runtime_actor_spawn *a,
                              struct rf_map_runtime_actor_spawn *b)
{
    struct rf_map_runtime_actor_spawn tmp = *a; *a = *b; *b = tmp;
}

static void swap_pickups(struct rf_map_runtime_pickup *a,
                         struct rf_map_runtime_pickup *b)
{
    struct rf_map_runtime_pickup tmp = *a; *a = *b; *b = tmp;
}

static void swap_objects(struct rf_map_runtime_object *a,
                         struct rf_map_runtime_object *b)
{
    struct rf_map_runtime_object tmp = *a; *a = *b; *b = tmp;
}

static void swap_renders(struct rf_map_runtime_render *a,
                         struct rf_map_runtime_render *b)
{
    struct rf_map_runtime_render tmp = *a; *a = *b; *b = tmp;
}

static void sort_runtime_records(struct rf_map_runtime_impl *impl)
{
    int i, j;
    for (i = 1; i < impl->region_count; i++) {
        j = i;
        while (j > 0 && strcmp(impl->regions[j - 1].id,
                              impl->regions[j].id) > 0) {
            swap_regions(&impl->regions[j - 1], &impl->regions[j]);
            j--;
        }
    }
    for (i = 1; i < impl->collision_count; i++) {
        j = i;
        while (j > 0 && strcmp(impl->collisions[j - 1].id,
                              impl->collisions[j].id) > 0) {
            swap_collisions(&impl->collisions[j - 1], &impl->collisions[j]);
            j--;
        }
    }
    for (i = 1; i < impl->surface_count; i++) {
        j = i;
        while (j > 0 && strcmp(impl->surfaces[j - 1].id,
                              impl->surfaces[j].id) > 0) {
            swap_surfaces(&impl->surfaces[j - 1], &impl->surfaces[j]);
            j--;
        }
    }
    for (i = 1; i < impl->interaction_count; i++) {
        j = i;
        while (j > 0 && strcmp(impl->interactions[j - 1].id,
                              impl->interactions[j].id) > 0) {
            swap_interactions(&impl->interactions[j - 1],
                              &impl->interactions[j]);
            j--;
        }
    }
    for (i = 1; i < impl->actor_spawn_count; i++) {
        j = i;
        while (j > 0 && strcmp(impl->actor_spawns[j - 1].id,
                              impl->actor_spawns[j].id) > 0) {
            swap_actor_spawns(&impl->actor_spawns[j - 1], &impl->actor_spawns[j]);
            j--;
        }
    }
    for (i = 1; i < impl->pickup_count; i++) {
        j = i;
        while (j > 0 && strcmp(impl->pickups[j - 1].id,
                              impl->pickups[j].id) > 0) {
            swap_pickups(&impl->pickups[j - 1], &impl->pickups[j]);
            j--;
        }
    }
    for (i = 1; i < impl->object_count; i++) {
        j = i;
        while (j > 0 && strcmp(impl->objects[j - 1].id,
                              impl->objects[j].id) > 0) {
            swap_objects(&impl->objects[j - 1], &impl->objects[j]);
            j--;
        }
    }
    for (i = 1; i < impl->render_count; i++) {
        j = i;
        while (j > 0 && strcmp(impl->renders[j - 1].id,
                              impl->renders[j].id) > 0) {
            swap_renders(&impl->renders[j - 1], &impl->renders[j]);
            j--;
        }
    }
}

int rf_map_runtime_load(struct rf_map_runtime *runtime, const char *path)
{
    struct rasterfall_map_ir parsed;
    struct rf_map_runtime_impl *impl;
    int i;
    if (!runtime || !path) return -1;
    rf_map_runtime_unload(runtime);
    if (rasterfall_map_ir_parse_file(path, &parsed) < 0) {
        runtime->error_line = parsed.error_line;
        copy_string(runtime->error, sizeof(runtime->error), parsed.error);
        return -1;
    }
    impl = (struct rf_map_runtime_impl *)tlibc_malloc(sizeof(*impl));
    if (!impl) {
        runtime->error_line = 0;
        copy_string(runtime->error, sizeof(runtime->error),
                    "out of memory allocating runtime map");
        return -1;
    }
    __memset(impl, 0, sizeof(*impl));
    impl->world.bounds.min_x = parsed.world.bounds.min_x;
    impl->world.bounds.max_x = parsed.world.bounds.max_x;
    impl->world.bounds.min_z = parsed.world.bounds.min_z;
    impl->world.bounds.max_z = parsed.world.bounds.max_z;
    impl->world.room_limit = parsed.world.room_limit;
    impl->world.has_room_limit = parsed.world.has_room_limit;
    impl->collision_count = parsed.collision_count;
    impl->surface_count = parsed.surface_count;
    impl->render_count = parsed.render_count;
    for (i = 0; i < impl->collision_count; i++) {
        copy_string(impl->collisions[i].id, RF_MAP_RUNTIME_ID_CAP,
                    parsed.collisions[i].id);
        copy_string(impl->collisions[i].shape, RF_MAP_RUNTIME_KIND_CAP,
                    parsed.collisions[i].shape);
        impl->collisions[i].bounds.min_x = parsed.collisions[i].bounds.min_x;
        impl->collisions[i].bounds.max_x = parsed.collisions[i].bounds.max_x;
        impl->collisions[i].bounds.min_z = parsed.collisions[i].bounds.min_z;
        impl->collisions[i].bounds.max_z = parsed.collisions[i].bounds.max_z;
        impl->collisions[i].height = parsed.collisions[i].height;
        impl->collisions[i].height2 = parsed.collisions[i].height2;
        impl->collisions[i].has_height2 = parsed.collisions[i].has_height2;
        impl->collisions[i].collision = parsed.collisions[i].collision;
        impl->collisions[i].visible = parsed.collisions[i].visible;
        impl->collisions[i].walkable = parsed.collisions[i].walkable;
        impl->collisions[i].blocks_airborne = parsed.collisions[i].blocks_airborne;
        impl->collisions[i].has_color = parsed.collisions[i].has_color;
        if (impl->collisions[i].has_color)
            copy_string(impl->collisions[i].color, RF_MAP_RUNTIME_KIND_CAP,
                        parsed.collisions[i].color);
        impl->collisions[i].has_role = parsed.collisions[i].has_role;
        if (impl->collisions[i].has_role)
            copy_string(impl->collisions[i].role, RF_MAP_RUNTIME_KIND_CAP,
                        parsed.collisions[i].role);
        if (extension_int(parsed.collisions[i].attributes,
                          parsed.collisions[i].attribute_count, "legacy_index",
                          &impl->collisions[i].legacy_index) == 0)
            impl->collisions[i].has_legacy_index = 1;
        impl->collisions[i].line = parsed.collisions[i].line;
    }
    for (i = 0; i < impl->surface_count; i++) {
        copy_string(impl->surfaces[i].id, RF_MAP_RUNTIME_ID_CAP,
                    parsed.surfaces[i].id);
        copy_string(impl->surfaces[i].kind, RF_MAP_RUNTIME_KIND_CAP,
                    parsed.surfaces[i].kind);
        impl->surfaces[i].bounds.min_x = parsed.surfaces[i].bounds.min_x;
        impl->surfaces[i].bounds.max_x = parsed.surfaces[i].bounds.max_x;
        impl->surfaces[i].bounds.min_z = parsed.surfaces[i].bounds.min_z;
        impl->surfaces[i].bounds.max_z = parsed.surfaces[i].bounds.max_z;
        impl->surfaces[i].height = parsed.surfaces[i].height;
        impl->surfaces[i].height2 = parsed.surfaces[i].height2;
        impl->surfaces[i].has_height2 = parsed.surfaces[i].has_height2;
        impl->surfaces[i].has_axis = parsed.surfaces[i].has_axis;
        if (impl->surfaces[i].has_axis)
            copy_string(impl->surfaces[i].axis, RF_MAP_RUNTIME_KIND_CAP,
                        parsed.surfaces[i].axis);
        impl->surfaces[i].has_material = parsed.surfaces[i].has_material;
        if (impl->surfaces[i].has_material)
            copy_string(impl->surfaces[i].material, RF_MAP_RUNTIME_KIND_CAP,
                        parsed.surfaces[i].material);
        if (extension_int(parsed.surfaces[i].attributes,
                          parsed.surfaces[i].attribute_count, "legacy_index",
                          &impl->surfaces[i].legacy_index) == 0)
            impl->surfaces[i].has_legacy_index = 1;
        impl->surfaces[i].line = parsed.surfaces[i].line;
    }
    impl->region_count = parsed.region_count;
    for (i = 0; i < impl->region_count; i++) {
        copy_string(impl->regions[i].id, RF_MAP_RUNTIME_ID_CAP,
                    parsed.regions[i].id);
        copy_string(impl->regions[i].kind, RF_MAP_RUNTIME_KIND_CAP,
                    parsed.regions[i].kind);
        impl->regions[i].bounds.min_x = parsed.regions[i].bounds.min_x;
        impl->regions[i].bounds.max_x = parsed.regions[i].bounds.max_x;
        impl->regions[i].bounds.min_z = parsed.regions[i].bounds.min_z;
        impl->regions[i].bounds.max_z = parsed.regions[i].bounds.max_z;
        if (extension_int(parsed.regions[i].attributes,
                          parsed.regions[i].attribute_count, "legacy_index",
                          &impl->regions[i].legacy_index) == 0)
            impl->regions[i].has_legacy_index = 1;
        impl->regions[i].line = parsed.regions[i].line;
    }
    impl->interaction_count = parsed.interaction_count;
    for (i = 0; i < impl->interaction_count; i++) {
        copy_string(impl->interactions[i].id, RF_MAP_RUNTIME_ID_CAP,
                    parsed.interactions[i].id);
        copy_string(impl->interactions[i].action, RF_MAP_RUNTIME_ACTION_CAP,
                    parsed.interactions[i].action);
        impl->interactions[i].action_id = rf_map_runtime_action_from_name(
            impl->interactions[i].action);
        if (impl->interactions[i].action_id == RF_MAP_ACTION_UNKNOWN) {
            runtime->error_line = parsed.interactions[i].line;
            snprintf(runtime->error, sizeof(runtime->error),
                       "unknown interaction action %s",
                       impl->interactions[i].action);
            tlibc_free(impl);
            return -1;
        }
        if (extension_int(parsed.interactions[i].attributes,
                          parsed.interactions[i].attribute_count, "legacy_index",
                          &impl->interactions[i].legacy_index) == 0)
            impl->interactions[i].has_legacy_index = 1;
        impl->interactions[i].x = parsed.interactions[i].x;
        impl->interactions[i].y = parsed.interactions[i].y;
        impl->interactions[i].z = parsed.interactions[i].z;
        impl->interactions[i].line = parsed.interactions[i].line;
    }
    impl->actor_spawn_count = parsed.actor_spawn_count;
    for (i = 0; i < impl->actor_spawn_count; i++) {
        copy_string(impl->actor_spawns[i].id, RF_MAP_RUNTIME_ID_CAP, parsed.actor_spawns[i].id);
        copy_string(impl->actor_spawns[i].class_name, RF_MAP_RUNTIME_KIND_CAP, parsed.actor_spawns[i].class_name);
        impl->actor_spawns[i].base_id = parsed.actor_spawns[i].base_id;
        impl->actor_spawns[i].x = parsed.actor_spawns[i].x;
        impl->actor_spawns[i].y = parsed.actor_spawns[i].y;
        impl->actor_spawns[i].z = parsed.actor_spawns[i].z;
        impl->actor_spawns[i].downed = parsed.actor_spawns[i].downed;
        impl->actor_spawns[i].has_weapon = parsed.actor_spawns[i].has_weapon;
        if (parsed.actor_spawns[i].has_weapon)
            copy_string(impl->actor_spawns[i].weapon, RF_MAP_RUNTIME_KIND_CAP, parsed.actor_spawns[i].weapon);
        if (extension_int(parsed.actor_spawns[i].attributes, parsed.actor_spawns[i].attribute_count, "legacy_index", &impl->actor_spawns[i].legacy_index) == 0)
            impl->actor_spawns[i].has_legacy_index = 1;
        impl->actor_spawns[i].line = parsed.actor_spawns[i].line;
    }
    impl->pickup_count = parsed.pickup_count;
    for (i = 0; i < impl->pickup_count; i++) {
        copy_string(impl->pickups[i].id, RF_MAP_RUNTIME_ID_CAP, parsed.pickups[i].id);
        copy_string(impl->pickups[i].kind, RF_MAP_RUNTIME_KIND_CAP, parsed.pickups[i].kind);
        impl->pickups[i].x = parsed.pickups[i].x;
        impl->pickups[i].y = parsed.pickups[i].y;
        impl->pickups[i].z = parsed.pickups[i].z;
        if (extension_int(parsed.pickups[i].attributes, parsed.pickups[i].attribute_count, "legacy_index", &impl->pickups[i].legacy_index) == 0)
            impl->pickups[i].has_legacy_index = 1;
        impl->pickups[i].line = parsed.pickups[i].line;
    }
    impl->object_count = parsed.object_count;
    for (i = 0; i < impl->object_count; i++) {
        copy_string(impl->objects[i].id, RF_MAP_RUNTIME_ID_CAP, parsed.objects[i].id);
        copy_string(impl->objects[i].kind, RF_MAP_RUNTIME_KIND_CAP, parsed.objects[i].kind);
        impl->objects[i].x = parsed.objects[i].x;
        impl->objects[i].y = parsed.objects[i].y;
        impl->objects[i].z = parsed.objects[i].z;
        impl->objects[i].yaw = parsed.objects[i].yaw;
        impl->objects[i].scale = parsed.objects[i].scale;
        if (extension_int(parsed.objects[i].attributes, parsed.objects[i].attribute_count, "legacy_index", &impl->objects[i].legacy_index) == 0)
            impl->objects[i].has_legacy_index = 1;
        impl->objects[i].line = parsed.objects[i].line;
    }
    impl->render_count = parsed.render_count;
    for (i = 0; i < impl->render_count; i++) {
        int j;
        copy_string(impl->renders[i].id, RF_MAP_RUNTIME_ID_CAP,
                    parsed.renders[i].id);
        copy_string(impl->renders[i].kind, RF_MAP_RUNTIME_KIND_CAP,
                    parsed.renders[i].kind);
        impl->renders[i].bounds.min_x = parsed.renders[i].bounds.min_x;
        impl->renders[i].bounds.max_x = parsed.renders[i].bounds.max_x;
        impl->renders[i].bounds.min_z = parsed.renders[i].bounds.min_z;
        impl->renders[i].bounds.max_z = parsed.renders[i].bounds.max_z;
        impl->renders[i].has_position = parsed.renders[i].has_position;
        impl->renders[i].x = parsed.renders[i].x;
        impl->renders[i].y = parsed.renders[i].y;
        impl->renders[i].z = parsed.renders[i].z;
        impl->renders[i].has_asset = parsed.renders[i].has_asset;
        if (impl->renders[i].has_asset)
            copy_string(impl->renders[i].asset, RF_MAP_RUNTIME_VALUE_CAP,
                        parsed.renders[i].asset);
        impl->renders[i].height = parsed.renders[i].height;
        impl->renders[i].has_height = parsed.renders[i].has_height;
        impl->renders[i].has_color = parsed.renders[i].has_color;
        if (impl->renders[i].has_color)
            copy_string(impl->renders[i].color, RF_MAP_RUNTIME_VALUE_CAP,
                        parsed.renders[i].color);
        impl->renders[i].attribute_count = parsed.renders[i].attribute_count;
        for (j = 0; j < impl->renders[i].attribute_count; j++) {
            copy_string(impl->renders[i].attributes[j].key,
                        RF_MAP_RUNTIME_KIND_CAP,
                        parsed.renders[i].attributes[j].key);
            copy_string(impl->renders[i].attributes[j].value,
                        RF_MAP_RUNTIME_VALUE_CAP,
                        parsed.renders[i].attributes[j].value);
        }
        if (extension_int(parsed.renders[i].attributes,
                          parsed.renders[i].attribute_count, "legacy_index",
                          &impl->renders[i].legacy_index) == 0)
            impl->renders[i].has_legacy_index = 1;
        impl->renders[i].line = parsed.renders[i].line;
    }
    sort_runtime_records(impl);
    runtime->impl = impl;
    runtime->error_line = 0;
    runtime->error[0] = '\0';
    return 0;
}

void rf_map_runtime_unload(struct rf_map_runtime *runtime)
{
    if (!runtime) return;
    if (runtime->impl) tlibc_free(runtime->impl);
    runtime->impl = NULL;
    runtime->error_line = 0;
    runtime->error[0] = '\0';
}

static const struct rf_map_runtime_impl *runtime_impl(
    const struct rf_map_runtime *runtime)
{
    return runtime ? (const struct rf_map_runtime_impl *)runtime->impl : NULL;
}

const struct rf_map_runtime_world *rf_map_runtime_world_info(
    const struct rf_map_runtime *runtime)
{
    const struct rf_map_runtime_impl *impl = runtime_impl(runtime);
    return impl ? &impl->world : NULL;
}

int rf_map_runtime_region_count(const struct rf_map_runtime *runtime)
{
    const struct rf_map_runtime_impl *impl = runtime_impl(runtime);
    return impl ? impl->region_count : 0;
}

int rf_map_runtime_collision_count(const struct rf_map_runtime *runtime)
{
    const struct rf_map_runtime_impl *impl = runtime_impl(runtime);
    return impl ? impl->collision_count : 0;
}

int rf_map_runtime_surface_count(const struct rf_map_runtime *runtime)
{
    const struct rf_map_runtime_impl *impl = runtime_impl(runtime);
    return impl ? impl->surface_count : 0;
}

const struct rf_map_runtime_surface *rf_map_runtime_surface_at(
    const struct rf_map_runtime *runtime, int index)
{
    const struct rf_map_runtime_impl *impl = runtime_impl(runtime);
    if (!impl || index < 0 || index >= impl->surface_count) return NULL;
    return &impl->surfaces[index];
}

const struct rf_map_runtime_surface *rf_map_runtime_find_surface(
    const struct rf_map_runtime *runtime, const char *id)
{
    const struct rf_map_runtime_impl *impl = runtime_impl(runtime);
    int i;
    if (!impl || !id) return NULL;
    for (i = 0; i < impl->surface_count; i++)
        if (text_equal(impl->surfaces[i].id, id)) return &impl->surfaces[i];
    return NULL;
}

const struct rf_map_runtime_collision *rf_map_runtime_collision_at(
    const struct rf_map_runtime *runtime, int index)
{
    const struct rf_map_runtime_impl *impl = runtime_impl(runtime);
    if (!impl || index < 0 || index >= impl->collision_count) return NULL;
    return &impl->collisions[index];
}

const struct rf_map_runtime_collision *rf_map_runtime_find_collision(
    const struct rf_map_runtime *runtime, const char *id)
{
    const struct rf_map_runtime_impl *impl = runtime_impl(runtime);
    int i;
    if (!impl || !id) return NULL;
    for (i = 0; i < impl->collision_count; i++)
        if (text_equal(impl->collisions[i].id, id)) return &impl->collisions[i];
    return NULL;
}

const struct rf_map_runtime_region *rf_map_runtime_region_at(
    const struct rf_map_runtime *runtime, int index)
{
    const struct rf_map_runtime_impl *impl = runtime_impl(runtime);
    if (!impl || index < 0 || index >= impl->region_count) return NULL;
    return &impl->regions[index];
}

const struct rf_map_runtime_region *rf_map_runtime_find_region(
    const struct rf_map_runtime *runtime, const char *id)
{
    const struct rf_map_runtime_impl *impl = runtime_impl(runtime);
    int i;
    if (!impl || !id) return NULL;
    for (i = 0; i < impl->region_count; i++)
        if (text_equal(impl->regions[i].id, id)) return &impl->regions[i];
    return NULL;
}

int rf_map_runtime_find_regions_by_kind(
    const struct rf_map_runtime *runtime, const char *kind,
    const struct rf_map_runtime_region **out, int capacity)
{
    const struct rf_map_runtime_impl *impl = runtime_impl(runtime);
    int i, count = 0;
    if (!impl || !kind) return -1;
    for (i = 0; i < impl->region_count; i++) {
        if (!text_equal(impl->regions[i].kind, kind)) continue;
        if (out && count < capacity) out[count] = &impl->regions[i];
        count++;
    }
    return count;
}

int rf_map_runtime_interaction_count(const struct rf_map_runtime *runtime)
{
    const struct rf_map_runtime_impl *impl = runtime_impl(runtime);
    return impl ? impl->interaction_count : 0;
}

const struct rf_map_runtime_interaction *rf_map_runtime_interaction_at(
    const struct rf_map_runtime *runtime, int index)
{
    const struct rf_map_runtime_impl *impl = runtime_impl(runtime);
    if (!impl || index < 0 || index >= impl->interaction_count) return NULL;
    return &impl->interactions[index];
}

const struct rf_map_runtime_interaction *rf_map_runtime_find_interaction(
    const struct rf_map_runtime *runtime, const char *id)
{
    const struct rf_map_runtime_impl *impl = runtime_impl(runtime);
    int i;
    if (!impl || !id) return NULL;
    for (i = 0; i < impl->interaction_count; i++)
        if (text_equal(impl->interactions[i].id, id))
            return &impl->interactions[i];
    return NULL;
}

int rf_map_runtime_actor_spawn_count(const struct rf_map_runtime *runtime)
{
    struct rf_map_runtime_impl *impl = runtime ? runtime->impl : NULL;
    return impl ? impl->actor_spawn_count : 0;
}

const struct rf_map_runtime_actor_spawn *rf_map_runtime_actor_spawn_at(
    const struct rf_map_runtime *runtime, int index)
{
    struct rf_map_runtime_impl *impl = runtime ? runtime->impl : NULL;
    return impl && index >= 0 && index < impl->actor_spawn_count ? &impl->actor_spawns[index] : NULL;
}

const struct rf_map_runtime_actor_spawn *rf_map_runtime_find_spawn(
    const struct rf_map_runtime *runtime, const char *id)
{
    int i;
    for (i = 0; i < rf_map_runtime_actor_spawn_count(runtime); i++) {
        const struct rf_map_runtime_actor_spawn *item = rf_map_runtime_actor_spawn_at(runtime, i);
        if (!strcmp(item->id, id)) return item;
    }
    return NULL;
}

int rf_map_runtime_pickup_count(const struct rf_map_runtime *runtime)
{
    struct rf_map_runtime_impl *impl = runtime ? runtime->impl : NULL;
    return impl ? impl->pickup_count : 0;
}

const struct rf_map_runtime_pickup *rf_map_runtime_pickup_at(
    const struct rf_map_runtime *runtime, int index)
{
    struct rf_map_runtime_impl *impl = runtime ? runtime->impl : NULL;
    return impl && index >= 0 && index < impl->pickup_count ? &impl->pickups[index] : NULL;
}

const struct rf_map_runtime_pickup *rf_map_runtime_find_pickup(
    const struct rf_map_runtime *runtime, const char *id)
{
    int i;
    for (i = 0; i < rf_map_runtime_pickup_count(runtime); i++) {
        const struct rf_map_runtime_pickup *item = rf_map_runtime_pickup_at(runtime, i);
        if (!strcmp(item->id, id)) return item;
    }
    return NULL;
}

int rf_map_runtime_object_count(const struct rf_map_runtime *runtime)
{
    struct rf_map_runtime_impl *impl = runtime ? runtime->impl : NULL;
    return impl ? impl->object_count : 0;
}

const struct rf_map_runtime_object *rf_map_runtime_object_at(
    const struct rf_map_runtime *runtime, int index)
{
    struct rf_map_runtime_impl *impl = runtime ? runtime->impl : NULL;
    return impl && index >= 0 && index < impl->object_count ? &impl->objects[index] : NULL;
}

const struct rf_map_runtime_object *rf_map_runtime_find_object(
    const struct rf_map_runtime *runtime, const char *id)
{
    int i;
    for (i = 0; i < rf_map_runtime_object_count(runtime); i++) {
        const struct rf_map_runtime_object *item = rf_map_runtime_object_at(runtime, i);
        if (!strcmp(item->id, id)) return item;
    }
    return NULL;
}

int rf_map_runtime_render_count(const struct rf_map_runtime *runtime)
{
    const struct rf_map_runtime_impl *impl = runtime_impl(runtime);
    return impl ? impl->render_count : 0;
}

const struct rf_map_runtime_render *rf_map_runtime_render_at(
    const struct rf_map_runtime *runtime, int index)
{
    const struct rf_map_runtime_impl *impl = runtime_impl(runtime);
    if (!impl || index < 0 || index >= impl->render_count) return NULL;
    return &impl->renders[index];
}

const struct rf_map_runtime_render *rf_map_runtime_find_render(
    const struct rf_map_runtime *runtime, const char *id)
{
    const struct rf_map_runtime_impl *impl = runtime_impl(runtime);
    int i;
    if (!impl || !id) return NULL;
    for (i = 0; i < impl->render_count; i++)
        if (text_equal(impl->renders[i].id, id)) return &impl->renders[i];
    return NULL;
}

const char *rf_map_runtime_render_attribute(
    const struct rf_map_runtime_render *render, const char *key)
{
    int i;
    if (!render || !key) return NULL;
    for (i = 0; i < render->attribute_count; i++)
        if (text_equal(render->attributes[i].key, key))
            return render->attributes[i].value;
    return NULL;
}
