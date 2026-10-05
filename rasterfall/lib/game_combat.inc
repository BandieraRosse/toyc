/* Deterministic combat capabilities. Included by game.c; no presentation,
 * system clock, allocation or faction-specific multipliers belong here. */

int toy_game_skill_tier(int class_id, int mastery)
{
    int tier;
    if (class_id < 0) class_id = 0;
    if (class_id > 2) class_id = 2;
    if (mastery < 0) mastery = 0;
    if (mastery > 3) mastery = 3;
    tier = 1 + class_id * 2 + mastery;
    return tier;
}

int toy_game_actor_skill_tier(const struct toy_game_actor *actor, int skill)
{
    if (!actor || skill < 0 || skill >= TOY_GAME_SKILL_COUNT) return 1;
    return toy_game_skill_tier(actor->class_id, actor->skills.mastery[skill]);
}

void toy_game_actor_capabilities(const struct toy_game_actor *actor, int weapon,
                                 struct toy_game_capabilities *out)
{
    static const int move[] = {95, 98, 100, 103, 106, 109, 112, 115};
    static const int interval[] = {150, 135, 120, 110, 105, 100, 100, 100};
    static const int reload[] = {120, 112, 104, 96, 90, 85, 80, 75};
    static const int heat[] = {140, 128, 116, 108, 100, 94, 88, 82};
    static const int spread[] = {120, 114, 108, 102, 96, 90, 84, 78};
    static const int turn[] = {240, 310, 380, 450, 520, 590, 660, 720};
    static const int aim[] = {400, 350, 300, 250, 220, 190, 160, 130};
    static const int evasion[] = {0, 0, 45, 65, 90, 110, 130, 150};
    static const int recovery[] = {0, 0, 10, 12, 14, 16, 18, 20};
    int w = weapon >= TOY_GAME_WEAPON_PISTOL && weapon <= TOY_GAME_WEAPON_AWP ?
        weapon : TOY_GAME_SKILL_PISTOL;
    int wi = toy_game_actor_skill_tier(actor, w) - 1;
    int mi = toy_game_actor_skill_tier(actor, TOY_GAME_SKILL_MOVEMENT) - 1;
    int ei = toy_game_actor_skill_tier(actor, TOY_GAME_SKILL_EVASION) - 1;
    int si = toy_game_actor_skill_tier(actor, TOY_GAME_SKILL_ENDURANCE) - 1;
    int pi = toy_game_actor_skill_tier(actor, TOY_GAME_SKILL_PRECISION) - 1;
    if (!out) return;
    out->max_hp = 80 + si * 20;
    out->carry_capacity = 20 + si * 5; /* reserved equipment interface, no bag */
    out->move_percent = move[mi];
    out->fire_interval_percent = interval[wi]; /* never faster than mechanics */
    out->reload_percent = reload[wi];
    out->heat_percent = heat[wi];
    out->spread_percent = spread[pi];
    out->turn_degrees = turn[pi];
    out->aim_ms = aim[pi];
    out->weakpoint_percent = 150 + pi * 5;
    out->evasion_capacity = actor && actor->base_core ? 0 : evasion[ei];
    out->evasion_per_second = actor && actor->base_core ? 0 : recovery[ei];
}

int toy_game_actor_move_step(const struct toy_game_actor *actor, int base_step)
{
    struct toy_game_capabilities caps;
    toy_game_actor_capabilities(actor, TOY_GAME_WEAPON_PISTOL, &caps);
    return base_step * caps.move_percent / 100;
}

void toy_game_actor_set_combat_template(struct toy_game_actor *actor,
                                       int template_id)
{
    struct toy_game_capabilities caps;
    int i;
    if (!actor) return;
    memset(&actor->skills, 0, sizeof(actor->skills));
    if (template_id == TOY_GAME_COMBAT_PLAYER) {
        actor->class_id = TOY_GAME_AI_LEVEL_2;
        for (i = 0; i <= TOY_GAME_SKILL_SNIPER; i++)
            actor->skills.mastery[i] = 1;
        actor->skills.mastery[TOY_GAME_SKILL_EVASION] = 2;
        actor->skills.mastery[TOY_GAME_SKILL_PRECISION] = 1;
    } else if (template_id == TOY_GAME_COMBAT_MOBILE) {
        actor->skills.mastery[TOY_GAME_SKILL_MOVEMENT] = 2;
        actor->skills.mastery[TOY_GAME_SKILL_EVASION] = 2;
    } else if (template_id == TOY_GAME_COMBAT_GUNNER) {
        actor->class_id = TOY_GAME_AI_LEVEL_1;
        actor->skills.mastery[TOY_GAME_SKILL_RIFLE] = 1;
        actor->skills.mastery[TOY_GAME_SKILL_ENDURANCE] = 1;
    } else if (template_id == TOY_GAME_COMBAT_ELITE) {
        actor->class_id = TOY_GAME_AI_LEVEL_2;
        actor->skills.mastery[TOY_GAME_SKILL_RIFLE] = 2;
        actor->skills.mastery[TOY_GAME_SKILL_MOVEMENT] = 1;
        actor->skills.mastery[TOY_GAME_SKILL_EVASION] = 1;
        actor->skills.mastery[TOY_GAME_SKILL_PRECISION] = 1;
    }
    toy_game_actor_capabilities(actor, TOY_GAME_WEAPON_PISTOL, &caps);
    actor->hp = actor->max_hp = caps.max_hp;
    toy_game_actor_reset_combat(actor, 1);
}

int toy_game_actors_hostile(const struct toy_game_actor *a,
                            const struct toy_game_actor *b)
{
    return a && b && a != b && a->faction != b->faction;
}

void toy_game_actor_reset_combat(struct toy_game_actor *actor, int refill_evasion)
{
    struct toy_game_capabilities caps;
    int i;
    if (!actor) return;
    toy_game_actor_capabilities(actor, toy_game_actor_current_weapon(actor), &caps);
    memset(&actor->evasion, 0, sizeof(actor->evasion));
    memset(&actor->combat_stats, 0, sizeof(actor->combat_stats));
    memset(&actor->combat_target, 0, sizeof(actor->combat_target));
    actor->combat_target.kind = -1;
    actor->combat_target.index = -1;
    actor->evasion.reserve_milli = refill_evasion ? caps.evasion_capacity * 1000 : 0;
    actor->combat_scan_ms = actor->combat_aim_ms = actor->combat_lost_ms = 0;
    actor->combat_last_x = actor->combat_last_z = actor->combat_last_y = 0;
    actor->ai_rescue_active = 0; actor->ai_rescue_index = -1;
    actor->ai_rescue_generation = 0;
    actor->damage_remainder_milli = 0;
    actor->fire_cooldown_ms = actor->reload_timer_ms = actor->reloading = 0;
    actor->weapon_switch_timer_ms = actor->weapon_spread_heat = 0;
    actor->melee_timer_ms = actor->throw_timer_ms = 0;
    actor->ai_shove_cooldown_ms = actor->ai_turn_remainder = 0;
    actor->damage_flash_ms = actor->muzzle_flash_ms = 0;
    actor->fire_seq = 0;
    actor->ray_count = 0;
    memset(actor->rays, 0, sizeof(actor->rays));
    for (i = 0; i < TOY_GAME_MAX_RAYS; i++)
        actor->rays[i].actor_index = actor->rays[i].enemy_index = -1;
    actor->nav_active = 0;
    actor->nav_direct_valid = actor->nav_direct_ms = 0;
    actor->jump_coyote_steps = actor->jump_buffer_steps = 0;
}

static int combat_timer(int value, int dt_ms)
{
    return value > dt_ms ? value - dt_ms : 0;
}

void toy_game_update_combat(struct toy_game *g, int dt_ms)
{
    int i;
    if (!g || dt_ms <= 0) return;
    g->combat_time_ms += dt_ms;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
        struct toy_game_actor *a = &g->actors[i];
        struct toy_game_capabilities caps;
        int recovery_ms;
        if (!a->active) continue;
        a->evasion.window_ms = combat_timer(a->evasion.window_ms, dt_ms);
        a->evasion.retrigger_ms = combat_timer(a->evasion.retrigger_ms, dt_ms);
        a->evasion.animation_ms = combat_timer(a->evasion.animation_ms, dt_ms);
        recovery_ms = dt_ms > a->evasion.pressure_ms ?
            dt_ms - a->evasion.pressure_ms : 0;
        a->evasion.pressure_ms = combat_timer(a->evasion.pressure_ms, dt_ms);
        if (a->state != TOY_GAME_ACTOR_ALIVE || a->hp <= 0) {
            a->evasion.reserve_milli = 0;
            continue;
        }
        toy_game_actor_capabilities(a, toy_game_actor_current_weapon(a), &caps);
        if (recovery_ms && !a->control_disabled &&
            a->special_control == TOY_GAME_SPECIAL_CONTROL_NONE)
            a->evasion.reserve_milli += caps.evasion_per_second * recovery_ms;
        if (a->evasion.reserve_milli > caps.evasion_capacity * 1000)
            a->evasion.reserve_milli = caps.evasion_capacity * 1000;
    }
}

struct toy_game_damage_result toy_game_damage_actor(
    struct toy_game *g, struct toy_game_actor *source,
    struct toy_game_actor *target, const struct toy_game_attack *attack)
{
    struct toy_game_damage_result result = {0};
    struct toy_game_capabilities caps;
    long long health_milli;
    int eligible, cost, spent, damage;
    if (!g || !target || !attack || !target->active ||
        target->state != TOY_GAME_ACTOR_ALIVE || target->hp <= 0 ||
        attack->base_damage_milli <= 0 || attack->health_damage_milli <= 0)
        return result;
    target->evasion.pressure_ms = TOY_CONFIG_EVASION_RECOVERY_DELAY_MS;
    health_milli = attack->health_damage_milli;
    eligible = attack->kind == TOY_GAME_ATTACK_BULLET ||
               attack->kind == TOY_GAME_ATTACK_BITE ||
               attack->kind == TOY_GAME_ATTACK_MELEE;
    toy_game_actor_capabilities(target, toy_game_actor_current_weapon(target), &caps);
    if (eligible && caps.evasion_capacity && target->evasion.reserve_milli > 0) {
        int was_window = target->evasion.window_ms > 0;
        cost = attack->base_damage_milli;
        if (attack->kind != TOY_GAME_ATTACK_BULLET)
            cost = cost * TOY_CONFIG_EVASION_MELEE_COST_PERCENT / 100;
        if (was_window) cost = cost *
            (attack->kind == TOY_GAME_ATTACK_BULLET ?
             TOY_CONFIG_EVASION_BULLET_WINDOW_PERCENT :
             TOY_CONFIG_EVASION_MELEE_WINDOW_PERCENT) / 100;
        if (cost < 1) cost = 1;
        spent = target->evasion.reserve_milli < cost ?
            target->evasion.reserve_milli : cost;
        target->evasion.reserve_milli -= spent;
        health_milli = health_milli * (cost - spent) / cost;
        result.evasion_spent_milli = spent;
        target->combat_stats.evasion_spent_milli += spent;
        if (!target->evasion.reserve_milli)
            target->combat_stats.evasion_exhaustions++;
        if (!was_window && !target->evasion.retrigger_ms) {
            target->evasion.window_ms = TOY_CONFIG_EVASION_WINDOW_MS;
            target->evasion.retrigger_ms = TOY_CONFIG_EVASION_RETRIGGER_MS;
            target->evasion.animation_ms = TOY_CONFIG_EVASION_ANIMATION_MS;
            target->evasion.trigger_time_ms = g->combat_time_ms;
            target->evasion.sequence++;
            target->evasion.source_x = attack->source_x;
            target->evasion.source_z = attack->source_z;
            target->evasion.strength_milli = spent;
            target->combat_stats.evasion_events++;
            toy_game_emit_event(g, TOY_GAME_EV_EVADE);
        }
    }
    /* Fractional leakage accumulates, so a small reserve cannot round every
     * low-damage hit down to zero. Armor is absent in V0: absorption stays 0. */
    health_milli += target->damage_remainder_milli;
    damage = (int)(health_milli / 1000);
    target->damage_remainder_milli = (int)(health_milli % 1000);
    result.health_damage = damage < target->hp ? damage : target->hp;
    result.overkill = damage - result.health_damage;
    target->hp -= result.health_damage;
    target->combat_stats.damage_taken += result.health_damage;
    if (source) {
        source->combat_stats.health_damage += result.health_damage;
        source->combat_stats.overkill += result.overkill;
        source->damage_dealt += result.health_damage;
    }
    if (result.health_damage) {
        target->damage_flash_ms = TOY_GAME_DAMAGE_FLASH_MS;
        toy_game_actor_set_animation(target, TOY_GAME_ANIM_HIT);
    }
    if (!target->hp) {
        result.killed = 1;
        target->state = target->faction == TOY_GAME_FACTION_HOSTILE ?
            TOY_GAME_ACTOR_DEAD : TOY_GAME_ACTOR_DOWNED;
        target->revive_progress_ms = 0;
        target->evasion.reserve_milli = 0;
        target->evasion.window_ms = target->evasion.animation_ms = 0;
        target->combat_target.kind = -1;
        target->combat_aim_ms = 0;
        toy_game_clear_actor_special_control(target, 0);
        toy_game_actor_set_animation(target, TOY_GAME_ANIM_DEATH);
        if (source) {
            source->kills++;
            toy_game_emit_event(g, TOY_GAME_EV_KILL);
        }
        if (target->state == TOY_GAME_ACTOR_DOWNED)
            toy_game_emit_event(g, TOY_GAME_EV_ACTOR_DOWN);
    }
    return result;
}
