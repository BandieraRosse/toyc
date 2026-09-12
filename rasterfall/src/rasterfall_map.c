#include "tlibc_everything.h"
#include "rasterfall_map.h"
#include "rasterfall_prop.h"

void rasterfall_map_bind(struct rasterfall_map_state *map,
                         struct toy_map *level,
                         struct toy_game_box *safe_rooms,
                         struct toy_game_box *spawn_zones,
                         int *spawn_count,
                         int *air_walls_enabled,
                         struct rasterfall_interactable *interactables,
                         int *interactable_count)
{
    map->level = level;
    map->safe_rooms = safe_rooms;
    map->spawn_zones = spawn_zones;
    map->spawn_count = spawn_count;
    map->air_walls_enabled = air_walls_enabled;
    map->interactables = interactables;
    map->interactable_count = interactable_count;
}

int rasterfall_map_load(struct rasterfall_map_state *map, const char *path)
{
    if (!map) return -1;
    return toy_map_load(path, map->level);
}

void rasterfall_map_unload(struct rasterfall_map_state *map)
{
    if (!map) return;
    rf_map_runtime_unload(&map->runtime);
    map->runtime_loaded = 0;
    toy_map_unload(map->level);
}

void rasterfall_map_prepare(struct rasterfall_map_state *map)
{
    int i, gate_count = 0;
    if (!map) return;
    for (i = 0; i < TOY_MAP_MAX_BASES; i++) map->air_wall_indices[i] = -1;
    map->air_wall_count = 0;
    for (i = 0; i < map->level->primitive_count; i++) {
        struct toy_map_primitive *p = &map->level->primitives[i];
        if ((!strcmp(p->role, "air_gate") ||
             !strncmp(p->role, "air_gate_", 9)) &&
            (p->flags & TOY_MAP_PRIMITIVE_COLLISION) &&
            gate_count < TOY_MAP_MAX_BASES) {
            map->air_wall_indices[gate_count++] = i;
        }
    }
    map->air_wall_count = gate_count;
    for (i = 0; i < map->level->safe_count; i++)
        map->safe_rooms[i] = map->level->safe_rooms[i];
    *map->spawn_count = map->level->spawn_count;
    for (i = 0; i < *map->spawn_count; i++)
        map->spawn_zones[i] = map->level->spawn_zones[i].box;
    *map->air_walls_enabled = 1;
}

void rasterfall_map_set_air_walls(struct rasterfall_map_state *map, int enabled)
{
    int i, index;
    if (!map) return;
    *map->air_walls_enabled = enabled != 0;
    for (i = 0; i < map->air_wall_count; i++) {
        index = map->air_wall_indices[i];
        if (index < 0) continue;
        if (*map->air_walls_enabled)
            map->level->primitives[index].flags |= TOY_MAP_PRIMITIVE_COLLISION;
        else
            map->level->primitives[index].flags &= ~TOY_MAP_PRIMITIVE_COLLISION;
    }
}

void rasterfall_map_reset_interactables(struct rasterfall_map_state *map)
{
    int i;
    if (!map) return;
    *map->interactable_count = map->level->pickup_count;
    for (i = 0; i < *map->interactable_count; i++) {
        map->interactables[i].kind = map->level->pickups[i].kind;
        map->interactables[i].weapon = map->level->pickups[i].weapon;
        map->interactables[i].x = map->level->pickups[i].x;
        map->interactables[i].z = map->level->pickups[i].z;
        map->interactables[i].y = map->level->pickups[i].y;
    }
}

int rasterfall_map_load_runtime_overlay(struct rasterfall_map_state *map,
                                        const char *path)
{
    if (!map || !path) return -1;
    if (rf_map_runtime_load(&map->runtime, path) < 0) {
        map->runtime_loaded = 0;
        return -1;
    }
    map->runtime_loaded = 1;
    return 0;
}

static int runtime_action_to_pickup(int action_id, int *weapon)
{
    if (weapon) *weapon = -1;
    switch (action_id) {
    case RF_MAP_ACTION_HORDE: return TOY_MAP_PICKUP_BUTTON;
    case RF_MAP_ACTION_WAVE_SKIP: return TOY_MAP_PICKUP_WAVE_SKIP_BUTTON;
    case RF_MAP_ACTION_ENEMY_DEATH_TEST:
        return TOY_MAP_PICKUP_ENEMY_DEATH_TEST_BUTTON;
    case RF_MAP_ACTION_HUMANOID_ACTIONS:
        return TOY_MAP_PICKUP_HUMANOID_ACTIONS_BUTTON;
    case RF_MAP_ACTION_AIR: return TOY_MAP_PICKUP_AIR_BUTTON;
    case RF_MAP_ACTION_ALARM: return TOY_MAP_PICKUP_ALARM_BUTTON;
    case RF_MAP_ACTION_HEAVY: return TOY_MAP_PICKUP_HEAVY_HORDE_BUTTON;
    case RF_MAP_ACTION_FAST: return TOY_MAP_PICKUP_FAST_HORDE_BUTTON;
    case RF_MAP_ACTION_BASE1: return TOY_MAP_PICKUP_BASE_1_BUTTON;
    case RF_MAP_ACTION_BASE2: return TOY_MAP_PICKUP_BASE_2_BUTTON;
    case RF_MAP_ACTION_SMOKER: return TOY_MAP_PICKUP_SMOKER_BUTTON;
    case RF_MAP_ACTION_CHARGER: return TOY_MAP_PICKUP_CHARGER_BUTTON;
    case RF_MAP_ACTION_TANK: return TOY_MAP_PICKUP_TANK_BUTTON;
    case RF_MAP_ACTION_MONEY: return TOY_MAP_PICKUP_MONEY_BUTTON;
    case RF_MAP_ACTION_CLEAR_HIRED: return TOY_MAP_PICKUP_CLEAR_HIRED_BUTTON;
    case RF_MAP_ACTION_ATTACK_X2: return TOY_MAP_PICKUP_ATTACK_X2_BUTTON;
    case RF_MAP_ACTION_ATTACK_X3: return TOY_MAP_PICKUP_ATTACK_X3_BUTTON;
    case RF_MAP_ACTION_ATTACK_X4: return TOY_MAP_PICKUP_ATTACK_X4_BUTTON;
    case RF_MAP_ACTION_POSE: return TOY_MAP_PICKUP_POSE_RESET_BUTTON;
    case RF_MAP_ACTION_POSE_PREV_BONE:
        return TOY_MAP_PICKUP_POSE_RIGHT_ARM_BUTTON;
    case RF_MAP_ACTION_POSE_NEXT_BONE:
        return TOY_MAP_PICKUP_POSE_ARMS_BUTTON;
    case RF_MAP_ACTION_POSE_AXIS_X:
        return TOY_MAP_PICKUP_POSE_BODY_BUTTON;
    case RF_MAP_ACTION_ANIM_IDLE: return TOY_MAP_PICKUP_ANIM_IDLE_BUTTON;
    case RF_MAP_ACTION_ANIM_WALK: return TOY_MAP_PICKUP_ANIM_WALK_BUTTON;
    case RF_MAP_ACTION_ANIM_JOG: return TOY_MAP_PICKUP_ANIM_JOG_BUTTON;
    case RF_MAP_ACTION_GLB_IDLE: return TOY_MAP_PICKUP_GLB_IDLE_BUTTON;
    case RF_MAP_ACTION_GLB_WALK: return TOY_MAP_PICKUP_GLB_WALK_BUTTON;
    case RF_MAP_ACTION_GLB_JOG: return TOY_MAP_PICKUP_GLB_JOG_BUTTON;
    case RF_MAP_ACTION_VMD_WALK: return TOY_MAP_PICKUP_VMD_WALK_BUTTON;
    case RF_MAP_ACTION_VMD_MANJUSAKA:
        return TOY_MAP_PICKUP_VMD_MANJUSAKA_BUTTON;
    case RF_MAP_ACTION_ANIMATION_COMPOSITION:
        return TOY_MAP_PICKUP_ANIMATION_COMPOSITION_BUTTON;
    case RF_MAP_ACTION_HUMANOID_POSE_DEBUG:
        return TOY_MAP_PICKUP_HUMANOID_POSE_DEBUG_BUTTON;
    case RF_MAP_ACTION_WEST_CORRIDOR:
        return TOY_MAP_PICKUP_WEST_CORRIDOR_BUTTON;
    case RF_MAP_ACTION_WEST_CORRIDOR_NO_TANK:
        return TOY_MAP_PICKUP_WEST_CORRIDOR_NO_TANK_BUTTON;
    case RF_MAP_ACTION_STATION_TERMINAL:
        return TOY_MAP_PICKUP_STATION_TERMINAL;
    case RF_MAP_ACTION_OPERATIONS_TERMINAL:
        return TOY_MAP_PICKUP_OPERATIONS_TERMINAL;
    case RF_MAP_ACTION_SUPER_TERMINAL:
        return TOY_MAP_PICKUP_SUPER_TERMINAL;
    case RF_MAP_ACTION_RETURN_OUTPOST:
        return TOY_MAP_PICKUP_RETURN_OUTPOST;
    case RF_MAP_ACTION_PICKUP_SMG:
        if (weapon) *weapon = TOY_GAME_WEAPON_SMG;
        return TOY_MAP_PICKUP_WEAPON;
    case RF_MAP_ACTION_PICKUP_SHOTGUN:
        if (weapon) *weapon = TOY_GAME_WEAPON_SHOTGUN;
        return TOY_MAP_PICKUP_WEAPON;
    case RF_MAP_ACTION_PICKUP_AMMO: return TOY_MAP_PICKUP_AMMO;
    case RF_MAP_ACTION_PICKUP_SHOP: return TOY_MAP_PICKUP_SHOP;
    case RF_MAP_ACTION_PICKUP_AK:
        if (weapon) *weapon = TOY_GAME_WEAPON_AK;
        return TOY_MAP_PICKUP_WEAPON;
    case RF_MAP_ACTION_PICKUP_AWP:
        if (weapon) *weapon = TOY_GAME_WEAPON_AWP;
        return TOY_MAP_PICKUP_WEAPON;
    case RF_MAP_ACTION_PICKUP_AXE:
        if (weapon) *weapon = TOY_GAME_WEAPON_AXE;
        return TOY_MAP_PICKUP_WEAPON;
    case RF_MAP_ACTION_PICKUP_BOMB:
        if (weapon) *weapon = TOY_GAME_WEAPON_BOMB;
        return TOY_MAP_PICKUP_THROWABLE;
    case RF_MAP_ACTION_PICKUP_MOLOTOV:
        if (weapon) *weapon = TOY_GAME_WEAPON_MOLOTOV;
        return TOY_MAP_PICKUP_THROWABLE;
    case RF_MAP_ACTION_PICKUP_PILL:
        if (weapon) *weapon = TOY_GAME_WEAPON_PILL;
        return TOY_MAP_PICKUP_PILL;
    default: return -1;
    }
}

static const struct rf_map_runtime_interaction *runtime_interaction_projection_at(
    const struct rf_map_runtime *runtime, int index)
{
    int i;
    for (i = 0; i < rf_map_runtime_interaction_count(runtime); i++) {
        const struct rf_map_runtime_interaction *item =
            rf_map_runtime_interaction_at(runtime, i);
        if (!item->has_legacy_index) continue;
        if (item->legacy_index == index) return item;
    }
    return NULL;
}

static const struct rf_map_runtime_region *runtime_region_projection_at(
    const struct rf_map_runtime *runtime, int index)
{
    const struct rf_map_runtime_region *fallback = NULL;
    int i;
    for (i = 0; i < rf_map_runtime_region_count(runtime); i++) {
        const struct rf_map_runtime_region *item =
            rf_map_runtime_region_at(runtime, i);
        if (!item->has_legacy_index) {
            if (!fallback) fallback = item;
            continue;
        }
        if (item->legacy_index == index) return item;
    }
    return fallback;
}

static const struct rf_map_runtime_actor_spawn *runtime_spawn_projection_at(
    const struct rf_map_runtime *runtime, int index)
{
    int i;
    for (i = 0; i < rf_map_runtime_actor_spawn_count(runtime); i++) {
        const struct rf_map_runtime_actor_spawn *item =
            rf_map_runtime_actor_spawn_at(runtime, i);
        if (item->has_legacy_index && item->legacy_index == index) return item;
    }
    return NULL;
}

static const struct rf_map_runtime_pickup *runtime_pickup_projection_at(
    const struct rf_map_runtime *runtime, int index)
{
    int i;
    for (i = 0; i < rf_map_runtime_pickup_count(runtime); i++) {
        const struct rf_map_runtime_pickup *item =
            rf_map_runtime_pickup_at(runtime, i);
        if (item->has_legacy_index && item->legacy_index == index) return item;
    }
    return NULL;
}

static const struct rf_map_runtime_object *runtime_object_projection_at(
    const struct rf_map_runtime *runtime, int index)
{
    int i;
    for (i = 0; i < rf_map_runtime_object_count(runtime); i++) {
        const struct rf_map_runtime_object *item =
            rf_map_runtime_object_at(runtime, i);
        if (item->has_legacy_index && item->legacy_index == index) return item;
    }
    return NULL;
}

static const struct rf_map_runtime_collision *runtime_collision_projection_at(
    const struct rf_map_runtime *runtime, int index)
{
    int i;
    for (i = 0; i < rf_map_runtime_collision_count(runtime); i++) {
        const struct rf_map_runtime_collision *item =
            rf_map_runtime_collision_at(runtime, i);
        if (item->has_legacy_index && item->legacy_index == index) return item;
    }
    return NULL;
}

static const struct rf_map_runtime_surface *runtime_surface_projection_at(
    const struct rf_map_runtime *runtime, int index)
{
    int i;
    for (i = 0; i < rf_map_runtime_surface_count(runtime); i++) {
        const struct rf_map_runtime_surface *item =
            rf_map_runtime_surface_at(runtime, i);
        if (item->has_legacy_index && item->legacy_index == index) return item;
    }
    return NULL;
}

static const struct rf_map_runtime_render *runtime_render_projection_at(
    const struct rf_map_runtime *runtime, int index)
{
    int i;
    for (i = 0; i < rf_map_runtime_render_count(runtime); i++) {
        const struct rf_map_runtime_render *item =
            rf_map_runtime_render_at(runtime, i);
        if (item->has_legacy_index && item->legacy_index == index) return item;
    }
    return NULL;
}

static int runtime_render_int(const struct rf_map_runtime_render *render,
                              const char *key, int fallback)
{
    const char *value = rf_map_runtime_render_attribute(render, key);
    char *end;
    long result;
    if (!value || !*value) return fallback;
    result = strtol(value, &end, 10);
    return end == value || *end ? fallback : (int)result;
}

static int runtime_render_type(const char *kind)
{
    if (!strcmp(kind, "floor") || !strcmp(kind, "ground")) return TOY_MAP_DRAW_FLOOR;
    if (!strcmp(kind, "border")) return TOY_MAP_DRAW_BORDER;
    if (!strcmp(kind, "wall")) return TOY_MAP_DRAW_WALL;
    if (!strcmp(kind, "label")) return TOY_MAP_DRAW_LABEL;
    if (!strcmp(kind, "sign")) return TOY_MAP_DRAW_SIGN;
    if (!strcmp(kind, "model")) return TOY_MAP_DRAW_MODEL;
    if (!strcmp(kind, "texture")) return TOY_MAP_DRAW_TEXTURE;
    if (!strcmp(kind, "ramp")) return TOY_MAP_DRAW_RAMP;
    if (!strcmp(kind, "box")) return TOY_MAP_DRAW_BOX;
    if (!strcmp(kind, "platform")) return TOY_MAP_DRAW_PLATFORM;
    return -1;
}

static int runtime_collision_shape(const char *shape)
{
    if (!strcmp(shape, "box")) return TOY_MAP_PRIMITIVE_BOX;
    if (!strcmp(shape, "flat")) return TOY_MAP_PRIMITIVE_FLAT;
    if (!strcmp(shape, "ramp_x")) return TOY_MAP_PRIMITIVE_RAMP_X;
    if (!strcmp(shape, "ramp_z")) return TOY_MAP_PRIMITIVE_RAMP_Z;
    return -1;
}

static unsigned int runtime_collision_color(const char *value)
{
    return value ? (unsigned int)strtol(value, NULL, 16) : 0;
}

static int runtime_ai_class(const char *name)
{
    if (!strcmp(name, "level1")) return TOY_GAME_AI_LEVEL_1;
    if (!strcmp(name, "level2")) return TOY_GAME_AI_LEVEL_2;
    if (!strcmp(name, "level3")) return TOY_GAME_AI_LEVEL_3;
    return (int)strtol(name, NULL, 10);
}

static int runtime_projection_count_kind(const struct rf_map_runtime *runtime,
                                         const char *kind)
{
    int i, count = 0;
    for (i = 0; i < rf_map_runtime_region_count(runtime); i++) {
        const struct rf_map_runtime_region *region =
            rf_map_runtime_region_at(runtime, i);
        if (region && !strcmp(region->kind, kind)) count++;
    }
    return count;
}

int rasterfall_map_projection_counts_match(
    const struct rasterfall_map_state *map)
{
    if (!map || !map->level || !map->runtime_loaded || !map->interactable_count)
        return 0;
    const struct rf_map_runtime *runtime = &map->runtime;
    int interactables = rf_map_runtime_pickup_count(runtime) +
                        rf_map_runtime_interaction_count(runtime);
    return map->level->primitive_count ==
               rf_map_runtime_collision_count(runtime) &&
           map->level->draw_count == rf_map_runtime_render_count(runtime) &&
           map->level->safe_count == runtime_projection_count_kind(runtime, "safe") &&
           map->level->spawn_count == runtime_projection_count_kind(runtime, "spawn") &&
           map->level->ai_spawn_count == rf_map_runtime_actor_spawn_count(runtime) &&
           map->level->pickup_count == interactables &&
           *map->interactable_count == interactables;
}

/*
 * Runtime Map projection layer.
 * Runtime Map is authoritative world representation.
 * This creates the current gameplay-facing view.
 *
 * The projection retains explicit legacy_index lookups only because the
 * existing toy_map/gameplay-facing arrays still require stable slots.
 * This function is not a legacy loader.
 */
int rasterfall_map_project_runtime(struct rasterfall_map_state *map)
{
    int i, surface_adapter_count = 0;
    const struct rf_map_runtime_region *region;
    const struct rf_map_runtime_interaction *interaction;
    if (!map || !map->runtime_loaded || !map->level) return -1;

    {
        const struct rf_map_runtime_world *world =
            rf_map_runtime_world_info(&map->runtime);
        const struct rf_map_runtime_region *start =
            rf_map_runtime_find_region(&map->runtime, "player_start");
        if (!world || !start) return -1;
        map->level->minx = world->bounds.min_x;
        map->level->maxx = world->bounds.max_x;
        map->level->minz = world->bounds.min_z;
        map->level->maxz = world->bounds.max_z;
        map->level->room_limit = world->has_room_limit ?
            world->room_limit : 45000;
        map->level->start_x = start->bounds.min_x;
        map->level->start_z = start->bounds.min_z;
        map->level->start_sy = 0;
        map->level->start_cy = 1024;
    }

    /* Render records are the only presentation input in the V1 path.  Keep
     * toy_map_draw as a compatibility adapter so rasterfall_render.c remains
     * unchanged; legacy_index restores the old submission order after the
     * runtime's stable-ID ordering. */
    map->level->draw_count = 0;
    for (i = 0; i < rf_map_runtime_render_count(&map->runtime) &&
                map->level->draw_count < TOY_MAP_MAX_DRAW; i++) {
        const struct rf_map_runtime_render *render =
            runtime_render_projection_at(&map->runtime, i);
        struct toy_map_draw *draw;
        int type;
        const char *text;
        if (!render) continue;
        type = runtime_render_type(render->kind);
        if (type < 0) return -1;
        draw = &map->level->draw[map->level->draw_count++];
        __memset(draw, 0, sizeof(*draw));
        draw->type = type;
        draw->a = render->bounds.min_x;
        draw->b = render->bounds.max_x;
        draw->c = render->bounds.min_z;
        draw->d = render->bounds.max_z;
        draw->e = render->has_height ? render->height : 0;
        draw->f = runtime_render_int(render, "height2", 0);
        draw->color = render->has_color ?
            (unsigned int)strtol(render->color, NULL, 16) : 0;
        draw->style = runtime_render_int(render, "style", 0);
        draw->texture_u = runtime_render_int(render, "texture_u", 0);
        draw->texture_v = runtime_render_int(render, "texture_v", 0);
        if (type == TOY_MAP_DRAW_BOX && runtime_render_int(render, "top", 0))
            draw->e = runtime_render_int(render, "top", draw->e);
        text = rf_map_runtime_render_attribute(render, "text");
        if (!text) text = rf_map_runtime_render_attribute(render, "role");
        if (text) strncpy(draw->text, text, sizeof(draw->text) - 1);
        if (type == TOY_MAP_DRAW_FLOOR && !strcmp(render->kind, "ground"))
            draw->style = TOY_MAP_FLOOR_GROUND;
    }
    if (map->level->draw_count != rf_map_runtime_render_count(&map->runtime))
        return -1;

    map->level->primitive_count = 0;
    for (i = 0; i < rf_map_runtime_collision_count(&map->runtime) &&
                map->level->primitive_count < TOY_MAP_MAX_PRIMITIVES; i++) {
        const struct rf_map_runtime_collision *collision =
            runtime_collision_projection_at(&map->runtime, i);
        const struct rf_map_runtime_surface *surface =
            runtime_surface_projection_at(&map->runtime, i);
        struct toy_map_primitive *primitive;
        int shape;
        if (!collision) continue;
        shape = runtime_collision_shape(collision->shape);
        if (shape < 0) return -1;
        primitive = &map->level->primitives[map->level->primitive_count++];
        __memset(primitive, 0, sizeof(*primitive));
        primitive->shape = shape;
        primitive->minx = collision->bounds.min_x;
        primitive->maxx = collision->bounds.max_x;
        primitive->minz = collision->bounds.min_z;
        primitive->maxz = collision->bounds.max_z;
        primitive->base_y = 0;
        primitive->surface_y0 = collision->height;
        primitive->surface_y1 = collision->has_height2 ?
            collision->height2 : collision->height;
        primitive->flags = (collision->collision ? TOY_MAP_PRIMITIVE_COLLISION : 0) |
            (collision->visible ? TOY_MAP_PRIMITIVE_VISIBLE : 0) |
            (collision->walkable ? TOY_MAP_PRIMITIVE_WALKABLE : 0) |
            (collision->blocks_airborne ? TOY_MAP_PRIMITIVE_BLOCKS_AIRBORNE : 0);
        primitive->color = collision->has_color ?
            runtime_collision_color(collision->color) : 0;
        if (collision->has_role)
            strncpy(primitive->role, collision->role, sizeof(primitive->role) - 1);
        /* Surface geometry is now authored by the V1 surface record.  The
         * collision record still owns collision flags and blocking policy;
         * this adapter only feeds the old primitive/world data structure. */
        if (surface) {
            primitive->minx = surface->bounds.min_x;
            primitive->maxx = surface->bounds.max_x;
            primitive->minz = surface->bounds.min_z;
            primitive->surface_y0 = surface->height;
            primitive->surface_y1 = surface->has_height2 ?
                surface->height2 : surface->height;
            if (!strcmp(surface->kind, "ramp")) {
                if (!surface->has_axis || !strcmp(surface->axis, "x"))
                    primitive->shape = TOY_MAP_PRIMITIVE_RAMP_X;
                else if (!strcmp(surface->axis, "z"))
                    primitive->shape = TOY_MAP_PRIMITIVE_RAMP_Z;
            } else {
                primitive->shape = TOY_MAP_PRIMITIVE_FLAT;
            }
            if (primitive->minx != surface->bounds.min_x ||
                primitive->maxx != surface->bounds.max_x ||
                primitive->minz != surface->bounds.min_z ||
                primitive->maxz != surface->bounds.max_z ||
                primitive->surface_y0 != surface->height ||
                primitive->surface_y1 != (surface->has_height2 ?
                                           surface->height2 : surface->height))
                return -1;
            surface_adapter_count++;
        }
    }
    if (surface_adapter_count != rf_map_runtime_surface_count(&map->runtime))
        return -1;

    map->level->safe_count = 0;
    /* V1 source order has no semantics.  legacy_index is explicit adapter
     * metadata used only while old gameplay arrays still require an order. */
    for (i = 0; i < rf_map_runtime_region_count(&map->runtime); i++) {
        region = runtime_region_projection_at(&map->runtime, i);
        if (!region) continue;
        if (!strcmp(region->kind, "safe") &&
            map->level->safe_count < TOY_MAP_MAX_ZONES) {
            map->level->safe_rooms[map->level->safe_count].minx =
                region->bounds.min_x;
            map->level->safe_rooms[map->level->safe_count].maxx =
                region->bounds.max_x;
            map->level->safe_rooms[map->level->safe_count].minz =
                region->bounds.min_z;
            map->level->safe_rooms[map->level->safe_count].maxz =
                region->bounds.max_z;
            map->safe_rooms[map->level->safe_count] =
                map->level->safe_rooms[map->level->safe_count];
            map->level->safe_count++;
        }
    }

    map->level->spawn_count = 0;
    for (i = 0; i < rf_map_runtime_region_count(&map->runtime); i++) {
        region = runtime_region_projection_at(&map->runtime, i);
        if (!region) continue;
        if (!strcmp(region->kind, "spawn") &&
            map->level->spawn_count < TOY_MAP_MAX_ZONES) {
            map->level->spawn_zones[map->level->spawn_count].box.minx =
                region->bounds.min_x;
            map->level->spawn_zones[map->level->spawn_count].box.maxx =
                region->bounds.max_x;
            map->level->spawn_zones[map->level->spawn_count].box.minz =
                region->bounds.min_z;
            map->level->spawn_zones[map->level->spawn_count].box.maxz =
                region->bounds.max_z;
            map->level->spawn_zones[map->level->spawn_count].color = 0;
            {
                int j;
                for (j = 0; j < map->level->draw_count; j++) {
                    const struct toy_map_draw *draw = &map->level->draw[j];
                    if (draw->type != TOY_MAP_DRAW_FLOOR ||
                        draw->a != region->bounds.min_x ||
                        draw->b != region->bounds.max_x ||
                        draw->c != region->bounds.min_z ||
                        draw->d != region->bounds.max_z)
                        continue;
                    map->level->spawn_zones[map->level->spawn_count].color =
                        draw->color;
                    break;
                }
            }
            map->spawn_zones[map->level->spawn_count] =
                map->level->spawn_zones[map->level->spawn_count].box;
            map->level->spawn_count++;
        }
    }
    *map->spawn_count = map->level->spawn_count;

    map->level->base_count = 0;
    for (i = 0; i < rf_map_runtime_region_count(&map->runtime); i++) {
        region = runtime_region_projection_at(&map->runtime, i);
        if (!region) continue;
        if (!strcmp(region->kind, "base") &&
            map->level->base_count < TOY_MAP_MAX_BASES) {
            map->level->bases[map->level->base_count].id =
                map->level->base_count;
            map->level->bases[map->level->base_count].box.minx =
                region->bounds.min_x;
            map->level->bases[map->level->base_count].box.maxx =
                region->bounds.max_x;
            map->level->bases[map->level->base_count].box.minz =
                region->bounds.min_z;
            map->level->bases[map->level->base_count].box.maxz =
                region->bounds.max_z;
            map->level->base_count++;
        }
    }

    map->level->ai_spawn_count = 0;
    for (i = 0; i < rf_map_runtime_actor_spawn_count(&map->runtime) &&
                map->level->ai_spawn_count < TOY_MAP_MAX_AI_SPAWNS; i++) {
        const struct rf_map_runtime_actor_spawn *spawn =
            runtime_spawn_projection_at(&map->runtime, i);
        struct toy_map_ai_spawn *old;
        if (!spawn) continue;
        old = &map->level->ai_spawns[map->level->ai_spawn_count++];
        __memset(old, 0, sizeof(*old));
        strncpy(old->name, spawn->id, sizeof(old->name) - 1);
        old->base_id = spawn->base_id;
        old->class_id = runtime_ai_class(spawn->class_name);
        old->x = spawn->x;
        old->z = spawn->z;
        old->downed = spawn->downed;
        old->weapon = spawn->has_weapon ?
            toy_game_weapon_from_name(spawn->weapon) : -1;
    }

    map->level->prop_count = 0;
    for (i = 0; i < rf_map_runtime_object_count(&map->runtime) &&
                map->level->prop_count < TOY_MAP_MAX_PROPS; i++) {
        const struct rf_map_runtime_object *object =
            runtime_object_projection_at(&map->runtime, i);
        const struct rasterfall_prop_asset_profile *profile;
        struct toy_map_prop *prop;
        if (!object) continue;
        profile = rasterfall_prop_asset_by_name(object->kind);
        if (!profile) continue;
        prop = &map->level->props[map->level->prop_count++];
        prop->asset_id = profile->id;
        prop->x = object->x;
        prop->z = object->z;
        prop->yaw_degrees = object->yaw;
        prop->scale_milli = object->scale;
    }

    /* Interactions are deliberately translated only at this compatibility
     * boundary.  The V1 parser and runtime registry retain action strings. */
    map->level->pickup_count = 0;
    for (i = 0; i < TOY_MAP_MAX_PICKUPS; i++) {
        int kind, weapon;
        const struct rf_map_runtime_pickup *pickup =
            runtime_pickup_projection_at(&map->runtime, i);
        interaction = runtime_interaction_projection_at(&map->runtime, i);
        if (pickup) {
            weapon = toy_game_weapon_from_name(pickup->kind);
            if (weapon == TOY_GAME_WEAPON_PILL) kind = TOY_MAP_PICKUP_PILL;
            else if (weapon == TOY_GAME_WEAPON_BOMB ||
                     weapon == TOY_GAME_WEAPON_MOLOTOV)
                kind = TOY_MAP_PICKUP_THROWABLE;
            else if (weapon >= 0) kind = TOY_MAP_PICKUP_WEAPON;
            else if (!strcmp(pickup->kind, "ammo")) kind = TOY_MAP_PICKUP_AMMO;
            else if (!strcmp(pickup->kind, "shop")) kind = TOY_MAP_PICKUP_SHOP;
            else kind = -1;
        } else if (interaction) {
            kind = runtime_action_to_pickup(interaction->action_id, &weapon);
        } else {
            continue;
        }
        if (kind < 0 || map->level->pickup_count >= TOY_MAP_MAX_PICKUPS)
            continue;
        map->level->pickups[map->level->pickup_count].kind = kind;
        map->level->pickups[map->level->pickup_count].weapon = weapon;
        map->level->pickups[map->level->pickup_count].x = pickup ? pickup->x : interaction->x;
        map->level->pickups[map->level->pickup_count].y = pickup ? pickup->y : interaction->y;
        map->level->pickups[map->level->pickup_count].z = pickup ? pickup->z : interaction->z;
        map->level->pickup_count++;
    }
    rasterfall_map_prepare(map);
    rasterfall_map_reset_interactables(map);
    return rasterfall_map_projection_counts_match(map) ? 0 : -1;
}
