#ifndef RASTERFALL_MAP_H
#define RASTERFALL_MAP_H

#include "toy_map.h"
#include "rasterfall_map_runtime.h"

struct rasterfall_interactable { int kind; int weapon; int x, z, y; };

struct rasterfall_map_state {
    struct toy_map *level;
    struct toy_game_box *safe_rooms;
    struct toy_game_box *spawn_zones;
    int *spawn_count;
    int air_wall_indices[TOY_MAP_MAX_BASES];
    int air_wall_count;
    int *air_walls_enabled;
    struct rasterfall_interactable *interactables;
    int *interactable_count;
    struct rf_map_runtime runtime;
    int runtime_loaded;
};

void rasterfall_map_bind(struct rasterfall_map_state *map,
                         struct toy_map *level,
                         struct toy_game_box *safe_rooms,
                         struct toy_game_box *spawn_zones,
                         int *spawn_count,
                         int *air_walls_enabled,
                         struct rasterfall_interactable *interactables,
                         int *interactable_count);
int rasterfall_map_load(struct rasterfall_map_state *map, const char *path);
void rasterfall_map_unload(struct rasterfall_map_state *map);
void rasterfall_map_prepare(struct rasterfall_map_state *map);
void rasterfall_map_set_air_walls(struct rasterfall_map_state *map, int enabled);
void rasterfall_map_reset_interactables(struct rasterfall_map_state *map);
int rasterfall_map_load_runtime_overlay(struct rasterfall_map_state *map,
                                        const char *path);
int rasterfall_map_apply_runtime_legacy(struct rasterfall_map_state *map);

#endif
