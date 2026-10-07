/* Headless lab: all simulation and weapon evaluation remain in native C. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "rf_tactical.h"
#include "rf_tactical_lab.h"
#include "rasterfall_units.h"
#include "rf_tactical_beam.h"
#include "rf_tactical_prediction.h"
#include "rf_tactical_weapon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

struct lab_options {
    unsigned int map_seed, shot_seed;
    int squad, weapon, duration_ms, pairs, budget, trace_tick, samples, order[2];
    int beam_width, beam_branches, beam_horizon_ms;
    const char *a, *b, *log;
};
struct lab_solver_cost {
    unsigned long long calls, work_units, prediction_calls, prediction_steps;
    unsigned long long exhausted_calls, unavailable_calls;
    double preparation_seconds, elapsed_seconds, max_elapsed_seconds;
};
struct lab_result {
    int winner, time_ms, ticks, alive[2], shots[2], hits[2], captured;
    float health[2], damage[2];
    unsigned int hash;
    struct lab_solver_cost costs[2];
};

/* Timing is diagnostic only and never changes simulation/search branches.
 * Microsoft CRT clock() measures elapsed time; actual process CPU comes from
 * GetProcessTimes. QPC/clock_gettime measure monotonic solver elapsed time. */
static double lab_wall_seconds(void) {
#ifdef _WIN32
    LARGE_INTEGER frequency, count;
    if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&count)) return 0;
    return (double)count.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value)) return 0;
    return value.tv_sec + value.tv_nsec / 1000000000.0;
#endif
}
static double lab_cpu_seconds(void) {
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    ULARGE_INTEGER k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return -1;
    k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
    return (double)(k.QuadPart + u.QuadPart) / 10000000.0;
#else
    clock_t value = clock();
    return value == (clock_t)-1 ? -1 : (double)value / CLOCKS_PER_SEC;
#endif
}
static void lab_cost_add(struct lab_solver_cost *sum, const struct lab_solver_cost *cost) {
    sum->calls += cost->calls; sum->work_units += cost->work_units;
    sum->prediction_calls += cost->prediction_calls; sum->prediction_steps += cost->prediction_steps;
    sum->exhausted_calls += cost->exhausted_calls; sum->unavailable_calls += cost->unavailable_calls;
    sum->preparation_seconds += cost->preparation_seconds; sum->elapsed_seconds += cost->elapsed_seconds;
    if (cost->max_elapsed_seconds > sum->max_elapsed_seconds) sum->max_elapsed_seconds = cost->max_elapsed_seconds;
}
static void lab_cost_json(FILE *f, const struct lab_solver_cost *cost) {
    fprintf(f, "{\"solver_calls\":%llu,\"work_units\":%llu,\"non_prediction_work_units\":%llu,"
            "\"prediction_calls\":%llu,\"prediction_steps\":%llu,\"predicted_ms\":%llu,"
            "\"budget_exhausted_calls\":%llu,\"prediction_unavailable_calls\":%llu,"
            "\"preparation_elapsed_seconds\":%.9g,\"solver_elapsed_seconds\":%.9g,"
            "\"mean_solver_ms\":%.9g,\"max_solver_ms\":%.9g}", cost->calls, cost->work_units,
            cost->work_units - cost->prediction_steps, cost->prediction_calls, cost->prediction_steps,
            cost->prediction_steps * RF_TAC_DT_MS, cost->exhausted_calls, cost->unavailable_calls,
            cost->preparation_seconds, cost->elapsed_seconds,
            cost->calls ? cost->elapsed_seconds * 1000 / cost->calls : 0, cost->max_elapsed_seconds * 1000);
}

static void lab_help(void) {
    puts("Rasterfall tactical lab v2 (native deterministic simulation)\n"
         "  rf-tactical range [--samples 2000] [--shot-seed 1337] [--weapon rifle|smg|both]\n"
         "  rf-tactical match [--a simple|mechanical|utility|beam|FILE.cfg] [--b POLICY]\n"
         "                    [--map-seed 100] [--shot-seed 1337] [--squad 4..6]\n"
         "                    [--weapon rifle|smg] [--duration-ms 60000] [--budget 128]\n"
         "                    [--log FILE.jsonl] [--trace-tick TICK]\n"
         "                    [--order-a attack|defend] [--order-b attack|defend]\n"
         "                    [--beam-width 1..8] [--beam-branches 2..8]\n"
         "                    [--beam-horizon-ms 200..4000 (multiple of 200)]\n"
         "  rf-tactical batch [same options] [--pairs 8] [--weapon rifle|smg|both]\n"
         "  rf-tactical inspect --log FILE.jsonl --trace-tick TICK\n"
         "  rf-tactical self-test\n"
         "Match team 0 attacks; team 1 defends. Attack clears and captures, then defends.\n"
         "Batch swaps A/B attack and defense on every map/shot seed and mirrored weapon.\n"
         "Range covers 5/10/15/20/30/40/60/80/100 m, full/upper/head/moving targets,\n"
         "single/burst/auto fire. stdout is JSON; full native replay is optional JSONL.\n"
         "Replay uses Game 16 ms steps; decisions cross 200 ms boundaries.\n"
         "Beam layers expand squad members, with unassigned members on HOLD.\n"
         "Prediction assumes enemy HOLD. Budget charges one per cheap evaluation,\n"
         "one per simulated 16 ms step, and one per terminal score. Elapsed/CPU\n"
         "diagnostics are reported separately and never determine search branches.\n"
         "Beam overrides apply to both beam policies; omitted values come from policy defaults.");
}

static int lab_integer(const char *s, int min, int max, int *out) {
    char *end;
    long n;
    errno = 0;
    n = strtol(s, &end, 10);
    if (errno || !*s || *end || n < min || n > max) return 0;
    *out = (int)n;
    return 1;
}
static int lab_seed(const char *s, unsigned int *out) {
    char *end;
    unsigned long n;
    errno = 0;
    if (!*s || *s == '-') return 0;
    n = strtoul(s, &end, 10);
    if (errno || *end || n > UINT_MAX) return 0;
    *out = (unsigned int)n;
    return 1;
}
static int lab_options(int argc, char **argv, struct lab_options *o) {
    int i;
    o->map_seed = 100; o->shot_seed = 1337;
    o->squad = 4; o->weapon = !strcmp(argv[1], "range") ? 2 : 0;
    o->duration_ms = 60000; o->pairs = 8;
    o->budget = 128; o->trace_tick = -1; o->samples = 2000;
    o->a = "utility"; o->b = "mechanical"; o->log = NULL;
    o->order[0] = RF_TAC_ATTACK; o->order[1] = RF_TAC_DEFEND;
    o->beam_width = o->beam_branches = o->beam_horizon_ms = 0;
    for (i = 2; i < argc; ++i) {
        const char *key = argv[i], *value;
        if (i + 1 == argc) { fprintf(stderr, "Missing value: %s\n", key); return 0; }
        value = argv[++i];
        if (!strcmp(key, "--a")) o->a = value;
        else if (!strcmp(key, "--b")) o->b = value;
        else if (!strcmp(key, "--log")) o->log = value;
        else if (!strcmp(key, "--order-a") || !strcmp(key, "--order-b")) {
            int team = !strcmp(key, "--order-b");
            if (!strcmp(value, "attack")) o->order[team] = RF_TAC_ATTACK;
            else if (!strcmp(value, "defend")) o->order[team] = RF_TAC_DEFEND;
            else goto invalid;
        }
        else if (!strcmp(key, "--weapon")) {
            if (!strcmp(value, "rifle")) o->weapon = 0;
            else if (!strcmp(value, "smg")) o->weapon = 1;
            else if (!strcmp(value, "both")) o->weapon = 2;
            else goto invalid;
        }
        else if (!strcmp(key, "--map-seed")) { if (!lab_seed(value, &o->map_seed)) goto invalid; }
        else if (!strcmp(key, "--shot-seed")) { if (!lab_seed(value, &o->shot_seed)) goto invalid; }
        else if (!strcmp(key, "--squad")) { if (!lab_integer(value, 4, 6, &o->squad)) goto invalid; }
        else if (!strcmp(key, "--duration-ms")) { if (!lab_integer(value, 200, 600000, &o->duration_ms)) goto invalid; }
        else if (!strcmp(key, "--pairs")) { if (!lab_integer(value, 1, 10000, &o->pairs)) goto invalid; }
        else if (!strcmp(key, "--budget")) { if (!lab_integer(value, 0, 100000, &o->budget)) goto invalid; }
        else if (!strcmp(key, "--beam-width")) { if (!lab_integer(value, 1, RF_TAC_BEAM_MAX_WIDTH, &o->beam_width)) goto invalid; }
        else if (!strcmp(key, "--beam-branches")) { if (!lab_integer(value, 2, RF_TAC_BEAM_MAX_BRANCHES, &o->beam_branches)) goto invalid; }
        else if (!strcmp(key, "--beam-horizon-ms")) {
            if (!lab_integer(value, RF_TAC_THINK_MS, RF_TAC_PREDICTION_MAX_MS, &o->beam_horizon_ms) ||
                o->beam_horizon_ms % RF_TAC_THINK_MS) goto invalid;
        }
        else if (!strcmp(key, "--trace-tick")) { if (!lab_integer(value, 0, INT_MAX, &o->trace_tick)) goto invalid; }
        else if (!strcmp(key, "--samples")) { if (!lab_integer(value, 1, 1000000, &o->samples)) goto invalid; }
        else { fprintf(stderr, "Unknown option: %s\n", key); return 0; }
        continue;
invalid:
        fprintf(stderr, "Invalid %s: %s\n", key, value); return 0;
    }
    return 1;
}
static void lab_string(FILE *f, const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    fputc('"', f);
    for (; *p; ++p) {
        if (*p == '"' || *p == '\\') { fputc('\\', f); fputc(*p, f); }
        else if (*p < 32) fprintf(f, "\\u%04x", *p);
        else fputc(*p, f);
    }
    fputc('"', f);
}
static unsigned int lab_policy_hash(const struct rf_tac_policy *p) {
    char text[256];
    const unsigned char *s;
    unsigned int h = 2166136261u;
    snprintf(text, sizeof(text), "%d,%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g",
             p->version, p->solver, p->budget, p->aggression, p->safety,
             p->progress, p->cover, p->focus, p->movement);
    if (p->solver == RF_TAC_BEAM) {
        size_t used = strlen(text);
        snprintf(text + used, sizeof(text) - used, ",%d,%d,%d", p->beam_width, p->beam_branches, p->beam_horizon_ms);
    }
    for (s = (const unsigned char *)text; *s; ++s) { h ^= *s; h *= 16777619u; }
    return h;
}
static void lab_weapon_json(FILE *f,int weapon) {
    const struct toy_game_weapon_info *p=toy_game_weapon_info(weapon==RF_TW_RIFLE?TOY_GAME_WEAPON_AK:TOY_GAME_WEAPON_SMG);
    fprintf(f,"{\"name\":\"%s\",\"kind\":%d,\"damage_milli\":%d,\"magazine\":%d,\"shot_interval_ms\":%d,\"reload_ms\":%d}",
        weapon==RF_TW_RIFLE?"rifle":"smg",weapon,p->damage*1000,p->mag_size,p->cooldown_ms,p->reload_ms);
}
static int lab_policy(const char *name, const struct lab_options *o, struct rf_tac_policy *out) {
    if (!strcmp(name, "simple")) rf_tac_policy_default(out, RF_TAC_SIMPLE);
    else if (!strcmp(name, "mechanical")) rf_tac_policy_default(out, RF_TAC_MECHANICAL);
    else if (!strcmp(name, "utility")) rf_tac_policy_default(out, RF_TAC_UTILITY);
    else if (!strcmp(name, "beam")) rf_tac_policy_default(out, RF_TAC_BEAM);
    else if (!rf_tac_policy_load(name, out)) {
        fprintf(stderr, "Cannot load policy: %s\n", name); return 0;
    }
    out->budget = o->budget;
    if (out->solver == RF_TAC_BEAM) {
        if (o->beam_width) out->beam_width = o->beam_width;
        if (o->beam_branches) out->beam_branches = o->beam_branches;
        if (o->beam_horizon_ms) out->beam_horizon_ms = o->beam_horizon_ms;
    }
    return 1;
}
static void lab_policy_json(FILE *f, const struct rf_tac_policy *p, const char *name) {
    fputs("{\"name\":", f); lab_string(f, name);
    fprintf(f, ",\"hash\":\"%08x\",\"version\":%d,\"solver\":%d,\"budget\":%d,"
               "\"aggression\":%.9g,\"safety\":%.9g,\"progress\":%.9g,\"cover\":%.9g,"
               "\"focus\":%.9g,\"movement\":%.9g", lab_policy_hash(p), p->version,
               p->solver, p->budget, p->aggression, p->safety, p->progress,
               p->cover, p->focus, p->movement);
    if (p->solver == RF_TAC_BEAM) fprintf(f, ",\"beam_width\":%d,\"beam_branches\":%d,\"beam_horizon_ms\":%d",
            p->beam_width, p->beam_branches, p->beam_horizon_ms);
    fputc('}', f);
}
static void lab_header(FILE *f, const struct rf_tac_map *map, const struct lab_options *o,
                       const struct rf_tac_policy *a, const struct rf_tac_policy *b) {
    int i;
    fprintf(f, "{\"type\":\"header\",\"schema\":1,\"simulation_version\":%d,"
               "\"map_seed\":%u,\"shot_seed\":%u,\"squad_size\":%d,\"weapon\":%d,"
               "\"duration_ms\":%d,\"dt_ms\":%d,\"think_ms\":%d,\"generation\":%u,"
               "\"width_m\":%g,\"height_m\":%g,\"node_count\":%d,"
               "\"objective\":[%.9g,%.9g],\"objective_radius\":%.9g,\"policies\":[",
            RF_TAC_VERSION, o->map_seed, o->shot_seed, o->squad, o->weapon,
            o->duration_ms, RF_TAC_DT_MS, RF_TAC_THINK_MS, map->generation,
            RF_TAC_GRID_W * RF_TAC_GRID_M, RF_TAC_GRID_H * RF_TAC_GRID_M,
            map->node_count, map->objective.x, map->objective.y, map->objective_radius);
    lab_policy_json(f, a, o->a); fputc(',', f); lab_policy_json(f, b, o->b);
    fputs("],\"covers\":[", f);
    for (i = 0; i < map->cover_count; ++i) {
        const struct rf_tac_cover *c = &map->covers[i];
        fprintf(f, "%s[%.9g,%.9g,%.9g,%.9g,%d]", i ? "," : "", c->x0, c->y0,
                c->x1, c->y1, c->height);
    }
    fprintf(f, "],\"map_content_hash\":\"%08x\",\"initial_orders\":[%d,%d],"
               "\"unit_id_stride\":%d,\"baseline\":{\"hp\":%g,\"risk\":%g,\"risk_regen_per_s\":%g,"
               "\"recovery_delay_ms\":%d,\"width_m\":%g,\"height_m\":%g,\"move_speed_mps\":%g},"
               "\"weapon_profile\":", map->content_hash, o->order[0], o->order[1],
            RF_TAC_MAX_SQUAD, RF_TW_BASE_HP, RF_TW_BASE_RISK, RF_TW_BASE_RISK_REGEN,
            RF_TW_BASE_RECOVERY_DELAY_MS, RF_TW_BASE_BODY_WIDTH_M, RF_TW_BASE_BODY_HEIGHT_M,
            RF_TW_BASE_MOVE_SPEED_MPS);
    lab_weapon_json(f, o->weapon); fputs("}\n", f);
}
static void lab_relation(FILE *f, const struct rf_tac_relation *r) {
    fprintf(f, "{\"distance\":%.9g,\"exposure\":%.9g,\"visible\":%d,"
               "\"hit_rate\":%.9g,\"expected_dps\":%.9g}",
            r->distance, r->exposure, r->visible, r->hit_rate, r->expected_dps);
}
static void lab_search_score(FILE *f, const struct rf_tac_search_score *s) {
    fprintf(f, "{\"health\":%.9g,\"progress\":%.9g,\"firepower\":%.9g,\"risk\":%.9g,"
               "\"cohesion\":%.9g,\"terminal\":%.9g,\"total\":%.9g}",
            s->health, s->progress, s->firepower, s->risk, s->cohesion, s->terminal, s->total);
}
static void lab_optional_score(FILE *f, float score) {
    if (isfinite(score) && score > -1e19f) fprintf(f, "%.9g", score);
    else fputs("null", f);
}
static void lab_joint_actions(FILE *f, const struct rf_tac_action *actions,
                              const struct rf_tac_vec *destinations, int count) {
    int i;
    fputc('[', f);
    for (i = 0; i < count; ++i) fprintf(f,
            "%s{\"unit\":%d,\"kind\":%d,\"candidate\":%d,\"target\":%d,\"destination\":[%.9g,%.9g]}",
            i ? "," : "", i, actions[i].kind, actions[i].candidate, actions[i].target,
            destinations[i].x, destinations[i].y);
    fputc(']', f);
}
static void lab_beam_trace(FILE *f, const struct rf_tac_plan *p,
                           const struct rf_tac_decision_trace *t, const struct rf_tac_policy *policy) {
    int layer, rank;
    fprintf(f, "{\"width\":%d,\"branches\":%d,\"requested_horizon_ms\":%d,"
               "\"layer_kind\":\"squad_member\",\"opponent_assumption\":\"HOLD\","
               "\"retained_entries_only\":true,\"prediction_calls\":%d,\"prediction_steps\":%d,"
               "\"predicted_ms\":%d,\"prediction_unavailable\":%d,\"selected_rank\":%d,"
               "\"stop_reason\":\"%s\",\"hold_uncertain_shots\":%d,\"hold_score\":", policy->beam_width, policy->beam_branches,
            policy->beam_horizon_ms, t->prediction_calls, t->prediction_steps,
            t->prediction_steps * RF_TAC_DT_MS, t->prediction_unavailable, t->beam_selected_rank,
            p->budget_exhausted ? "budget" : t->prediction_unavailable ? "prediction_unavailable" : "completed",
            t->beam_hold_uncertain_shots);
    if (t->prediction_calls) lab_search_score(f, &t->beam_hold_score); else fputs("null", f);
    fputs(",\"selected_score\":", f);
    if (t->prediction_calls) lab_search_score(f, &t->beam_selected_score); else fputs("null", f);
    fputs(",\"selected_joint_plan\":", f);
    lab_joint_actions(f, p->actions, p->destinations, p->count);
    fputs(",\"layers\":[", f);
    for (layer = 0; layer < t->beam_layer_count && layer < RF_TAC_BEAM_MAX_LAYERS; ++layer) {
        const struct rf_tac_beam_layer *l = &t->beam_layers[layer];
        fprintf(f, "%s{\"index\":%d,\"unit\":%d,\"expanded\":%d,\"retained\":%d,\"entries\":[",
                layer ? "," : "", layer, l->unit, l->expanded, l->retained);
        for (rank = 0; rank < l->retained && rank < RF_TAC_BEAM_MAX_WIDTH; ++rank) {
            const struct rf_tac_beam_entry *e = &l->entries[rank];
            fprintf(f, "%s{\"rank\":%d,\"parent_rank\":%d,\"changed_unit\":%d,\"forecasted\":%d,"
                       "\"forecast_ms\":%d,\"prediction_steps\":%d,\"uncertain_shots\":%d,\"score\":", rank ? "," : "", rank, e->parent_rank,
                    e->changed_unit, e->forecasted, e->forecast_ms, e->forecast_ms / RF_TAC_DT_MS, e->uncertain_shots);
            lab_search_score(f, &e->score);
            fputs(",\"predicted_health\":", f);
            if (e->forecasted) fprintf(f, "[%.9g,%.9g]", e->predicted_health[0], e->predicted_health[1]);
            else fputs("null", f);
            fputs(",\"joint_plan\":", f); lab_joint_actions(f, e->actions, e->destinations, p->count);
            fputc('}', f);
        }
        fputs("]}", f);
    }
    fputs("]}", f);
}
static void lab_decision(FILE *f, const struct rf_tac_observation *o,
                         const struct rf_tac_plan *p, const struct rf_tac_decision_trace *t,
                         const struct rf_tac_policy *policy) {
    int i, j, k;
    fprintf(f, "{\"type\":\"decision\",\"tick\":%d,\"time_ms\":%d,\"team\":%d,"
               "\"root_tactical_tick\":%d,\"evaluations\":%d,\"work_units\":%d,\"budget_limit\":%d,"
               "\"prediction_calls\":%d,\"prediction_steps\":%d,\"predicted_ms\":%d,"
               "\"budget_exhausted\":%d,\"order\":%d,\"score_scope\":\"%s\",\"units\":[",
            o->time_ms / RF_TAC_DT_MS, o->time_ms, o->team, o->tick, p->evaluations, p->evaluations,
            policy->budget, p->prediction_calls, p->prediction_steps, p->prediction_steps * RF_TAC_DT_MS,
            p->budget_exhausted, o->order.kind, policy->solver == RF_TAC_BEAM ? "joint_plan" : "individual");
    for (i = 0; i < o->count; ++i) {
        fprintf(f, "%s{\"id\":%d,\"selected\":%d,\"target\":%d,\"score\":%.9g,"
                   "\"action\":%d,\"destination\":[%.9g,%.9g],\"candidates\":[",
                i ? "," : "", o->friendly[i].id, t->selected[i], t->targets[i],
                t->selected_scores[i], p->actions[i].kind, p->destinations[i].x, p->destinations[i].y);
        for (j = 0; j < o->candidate_count[i]; ++j) {
            const struct rf_tac_candidate *c = &o->candidates[i][j];
            fprintf(f, "%s{\"index\":%d,\"kind\":%d,\"node\":%d,\"position\":[%.9g,%.9g],\"score\":",
                    j ? "," : "", j, c->kind, c->node, c->pos.x, c->pos.y);
            lab_optional_score(f, t->candidate_scores[i][j]);
            fprintf(f, ",\"score_scope\":\"%s\",\"cover_normal\":[%.9g,%.9g],\"height\":%d,"
                       "\"peek_left\":%d,\"peek_right\":%d,\"fire_over\":%d,"
                       "\"visible_enemy_mask\":%u,\"exposed_to_enemy_mask\":%u,"
                       "\"cover_quality\":%.9g,\"outgoing_dps\":%.9g,\"incoming_dps\":%.9g,"
                       "\"objective_distance\":%.9g,\"objective_path_distance\":%.9g,"
                       "\"path\":{\"move_distance\":%.9g,"
                       "\"travel_time\":%.9g,\"exposed_time\":%.9g,\"incoming_damage\":%.9g},"
                       "\"relations\":[",
                    policy->solver == RF_TAC_BEAM ? "last_joint_prefix" : "individual",
                    c->cover_normal.x, c->cover_normal.y, c->height, c->peek_left, c->peek_right,
                    c->fire_over, c->visible_enemy_mask, c->exposed_to_enemy_mask, c->cover_quality,
                    c->outgoing_dps, c->incoming_dps, c->objective_distance, c->objective_path_distance, c->path.move_distance,
                    c->path.travel_time, c->path.exposed_time, c->path.incoming_damage);
            for (k = 0; k < o->count; ++k) { if (k) fputc(',', f); lab_relation(f, &c->relations[k]); }
            fputs("]}", f);
        }
        fputs("],\"current_relations\":[", f);
        for (k = 0; k < o->count; ++k) { if (k) fputc(',', f); lab_relation(f, &o->relations[i][k]); }
        fputs("]}", f);
    }
    fprintf(f, "],\"root_plan\":{\"version\":%d,\"generation\":%u,\"tactical_tick\":%d,"
               "\"time_ms\":%d,\"team\":%d,\"count\":%d}",
            p->version, p->generation, p->tick, p->time_ms, p->team, p->count);
    if (policy->solver == RF_TAC_BEAM) { fputs(",\"beam\":", f); lab_beam_trace(f, p, t, policy); }
    fputs("}\n", f);
}
static void lab_tick(FILE *f, const struct rf_tac_world *w, const struct rf_tac_world *old) {
    int i;
    fprintf(f, "{\"type\":\"tick\",\"tick\":%d,\"time_ms\":%d,\"hash\":\"%08x\","
               "\"orders\":[%d,%d],\"captured\":%d,\"units\":[", w->time_ms / RF_TAC_DT_MS,
            w->time_ms, rf_tac_hash(w), w->orders[0].kind, w->orders[1].kind, w->orders[0].captured);
    for (i = 0; i < w->squad_size * 2; ++i) {
        int slot = (i / w->squad_size) * RF_TAC_MAX_SQUAD + i % w->squad_size;
        const struct rf_tac_unit *u = &w->units[slot];
        const struct rf_tac_unit *prev = &old->units[slot];
        struct rf_tac_vec facing = u->destination;
        int facing_target = u->shots > prev->shots ? u->last_shot_target : u->action.target;
        if (facing_target >= 0 && facing_target < w->squad_size &&
            (u->shots > prev->shots || u->action.kind == RF_TAC_FIRE))
            facing = w->units[(1 - u->team) * RF_TAC_MAX_SQUAD + facing_target].pos;
        fprintf(f, "%s{\"id\":%d,\"team\":%d,\"alive\":%d,\"position\":[%.9g,%.9g],"
                   "\"action_heading\":%.9g,\"hp\":%.9g,\"evasion\":%.9g,"
                   "\"effective_health\":%.9g,\"ammo\":%d,\"reload_ms\":%d,\"cooldown_ms\":%d,"
                   "\"recovery_ms\":%d,\"action\":%d,\"candidate\":%d,\"target\":%d,"
                   "\"destination\":[%.9g,%.9g],\"shot_target\":%d,\"shots\":%d,\"hits\":%d,\"health_damage\":%.9g,"
                   "\"absorbed\":%.9g,\"shot_delta\":%d,\"hit_delta\":%d,\"health_damage_delta\":%.9g,"
                   "\"hp_damage_delta\":%.9g,\"absorbed_delta\":%.9g}",
                i ? "," : "", u->id, u->team, u->alive, u->pos.x, u->pos.y,
                atan2(facing.y - u->pos.y, facing.x - u->pos.x), u->hp, u->evasion,
                u->hp + u->evasion, u->ammo, u->reload_ms, u->cooldown_ms, u->recovery_ms,
                u->action.kind, u->action.candidate, u->action.target, u->destination.x,
                u->destination.y, u->last_shot_target, u->shots, u->hits, u->damage, u->absorbed,
                u->shots - prev->shots, u->hits - prev->hits, u->damage - prev->damage,
                prev->hp - u->hp, u->absorbed - prev->absorbed);
    }
    fputs("]}\n", f);
}
static void lab_result_json(FILE *f, const struct lab_result *r, unsigned int map_seed,
                            unsigned int shot_seed, int weapon) {
    fprintf(f, "{\"type\":\"result\",\"simulation_version\":%d,\"map_seed\":%u,"
               "\"shot_seed\":%u,\"weapon\":%d,\"winner\":%d,\"time_ms\":%d,"
               "\"ticks\":%d,\"captured\":%d,\"alive\":[%d,%d],\"health\":[%.9g,%.9g],"
               "\"shots\":[%d,%d],\"hits\":[%d,%d],\"health_damage\":[%.9g,%.9g],"
               "\"final_hash\":\"%08x\",\"solver_costs\":[", RF_TAC_VERSION, map_seed, shot_seed, weapon,
            r->winner, r->time_ms, r->ticks, r->captured, r->alive[0], r->alive[1],
            r->health[0], r->health[1], r->shots[0], r->shots[1], r->hits[0], r->hits[1],
            r->damage[0], r->damage[1], r->hash);
    lab_cost_json(f, &r->costs[0]); fputc(',', f); lab_cost_json(f, &r->costs[1]);
    fputs("]}\n", f);
}
static int lab_match(const struct rf_tac_map *map, const struct lab_options *o,
                     const struct rf_tac_policy *a, const struct rf_tac_policy *b,
                     FILE *log, struct lab_result *result) {
    struct rf_tac_world w, old;
    struct rf_tac_observation obs[2];
    struct rf_tac_plan plan[2];
    struct rf_tac_decision_trace trace[2];
    struct rf_tac_prediction *prediction[2] = {NULL, NULL};
    struct lab_solver_cost costs[2];
    const struct rf_tac_policy *policies[2];
    int i, team, ok = 1;
    memset(costs, 0, sizeof(costs));
    policies[0] = a; policies[1] = b;
    if (!rf_tac_world_init(&w, map, o->shot_seed, o->squad, o->weapon, o->duration_ms)) return 0;
    for (team = 0; team < 2; ++team)
        if (!rf_tac_command(&w, team, o->order[team], map->objective, map->objective_radius)) return 0;
    if (log) { lab_header(log, map, o, a, b); lab_tick(log, &w, &w); }
    while (!w.finished) {
        if (w.time_ms % RF_TAC_THINK_MS < RF_TAC_DT_MS) {
            /* Both policies observe exactly the same state before either is applied. */
            for (team = 0; team < 2; ++team) {
                struct rf_tac_predictor provider;
                const struct rf_tac_predictor *service = NULL;
                int need_trace = log || o->trace_tick == w.time_ms / RF_TAC_DT_MS;
                double started = lab_wall_seconds(), prepared, elapsed;
                rf_tac_observe(&w, team, &obs[team]);
                if (policies[team]->solver == RF_TAC_BEAM) {
                    if (!prediction[team]) prediction[team] = rf_tac_prediction_create(&w, team, policies[team]->budget);
                    else if (!rf_tac_prediction_reset(prediction[team], &w, team, policies[team]->budget)) {
                        ok = 0; goto cleanup;
                    }
                    if (prediction[team]) { rf_tac_prediction_provider(prediction[team], &provider); service = &provider; }
                }
                prepared = lab_wall_seconds();
                costs[team].preparation_seconds += prepared - started;
                if (policies[team]->solver == RF_TAC_BEAM)
                    rf_tac_solve_with_predictor(&obs[team], policies[team], service, &plan[team], need_trace ? &trace[team] : NULL);
                else rf_tac_solve(&obs[team], policies[team], &plan[team], need_trace ? &trace[team] : NULL);
                elapsed = lab_wall_seconds() - prepared;
                ++costs[team].calls; costs[team].work_units += plan[team].evaluations;
                costs[team].prediction_calls += plan[team].prediction_calls;
                costs[team].prediction_steps += plan[team].prediction_steps;
                costs[team].exhausted_calls += plan[team].budget_exhausted != 0;
                costs[team].unavailable_calls += policies[team]->solver == RF_TAC_BEAM && !service;
                costs[team].elapsed_seconds += elapsed;
                if (elapsed > costs[team].max_elapsed_seconds) costs[team].max_elapsed_seconds = elapsed;
                if (log) lab_decision(log, &obs[team], &plan[team], &trace[team], policies[team]);
                if (o->trace_tick == w.time_ms / RF_TAC_DT_MS)
                    lab_decision(stdout, &obs[team], &plan[team], &trace[team], policies[team]);
            }
            for (team = 0; team < 2; ++team) if (!rf_tac_apply(&w, &plan[team])) { ok = 0; goto cleanup; }
        }
        old = w;
        rf_tac_step(&w);
        if (log) lab_tick(log, &w, &old);
        if (o->trace_tick == w.time_ms / RF_TAC_DT_MS && !log) lab_tick(stdout, &w, &old);
    }
    memset(result, 0, sizeof(*result));
    result->winner = w.winner; result->time_ms = w.time_ms; result->ticks = w.time_ms / RF_TAC_DT_MS;
    result->hash = rf_tac_hash(&w); result->captured = w.orders[0].captured;
    result->costs[0] = costs[0]; result->costs[1] = costs[1];
    for (i = 0; i < w.squad_size * 2; ++i) {
        const struct rf_tac_unit *u = &w.units[(i / w.squad_size) * RF_TAC_MAX_SQUAD + i % w.squad_size];
        result->alive[u->team] += u->alive;
        result->health[u->team] += u->hp + u->evasion;
        result->shots[u->team] += u->shots; result->hits[u->team] += u->hits;
        result->damage[u->team] += u->damage;
    }
    if (log) lab_result_json(log, result, o->map_seed, o->shot_seed, o->weapon);
cleanup:
    for (team = 0; team < 2; ++team) rf_tac_prediction_destroy(prediction[team]);
    rf_tac_world_destroy(&w);
    return ok;
}
static int lab_inspect(const struct lab_options *o) {
    FILE *f;
    char *line;
    size_t size = 131072;
    int found = 0;
    if (!o->log || o->trace_tick < 0) { fputs("inspect requires --log and --trace-tick\n", stderr); return 0; }
    f = fopen(o->log, "rb");
    line = (char *)malloc(size);
    if (!f || !line) { if (f) fclose(f); free(line); return 0; }
    while (fgets(line, (int)size, f)) {
        int tick = -1;
        if (strstr(line, "\"type\":\"header\"")) fputs(line, stdout);
        if (sscanf(line, "{\"type\":\"tick\",\"tick\":%d", &tick) != 1)
            sscanf(line, "{\"type\":\"decision\",\"tick\":%d", &tick);
        if (tick == o->trace_tick) { fputs(line, stdout); found = 1; }
    }
    free(line); fclose(f);
    if (!found) fputs("No record at requested tick\n", stderr);
    return found;
}

/* Range and invariant test implementations below use the same native modules. */
static int lab_range(const struct lab_options *o) {
    struct rf_range_lab *r=calloc(1,sizeof(*r));
    static const char *targets[]={"full","upper","head","moving"};
    int row=0;
    if(!r)return 0;
    printf("{\"type\":\"range\",\"simulation_version\":%d,\"authority\":\"toy_game\",\"shot_seed\":%u,\"dt_ms\":%d,",RF_TAC_VERSION,o->shot_seed,RF_TAC_DT_MS);
    printf("\"baseline\":{\"hp\":%g,\"risk\":%g,\"width_m\":%g,\"height_m\":%g,\"move_speed_mps\":%g,\"recovery_delay_ms\":%d},\"rows\":[",
        RF_TW_BASE_HP,RF_TW_BASE_RISK,RF_TW_BASE_BODY_WIDTH_M,RF_TW_BASE_BODY_HEIGHT_M,
        RF_TW_BASE_MOVE_SPEED_MPS,RF_TW_BASE_RECOVERY_DELAY_MS);
    for(int weapon=0;weapon<2;++weapon) {
        if(o->weapon!=2 && o->weapon!=weapon)continue;
        for(int lane=0;lane<RF_RANGE_LANES;++lane)for(int target=0;target<4;++target)for(int mode=0;mode<3;++mode) {
            rf_range_reset(r,weapon,mode,lane,target==3?0:target,o->shot_seed);
            if(!r->game){free(r);return 0;}
            r->running=1;
            struct rf_range_stats *stats=&r->stats[0][lane];
            int elapsed=0,limit=o->samples*2000+120000;
            while(stats->shots<o->samples && elapsed<limit) {
                if(target==3) {
                    struct toy_game_actor *a=toy_game_actor_by_id(r->game,r->target_ids[0][lane]);
                    int x=(lane-4)*1536+640+((elapsed/2000)%2?350:-350);
                    toy_game_actor_set_intent(a,1,x,0,rf_range_distances[lane]*512,-1,0,0,0);
                }
                rf_range_step(r,RF_TAC_DT_MS);elapsed+=RF_TAC_DT_MS;
            }
            printf("%s{\"weapon\":\"%s\",\"distance_m\":%d,\"target\":\"%s\",\"mode\":\"%s\",\"sampled_shots\":%d,\"sampled_hits\":%d,\"sampled_head_hits\":%d,\"sampled_hit_rate\":%.9g,\"sampled_damage_per_shot\":%.9g,\"sampled_dps\":%.9g,\"ttk_trials\":%d,\"ttk_kills\":%d,\"ttk_censored\":%d,\"unfinished_trial_ms\":%d,\"mean_ttk_ms\":",
                row++?",":"",weapon?"smg":"rifle",rf_range_distances[lane],targets[target],rf_tw_fire_mode_name(mode),
                stats->shots,stats->hits,stats->heads,stats->shots?(double)stats->hits/stats->shots:0,
                stats->shots?stats->damage/stats->shots:0,elapsed?stats->damage*1000.0/elapsed:0,
                stats->kills+(stats->trial_ms>0),stats->kills,stats->trial_ms>0,stats->trial_ms);
            if(stats->kills)printf("%.9g}",(double)stats->ttk_total_ms/stats->kills);else fputs("null}",stdout);
        }
    }
    rf_range_destroy(r);free(r);puts("]}");return 1;
}
int rf_tac_run_tests(void);
static int lab_self_test(void) { return rf_tac_run_tests(); }

int main(int argc, char **argv) {
    struct lab_options o;
    struct rf_tac_policy a, b;
    struct rf_tac_map *map;
    struct lab_result r;
    FILE *log = NULL;
    int ok = 1;
    if (argc < 2 || !strcmp(argv[1], "--help") || !strcmp(argv[1], "help")) { lab_help(); return 0; }
    if (!lab_options(argc, argv, &o)) return 2;
    if (!strcmp(argv[1], "range")) return lab_range(&o) ? 0 : 1;
    if (!strcmp(argv[1], "inspect")) return lab_inspect(&o) ? 0 : 1;
    if (!strcmp(argv[1], "self-test")) return lab_self_test() ? 0 : 1;
    if (strcmp(argv[1], "match") && strcmp(argv[1], "batch")) { lab_help(); return 2; }
    if (!lab_policy(o.a, &o, &a) || !lab_policy(o.b, &o, &b)) return 2;
    map = (struct rf_tac_map *)calloc(1,sizeof(*map));
    if (!map) return 1;
    if (!strcmp(argv[1], "match")) {
        if (o.weapon == 2) { fputs("match requires one weapon\n", stderr); free(map); return 2; }
        if (o.log) { log = fopen(o.log, "wb"); if (!log) { perror(o.log); free(map); return 1; } }
        ok = rf_tac_map_generate(map, o.map_seed) && lab_match(map, &o, &a, &b, log, &r);
        if (log && fclose(log)) ok = 0;
        if (ok) lab_result_json(stdout, &r, o.map_seed, o.shot_seed, o.weapon);
    } else {
        int pair, weapon, swap, wins[2] = {0, 0}, draws = 0, games = 0;
        int role_games[2] = {0, 0}, role_wins[2] = {0, 0}, captures = 0;
        unsigned int aggregate_hash = 2166136261u;
        double health_margin = 0;
        double wall_start = lab_wall_seconds(), cpu_start = lab_cpu_seconds(), map_seconds = 0;
        struct lab_solver_cost strategy_costs[2];
        unsigned long long simulation_ms = 0;
        struct lab_options run = o;
        memset(strategy_costs, 0, sizeof(strategy_costs));
        if (o.log || o.trace_tick >= 0) { fputs("Use match for full logs or traces\n", stderr); free(map); return 2; }
        if (o.order[0] != RF_TAC_ATTACK || o.order[1] != RF_TAC_DEFEND) {
            fputs("batch fixes team 0 attack / team 1 defend; use match for custom orders\n", stderr);
            free(map); return 2;
        }
        for (pair = 0; pair < o.pairs && ok; ++pair) {
            run.map_seed = o.map_seed + (unsigned int)pair;
            run.shot_seed = o.shot_seed + (unsigned int)pair * 7919u;
            {
                double map_start = lab_wall_seconds();
                rf_tac_map_destroy(map);
                if (!rf_tac_map_generate(map, run.map_seed)) { ok = 0; break; }
                map_seconds += lab_wall_seconds() - map_start;
            }
            for (weapon = 0; weapon < 2 && ok; ++weapon) {
                if (o.weapon != 2 && o.weapon != weapon) continue;
                run.weapon = weapon;
                for (swap = 0; swap < 2 && ok; ++swap) {
                    ok = lab_match(map, &run, swap ? &b : &a, swap ? &a : &b, NULL, &r);
                    if (!ok) break;
                    ++games; ++role_games[swap]; captures += r.captured;
                    simulation_ms += r.time_ms;
                    lab_cost_add(&strategy_costs[0], &r.costs[swap]);
                    lab_cost_add(&strategy_costs[1], &r.costs[1 - swap]);
                    if (r.winner < 0) ++draws;
                    else if (r.winner == swap) { ++wins[0]; ++role_wins[swap]; }
                    else ++wins[1];
                    health_margin += (r.health[swap] - r.health[1 - swap]) /
                                     ((RF_TW_BASE_HP + RF_TW_BASE_RISK) * o.squad);
                    aggregate_hash ^= r.hash; aggregate_hash *= 16777619u;
                }
            }
        }
        if (ok) {
            fprintf(stdout, "{\"type\":\"batch\",\"simulation_version\":%d,\"map_seed\":%u,"
                    "\"shot_seed\":%u,\"pairs\":%d,\"games\":%d,\"weapon\":%d,\"squad_size\":%d,"
                    "\"a_wins\":%d,\"b_wins\":%d,\"draws\":%d,\"a_score\":%.9g,"
                    "\"a_attack_wins\":%d,\"a_defend_wins\":%d,\"role_games\":%d,"
                    "\"captures\":%d,\"mean_health_margin\":%.9g,\"simulation_ms\":%llu,"
                    "\"wall_seconds\":%.9g,\"map_preparation_elapsed_seconds\":%.9g,"
                    "\"aggregate_hash\":\"%08x\",\"policies\":[",
                    RF_TAC_VERSION, o.map_seed, o.shot_seed, o.pairs, games, o.weapon, o.squad,
                    wins[0], wins[1], draws, (wins[0] + draws * 0.5) / games,
                    role_wins[0], role_wins[1], role_games[0], captures, health_margin / games,
                    simulation_ms, lab_wall_seconds() - wall_start, map_seconds, aggregate_hash);
            lab_policy_json(stdout, &a, o.a); fputc(',', stdout); lab_policy_json(stdout, &b, o.b);
            fputs("],\"strategy_costs\":[", stdout);
            lab_cost_json(stdout, &strategy_costs[0]); fputc(',', stdout); lab_cost_json(stdout, &strategy_costs[1]);
            fputs("],\"cpu_seconds\":", stdout);
            {
                double cpu_end = lab_cpu_seconds();
                if (cpu_start >= 0 && cpu_end >= cpu_start) fprintf(stdout, "%.9g", cpu_end - cpu_start);
                else fputs("null", stdout);
            }
            fputs(",\"timing_scope\":\"batch process CPU includes map preparation, observations, solvers and execution; "
                  "strategy costs are elapsed time and include scheduler effects\"}\n", stdout);
        }
    }
    rf_tac_map_destroy(map);free(map);
    if (!ok) fputs("Native tactical simulation failed\n", stderr);
    return ok ? 0 : 1;
}
