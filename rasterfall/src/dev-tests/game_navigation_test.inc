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
        g.actors[0].knockback_cooldown_ms = 1000000;
        for (i = 0; i < population; ++i)
            toy_game_place_enemy(&g, -3200 - (i / 8) * 180, (i % 8) * 220 - 770);
        memset(&profile, 0, sizeof(profile));
        g.update_profile = &profile;
        for (tick = 0; tick < 900; ++tick) {
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
        if (arrived != population || g.actors[0].hp == 100000 ||
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
        g.enemies[i-1].retarget_timer_ms = 1000000;
    }
    memset(&profile, 0, sizeof(profile)); g.update_profile = &profile;
    for (tick = 0; tick < 1200; ++tick) toy_game_update_world(&g, 16);
    g.update_profile = NULL;
    for (i = 1; i <= 6; ++i) if (g.actors[i].hp == 100000) {
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
    if (g.actors[0].hp == 100000 || profile.nav_searches) return 14;
    toy_game_init(&g, 906); g.to_spawn = 0;
    toy_game_place_enemy(&g, 1000, 0);
    for (tick = 0; tick < 8; ++tick) toy_game_update_world(&g,16);
    if (g.flow_probe_active || g.flow_probe_count) return 15;
    __printf("FLOW-TEST passed\n");
    return 0;
}
