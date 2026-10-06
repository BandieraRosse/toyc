#include "rf_tactical_weapon.h"

/* These are gameplay profiles, not firearm lethality/bench-accuracy claims.
 * The shared contract and source/design distinction are in tactical-weapons.md. */
static const rf_tw_profile rf_tw_profiles[RF_TW_KIND_COUNT] = {
    {RF_TW_RIFLE, "rifle", 16000, 12000, 2000, 30, 100, 2200,
     35.0f, 90.0f, 790.0f, 4.0f, 1.6f, 15.0f, 10.0f, 6.0f, 65.0f},
    {RF_TW_SMG, "smg", 13000, 4000, 2000, 30, 75, 1800,
     15.0f, 60.0f, 400.0f, 7.0f, 0.8f, 18.0f, 6.0f, 2.5f, 45.0f}
};

typedef struct rf_tw_geometry {
    double spread_x;
    double spread_y;
    double half_width;
    double half_height;
    double head_half_width;
    double head_bottom;
    int body_damage_milli;
    int visible;
} rf_tw_geometry;

static int rf_tw_equal(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static float rf_tw_nonnegative(float value) {
    /* Also reject NaN: public contexts may come from untrusted strategy input. */
    return value >= 0.0f ? value : 0.0f;
}

static double rf_tw_abs(double value) { return value < 0.0 ? -value : value; }

const rf_tw_profile *rf_tw_profile_get(int kind) {
    if (kind < 0 || kind >= RF_TW_KIND_COUNT) return 0;
    return &rf_tw_profiles[kind];
}

int rf_tw_kind_from_name(const char *name) {
    int i;
    for (i = 0; i < RF_TW_KIND_COUNT; ++i)
        if (rf_tw_equal(name, rf_tw_profiles[i].name)) return i;
    return -1;
}

const char *rf_tw_exposure_name(int exposure) {
    switch (exposure) {
        case RF_TW_NONE: return "none";
        case RF_TW_HEAD: return "head";
        case RF_TW_UPPER: return "upper";
        case RF_TW_FULL: return "full";
        default: return "invalid";
    }
}

const char *rf_tw_fire_mode_name(int mode) {
    switch (mode) {
        case RF_TW_SINGLE: return "single";
        case RF_TW_BURST: return "burst";
        case RF_TW_AUTO: return "auto";
        default: return "invalid";
    }
}

void rf_tw_context_reset(rf_tw_context *context) {
    if (!context) return;
    context->distance_m = 10.0f;
    context->exposure = RF_TW_FULL;
    context->shooter_speed_mps = 0.0f;
    context->target_lateral_speed_mps = 0.0f;
    context->recoil_milli_mrad = 0;
}

static void rf_tw_metrics_clear(rf_tw_metrics *metrics) {
    if (!metrics) return;
    metrics->hit_probability = 0.0f;
    metrics->head_probability = 0.0f;
    metrics->body_damage = 0.0f;
    metrics->expected_damage = 0.0f;
    metrics->expected_risk_cost = 0.0f;
    metrics->spread_half_width_m = 0.0f;
    metrics->spread_half_height_m = 0.0f;
    metrics->mechanical_dps = 0.0f;
    metrics->expected_dps = 0.0f;
    metrics->reload_cycle_dps = 0.0f;
}

static int rf_tw_damage_milli(const rf_tw_profile *profile, double distance) {
    double damage = profile->damage_milli;
    if (distance >= profile->falloff_end_m) {
        damage = profile->minimum_damage_milli;
    } else if (distance > profile->falloff_start_m &&
               profile->falloff_end_m > profile->falloff_start_m) {
        double fraction = (distance - profile->falloff_start_m) /
                          (profile->falloff_end_m - profile->falloff_start_m);
        damage += fraction * (profile->minimum_damage_milli - damage);
    }
    return damage > 0.0 ? (int)(damage + 0.5) : 0;
}

static void rf_tw_geometry_build(const rf_tw_profile *profile,
                                 const rf_tw_context *context,
                                 rf_tw_geometry *geometry) {
    double distance = rf_tw_nonnegative(context->distance_m);
    double moving = rf_tw_nonnegative(context->shooter_speed_mps);
    double lateral = rf_tw_abs(context->target_lateral_speed_mps);
    double recoil = context->recoil_milli_mrad > 0 ?
                    context->recoil_milli_mrad * 0.001 : 0.0;
    double angular, tracking;
    if (!(lateral >= 0.0)) lateral = 0.0;
    if (recoil > profile->max_recoil_mrad) recoil = profile->max_recoil_mrad;
    /* The spread parameters denote one-axis standard deviation. A difference
     * of two uniform variates has triangular support sqrt(6) times its SD.
     * Tracking error includes imperfect lead and grows with time of flight. */
    angular = (profile->spread_mrad + recoil +
              profile->moving_spread_mrad * moving / RF_TW_BASE_MOVE_SPEED_MPS)
              * 0.001 * distance;
    tracking = lateral * (profile->tracking_error_ms * 0.001 +
               (profile->muzzle_speed_mps > 0.0f ?
                0.25 * distance / profile->muzzle_speed_mps : 0.0));
    geometry->spread_x = 2.449489742783178 * angular + tracking;
    geometry->spread_y = 2.449489742783178 * angular * 1.25;
    if (geometry->spread_x < 0.000001) geometry->spread_x = 0.000001;
    if (geometry->spread_y < 0.000001) geometry->spread_y = 0.000001;
    geometry->half_width = RF_TW_BASE_BODY_WIDTH_M * 0.5;
    geometry->half_height = RF_TW_BASE_BODY_HEIGHT_M * 0.5;
    geometry->head_half_width = 0.12;
    geometry->visible = 1;
    switch (context->exposure) {
        case RF_TW_FULL: break;
        case RF_TW_UPPER:
            geometry->half_height *= 0.5;
            break;
        case RF_TW_HEAD:
            geometry->half_width = geometry->head_half_width;
            geometry->half_height = RF_TW_BASE_BODY_HEIGHT_M * 0.08;
            break;
        default: geometry->visible = 0; break;
    }
    geometry->head_bottom = geometry->half_height -
                            RF_TW_BASE_BODY_HEIGHT_M * 0.16;
    geometry->body_damage_milli = rf_tw_damage_milli(profile, distance);
}

/* Exact CDF of U(-support/2,support/2) + U(-support/2,support/2). */
static double rf_tw_triangle_cdf(double value, double support) {
    double t;
    if (value <= -support) return 0.0;
    if (value >= support) return 1.0;
    if (value < 0.0) {
        t = (value + support) / support;
        return 0.5 * t * t;
    }
    t = (support - value) / support;
    return 1.0 - 0.5 * t * t;
}

static double rf_tw_triangle_interval(double low, double high, double support) {
    double probability;
    if (low >= high) return 0.0;
    probability = rf_tw_triangle_cdf(high, support) -
                  rf_tw_triangle_cdf(low, support);
    return probability > 0.0 ? probability : 0.0;
}

void rf_tw_query(const rf_tw_profile *profile, const rf_tw_context *context,
                 rf_tw_metrics *metrics) {
    rf_tw_geometry geometry;
    double head, body, y_head, y_body, x_head, x_body, cycle_seconds;
    rf_tw_metrics_clear(metrics);
    if (!profile || !context || !metrics || profile->shot_interval_ms <= 0)
        return;
    rf_tw_geometry_build(profile, context, &geometry);
    metrics->body_damage = geometry.body_damage_milli * 0.001f;
    metrics->spread_half_width_m = (float)geometry.spread_x;
    metrics->spread_half_height_m = (float)geometry.spread_y;
    metrics->mechanical_dps = (float)geometry.body_damage_milli /
                              profile->shot_interval_ms;
    if (!geometry.visible) return;
    x_body = rf_tw_triangle_interval(-geometry.half_width, geometry.half_width,
                                     geometry.spread_x);
    x_head = rf_tw_triangle_interval(-geometry.head_half_width,
                                     geometry.head_half_width, geometry.spread_x);
    y_head = rf_tw_triangle_interval(geometry.head_bottom, geometry.half_height,
                                     geometry.spread_y);
    y_body = rf_tw_triangle_interval(-geometry.half_height, geometry.head_bottom,
                                     geometry.spread_y);
    /* Head silhouette is narrower than torso. The two disjoint rectangles
     * also let actual hit location choose damage, rather than a head roll. */
    head = x_head * y_head;
    body = x_body * y_body;
    metrics->hit_probability = (float)(head + body);
    metrics->head_probability = (float)head;
    metrics->expected_damage = (float)((body + head *
        profile->head_multiplier_milli * 0.001) * metrics->body_damage);
    metrics->expected_risk_cost = metrics->hit_probability * metrics->body_damage;
    metrics->expected_dps = metrics->expected_damage * 1000.0f /
                            profile->shot_interval_ms;
    cycle_seconds = ((profile->magazine - 1) * profile->shot_interval_ms +
        (profile->reload_ms > profile->shot_interval_ms ?
         profile->reload_ms : profile->shot_interval_ms)) * 0.001;
    if (cycle_seconds > 0.0)
        metrics->reload_cycle_dps = (float)(metrics->expected_damage *
                                           profile->magazine / cycle_seconds);
}

unsigned int rf_tw_rng_next(unsigned int *rng) {
    unsigned int x;
    if (!rng) return 0;
    x = *rng;
    if (!x) x = 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *rng = x;
    return x;
}

static double rf_tw_uniform(unsigned int *rng) {
    return ((rf_tw_rng_next(rng) >> 8) + 0.5) / 16777216.0;
}

void rf_tw_sample_shot(const rf_tw_profile *profile,
                       const rf_tw_context *context, unsigned int *rng,
                       rf_tw_shot *shot) {
    rf_tw_geometry geometry;
    double x, y, first, second;
    if (!shot) return;
    shot->hit = 0;
    shot->head = 0;
    shot->damage_milli = 0;
    shot->risk_cost_milli = 0;
    shot->offset_x_m = 0.0f;
    shot->offset_y_m = 0.0f;
    if (!profile || !context || !rng) return;
    rf_tw_geometry_build(profile, context, &geometry);
    /* Ordered draws avoid unspecified expression-evaluation order in C. */
    first = rf_tw_uniform(rng);
    second = rf_tw_uniform(rng);
    x = (first + second - 1.0) * geometry.spread_x;
    first = rf_tw_uniform(rng);
    second = rf_tw_uniform(rng);
    y = (first + second - 1.0) * geometry.spread_y;
    shot->offset_x_m = (float)x;
    shot->offset_y_m = (float)y;
    if (!geometry.visible || rf_tw_abs(y) > geometry.half_height) return;
    shot->head = y >= geometry.head_bottom;
    if (rf_tw_abs(x) > (shot->head ? geometry.head_half_width : geometry.half_width)) {
        shot->head = 0;
        return;
    }
    shot->hit = 1;
    shot->risk_cost_milli = geometry.body_damage_milli;
    shot->damage_milli = shot->head ?
        (geometry.body_damage_milli * profile->head_multiplier_milli + 500) / 1000 :
        geometry.body_damage_milli;
}

void rf_tw_state_reset(const rf_tw_profile *profile, rf_tw_state *state) {
    if (!state) return;
    state->ammo = profile ? profile->magazine : 0;
    state->cooldown_ms = 0;
    state->reload_remaining_ms = 0;
    state->recoil_milli_mrad = 0;
    state->recoil_recovery_remainder = 0;
}

void rf_tw_state_advance(const rf_tw_profile *profile, rf_tw_state *state,
                         int elapsed_ms) {
    int remaining, rate;
    long long recovery;
    if (!profile || !state || elapsed_ms <= 0) return;
    state->cooldown_ms = state->cooldown_ms > elapsed_ms ?
                         state->cooldown_ms - elapsed_ms : 0;
    remaining = state->reload_remaining_ms;
    if (remaining > 0) {
        state->reload_remaining_ms = remaining > elapsed_ms ? remaining - elapsed_ms : 0;
        if (!state->reload_remaining_ms) state->ammo = profile->magazine;
    }
    rate = (int)(profile->recoil_recovery_mrad_s * 1000.0f + 0.5f);
    recovery = (long long)rate * elapsed_ms + state->recoil_recovery_remainder;
    if (recovery / 1000 >= state->recoil_milli_mrad) {
        state->recoil_milli_mrad = 0;
        state->recoil_recovery_remainder = 0;
    } else {
        state->recoil_milli_mrad -= (int)(recovery / 1000);
        state->recoil_recovery_remainder = (int)(recovery % 1000);
    }
}

int rf_tw_state_begin_shot(const rf_tw_profile *profile, rf_tw_state *state) {
    int maximum;
    if (!profile || !state || profile->shot_interval_ms <= 0 ||
        state->ammo <= 0 || state->cooldown_ms > 0 || state->reload_remaining_ms > 0)
        return 0;
    --state->ammo;
    state->cooldown_ms = profile->shot_interval_ms;
    maximum = (int)(profile->max_recoil_mrad * 1000.0f + 0.5f);
    state->recoil_milli_mrad += (int)(profile->recoil_mrad_per_shot * 1000.0f + 0.5f);
    if (state->recoil_milli_mrad > maximum) state->recoil_milli_mrad = maximum;
    return 1;
}

int rf_tw_state_begin_reload(const rf_tw_profile *profile, rf_tw_state *state) {
    if (!profile || !state || profile->reload_ms <= 0 ||
        state->ammo >= profile->magazine || state->reload_remaining_ms > 0)
        return 0;
    state->reload_remaining_ms = profile->reload_ms;
    return 1;
}

float rf_tw_apply_damage(float *hp, float *risk, float damage, float risk_cost) {
    float absorbed = 0.0f, fraction = 1.0f;
    if (!hp || !risk || !(damage > 0.0f)) return 0.0f;
    if (!(*hp > 0.0f)) { *hp = 0.0f; return 0.0f; }
    if (!(*risk >= 0.0f)) *risk = 0.0f;
    if (risk_cost > 0.0f) {
        absorbed = *risk < risk_cost ? *risk : risk_cost;
        *risk -= absorbed;
        fraction -= absorbed / risk_cost;
    }
    *hp -= damage * fraction;
    if (*hp < 0.0f) *hp = 0.0f;
    return absorbed;
}

void rf_tw_recover(float *risk, int *recovery_ms, int elapsed_ms) {
    int delay;
    if (!risk || !recovery_ms || elapsed_ms <= 0) return;
    delay = *recovery_ms > 0 ? *recovery_ms : 0;
    if (delay >= elapsed_ms) {
        *recovery_ms = delay - elapsed_ms;
        return;
    }
    *recovery_ms = 0;
    if (!(*risk >= 0.0f)) *risk = 0.0f;
    *risk += RF_TW_BASE_RISK_REGEN * (elapsed_ms - delay) * 0.001f;
    if (*risk > RF_TW_BASE_RISK) *risk = RF_TW_BASE_RISK;
}

int rf_tw_pattern_delay_ms(const rf_tw_profile *profile, int mode,
                            int shots_fired) {
    int delay;
    if (!profile) return 0;
    delay = profile->shot_interval_ms;
    if (mode == RF_TW_SINGLE && delay < 400) delay = 400;
    if (mode == RF_TW_BURST && shots_fired > 0 && shots_fired % 3 == 0 && delay < 350)
        delay = 350;
    return delay;
}

void rf_tw_pattern_metrics(const rf_tw_profile *profile,
                           const rf_tw_context *context, int mode,
                           rf_tw_metrics *metrics) {
    rf_tw_state state;
    rf_tw_context current;
    rf_tw_metrics shot_metrics;
    double hits = 0.0, heads = 0.0, damage = 0.0, cost = 0.0;
    double width = 0.0, height = 0.0;
    int i, last_shot_ms = 0, delay;
    rf_tw_metrics_clear(metrics);
    if (!profile || !context || !metrics || profile->magazine <= 0 ||
        profile->shot_interval_ms <= 0) return;
    rf_tw_state_reset(profile, &state);
    current = *context;
    state.recoil_milli_mrad = context->recoil_milli_mrad;
    for (i = 0; i < profile->magazine; ++i) {
        current.recoil_milli_mrad = state.recoil_milli_mrad;
        rf_tw_query(profile, &current, &shot_metrics);
        hits += shot_metrics.hit_probability;
        heads += shot_metrics.head_probability;
        damage += shot_metrics.expected_damage;
        cost += shot_metrics.expected_risk_cost;
        width += shot_metrics.spread_half_width_m;
        height += shot_metrics.spread_half_height_m;
        rf_tw_state_begin_shot(profile, &state);
        if (i + 1 < profile->magazine) {
            delay = rf_tw_pattern_delay_ms(profile, mode, i + 1);
            rf_tw_state_advance(profile, &state, delay);
            last_shot_ms += delay;
        }
    }
    metrics->hit_probability = (float)(hits / profile->magazine);
    metrics->head_probability = (float)(heads / profile->magazine);
    metrics->body_damage = shot_metrics.body_damage;
    metrics->expected_damage = (float)(damage / profile->magazine);
    metrics->expected_risk_cost = (float)(cost / profile->magazine);
    metrics->spread_half_width_m = (float)(width / profile->magazine);
    metrics->spread_half_height_m = (float)(height / profile->magazine);
    metrics->mechanical_dps = shot_metrics.mechanical_dps;
    delay = rf_tw_pattern_delay_ms(profile, mode, profile->magazine);
    metrics->expected_dps = (float)(damage * 1000.0 / (last_shot_ms + delay));
    delay = profile->reload_ms > delay ? profile->reload_ms : delay;
    metrics->reload_cycle_dps = (float)(damage * 1000.0 / (last_shot_ms + delay));
}
