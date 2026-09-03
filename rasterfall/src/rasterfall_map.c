#include "tlibc_everything.h"
#include "rasterfall_map.h"

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
    if (map) toy_map_unload(map->level);
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
