#include "rf_tactical.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

static float distance(struct rf_tac_vec a, struct rf_tac_vec b)
{
    float x = a.x - b.x, y = a.y - b.y;
    return sqrtf(x * x + y * y);
}

void rf_tac_policy_default(struct rf_tac_policy *p, int solver)
{
    memset(p, 0, sizeof(*p));
    p->version = RF_TAC_VERSION;
    p->solver = solver;
    p->budget = 256;
    p->aggression = 1.0f;
    p->safety = 1.0f;
    p->progress = 4.0f;
    p->cover = 3.0f;
    p->focus = 2.0f;
    p->movement = 0.4f;
    p->beam_width = 2;
    p->beam_branches = 4;
    p->beam_horizon_ms = 800;
}

int rf_tac_policy_validate(const struct rf_tac_policy *p)
{
    const float values[] = { p->aggression, p->safety, p->progress,
                            p->cover, p->focus, p->movement };
    int i;
    if (p->version != RF_TAC_VERSION || p->solver < RF_TAC_SIMPLE ||
        p->solver > RF_TAC_BEAM || p->budget < 0 || p->budget > 100000) return 0;
    for (i = 0; i < 6; ++i)
        if (!isfinite(values[i]) || values[i] < 0 || values[i] > 100) return 0;
    if (p->solver == RF_TAC_BEAM && (p->beam_width < 1 || p->beam_width > RF_TAC_BEAM_MAX_WIDTH ||
        p->beam_branches < 2 || p->beam_branches > RF_TAC_BEAM_MAX_BRANCHES ||
        p->beam_horizon_ms < RF_TAC_THINK_MS || p->beam_horizon_ms > 4000 ||
        p->beam_horizon_ms % RF_TAC_THINK_MS)) return 0;
    return 1;
}

int rf_tac_policy_load(const char *path, struct rf_tac_policy *p)
{
    FILE *f = fopen(path, "rb");
    struct rf_tac_policy loaded;
    char line[256];
    unsigned int seen = 0;
    int ok = 1;
    if (!f) return 0;
    rf_tac_policy_default(&loaded, RF_TAC_UTILITY);
    while (fgets(line, sizeof(line), f)) {
        char *key = line, *value, *end, *eq;
        float *field = NULL;
        int *integer = NULL;
        unsigned int bit = 0;
        while (*key == ' ' || *key == '\t') ++key;
        if (*key == '#' || *key == '\r' || *key == '\n' || !*key) continue;
        if (!strchr(key, '\n') && !feof(f)) { ok = 0; break; }
        eq = strchr(key, '=');
        if (!eq) { ok = 0; break; }
        *eq = 0;
        end = eq;
        while (end > key && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
        value = eq + 1;
        while (*value == ' ' || *value == '\t') ++value;
        end = value + strlen(value);
        while (end > value && (end[-1] == '\r' || end[-1] == '\n' ||
               end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
        if (!strcmp(key, "policy_version")) { bit = 1; integer = &loaded.version; }
        else if (!strcmp(key, "solver")) bit = 2;
        else if (!strcmp(key, "budget")) { bit = 4; integer = &loaded.budget; }
        else if (!strcmp(key, "aggression")) { bit = 8; field = &loaded.aggression; }
        else if (!strcmp(key, "safety")) { bit = 16; field = &loaded.safety; }
        else if (!strcmp(key, "progress")) { bit = 32; field = &loaded.progress; }
        else if (!strcmp(key, "cover")) { bit = 64; field = &loaded.cover; }
        else if (!strcmp(key, "focus")) { bit = 128; field = &loaded.focus; }
        else if (!strcmp(key, "movement")) { bit = 256; field = &loaded.movement; }
        else if (!strcmp(key, "beam_width")) { bit = 512; integer = &loaded.beam_width; }
        else if (!strcmp(key, "beam_branches")) { bit = 1024; integer = &loaded.beam_branches; }
        else if (!strcmp(key, "beam_horizon_ms")) { bit = 2048; integer = &loaded.beam_horizon_ms; }
        else { ok = 0; break; }
        if (seen & bit) { ok = 0; break; }
        seen |= bit;
        if (field) {
            *field = strtof(value, &end);
            if (end == value || *end) { ok = 0; break; }
        } else if (bit == 2) {
            if (!strcmp(value, "simple")) loaded.solver = RF_TAC_SIMPLE;
            else if (!strcmp(value, "mechanical")) loaded.solver = RF_TAC_MECHANICAL;
            else if (!strcmp(value, "utility")) loaded.solver = RF_TAC_UTILITY;
            else if (!strcmp(value, "beam")) loaded.solver = RF_TAC_BEAM;
            else { ok = 0; break; }
        } else {
            long n = strtol(value, &end, 10);
            if (end == value || *end || n < 0 || n > 100000) { ok = 0; break; }
            *integer = (int)n;
        }
    }
    if (ferror(f)) ok = 0;
    fclose(f);
    if (!ok || (seen & 511) != 511 || !rf_tac_policy_validate(&loaded) ||
        (loaded.solver != RF_TAC_BEAM && (seen & 3584))) return 0;
    *p = loaded;
    return 1;
}

int rf_tac_policy_save(const char *path, const struct rf_tac_policy *p)
{
    FILE *f;
    int ok;
    if (!rf_tac_policy_validate(p)) return 0;
    f = fopen(path, "wb");
    if (!f) return 0;
    ok = fprintf(f, "policy_version=%d\nsolver=%s\nbudget=%d\naggression=%.9g\n"
                   "safety=%.9g\nprogress=%.9g\ncover=%.9g\nfocus=%.9g\nmovement=%.9g\n",
                   p->version, p->solver == RF_TAC_SIMPLE ? "simple" :
                   p->solver == RF_TAC_MECHANICAL ? "mechanical" :
                   p->solver == RF_TAC_BEAM ? "beam" : "utility",
                   p->budget, p->aggression, p->safety, p->progress,
                   p->cover, p->focus, p->movement) >= 0;
    if (ok && p->solver == RF_TAC_BEAM)
        ok = fprintf(f, "beam_width=%d\nbeam_branches=%d\nbeam_horizon_ms=%d\n",
                     p->beam_width, p->beam_branches, p->beam_horizon_ms) >= 0;
    if (fclose(f)) ok = 0;
    return ok;
}

static int consume(struct rf_tac_plan *p, int budget)
{
    if (p->evaluations >= budget) { p->budget_exhausted = 1; return 0; }
    ++p->evaluations;
    return 1;
}

/* HOLD's engine target is nearest legal enemy. FIRE may change only its target.
 * The score below is solver-owned and can be replaced by a future beam solver. */
static int best_target(const struct rf_tac_observation *o, int unit,
                       const struct rf_tac_policy *policy, struct rf_tac_plan *plan,
                       const int *assigned)
{
    float best = -1;
    int j, target = -1;
    for (j = 0; j < o->count; ++j) {
        const struct rf_tac_relation *r = &o->relations[unit][j];
        float score;
        if (!o->enemy[j].alive || !r->visible || r->expected_dps <= 0) continue;
        if (!consume(plan, policy->budget)) break;
        score = r->expected_dps * (1 + policy->focus /
                (1 + o->enemy[j].effective_health / 25.0f));
        /* Marginal focus benefit saturates once committed fire can kill. */
        if (assigned[j] && assigned[j] * r->expected_dps * 0.6f <
            o->enemy[j].effective_health) score += policy->focus * 2;
        if (score > best) { best = score; target = j; }
    }
    return target;
}

void rf_tac_solve(const struct rf_tac_observation *o, const struct rf_tac_policy *policy,
                  struct rf_tac_plan *p, struct rf_tac_decision_trace *trace)
{
    int i, j, assigned[RF_TAC_MAX_SQUAD] = { 0 };
    rf_tac_plan_hold(o, p);
    if (trace) {
        memset(trace, 0, sizeof(*trace));
        for (i = 0; i < RF_TAC_MAX_SQUAD; ++i) {
            trace->targets[i] = -1;
            for (j = 0; j < RF_TAC_CANDIDATES; ++j)
                trace->candidate_scores[i][j] = -1e20f;
        }
    }
    if (!rf_tac_policy_validate(policy) || policy->budget == 0) {
        p->budget_exhausted = 1;
        return;
    }
    if (policy->solver == RF_TAC_BEAM) {
        if (trace) trace->prediction_unavailable = 1;
        return;
    }
    for (i = 0; i < o->count; ++i) {
        int selected = 0, target = -1;
        float best = -1e20f;
        float current_distance = o->candidate_count[i] ?
            o->candidates[i][0].objective_path_distance : 0;
        int count = o->candidate_count[i];
        if (!o->friendly[i].alive) continue;
        /* Unvisited members keep the initial HOLD even when their magazine is
         * empty. Execution owns HOLD's automatic reload; it costs no search. */
        if (p->evaluations >= policy->budget) {
            p->budget_exhausted = 1;
            break;
        }
        if (policy->solver != RF_TAC_SIMPLE)
            target = best_target(o, i, policy, p, assigned);
        for (j = 0; j < count; ++j) {
            const struct rf_tac_candidate *c = &o->candidates[i][j];
            float score;
            if (!consume(p, policy->budget)) break;
            if (o->order.kind == RF_TAC_DEFEND && j &&
                c->objective_distance > o->order.radius) continue;
            if (policy->solver == RF_TAC_SIMPLE) {
                score = o->order.kind == RF_TAC_ATTACK ?
                    (c->kind == RF_TAC_ADVANCE ? 1 : (j ? -1e10f : 0)) :
                    (j ? -1e10f : 0);
            } else if (policy->solver == RF_TAC_MECHANICAL) {
                score = (current_distance - c->objective_path_distance) *
                        (o->order.kind == RF_TAC_ATTACK ? 4 : 0) +
                        c->cover_quality * 3 - c->path.incoming_damage * 0.6f;
                if (j && target >= 0 && o->relations[i][target].hit_rate > 0.20f)
                    score -= 100;
                if (o->order.kind == RF_TAC_DEFEND) score += c->outgoing_dps * 0.3f;
            } else {
                float health_fraction = o->friendly[i].effective_health /
                    fmaxf(1, o->friendly[i].max_effective_health);
                float risk = policy->safety * (2 - health_fraction);
                float fire = c->outgoing_dps * 0.6f;
                float progress = current_distance - c->objective_path_distance;
                /* Units cannot fire during MOVE. Position benefit is discounted
                 * by travel duration; transient exposure is integrated by nav. */
                float horizon = 1.0f / (1 + c->path.travel_time);
                score = policy->aggression * fire * horizon -
                        risk * (c->incoming_dps * 0.6f + c->path.incoming_damage) +
                        policy->progress * progress *
                        (o->order.kind == RF_TAC_ATTACK ? 1 : 0.15f) +
                        policy->cover * c->cover_quality -
                        policy->movement * c->path.travel_time;
                if (j == 0 && target >= 0) score += policy->focus * 2;
                if (j) {
                    int k;
                    for (k = 0; k < i; ++k)
                        if (o->friendly[k].alive && p->actions[k].kind == RF_TAC_MOVE &&
                            distance(c->pos, p->destinations[k]) < 1.2f) score -= 20;
                }
            }
            if (trace) trace->candidate_scores[i][j] = score;
            if (score > best) { best = score; selected = j; }
        }
        if (selected && best > -1e19f) {
            p->actions[i].kind = RF_TAC_MOVE;
            p->actions[i].candidate = selected;
            p->destinations[i] = o->candidates[i][selected].pos;
        } else if (target >= 0 && policy->solver != RF_TAC_SIMPLE) {
            p->actions[i].kind = RF_TAC_FIRE;
            p->actions[i].target = target;
            ++assigned[target];
        }
        if (o->friendly[i].ammo == 0 && o->friendly[i].reload_ms == 0 &&
            p->actions[i].kind != RF_TAC_MOVE) p->actions[i].kind = RF_TAC_RELOAD;
        if (trace) {
            trace->selected[i] = selected;
            trace->targets[i] = target;
            trace->selected_scores[i] = best > -1e19f ? best : 0;
        }
    }
}
