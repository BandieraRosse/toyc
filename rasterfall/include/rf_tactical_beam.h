#ifndef RF_TACTICAL_BEAM_H
#define RF_TACTICAL_BEAM_H

#include "rf_tactical.h"
#include "rf_tactical_prediction.h"

/* Beam searches root joint actions using observations and a restricted engine
 * service. It never owns or receives the authoritative world or a map pointer.
 * Legacy policies use the same entry point without consulting the predictor. */
void rf_tac_solve_with_predictor(const struct rf_tac_observation *observation,
                                const struct rf_tac_policy *policy,
                                const struct rf_tac_predictor *predictor,
                                struct rf_tac_plan *plan,
                                struct rf_tac_decision_trace *trace);

#endif
