#include "rf_frontier_mission.h"
#include "string.h"
#include "core.h"

#define FRONTIER_PERIOD_MS 20000
#define FRONTIER_PREPARE_MS 45000
#define FRONTIER_PERIOD_CAP 12
#define FRONTIER_RETRY_MS 250
#define FRONTIER_STEP_ATTEMPTS 12

static int frontier_actor_index(const struct toy_game *g, int id)
{
    int i;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++)
        if (g->actors[i].active && g->actors[i].actor_id == id) return i;
    return -1;
}

static int frontier_entity_matches(const struct rf_frontier_entity *e,
                                   const struct toy_game *g)
{
    if (e->actor) {
        const struct toy_game_actor *a;
        if (e->index < 0 || e->index >= TOY_GAME_MAX_ACTORS) return 0;
        a = &g->actors[e->index];
        return a->active && a->actor_id == e->stable_id &&
               a->combat_generation == e->generation;
    }
    if (e->index < 0 || e->index >= TOY_GAME_MAX_ENEMIES) return 0;
    return g->enemies[e->index].active && e->stable_id == e->index &&
           g->enemies[e->index].combat_generation == e->generation;
}

static int frontier_entity_alive(const struct rf_frontier_entity *e,
                                 const struct toy_game *g)
{
    if (!frontier_entity_matches(e, g)) return 0;
    if (e->actor)
        return g->actors[e->index].state == TOY_GAME_ACTOR_ALIVE &&
               g->actors[e->index].hp > 0;
    return g->enemies[e->index].active == 1 && g->enemies[e->index].hp > 0;
}

static void frontier_refresh(struct rf_frontier_mission *m,
                             const struct toy_game *g)
{
    int i;
    m->guards_alive = m->enemies_alive = m->periodic_alive = 0;
    for (i = 0; i < RF_FRONTIER_TRACKED; i++) {
        struct rf_frontier_entity *e = &m->entities[i];
        if (!e->active || e->mission_id != m->mission_id) continue;
        if (!frontier_entity_matches(e, g)) { e->active = 0; continue; }
        if (!frontier_entity_alive(e, g)) continue;
        m->enemies_alive++;
        if (e->role == RF_FRONTIER_GUARD) m->guards_alive++;
        if (e->role == RF_FRONTIER_PERIODIC_INFECTED) m->periodic_alive++;
    }
}

static int frontier_record_slot(const struct rf_frontier_mission *m)
{
    int i;
    for (i = 0; i < RF_FRONTIER_TRACKED; i++)
        if (!m->entities[i].active) return i;
    return -1;
}

static int frontier_period_capacity(const struct rf_frontier_mission *m,
                                     const struct toy_game *g)
{
    int i, records = 0, enemies = 0;
    for (i = 0; i < RF_FRONTIER_TRACKED; i++) records += !m->entities[i].active;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) enemies += !g->enemies[i].active;
    return records >= 3 && enemies >= 3;
}

static void frontier_record(struct rf_frontier_mission *m,
    const struct toy_game *g, int slot, int actor, int index, int role, int ordinal)
{
    struct rf_frontier_entity *e = &m->entities[slot];
    e->active = 1; e->mission_id = m->mission_id;
    e->actor = actor; e->index = index; e->role = role; e->ordinal = ordinal;
    e->stable_id = actor ? g->actors[index].actor_id : index;
    e->generation = actor ? g->actors[index].combat_generation :
                           g->enemies[index].combat_generation;
}

void rf_frontier_mission_reset(struct rf_frontier_mission *m, struct toy_game *g)
{
    int i;
    if (!m) return;
    if (g && m->mission_id) for (i = 0; i < RF_FRONTIER_TRACKED; i++) {
        struct rf_frontier_entity *e = &m->entities[i];
        if (!e->active || e->mission_id != m->mission_id ||
            !frontier_entity_matches(e, g)) continue;
        if (e->actor) memset(&g->actors[e->index], 0, sizeof(g->actors[0]));
        else {
            if (g->enemies[e->index].active == 1 && g->enemies_alive > 0)
                g->enemies_alive--;
            memset(&g->enemies[e->index], 0, sizeof(g->enemies[0]));
        }
    }
    memset(m, 0, sizeof(*m));
}

static int frontier_valid_id(const char *id)
{
    int i;
    if (!id || !id[0]) return 0;
    for (i = 1; i < RF_FRONTIER_ID_CAP; i++) if (!id[i]) return 1;
    return 0;
}

static int frontier_config_valid(const struct rf_frontier_config *c)
{
    const char *ids[RF_FRONTIER_GUARDS + RF_FRONTIER_FACILITIES + 3];
    int i, j, count = 0, elite = 0;
    if (!c || !c->offline_authority) return 0;
    for (i = 0; i < RF_FRONTIER_GUARDS; i++) {
        if (!toy_game_weapon_info_or_null(c->guards[i].weapon) ||
            c->guards[i].guard_radius <= 0 ||
            (c->guards[i].elite != 0 && c->guards[i].elite != 1)) return 0;
        elite += c->guards[i].elite;
        ids[count++] = c->guards[i].point.id;
    }
    if (elite != 2) return 0;
    ids[count++] = c->north.id; ids[count++] = c->east.id; ids[count++] = c->west.id;
    for (i = 0; i < RF_FRONTIER_FACILITIES; i++) {
        if (!c->facilities[i].identity || !c->facilities[i].generation ||
            c->facilities[i].interact_range <= 0) return 0;
        ids[count++] = c->facilities[i].point.id;
    }
    for (i = 0; i < count; i++) {
        if (!frontier_valid_id(ids[i])) return 0;
        for (j = 0; j < i; j++) if (!strcmp(ids[i], ids[j])) return 0;
    }
    return 1;
}

int rf_frontier_mission_begin(struct rf_frontier_mission *m, struct toy_game *g,
    const struct rf_frontier_config *c, unsigned int mission_id)
{
    struct rf_frontier_config config;
    int i;
    if (!m || !g || !mission_id || !frontier_config_valid(c)) return 0;
    for (i = 0; i < RF_FRONTIER_GUARDS; i++) {
        const struct rf_frontier_point *p = &c->guards[i].point;
        if (toy_game_position_blocked_at_height(g, p->x, p->z,
                                                TOY_GAME_PLAYER_RADIUS, 0)) {
            __fprintf(2, "FRONTIER defender binding blocked: id=%s x=%d z=%d\n",
                      p->id, p->x, p->z);
            return 0;
        }
    }
    memcpy(&config, c, sizeof(config));
    rf_frontier_mission_reset(m, g);
    memcpy(&m->config, &config, sizeof(config));
    m->mission_id = mission_id;
    m->first_final_infected_ms = -1;
    for (i = 0; i < RF_FRONTIER_GUARDS; i++) {
        const struct rf_frontier_guard_config *guard = &config.guards[i];
        int id = toy_game_add_gunner(g, guard->elite, guard->point.x,
                                    guard->point.z, "Station defender");
        int index = frontier_actor_index(g, id);
        if (index < 0) { rf_frontier_mission_reset(m, g); return 0; }
        frontier_record(m, g, i, 1, index, RF_FRONTIER_GUARD, i);
        if (!toy_game_set_ai_weapon(g, index, guard->weapon)) {
            rf_frontier_mission_reset(m, g); return 0;
        }
        toy_game_actor_set_guard(g, index, guard->point.x, guard->point.z,
                                 guard->guard_radius);
    }
    m->phase = RF_FRONTIER_ASSAULT;
    m->guards_alive = m->enemies_alive = RF_FRONTIER_GUARDS;
    m->pending_events = RF_FRONTIER_EVENT_ARRIVAL;
    return 1;
}

static int frontier_spawn_infected(struct rf_frontier_mission *m,
    struct toy_game *g, int type, const struct rf_frontier_point *point,
    int role, int ordinal)
{
    int record = frontier_record_slot(m), index, offset;
    if (record < 0) return 0;
    /* Five small rows use real entrance space instead of overlapping bodies. */
    offset = (ordinal % 3 - 1) * 640;
    m->spawn_attempts++;
    index = toy_game_spawn_enemy(g, type, point->x + offset,
                                 point->z + (ordinal / 3 % 4) * 640);
    if (index < 0) { m->spawn_failures++; return 0; }
    frontier_record(m, g, record, 0, index, role, ordinal);
    return 1;
}

static int frontier_spawn_gunner(struct rf_frontier_mission *m,
                                 struct toy_game *g, int ordinal)
{
    int record = frontier_record_slot(m), id, index;
    if (record < 0) return 0;
    m->spawn_attempts++;
    id = toy_game_add_gunner(g, ordinal == 5,
        m->config.west.x + (ordinal % 3 - 1) * 640,
        m->config.west.z + ordinal / 3 * 640, "Station reinforcement");
    index = frontier_actor_index(g, id);
    if (index < 0) { m->spawn_failures++; return 0; }
    frontier_record(m, g, record, 1, index, RF_FRONTIER_REINFORCEMENT_GUNNER, ordinal);
    toy_game_set_ai_weapon(g, index, ordinal % 3 == 1 ?
                           TOY_GAME_WEAPON_SMG : TOY_GAME_WEAPON_AK);
    toy_game_actor_set_assault(g, index, m->config.assault_x, m->config.assault_z);
    return 1;
}

static int frontier_final_type(int ordinal)
{
    /* Batch totals: 10/2/0, 9/3/0, 7/3/2; total 26/8/2. */
    if (ordinal < 12) return ordinal < 10 ? TOY_GAME_ENEMY_PURSUIT_COMMON :
                                          TOY_GAME_ENEMY_PURSUIT_FAST;
    if (ordinal < 24) return ordinal < 21 ? TOY_GAME_ENEMY_PURSUIT_COMMON :
                                          TOY_GAME_ENEMY_PURSUIT_FAST;
    if (ordinal < 31) return TOY_GAME_ENEMY_PURSUIT_COMMON;
    return ordinal < 34 ? TOY_GAME_ENEMY_PURSUIT_FAST : TOY_GAME_ENEMY_PURSUIT_HEAVY;
}

static void frontier_counterattack(struct rf_frontier_mission *m,
                                    struct toy_game *g)
{
    int budget = FRONTIER_STEP_ATTEMPTS, released, gunner_released = 0;
    released = m->phase_ms >= 16000 ? 36 : m->phase_ms >= 8000 ? 24 : 12;
    if (m->first_final_infected_ms >= 0) {
        int elapsed = m->phase_ms - m->first_final_infected_ms;
        gunner_released = elapsed >= 12000 ? 6 : elapsed >= 10000 ? 3 : 0;
    }
    while (budget > 0 && !m->retry_ms && m->final_infected_spawned < released) {
        int ordinal = m->final_infected_spawned;
        const struct rf_frontier_point *point = ordinal % 2 ?
            &m->config.east : &m->config.north;
        budget--;
        if (!frontier_spawn_infected(m, g, frontier_final_type(ordinal),
                                    point, RF_FRONTIER_FINAL_INFECTED, ordinal)) {
            m->retry_ms = FRONTIER_RETRY_MS; break;
        }
        if (m->first_final_infected_ms < 0) m->first_final_infected_ms = m->phase_ms;
        m->final_infected_spawned++;
    }
    /* Independent actor capacity must not be blocked by a full infected pool. */
    while (budget > 0 && !m->gunner_retry_ms && m->final_gunners_spawned < gunner_released) {
        budget--;
        if (!frontier_spawn_gunner(m, g, m->final_gunners_spawned)) {
            m->gunner_retry_ms = FRONTIER_RETRY_MS; break;
        }
        m->final_gunners_spawned++;
    }
    m->pending_spawns = 42 - m->final_infected_spawned - m->final_gunners_spawned;
}

void rf_frontier_mission_step(struct rf_frontier_mission *m, struct toy_game *g,
                              int dt_ms)
{
    int i;
    if (!m || !g || dt_ms <= 0 || !m->config.offline_authority ||
        m->phase == RF_FRONTIER_INACTIVE || m->phase == RF_FRONTIER_FAILED ||
        m->phase == RF_FRONTIER_SECURED) return;
    frontier_refresh(m, g);
    if (m->phase == RF_FRONTIER_ASSAULT && m->guards_alive == 0) {
        /* Phase transition wins over a periodic opportunity in the same step. */
        m->phase = RF_FRONTIER_PREPARE; m->phase_ms = m->periodic_ms = 0;
        m->pending_events |= RF_FRONTIER_EVENT_PREPARE;
        return;
    }
    m->phase_ms += dt_ms;
    if (m->phase == RF_FRONTIER_ASSAULT) {
        m->periodic_ms += dt_ms;
        if (m->periodic_ms >= FRONTIER_PERIOD_MS) {
            const struct rf_frontier_point *point = m->periodic_opportunities % 2 ?
                &m->config.east : &m->config.north;
            /* Fixed-step callers cannot build an opportunity debt. */
            m->periodic_ms %= FRONTIER_PERIOD_MS;
            m->periodic_opportunities++;
            if (m->periodic_alive + 3 > FRONTIER_PERIOD_CAP ||
                !frontier_period_capacity(m, g)) m->periodic_skipped++;
            else for (i = 0; i < 3; i++) {
                if (!frontier_spawn_infected(m, g, i == 2 ?
                    TOY_GAME_ENEMY_PURSUIT_FAST : TOY_GAME_ENEMY_PURSUIT_COMMON,
                    point, RF_FRONTIER_PERIODIC_INFECTED, m->periodic_spawned)) {
                    m->periodic_skipped++; break;
                }
                if (!m->periodic_spawned)
                    m->pending_events |= RF_FRONTIER_EVENT_PERIODIC;
                m->periodic_spawned++;
            }
        }
    } else if (m->phase == RF_FRONTIER_PREPARE && m->phase_ms >= FRONTIER_PREPARE_MS) {
        m->phase = RF_FRONTIER_COUNTERATTACK; m->phase_ms = 0;
        m->pending_spawns = 42; m->retry_ms = 0;
        m->pending_events |= RF_FRONTIER_EVENT_COUNTERATTACK;
        frontier_counterattack(m, g);
    } else if (m->phase == RF_FRONTIER_COUNTERATTACK) {
        if (m->retry_ms > 0) {
            m->retry_ms -= dt_ms;
            if (m->retry_ms < 0) m->retry_ms = 0;
        }
        if (m->gunner_retry_ms > 0) {
            m->gunner_retry_ms -= dt_ms;
            if (m->gunner_retry_ms < 0) m->gunner_retry_ms = 0;
        }
        frontier_counterattack(m, g);
    }
    frontier_refresh(m, g);
    if (m->phase == RF_FRONTIER_COUNTERATTACK && !m->pending_spawns &&
        !m->enemies_alive && m->captured[0] && m->captured[1] && m->captured[2]) {
        m->phase = RF_FRONTIER_SECURED; m->victory_count++;
        m->pending_events |= RF_FRONTIER_EVENT_SECURED;
    }
}

int rf_frontier_mission_capture(struct rf_frontier_mission *m,
    const struct toy_game *g, const char *facility_id,
    unsigned int identity, unsigned int generation,
    int actor_id, unsigned int actor_generation)
{
    int i, index;
    const struct toy_game_actor *a;
    if (!m || !g || !facility_id || !m->config.offline_authority ||
        (m->phase != RF_FRONTIER_PREPARE && m->phase != RF_FRONTIER_COUNTERATTACK &&
         m->phase != RF_FRONTIER_SECURED)) return RF_FRONTIER_CAPTURE_INVALID;
    index = frontier_actor_index(g, actor_id);
    if (index < 0) return RF_FRONTIER_CAPTURE_INVALID;
    a = &g->actors[index];
    if (a->combat_generation != actor_generation || a->state != TOY_GAME_ACTOR_ALIVE ||
        a->hp <= 0 || a->faction != TOY_GAME_FACTION_ALLIED)
        return RF_FRONTIER_CAPTURE_INVALID;
    for (i = 0; i < RF_FRONTIER_FACILITIES; i++) {
        const struct rf_frontier_facility_config *f = &m->config.facilities[i];
        long long dx, dz, range;
        if (strcmp(facility_id, f->point.id)) continue;
        if (f->identity != identity || f->generation != generation)
            return RF_FRONTIER_CAPTURE_INVALID;
        dx = (long long)a->x - f->point.x; dz = (long long)a->z - f->point.z;
        range = f->interact_range;
        if (dx * dx + dz * dz > range * range) return RF_FRONTIER_CAPTURE_INVALID;
        if (m->captured[i]) return RF_FRONTIER_CAPTURE_ALREADY;
        m->captured[i] = 1;
        return RF_FRONTIER_CAPTURE_APPLIED;
    }
    return RF_FRONTIER_CAPTURE_INVALID;
}

void rf_frontier_mission_fail(struct rf_frontier_mission *m)
{
    if (!m || m->phase == RF_FRONTIER_INACTIVE || m->phase == RF_FRONTIER_FAILED ||
        m->phase == RF_FRONTIER_SECURED) return;
    m->phase = RF_FRONTIER_FAILED; m->pending_spawns = 0;
    m->retry_ms = m->gunner_retry_ms = 0;
    m->pending_events |= RF_FRONTIER_EVENT_FAILED;
}

unsigned int rf_frontier_mission_drain_events(struct rf_frontier_mission *m)
{
    unsigned int events;
    if (!m) return 0;
    events = m->pending_events; m->pending_events = 0; return events;
}

const char *rf_frontier_mission_phase_name(int phase)
{
    switch (phase) {
    case RF_FRONTIER_ASSAULT: return "ASSAULT";
    case RF_FRONTIER_PREPARE: return "PREPARE";
    case RF_FRONTIER_COUNTERATTACK: return "COUNTERATTACK";
    case RF_FRONTIER_SECURED: return "SECURED";
    case RF_FRONTIER_FAILED: return "FAILED";
    default: return "INACTIVE";
    }
}
