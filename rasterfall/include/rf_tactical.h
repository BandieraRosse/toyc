#ifndef RF_TACTICAL_H
#define RF_TACTICAL_H
#include "toy_game.h"

/* Game owns actor simulation; observations are disposable read-only
 * projections. Solvers receive no world pointer and do not own navigation. */
#define RF_TAC_VERSION 2
#define RF_TAC_MAX_SQUAD 6
#define RF_TAC_MAX_UNITS 12
#define RF_TAC_MAX_NODES 1024
#define RF_TAC_MAX_COVERS 40
#define RF_TAC_CANDIDATES 8
#define RF_TAC_DT_MS 16
#define RF_TAC_THINK_MS 200
#define RF_TAC_GRID_W 32
#define RF_TAC_GRID_H 24
#define RF_TAC_GRID_M 2.0f
#define RF_TAC_MAX_PATH 128
#define RF_TAC_NEIGHBORS 12
#define RF_TAC_BEAM_MAX_WIDTH 8
#define RF_TAC_BEAM_MAX_BRANCHES 8
#define RF_TAC_BEAM_MAX_LAYERS (RF_TAC_MAX_SQUAD + 1)

enum rf_tac_order_kind { RF_TAC_DEFEND, RF_TAC_ATTACK };
enum rf_tac_action_kind { RF_TAC_HOLD, RF_TAC_MOVE, RF_TAC_FIRE, RF_TAC_RELOAD };
enum rf_tac_cover_kind { RF_TAC_LOW = 1, RF_TAC_HIGH = 2 };
enum rf_tac_solver_kind { RF_TAC_SIMPLE = 1, RF_TAC_MECHANICAL = 2, RF_TAC_UTILITY = 3, RF_TAC_BEAM = 4 };
enum rf_tac_candidate_kind {
    RF_TAC_CURRENT, RF_TAC_ADVANCE, RF_TAC_FLANK_LEFT, RF_TAC_FLANK_RIGHT,
    RF_TAC_COVER_LEFT, RF_TAC_COVER_RIGHT, RF_TAC_RETREAT, RF_TAC_CONTINUE
};

struct rf_tac_vec { float x, y; };
struct rf_tac_cover {
    float x0, y0, x1, y1;
    int height;
};
struct rf_tac_node {
    struct rf_tac_vec pos;
    int cover_index;
    struct rf_tac_vec cover_normal;
    int height, peek_left, peek_right, fire_over;
};
struct rf_tac_map {
    /* Game geometry owns collision queries; nodes are disposable advice. */
    struct toy_game *geometry;
    struct toy_map_primitive primitives[TOY_GAME_MAX_PRIMITIVES];
    int primitive_count;
    unsigned int seed, generation, content_hash;
    int node_count, cover_count;
    struct rf_tac_vec objective, spawn[2];
    float objective_radius;
    struct rf_tac_cover covers[RF_TAC_MAX_COVERS];
    struct rf_tac_node nodes[RF_TAC_MAX_NODES];
    int grid_node[RF_TAC_GRID_W * RF_TAC_GRID_H];
    unsigned char neighbor_count[RF_TAC_MAX_NODES];
    short neighbors[RF_TAC_MAX_NODES][RF_TAC_NEIGHBORS];
    unsigned char exposure[RF_TAC_MAX_NODES][RF_TAC_MAX_NODES];
    unsigned int visible[RF_TAC_MAX_NODES][(RF_TAC_MAX_NODES + 31) / 32];
};
struct rf_tac_order {
    int kind;
    struct rf_tac_vec target;
    float radius;
    int captured, clear_ms;
};
struct rf_tac_action { int kind, candidate, target; };
struct rf_tac_plan {
    int version;
    unsigned int generation;
    int tick, time_ms, team, count;
    struct rf_tac_action actions[RF_TAC_MAX_SQUAD];
    struct rf_tac_vec destinations[RF_TAC_MAX_SQUAD];
    int evaluations, budget_exhausted;
    int prediction_calls, prediction_steps;
};
/* Actor reference, read-only physical projection and policy/advisory state.
 * Strategy-facing health is only effective_health. */
struct rf_tac_unit {
    int actor_id;
    unsigned int actor_generation;
    int id, team, alive, weapon;
    struct rf_tac_vec pos;
    float hp, evasion;
    int recovery_ms, ammo, cooldown_ms, reload_ms;
    int recoil_milli_mrad, recoil_recovery_remainder;
    struct rf_tac_action action;
    struct rf_tac_vec destination;
    int shots, hits, last_shot_target;
    float damage, absorbed;
    short nav_nodes[RF_TAC_MAX_PATH];
    int nav_count, nav_cursor, nav_kind;
};
struct rf_tac_world {
    struct toy_game *game;
    int owns_game;
    const struct rf_tac_map *map;
    unsigned int rng, seed;
    int squad_size, time_ms, tick, winner, finished, max_time_ms;
    struct rf_tac_order orders[2];
    struct rf_tac_unit units[RF_TAC_MAX_UNITS];
    int invalid_actions, captures;
    unsigned int order_revision[2];
};
struct rf_tac_unit_view {
    int id, alive, weapon, ammo, reload_ms, cooldown_ms;
    struct rf_tac_vec pos;
    float effective_health, max_effective_health;
};
struct rf_tac_relation {
    float distance, exposure, hit_rate, expected_dps;
    int visible;
};
struct rf_tac_path_summary {
    float move_distance, travel_time, exposed_time, incoming_damage;
};
struct rf_tac_candidate {
    int node, kind;
    struct rf_tac_vec pos, cover_normal;
    int height, peek_left, peek_right, fire_over;
    unsigned int visible_enemy_mask, exposed_to_enemy_mask;
    float cover_quality, outgoing_dps, incoming_dps, objective_distance;
    float objective_path_distance;
    struct rf_tac_path_summary path;
    struct rf_tac_relation relations[RF_TAC_MAX_SQUAD];
};
struct rf_tac_observation {
    unsigned int generation;
    int tick, time_ms, team, count;
    struct rf_tac_order order;
    struct rf_tac_unit_view friendly[RF_TAC_MAX_SQUAD], enemy[RF_TAC_MAX_SQUAD];
    struct rf_tac_relation relations[RF_TAC_MAX_SQUAD][RF_TAC_MAX_SQUAD];
    int candidate_count[RF_TAC_MAX_SQUAD];
    struct rf_tac_candidate candidates[RF_TAC_MAX_SQUAD][RF_TAC_CANDIDATES];
};
/* Values belong to the solver. Map/observation deliberately have no score. */
struct rf_tac_policy {
    int version, solver, budget;
    float aggression, safety, progress, cover, focus, movement;
    int beam_width, beam_branches, beam_horizon_ms;
};
/* Solver-owned values, not engine facts. A layer expands one squad member;
 * each prefix is a complete root plan with HOLD for unassigned members. */
struct rf_tac_search_score {
    float health, progress, firepower, risk, cohesion, terminal, total;
};
struct rf_tac_beam_entry {
    int parent_rank, changed_unit, forecasted, forecast_ms, uncertain_shots;
    struct rf_tac_action actions[RF_TAC_MAX_SQUAD];
    struct rf_tac_vec destinations[RF_TAC_MAX_SQUAD];
    struct rf_tac_search_score score;
    float predicted_health[2];
};
struct rf_tac_beam_layer {
    int unit, expanded, retained;
    struct rf_tac_beam_entry entries[RF_TAC_BEAM_MAX_WIDTH];
};
struct rf_tac_decision_trace {
    float candidate_scores[RF_TAC_MAX_SQUAD][RF_TAC_CANDIDATES];
    float selected_scores[RF_TAC_MAX_SQUAD];
    int selected[RF_TAC_MAX_SQUAD], targets[RF_TAC_MAX_SQUAD];
    int beam_layer_count, prediction_calls, prediction_steps, prediction_unavailable;
    int beam_selected_rank, beam_hold_uncertain_shots;
    struct rf_tac_search_score beam_hold_score, beam_selected_score;
    struct rf_tac_beam_layer beam_layers[RF_TAC_BEAM_MAX_LAYERS];
};

int rf_tac_map_generate(struct rf_tac_map *map, unsigned int seed);
int rf_tac_nearest_node(const struct rf_tac_map *map, struct rf_tac_vec pos);
float rf_tac_exposure(const struct rf_tac_map *map, struct rf_tac_vec from, struct rf_tac_vec to);
int rf_tac_world_init(struct rf_tac_world *world, const struct rf_tac_map *map,
                      unsigned int seed, int squad_size, int weapon, int max_time_ms);
int rf_tac_command(struct rf_tac_world *world, int team, int kind,
                   struct rf_tac_vec target, float radius);
void rf_tac_observe(const struct rf_tac_world *world, int team, struct rf_tac_observation *out);
void rf_tac_plan_hold(const struct rf_tac_observation *obs, struct rf_tac_plan *out);
int rf_tac_apply(struct rf_tac_world *world, const struct rf_tac_plan *plan);
void rf_tac_step(struct rf_tac_world *world);
void rf_tac_world_destroy(struct rf_tac_world *world);
int rf_tac_world_clone(struct rf_tac_world *out, const struct rf_tac_world *source);
int rf_tac_world_bind(struct rf_tac_world *world, struct toy_game *game);
void rf_tac_prepare(struct rf_tac_world *world, int paused);
void rf_tac_finish(struct rf_tac_world *world, int dt_ms);
void rf_tac_sync(struct rf_tac_world *world);
void rf_tac_map_destroy(struct rf_tac_map *map);
int rf_tac_map_bind(struct rf_tac_map *map, const struct toy_game *game);
unsigned int rf_tac_hash(const struct rf_tac_world *world);
void rf_tac_policy_default(struct rf_tac_policy *policy, int solver);
int rf_tac_policy_validate(const struct rf_tac_policy *policy);
int rf_tac_policy_load(const char *path, struct rf_tac_policy *policy);
int rf_tac_policy_save(const char *path, const struct rf_tac_policy *policy);
void rf_tac_solve(const struct rf_tac_observation *obs, const struct rf_tac_policy *policy,
                  struct rf_tac_plan *out, struct rf_tac_decision_trace *trace);

#endif
