/* Unified damage and fixed-step evasion contracts. Presentation must remain
 * an observer even when its update rate differs from the gameplay clock. */
static struct toy_game_actor *combat_evasion_fixture(struct toy_game *combat_case)
{
    struct toy_game_actor *actor;
    toy_game_init(combat_case, 811);
    actor = toy_game_local_player_actor(combat_case);
    toy_game_actor_set_combat_template(actor, TOY_GAME_COMBAT_PLAYER);
    actor->hp = actor->max_hp = 1000;
    return actor;
}

static struct toy_game_damage_result combat_evasion_hit(
    struct toy_game *combat_case, struct toy_game_actor *actor,
    int kind, int base_milli, int health_milli)
{
    struct toy_game_attack attack = {0};
    attack.kind = kind;
    attack.base_damage_milli = base_milli;
    attack.health_damage_milli = health_milli;
    attack.source_x = actor->x - 500;
    attack.source_z = actor->z + 700;
    return toy_game_damage_actor(combat_case, NULL, actor, &attack);
}

static int combat_evasion_logic_test(void)
{
    static struct toy_game combat_case, low_rate, high_rate;
    static struct rasterfall_effects low_fx, high_fx;
    struct toy_game_actor *actor, *low_target, *high_target;
    struct toy_game_damage_result result;
    struct toy_game_capabilities caps;
    int i, before, window, retrigger, reserve, target_index, damage;
    unsigned int sequence;

    /* A partially funded hit consumes the last reserve and leaks only its
     * uncovered fraction. Weakpoints change health loss, never the cost. */
    actor = combat_evasion_fixture(&combat_case);
    actor->evasion.reserve_milli = 15000;
    result = combat_evasion_hit(&combat_case, actor, TOY_GAME_ATTACK_BULLET, 60000, 60000);
    if (result.health_damage != 45 || result.evasion_spent_milli != 15000 ||
        actor->hp != 955 || actor->evasion.reserve_milli ||
        actor->combat_stats.evasion_exhaustions != 1) return 1;
    actor = combat_evasion_fixture(&combat_case);
    actor->evasion.reserve_milli = 15000;
    result = combat_evasion_hit(&combat_case, actor, TOY_GAME_ATTACK_BULLET, 60000, 90000);
    if (result.health_damage != 67 || result.evasion_spent_milli != 15000 ||
        actor->damage_remainder_milli != 500) return 2;

    /* Continued hits get the existing window discount without extending any
     * timer or changing the event direction/strength/sequence. */
    actor = combat_evasion_fixture(&combat_case);
    result = combat_evasion_hit(&combat_case, actor, TOY_GAME_ATTACK_BULLET, 6000, 6000);
    if (result.health_damage || actor->damage_flash_ms ||
        actor->evasion.window_ms != TOY_CONFIG_EVASION_WINDOW_MS ||
        actor->evasion.retrigger_ms != TOY_CONFIG_EVASION_RETRIGGER_MS ||
        actor->evasion.animation_ms != TOY_CONFIG_EVASION_ANIMATION_MS ||
        actor->evasion.sequence != 1) return 3;
    toy_game_update_combat(&combat_case, 100);
    window = actor->evasion.window_ms;
    retrigger = actor->evasion.retrigger_ms;
    sequence = actor->evasion.sequence;
    result = combat_evasion_hit(&combat_case, actor, TOY_GAME_ATTACK_BULLET, 10000, 10000);
    if (result.evasion_spent_milli !=
            10000 * TOY_CONFIG_EVASION_BULLET_WINDOW_PERCENT / 100 ||
        actor->evasion.window_ms != window ||
        actor->evasion.retrigger_ms != retrigger ||
        actor->evasion.sequence != sequence ||
        actor->evasion.strength_milli != 6000) return 4;
    toy_game_update_combat(&combat_case, window);
    result = combat_evasion_hit(&combat_case, actor, TOY_GAME_ATTACK_BULLET, 10000, 10000);
    if (result.evasion_spent_milli != 10000 || actor->evasion.window_ms ||
        actor->evasion.sequence != sequence) return 5;
    toy_game_update_combat(&combat_case, actor->evasion.retrigger_ms);
    result = combat_evasion_hit(&combat_case, actor, TOY_GAME_ATTACK_BULLET, 1000, 1000);
    if (result.health_damage || actor->evasion.sequence != sequence + 1 ||
        actor->evasion.trigger_time_ms != combat_case.combat_time_ms ||
        actor->combat_stats.evasion_events != 2) return 6;

    /* Only successful damage contacts create pressure. Repeated contacts
     * postpone recovery, including the exact sub-step delay boundary. */
    actor = combat_evasion_fixture(&combat_case);
    actor->evasion.reserve_milli = 30000;
    toy_game_actor_capabilities(actor, TOY_GAME_WEAPON_AK, &caps);
    for (i = 0; i < 12; i++) {
        combat_evasion_hit(&combat_case, actor, TOY_GAME_ATTACK_BULLET, 1000, 1000);
        before = actor->evasion.reserve_milli;
        toy_game_update_combat(&combat_case, 100);
        if (actor->evasion.reserve_milli != before) return 7;
    }
    before = actor->evasion.reserve_milli;
    toy_game_update_combat(&combat_case, actor->evasion.pressure_ms - 16);
    if (actor->evasion.reserve_milli != before) return 8;
    toy_game_update_combat(&combat_case, 32);
    if (actor->evasion.pressure_ms || actor->evasion.reserve_milli !=
        before + caps.evasion_per_second * 16) return 9;
    combat_evasion_hit(&combat_case, actor, TOY_GAME_ATTACK_BULLET, 0, 1000);
    combat_evasion_hit(&combat_case, actor, TOY_GAME_ATTACK_BULLET, 1000, 0);
    if (actor->evasion.pressure_ms) return 10;
    actor->control_disabled = 1;
    before = actor->evasion.reserve_milli;
    toy_game_update_combat(&combat_case, 100);
    if (actor->evasion.reserve_milli != before) return 11;
    actor->control_disabled = 0;
    toy_game_set_actor_special_control(actor, TOY_GAME_SPECIAL_CONTROL_SMOKER,
                                        31, 3, 7);
    toy_game_update_combat(&combat_case, 100);
    if (actor->evasion.reserve_milli != before) return 12;
    toy_game_clear_actor_special_control(actor, 0);
    toy_game_update_combat(&combat_case, 100);
    if (actor->evasion.reserve_milli != before + caps.evasion_per_second * 100)
        return 13;

    /* Fractional leakage is accumulated, including after reserve exhaustion;
     * neither tiny hits nor the lingering visual window grant immunity. */
    actor = combat_evasion_fixture(&combat_case);
    actor->evasion.reserve_milli = 0;
    for (i = 0; i < 3; i++) {
        result = combat_evasion_hit(&combat_case, actor, TOY_GAME_ATTACK_BULLET, 250, 250);
        if (result.health_damage || actor->damage_flash_ms) return 14;
    }
    result = combat_evasion_hit(&combat_case, actor, TOY_GAME_ATTACK_BULLET, 250, 250);
    if (result.health_damage != 1 || actor->hp != 999 ||
        actor->damage_remainder_milli || !actor->damage_flash_ms) return 15;
    actor = combat_evasion_fixture(&combat_case);
    actor->evasion.reserve_milli = 499;
    result = combat_evasion_hit(&combat_case, actor, TOY_GAME_ATTACK_BULLET, 1000, 1000);
    if (result.health_damage || result.evasion_spent_milli != 499 ||
        actor->damage_remainder_milli != 501) return 16;
    result = combat_evasion_hit(&combat_case, actor, TOY_GAME_ATTACK_BULLET, 499, 499);
    if (result.health_damage != 1 || result.evasion_spent_milli ||
        actor->damage_remainder_milli || actor->combat_stats.evasion_exhaustions != 1)
        return 17;

    /* Control blocks recovery, not evasion of an outside bullet. Its own
     * damage and physical impacts bypass reserve and preserve real impulse. */
    actor = combat_evasion_fixture(&combat_case);
    toy_game_set_actor_special_control(actor, TOY_GAME_SPECIAL_CONTROL_SMOKER,
                                        42, 3, 7);
    before = actor->x;
    result = combat_evasion_hit(&combat_case, actor, TOY_GAME_ATTACK_BULLET, 1000, 1000);
    if (result.health_damage || !result.evasion_spent_milli || actor->x != before ||
        actor->special_control != TOY_GAME_SPECIAL_CONTROL_SMOKER ||
        actor->special_control_id != 42) return 18;
    reserve = actor->evasion.reserve_milli;
    for (i = TOY_GAME_ATTACK_CONTROL; i <= TOY_GAME_ATTACK_BURN; i++) {
        if (i == TOY_GAME_ATTACK_MELEE) continue;
        result = combat_evasion_hit(&combat_case, actor, i, 1000, 1000);
        if (result.health_damage != 1 || result.evasion_spent_milli ||
            actor->evasion.reserve_milli != reserve) return 19;
    }
    before = actor->hp;
    if (!toy_game_apply_entity_impact(&combat_case, TOY_GAME_ENTITY_PLAYER, 0,
                                      1024, 0, 7) || actor->hp != before - 7 ||
        actor->evasion.reserve_milli != reserve || !actor->control_disabled ||
        actor->airborne_ms <= 0 || actor->knockback_x <= 0) return 20;

    actor = combat_evasion_fixture(&combat_case);
    result = combat_evasion_hit(&combat_case, actor, TOY_GAME_ATTACK_BITE, 1000, 1000);
    damage = 1000 * TOY_CONFIG_EVASION_MELEE_COST_PERCENT / 100;
    if (result.health_damage || result.evasion_spent_milli != damage ||
        result.evasion_spent_milli <= 1000) return 21;
    result = combat_evasion_hit(&combat_case, actor, TOY_GAME_ATTACK_MELEE, 1000, 1000);
    if (result.health_damage || result.evasion_spent_milli !=
        damage * TOY_CONFIG_EVASION_MELEE_WINDOW_PERCENT / 100) return 22;

    /* A dead/downed body accepts no later damage. Reset clears combat history
     * and ray identity, while spawn/revive retains ownership of life state. */
    for (i = TOY_GAME_FACTION_ALLIED; i <= TOY_GAME_FACTION_HOSTILE; i++) {
        actor = combat_evasion_fixture(&combat_case);
        actor->faction = i;
        actor->hp = 5;
        actor->evasion.reserve_milli = 0;
        actor->evasion.window_ms = actor->evasion.animation_ms = 200;
        actor->combat_target.kind = TOY_GAME_ENTITY_ENEMY;
        actor->combat_target.index = 3;
        toy_game_set_actor_special_control(actor, TOY_GAME_SPECIAL_CONTROL_SMOKER,
                                            51, 3, 7);
        result = combat_evasion_hit(&combat_case, actor, TOY_GAME_ATTACK_CONTROL, 60000, 60000);
        if (!result.killed || result.health_damage != 5 || result.overkill != 55 ||
            actor->hp || actor->evasion.window_ms || actor->evasion.animation_ms ||
            actor->special_control || actor->combat_target.kind != -1 ||
            actor->state != (i == TOY_GAME_FACTION_HOSTILE ?
                TOY_GAME_ACTOR_DEAD : TOY_GAME_ACTOR_DOWNED)) return 23;
        result = combat_evasion_hit(&combat_case, actor, TOY_GAME_ATTACK_BULLET, 60000, 60000);
        if (result.killed || result.health_damage || result.evasion_spent_milli)
            return 24;
        actor->damage_remainder_milli = 333;
        actor->fire_seq = 9;
        actor->ray_count = 1;
        actor->rays[0].actor_index = 4;
        actor->rays[0].enemy_index = 5;
        toy_game_actor_reset_combat(actor, 1);
        toy_game_actor_capabilities(actor, TOY_GAME_WEAPON_AK, &caps);
        if (actor->evasion.reserve_milli != caps.evasion_capacity * 1000 ||
            actor->evasion.sequence || actor->evasion.pressure_ms ||
            actor->evasion.retrigger_ms || actor->damage_remainder_milli ||
            actor->fire_seq || actor->ray_count || actor->rays[0].actor_index != -1 ||
            actor->rays[0].enemy_index != -1 || actor->combat_stats.damage_taken ||
            actor->combat_target.kind != -1 || actor->combat_target.index != -1)
            return 25;
    }

    /* Both simulations execute the same fixed gameplay steps and firing.
     * One renders four times per tick, the other only once every eight ticks. */
    combat_evasion_fixture(&low_rate);
    target_index = toy_game_add_ai(&low_rate, TOY_GAME_AI_LEVEL_2, 0, 2000, "EVADE");
    if (target_index < 0) return 26;
    low_target = toy_game_actor_by_id(&low_rate, target_index);
    if (!low_target) return 26;
    target_index = (int)(low_target - low_rate.actors);
    toy_game_actor_set_combat_template(low_target, TOY_GAME_COMBAT_ELITE);
    low_target->faction = TOY_GAME_FACTION_HOSTILE;
    low_target->hp = low_target->max_hp = 10000;
    toy_game_actor_equip_weapon(&low_rate, &low_rate.actors[0], TOY_GAME_WEAPON_AK);
    low_rate.actors[0].weapon_switch_timer_ms = 0;
    memcpy(&high_rate, &low_rate, sizeof(low_rate));
    high_target = &high_rate.actors[target_index];
    rasterfall_effects_init(&low_fx);
    rasterfall_effects_init(&high_fx);
    for (i = 0; i < 640; i++) {
        int frame;
        toy_game_update_combat(&low_rate, 16);
        toy_game_update_combat(&high_rate, 16);
        toy_game_update_actor_weapon_held(&low_rate, &low_rate.actors[0],
                                          NULL, 0, 1, 1024, 0, 16, 100);
        toy_game_update_actor_weapon_held(&high_rate, &high_rate.actors[0],
                                          NULL, 0, 1, 1024, 0, 16, 100);
        if (i % 240 == 0) {
            combat_evasion_hit(&low_rate, low_target, TOY_GAME_ATTACK_BULLET, 5000, 5000);
            combat_evasion_hit(&high_rate, high_target, TOY_GAME_ATTACK_BULLET, 5000, 5000);
        }
        if (i % 8 == 0) {
            rasterfall_effects_sync_evasion(&low_fx, &low_rate);
            rasterfall_effects_update(&low_fx, 128);
        }
        for (frame = 0; frame < 4; frame++) {
            rasterfall_effects_sync_evasion(&high_fx, &high_rate);
            rasterfall_effects_update(&high_fx, 4);
        }
    }
    if (!low_rate.actors[0].combat_stats.shots ||
        low_target->combat_stats.evasion_events < 2 ||
        low_rate.combat_time_ms != high_rate.combat_time_ms ||
        low_rate.rng != high_rate.rng ||
        memcmp(low_rate.actors, high_rate.actors, sizeof(low_rate.actors))) return 27;
    /* Existing multi-Smoker connections remain enemy-owned. A successful
     * outside evade neither drops a connection nor changes pull/contact ticks. */
    actor = combat_evasion_fixture(&combat_case);
    combat_case.to_spawn = 0;
    combat_case.primitive_count = 0;
    combat_case.room_limit = 12000;
    actor->x = actor->z = 0;
    for (i = 0; i < 2; i++) {
        struct toy_game_enemy *smoker = &combat_case.enemies[i];
        memset(smoker, 0, sizeof(*smoker));
        smoker->active = 1;
        smoker->type = TOY_GAME_ENEMY_SMOKER;
        smoker->hp = TOY_CONFIG_SMOKER_HP;
        smoker->x = i ? 300 : -300;
        smoker->ability.special_target_active = 1;
        smoker->ability.special_target_kind = TOY_GAME_TARGET_HOST;
        smoker->ability.special_target_index = -1;
        smoker->ability.special_pull_timer_ms = 5000;
    }
    combat_case.enemies_alive = 2;
    before = actor->hp;
    reserve = actor->evasion.reserve_milli;
    toy_game_update_world(&combat_case, 16);
    if (actor->hp != before - 2 * TOY_GAME_SMOKER_DAMAGE ||
        actor->evasion.reserve_milli != reserve || !actor->control_disabled ||
        !combat_case.enemies[0].ability.special_target_active ||
        !combat_case.enemies[1].ability.special_target_active) return 28;
    memcpy(&low_rate, &combat_case, sizeof(low_rate));
    result = combat_evasion_hit(&combat_case, actor, TOY_GAME_ATTACK_BULLET, 1000, 1000);
    if (result.health_damage || result.evasion_spent_milli != 1000) return 29;
    toy_game_update_world(&combat_case, 16);
    toy_game_update_world(&low_rate, 16);
    if (actor->x != low_rate.actors[0].x || actor->z != low_rate.actors[0].z ||
        actor->hp != low_rate.actors[0].hp ||
        actor->special_control != TOY_GAME_SPECIAL_CONTROL_SMOKER ||
        !combat_case.enemies[0].ability.special_target_active ||
        !combat_case.enemies[1].ability.special_target_active) return 30;
    result = combat_evasion_hit(&combat_case, actor, TOY_GAME_ATTACK_CONTROL,
                                actor->hp * 1000, actor->hp * 1000);
    toy_game_update_world(&combat_case, 16);
    if (!result.killed || actor->special_control || actor->control_disabled ||
        combat_case.enemies[0].ability.special_target_active ||
        combat_case.enemies[1].ability.special_target_active) return 31;
    __printf("COMBAT evasion/window/pressure/fraction/control/death/render-rate passed\n");
    return 0;
}
