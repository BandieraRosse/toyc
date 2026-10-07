#ifndef RF_AI_HOST_H
#define RF_AI_HOST_H
#include "rf_ai.h"
#include "rf_tactical.h"
/* Session/CLI owner; algorithms include rf_ai.h only. Zero initialize owners. */
struct rf_ai_host {
    struct rf_ai_instance instance;
    struct rf_ai_snapshot snapshot;
    struct rf_ai_plan plan;
    struct rf_ai_stats stats;
    struct rf_ai_point previous_positions[RF_AI_MAX_MEMBERS];
    int blocked_ms[RF_AI_MAX_MEMBERS], last_time_ms;
    unsigned generation;
    double snapshot_seconds;
};
void rf_ai_host_destroy(struct rf_ai_host *);
int rf_ai_host_decide(struct rf_ai_host *,const struct rf_tac_world *,int team,
    const struct rf_tac_policy *,struct rf_tac_plan *,struct rf_tac_observation *,struct rf_tac_decision_trace *);
#endif
