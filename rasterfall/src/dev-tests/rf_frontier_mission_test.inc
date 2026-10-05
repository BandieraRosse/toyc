#include "rf_frontier_mission.h"

static void frontier_test_config(struct rf_frontier_config *c)
{
    static const char *guards[RF_FRONTIER_GUARDS] = { "gate0", "gate1", "gate2", "store0",
        "store1", "store2", "work0", "work1", "work2", "power0", "power1", "power2", "upper" };
    static const char *facilities[3] = { "workshop", "storage", "energy" };
    int i;
    memset(c, 0, sizeof(*c)); c->offline_authority = 1;
    for (i = 0; i < RF_FRONTIER_GUARDS; i++) {
        strcpy(c->guards[i].point.id, guards[i]);
        c->guards[i].point.x = (i % 4 - 2) * 3000;
        c->guards[i].point.z = 3000 + i / 4 * 3000;
        c->guards[i].weapon = i % 3 == 1 ? TOY_GAME_WEAPON_SMG : TOY_GAME_WEAPON_AK;
        c->guards[i].elite = i == 8 || i == 11;
        c->guards[i].guard_radius = 3000;
    }
    strcpy(c->north.id, "north"); c->north.z = 25000;
    strcpy(c->east.id, "east"); c->east.x = 25000;
    strcpy(c->west.id, "west"); c->west.x = -25000;
    c->assault_z = 5000;
    for (i = 0; i < 3; i++) {
        strcpy(c->facilities[i].point.id, facilities[i]);
        c->facilities[i].point.x = i * 4000;
        c->facilities[i].identity = i + 1; c->facilities[i].generation = 9;
        c->facilities[i].interact_range = 1000;
    }
}

static void frontier_test_remove_role(struct rf_frontier_mission *m,
    struct toy_game *g, int role, int release_slots)
{
    int i;
    for (i = 0; i < RF_FRONTIER_TRACKED; i++) {
        const struct rf_frontier_entity *e = &m->entities[i];
        if (!e->active || e->role != role) continue;
        if (e->actor) {
            g->actors[e->index].hp = 0;
            g->actors[e->index].state = TOY_GAME_ACTOR_DOWNED;
            if (release_slots) g->actors[e->index].active = 0;
        } else {
            if (g->enemies[e->index].active == 1) g->enemies_alive--;
            g->enemies[e->index].hp = 0;
            g->enemies[e->index].active = release_slots ? 0 : 2;
        }
    }
}

static int frontier_test_capture_all(struct rf_frontier_mission *m, struct toy_game *g)
{
    struct toy_game_actor *player = toy_game_local_player_actor(g);
    int i;
    for (i = 0; i < 3; i++) {
        const struct rf_frontier_facility_config *f = &m->config.facilities[i];
        player->x = f->point.x; player->z = f->point.z;
        int result = rf_frontier_mission_capture(m, g, f->point.id, f->identity,
            f->generation, player->actor_id, player->combat_generation);
        if (result != RF_FRONTIER_CAPTURE_APPLIED &&
            result != RF_FRONTIER_CAPTURE_ALREADY) return 0;
    }
    return 1;
}

static int rf_frontier_mission_logic_test(void)
{
    static struct toy_game mission_game;
    struct rf_frontier_mission m;
    struct rf_frontier_config c;
    struct toy_game_actor *player;
    int i, j, counts[3], id, slot, before, attempts;
    unsigned int old_generation;
    memset(&m, 0, sizeof(m)); frontier_test_config(&c);
    toy_game_init(&mission_game, 9123); mission_game.room_limit = 100000;
    player = toy_game_local_player_actor(&mission_game);
    c.offline_authority = 0;
    if (rf_frontier_mission_begin(&m, &mission_game, &c, 1)) return 1;
    c.offline_authority = 1;
    c.guards[1].point.id[0] = 0;
    if (rf_frontier_mission_begin(&m, &mission_game, &c, 1)) return 2;
    frontier_test_config(&c);
    {
        int saved_x = c.guards[0].point.x;
        c.guards[0].point.x = mission_game.room_limit + TOY_GAME_PLAYER_RADIUS;
        if (rf_frontier_mission_begin(&m, &mission_game, &c, 1) || m.mission_id) return 50;
        c.guards[0].point.x = saved_x;
    }
    if (!rf_frontier_mission_begin(&m, &mission_game, &c, 1) || m.guards_alive != RF_FRONTIER_GUARDS ||
        m.phase_ms || m.periodic_spawned) return 3;
    if (rf_frontier_mission_drain_events(&m) != RF_FRONTIER_EVENT_ARRIVAL ||
        rf_frontier_mission_drain_events(&m)) return 4;
    /* Loading/paused/render-only observations have no API that advances time. */
    rf_frontier_mission_step(&m, &mission_game, 0);
    rf_frontier_mission_step(&m, &mission_game, 19999);
    if (m.periodic_spawned) return 5;
    rf_frontier_mission_step(&m, &mission_game, 1);
    if (m.periodic_spawned != 3 || m.periodic_alive != 3 ||
        !(rf_frontier_mission_drain_events(&m) & RF_FRONTIER_EVENT_PERIODIC)) return 6;
    for (i = 0; i < 3; i++) rf_frontier_mission_step(&m, &mission_game, 20000);
    if (m.periodic_spawned != 12 || m.periodic_alive != 12 ||
        rf_frontier_mission_drain_events(&m)) return 7;
    rf_frontier_mission_step(&m, &mission_game, 20000);
    if (m.periodic_spawned != 12 || m.periodic_skipped != 1) return 8;
    frontier_test_remove_role(&m, &mission_game, RF_FRONTIER_PERIODIC_INFECTED, 1);
    rf_frontier_mission_step(&m, &mission_game, 1);
    if (m.periodic_spawned != 12 || m.periodic_alive) return 9;
    rf_frontier_mission_step(&m, &mission_game, 19999);
    if (m.periodic_spawned != 15 || m.periodic_alive != 3) return 10;
    /* Actor slot reuse cannot resurrect a defender or turn a teammate into one. */
    slot = m.entities[0].index; old_generation = m.entities[0].generation;
    mission_game.actors[slot].active = 0;
    id = toy_game_add_ai(&mission_game, TOY_GAME_AI_LEVEL_2, 0, 0, "Unrelated ally");
    if (id != m.entities[0].stable_id ||
        mission_game.actors[slot].combat_generation == old_generation) return 11;
    rf_frontier_mission_step(&m, &mission_game, 1);
    if (m.guards_alive != RF_FRONTIER_GUARDS-1) return 12;
    frontier_test_remove_role(&m, &mission_game, RF_FRONTIER_GUARD, 0);
    /* A periodic deadline coincident with the last defender's death is cancelled. */
    before = m.periodic_spawned;
    rf_frontier_mission_step(&m, &mission_game, 20000);
    if (m.phase != RF_FRONTIER_PREPARE || m.phase_ms || m.periodic_spawned != before ||
        m.periodic_alive != 3 || m.guards_alive) return 13;
    if (rf_frontier_mission_drain_events(&m) != RF_FRONTIER_EVENT_PREPARE) return 14;
    /* Every capture validation runs in authority, even for repeated requests. */
    player->x = 0; player->z = 0;
    if (rf_frontier_mission_capture(&m, &mission_game, "workshop", 1, 9,
        player->actor_id, player->combat_generation + 1) ||
        rf_frontier_mission_capture(&m, &mission_game, "workshop", 2, 9,
        player->actor_id, player->combat_generation) ||
        rf_frontier_mission_capture(&m, &mission_game, "workshop", 1, 10,
        player->actor_id, player->combat_generation) ||
        rf_frontier_mission_capture(&m, &mission_game, "unknown", 1, 9,
        player->actor_id, player->combat_generation)) return 15;
    player->x = 1001;
    if (rf_frontier_mission_capture(&m, &mission_game, "workshop", 1, 9,
        player->actor_id, player->combat_generation)) return 16;
    player->x = 1000;
    player->state = TOY_GAME_ACTOR_DOWNED;
    if (rf_frontier_mission_capture(&m, &mission_game, "workshop", 1, 9,
        player->actor_id, player->combat_generation)) return 17;
    player->state = TOY_GAME_ACTOR_ALIVE;
    if (rf_frontier_mission_capture(&m, &mission_game, "workshop", 1, 9,
        player->actor_id, player->combat_generation) != RF_FRONTIER_CAPTURE_APPLIED ||
        rf_frontier_mission_capture(&m, &mission_game, "workshop", 1, 9,
        player->actor_id, player->combat_generation) != RF_FRONTIER_CAPTURE_ALREADY) return 18;
    rf_frontier_mission_step(&m, &mission_game, 44999);
    if (m.phase != RF_FRONTIER_PREPARE || m.final_infected_spawned) return 19;
    rf_frontier_mission_step(&m, &mission_game, 1);
    if (m.phase != RF_FRONTIER_COUNTERATTACK || m.final_infected_spawned != 12 ||
        m.final_gunners_spawned || m.pending_spawns != 30) return 20;
    rf_frontier_mission_step(&m, &mission_game, 7999);
    if (m.final_infected_spawned != 12) return 21;
    rf_frontier_mission_step(&m, &mission_game, 1);
    if (m.final_infected_spawned != 24 || m.final_gunners_spawned) return 22;
    rf_frontier_mission_step(&m, &mission_game, 2000);
    if (m.final_gunners_spawned != 3) return 23;
    rf_frontier_mission_step(&m, &mission_game, 2000);
    if (m.final_gunners_spawned != 6) return 24;
    rf_frontier_mission_step(&m, &mission_game, 4000);
    if (m.final_infected_spawned != 36 || m.pending_spawns) return 25;
    memset(counts, 0, sizeof(counts)); j = 0;
    for (i = 0; i < RF_FRONTIER_TRACKED; i++) {
        const struct rf_frontier_entity *e = &m.entities[i];
        if (!e->active) continue;
        if (e->role == RF_FRONTIER_FINAL_INFECTED) {
            int type = mission_game.enemies[e->index].type;
            if (type == TOY_GAME_ENEMY_PURSUIT_COMMON) counts[0]++;
            if (type == TOY_GAME_ENEMY_PURSUIT_FAST) counts[1]++;
            if (type == TOY_GAME_ENEMY_PURSUIT_HEAVY) counts[2]++;
        }
        if (e->role == RF_FRONTIER_REINFORCEMENT_GUNNER &&
            mission_game.actors[e->index].class_id == TOY_GAME_AI_LEVEL_2) j++;
    }
    if (counts[0] != 26 || counts[1] != 8 || counts[2] != 2 || j != 1) return 26;
    frontier_test_remove_role(&m, &mission_game, RF_FRONTIER_FINAL_INFECTED, 1);
    frontier_test_remove_role(&m, &mission_game, RF_FRONTIER_REINFORCEMENT_GUNNER, 1);
    frontier_test_remove_role(&m, &mission_game, RF_FRONTIER_PERIODIC_INFECTED, 1);
    rf_frontier_mission_step(&m, &mission_game, 50000);
    if (m.phase != RF_FRONTIER_COUNTERATTACK || m.enemies_alive ||
        m.final_infected_spawned != 36 || m.final_gunners_spawned != 6) return 27;
    /* Two unclaimed facilities keep an otherwise cleared station unfinished. */
    if (!frontier_test_capture_all(&m, &mission_game)) return 28;
    rf_frontier_mission_drain_events(&m);
    rf_frontier_mission_step(&m, &mission_game, 16);
    if (m.phase != RF_FRONTIER_SECURED || m.victory_count != 1 ||
        rf_frontier_mission_drain_events(&m) != RF_FRONTIER_EVENT_SECURED) return 29;
    rf_frontier_mission_step(&m, &mission_game, 100000);
    if (m.victory_count != 1 || rf_frontier_mission_drain_events(&m)) return 30;
    rf_frontier_mission_reset(&m, &mission_game);
    if (!mission_game.actors[slot].active || m.phase || m.mission_id || m.pending_spawns) return 31;

    /* A full enemy pool must retain fixed quotas, report bounded failures, and
     * retry only when scheduled. It must never make an empty mission win. */
    toy_game_init(&mission_game, 9124); mission_game.room_limit = 100000;
    if (!rf_frontier_mission_begin(&m, &mission_game, &c, 2)) return 32;
    player = toy_game_local_player_actor(&mission_game);
    if (rf_frontier_mission_capture(&m, &mission_game, "workshop", 1, 9,
        player->actor_id, player->combat_generation)) return 33;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++)
        if (toy_game_spawn_enemy(&mission_game, TOY_GAME_ENEMY_PURSUIT_COMMON,
                                30000 + i * 640, 30000) < 0) return 34;
    rf_frontier_mission_step(&m, &mission_game, 20000);
    if (m.periodic_spawned || m.periodic_skipped != 1 || m.spawn_attempts) return 49;
    frontier_test_remove_role(&m, &mission_game, RF_FRONTIER_GUARD, 1);
    rf_frontier_mission_step(&m, &mission_game, 16);
    if (!frontier_test_capture_all(&m, &mission_game)) return 35;
    rf_frontier_mission_step(&m, &mission_game, 45000);
    if (m.final_infected_spawned || m.pending_spawns != 42 ||
        m.spawn_failures != 1 || m.spawn_attempts != 1) return 36;
    attempts = m.spawn_attempts;
    rf_frontier_mission_step(&m, &mission_game, 249);
    if (m.spawn_attempts != attempts || m.phase != RF_FRONTIER_COUNTERATTACK) return 37;
    rf_frontier_mission_step(&m, &mission_game, 1);
    if (m.spawn_attempts != attempts + 1 || m.pending_spawns != 42) return 38;
    /* One free slot permits exactly one success; later deaths do not replenish
     * it. Generation protects the mission from an unrelated recycled occupant. */
    mission_game.enemies[0].active = 0; mission_game.enemies_alive--;
    rf_frontier_mission_step(&m, &mission_game, 250);
    if (m.final_infected_spawned != 1 || m.pending_spawns != 41) return 39;
    old_generation = mission_game.enemies[0].combat_generation;
    mission_game.enemies[0].active = 0; mission_game.enemies_alive--;
    if (toy_game_spawn_enemy(&mission_game, TOY_GAME_ENEMY_PURSUIT_FAST, 10000, 30000) != 0 ||
        mission_game.enemies[0].combat_generation == old_generation) return 40;
    rf_frontier_mission_step(&m, &mission_game, 250);
    if (m.enemies_alive || m.final_infected_spawned != 1 || m.pending_spawns != 41) return 41;
    rf_frontier_mission_step(&m, &mission_game, 9749);
    if (m.final_gunners_spawned) return 47;
    rf_frontier_mission_step(&m, &mission_game, 1);
    if (m.final_gunners_spawned != 3 || m.pending_spawns != 38) return 48;
    rf_frontier_mission_fail(&m);
    attempts = m.spawn_attempts;
    rf_frontier_mission_step(&m, &mission_game, 100000);
    if (m.phase != RF_FRONTIER_FAILED || m.spawn_attempts != attempts ||
        rf_frontier_mission_capture(&m, &mission_game, "workshop", 1, 9,
        player->actor_id, player->combat_generation)) return 42;
    rf_frontier_mission_reset(&m, &mission_game);
    if (!mission_game.enemies[0].active || m.mission_id || m.periodic_ms ||
        m.retry_ms || m.pending_events || m.entities[0].active || m.captured[0]) return 43;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) mission_game.enemies[i].active = 0;
    mission_game.enemies_alive = 0;
    if (!rf_frontier_mission_begin(&m, &mission_game, &c, 3) || m.periodic_spawned ||
        m.final_infected_spawned || m.phase_ms || m.victory_count) return 44;
    rf_frontier_mission_reset(&m, &mission_game);
    /* Atomic begin cleans partially created defenders if actor capacity runs out. */
    for (i = 0; i < TOY_GAME_REMOTE_ACTOR_BASE - 6; i++)
        toy_game_add_ai(&mission_game, TOY_GAME_AI_LEVEL_1, 0, 0, "Capacity occupant");
    before = 0;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) before += !!mission_game.actors[i].active;
    if (rf_frontier_mission_begin(&m, &mission_game, &c, 4)) return 45;
    j = 0;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) j += !!mission_game.actors[i].active;
    if (j != before || m.phase || m.mission_id) return 46;
    __printf("FRONTIER fixed-time/cap/identity/capture/quotas/retry/reset passed\n");
    return 0;
}
