/* Game-owned fabrication. No clock, asset loader, renderer or allocation. */
static int weaver_number(double value)
{
    return value >= 0.0 && value <= 1.0e30;
}

static double weaver_min(double a, double b) { return a < b ? a : b; }

void toy_mesh_weaver_defaults(struct toy_mesh_weaver *w)
{
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->coefficients.work_per_vertex = 1.0;
    w->coefficients.work_per_triangle = 1.5;
    w->coefficients.work_per_texture_mib = 8.0;
    w->coefficients.kj_per_m3 = 100000.0;
    w->coefficients.kj_per_vertex = 0.001;
    w->coefficients.kj_per_triangle = 0.0005;
    w->coefficients.work_per_round = 0.25;
    w->coefficients.kj_per_round = 0.05;
    w->coefficients.calibration_ms = 300.0;
    w->coefficients.delivery_ms = 350.0;
    w->supply.power_on = w->supply.cpu_on = w->supply.x1_on = 1;
    w->supply.source_kw = 25.0;
    w->supply.energy_kj = 20000.0;
    w->supply.cpu_ghz = 3.2;
    w->supply.cpu_work_per_ghz = 25.0;
    w->supply.x1_work_per_sec = 520.0;
    w->supply.compute_limit = 600.0;
    w->supply.base_kw = 0.35;
    w->supply.cpu_kw = 0.065;
    w->supply.x1_kw = 0.20;
    w->supply.storage_bytes = 64ULL * 1024 * 1024;
}

int toy_mesh_weaver_cost(const struct toy_mesh_blueprint *b,
    const struct toy_mesh_weaver_coefficients *c, struct toy_mesh_weaver_cost *out)
{
    const struct toy_game_weapon_info *weapon;
    struct toy_mesh_weaver_cost cost;
    if (!b || !c || !out || b->weapon < TOY_GAME_WEAPON_PISTOL ||
        b->weapon > TOY_GAME_WEAPON_AWP || !b->vertex_count ||
        !b->triangle_count || !b->volume_mm3 || !b->bounds_mm[0] ||
        !b->bounds_mm[1] || !b->bounds_mm[2]) return TOY_WEAVER_BAD_BLUEPRINT;
    if (!weaver_number(c->work_per_vertex) ||
        !weaver_number(c->work_per_triangle) ||
        !weaver_number(c->work_per_texture_mib) ||
        !weaver_number(c->kj_per_m3) || !weaver_number(c->kj_per_vertex) ||
        !weaver_number(c->kj_per_triangle) || !weaver_number(c->work_per_round) ||
        !weaver_number(c->kj_per_round) || !weaver_number(c->calibration_ms) ||
        !weaver_number(c->delivery_ms)) return TOY_WEAVER_BAD_BLUEPRINT;
    weapon = toy_game_weapon_info(b->weapon);
    memset(&cost, 0, sizeof(cost));
    cost.initial_rounds = weapon->mag_size;
    cost.work = c->work_per_vertex * b->vertex_count +
        c->work_per_triangle * b->triangle_count +
        c->work_per_texture_mib * ((double)b->texture_bytes / 1048576.0) +
        c->work_per_round * cost.initial_rounds;
    cost.form_kj = c->kj_per_m3 * ((double)b->volume_mm3 / 1000000000.0) +
        c->kj_per_vertex * b->vertex_count +
        c->kj_per_triangle * b->triangle_count +
        c->kj_per_round * cost.initial_rounds;
    cost.storage_bytes = (unsigned long long)b->vertex_count * 12 +
        (unsigned long long)b->triangle_count * 12;
    if (b->texture_bytes > ~0ULL - cost.storage_bytes)
        return TOY_WEAVER_BAD_BLUEPRINT;
    cost.storage_bytes += b->texture_bytes;
    if (!(cost.work > 0.0) || !(cost.form_kj > 0.0) ||
        !weaver_number(cost.work) || !weaver_number(cost.form_kj))
        return TOY_WEAVER_BAD_BLUEPRINT;
    *out = cost;
    return TOY_WEAVER_OK;
}

static double weaver_base_power(const struct toy_mesh_weaver *w)
{
    return w->supply.base_kw +
        (w->supply.cpu_on ? w->supply.cpu_kw : 0.0) +
        (w->supply.cpu_on && w->supply.x1_on ? w->supply.x1_kw : 0.0);
}

double toy_mesh_weaver_compute(const struct toy_mesh_weaver *w)
{
    double compute;
    if (!w || !w->supply.power_on || !w->supply.cpu_on) return 0.0;
    compute = w->supply.cpu_ghz * w->supply.cpu_work_per_ghz +
        (w->supply.x1_on ? w->supply.x1_work_per_sec : 0.0);
    if (!weaver_number(compute) || !weaver_number(w->supply.compute_limit))
        return 0.0;
    return weaver_min(compute, w->supply.compute_limit);
}

double toy_mesh_weaver_form_power(const struct toy_mesh_weaver *w)
{
    double base, power;
    if (!w || !w->supply.power_on) return 0.0;
    base = weaver_base_power(w);
    power = w->supply.source_kw - base;
    return weaver_number(base) && weaver_number(power) ? power : 0.0;
}

double toy_mesh_weaver_estimate(const struct toy_mesh_weaver *w,
    const struct toy_mesh_weaver_cost *cost)
{
    double compute = toy_mesh_weaver_compute(w);
    double power = toy_mesh_weaver_form_power(w), time;
    if (!w || !cost || compute <= 0.0 || power <= 0.0 ||
        !weaver_number(cost->work) || !weaver_number(cost->form_kj)) return -1.0;
    time = cost->work / compute;
    if (cost->form_kj / power > time) time = cost->form_kj / power;
    return time + (w->coefficients.calibration_ms +
        w->coefficients.delivery_ms) / 1000.0;
}

void toy_game_weaver_set_supply(struct toy_game *g,
    const struct toy_mesh_weaver_supply *supply)
{
    double previous_energy;
    if (!g || !supply) return;
    if (!weaver_number(supply->source_kw) || !weaver_number(supply->energy_kj) ||
        !weaver_number(supply->cpu_ghz) || !weaver_number(supply->cpu_work_per_ghz) ||
        !weaver_number(supply->x1_work_per_sec) || !weaver_number(supply->compute_limit) ||
        !weaver_number(supply->base_kw) || !weaver_number(supply->cpu_kw) ||
        !weaver_number(supply->x1_kw)) return;
    previous_energy = g->weaver.supply.energy_kj;
    g->weaver.supply = *supply;
    /* Reapplying an old UI snapshot cannot refill the source reserve. */
    if (!weaver_number(g->weaver.supply.energy_kj))
        g->weaver.supply.energy_kj = 0.0;
    if (g->weaver.supply.energy_kj > previous_energy)
        g->weaver.supply.energy_kj = previous_energy;
    g->weaver.supply.power_on = !!g->weaver.supply.power_on;
    g->weaver.supply.cpu_on = !!g->weaver.supply.cpu_on;
    g->weaver.supply.x1_on = !!g->weaver.supply.x1_on;
}

int toy_game_weaver_start(struct toy_game *g, const struct toy_mesh_blueprint *b)
{
    struct toy_mesh_weaver *w;
    struct toy_mesh_weaver_cost cost;
    int reason;
    if (!g || !g->weaver.enabled || g->state != TOY_GAME_PLAYING)
        return TOY_WEAVER_DISABLED;
    w = &g->weaver;
    if (w->phase == TOY_WEAVER_READY) return TOY_WEAVER_OUTPUT_OCCUPIED;
    if (w->phase != TOY_WEAVER_IDLE) return TOY_WEAVER_BUSY;
    reason = toy_mesh_weaver_cost(b, &w->coefficients, &cost);
    if (reason) return reason;
    /* The presentation uses the same 70-degree deck placement. These are
     * abs(cos(70)) and abs(sin(70)); bounds include the actual weapon width.
     * Never rescale a product merely to fit the ordinary 1.02 m envelope. */
    if (b->bounds_mm[1] > TOY_WEAVER_ENVELOPE_MM ||
        b->bounds_mm[0] * 0.342020143326 +
            b->bounds_mm[2] * 0.939692620786 > TOY_WEAVER_ENVELOPE_MM ||
        b->bounds_mm[0] * 0.939692620786 +
            b->bounds_mm[2] * 0.342020143326 > TOY_WEAVER_ENVELOPE_MM)
        return TOY_WEAVER_TOO_LARGE;
    if (cost.storage_bytes > w->supply.storage_bytes) return TOY_WEAVER_NO_STORAGE;
    if (!w->supply.power_on || !weaver_number(w->supply.energy_kj) ||
        w->supply.energy_kj <= 0.0 || toy_mesh_weaver_form_power(w) <= 0.0)
        return TOY_WEAVER_NO_POWER;
    if (toy_mesh_weaver_compute(w) <= 0.0) return TOY_WEAVER_NO_COMPUTE;
    w->blueprint = *b;
    w->cost = cost;
    w->progress = w->phase_ms = w->elapsed_ms = 0.0;
    w->used_compute = w->used_form_kj = w->used_base_kj = 0.0;
    w->pause_reason = TOY_WEAVER_OK;
    w->phase = TOY_WEAVER_CALIBRATING;
    ++w->job_serial;
    if (!w->job_serial) ++w->job_serial;
    return TOY_WEAVER_OK;
}

void toy_game_weaver_update(struct toy_game *g, int dt_ms)
{
    struct toy_mesh_weaver *w;
    double remaining;
    if (!g || dt_ms <= 0 || g->state != TOY_GAME_PLAYING) return;
    w = &g->weaver;
    if (!w->enabled || w->phase == TOY_WEAVER_IDLE ||
        w->phase == TOY_WEAVER_READY) return;
    remaining = (double)dt_ms / 1000.0;
    while (remaining > 1.0e-12 && w->phase != TOY_WEAVER_READY) {
        double compute = toy_mesh_weaver_compute(w);
        double power = toy_mesh_weaver_form_power(w);
        double base = weaver_base_power(w), step, rate = 0.0, draw;
        if (!w->supply.power_on || !weaver_number(w->supply.energy_kj) ||
            w->supply.energy_kj <= 1.0e-12 || power <= 0.0) {
            w->pause_reason = TOY_WEAVER_NO_POWER;
            return;
        }
        if (compute <= 0.0) { w->pause_reason = TOY_WEAVER_NO_COMPUTE; return; }
        if (w->cost.storage_bytes > w->supply.storage_bytes) {
            w->pause_reason = TOY_WEAVER_NO_STORAGE;
            return;
        }
        w->pause_reason = TOY_WEAVER_OK;
        if (w->phase == TOY_WEAVER_WEAVING) {
            rate = weaver_min(compute / w->cost.work, power / w->cost.form_kj);
            step = weaver_min(remaining, (1.0 - w->progress) / rate);
        } else {
            double duration = w->phase == TOY_WEAVER_CALIBRATING ?
                w->coefficients.calibration_ms : w->coefficients.delivery_ms;
            step = weaver_min(remaining, (duration - w->phase_ms) / 1000.0);
        }
        draw = base + rate * w->cost.form_kj;
        if (draw > 0.0) step = weaver_min(step, w->supply.energy_kj / draw);
        if (step < 0.0) step = 0.0;
        if (w->phase == TOY_WEAVER_WEAVING) {
            double progress = weaver_min(rate * step, 1.0 - w->progress);
            w->progress += progress;
            w->used_compute += progress * w->cost.work;
            w->used_form_kj += progress * w->cost.form_kj;
        }
        w->used_base_kj += base * step;
        w->supply.energy_kj -= draw * step;
        if (w->supply.energy_kj < 1.0e-12) w->supply.energy_kj = 0.0;
        w->phase_ms += step * 1000.0;
        w->elapsed_ms += step * 1000.0;
        remaining -= step;
        if ((w->phase == TOY_WEAVER_CALIBRATING &&
             w->phase_ms + 1.0e-7 >= w->coefficients.calibration_ms) ||
            (w->phase == TOY_WEAVER_WEAVING && w->progress >= 1.0 - 1.0e-12) ||
            (w->phase == TOY_WEAVER_DELIVERING &&
             w->phase_ms + 1.0e-7 >= w->coefficients.delivery_ms)) {
            if (w->phase == TOY_WEAVER_WEAVING) w->progress = 1.0;
            ++w->phase;
            w->phase_ms = 0.0;
            if (w->phase == TOY_WEAVER_READY) ++w->produced_count;
        } else if (step <= 1.0e-12 || w->supply.energy_kj <= 0.0) {
            w->pause_reason = TOY_WEAVER_NO_POWER;
            return;
        }
    }
}

int toy_game_weaver_collect(struct toy_game *g, struct toy_game_actor *actor)
{
    const struct toy_game_weapon_info *weapon;
    struct toy_mesh_weaver *w;
    if (!g || !actor || !actor->active || actor->state != TOY_GAME_ACTOR_ALIVE ||
        g->state != TOY_GAME_PLAYING) return 0;
    w = &g->weaver;
    if (!w->enabled || w->phase != TOY_WEAVER_READY) return 0;
    weapon = toy_game_weapon_info_or_null(w->blueprint.weapon);
    if (!weapon || toy_game_actor_equip_weapon(g, actor, w->blueprint.weapon) < 0)
        return 0;
    /* Exactly the paid standard magazine. In particular the old infinite
     * pistol reserve is not inherited by a newly manufactured pistol. */
    actor->slots[weapon->slot].mag = w->cost.initial_rounds;
    actor->slots[weapon->slot].reserve = 0;
    actor->current_slot = weapon->slot;
    actor->reloading = actor->reload_timer_ms = 0;
    actor->weapon_switch_timer_ms = TOY_CONFIG_WEAPON_SWITCH_MS;
    w->phase = TOY_WEAVER_IDLE;
    w->pause_reason = TOY_WEAVER_OK;
    ++w->collected_count;
    toy_game_emit_event(g, TOY_GAME_EV_PICKUP);
    return 1;
}

const char *toy_mesh_weaver_reason_name(int reason)
{
    static const char *names[] = { "READY", "OFFLINE ONLY", "POWER UNAVAILABLE",
        "COMPUTE UNAVAILABLE", "BLUEPRINT STORAGE FULL", "INVALID BLUEPRINT",
        "EXCEEDS 1.02 M ENVELOPE", "TASK IN PROGRESS", "COLLECT FINISHED WEAPON",
        "NO FINISHED WEAPON" };
    return (unsigned)reason < sizeof(names) / sizeof(names[0]) ? names[reason] : "ERROR";
}

const char *toy_mesh_weaver_phase_name(int phase)
{
    static const char *names[] = { "IDLE", "CALIBRATING", "WEAVING", "DELIVERING", "READY TO COLLECT" };
    return (unsigned)phase < sizeof(names) / sizeof(names[0]) ? names[phase] : "ERROR";
}
