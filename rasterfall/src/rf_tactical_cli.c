/* Headless lab: all simulation and weapon evaluation remain in native C. */
#include "rf_tactical.h"
#include "rf_tactical_weapon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <math.h>
#include <time.h>

struct lab_options {
    unsigned int map_seed, shot_seed;
    int squad, weapon, duration_ms, pairs, budget, trace_tick, samples, order[2];
    const char *a, *b, *log;
};
struct lab_result {
    int winner, time_ms, ticks, alive[2], shots[2], hits[2], captured;
    float health[2], damage[2];
    unsigned int hash;
};

static void lab_help(void) {
    puts("Rasterfall tactical lab v1 (native deterministic simulation)\n"
         "  rf-tactical range [--samples 2000] [--shot-seed 1337] [--weapon rifle|smg|both]\n"
         "  rf-tactical match [--a simple|mechanical|utility|FILE.cfg] [--b POLICY]\n"
         "                    [--map-seed 100] [--shot-seed 1337] [--squad 4..6]\n"
         "                    [--weapon rifle|smg] [--duration-ms 60000] [--budget 128]\n"
         "                    [--log FILE.jsonl] [--trace-tick TICK]\n"
         "                    [--order-a attack|defend] [--order-b attack|defend]\n"
         "  rf-tactical batch [same options] [--pairs 8] [--weapon rifle|smg|both]\n"
         "  rf-tactical inspect --log FILE.jsonl --trace-tick TICK\n"
         "  rf-tactical self-test\n"
         "Match team 0 attacks; team 1 defends. Attack clears and captures, then defends.\n"
         "Batch swaps A/B attack and defense on every map/shot seed and mirrored weapon.\n"
         "Range covers 5/10/15/20/30/40/60/80/100 m, full/upper/head/moving targets,\n"
         "single/burst/auto fire. stdout is JSON; full native replay is optional JSONL.\n"
         "Replay tick is the 20 ms physics tick; decisions occur every 10 ticks.");
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
    for (s = (const unsigned char *)text; *s; ++s) { h ^= *s; h *= 16777619u; }
    return h;
}
static void lab_weapon_json(FILE *f, int weapon) {
    const rf_tw_profile *p = rf_tw_profile_get(weapon);
    fputs("{\"name\":", f); lab_string(f, p->name);
    fprintf(f, ",\"kind\":%d,\"damage_milli\":%d,\"minimum_damage_milli\":%d,"
               "\"head_multiplier_milli\":%d,\"magazine\":%d,\"shot_interval_ms\":%d,"
               "\"reload_ms\":%d,\"falloff_start_m\":%.9g,\"falloff_end_m\":%.9g,"
               "\"muzzle_speed_mps\":%.9g,\"spread_mrad\":%.9g,\"recoil_mrad_per_shot\":%.9g,"
               "\"max_recoil_mrad\":%.9g,\"recoil_recovery_mrad_s\":%.9g,"
               "\"moving_spread_mrad\":%.9g,\"tracking_error_ms\":%.9g}", p->kind,
            p->damage_milli, p->minimum_damage_milli, p->head_multiplier_milli, p->magazine,
            p->shot_interval_ms, p->reload_ms, p->falloff_start_m, p->falloff_end_m,
            p->muzzle_speed_mps, p->spread_mrad, p->recoil_mrad_per_shot, p->max_recoil_mrad,
            p->recoil_recovery_mrad_s, p->moving_spread_mrad, p->tracking_error_ms);
}
static int lab_policy(const char *name, int budget, struct rf_tac_policy *out) {
    if (!strcmp(name, "simple")) rf_tac_policy_default(out, RF_TAC_SIMPLE);
    else if (!strcmp(name, "mechanical")) rf_tac_policy_default(out, RF_TAC_MECHANICAL);
    else if (!strcmp(name, "utility")) rf_tac_policy_default(out, RF_TAC_UTILITY);
    else if (!rf_tac_policy_load(name, out)) {
        fprintf(stderr, "Cannot load policy: %s\n", name); return 0;
    }
    out->budget = budget;
    return 1;
}
static void lab_policy_json(FILE *f, const struct rf_tac_policy *p, const char *name) {
    fputs("{\"name\":", f); lab_string(f, name);
    fprintf(f, ",\"hash\":\"%08x\",\"version\":%d,\"solver\":%d,\"budget\":%d,"
               "\"aggression\":%.9g,\"safety\":%.9g,\"progress\":%.9g,\"cover\":%.9g,"
               "\"focus\":%.9g,\"movement\":%.9g}", lab_policy_hash(p), p->version,
               p->solver, p->budget, p->aggression, p->safety, p->progress,
               p->cover, p->focus, p->movement);
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
static void lab_decision(FILE *f, const struct rf_tac_observation *o,
                         const struct rf_tac_plan *p, const struct rf_tac_decision_trace *t) {
    int i, j, k;
    fprintf(f, "{\"type\":\"decision\",\"tick\":%d,\"time_ms\":%d,\"team\":%d,"
               "\"evaluations\":%d,\"budget_exhausted\":%d,\"order\":%d,\"units\":[",
            o->time_ms / RF_TAC_DT_MS, o->time_ms, o->team, p->evaluations, p->budget_exhausted, o->order.kind);
    for (i = 0; i < o->count; ++i) {
        fprintf(f, "%s{\"id\":%d,\"selected\":%d,\"target\":%d,\"score\":%.9g,"
                   "\"action\":%d,\"destination\":[%.9g,%.9g],\"candidates\":[",
                i ? "," : "", o->friendly[i].id, t->selected[i], t->targets[i],
                t->selected_scores[i], p->actions[i].kind, p->destinations[i].x, p->destinations[i].y);
        for (j = 0; j < o->candidate_count[i]; ++j) {
            const struct rf_tac_candidate *c = &o->candidates[i][j];
            fprintf(f, "%s{\"index\":%d,\"kind\":%d,\"node\":%d,\"position\":[%.9g,%.9g],"
                       "\"score\":%.9g,\"cover_normal\":[%.9g,%.9g],\"height\":%d,"
                       "\"peek_left\":%d,\"peek_right\":%d,\"fire_over\":%d,"
                       "\"visible_enemy_mask\":%u,\"exposed_to_enemy_mask\":%u,"
                       "\"cover_quality\":%.9g,\"outgoing_dps\":%.9g,\"incoming_dps\":%.9g,"
                       "\"objective_distance\":%.9g,\"objective_path_distance\":%.9g,"
                       "\"path\":{\"move_distance\":%.9g,"
                       "\"travel_time\":%.9g,\"exposed_time\":%.9g,\"incoming_damage\":%.9g},"
                       "\"relations\":[",
                    j ? "," : "", j, c->kind, c->node, c->pos.x, c->pos.y, t->candidate_scores[i][j],
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
    fputs("]}\n", f);
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
                   "\"destination\":[%.9g,%.9g],\"shot_target\":%d,\"shots\":%d,\"hits\":%d,\"raw_damage\":%.9g,"
                   "\"absorbed\":%.9g,\"shot_delta\":%d,\"hit_delta\":%d,\"raw_damage_delta\":%.9g,"
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
               "\"shots\":[%d,%d],\"hits\":[%d,%d],\"raw_damage\":[%.9g,%.9g],"
               "\"final_hash\":\"%08x\"}\n", RF_TAC_VERSION, map_seed, shot_seed, weapon,
            r->winner, r->time_ms, r->ticks, r->captured, r->alive[0], r->alive[1],
            r->health[0], r->health[1], r->shots[0], r->shots[1], r->hits[0], r->hits[1],
            r->damage[0], r->damage[1], r->hash);
}
static int lab_match(const struct rf_tac_map *map, const struct lab_options *o,
                     const struct rf_tac_policy *a, const struct rf_tac_policy *b,
                     FILE *log, struct lab_result *result) {
    struct rf_tac_world w, old;
    struct rf_tac_observation obs[2];
    struct rf_tac_plan plan[2];
    struct rf_tac_decision_trace trace[2];
    const struct rf_tac_policy *policies[2];
    int i, team;
    policies[0] = a; policies[1] = b;
    if (!rf_tac_world_init(&w, map, o->shot_seed, o->squad, o->weapon, o->duration_ms)) return 0;
    for (team = 0; team < 2; ++team)
        if (!rf_tac_command(&w, team, o->order[team], map->objective, map->objective_radius)) return 0;
    if (log) { lab_header(log, map, o, a, b); lab_tick(log, &w, &w); }
    while (!w.finished) {
        if (w.time_ms % RF_TAC_THINK_MS == 0) {
            /* Both policies observe exactly the same state before either is applied. */
            for (team = 0; team < 2; ++team) {
                rf_tac_observe(&w, team, &obs[team]);
                memset(&trace[team], 0, sizeof(trace[team]));
                rf_tac_solve(&obs[team], policies[team], &plan[team], &trace[team]);
                if (log) lab_decision(log, &obs[team], &plan[team], &trace[team]);
                if (o->trace_tick == w.time_ms / RF_TAC_DT_MS) lab_decision(stdout, &obs[team], &plan[team], &trace[team]);
            }
            for (team = 0; team < 2; ++team) if (!rf_tac_apply(&w, &plan[team])) return 0;
        }
        old = w;
        rf_tac_step(&w);
        if (log) lab_tick(log, &w, &old);
        if (o->trace_tick == w.time_ms / RF_TAC_DT_MS && !log) lab_tick(stdout, &w, &old);
    }
    memset(result, 0, sizeof(*result));
    result->winner = w.winner; result->time_ms = w.time_ms; result->ticks = w.time_ms / RF_TAC_DT_MS;
    result->hash = rf_tac_hash(&w); result->captured = w.orders[0].captured;
    for (i = 0; i < w.squad_size * 2; ++i) {
        const struct rf_tac_unit *u = &w.units[(i / w.squad_size) * RF_TAC_MAX_SQUAD + i % w.squad_size];
        result->alive[u->team] += u->alive;
        result->health[u->team] += u->hp + u->evasion;
        result->shots[u->team] += u->shots; result->hits[u->team] += u->hits;
        result->damage[u->team] += u->damage;
    }
    if (log) lab_result_json(log, result, o->map_seed, o->shot_seed, o->weapon);
    return 1;
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
    static const int distances[] = {5, 10, 15, 20, 30, 40, 60, 80, 100};
    static const char *targets[] = {"full", "upper", "head", "moving"};
    int weapon, distance, target, mode;
    fprintf(stdout, "{\"type\":\"range\",\"simulation_version\":%d,\"shot_seed\":%u,"
            "\"dt_ms\":%d,\"baseline\":{\"hp\":%g,\"risk\":%g,\"width_m\":%g,"
            "\"height_m\":%g,\"move_speed_mps\":%g,\"recovery_delay_ms\":%d},"
            "\"ttk_mean_conditional_on_kill\":true,\"ttk_limit_ms\":120000,\"rows\":[",
            RF_TAC_VERSION, o->shot_seed, RF_TAC_DT_MS, RF_TW_BASE_HP, RF_TW_BASE_RISK,
            RF_TW_BASE_BODY_WIDTH_M, RF_TW_BASE_BODY_HEIGHT_M, RF_TW_BASE_MOVE_SPEED_MPS,
            RF_TW_BASE_RECOVERY_DELAY_MS);
    {
        int row = 0;
        for (weapon = 0; weapon < RF_TW_KIND_COUNT; ++weapon) {
            const rf_tw_profile *profile = rf_tw_profile_get(weapon);
            if (o->weapon != 2 && o->weapon != weapon) continue;
            for (distance = 0; distance < 9; ++distance)
            for (target = 0; target < 4; ++target)
            for (mode = RF_TW_SINGLE; mode <= RF_TW_AUTO; ++mode) {
                rf_tw_context context;
                rf_tw_metrics analytic, current;
                rf_tw_state state;
                rf_tw_shot shot;
                unsigned int rng = o->shot_seed ^ ((unsigned int)distance * 0x9e3779b9u) ^
                                   ((unsigned int)target * 7919u) ^ ((unsigned int)mode * 101u);
                int shots = 0, hits = 0, heads = 0, wait_ms = 0, elapsed_ms = 0;
                int trials = o->samples / 30, trial, kills = 0;
                double damage = 0, expected_hits = 0, expected_damage = 0, spread_squared = 0;
                double ttk_sum = 0;
                if (trials < 20) trials = 20;
                if (trials > 2000) trials = 2000;
                rf_tw_context_reset(&context); context.distance_m = (float)distances[distance];
                context.exposure = target == 1 ? RF_TW_UPPER : target == 2 ? RF_TW_HEAD : RF_TW_FULL;
                context.target_lateral_speed_mps = target == 3 ? RF_TW_BASE_MOVE_SPEED_MPS : 0;
                rf_tw_pattern_metrics(profile, &context, mode, &analytic);
                rf_tw_state_reset(profile, &state);
                while (shots < o->samples) {
                    if (!state.ammo && !state.reload_remaining_ms) rf_tw_state_begin_reload(profile, &state);
                    if (!wait_ms && !state.cooldown_ms && !state.reload_remaining_ms && state.ammo) {
                        context.recoil_milli_mrad = state.recoil_milli_mrad;
                        rf_tw_query(profile, &context, &current);
                        rf_tw_sample_shot(profile, &context, &rng, &shot);
                        if (!rf_tw_state_begin_shot(profile, &state)) return 0;
                        ++shots; hits += shot.hit; heads += shot.head;
                        damage += shot.damage_milli / 1000.0;
                        expected_hits += current.hit_probability;
                        expected_damage += current.expected_damage;
                        spread_squared += shot.offset_x_m * shot.offset_x_m + shot.offset_y_m * shot.offset_y_m;
                        wait_ms = rf_tw_pattern_delay_ms(profile, mode, shots);
                    }
                    rf_tw_state_advance(profile, &state, RF_TAC_DT_MS);
                    wait_ms = wait_ms > RF_TAC_DT_MS ? wait_ms - RF_TAC_DT_MS : 0;
                    elapsed_ms += RF_TAC_DT_MS;
                }
                /* TTK is actual standard-warrior HP/risk resolution, with misses,
                 * recoil, reload and delayed recovery; censored trials stay explicit. */
                for (trial = 0; trial < trials; ++trial) {
                    float hp = RF_TW_BASE_HP, risk = RF_TW_BASE_RISK;
                    int recovery_ms = 0, trial_ms = 0, trial_shots = 0;
                    rf_tw_state_reset(profile, &state); wait_ms = 0;
                    while (hp > 0 && trial_ms < 120000) {
                        rf_tw_recover(&risk, &recovery_ms, RF_TAC_DT_MS);
                        if (!state.ammo && !state.reload_remaining_ms) rf_tw_state_begin_reload(profile, &state);
                        if (!wait_ms && !state.cooldown_ms && !state.reload_remaining_ms && state.ammo) {
                            context.recoil_milli_mrad = state.recoil_milli_mrad;
                            rf_tw_sample_shot(profile, &context, &rng, &shot);
                            if (!rf_tw_state_begin_shot(profile, &state)) return 0;
                            ++trial_shots;
                            if (shot.hit) {
                                rf_tw_apply_damage(&hp, &risk, shot.damage_milli / 1000.0f,
                                                   shot.risk_cost_milli / 1000.0f);
                                recovery_ms = RF_TW_BASE_RECOVERY_DELAY_MS;
                            }
                            wait_ms = rf_tw_pattern_delay_ms(profile, mode, trial_shots);
                        }
                        rf_tw_state_advance(profile, &state, RF_TAC_DT_MS);
                        wait_ms = wait_ms > RF_TAC_DT_MS ? wait_ms - RF_TAC_DT_MS : 0;
                        trial_ms += RF_TAC_DT_MS;
                    }
                    if (hp <= 0) { ++kills; ttk_sum += trial_ms; }
                }
                fprintf(stdout, "%s{\"weapon\":", row++ ? "," : ""); lab_string(stdout, profile->name);
                fprintf(stdout, ",\"distance_m\":%d,\"target\":\"%s\",\"mode\":\"%s\","
                        "\"sampled_shots\":%d,\"sampled_hits\":%d,\"sampled_head_hits\":%d,"
                        "\"sampled_hit_rate\":%.9g,\"expected_hit_rate\":%.9g,"
                        "\"sampled_damage_per_shot\":%.9g,\"expected_damage_per_shot\":%.9g,"
                        "\"sampled_dps\":%.9g,\"expected_dps\":%.9g,\"spread_rms_m\":%.9g,"
                        "\"cold_magazine_expected_hit_rate\":%.9g,\"mechanical_reload_cycle_dps\":%.9g,"
                        "\"ttk_trials\":%d,\"ttk_kills\":%d,\"ttk_censored\":%d,\"mean_ttk_ms\":",
                        distances[distance], targets[target], rf_tw_fire_mode_name(mode), shots, hits, heads,
                        (double)hits / shots, expected_hits / shots, damage / shots, expected_damage / shots,
                        damage * 1000 / elapsed_ms, expected_damage * 1000 / elapsed_ms,
                        sqrt(spread_squared / shots), analytic.hit_probability, analytic.reload_cycle_dps,
                        trials, kills, trials - kills);
                if (kills) fprintf(stdout, "%.9g}", ttk_sum / kills); else fputs("null}", stdout);
            }
        }
    }
    fputs("]}\n", stdout);
    return 1;
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
    if (!lab_policy(o.a, o.budget, &a) || !lab_policy(o.b, o.budget, &b)) return 2;
    map = (struct rf_tac_map *)malloc(sizeof(*map));
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
        clock_t start = clock();
        struct lab_options run = o;
        if (o.log || o.trace_tick >= 0) { fputs("Use match for full logs or traces\n", stderr); free(map); return 2; }
        if (o.order[0] != RF_TAC_ATTACK || o.order[1] != RF_TAC_DEFEND) {
            fputs("batch fixes team 0 attack / team 1 defend; use match for custom orders\n", stderr);
            free(map); return 2;
        }
        for (pair = 0; pair < o.pairs && ok; ++pair) {
            run.map_seed = o.map_seed + (unsigned int)pair;
            run.shot_seed = o.shot_seed + (unsigned int)pair * 7919u;
            if (!rf_tac_map_generate(map, run.map_seed)) { ok = 0; break; }
            for (weapon = 0; weapon < 2 && ok; ++weapon) {
                if (o.weapon != 2 && o.weapon != weapon) continue;
                run.weapon = weapon;
                for (swap = 0; swap < 2 && ok; ++swap) {
                    ok = lab_match(map, &run, swap ? &b : &a, swap ? &a : &b, NULL, &r);
                    if (!ok) break;
                    ++games; ++role_games[swap]; captures += r.captured;
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
                    "\"captures\":%d,\"mean_health_margin\":%.9g,\"cpu_seconds\":%.9g,"
                    "\"aggregate_hash\":\"%08x\",\"policies\":[",
                    RF_TAC_VERSION, o.map_seed, o.shot_seed, o.pairs, games, o.weapon, o.squad,
                    wins[0], wins[1], draws, (wins[0] + draws * 0.5) / games,
                    role_wins[0], role_wins[1], role_games[0], captures, health_margin / games,
                    (double)(clock() - start) / CLOCKS_PER_SEC, aggregate_hash);
            lab_policy_json(stdout, &a, o.a); fputc(',', stdout); lab_policy_json(stdout, &b, o.b);
            fputs("]}\n", stdout);
        }
    }
    free(map);
    if (!ok) fputs("Native tactical simulation failed\n", stderr);
    return ok ? 0 : 1;
}
