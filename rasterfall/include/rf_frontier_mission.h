#ifndef RF_FRONTIER_MISSION_H
#define RF_FRONTIER_MISSION_H

#include "toy_game.h"

#define RF_FRONTIER_ID_CAP 64
#define RF_FRONTIER_GUARDS 13
#define RF_FRONTIER_FACILITIES 3
#define RF_FRONTIER_TRACKED 96

enum rf_frontier_phase {
    RF_FRONTIER_INACTIVE, RF_FRONTIER_ASSAULT, RF_FRONTIER_PREPARE,
    RF_FRONTIER_COUNTERATTACK, RF_FRONTIER_SECURED, RF_FRONTIER_FAILED
};
enum rf_frontier_role {
    RF_FRONTIER_GUARD, RF_FRONTIER_PERIODIC_INFECTED,
    RF_FRONTIER_FINAL_INFECTED, RF_FRONTIER_REINFORCEMENT_GUNNER
};
enum rf_frontier_event {
    RF_FRONTIER_EVENT_ARRIVAL = 1,
    RF_FRONTIER_EVENT_PERIODIC = 2,
    RF_FRONTIER_EVENT_PREPARE = 4,
    RF_FRONTIER_EVENT_COUNTERATTACK = 8,
    RF_FRONTIER_EVENT_SECURED = 16,
    RF_FRONTIER_EVENT_FAILED = 32
};
enum rf_frontier_capture_result {
    RF_FRONTIER_CAPTURE_INVALID, RF_FRONTIER_CAPTURE_APPLIED,
    RF_FRONTIER_CAPTURE_ALREADY
};
struct rf_frontier_point {
    char id[RF_FRONTIER_ID_CAP];
    int x, z, y;
};
struct rf_frontier_guard_config {
    struct rf_frontier_point point;
    int weapon, elite, guard_radius;
};
struct rf_frontier_facility_config {
    struct rf_frontier_point point;
    unsigned int identity, generation;
    int interact_range;
};
struct rf_frontier_config {
    int offline_authority;
    struct rf_frontier_guard_config guards[RF_FRONTIER_GUARDS];
    struct rf_frontier_point north, east, west;
    int assault_x, assault_z;
    struct rf_frontier_facility_config facilities[RF_FRONTIER_FACILITIES];
};
/* Slot is only a lookup hint. Generation and stable identity are always checked.
 * Periodic records are recycled after death; total spawned is cumulative. */
struct rf_frontier_entity {
    unsigned int mission_id, generation;
    int active, actor, index, stable_id, role, ordinal;
};
struct rf_frontier_mission {
    struct rf_frontier_config config;
    unsigned int mission_id, pending_events;
    int phase, phase_ms, periodic_ms, periodic_opportunities;
    int periodic_spawned, periodic_skipped, periodic_alive;
    int guards_alive, enemies_alive, captured[RF_FRONTIER_FACILITIES];
    int final_infected_spawned, final_gunners_spawned, pending_spawns;
    int first_final_infected_ms, retry_ms, gunner_retry_ms;
    int spawn_failures, spawn_attempts;
    int victory_count;
    struct rf_frontier_entity entities[RF_FRONTIER_TRACKED];
};

/* begin validates every binding and creates all initial defenders atomically.
 * Loading does not advance mission time. Call step only from live authority
 * fixed steps, after the normal Game update; paused frames never call step. */
int rf_frontier_mission_begin(struct rf_frontier_mission *mission,
    struct toy_game *game, const struct rf_frontier_config *config,
    unsigned int mission_id);
void rf_frontier_mission_step(struct rf_frontier_mission *mission,
    struct toy_game *game, int dt_ms);
int rf_frontier_mission_capture(struct rf_frontier_mission *mission,
    const struct toy_game *game, const char *facility_id,
    unsigned int facility_identity, unsigned int facility_generation,
    int actor_id, unsigned int actor_generation);
void rf_frontier_mission_fail(struct rf_frontier_mission *mission);
void rf_frontier_mission_reset(struct rf_frontier_mission *mission,
    struct toy_game *game);
unsigned int rf_frontier_mission_drain_events(struct rf_frontier_mission *mission);
const char *rf_frontier_mission_phase_name(int phase);

#endif
