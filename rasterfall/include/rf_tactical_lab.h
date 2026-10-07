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

/* Session-owned authority. Frontends only submit controls and read results. */
struct rf_tac_match {
    struct rf_tac_map map;
    struct rf_tac_world world;
    struct rf_tac_world previous; /* Read-only display history, never advanced. */
    struct rf_tac_policy policies[2];
    struct rf_tac_observation observations[2];
    struct rf_tac_plan plans[2];
    struct rf_tac_decision_trace traces[2];
    struct rf_tac_prediction *prediction[2];
    int ready, running, remainder_ms;
};
int rf_tac_match_reset(struct rf_tac_match *m, unsigned map_seed, unsigned shot_seed,
    int squad, int weapon, const struct rf_tac_policy policies[2]);
int rf_tac_match_step(struct rf_tac_match *m);
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
    rf_tw_state weapons[2];
    struct rf_range_stats stats[2][RF_RANGE_LANES];
    struct rf_range_mark marks[RF_RANGE_MARKS];
    unsigned rng[2];
    int mark_count, weapon, mode, lane, target, running, wait_ms, pattern_shots;
};
void rf_range_reset(struct rf_range_lab *r, int weapon, int mode, int lane, int target, unsigned seed);
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
