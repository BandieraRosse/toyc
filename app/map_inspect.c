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

static void dump_collisions_json(const struct rf_map_runtime *map)
{
    int i;
    __printf("[\n");
    for (i = 0; i < rf_map_runtime_collision_count(map); i++) {
        const struct rf_map_runtime_collision *c = rf_map_runtime_collision_at(map, i);
        /* IDs/shapes are validated ASCII names; no arbitrary strings here. */
        __printf("%s{\"id\":\"%s\",\"owner_id\":\"%s\",\"shape\":\"%s\","
                 "\"min_x\":%d,\"max_x\":%d,\"min_z\":%d,\"max_z\":%d,"
                 "\"base_y\":%d,\"height\":%d,\"collision\":%s,\"walkable\":%s,"
                 "\"blocks_airborne\":%s,\"line\":%d}",
                 i ? ",\n" : "", c->id, c->owner_id, c->shape,
                 c->bounds.min_x, c->bounds.max_x, c->bounds.min_z, c->bounds.max_z,
                 c->base_y, c->height, c->collision ? "true" : "false",
                 c->walkable ? "true" : "false", c->blocks_airborne ? "true" : "false", c->line);
    }
    __printf("\n]\n");
}

int main(int argc, char **argv)
{
    struct rf_map_runtime map;
    int json = argc == 3 && !strcmp(argv[1], "--collision-json");
    const char *path = json ? argv[2] : argc == 1 ? "rasterfall/assets/maps/rasterfall.map" :
                       argc == 2 ? argv[1] : NULL;
    if (!path) {
        __fprintf(2, "usage: map-inspect [--collision-json] [map-file]\n");
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
    if (json) dump_collisions_json(&map);
    else dump_map(&map);
    rf_map_runtime_unload(&map);
    return 0;
}
