#include "core.h"
#include "tlibc_everything.h"
#include "rasterfall_map_runtime.h"

static void dump_map(const struct rf_map_runtime *map)
{
    int i;
    const struct rf_map_runtime_world *world = rf_map_runtime_world_info(map);
    __printf("parse success\n");
    __printf("World:\nmin_x=%d max_x=%d min_z=%d max_z=%d\n",
             world->bounds.min_x, world->bounds.max_x,
             world->bounds.min_z, world->bounds.max_z);
    __printf("Regions: %d\n", rf_map_runtime_region_count(map));
    for (i = 0; i < rf_map_runtime_region_count(map); i++) {
        const struct rf_map_runtime_region *item = rf_map_runtime_region_at(map, i);
        __printf("  %s kind=%s\n", item->id, item->kind);
    }
    __printf("Collision: %d\n", rf_map_runtime_collision_count(map));
    __printf("Surfaces: %d\n", rf_map_runtime_surface_count(map));
    __printf("Render:\ncount: %d\n", rf_map_runtime_render_count(map));
    __printf("Interactions: %d\n", rf_map_runtime_interaction_count(map));
    __printf("Actor spawns: %d\n", rf_map_runtime_actor_spawn_count(map));
    __printf("Pickups: %d\n", rf_map_runtime_pickup_count(map));
    __printf("Objects: %d\n", rf_map_runtime_object_count(map));
}

int main(int argc, char **argv)
{
    struct rf_map_runtime map;
    const char *path = argc == 1 ? "rasterfall/assets/maps/rasterfall.map" :
                       argc == 2 ? argv[1] : NULL;
    if (!path) {
        __fprintf(2, "usage: map-inspect [map-file]\n");
        return 2;
    }
    __memset(&map, 0, sizeof(map));
    if (rf_map_runtime_load(&map, path) < 0) {
        if (map.error_line > 0)
            __fprintf(2, "%s:%d:\nerror: %s\n", path, map.error_line, map.error);
        else
            __fprintf(2, "%s:\nerror: %s\n", path, map.error);
        return 1;
    }
    dump_map(&map);
    rf_map_runtime_unload(&map);
    return 0;
}
