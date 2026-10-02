/* AI observes the same actor lifecycle and weapon rules as human actors. */
static struct toy_game_actor *combat_ai_fixture(struct toy_game *g)
{
    static struct toy_map_primitive floor;
    struct toy_game_actor *shooter;
    int id;
    toy_game_init(g, 9204);
    g->to_spawn = g->safe_room_count = 0;
    memset(&floor, 0, sizeof(floor));
    floor.shape = TOY_MAP_PRIMITIVE_FLAT;
    floor.flags = TOY_MAP_PRIMITIVE_COLLISION | TOY_MAP_PRIMITIVE_WALKABLE;
    floor.minx = floor.minz = -20000;
    floor.maxx = floor.maxz = 20000;
    toy_game_set_primitives(g, &floor, 1, 20000);
    g->actors[0].x = 0; g->actors[0].z = 3500;
    g->actors[0].hp = g->actors[0].max_hp = 5000;
    id = toy_game_add_gunner(g, 1, 0, 0, "AI CONTRACT");
    shooter = toy_game_actor_by_id(g, id);
    if (!shooter) return NULL;
    toy_game_actor_equip_weapon(g, shooter, TOY_GAME_WEAPON_AK);
    shooter->weapon_switch_timer_ms = 0;
    shooter->sy = 0; shooter->cy = 1024;
    return shooter;
}

static int combat_ai_logic_test(void)
{
    static struct toy_game g;
    struct toy_game_actor *shooter, *target, *peer, peer_before;
    struct toy_game_capabilities caps;
    struct toy_game_attack fatal = {0};
    struct toy_map_primitive wall = {0};
    struct toy_map_primitive obstruction[2] = {{0}};
    unsigned int sequence, generation;
    int i, elapsed, id, old_index;
    shooter = combat_ai_fixture(&g);
    if (!shooter) return 1;
    target = &g.actors[0];
    toy_game_actor_capabilities(shooter, TOY_GAME_WEAPON_AK, &caps);
    /* Already facing a visible opponent still requires the full aim interval. */
    elapsed = 0;
    while (elapsed + 16 < caps.aim_ms) {
        toy_game_update_ai_teammates(&g, 16);
        elapsed += 16;
        if (shooter->fire_seq || shooter->combat_stats.shots) return 2;
    }
    toy_game_update_ai_teammates(&g, 16);
    if (shooter->combat_target.kind != TOY_GAME_ENTITY_ACTOR ||
        shooter->combat_target.index != TOY_GAME_PLAYER_ACTOR_INDEX ||
        shooter->combat_target.generation != (int)target->combat_generation ||
        shooter->fire_seq != 1 || shooter->combat_stats.shots != 1 ||
        shooter->combat_aim_ms < caps.aim_ms) return 3;

    /* Visibility is checked even between budgeted scans. Closing a wall must
     * stop the very next shot and reset aim readiness, not merely lose a scan. */
    sequence = shooter->fire_seq;
    wall.shape = TOY_MAP_PRIMITIVE_BOX;
    wall.flags = TOY_MAP_PRIMITIVE_COLLISION;
    wall.minx = -1500; wall.maxx = 1500;
    wall.minz = 1500; wall.maxz = 1800;
    wall.base_y = 0; wall.surface_y0 = wall.surface_y1 = 1600;
    obstruction[0].shape = TOY_MAP_PRIMITIVE_FLAT;
    obstruction[0].flags = TOY_MAP_PRIMITIVE_COLLISION | TOY_MAP_PRIMITIVE_WALKABLE;
    obstruction[0].minx = obstruction[0].minz = -20000;
    obstruction[0].maxx = obstruction[0].maxz = 20000;
    obstruction[1] = wall;
    toy_game_set_primitives(&g, obstruction, 2, 20000);
    shooter->fire_cooldown_ms = 0;
    shooter->combat_scan_ms = 200;
    toy_game_update_ai_teammates(&g, 16);
    if (shooter->fire_seq != sequence || shooter->combat_aim_ms) return 4;
    for (i = 0; i < 40; i++) toy_game_update_ai_teammates(&g, 16);
    if (shooter->fire_seq != sequence || shooter->combat_target.kind != -1)
        return 5;
    /* A dying opponent becomes invalid before the next shot, even with a
     * ready weapon and a cached target whose next scan has not come due. */
    shooter = combat_ai_fixture(&g);
    if (!shooter) return 6;
    target = &g.actors[0];
    toy_game_update_ai_teammates(&g, 16);
    sequence = shooter->fire_seq;
    shooter->combat_aim_ms = caps.aim_ms;
    shooter->combat_scan_ms = 200;
    shooter->fire_cooldown_ms = 0;
    fatal.kind = TOY_GAME_ATTACK_CONTROL;
    fatal.base_damage_milli = fatal.health_damage_milli = target->hp * 1000;
    toy_game_damage_actor(&g, shooter, target, &fatal);
    toy_game_update_ai_teammates(&g, 16);
    if (target->state != TOY_GAME_ACTOR_DOWNED ||
        shooter->combat_target.kind != -1 || shooter->combat_aim_ms ||
        shooter->fire_seq != sequence) return 7;

    /* Reusing a live target's slot with another generation invalidates the
     * cached reference, then requires a fresh perception/aim cycle. */
    shooter = combat_ai_fixture(&g);
    if (!shooter) return 8;
    target = &g.actors[0];
    toy_game_update_ai_teammates(&g, 16);
    sequence = shooter->fire_seq;
    generation = target->combat_generation;
    if (shooter->combat_target.generation != (int)generation) return 8;
    target->combat_generation++;
    toy_game_actor_reset_combat(target, 1);
    shooter->combat_scan_ms = 200;
    shooter->combat_aim_ms = caps.aim_ms;
    shooter->fire_cooldown_ms = 0;
    toy_game_update_ai_teammates(&g, 16);
    if (shooter->combat_target.kind != -1 || shooter->combat_aim_ms ||
        shooter->fire_seq != sequence) return 9;
    shooter->combat_scan_ms = 0;
    toy_game_update_ai_teammates(&g, 16);
    if (shooter->combat_target.generation != (int)target->combat_generation ||
        shooter->combat_aim_ms >= caps.aim_ms || shooter->fire_seq != sequence)
        return 10;
    target->active = 0;
    shooter->combat_scan_ms = 200;
    shooter->combat_aim_ms = caps.aim_ms;
    toy_game_update_ai_teammates(&g, 16);
    if (shooter->combat_target.kind != -1 || shooter->fire_seq != sequence)
        return 11;

    /* One gunner's death and later slot reuse must not reset its peer's
     * independent cooldown, aim, reserve, target or presentation sequence. */
    shooter = combat_ai_fixture(&g);
    if (!shooter) return 12;
    id = toy_game_add_gunner(&g, 1, 600, 0, "PEER");
    peer = toy_game_actor_by_id(&g, id);
    if (!peer) return 13;
    for (i = 0; i < 40; i++) toy_game_update_ai_teammates(&g, 16);
    peer_before = *peer;
    old_index = (int)(shooter - g.actors);
    generation = shooter->combat_generation;
    shooter->evasion.window_ms = shooter->evasion.animation_ms = 200;
    fatal.base_damage_milli = fatal.health_damage_milli = shooter->hp * 1000;
    toy_game_damage_actor(&g, &g.actors[0], shooter, &fatal);
    if (shooter->state != TOY_GAME_ACTOR_DEAD || shooter->hp ||
        shooter->evasion.reserve_milli || shooter->evasion.animation_ms ||
        shooter->combat_target.kind != -1 || memcmp(peer, &peer_before, sizeof(*peer)))
        return 14;
    sequence = shooter->fire_seq;
    g.ai_context_actor_index = old_index;
    toy_game_update_ai_teammate(&g, 16);
    if (shooter->fire_seq != sequence || shooter->animation.id != TOY_GAME_ANIM_DEATH)
        return 15;
    shooter->active = 0;
    id = toy_game_add_gunner(&g, 1, 0, 0, "REPLACEMENT");
    shooter = toy_game_actor_by_id(&g, id);
    if (!shooter || shooter != &g.actors[old_index] ||
        shooter->combat_generation == generation ||
        shooter->state != TOY_GAME_ACTOR_ALIVE || shooter->fire_seq ||
        shooter->combat_stats.shots || shooter->combat_target.kind != -1 ||
        shooter->combat_aim_ms || shooter->evasion.sequence ||
        shooter->rays[0].actor_index != -1 ||
        memcmp(peer, &peer_before, sizeof(*peer))) return 16;
    /* Physical impulse outlives AI intent. Death during flight must finish
     * falling, retain DEATH, and never fire; surviving landings regain control. */
    for (int dies = 0; dies <= 1; dies++) {
        shooter = combat_ai_fixture(&g);
        if (!shooter) return 17;
        old_index = (int)(shooter - g.actors);
        shooter->fire_enabled = 0;
        if (!toy_game_apply_entity_impact(&g, TOY_GAME_ENTITY_ACTOR,
                                          old_index, 1024, 0, 1)) return 18;
        for (i = 0; i < 8; i++) toy_game_update_ai_teammates(&g, 16);
        if (shooter->airborne_y <= 0 || !shooter->control_disabled) return 19;
        if (dies) {
            fatal.base_damage_milli = fatal.health_damage_milli = shooter->hp * 1000;
            toy_game_damage_actor(&g, &g.actors[0], shooter, &fatal);
        }
        sequence = shooter->fire_seq;
        for (i = 0; i < 180 && shooter->airborne_ms > 0; i++)
            toy_game_update_ai_teammates(&g, 16);
        if (shooter->airborne_ms || shooter->airborne_y || shooter->vertical_velocity ||
            shooter->knockback_x || shooter->knockback_z || shooter->control_disabled ||
            shooter->fire_seq != sequence || (dies &&
                (shooter->state != TOY_GAME_ACTOR_DEAD ||
                 shooter->animation.id != TOY_GAME_ANIM_DEATH))) return 20;
    }
    /* Both AI and human rescue can complete before a lethal impulse lands.
     * Revive owns clearing that old impulse and any remaining control state. */
    for (int human = 0; human <= 1; human++) {
        shooter = combat_ai_fixture(&g);
        if (!shooter) return 21;
        if (human) shooter = &g.actors[0];
        old_index = (int)(shooter - g.actors);
        shooter->faction = TOY_GAME_FACTION_ALLIED;
        shooter->max_hp = human ? 160 : 80;
        shooter->hp = 5;
        if (!toy_game_apply_entity_impact(&g, TOY_GAME_ENTITY_ACTOR,
                                          old_index, 0, 1024, 5) ||
            shooter->state != TOY_GAME_ACTOR_DOWNED || !shooter->airborne_ms)
            return 22;
        shooter->airborne_y = 700;
        shooter->air_x = 17; shooter->air_z = 23;
        if (!toy_game_revive_actor(&g, old_index, TOY_GAME_REVIVE_MS) ||
            shooter->state != TOY_GAME_ACTOR_ALIVE ||
            shooter->hp != (human ? TOY_GAME_REVIVE_HP : 80) ||
            shooter->animation.id != TOY_GAME_ANIM_REVIVE ||
            shooter->control_disabled || shooter->special_control ||
            shooter->airborne_ms || shooter->airborne_y || shooter->vertical_velocity ||
            shooter->air_x || shooter->air_z || shooter->knockback_x ||
            shooter->knockback_z || shooter->knockback_cooldown_ms) return 23;
    }
    __printf("COMBAT AI aim/visibility/death/target-generation/independent-reset/landing-revive passed\n");
    return 0;
}
