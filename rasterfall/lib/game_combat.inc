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
    out->evasion_capacity = evasion[ei];
    out->evasion_per_second = recovery[ei];
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
}
