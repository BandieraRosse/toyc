#include "rf_tactical_beam.h"
#include "rf_tactical_weapon.h"
#include <math.h>
#include <string.h>

/* Root joint-action beam. Expansion is deliberately cheap; only retained full
 * plans receive an engine forecast. This keeps a real six-member search within
 * the default 128 work units: at most 44 prefixes + two 40-step forecasts + two
 * leaf scores. The temporal horizon holds these actions, then a new real tick
 * replans. It is not yet a tree of multiple future decision cycles. */
struct beam_node {
    struct rf_tac_plan plan;
    struct rf_tac_search_score score;
    int parent, changed;
};
struct beam_option {
    struct rf_tac_action action;
    struct rf_tac_vec destination;
};

static float minimum(float a, float b) { return a < b ? a : b; }
static float positive(float a) { return a > 0 ? a : 0; }
static float length(struct rf_tac_vec a, struct rf_tac_vec b)
{
    float x = a.x - b.x, y = a.y - b.y;
    return sqrtf(x*x + y*y);
}
static void total(struct rf_tac_search_score *s)
{
    s->total = s->health + s->progress + s->firepower + s->risk + s->cohesion + s->terminal;
}
static float remaining(float distance, float navigation, float radius)
{
    return distance <= radius ? 0 : positive(navigation - radius);
}
static float root_remaining(const struct rf_tac_observation *o, int unit, int candidate)
{
    const struct rf_tac_candidate *c = &o->candidates[unit][candidate];
    return remaining(c->objective_distance, c->objective_path_distance, o->order.radius);
}
static float group_distance(const float *distances, const int *alive, int count)
{
    float sum = 0, worst = 0;
    int i, n = 0;
    for (i = 0; i < count; ++i) if (alive[i]) {
        sum += distances[i]; ++n;
        if (distances[i] > worst) worst = distances[i];
    }
    /* Occupation requires every surviving member, so the last one matters. */
    return n ? sum/n + 0.5f*worst : 0;
}
static int nearest_target(const struct rf_tac_unit_view *enemy,
                          const struct rf_tac_relation *relations, int count)
{
    int j, best = -1;
    float distance = 1e30f;
    for (j = 0; j < count; ++j)
        if (enemy[j].alive && relations[j].visible && relations[j].distance < distance) {
            best = j; distance = relations[j].distance;
        }
    return best;
}
static float kill_value(float damage, float health, float focus)
{
    float useful = minimum(positive(damage), positive(health));
    /* No reward for excessive assignment to an already covered target. */
    return useful + (health > 0 && damage >= health ? focus : 0);
}

/* Cheap fixed-rate fire potential uses public weapon constants and observed
 * ammo/timers. Geometry and recoil remain the engine's relation facts. Count
 * finite-magazine shots, including automatic empty reload, so manual RELOAD
 * is not discarded merely because its first part cannot fire. */
static float cheap_fire(const struct rf_tac_unit_view *u,
                         const struct rf_tac_relation *relation,
                         int action, float travel_seconds, float horizon)
{
    (void)action;
    float ready=fmaxf(u->reload_ms,u->cooldown_ms)/1000.0f;
    float available=horizon-fmaxf(ready,travel_seconds);
    return relation->expected_dps*fmaxf(0,available);

}

static struct rf_tac_search_score cheap_score(const struct rf_tac_observation *o,
                                             const struct rf_tac_policy *p,
                                             const struct rf_tac_plan *plan)
{
    struct rf_tac_search_score s = {0};
    float output[RF_TAC_MAX_SQUAD] = {0}, distances[RF_TAC_MAX_SQUAD] = {0};
    float before[RF_TAC_MAX_SQUAD] = {0};
    int alive[RF_TAC_MAX_SQUAD] = {0}, i, j, survivors = 0;
    const float tail = 3.0f;
    for (i = 0; i < o->count; ++i) if (o->friendly[i].alive) {
        const struct rf_tac_action *a = &plan->actions[i];
        int moving = a->kind == RF_TAC_MOVE;
        int ci = moving ? a->candidate : 0;
        const struct rf_tac_candidate *c = &o->candidates[i][ci];
        int target = a->kind == RF_TAC_FIRE ? a->target :
            nearest_target(o->enemy, c->relations, o->count);
        float incoming_available = positive(tail - (moving ? c->path.travel_time : 0));
        alive[i] = 1; before[i] = root_remaining(o, i, 0);
        ++survivors;
        distances[i] = root_remaining(o, i, ci);
        if (target >= 0)
            output[target] += cheap_fire(&o->friendly[i], &c->relations[target],
                a->kind, moving ? c->path.travel_time : 0, tail);
        s.risk -= p->safety*(c->path.incoming_damage + c->incoming_dps*incoming_available)*0.25f;
        s.cohesion += p->cover*c->cover_quality - p->movement*c->path.travel_time;
        if (moving) for (j = 0; j < i; ++j)
            if (o->friendly[j].alive && plan->actions[j].kind == RF_TAC_MOVE &&
                length(plan->destinations[i], plan->destinations[j]) < 1.2f)
                s.cohesion -= 2;
    }
    for (j = 0; j < o->count; ++j) if (o->enemy[j].alive)
        s.firepower += p->aggression*kill_value(output[j], o->enemy[j].effective_health, p->focus)*0.25f;
    s.progress = p->progress*survivors*(group_distance(before, alive, o->count) -
        group_distance(distances, alive, o->count))*(o->order.kind == RF_TAC_ATTACK ? 1 : 0.15f);
    total(&s); return s;
}

static int add_option(struct beam_option *options, int count, int limit,
                       int kind, int candidate, int target, struct rf_tac_vec destination)
{
    int i;
    for (i = 0; i < count; ++i)
        if (options[i].action.kind == kind && options[i].action.candidate == candidate &&
            options[i].action.target == target) return count;
    if (count == limit) return count;
    options[count].action.kind = kind;
    options[count].action.candidate = candidate;
    options[count].action.target = target;
    options[count].destination = destination;
    return count + 1;
}

static int options_for(const struct rf_tac_observation *o, const struct rf_tac_policy *p,
                         int unit, struct beam_option *options)
{
    float rank[RF_TAC_CANDIDATES], targets[RF_TAC_MAX_SQUAD];
    int i, k, count = 0, advance = -1, nearest;
    const struct rf_tac_unit_view *u = &o->friendly[unit];
    count = add_option(options, count, p->beam_branches, RF_TAC_HOLD, 0, -1, u->pos);
    if (!u->alive) return count;
    for (i = 0; i < o->candidate_count[unit]; ++i) {
        const struct rf_tac_candidate *c = &o->candidates[unit][i];
        rank[i] = -1e30f;
        if (!i || (o->order.kind == RF_TAC_DEFEND && c->objective_distance > o->order.radius)) continue;
        /* Fixed-capacity option admission reads local facts only. It never
         * performs an unmetered full joint-plan evaluation or forecast. */
        rank[i] = p->progress*(root_remaining(o, unit, 0) - root_remaining(o, unit, i))*
            (o->order.kind == RF_TAC_ATTACK ? 1 : 0.15f) + p->cover*c->cover_quality -
            p->safety*c->path.incoming_damage*0.25f - p->movement*c->path.travel_time;
        if (c->kind == RF_TAC_ADVANCE) advance = i;
    }
    if (o->order.kind == RF_TAC_ATTACK && advance >= 0) {
        count = add_option(options, count, p->beam_branches, RF_TAC_MOVE, advance, -1,
                           o->candidates[unit][advance].pos);
        rank[advance] = -1e30f;
    }
    for (i = 0; i < o->count; ++i) {
        const struct rf_tac_relation *r = &o->relations[unit][i];
        targets[i] = o->enemy[i].alive && r->visible && r->expected_dps > 0 ?
            r->expected_dps*(1 + p->focus/(1 + o->enemy[i].effective_health/25)) : -1;
    }
    /* HOLD initially fires at the nearest enemy and may retarget after death.
     * Under enemy HOLD, an explicit nearest FIRE adds no useful alternative.
     * Keep a slot for cover and admit a different firing target instead. */
    nearest = nearest_target(o->enemy, o->relations[unit], o->count);
    if (nearest >= 0) targets[nearest] = -1;
    for (k = 0; k < 1; ++k) {
        int best = -1;
        for (i = 0; i < o->count; ++i)
            if (targets[i] >= 0 && (best < 0 || targets[i] > targets[best])) best = i;
        if (best < 0) break;
        count = add_option(options, count, p->beam_branches, RF_TAC_FIRE, 0, best, u->pos);
        targets[best] = -1;
    }
    {
        int best = -1;
        for (i = 1; i < o->candidate_count[unit]; ++i)
            if (rank[i] > -1e29f && (best < 0 || rank[i] > rank[best])) best = i;
        if (best >= 0) {
            count = add_option(options, count, p->beam_branches, RF_TAC_MOVE, best, -1,
                               o->candidates[unit][best].pos);
            rank[best] = -1e30f;
        }
    }
    if (u->ammo > 0 && u->ammo <= 6 && !u->reload_ms)
        count = add_option(options, count, p->beam_branches, RF_TAC_RELOAD, 0, -1, u->pos);
    for (k = 0; k < o->count && count < p->beam_branches; ++k) {
        int best = -1;
        for (i = 0; i < o->count; ++i)
            if (targets[i] >= 0 && (best < 0 || targets[i] > targets[best])) best = i;
        if (best < 0) break;
        count = add_option(options, count, p->beam_branches, RF_TAC_FIRE, 0, best, u->pos);
        targets[best] = -1;
    }
    while (count < p->beam_branches) {
        int best = -1;
        for (i = 1; i < o->candidate_count[unit]; ++i)
            if (rank[i] > -1e29f && (best < 0 || rank[i] > rank[best])) best = i;
        if (best < 0) break;
        count = add_option(options, count, p->beam_branches, RF_TAC_MOVE, best, -1,
                           o->candidates[unit][best].pos);
        rank[best] = -1e30f;
    }
    return count;
}

static struct rf_tac_search_score forecast_score(const struct rf_tac_observation *o,
                                                 const struct rf_tac_policy *p,
                                                 const struct rf_tac_plan *plan,
                                                 const struct rf_tac_forecast *f)
{
    struct rf_tac_search_score s = {0};
    float output[RF_TAC_MAX_SQUAD] = {0}, distances[RF_TAC_MAX_SQUAD] = {0};
    float before[RF_TAC_MAX_SQUAD] = {0};
    struct rf_tac_unit_view enemy[RF_TAC_MAX_SQUAD];
    int alive[RF_TAC_MAX_SQUAD] = {0}, i, j, survivors = 0;
    float friendly_loss = f->health_before[0] - f->health_after[0];
    float enemy_loss = f->health_before[1] - f->health_after[1];
    const float tail = 3.0f;
    s.health = p->aggression*enemy_loss - p->safety*friendly_loss;
    for (i = 0; i < o->count; ++i) enemy[i] = f->enemy[i].after;
    for (i = 0; i < o->count; ++i) if (f->friendly[i].after.alive) {
        const struct rf_tac_forecast_unit *u = &f->friendly[i];
        const struct rf_tac_action *a = &plan->actions[i];
        int ci = a->kind == RF_TAC_MOVE ? a->candidate : 0;
        int target = a->kind == RF_TAC_FIRE ?
            (enemy[a->target].alive ? a->target : -1) :
            nearest_target(enemy, u->destination_relations, o->count);
        float available = positive(tail - u->destination_fire_ready_time_s);
        float incoming = 0;
        alive[i] = 1; before[i] = root_remaining(o, i, 0);
        ++survivors;
        distances[i] = remaining(u->objective_distance, u->objective_path_distance, o->order.radius);
        if (target >= 0) output[target] += u->destination_relations[target].expected_dps*available;
        /* Short-term damage is already in health. Charge only the unsimulated
         * route suffix here; MOVE cannot earn fire during its remaining travel. */
        for (j = 0; j < o->count; ++j)
            incoming += u->destination_reverse_relations[j].expected_dps*
                positive(tail - u->destination_incoming_ready_time_s[j]);
        s.risk -= p->safety*(u->remaining_path_incoming_damage + incoming)*0.20f;
        s.cohesion += p->cover*o->candidates[i][ci].cover_quality -
            p->movement*u->remaining_move_time_s;
        if (a->kind == RF_TAC_MOVE) for (j = 0; j < i; ++j)
            if (f->friendly[j].after.alive && plan->actions[j].kind == RF_TAC_MOVE &&
                length(plan->destinations[i], plan->destinations[j]) < 1.2f)
                s.cohesion -= 2;
    }
    for (j = 0; j < o->count; ++j) if (enemy[j].alive)
        s.firepower += p->aggression*kill_value(output[j], enemy[j].effective_health, p->focus)*0.20f;
    s.progress = p->progress*survivors*(group_distance(before, alive, o->count) -
        group_distance(distances, alive, o->count))*(o->order.kind == RF_TAC_ATTACK ? 1 : 0.15f);
    /* Mean-state death is not a certain kill after probabilistic fire. Keep
     * its continuous health estimate, but never award a hard terminal outcome
     * unless all simulated damage events are deterministic in this model. */
    if (!f->uncertain_shots && f->finished && f->winner == o->team) s.terminal = 10000;
    else if (!f->uncertain_shots && f->finished && f->winner >= 0) s.terminal = -10000;
    total(&s); return s;
}

static void entry(struct rf_tac_beam_entry *out, const struct beam_node *node)
{
    memset(out, 0, sizeof(*out));
    out->parent_rank = node->parent; out->changed_unit = node->changed;
    memcpy(out->actions, node->plan.actions, sizeof(out->actions));
    memcpy(out->destinations, node->plan.destinations, sizeof(out->destinations));
    out->score = node->score;
}
static void insert(struct beam_node *nodes, int *count, int width, const struct beam_node *node)
{
    int at = 0, i, n = *count;
    /* Strict comparison and stable enumeration give deterministic ties. */
    while (at < n && nodes[at].score.total >= node->score.total) ++at;
    if (at >= width) return;
    if (n < width) ++n;
    for (i = n - 1; i > at; --i) nodes[i] = nodes[i - 1];
    nodes[at] = *node; *count = n;
}
static int same_plan(const struct rf_tac_plan *a, const struct rf_tac_plan *b)
{
    return !memcmp(a->actions, b->actions, sizeof(a->actions)) &&
           !memcmp(a->destinations, b->destinations, sizeof(a->destinations));
}
static int better_forecast(const struct rf_tac_search_score *a, const struct rf_tac_search_score *b)
{
    int outcome_a = (a->terminal > 0) - (a->terminal < 0);
    int outcome_b = (b->terminal > 0) - (b->terminal < 0);
    /* Terminal outcomes outrank even legal maximum weights. */
    return outcome_a != outcome_b ? outcome_a > outcome_b : a->total > b->total;
}

void rf_tac_solve_with_predictor(const struct rf_tac_observation *o,
                                const struct rf_tac_policy *p,
                                const struct rf_tac_predictor *provider,
                                struct rf_tac_plan *out,
                                struct rf_tac_decision_trace *trace)
{
    struct beam_node current[RF_TAC_BEAM_MAX_WIDTH], next[RF_TAC_BEAM_MAX_WIDTH], hold, winner;
    struct rf_tac_forecast f;
    int count = 1, unit, used = 0, calls = 0, steps = 0, selected = -1, exhausted = 0;
    int forecast_cost, reserve;
    if (p->solver != RF_TAC_BEAM) { rf_tac_solve(o, p, out, trace); return; }
    rf_tac_plan_hold(o, out);
    if (trace) {
        memset(trace, 0, sizeof(*trace)); trace->beam_selected_rank = -1;
        for (unit = 0; unit < RF_TAC_MAX_SQUAD; ++unit) {
            int k; trace->targets[unit] = -1;
            for (k = 0; k < RF_TAC_CANDIDATES; ++k) trace->candidate_scores[unit][k] = -1e20f;
        }
    }
    if (!rf_tac_policy_validate(p)) return;
    if (!provider || !provider->evaluate || !provider->remaining_steps) {
        if (trace) trace->prediction_unavailable = 1;
        return;
    }
    forecast_cost = (p->beam_horizon_ms+RF_TAC_DT_MS-1)/RF_TAC_DT_MS + 1;
    reserve = 2*forecast_cost;
    if (p->budget < forecast_cost || provider->remaining_steps(provider->opaque) < forecast_cost - 1) {
        out->budget_exhausted = 1; return;
    }
    memset(&hold, 0, sizeof(hold)); hold.plan = *out; hold.changed = hold.parent = -1;
    current[0] = hold;
    for (unit = 0; unit < o->count; ++unit) {
        struct beam_option options[RF_TAC_BEAM_MAX_BRANCHES];
        int option_count, i, j, retained = 0, expanded = 0;
        if (!o->friendly[unit].alive) continue;
        option_count = options_for(o, p, unit, options);
        if (p->budget - used <= reserve) { exhausted = 1; break; }
        for (i = 0; i < count; ++i) for (j = 0; j < option_count; ++j) {
            struct beam_node child = current[i];
            if (p->budget - used <= reserve) { exhausted = 1; break; }
            child.plan.actions[unit] = options[j].action;
            child.plan.destinations[unit] = options[j].destination;
            child.score = cheap_score(o, p, &child.plan); child.parent = i; child.changed = unit;
            ++used; ++expanded;
            insert(next, &retained, p->beam_width, &child);
            if (trace && options[j].action.kind == RF_TAC_MOVE)
                trace->candidate_scores[unit][options[j].action.candidate] = child.score.total;
        }
        if (!retained) break;
        memcpy(current, next, retained*sizeof(*current)); count = retained;
        if (trace) {
            struct rf_tac_beam_layer *layer = &trace->beam_layers[trace->beam_layer_count++];
            layer->unit = unit; layer->expanded = expanded; layer->retained = retained;
            for (i = 0; i < retained; ++i) entry(&layer->entries[i], &current[i]);
        }
        if (exhausted) break;
    }
    winner = hold;
    /* Compare all plans in the same model and horizon, including the incumbent
     * HOLD. Unsimulated prefixes can never replace a forecasted incumbent. */
    if (provider->evaluate(provider->opaque, &hold.plan, NULL, p->beam_horizon_ms, &f) != RF_TAC_PRED_OK) {
        if (trace) trace->prediction_unavailable = 1;
        out->evaluations = used; return;
    }
    used += f.steps + 1; steps += f.steps; ++calls;
    winner.score = forecast_score(o, p, &hold.plan, &f);
    if (trace) {
        trace->beam_hold_score = winner.score;
        trace->beam_hold_uncertain_shots = f.uncertain_shots;
    }
    {
        struct rf_tac_beam_layer *layer = trace ? &trace->beam_layers[trace->beam_layer_count++] : NULL;
        int i;
        if (layer) layer->unit = -1;
        for (i = 0; i < count; ++i) {
            struct beam_node evaluated = current[i];
            if (same_plan(&evaluated.plan, &hold.plan)) continue;
            if (p->budget - used < forecast_cost ||
                provider->remaining_steps(provider->opaque) < forecast_cost - 1) { exhausted = 1; break; }
            if (provider->evaluate(provider->opaque, &evaluated.plan, NULL, p->beam_horizon_ms, &f) != RF_TAC_PRED_OK) {
                if (trace) trace->prediction_unavailable = 1;
                break;
            }
            used += f.steps + 1; steps += f.steps; ++calls;
            evaluated.score = forecast_score(o, p, &evaluated.plan, &f);
            evaluated.parent = i; evaluated.changed = -1;
            if (layer) {
                struct rf_tac_beam_entry *record = &layer->entries[layer->retained++];
                entry(record, &evaluated); record->forecasted = 1; record->forecast_ms = f.elapsed_ms;
                record->uncertain_shots = f.uncertain_shots;
                record->predicted_health[0] = f.health_after[0]; record->predicted_health[1] = f.health_after[1];
                ++layer->expanded;
            }
            if (better_forecast(&evaluated.score, &winner.score)) { winner = evaluated; selected = i; }
        }
    }
    *out = winner.plan; out->evaluations = used; out->budget_exhausted = exhausted;
    out->prediction_calls = calls; out->prediction_steps = steps;
    if (trace) {
        trace->prediction_calls = calls; trace->prediction_steps = steps;
        trace->beam_selected_rank = selected; trace->beam_selected_score = winner.score;
        for (unit = 0; unit < o->count; ++unit) {
            trace->selected[unit] = out->actions[unit].kind == RF_TAC_MOVE ? out->actions[unit].candidate : 0;
            trace->targets[unit] = out->actions[unit].target;
            trace->selected_scores[unit] = winner.score.total;
        }
    }
}
