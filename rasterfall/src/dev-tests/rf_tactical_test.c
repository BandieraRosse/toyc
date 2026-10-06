/* Contracts for the standalone tactical lab; no renderer or old game fixture. */
#include "rf_tactical.h"
#include "rf_tactical_weapon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int checked, failed;
#define CHECK(condition, name) do { ++checked; if (!(condition)) { \
    ++failed; fprintf(stderr, "TACTICAL-TEST failed: %s (line %d)\n", name, __LINE__); } } while (0)

static float test_distance(struct rf_tac_vec a, struct rf_tac_vec b) {
    float x = a.x - b.x, y = a.y - b.y; return sqrtf(x*x + y*y);
}
static void test_think(struct rf_tac_world *w, int solver, int budget) {
    struct rf_tac_observation o[2];
    struct rf_tac_plan p[2];
    struct rf_tac_policy policy;
    int t;
    rf_tac_policy_default(&policy, solver); policy.budget = budget;
    for (t = 0; t < 2; ++t) {
        rf_tac_observe(w, t, &o[t]); rf_tac_solve(&o[t], &policy, &p[t], NULL);
        CHECK(p[t].evaluations >= 0 && p[t].evaluations <= budget, "logical solver budget is bounded");
    }
    for (t = 0; t < 2; ++t) CHECK(rf_tac_apply(w, &p[t]), "valid simultaneous plans");
}

static unsigned int test_candidate_issues(const struct rf_tac_observation *o, int unit, int index) {
    const struct rf_tac_candidate *c = &o->candidates[unit][index];
    unsigned int issues = 0;
    float direct = test_distance(o->friendly[unit].pos, c->pos);
    if (!isfinite(c->pos.x) || !isfinite(c->pos.y)) issues |= 1u << 0;
    if (c->kind < RF_TAC_CURRENT || c->kind > RF_TAC_CONTINUE) issues |= 1u << 1;
    if (!isfinite(c->objective_distance) || c->objective_distance < 0) issues |= 1u << 2;
    if (!isfinite(c->objective_path_distance)) issues |= 1u << 3;
    if (c->objective_path_distance + 0.02f < c->objective_distance) issues |= 1u << 4;
    if (c->kind == RF_TAC_ADVANCE &&
        c->objective_path_distance > o->candidates[unit][0].objective_path_distance + 0.02f)
        issues |= 1u << 5;
    if (!isfinite(c->path.move_distance) || c->path.move_distance + 0.02f < direct) issues |= 1u << 6;
    if (!isfinite(c->path.travel_time) || c->path.travel_time < 0) issues |= 1u << 7;
    if (!isfinite(c->path.exposed_time) || c->path.exposed_time < 0) issues |= 1u << 8;
    if (c->path.exposed_time > c->path.travel_time + 0.02f) issues |= 1u << 9;
    if (!isfinite(c->path.incoming_damage) || c->path.incoming_damage < 0) issues |= 1u << 10;
    return issues;
}

static void test_candidate_diagnostic(unsigned int seed, int squad,
                                       const struct rf_tac_observation *o,
                                       int unit, int index, unsigned int issues) {
    static const char *names[] = {"finite_position", "legal_kind", "nonnegative_finite_euclidean_distance",
        "finite_objective_path_distance", "objective_route_not_shorter_than_euclidean",
        "advance_route_does_not_increase", "movement_route_not_shorter_than_direct",
        "nonnegative_finite_travel_time", "nonnegative_finite_exposed_time", "exposed_time_within_travel",
        "nonnegative_finite_incoming_damage", "current_candidate_exists", "advance_candidate_exists"};
    const struct rf_tac_candidate *c = index >= 0 ? &o->candidates[unit][index] : NULL;
    int bit;
    fprintf(stderr, "TACTICAL-TEST candidate seed=%u squad=%d tick=%d time_ms=%d unit=%d index=%d condition=",
            seed, squad, o->tick, o->time_ms, unit, index);
    for (bit = 0; bit < 13; ++bit) if (issues & (1u << bit)) fprintf(stderr, "%s,", names[bit]);
    if (c) fprintf(stderr, " kind=%d objective_euclidean=%.9g objective_path=%.9g current_path=%.9g"
                   " move_direct=%.9g move_path=%.9g travel=%.9g exposed=%.9g incoming_damage=%.9g",
            c->kind, c->objective_distance, c->objective_path_distance,
            o->candidates[unit][0].objective_path_distance, test_distance(o->friendly[unit].pos, c->pos),
            c->path.move_distance, c->path.travel_time, c->path.exposed_time, c->path.incoming_damage);
    fputc('\n', stderr);
}

static void test_generated_navigation(struct rf_tac_map *map, unsigned int seed, int squad) {
    struct rf_tac_world w;
    struct rf_tac_observation o;
    int i, k, elapsed, valid_facts = 1, solid_entry = 0;
    unsigned int diagnosed = 0;
    CHECK(rf_tac_map_generate(map, seed), "navigation regression map generation");
    CHECK(rf_tac_world_init(&w, map, 777, squad, RF_TW_RIFLE, 60000), "navigation regression squad init");
    CHECK(rf_tac_command(&w, 0, RF_TAC_ATTACK, map->objective, map->objective_radius), "navigation regression attack");
    for (i = 0; i < squad; ++i) w.units[RF_TAC_MAX_SQUAD + i].alive = 0;
    for (elapsed = 0; elapsed < 60000 && !w.finished; elapsed += RF_TAC_DT_MS) {
        if (elapsed % RF_TAC_THINK_MS == 0) {
            rf_tac_observe(&w, 0, &o);
            for (i = 0; i < squad; ++i) {
                int advance_found = 0;
                if (!o.candidate_count[i] || o.candidates[i][0].kind != RF_TAC_CURRENT) {
                    if (!(diagnosed & (1u << 11))) test_candidate_diagnostic(seed, squad, &o, i,
                            o.candidate_count[i] ? 0 : -1, 1u << 11);
                    diagnosed |= 1u << 11;
                    valid_facts = 0; continue;
                }
                for (k = 0; k < o.candidate_count[i]; ++k)
                    if (o.candidates[i][k].kind == RF_TAC_ADVANCE) advance_found = 1;
                if (o.candidates[i][0].objective_path_distance > o.order.radius && !advance_found) {
                    if (!(diagnosed & (1u << 12))) test_candidate_diagnostic(seed, squad, &o, i, 0, 1u << 12);
                    diagnosed |= 1u << 12;
                    valid_facts = 0;
                }
            }
            for (i = 0; i < squad; ++i) for (k = 0; k < o.candidate_count[i]; ++k) {
                unsigned int issues = test_candidate_issues(&o, i, k);
                if (issues) {
                    if (issues & ~diagnosed) test_candidate_diagnostic(seed, squad, &o, i, k, issues);
                    diagnosed |= issues;
                    valid_facts = 0;
                }
            }
            test_think(&w, RF_TAC_SIMPLE, 128);
        }
        rf_tac_step(&w);
        for (i = 0; i < squad; ++i) for (k = 0; k < map->cover_count; ++k) {
            const struct rf_tac_cover *box = &map->covers[k];
            if (w.units[i].pos.x > box->x0 && w.units[i].pos.x < box->x1 &&
                w.units[i].pos.y > box->y0 && w.units[i].pos.y < box->y1) solid_entry = 1;
        }
    }
    CHECK(valid_facts, "replanned navigation candidate facts remain physically meaningful");
    CHECK(!solid_entry, "generated navigation never moves through solid cover");
    if (!w.orders[0].captured) {
        fprintf(stderr, "TACTICAL-TEST navigation regression seed=%u squad=%d elapsed=%d\n", seed, squad, w.time_ms);
        for (i = 0; i < squad; ++i)
            fprintf(stderr, "  unit=%d objective_distance=%.3f action=%d\n", i,
                    test_distance(w.units[i].pos, map->objective), w.units[i].action.kind);
    }
    CHECK(w.finished && w.winner == 0 && w.orders[0].captured && w.orders[0].kind == RF_TAC_DEFEND,
          "regression seed full squad replans and captures within sixty seconds");
    for (i = 0; i < squad; ++i)
        CHECK(w.units[i].alive && test_distance(w.units[i].pos, map->objective) <= map->objective_radius,
              "every generated-map squad member reaches the capture area");
}

int rf_tac_run_tests(void) {
    struct rf_tac_map *m = (struct rf_tac_map *)malloc(sizeof(*m));
    struct rf_tac_map *copy = (struct rf_tac_map *)malloc(sizeof(*copy));
    struct rf_tac_world w, a, b;
    struct rf_tac_observation o;
    struct rf_tac_plan p;
    struct rf_tac_policy policy;
    struct rf_tac_decision_trace trace;
    unsigned int original_hash;
    int i, size;
    checked = failed = 0;
    if (!m || !copy) { free(m); free(copy); return 0; }
    CHECK(rf_tac_map_generate(m, 100), "map generation");
    CHECK(rf_tac_map_generate(copy, 100), "same seed map generation");
    CHECK(!memcmp(m, copy, sizeof(*m)), "map seed determinism");
    CHECK(rf_tac_map_generate(copy, 101), "alternative map generation");
    CHECK(memcmp(m->covers, copy->covers, sizeof(m->covers)) != 0, "map seed changes geometry");
    for (size = 4; size <= 6; ++size) {
        CHECK(rf_tac_world_init(&w, m, 1337, size, RF_TW_RIFLE, 60000), "variable squad size");
        CHECK(w.orders[0].kind == RF_TAC_DEFEND && w.orders[1].kind == RF_TAC_DEFEND,
              "defend is default");
        CHECK(w.units[RF_TAC_MAX_SQUAD + size - 1].alive, "fixed-stride roster is populated");
    }
    CHECK(rf_tac_world_init(&w, m, 1337, 4, RF_TW_RIFLE, 60000), "world initialization");
    CHECK(rf_tac_command(&w, 0, RF_TAC_ATTACK, m->objective, m->objective_radius), "attack command");
    original_hash = rf_tac_hash(&w);
    rf_tac_observe(&w, 0, &o);
    CHECK(rf_tac_hash(&w) == original_hash, "observation does not mutate world or RNG");
    rf_tac_policy_default(&policy, RF_TAC_UTILITY); policy.budget = 0;
    rf_tac_solve(&o, &policy, &p, &trace);
    for (i = 0; i < 4; ++i) CHECK(p.actions[i].kind == RF_TAC_HOLD, "zero budget produces HOLD");
    CHECK(p.evaluations == 0, "zero budget consumes no evaluations");
    {
        struct rf_tac_observation small = o;
        /* One CURRENT candidate and no live opponents let exactly the first
         * member be evaluated. Empty magazines must not bypass the fallback
         * for the remaining members after a positive budget is exhausted. */
        for (i = 0; i < small.count; ++i) {
            small.friendly[i].ammo = 0; small.friendly[i].reload_ms = 0;
            small.enemy[i].alive = 0; small.candidate_count[i] = 1;
        }
        policy.budget = 1;
        rf_tac_solve(&small, &policy, &p, &trace);
        CHECK(p.evaluations == 1 && p.evaluations <= policy.budget, "positive tiny budget is used without overflow");
        for (i = 1; i < small.count; ++i)
            CHECK(p.actions[i].kind == RF_TAC_HOLD, "unevaluated empty-magazine members stay HOLD after budget exhaustion");
    }
    rf_tac_plan_hold(&o, &p); p.generation ^= 1;
    w.units[0].action.kind = RF_TAC_MOVE;
    CHECK(!rf_tac_apply(&w, &p) && w.units[0].action.kind == RF_TAC_HOLD, "stale generation falls back HOLD");
    rf_tac_observe(&w, 0, &o); rf_tac_plan_hold(&o, &p);
    p.actions[0].kind = RF_TAC_MOVE; p.actions[0].candidate = RF_TAC_CANDIDATES + 1;
    CHECK(!rf_tac_apply(&w, &p) && w.units[0].action.kind == RF_TAC_HOLD, "illegal move falls back HOLD");
    rf_tac_observe(&w, 0, &o); rf_tac_plan_hold(&o, &p); p.version = 0;
    CHECK(!rf_tac_apply(&w, &p), "version mismatch rejected");
    rf_tac_observe(&w, 0, &o); rf_tac_plan_hold(&o, &p); p.team = 1;
    CHECK(!rf_tac_apply(&w, &p), "changing team cannot reuse another team's plan generation");
    rf_tac_observe(&w, 0, &o); rf_tac_plan_hold(&o, &p);
    p.actions[0].kind = RF_TAC_FIRE; p.actions[0].target = 100;
    CHECK(!rf_tac_apply(&w, &p) && w.units[0].action.kind == RF_TAC_HOLD, "illegal fire target falls back HOLD");
    rf_tac_observe(&w, 0, &o); rf_tac_plan_hold(&o, &p);
    CHECK(rf_tac_command(&w, 0, RF_TAC_DEFEND, m->objective, m->objective_radius), "replace order at same time");
    CHECK(!rf_tac_apply(&w, &p), "same-time old command revision is rejected");
    rf_tac_observe(&w, 0, &o); rf_tac_plan_hold(&o, &p);
    rf_tac_step(&w);
    CHECK(w.tick == o.tick && !rf_tac_apply(&w, &p), "old plan rejected within same tactical tick");
    original_hash = rf_tac_hash(&w); a = w; b = w;
    for (i = 0; i < 100; ++i) {
        if (i % (RF_TAC_THINK_MS / RF_TAC_DT_MS) == 0) { test_think(&a, RF_TAC_MECHANICAL, 128); test_think(&b, RF_TAC_MECHANICAL, 128); }
        rf_tac_step(&a); rf_tac_step(&b);
    }
    CHECK(rf_tac_hash(&a) == rf_tac_hash(&b), "cloned rollout deterministic final hash");
    CHECK(rf_tac_hash(&w) == original_hash, "cloned rollout leaves source unchanged");

    /* Actual movement uses generated navigation and repeatedly replans, with
     * defenders removed only to isolate capture/navigation from firepower. */
    CHECK(rf_tac_world_init(&w, m, 777, 4, RF_TW_RIFLE, 60000), "navigation fixture init");
    CHECK(rf_tac_command(&w, 0, RF_TAC_ATTACK, m->objective, m->objective_radius), "navigation attack");
    for (i = 0; i < 4; ++i) w.units[RF_TAC_MAX_SQUAD + i].alive = 0;
    {
        float initial = test_distance(w.units[0].pos, m->objective);
        int elapsed;
        for (elapsed = 0; elapsed < 50000 && !w.finished; elapsed += RF_TAC_DT_MS) {
            if (elapsed % RF_TAC_THINK_MS == 0) test_think(&w, RF_TAC_SIMPLE, 128);
            rf_tac_step(&w);
            for (i = 0; i < 4; ++i) {
                int c;
                for (c = 0; c < m->cover_count; ++c) {
                    const struct rf_tac_cover *box = &m->covers[c];
                    CHECK(!(w.units[i].pos.x > box->x0 && w.units[i].pos.x < box->x1 &&
                            w.units[i].pos.y > box->y0 && w.units[i].pos.y < box->y1), "movement never enters solid cover");
                }
            }
            if (elapsed == 19980) CHECK(initial - test_distance(w.units[0].pos, m->objective) > 20,
                                        "frequent replanning makes sustained progress");
        }
        CHECK(w.orders[0].captured && w.orders[0].kind == RF_TAC_DEFEND && w.winner == 0,
              "actual attack movement clears captures and becomes defend");
        CHECK(w.time_ms >= 2000, "capture requires sustained occupation");
    }

    /* Seeds that exposed obstacle-edge stalls must work at every supported
     * squad size; only successful physical occupation satisfies this contract. */
    for (size = 4; size <= 6; ++size) {
        test_generated_navigation(copy, 106, size);
        test_generated_navigation(copy, 107, size);
    }

    /* Precise geometry remains directional and distinguishes low/high cover. */
    memcpy(copy, m, sizeof(*m)); copy->cover_count = 1;
    copy->covers[0].x0 = 30; copy->covers[0].x1 = 31;
    copy->covers[0].y0 = 15; copy->covers[0].y1 = 25; copy->covers[0].height = RF_TAC_LOW;
    {
        struct rf_tac_vec front = {20, 20}, behind = {31.5f, 20}, back = {40, 20};
        float low = rf_tac_exposure(copy, front, behind);
        float unprotected = rf_tac_exposure(copy, back, behind);
        copy->covers[0].height = RF_TAC_HIGH;
        CHECK(low > rf_tac_exposure(copy, front, behind), "high wall blocks more than low cover");
        CHECK(unprotected > low, "cover only protects its facing direction");
    }

    copy->cover_count = 0;
    CHECK(rf_tac_world_init(&w, copy, 42, 1, RF_TW_RIFLE, 20000), "simultaneous fixture init");
    w.units[0].pos.x = 30; w.units[0].pos.y = 20;
    w.units[RF_TAC_MAX_SQUAD].pos.x = 31; w.units[RF_TAC_MAX_SQUAD].pos.y = 20;
    w.units[0].hp = w.units[RF_TAC_MAX_SQUAD].hp = 1;
    w.units[0].evasion = w.units[RF_TAC_MAX_SQUAD].evasion = 0;
    w.units[0].recovery_ms = w.units[RF_TAC_MAX_SQUAD].recovery_ms = 3000;
    rf_tac_step(&w);
    CHECK(w.units[0].shots == 1 && w.units[RF_TAC_MAX_SQUAD].shots == 1,
          "both soldiers fire before simultaneous damage");
    CHECK(!w.units[0].alive && !w.units[RF_TAC_MAX_SQUAD].alive && w.winner == -1,
          "simultaneous lethal fire is a draw");
    CHECK(rf_tac_world_init(&w, m, 42, 4, RF_TW_RIFLE, 60000), "reverse attacker fixture init");
    CHECK(rf_tac_command(&w, 1, RF_TAC_ATTACK, m->spawn[0], m->objective_radius), "reverse attack command");
    for (i = 0; i < 4; ++i) w.units[i].alive = 0;
    rf_tac_step(&w);
    CHECK(!w.finished && !w.orders[1].captured, "reverse attacker must occupy after clearing enemies");
    for (i = 1; i < 2500 && !w.finished; ++i) {
        if (w.time_ms % RF_TAC_THINK_MS == 0) test_think(&w, RF_TAC_SIMPLE, 128);
        rf_tac_step(&w);
    }
    CHECK(w.winner == 1 && w.orders[1].captured && w.orders[1].kind == RF_TAC_DEFEND,
          "reverse attacker physically reaches captures and defends");
    for (size = 0; size < 2; ++size) {
        CHECK(rf_tac_world_init(&w, copy, 42, 1, RF_TW_RIFLE, 200), "neutral timeout fixture init");
        if (size) {
            CHECK(rf_tac_command(&w, 0, RF_TAC_ATTACK, m->objective, m->objective_radius), "both attack team zero");
            CHECK(rf_tac_command(&w, 1, RF_TAC_ATTACK, m->spawn[0], m->objective_radius), "both attack team one");
        }
        w.units[0].cooldown_ms = w.units[RF_TAC_MAX_SQUAD].cooldown_ms = 100000;
        for (i = 0; i < 10; ++i) rf_tac_step(&w);
        CHECK(w.finished && w.winner == -1, "equal-order timeout has no team bias");
    }
    CHECK(rf_tac_world_init(&w, copy, 42, 1, RF_TW_RIFLE, 10000), "simultaneous capture fixture init");
    CHECK(rf_tac_command(&w, 0, RF_TAC_ATTACK, w.units[0].pos, 1), "own point capture team zero");
    CHECK(rf_tac_command(&w, 1, RF_TAC_ATTACK, w.units[RF_TAC_MAX_SQUAD].pos, 1), "own point capture team one");
    w.units[0].cooldown_ms = w.units[RF_TAC_MAX_SQUAD].cooldown_ms = 100000;
    for (i = 0; i < 100; ++i) rf_tac_step(&w);
    CHECK(w.finished && w.winner == -1 && w.orders[0].captured && w.orders[1].captured,
          "simultaneous captures produce draw");
    CHECK(rf_tac_world_init(&w, copy, 42, 1, RF_TW_RIFLE, 10000), "reload fixture init");
    w.units[0].pos.x = 30; w.units[0].pos.y = 20;
    w.units[RF_TAC_MAX_SQUAD].pos.x = 31; w.units[RF_TAC_MAX_SQUAD].pos.y = 20;
    w.units[0].ammo = 0; w.units[0].action.kind = RF_TAC_RELOAD;
    w.units[RF_TAC_MAX_SQUAD].cooldown_ms = 100000;
    for (i = 0; i < rf_tw_profile_get(RF_TW_RIFLE)->reload_ms / RF_TAC_DT_MS + 3; ++i) rf_tac_step(&w);
    CHECK(w.units[0].action.kind == RF_TAC_HOLD && w.units[0].shots > 0,
          "finished reload returns to HOLD automatic firing");
    memset(copy->exposure, RF_TW_FULL, sizeof(copy->exposure));
    memset(copy->visible, 255, sizeof(copy->visible));
    CHECK(rf_tac_world_init(&w, copy, 42, 1, RF_TW_RIFLE, 10000), "future path exposure fixture init");
    CHECK(rf_tac_command(&w, 0, RF_TAC_ATTACK, m->objective, m->objective_radius), "future exposure attack");
    w.units[RF_TAC_MAX_SQUAD].reload_ms = RF_TAC_DT_MS;
    rf_tac_observe(&w, 0, &o);
    {
        float incoming = 0;
        for (i = 1; i < o.candidate_count[0]; ++i) incoming += o.candidates[0][i].path.incoming_damage;
        CHECK(incoming > 0, "path predicts firing after enemy reload finishes");
    }
    {
        float hp = RF_TW_BASE_HP, risk = RF_TW_BASE_RISK;
        int recovery = RF_TW_BASE_RECOVERY_DELAY_MS;
        float absorbed = rf_tw_apply_damage(&hp, &risk, 32, 16);
        CHECK(absorbed == 16 && hp == RF_TW_BASE_HP && risk == 29,
              "risk absorbs head damage using body risk cost");
        rf_tw_recover(&risk, &recovery, 3000);
        CHECK(risk == 29 && recovery == 0, "recovery respects seconds-long delay");
        rf_tw_recover(&risk, &recovery, 500);
        CHECK(fabsf(risk - 34) < 0.001f, "risk recovers at shared standard-warrior rate");
        rf_tw_recover(&risk, &recovery, 10000);
        CHECK(risk == RF_TW_BASE_RISK, "risk recovery is capped");
    }
    {
        rf_tw_context context;
        rf_tw_metrics rifle, smg;
        rf_tw_context_reset(&context); context.distance_m = 60;
        rf_tw_pattern_metrics(rf_tw_profile_get(RF_TW_RIFLE), &context, RF_TW_AUTO, &rifle);
        rf_tw_pattern_metrics(rf_tw_profile_get(RF_TW_SMG), &context, RF_TW_AUTO, &smg);
        CHECK(rifle.expected_dps > smg.expected_dps, "rifle has long-distance effectiveness advantage");
        context.distance_m = 5;
        rf_tw_pattern_metrics(rf_tw_profile_get(RF_TW_RIFLE), &context, RF_TW_AUTO, &rifle);
        rf_tw_pattern_metrics(rf_tw_profile_get(RF_TW_SMG), &context, RF_TW_AUTO, &smg);
        CHECK(smg.reload_cycle_dps > rifle.reload_cycle_dps, "SMG has close-distance sustained-fire advantage");
    }
    free(m); free(copy);
    printf("{\"type\":\"self-test\",\"checked\":%d,\"failed\":%d,\"passed\":%s}\n",
           checked, failed, failed ? "false" : "true");
    return failed == 0;
}
