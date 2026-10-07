#ifndef RF_TACTICAL_WEAPON_H
#define RF_TACTICAL_WEAPON_H

/* Shared range/arena combat model. Distances are metres, time milliseconds,
 * damage is 1/1000 HP. No renderer, actor, map or process-global RNG state. */
#define RF_TW_BASE_HP 100.0f
#define RF_TW_BASE_RISK 45.0f
#define RF_TW_BASE_RISK_REGEN 10.0f
#define RF_TW_BASE_RECOVERY_DELAY_MS 3000
#define RF_TW_BASE_BODY_WIDTH_M 0.50f
#define RF_TW_BASE_BODY_HEIGHT_M 1.70f
#define RF_TW_BASE_MOVE_SPEED_MPS 3.0f

enum rf_tw_kind { RF_TW_RIFLE, RF_TW_SMG, RF_TW_KIND_COUNT };
enum rf_tw_exposure { RF_TW_NONE, RF_TW_HEAD, RF_TW_UPPER, RF_TW_FULL };
enum rf_tw_fire_mode { RF_TW_SINGLE, RF_TW_BURST, RF_TW_AUTO };

typedef struct rf_tw_profile {
    int kind;
    const char *name;
    int damage_milli;
    int minimum_damage_milli;
    int head_multiplier_milli;
    int magazine;
    int shot_interval_ms;
    int reload_ms;
    float falloff_start_m;
    float falloff_end_m;
    float muzzle_speed_mps;
    float spread_mrad;
    float recoil_mrad_per_shot;
    float max_recoil_mrad;
    float recoil_recovery_mrad_s;
    float moving_spread_mrad;
    float tracking_error_ms;
} rf_tw_profile;

typedef struct rf_tw_context {
    float distance_m;
    int exposure;
    float shooter_speed_mps;
    float target_lateral_speed_mps;
    int recoil_milli_mrad;
} rf_tw_context;

typedef struct rf_tw_metrics {
    float hit_probability;
    float head_probability; /* Unconditional probability, part of hit. */
    float body_damage;
    float expected_damage;
    float expected_risk_cost; /* Head multiplier never increases risk cost. */
    float spread_half_width_m;
    float spread_half_height_m;
    float mechanical_dps;
    float expected_dps;
    float reload_cycle_dps;
} rf_tw_metrics;

typedef struct rf_tw_shot {
    int hit;
    int head;
    int damage_milli;
    int risk_cost_milli;
    float offset_x_m;
    float offset_y_m;
} rf_tw_shot;

typedef struct rf_tw_state {
    int ammo;
    int cooldown_ms;
    int reload_remaining_ms;
    int recoil_milli_mrad;
    int recoil_recovery_remainder;
} rf_tw_state;

const rf_tw_profile *rf_tw_profile_get(int kind);
int rf_tw_kind_from_name(const char *name);
const char *rf_tw_exposure_name(int exposure);
const char *rf_tw_fire_mode_name(int mode);
void rf_tw_context_reset(rf_tw_context *context);

/* Both functions use the same visible rectangle and triangular 2-D spread.
 * query describes the current recoil; sample consumes four draws per shot.
 * Geometry/LOS must choose exposure before calling, not after damage. */
void rf_tw_query(const rf_tw_profile *profile, const rf_tw_context *context,
                 rf_tw_metrics *metrics);
void rf_tw_sample_shot(const rf_tw_profile *profile,
                       const rf_tw_context *context, unsigned int *rng,
                       rf_tw_shot *shot);
void rf_tw_sample_aim(const rf_tw_profile *profile, const rf_tw_context *context,
    unsigned int *rng, float aim_x, float aim_y, rf_tw_shot *shot);
unsigned int rf_tw_rng_next(unsigned int *rng);

/* Finite magazines, unlimited reserve. begin_shot succeeds only when legal.
 * Query/sample the pre-shot recoil, then begin_shot to consume ammo and heat.
 * Rejected fire/reload requests do not mutate state. Reload overlaps cooldown. */
void rf_tw_state_reset(const rf_tw_profile *profile, rf_tw_state *state);
void rf_tw_state_advance(const rf_tw_profile *profile, rf_tw_state *state,
                         int elapsed_ms);
int rf_tw_state_begin_shot(const rf_tw_profile *profile, rf_tw_state *state);
int rf_tw_state_begin_reload(const rf_tw_profile *profile, rf_tw_state *state);

/* Common standard-fighter resource settlement for range and arena. Returns
 * risk absorbed; HP loses the uncovered fraction of the location damage.
 * On an effective hit the caller resets its recovery_ms to the baseline delay.
 * recover consumes that delay and applies only the remaining elapsed time.
 * Call recovery only for living fighters; no automatic HP regeneration. */
float rf_tw_apply_damage(float *hp, float *risk, float damage, float risk_cost);
void rf_tw_recover(float *risk, int *recovery_ms, int elapsed_ms);

/* Fire-mode policy is separate from mechanical weapon state. shots_fired is
 * counted from one; burst uses a 350ms pause after every three shots, single
 * uses 400ms between shots. Delay is never below the mechanical interval.
 * pattern_metrics averages one initially cold magazine, including recoil and
 * mode pauses; reload_cycle_dps additionally includes the final reload. */
int rf_tw_pattern_delay_ms(const rf_tw_profile *profile, int mode,
                            int shots_fired);
void rf_tw_pattern_metrics(const rf_tw_profile *profile,
                           const rf_tw_context *context, int mode,
                           rf_tw_metrics *metrics);

#endif
