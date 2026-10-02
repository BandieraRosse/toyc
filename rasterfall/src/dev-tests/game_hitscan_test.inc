/* Real geometry and actor execution: no random body-to-head conversion. */
static void combat_shot_ready(struct toy_game *g, struct toy_game_actor *a,
                               int weapon)
{
    toy_game_actor_equip_weapon(g, a, weapon);
    a->current_slot = toy_game_weapon_info(weapon)->slot;
    a->weapon_switch_timer_ms = a->fire_cooldown_ms = 0;
    a->reload_timer_ms = a->reloading = a->weapon_spread_heat = 0;
    a->moving = 0;
    a->pitch_sy = -128; a->pitch_cy = 1024;
}

static int combat_hitscan_logic_test(void)
{
    static struct toy_game g, replay;
    struct toy_game_actor *shooter, *target, *friend;
    struct toy_game_combat_target found;
    struct toy_map_primitive wall = {0};
    struct toy_game_capabilities caps;
    int id, i, before, spent, hit_pellets, base, health, index;
    toy_game_init(&g, 9401);
    g.to_spawn = g.safe_room_count = 0;
    toy_game_set_primitives(&g, NULL, 0, 20000);
    shooter = &g.actors[0]; shooter->x = shooter->z = 0;
    id = toy_game_add_gunner(&g, 1, 0, 2000, "TARGET");
    target = toy_game_actor_by_id(&g, id);
    if (!target) return 1;
    index = (int)(target - g.actors);
    target->hit_test_dummy = 1;
    before = target->hp; spent = target->evasion.reserve_milli;
    combat_shot_ready(&g, shooter, TOY_GAME_WEAPON_PISTOL);
    /* A miss and a wall both consume ammo, never the target's reserve. */
    toy_game_actor_fire(&g, shooter, 1024, 0, 100);
    if (target->hp != before || target->evasion.reserve_milli != spent ||
        shooter->combat_stats.shots != 1 || shooter->combat_stats.hits) return 2;
    wall.shape = TOY_MAP_PRIMITIVE_BOX;
    wall.flags = TOY_MAP_PRIMITIVE_COLLISION;
    wall.minx = -1000; wall.maxx = 1000;
    wall.minz = 800; wall.maxz = 1100;
    wall.base_y = 0; wall.surface_y0 = wall.surface_y1 = 1500;
    toy_game_set_primitives(&g, &wall, 1, 20000);
    combat_shot_ready(&g, shooter, TOY_GAME_WEAPON_PISTOL);
    toy_game_actor_fire(&g, shooter, 0, 1024, 100);
    if (!shooter->rays[0].hit_world || target->hp != before ||
        target->evasion.reserve_milli != spent ||
        toy_game_find_combat_target(&g, shooter, &found)) return 3;
    toy_game_set_primitives(&g, NULL, 0, 20000);
    /* Same-faction bodies retain the old transparent/no-friendly-fire policy. */
    id = toy_game_add_ai(&g, TOY_GAME_AI_LEVEL_2, 0, 1000, "FRIEND");
    friend = toy_game_actor_by_id(&g, id);
    if (!friend) return 4;
    friend->hit_test_dummy = 1;
    combat_shot_ready(&g, shooter, TOY_GAME_WEAPON_PISTOL);
    toy_game_actor_fire(&g, shooter, 0, 1024, 100);
    if (shooter->rays[0].actor_index != index ||
        !shooter->rays[0].hit_enemy || shooter->rays[0].weakpoint ||
        target->hp != before || target->evasion.reserve_milli >= spent ||
        friend->hp != friend->max_hp || friend->combat_stats.evasion_events)
        return 5;
    /* Height is part of the actual ray; only a genuine head hit gets precision. */
    toy_game_actor_reset_combat(target, 0);
    combat_shot_ready(&g, shooter, TOY_GAME_WEAPON_PISTOL);
    toy_game_actor_fire(&g, shooter, 0, 1024, 100);
    base = toy_game_weapon_info(TOY_GAME_WEAPON_PISTOL)->damage;
    if (shooter->rays[0].weakpoint || before - target->hp != base) return 6;
    target->hp = before; toy_game_actor_reset_combat(target, 0);
    combat_shot_ready(&g, shooter, TOY_GAME_WEAPON_PISTOL);
    shooter->pitch_sy = 0;
    toy_game_actor_fire(&g, shooter, 0, 1024, 100);
    toy_game_actor_capabilities(shooter, TOY_GAME_WEAPON_PISTOL, &caps);
    if (!shooter->rays[0].weakpoint ||
        before - target->hp != base * caps.weakpoint_percent / 100) return 7;
    target->hp = before; toy_game_actor_reset_combat(target, 1);
    combat_shot_ready(&g, shooter, TOY_GAME_WEAPON_PISTOL);
    shooter->pitch_sy = 1024;
    toy_game_actor_fire(&g, shooter, 0, 1024, 100);
    if (target->hp != before || target->evasion.sequence ||
        shooter->rays[0].actor_index >= 0) return 8;
    /* Ten actual pellet rays are aggregated once per target, excluding misses. */
    combat_shot_ready(&g, shooter, TOY_GAME_WEAPON_SHOTGUN);
    shooter->pitch_sy = -128;
    target->x = 150; target->z = 2000;
    toy_game_actor_fire(&g, shooter, 0, 1024, 100);
    spent = hit_pellets = health = 0;
    for (i = 0; i < shooter->ray_count; i++) {
        const struct toy_game_ray *r = &shooter->rays[i];
        if (r->actor_index == index) {
            hit_pellets++;
            spent += r->evasion_spent_milli; health += r->health_damage;
        }
    }
    base = toy_game_weapon_info(TOY_GAME_WEAPON_SHOTGUN)->damage;
    if (!hit_pellets || hit_pellets == shooter->ray_count || health ||
        target->evasion.sequence != 1 || spent != hit_pellets * base * 1000 ||
        target->evasion.reserve_milli != 65000 - spent) return 9;
    /* Deferred client geometry cannot predict actor health or reserve. */
    toy_game_actor_reset_combat(target, 1);
    combat_shot_ready(&g, shooter, TOY_GAME_WEAPON_PISTOL);
    target->x = 0;
    g.defer_actor_damage = 1;
    toy_game_actor_fire(&g, shooter, 0, 1024, 100);
    if (!shooter->rays[0].hit_enemy || target->evasion.sequence ||
        target->evasion.reserve_milli != 65000 || target->hp != before)
        return 10;
    /* A reported shot aimed at an old slot generation cannot hurt its reuse. */
    g.defer_actor_damage = 0;
    target->combat_generation++;
    toy_game_actor_resolve_shot(&g, shooter);
    if (target->evasion.sequence || target->hp != before) return 11;
    /* All factions use the same weapon executor and capability map. */
    memcpy(&replay, &g, sizeof(g));
    shooter->faction = TOY_GAME_FACTION_HOSTILE;
    target->faction = TOY_GAME_FACTION_ALLIED;
    friend->active = 0; replay.actors[(int)(friend-g.actors)].active = 0;
    for (i = 0; i < 2; i++) {
        struct toy_game *sample = i ? &replay : &g;
        sample->actors[index].hp = before;
        toy_game_actor_reset_combat(&sample->actors[index], 1);
        combat_shot_ready(sample, &sample->actors[0], TOY_GAME_WEAPON_AK);
        toy_game_actor_fire(sample, &sample->actors[0], 0, 1024, 100);
    }
    if (g.actors[index].hp != replay.actors[index].hp ||
        g.actors[index].evasion.reserve_milli != replay.actors[index].evasion.reserve_milli ||
        g.actors[0].fire_cooldown_ms != replay.actors[0].fire_cooldown_ms)
        return 12;
    __printf("COMBAT geometry/wall/miss/faction/head/pellet-aggregation/deferred passed\n");
    return 0;
}
