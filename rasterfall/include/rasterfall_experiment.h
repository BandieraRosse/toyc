#ifndef RASTERFALL_EXPERIMENT_H
#define RASTERFALL_EXPERIMENT_H
#include "rasterfall_map_runtime.h"

#define RF_EXPERIMENT_GROUPS 4
#define RF_EXPERIMENT_ACTORS 24
struct rasterfall_session;
struct rf_experiment_actor_spec {
    int character, weapon, animation, mode, time_ms;
    int x, y, z, sy, cy;
    int enemy_kind; /* zero: actor; positive: enemy type + 1 */
    int target;
};
struct rf_experiment_group {
    int active, count, paused, behavior, elapsed_ms;
    unsigned serial;
    int ids[RF_EXPERIMENT_ACTORS], modes[RF_EXPERIMENT_ACTORS];
    unsigned generations[RF_EXPERIMENT_ACTORS];
    struct rf_map_runtime map;
};
struct rasterfall_experiments {
    struct rf_map_runtime base;
    struct rf_experiment_group groups[RF_EXPERIMENT_GROUPS];
    unsigned serial;
};
/* Replace one owned group atomically. NULL specs and map destroy the group.
 * Map fragments use world coordinates and the ordinary component catalog. */
int rasterfall_experiment_replace(struct rasterfall_session *session, int group,
    const struct rf_experiment_actor_spec *actors, int count, const char *map_path);
int rasterfall_experiment_pause(struct rasterfall_session *session, int group, int paused);
void rasterfall_experiment_dispose(struct rasterfall_session *session, int restore_base);
#endif
