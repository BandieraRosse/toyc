#ifndef RF_AI_H
#define RF_AI_H
/* Algorithm ABI: no Game, renderer, tactical candidates or solver scores. */
#define RF_AI_API_VERSION 1
#define RF_AI_MAX_MEMBERS 6
#define RF_AI_MAX_PARAMETERS 8
#define RF_AI_STATE_BYTES 1024
#define RF_AI_NAV_NEIGHBORS 12
enum rf_ai_command { RF_AI_DEFEND, RF_AI_ATTACK };
enum rf_ai_action_kind { RF_AI_HOLD, RF_AI_MOVE, RF_AI_FIRE, RF_AI_RELOAD };
enum rf_ai_feedback { RF_AI_IDLE, RF_AI_RUNNING, RF_AI_ARRIVED, RF_AI_BLOCKED,
    RF_AI_TARGET_LOST, RF_AI_REJECTED };
enum rf_ai_query_kind { RF_AI_SHOT, RF_AI_ROUTE, RF_AI_CONNECTION };
enum rf_ai_answer { RF_AI_UNKNOWN=-1, RF_AI_NO=0, RF_AI_YES=1 };
struct rf_ai_point { float x,z; };
struct rf_ai_nav_node { struct rf_ai_point position; int neighbor_count; short neighbors[RF_AI_NAV_NEIGHBORS]; };
struct rf_ai_identity { int id; unsigned generation; };
struct rf_ai_action {
    int kind;
    struct rf_ai_identity member, target;
    struct rf_ai_point destination;
};
struct rf_ai_member {
    struct rf_ai_identity identity;
    int alive, ammo, reserve, magazine, reload_ms, cooldown_ms, aim_ms;
    int feedback, shots, hits, damage_taken;
    float health, evasion, range_m;
    struct rf_ai_point position, facing;
    struct rf_ai_action previous;
};
struct rf_ai_snapshot {
    int version, team, count, enemy_count, time_ms;
    unsigned generation, map_generation;
    int command, completed, capture_ms;
    struct rf_ai_point objective, bounds_min, bounds_max;
    float radius;
    /* Immutable navigation facts, borrowed only for this decision. No scores. */
    int navigation_count;
    const struct rf_ai_nav_node *navigation;
    struct rf_ai_member members[RF_AI_MAX_MEMBERS], enemies[RF_AI_MAX_MEMBERS];
};
struct rf_ai_plan {
    int version, team, count, time_ms;
    unsigned generation;
    struct rf_ai_action actions[RF_AI_MAX_MEMBERS];
};
/* SHOT: member -> enemy, at optional hypothetical from position. ROUTE:
 * member -> arbitrary destination. Returns a snapped reachable destination and
 * geometric path distance, never a recommended position or tactical score.
 * CONNECTION tests a straight movement segment. UNKNOWN must not mean safe. */
struct rf_ai_query {
    int kind, member, enemy, hypothetical;
    struct rf_ai_point from, destination;
};
struct rf_ai_query_result { int answer; float distance; struct rf_ai_point destination, waypoint; };
struct rf_ai_services {
    void *context;
    int (*spend)(void *,int);
    int (*remaining)(const void *);
    int (*query)(void *,const struct rf_ai_query *,struct rf_ai_query_result *);
};
struct rf_ai_parameter { const char *name; float initial, minimum, maximum; };
struct rf_ai_config { int version; float parameters[RF_AI_MAX_PARAMETERS]; };
struct rf_ai_algorithm {
    const char *name;
    int version, state_bytes, parameter_count;
    const struct rf_ai_parameter *parameters;
    void (*reset)(void *state);
    void (*decide)(void *state,const struct rf_ai_config *,const struct rf_ai_snapshot *,
                   const struct rf_ai_services *,struct rf_ai_plan *);
    void (*destroy)(void *state);
};
struct rf_ai_stats {
    int work, exhausted, queries, unknown, invalid_actions;
    double query_seconds, decision_seconds;
};
struct rf_ai_instance {
    const struct rf_ai_algorithm *algorithm;
    struct rf_ai_config config;
    unsigned generation;
    int initialized, generation_known; /* zero initialize before first init */
    union { double alignment; unsigned char bytes[RF_AI_STATE_BYTES]; } state;
};
typedef int (*rf_ai_query_backend)(void *,const struct rf_ai_query *,struct rf_ai_query_result *);
double rf_ai_clock_seconds(void); /* diagnostics only */
const struct rf_ai_algorithm *rf_ai_algorithm_find(const char *name);
int rf_ai_algorithm_count(void);
const struct rf_ai_algorithm *rf_ai_algorithm_at(int index);
void rf_ai_config_default(const struct rf_ai_algorithm *,struct rf_ai_config *);
int rf_ai_instance_init(struct rf_ai_instance *,const struct rf_ai_algorithm *,const struct rf_ai_config *);
void rf_ai_instance_destroy(struct rf_ai_instance *);
void rf_ai_plan_hold(const struct rf_ai_snapshot *,struct rf_ai_plan *);
/* All callbacks are trusted compiled code and must cooperate with spend().
 * Queries also charge the shared budget; zero budget produces explicit HOLD. */
int rf_ai_decide(struct rf_ai_instance *,const struct rf_ai_snapshot *,int budget,
    rf_ai_query_backend,void *backend,struct rf_ai_plan *,struct rf_ai_stats *);
#endif
