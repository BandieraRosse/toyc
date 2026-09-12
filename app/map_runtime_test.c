#include "core.h"
#include "tlibc_everything.h"
#include "rasterfall_map_runtime.h"

int main(int argc, char **argv)
{
    struct rf_map_runtime runtime;
    const struct rf_map_runtime_region *safe;
    const struct rf_map_runtime_interaction *interaction;
    const struct rf_map_runtime_actor_spawn *spawn;
    const struct rf_map_runtime_pickup *pickup;
    const struct rf_map_runtime_object *object;
    const struct rf_map_runtime_collision *collision;
    const struct rf_map_runtime_surface *surface;
    const struct rf_map_runtime_render *render;
    if (argc != 2) {
        __fprintf(2, "usage: map-runtime-test map-file\n");
        return 2;
    }
    __memset(&runtime, 0, sizeof(runtime));
    if (rf_map_runtime_load(&runtime, argv[1]) < 0) {
        if (runtime.error_line > 0)
            __fprintf(2, "%s:%d:\nerror: %s\n", argv[1],
                      runtime.error_line, runtime.error);
        else
            __fprintf(2, "%s:\nerror: %s\n", argv[1], runtime.error);
        return 1;
    }
    if (rf_map_runtime_interaction_count(&runtime) == 0) {
        safe = rf_map_runtime_find_region(&runtime, "outpost_safe");
        if (!safe) { rf_map_runtime_unload(&runtime); return 1; }
        __printf("runtime map success\nregions: %d\ninteractions: 0\nspawns: %d\npickups: %d\nobjects: %d\ncollisions: %d\nsurfaces: %d\nrenders: %d\nsafe: %s\n",
                 rf_map_runtime_region_count(&runtime),
                 rf_map_runtime_actor_spawn_count(&runtime),
                 rf_map_runtime_pickup_count(&runtime),
                 rf_map_runtime_object_count(&runtime),
                 rf_map_runtime_collision_count(&runtime),
                 rf_map_runtime_surface_count(&runtime),
                 rf_map_runtime_render_count(&runtime), safe->id);
        rf_map_runtime_unload(&runtime);
        return 0;
    }
    safe = rf_map_runtime_find_region(&runtime, "start_area");
    if (!safe) safe = rf_map_runtime_find_region(&runtime, "safe_start");
    if (!safe) safe = rf_map_runtime_find_region(&runtime, "outpost_safe");
    interaction = rf_map_runtime_find_interaction(&runtime, "wave_skip");
    if (!interaction) interaction = rf_map_runtime_find_interaction(&runtime,
                                                                      "station_terminal");
    spawn = rf_map_runtime_find_spawn(&runtime, "Jesus");
    if (!spawn) spawn = rf_map_runtime_find_spawn(&runtime, "Null");
    if (!spawn) spawn = rf_map_runtime_find_spawn(&runtime, "GUARD");
    pickup = rf_map_runtime_find_pickup(&runtime, "pickup_smg");
    object = rf_map_runtime_find_object(&runtime, "object_crate");
    collision = rf_map_runtime_find_collision(&runtime, "box_air_gate_left");
    if (!collision) collision = rf_map_runtime_collision_at(&runtime, 0);
    surface = rf_map_runtime_find_surface(&runtime, "surface_ramp_dev_exit");
    if (!surface) surface = rf_map_runtime_surface_at(&runtime, 0);
    render = rf_map_runtime_find_render(&runtime, "arena");
    if (!render) render = rf_map_runtime_render_at(&runtime, 0);
    if (rf_map_runtime_actor_spawn_count(&runtime) > 0 && !spawn) {
        __fprintf(2, "spawn lookup failed\n");
        rf_map_runtime_unload(&runtime);
        return 1;
    }
    if (rf_map_runtime_pickup_count(&runtime) > 0 && !pickup) {
        __fprintf(2, "pickup lookup failed\n");
        rf_map_runtime_unload(&runtime);
        return 1;
    }
    if (rf_map_runtime_object_count(&runtime) > 0 && !object) {
        __fprintf(2, "object lookup failed\n");
        rf_map_runtime_unload(&runtime);
        return 1;
    }
    if (rf_map_runtime_collision_count(&runtime) > 0 && !collision) {
        __fprintf(2, "collision lookup failed\n");
        rf_map_runtime_unload(&runtime);
        return 1;
    }
    if (!safe ||
        (rf_map_runtime_interaction_count(&runtime) > 0 && !interaction) ||
        (strcmp(interaction->id, "wave_skip") == 0 &&
         interaction->action_id != RF_MAP_ACTION_WAVE_SKIP) ||
        rf_map_runtime_find_region(&runtime, "missing") != NULL) {
        __fprintf(2, "runtime lookup failed\n");
        rf_map_runtime_unload(&runtime);
        return 1;
    }
    if (rf_map_runtime_surface_count(&runtime) > 0 && !surface) {
        __fprintf(2, "surface lookup failed\n");
        rf_map_runtime_unload(&runtime);
        return 1;
    }
    if (rf_map_runtime_find_surface(&runtime, "surface_ramp_dev_exit") &&
        (strcmp(surface->kind, "ramp") || !surface->has_height2 ||
         !surface->has_axis || strcmp(surface->axis, "z"))) {
        __fprintf(2, "ramp surface lookup failed\n");
        rf_map_runtime_unload(&runtime);
        return 1;
    }
    if (rf_map_runtime_render_count(&runtime) > 0 && !render) {
        __fprintf(2, "render lookup failed\n");
        rf_map_runtime_unload(&runtime);
        return 1;
    }
    __printf("runtime map success\n");
    __printf("regions: %d\n", rf_map_runtime_region_count(&runtime));
    __printf("interactions: %d\n",
             rf_map_runtime_interaction_count(&runtime));
    __printf("spawns: %d\n", rf_map_runtime_actor_spawn_count(&runtime));
    __printf("pickups: %d\n", rf_map_runtime_pickup_count(&runtime));
    __printf("objects: %d\n", rf_map_runtime_object_count(&runtime));
    __printf("collisions: %d\n", rf_map_runtime_collision_count(&runtime));
    __printf("surfaces: %d\n", rf_map_runtime_surface_count(&runtime));
    __printf("renders: %d\n", rf_map_runtime_render_count(&runtime));
    __printf("safe: %s\n", safe->id);
    if (interaction)
        __printf("interaction: %s action=%s\n", interaction->id,
                 rf_map_runtime_action_name(interaction->action_id));
    rf_map_runtime_unload(&runtime);
    return 0;
}
