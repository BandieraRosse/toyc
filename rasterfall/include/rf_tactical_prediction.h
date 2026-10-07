#ifndef RF_TACTICAL_PREDICTION_H
#define RF_TACTICAL_PREDICTION_H

#include "rf_tactical.h"

#define RF_TAC_PREDICTION_MAX_MS 4000

enum rf_tac_prediction_result {
    RF_TAC_PRED_INVALID = 0,
    RF_TAC_PRED_OK = 1,
    RF_TAC_PRED_BUDGET = -1
};

/* Reproducible sampled Game rollout, not a probability distribution.
 * No hidden HP/risk split, seed, RNG, map or mutable world is
 * returned. before/after are standard effective-health unit projections. */
struct rf_tac_forecast_unit {
    struct rf_tac_unit_view before, after;
    int moving, inside_objective, shots_fired;
    struct rf_tac_vec destination;
    float objective_distance, objective_path_distance;
    float outgoing_dps, incoming_dps;
    float remaining_move_time_s, remaining_path_incoming_damage;
    float destination_outgoing_dps, destination_incoming_dps, destination_fire_ready_time_s;
    struct rf_tac_relation destination_relations[RF_TAC_MAX_SQUAD];
    struct rf_tac_relation destination_reverse_relations[RF_TAC_MAX_SQUAD];
    float destination_incoming_ready_time_s[RF_TAC_MAX_SQUAD];
};

struct rf_tac_forecast {
    int team, count, requested_ms, elapsed_ms, steps;
    int finished, winner;
    /* Always set for sampled Game rollouts; not a confidence interval or
     * validation of the opponent's assumed future actions. */
    int uncertain_shots;
    int alive_before[2], alive_after[2];
    float health_before[2], health_after[2];
    /* Index zero is the requesting side, index one its opponent. winner
     * retains authoritative team numbering; orders retain capture progress. */
    struct rf_tac_order orders[2];
    struct rf_tac_forecast_unit friendly[RF_TAC_MAX_SQUAD], enemy[RF_TAC_MAX_SQUAD];
    /* Current-position firing potential, not DPS awarded while MOVE is active.
     * reverse_relations[e][f] is enemy-to-friendly. Destination relations are
     * a tail estimate holding enemy terminal positions fixed and projecting
     * weapon readiness. Incoming readiness is max(arrival, enemy readiness),
     * independent of this unit's reload; these are firing potentials, not
     * actual damage awarded before arrival. */
    struct rf_tac_relation relations[RF_TAC_MAX_SQUAD][RF_TAC_MAX_SQUAD];
    struct rf_tac_relation reverse_relations[RF_TAC_MAX_SQUAD][RF_TAC_MAX_SQUAD];
};

/* A solver receives this interface only. Plans refer to the fixed root
 * observations; enemy == NULL explicitly assumes every opponent will HOLD.
 * Duration is a positive 200ms multiple, at most 4000ms. Enough budget for
 * the entire duration must remain before evaluation begins. Each simulated
 * 16ms step costs one unit; durations round up to full Game steps and early
 * terminal forecasts report actual steps.
 * INVALID/BUDGET leave output zero and do not consume prediction steps. */
struct rf_tac_predictor {
    void *opaque;
    int (*evaluate)(void *opaque, const struct rf_tac_plan *friendly,
                    const struct rf_tac_plan *enemy, int duration_ms,
                    struct rf_tac_forecast *out);
    int (*remaining_steps)(const void *opaque);
};

/* Host-owned lifecycle. The implementation alone owns the private snapshot
 * and prepared execution routes. The source is copied and its map borrowed
 * read-only: keep the map alive and immutable until destroy/reset. Neither
 * successful nor failed evaluations mutate the source world/map/RNG. */
struct rf_tac_prediction;
struct rf_tac_prediction *rf_tac_prediction_create(const struct rf_tac_world *source,
                                                    int team, int step_budget);
int rf_tac_prediction_reset(struct rf_tac_prediction *prediction,
                            const struct rf_tac_world *source, int team, int step_budget);
void rf_tac_prediction_destroy(struct rf_tac_prediction *prediction);
void rf_tac_prediction_provider(struct rf_tac_prediction *prediction,
                                struct rf_tac_predictor *out);

#endif
