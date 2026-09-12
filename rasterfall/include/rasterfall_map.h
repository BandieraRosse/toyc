#ifndef RASTERFALL_MAP_H
#define RASTERFALL_MAP_H

#include "toy_map.h"
#include "rasterfall_map_runtime.h"

struct rasterfall_interactable { int kind; int weapon; int x, z, y; };

struct rasterfall_map_state {
    /* Compatibility/runtime view consumed by existing gameplay, collision,
     * and renderer interfaces.  The Runtime Map below remains authoritative. */
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
/* Runtime Map projection layer.  Runtime Map is authoritative world data;
 * this creates the current gameplay-facing view.  It is not a legacy loader. */
int rasterfall_map_project_runtime(struct rasterfall_map_state *map);
/* Count-only migration guard; does not inspect gameplay behavior. */
int rasterfall_map_projection_counts_match(
    const struct rasterfall_map_state *map);

#endif
