/* Stable combat contracts, deliberately independent of balance table values. */
static int combat_capabilities_logic_test(void)
{
    static struct toy_game a, b;
    struct toy_game_actor *pa, *pb;
    struct toy_game_capabilities ca, cb;
    int skill, weapon;
    toy_game_init(&a, 731);
    toy_game_init(&b, 731);
    pa = toy_game_local_player_actor(&a);
    pb = toy_game_local_player_actor(&b);
    pa->class_id = TOY_GAME_AI_LEVEL_1;
    pb->class_id = TOY_GAME_AI_LEVEL_2;
    memset(&pa->skills, 2, sizeof(pa->skills));
    memset(&pb->skills, 0, sizeof(pb->skills));
    pb->kind = TOY_GAME_ACTOR_AI;
    for (skill = 0; skill < TOY_GAME_SKILL_COUNT; skill++)
        if (toy_game_actor_skill_tier(pa, skill) !=
            toy_game_actor_skill_tier(pb, skill)) return 1;
    toy_game_actor_capabilities(pa, TOY_GAME_WEAPON_AK, &ca);
    toy_game_actor_capabilities(pb, TOY_GAME_WEAPON_AK, &cb);
    if (memcmp(&ca, &cb, sizeof(ca))) return 2;
    if (toy_game_actor_move_step(pa, 100) !=
        toy_game_actor_move_step(pb, 100)) return 3;
    if (toy_game_skill_tier(0, 3) != toy_game_skill_tier(1, 1) ||
        toy_game_skill_tier(1, 3) != toy_game_skill_tier(2, 1)) return 4;
    for (weapon = TOY_GAME_WEAPON_PISTOL; weapon <= TOY_GAME_WEAPON_AWP; weapon++) {
        const struct toy_game_weapon_info *w = toy_game_weapon_info(weapon);
        toy_game_actor_equip_weapon(&a, pa, weapon);
        toy_game_actor_equip_weapon(&b, pb, weapon);
        pa->weapon_switch_timer_ms = pb->weapon_switch_timer_ms = 0;
        pa->fire_cooldown_ms = pb->fire_cooldown_ms = 0;
        pa->reloading = pb->reloading = 0;
        pa->slots[pa->current_slot].mag = pb->slots[pb->current_slot].mag = 1;
        toy_game_actor_fire(&a, pa, 0, 1024, 100);
        toy_game_actor_fire(&b, pb, 0, 1024, 100);
        if (pa->fire_cooldown_ms != pb->fire_cooldown_ms ||
            pa->reload_timer_ms != pb->reload_timer_ms ||
            pa->fire_cooldown_ms < w->cooldown_ms ||
            toy_game_actor_current_spread(pa) != toy_game_actor_current_spread(pb))
            return 5;
        memset(&pa->skills, 3, sizeof(pa->skills));
        pa->class_id = TOY_GAME_AI_LEVEL_3;
        toy_game_actor_capabilities(pa, weapon, &ca);
        if (ca.fire_interval_percent < 100) return 6;
        memset(&pa->skills, 2, sizeof(pa->skills));
        pa->class_id = TOY_GAME_AI_LEVEL_1;
    }
    /* Endurance changes actual spawn health; movement and firearm specialties
     * can improve independently, without increasing every capability. */
    toy_game_actor_set_combat_template(pa, TOY_GAME_COMBAT_GUNNER);
    toy_game_actor_capabilities(pa, TOY_GAME_WEAPON_AK, &ca);
    if (pa->max_hp != ca.max_hp || ca.evasion_capacity) return 7;
    pa->skills.mastery[TOY_GAME_SKILL_EVASION] = 2;
    toy_game_actor_capabilities(pa, TOY_GAME_WEAPON_AK, &cb);
    if (cb.evasion_capacity <= 0 || cb.max_hp != ca.max_hp ||
        cb.fire_interval_percent != ca.fire_interval_percent) return 8;
    /* Unchanged weapons retain their infected breakpoints. AK/SMG are tuned
     * through the shared table; their old one-shot thresholds are not a contract. */
    if ((TOY_CONFIG_PURSUIT_COMMON_HP + TOY_CONFIG_PISTOL_DAMAGE - 1) /
            TOY_CONFIG_PISTOL_DAMAGE != 3 ||
        TOY_CONFIG_AWP_DAMAGE < TOY_CONFIG_PURSUIT_HEAVY_HP ||
        (TOY_CONFIG_CHARGER_HP + TOY_CONFIG_AWP_DAMAGE - 1) /
            TOY_CONFIG_AWP_DAMAGE != 2) return 9;
    __printf("COMBAT capabilities/equivalent-tiers/shared-cadence/mechanical-limit passed\n");
    return 0;
}
