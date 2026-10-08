/* Behavior and bounded-work regressions for the default shared field lane. */
static int enemy_flow_logic_test(void)
{
    static struct toy_game g, replay;
    struct toy_map_primitive world[5];
    struct toy_game_update_profile profile;
    struct toy_game_update_profile reference_profile = {0};
    int population, i, tick, arrived;
    unsigned int small_edges = 0;
    for (population = 1; population <= 64; population *= 8) {
        toy_game_init(&g, 901);
        g.to_spawn = 0; g.safe_room_count = 0;
        world[0] = test_flat_primitive(-6000, 6000, -6000, 6000, 0);
        world[1] = test_box_primitive(-300, 300, -1200, 1200, 0, 1200,
                                      TOY_MAP_PRIMITIVE_COLLISION);
        toy_game_set_primitives(&g, world, 2, 6000);
        g.actors[0].x = 3000; g.actors[0].z = 0; g.actors[0].hp = 100000;
        /* Route cost is measured from clear bodies, not intersecting spawns. */
        for (i = 0; i < population; ++i)
            toy_game_place_enemy(&g, -3200 - (i / 8) * 300, (i % 8) * 300 - 1050);
        memset(&profile, 0, sizeof(profile));
        g.update_profile = &profile;
        /* Solid bodies queue around the obstacle; retain all-member arrival
         * and per-tick/shared-work bounds with a finite traversal window. */
        for (tick = 0; tick < 1200; ++tick) {
            unsigned int edges = profile.flow_edges, samples = profile.flow_samples;
            toy_game_update_world(&g, 16);
            if (profile.flow_edges - edges > TOY_GAME_FLOW_EDGES_PER_TICK ||
                profile.flow_samples - samples > TOY_GAME_FLOW_SAMPLES_PER_TICK +
                                                 TOY_GAME_FLOW_LOCAL_PER_TICK)
                return 1;
        }
        g.update_profile = NULL;
        arrived = 0;
        for (i = 0; i < population; ++i) if (g.enemies[i].x > 1000) arrived++;
        __printf("FLOW-TEST enemies=%d arrived=%d builds=%u edges=%u samples=%u legacy=%u hp=%d\n",
            population, arrived, profile.flow_builds, profile.flow_edges,
            profile.flow_samples, profile.nav_searches, g.actors[0].hp);
        if (arrived != population || (g.actors[0].hp == 100000 &&
            !g.actors[0].combat_stats.evasion_spent_milli) ||
            profile.nav_searches || profile.nav_group_searches) {
            __printf("FLOW-TEST first x=%d z=%d active=%d state=%d target=%d nav=%d,%d\n",
                g.enemies[0].x,g.enemies[0].z,g.enemies[0].active,g.state,
                g.enemies[0].target_kind,g.enemies[0].nav_x,g.enemies[0].nav_z);
            return 2;
        }
        if (population == 1) small_edges = profile.flow_edges;
        if (profile.flow_builds != 1 ||
            profile.flow_edges > small_edges + 64u * (unsigned int)population) return 3;
    }
    /* Rebuilding invalidates every field/member/cache, and repeat inputs
     * reproduce all scheduling and field scratch state without a clock. */
    toy_game_rebuild_navigation(&g);
    for (i = 0; i < TOY_GAME_FLOW_FIELDS; ++i)
        if (g.flow_fields[i].active) return 4;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; ++i)
        if (g.enemies[i].nav_field >= 0) return 5;
    g.actors[0].x = -3000;
    memcpy(&replay, &g, sizeof(g));
    reference_profile.flow_reference_probe = 1;
    for (tick = 0; tick < 180; ++tick) {
        replay.update_profile = &reference_profile;
        toy_game_update_world(&g, 16); toy_game_update_world(&replay, 16);
        replay.update_profile = NULL;
        if (memcmp(&g, &replay, sizeof(g))) return 6;
    }
    /* A thin off-grid door, wider than the navigation body but containing
     * no grid center, must connect through supplemental geometry nodes. */
    toy_game_init(&g, 902); g.to_spawn = 0; g.safe_room_count = 0;
    world[0] = test_flat_primitive(-6000, 6000, -6000, 6000, 0);
    world[1] = test_box_primitive(-80, 80, -6000, 40, 0, 1200,
                                  TOY_MAP_PRIMITIVE_COLLISION);
    world[2] = test_box_primitive(-80, 80, 320, 6000, 0, 1200,
                                  TOY_MAP_PRIMITIVE_COLLISION);
    toy_game_set_primitives(&g, world, 3, 6000);
    g.actors[0].x = 3000; g.actors[0].z = 180; g.actors[0].hp = 100000;
    g.actors[0].knockback_cooldown_ms = 1000000;
    toy_game_place_enemy(&g, -3000, 180);
    for (tick = 0; tick < 900; ++tick) toy_game_update_world(&g, 16);
    if (g.enemies[0].x < 1000) {
        __printf("FLOW-TEST narrow failed x=%d z=%d\n", g.enemies[0].x, g.enemies[0].z);
        return 7;
    }
    /* Two floors in the same X/Z cells remain distinct; the upper target is
     * reached by the ramp, never by walking through the overhead platform. */
    toy_game_init(&g, 903); g.to_spawn = 0; g.safe_room_count = 0;
    world[0] = test_flat_primitive(-6000, 6000, -3000, 3000, 0);
    world[1] = test_box_primitive(200, 3000, -900, 900, 2500, 3000,
        TOY_MAP_PRIMITIVE_COLLISION | TOY_MAP_PRIMITIVE_WALKABLE);
    world[2] = test_ramp_primitive(TOY_MAP_PRIMITIVE_RAMP_X,
                                    -3000, 0, -900, 900, 0, 3000);
    world[3] = test_flat_primitive(0, 3000, -900, 900, 3000);
    world[3].flags = TOY_MAP_PRIMITIVE_WALKABLE;
    toy_game_set_primitives(&g, world, 4, 6000);
    g.actors[0].x = 2400; g.actors[0].z = 0;
    g.actors[0].ground_y = 3000; g.actors[0].hp = 100000;
    g.actors[0].knockback_cooldown_ms = 1000000;
    toy_game_place_enemy(&g, -3500, 0);
    for (tick = 0; tick < 1000; ++tick) toy_game_update_world(&g, 16);
    if (g.enemies[0].x < 1800 || g.enemies[0].ground_y != 3000) {
        __printf("FLOW-TEST layers failed x=%d y=%d\n", g.enemies[0].x, g.enemies[0].ground_y);
        for(i=0;i<TOY_GAME_FLOW_FIELDS;i++) if(g.flow_fields[i].active)
            __printf("field %d nodes=%d seed=%d complete=%d heap=%d clear=%d target_y=%d retry=%d\n", i,g.flow_count,g.flow_fields[i].seed,g.flow_fields[i].complete,g.flow_fields[i].heap_count,g.flow_fields[i].clearing,g.flow_fields[i].target_y,g.enemies[0].nav_flow_retry);
        for(i=1;i<=g.flow_count;i++) if(g.flow_nodes[i].z==150 && g.flow_nodes[i].x>=-3450 && g.flow_nodes[i].x<=450)
            __printf("node %d x=%d y=%d dist=%u next=%d\n",i,g.flow_nodes[i].x,g.flow_nodes[i].y,g.flow_fields[3].distance[i],g.flow_fields[3].next[i]);
        return 8;
    }
    /* Same X/Z on different floors must still navigate, and must not bite
     * the upstairs target before using the ramp. */
    g.enemies[0].x = 2400; g.enemies[0].z = 0; g.enemies[0].ground_y = 0;
    g.enemies[0].nav_active = 0; g.enemies[0].nav_direct_valid = 0;
    g.enemies[0].nav_flow_node = 0; g.enemies[0].bite_cooldown_ms = 0;
    g.actors[0].hp = 100000;
    g.actors[0].evasion.reserve_milli = 0;
    for (tick = 0; tick < 1200; ++tick) {
        toy_game_update_world(&g, 16);
        if (g.enemies[0].ground_y < 2900 && g.actors[0].hp != 100000) return 9;
    }
    if (g.enemies[0].ground_y != 3000 || g.actors[0].hp == 100000) return 10;
    /* More destinations than cache slots: uncached enemies can still explore
     * and arrive, without eviction churn or individual fallback searches. */
    toy_game_init(&g, 904); g.to_spawn = 0; g.safe_room_count = 0;
    world[0] = test_flat_primitive(-6000, 6000, -6000, 6000, 0);
    toy_game_set_primitives(&g, world, 1, 6000);
    g.actors[0].x = 5000; g.actors[0].hp = 100000;
    for (i = 1; i <= 6; ++i) {
        memcpy(&g.actors[i], &g.actors[0], sizeof(g.actors[i]));
        g.actors[i].actor_id = 100 + i;
        g.actors[i].x = 3000; g.actors[i].z = -2400 + i * 700;
        toy_game_place_enemy(&g, -3000, g.actors[i].z);
        g.enemies[i-1].target_kind = TOY_GAME_TARGET_ACTOR;
        g.enemies[i-1].target_index = i;
        g.enemies[i-1].target_generation = g.actors[i].combat_generation;
        g.enemies[i-1].retarget_timer_ms = 1000000;
    }
    memset(&profile, 0, sizeof(profile)); g.update_profile = &profile;
    for (tick = 0; tick < 1200; ++tick) toy_game_update_world(&g, 16);
    g.update_profile = NULL;
    for (i = 1; i <= 6; ++i) if (g.actors[i].hp == 100000 &&
        !g.actors[i].combat_stats.evasion_spent_milli) {
        __printf("FLOW-TEST target=%d enemy=%d,%d goal=%d,%d active=%d node=%d\n",
            i,g.enemies[i-1].x,g.enemies[i-1].z,g.actors[i].x,g.actors[i].z,
            g.enemies[i-1].nav_active,g.enemies[i-1].nav_flow_node);
        return 11;
    }
    if (profile.nav_searches || profile.flow_builds > 12)
        return 12;
    /* A real disconnection settles as unreachable, then a topology rebuild
     * opens it. The same task never invokes legacy BFS to escape the budget. */
    toy_game_init(&g, 905); g.to_spawn = 0; g.safe_room_count = 0;
    world[1] = test_box_primitive(-100,100,-6000,6000,0,1200,
                                  TOY_MAP_PRIMITIVE_COLLISION);
    toy_game_set_primitives(&g, world, 2, 6000);
    g.actors[0].x = 3000; g.actors[0].hp = 100000;
    g.actors[0].knockback_cooldown_ms = 1000000;
    toy_game_place_enemy(&g,-3000,0);
    memset(&profile, 0, sizeof(profile)); g.update_profile = &profile;
    for (tick=0;tick<700;++tick) toy_game_update_world(&g,16);
    if (g.enemies[0].x > -100 || profile.nav_searches) return 13;
    world[1].flags = 0;
    toy_game_rebuild_navigation(&g);
    for (tick=0;tick<500;++tick) toy_game_update_world(&g,16);
    g.update_profile = NULL;
    if ((g.actors[0].hp == 100000 &&
         !g.actors[0].combat_stats.evasion_spent_milli) ||
        profile.nav_searches) return 14;
    toy_game_init(&g, 906); g.to_spawn = 0;
    toy_game_place_enemy(&g, 1000, 0);
    for (tick = 0; tick < 8; ++tick) toy_game_update_world(&g,16);
    if (g.flow_probe_active || g.flow_probe_count) return 15;
    __printf("FLOW-TEST passed\n");
    return 0;
}

/* Identity and behavior contracts for ordinary pursuit's existing scan. */
static void ordinary_enemy_retention_fixture(struct toy_game *g,
                                             struct toy_map_primitive *world)
{
    int i, id;
    struct toy_game_actor *a;
    toy_game_init(g, 913);
    g->external_director = 1;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; ++i) g->actors[i].active = 0;
    world[0] = test_flat_primitive(-10000, 10000, -10000, 10000, 0);
    toy_game_set_primitives(g, world, 1, 10000);
    id = toy_game_add_ai(g, TOY_GAME_AI_LEVEL_2, -240, 1920, "PURSUIT ALLY");
    a = toy_game_actor_by_id(g, id);
    if (a) {
        a->movement_hold_token = 1; a->fire_enabled = 0;
        a->hp = a->max_hp = 100000;
    }
    id = toy_game_add_gunner(g, 0, 240, 2080, "PURSUIT GUNNER");
    a = toy_game_actor_by_id(g, id);
    if (a) {
        a->movement_hold_token = 1; a->fire_enabled = 0;
        a->hp = a->max_hp = 100000;
    }
    toy_game_spawn_enemy(g, TOY_GAME_ENEMY_PURSUIT_COMMON, 0, 0);
    g->enemies[0].speed = 0;
}

static int ordinary_enemy_retention_logic_test(void)
{
    static struct toy_game g;
    struct toy_map_primitive world[4];
    struct toy_game_enemy *e;
    struct toy_game_actor *a, *b;
    struct toy_game_update_profile p = {0};
    unsigned int generation;
    uint64_t rng;
    int tick, selected, index, type;
    ordinary_enemy_retention_fixture(&g, world);
    a = &g.actors[1]; b = &g.actors[2]; e = &g.enemies[0];
    if (!a->active || !b->active || e->active != 1 ||
        e->target_generation || e->target_kind != -1 ||
        e->target_index != -1) return 1;
    rng = g.rng;
    toy_game_update_world(&g, 16);
    if (e->target_kind != TOY_GAME_TARGET_ACTOR || e->target_index != 1 ||
        e->target_generation != a->combat_generation) return 2;
    /* Tiny near-equal motion must not change a valid identity every scan. */
    for (tick = 0; tick < 160; ++tick) {
        a->z = tick / 32 % 2 ? 2080 : 1920;
        b->z = tick / 32 % 2 ? 1920 : 2080;
        toy_game_update_world(&g, 16);
        if (e->target_index != 1 || e->target_generation != a->combat_generation)
            return 3;
        if (e->retarget_timer_ms < 0 || e->retarget_timer_ms > TOY_GAME_RETARGET_MS)
            return 4;
    }
    if (rng != g.rng) return 5;
    /* A substantially nearer target wins by the scheduled scan, <=512ms. */
    b->z = 1100;
    for (tick = 0; tick < 32 && e->target_index != 2; ++tick)
        toy_game_update_world(&g, 16);
    if (e->target_index != 2 || e->target_generation != b->combat_generation)
        return 6;
    /* Retaining a higher slot must not be overwritten by scan output aliases. */
    for (tick = 0; tick < 160; ++tick) {
        a->z = tick / 32 % 2 ? 2080 : 1920;
        b->z = tick / 32 % 2 ? 1920 : 2080;
        toy_game_update_world(&g, 16);
        if (e->target_index != 2 || e->target_generation != b->combat_generation)
            return 22;
    }
    /* Inactive, dead and replaced targets invalidate between scheduled scans. */
    b->active = 0; e->retarget_timer_ms = TOY_GAME_RETARGET_MS;
    toy_game_update_world(&g, 16);
    if (e->target_index != 1 || e->target_generation != a->combat_generation)
        return 7;
    b->active = 1; b->z = 1900; a->z = 2000;
    generation = a->combat_generation;
    a->combat_generation++; /* Same actor_id: generation itself is required. */
    e->retarget_timer_ms = TOY_GAME_RETARGET_MS;
    e->nav_direct_valid = 1; e->nav_direct_actor = 1;
    e->nav_direct_tick = g.nav_tick + 1000;
    e->nav_active = 1;
    toy_game_update_world(&g, 16);
    if (e->target_index != 2 || e->target_generation != b->combat_generation ||
        e->target_generation == generation || e->nav_direct_actor == 1)
        return 8;
    b->combat_generation++; b->actor_id += 1000; b->z = 2000; a->z = 1900;
    e->retarget_timer_ms = TOY_GAME_RETARGET_MS;
    toy_game_update_world(&g, 16);
    if (e->target_index != 1 || e->target_generation != a->combat_generation)
        return 9;
    a->hp = 0; a->state = TOY_GAME_ACTOR_DOWNED;
    e->retarget_timer_ms = TOY_GAME_RETARGET_MS;
    toy_game_update_world(&g, 16);
    if (e->target_index != 2 || e->target_generation != b->combat_generation)
        return 10;
    b->active = 0;
    toy_game_update_world(&g, 16);
    if (e->target_kind != -1 || e->target_index != -1 ||
        e->target_generation || e->retarget_timer_ms) return 11;
    index = toy_game_spawn_enemy(&g, TOY_GAME_ENEMY_PURSUIT_COMMON, 1000, 0);
    if (index < 0 || g.enemies[index].target_generation) return 12;
    e->active = 0; e->target_generation = 0xf123u;
    index = toy_game_spawn_enemy(&g, TOY_GAME_ENEMY_PURSUIT_COMMON, 0, 0);
    if (index != 0 || e->target_generation || e->target_kind != -1 ||
        e->target_index != -1) return 13;
    /* Every spawn lane reuses the same dirty slot through init_enemy_ai. */
    e->active = 0; e->target_generation = 0xf123u;
    toy_game_place_enemy(&g, 0, 0);
    if (e->active != 1 || e->target_generation || e->target_kind != -1 ||
        e->target_index != -1) return 23;
    {
        struct toy_game_box zone = {0};
        zone.minx = -5000; zone.maxx = -4000;
        zone.minz = -5000; zone.maxz = -4000;
        e->active = 0; e->target_generation = 0xf123u;
        if (toy_game_spawn_horde_type(&g, TOY_GAME_ENEMY_PURSUIT_COMMON,
                                       1, 1, &zone, 1, 0) != 1 ||
            e->target_generation || e->target_kind != -1 ||
            e->target_index != -1) return 24;
        e->active = 0; e->target_generation = 0xf123u;
        g.external_director = 0; g.campaign_mode = 1;
        g.campaign_phase = TOY_GAME_PHASE_BUILDUP; g.spawn_budget = 1;
        g.phase_timer_ms = 0; g.spawn_timer_ms = 0;
        g.spawn_zones = &zone; g.spawn_zone_count = 1; g.alarm_spawn_zone = -1;
        toy_game_update_world(&g, 16);
        if (e->active != 1 || e->target_generation || e->target_kind != -1 ||
            e->target_index != -1) return 25;
    }
    for (type = TOY_GAME_ENEMY_PURSUIT_COMMON;
         type <= TOY_GAME_ENEMY_PURSUIT_FAST; ++type) {
        e->active = 0; e->target_generation = 0xf123u;
        index = toy_game_spawn_enemy(&g, type, 0, 0);
        if (index != 0 || e->type != type || e->target_generation ||
            e->target_kind != -1 || e->target_index != -1) return 29;
    }

    /* A wall-hidden current target cannot beat a physically direct candidate. */
    ordinary_enemy_retention_fixture(&g, world);
    a = &g.actors[1]; b = &g.actors[2]; e = &g.enemies[0];
    world[1] = test_box_primitive(-300, -100, -400, 400, 0, 1000,
                                  TOY_MAP_PRIMITIVE_COLLISION);
    toy_game_set_primitives(&g, world, 2, 10000);
    e->x = -700; a->x = 200; a->z = 0; b->x = -700; b->z = 1300;
    e->target_kind = TOY_GAME_TARGET_ACTOR; e->target_index = 1;
    e->target_generation = a->combat_generation; e->retarget_timer_ms = 0;
    toy_game_update_world(&g, 16);
    if (e->target_index != 2) return 14;
    /* A ground-to-ceiling combat boundary invalidates even a pinned target. */
    world[1].minx = -10000; world[1].maxx = 10000;
    world[1].minz = 600; world[1].maxz = 700;
    memcpy(world[1].role, "air_gate", 9);
    toy_game_set_primitives(&g, world, 2, 10000);
    a->x = -700; a->z = 100; b->x = -700; b->z = 1300;
    e->target_kind = TOY_GAME_TARGET_ACTOR; e->target_index = 2;
    e->target_generation = b->combat_generation;
    e->retarget_timer_ms = TOY_GAME_RETARGET_MS;
    toy_game_update_world(&g, 16);
    if (e->target_index != 1 || e->target_generation != a->combat_generation)
        return 15;
    /* Navigation rebuild changes route lifetime, not the valid target identity. */
    selected = e->target_index; generation = e->target_generation;
    e->nav_direct_valid = 1; e->nav_active = 1;
    toy_game_rebuild_navigation(&g);
    if (e->nav_active || e->nav_direct_valid || e->target_index != selected ||
        e->target_generation != generation) return 16;
    toy_game_update_world(&g, 16);
    if (e->target_index != selected || e->target_generation != generation)
        return 17;

    /* Special abilities keep nearest selection, with no ordinary preference. */
    ordinary_enemy_retention_fixture(&g, world);
    a = &g.actors[1]; b = &g.actors[2]; e = &g.enemies[0];
    a->z = 2080; b->z = 1920;
    e->type = TOY_GAME_ENEMY_SMOKER;
    e->target_kind = TOY_GAME_TARGET_ACTOR; e->target_index = 1;
    e->target_generation = a->combat_generation;
    toy_game_update_world(&g, 16);
    if (e->nav_target_actor != 2) return 18;
    /* Weighting cannot expand the original physical-direct probe range. */
    ordinary_enemy_retention_fixture(&g, world);
    a = &g.actors[1]; b = &g.actors[2]; e = &g.enemies[0];
    a->z = 2500; b->z = 2600;
    e->target_kind = TOY_GAME_TARGET_ACTOR; e->target_index = 1;
    e->target_generation = a->combat_generation; e->retarget_timer_ms = 0;
    memset(&p, 0, sizeof(p)); g.update_profile = &p;
    toy_game_update_world(&g, 16);
    g.update_profile = NULL;
    if (p.nav_short_queries || e->target_index != 1) return 27;
    /* An artificial unbound cache never inherits identity-based preference. */
    b->z = 2400; e->target_generation = 0;
    e->retarget_timer_ms = TOY_GAME_RETARGET_MS;
    toy_game_update_world(&g, 16);
    if (e->target_index != 2 || e->target_generation != b->combat_generation)
        return 28;

    /* A retained upper-floor actor must not be bitten through the floor. */
    ordinary_enemy_retention_fixture(&g, world);
    a = &g.actors[1]; b = &g.actors[2]; e = &g.enemies[0];
    world[1] = test_flat_primitive(-10000, 10000, -10000, 10000, 3500);
    toy_game_set_primitives(&g, world, 2, 10000);
    a->x = a->z = 0; a->ground_y = 3500; b->active = 0;
    e->x = 0; e->z = 100; e->ground_y = 0;
    e->target_kind = TOY_GAME_TARGET_ACTOR; e->target_index = 1;
    e->target_generation = a->combat_generation;
    e->retarget_timer_ms = TOY_GAME_RETARGET_MS;
    for (tick = 0; tick < 32; ++tick) {
        toy_game_update_world(&g, 16);
        if (a->hp != a->max_hp || e->target_generation != a->combat_generation)
            return 26;
    }

    /* A generic narrow doorway: coherent pursuit arrives under collision. */
    ordinary_enemy_retention_fixture(&g, world);
    a = &g.actors[1]; b = &g.actors[2]; e = &g.enemies[0];
    world[1] = test_box_primitive(-10000, 500, 1000, 1300, 0, 1800,
                                  TOY_MAP_PRIMITIVE_COLLISION);
    world[2] = test_box_primitive(2000, 10000, 1000, 1300, 0, 1800,
                                  TOY_MAP_PRIMITIVE_COLLISION);
    toy_game_set_primitives(&g, world, 3, 10000);
    a->x = 1100; a->z = 3500; b->x = 1580; b->z = 3500;
    a->evasion.reserve_milli = b->evasion.reserve_milli = 0;
    e->speed = toy_game_enemy_info(e->type)->speed_max;
    memset(&p, 0, sizeof(p));
    g.update_profile = &p;
    for (tick = 0; tick < 1200 && a->hp == a->max_hp && b->hp == b->max_hp; ++tick) {
        unsigned int edges = p.flow_edges, samples = p.flow_samples;
        a->z = 3500 + (tick / 32 % 2 ? 80 : -80);
        b->z = 3500 - (tick / 32 % 2 ? 80 : -80);
        toy_game_update_world(&g, 16);
        if (p.flow_edges - edges > TOY_GAME_FLOW_EDGES_PER_TICK ||
            p.flow_samples - samples > TOY_GAME_FLOW_SAMPLES_PER_TICK +
                                      TOY_GAME_FLOW_LOCAL_PER_TICK)
            return 19;
    }
    g.update_profile = NULL;
    if (a->hp == a->max_hp && b->hp == b->max_hp) return 20;
    if (p.nav_searches || p.nav_group_searches) return 21;
    __printf("INFECTED TARGET passed\n");
    return 0;
}
