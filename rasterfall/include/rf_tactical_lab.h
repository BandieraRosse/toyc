#ifndef RF_TACTICAL_LAB_H
#define RF_TACTICAL_LAB_H
/* Native hosted frontend. The freestanding frontend keeps its prior feature set. */
#ifdef TOYC_WINDOWS
#define RF_TACTICAL_INTERACTIVE 1
#else
#define RF_TACTICAL_INTERACTIVE 0
#endif
#include "rf_tactical.h"
#include "rf_tactical_weapon.h"
#include "rf_tactical_prediction.h"
#include "rf_ai_host.h"

/* Session-owned authority. Frontends only submit controls and read results. */
struct rf_tac_match {
    struct toy_game *bound_game; /* borrowed session Game; NULL for owned CLI */
    struct rf_tac_map map;
    struct rf_tac_world world;
    struct rf_tac_world previous; /* Read-only display history, never advanced. */
    struct rf_tac_policy policies[2];
    struct rf_tac_observation observations[2];
    struct rf_tac_plan plans[2];
    struct rf_tac_decision_trace traces[2];
    struct rf_tac_prediction *prediction[2];
    struct rf_ai_host ai[2];
    int ready, running, remainder_ms;
};
int rf_tac_match_reset(struct rf_tac_match *m, unsigned map_seed, unsigned shot_seed,
    int squad, int weapon, const struct rf_tac_policy policies[2]);
int rf_tac_match_step(struct rf_tac_match *m);
int rf_tac_match_prepare(struct rf_tac_match *m);
void rf_tac_match_destroy(struct rf_tac_match *m);

#define RF_RANGE_LANES 9
#define RF_RANGE_MARKS 128
extern const int rf_range_distances[RF_RANGE_LANES];
struct rf_range_stats {
    int shots, hits, heads, kills, elapsed_ms, trial_ms;
    float damage, absorbed, hp, risk;
    float distance_total, distance_min, distance_max;
    int recovery_ms, ttk_total_ms, last_ttk_ms;
};
struct rf_range_mark { float x, y; int lane, hit, shooter; };
struct rf_range_lab {
    struct toy_game *game;
    int owns_game, shooter_ids[2], target_ids[2][RF_RANGE_LANES];
    unsigned shooter_generations[2], target_generations[2][RF_RANGE_LANES], event_cursor;
    struct toy_map_primitive primitives[TOY_GAME_MAX_PRIMITIVES];
    const struct toy_map_primitive *base_primitives;
    int base_primitive_count, primitive_count, missed_events;
    int trial_elapsed[2][RF_RANGE_LANES], trial_owner[2][RF_RANGE_LANES];
    rf_tw_state weapons[2];
    struct rf_range_stats stats[2][RF_RANGE_LANES];
    struct rf_range_mark marks[RF_RANGE_MARKS];
    unsigned rng[2];
    int mark_count, weapon, mode, lane, target, running, wait_ms, pattern_shots;
};
void rf_range_reset(struct rf_range_lab *r, int weapon, int mode, int lane, int target, unsigned seed);
void rf_range_destroy(struct rf_range_lab *r);
int rf_range_bind(struct rf_range_lab *r, struct toy_game *game);
void rf_range_prepare(struct rf_range_lab *r, int dt_ms);
void rf_range_collect(struct rf_range_lab *r, int dt_ms);
void rf_range_step(struct rf_range_lab *r, int dt_ms);
int rf_range_fire(struct rf_range_lab *r, int shooter, int lane, float aim_x, float aim_y);
int rf_range_fire_distance(struct rf_range_lab *r,int shooter,int lane,float aim_x,float aim_y,float distance);
struct rf_tactical_lab {
    struct rf_tac_match match;
    struct rf_range_lab range;
    int kind, accumulator_us, selected, generation, player_firing;
    int pending_fire, pending_reload;
    unsigned seed;
    int presented_shots[RF_TAC_MAX_UNITS], presented_marks;
    char output[192];
};
int rf_tactical_lab_export(struct rf_tactical_lab *lab);
#endif
