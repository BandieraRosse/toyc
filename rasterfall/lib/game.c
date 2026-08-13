/*
 * game.c — 僵尸潮射击游戏规则（平台无关）。
 *
 * 全部规则集中于此：xorshift64* PRNG、世界碰撞查询、僵尸 AI（追逐/
 * 攻击/分离/倒地，以及无视遮挡持续追踪玩家的尸潮追踪者）、波次或
 * 固定区域生成、安全室与终点、hitscan 射击与障碍遮挡、弹匣/换弹、
 * 玩家生命/死亡/通关冻结、事件队列。
 *
 * 实现约束（自托管友好，同 renderer.c 注释风格）：
 *  - 禁整结构赋值（用 memcpy / 逐字段）
 *  - 变量声明放块首
 *  - 命中判定禁 int32 平方（|cross| 最大 ~16.4M，平方会溢出 2^31），
 *    用 |cross| 与 R*1024 直接比较
 */

#include "toy_game.h"
#include "string.h"
#include "math.h"
#include "tlibc_compat.h"

/* Transitional field aliases keep the AI behavior patch small while the
 * special state moves behind enemy.ability.  New code should use ability.*. */

static int enemy_target_reachable(const struct toy_game *g,
                                  const struct toy_game_enemy *e,
                                  int x, int z);
static int enemy_target_valid(const struct toy_game *g,
                              const struct toy_game_enemy *e,
                              int target_kind, int target_index,
                              int *out_x, int *out_z);
static int ai_try_shove(struct toy_game *g, struct toy_game_actor *actor);

/* ── PRNG：xorshift64* ──────────────────────────────────────────── */

static uint64_t xorshift64star(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

/* [lo, hi] 闭区间随机整数 */
static int rand_range(struct toy_game *g, int lo, int hi)
{
    uint64_t raw = xorshift64star(&g->rng) & 0xffffffffULL;
    int span = hi - lo + 1;
    return lo + (int)(raw % (uint64_t)span);
}

/* ── 武器定义 ─────────────────────────────────────────────────── */

/* ── 战斗平衡配置 ────────────────────────────────────────────────
 * 改武器伤害、敌人生命或敌人近战伤害时只改这里；下面的规则表不再
 * 直接散落这些平衡数字。弹匣容量、射速和移动速度仍在各自表中维护。 */
/* 固定散射：每颗弹丸在 [-spread, +spread] 内随机偏转（1024 定点）。
 * 手枪 ±12（≈±0.7°，几乎精准）；SMG ±90（≈±5°，连发略散）；
 * 霰弹枪 ±230（≈±12.7°，近距离密集、远距离发散）。 */
static const struct toy_game_weapon_info weapon_table[TOY_GAME_WEAPON_COUNT] = {
    { TOY_CONFIG_PISTOL_MAG, TOY_CONFIG_PISTOL_RESERVE,
      TOY_CONFIG_PISTOL_COOLDOWN_MS, TOY_CONFIG_PISTOL_RELOAD_MS,
      TOY_CONFIG_PISTOL_FULL_AUTO, TOY_CONFIG_PISTOL_PELLETS,
      TOY_CONFIG_PISTOL_SPREAD, TOY_CONFIG_PISTOL_SLOT,
      TOY_CONFIG_PISTOL_DAMAGE,
      TOY_GAME_WEAPON_ID_PISTOL, "PISTOL", "PG",
      TOY_GAME_MUZZLE_STANDARD, TOY_CONFIG_PISTOL_RANGE,
      TOY_CONFIG_PISTOL_ALERT_RANGE, 0 },
    { TOY_CONFIG_SMG_MAG, TOY_CONFIG_SMG_RESERVE,
      TOY_CONFIG_SMG_COOLDOWN_MS, TOY_CONFIG_SMG_RELOAD_MS,
      TOY_CONFIG_SMG_FULL_AUTO, TOY_CONFIG_SMG_PELLETS,
      TOY_CONFIG_SMG_SPREAD, TOY_CONFIG_SMG_SLOT, TOY_CONFIG_SMG_DAMAGE,
      TOY_GAME_WEAPON_ID_SMG, "SMG", "SMG",
      TOY_GAME_MUZZLE_STANDARD, TOY_CONFIG_SMG_RANGE,
      TOY_CONFIG_SMG_ALERT_RANGE, 0 },
    { TOY_CONFIG_SHOTGUN_MAG, TOY_CONFIG_SHOTGUN_RESERVE,
      TOY_CONFIG_SHOTGUN_COOLDOWN_MS, TOY_CONFIG_SHOTGUN_RELOAD_MS,
      TOY_CONFIG_SHOTGUN_FULL_AUTO, TOY_CONFIG_SHOTGUN_PELLETS,
      TOY_CONFIG_SHOTGUN_SPREAD, TOY_CONFIG_SHOTGUN_SLOT,
      TOY_CONFIG_SHOTGUN_DAMAGE,
      TOY_GAME_WEAPON_ID_SHOTGUN, "SHOTGUN", "SG",
      TOY_GAME_MUZZLE_SHOTGUN, TOY_CONFIG_SHOTGUN_RANGE,
      TOY_CONFIG_SHOTGUN_ALERT_RANGE, 0 },
    { TOY_CONFIG_AK_MAG, TOY_CONFIG_AK_RESERVE,
      TOY_CONFIG_AK_COOLDOWN_MS, TOY_CONFIG_AK_RELOAD_MS,
      TOY_CONFIG_AK_FULL_AUTO, TOY_CONFIG_AK_PELLETS,
      TOY_CONFIG_AK_SPREAD, TOY_CONFIG_AK_SLOT, TOY_CONFIG_AK_DAMAGE,
      TOY_GAME_WEAPON_ID_AK, "AK", "AK", TOY_GAME_MUZZLE_STANDARD,
      TOY_CONFIG_AK_RANGE, TOY_CONFIG_AK_ALERT_RANGE, 0 },
    { TOY_CONFIG_AWP_MAG, TOY_CONFIG_AWP_RESERVE,
      TOY_CONFIG_AWP_COOLDOWN_MS, TOY_CONFIG_AWP_RELOAD_MS,
      TOY_CONFIG_AWP_FULL_AUTO, TOY_CONFIG_AWP_PELLETS,
      TOY_CONFIG_AWP_SPREAD, TOY_CONFIG_AWP_SLOT, TOY_CONFIG_AWP_DAMAGE,
      TOY_GAME_WEAPON_ID_AWP, "AWP", "AWP", TOY_GAME_MUZZLE_STANDARD,
      TOY_CONFIG_AWP_RANGE, TOY_CONFIG_AWP_ALERT_RANGE,
      TOY_CONFIG_COMBAT_AWP_POWER_BIAS },
};

static const struct toy_game_enemy_info enemy_table[TOY_GAME_ENEMY_TYPE_COUNT] = {
    /* max hp, speed range, bite damage, model, base color */
    { TOY_CONFIG_COMMON_HP, TOY_CONFIG_COMMON_SPEED_MIN, TOY_CONFIG_COMMON_SPEED_MAX, TOY_CONFIG_COMMON_BITE_DAMAGE, 0, RF_COLOR_ENEMY_COMMON, TOY_GAME_ENEMY_ID_COMMON, "COMMON", TOY_GAME_ENEMY_ABILITY_NONE },
    /* PURSUIT_COMMON is the ordinary tracking zombie.  Its numeric content
     * ID remains 110 for save/network compatibility; PURSUIT_FAST is the
     * faster red variant. */
    { TOY_CONFIG_PURSUIT_COMMON_HP, TOY_CONFIG_PURSUIT_COMMON_SPEED_MIN, TOY_CONFIG_PURSUIT_COMMON_SPEED_MAX, TOY_CONFIG_PURSUIT_COMMON_BITE_DAMAGE, 1, RF_COLOR_ENEMY_PURSUIT_COMMON, TOY_GAME_ENEMY_ID_PURSUIT_COMMON, "PURSUIT_COMMON", TOY_GAME_ENEMY_ABILITY_NONE },
    { TOY_CONFIG_HEAVY_HP, TOY_CONFIG_HEAVY_SPEED_MIN, TOY_CONFIG_HEAVY_SPEED_MAX, TOY_CONFIG_HEAVY_BITE_DAMAGE, 2, RF_COLOR_ENEMY_HEAVY, TOY_GAME_ENEMY_ID_HEAVY, "HEAVY", TOY_GAME_ENEMY_ABILITY_NONE },
    { TOY_CONFIG_PURSUIT_HEAVY_HP, TOY_CONFIG_PURSUIT_HEAVY_SPEED_MIN, TOY_CONFIG_PURSUIT_HEAVY_SPEED_MAX, TOY_CONFIG_PURSUIT_HEAVY_BITE_DAMAGE, 2, RF_COLOR_ENEMY_PURSUIT_HEAVY, TOY_GAME_ENEMY_ID_PURSUIT_HEAVY, "PURSUIT_HEAVY", TOY_GAME_ENEMY_ABILITY_NONE },
    { TOY_CONFIG_PURSUIT_FAST_HP, TOY_CONFIG_PURSUIT_FAST_SPEED_MIN, TOY_CONFIG_PURSUIT_FAST_SPEED_MAX, TOY_CONFIG_PURSUIT_FAST_BITE_DAMAGE, 1, RF_COLOR_ENEMY_PURSUIT_FAST, TOY_GAME_ENEMY_ID_PURSUIT_FAST, "PURSUIT_FAST", TOY_GAME_ENEMY_ABILITY_NONE },
    { TOY_CONFIG_SMOKER_HP, TOY_CONFIG_SMOKER_SPEED_MIN, TOY_CONFIG_SMOKER_SPEED_MAX, TOY_CONFIG_SMOKER_BITE_DAMAGE, 1, RF_COLOR_ENEMY_SMOKER, TOY_GAME_ENEMY_ID_SMOKER, "SMOKER", TOY_GAME_ENEMY_ABILITY_SMOKER_TONGUE },
    { TOY_CONFIG_CHARGER_HP, TOY_CONFIG_CHARGER_SPEED_MIN, TOY_CONFIG_CHARGER_SPEED_MAX, TOY_CONFIG_CHARGER_BITE_DAMAGE, 2, RF_COLOR_ENEMY_CHARGER, TOY_GAME_ENEMY_ID_CHARGER, "CHARGER", TOY_GAME_ENEMY_ABILITY_CHARGER_RUSH },
    { TOY_CONFIG_TANK_HP, TOY_CONFIG_TANK_SPEED_MIN, TOY_CONFIG_TANK_SPEED_MAX, TOY_CONFIG_TANK_BITE_DAMAGE, 2, RF_COLOR_ENEMY_TANK, TOY_GAME_ENEMY_ID_TANK, "TANK", TOY_GAME_ENEMY_ABILITY_TANK_SWEEP }
};

struct toy_game_ai_info {
    int max_hp;
    unsigned int body_color;
    int fire_interval_percent;
    int turn_speed_degree;
    int shove_cooldown_ms;
    int move_speed;
    int spread_percent;
};

static const struct toy_game_ai_info ai_table[TOY_GAME_AI_CLASS_COUNT] = {
    { TOY_CONFIG_AI_LEVEL_1_HP, RF_COLOR_AI_BASIC,
      TOY_CONFIG_AI_LEVEL_1_FIRE_INTERVAL_PERCENT,
      TOY_CONFIG_AI_LEVEL_1_TURN_SPEED_DEGREE,
      TOY_CONFIG_AI_LEVEL_1_SHOVE_COOLDOWN_MS,
      TOY_CONFIG_AI_LEVEL_1_MOVE_SPEED,
      TOY_CONFIG_AI_LEVEL_1_SPREAD_PERCENT },
    { TOY_CONFIG_AI_LEVEL_2_HP, RF_COLOR_AI_RIFLE,
      TOY_CONFIG_AI_LEVEL_2_FIRE_INTERVAL_PERCENT,
      TOY_CONFIG_AI_LEVEL_2_TURN_SPEED_DEGREE,
      TOY_CONFIG_AI_LEVEL_2_SHOVE_COOLDOWN_MS,
      TOY_CONFIG_AI_LEVEL_2_MOVE_SPEED,
      TOY_CONFIG_AI_LEVEL_2_SPREAD_PERCENT },
    { TOY_CONFIG_AI_LEVEL_3_HP, RF_COLOR_AI_HEAVY,
      TOY_CONFIG_AI_LEVEL_3_FIRE_INTERVAL_PERCENT,
      TOY_CONFIG_AI_LEVEL_3_TURN_SPEED_DEGREE,
      TOY_CONFIG_AI_LEVEL_3_SHOVE_COOLDOWN_MS,
      TOY_CONFIG_AI_LEVEL_3_MOVE_SPEED,
      TOY_CONFIG_AI_LEVEL_3_SPREAD_PERCENT }
};

static int ai_random_weapon(struct toy_game *g, int class_id)
{
    if (class_id == TOY_GAME_AI_LEVEL_1) return TOY_GAME_WEAPON_PISTOL;
    if (class_id == TOY_GAME_AI_LEVEL_2)
        return rand_range(g, TOY_GAME_WEAPON_SMG, TOY_GAME_WEAPON_SHOTGUN);
    return rand_range(g, TOY_GAME_WEAPON_AK, TOY_GAME_WEAPON_AWP);
}

static void actor_clear_weapons(struct toy_game_actor *a)
{
    memset(a->slots, 0, sizeof(a->slots));
    a->slots[0].weapon = -1;
    a->slots[1].weapon = -1;
    a->current_slot = 0;
}

static void actor_set_weapon(struct toy_game_actor *a, int weapon)
{
    const struct toy_game_weapon_info *info = toy_game_weapon_info(weapon);
    int slot = info->slot;
    actor_clear_weapons(a);
    a->slots[slot].weapon = weapon;
    a->slots[slot].mag = info->mag_size;
    a->slots[slot].reserve = TOY_GAME_AMMO_INFINITE;
    a->current_slot = slot;
}

const struct toy_game_weapon_info *toy_game_weapon_info(int weapon)
{
    const struct toy_game_weapon_info *info =
        toy_game_weapon_info_or_null(weapon);
    if (!info)
        return &weapon_table[TOY_GAME_WEAPON_PISTOL];
    return info;
}

const struct toy_game_weapon_info *toy_game_weapon_info_or_null(int weapon)
{
    if (weapon < 0 || weapon >= TOY_GAME_WEAPON_COUNT) return NULL;
    return &weapon_table[weapon];
}

int toy_game_weapon_is_valid(int weapon)
{
    return toy_game_weapon_info_or_null(weapon) != NULL;
}

int toy_game_weapon_content_id(int weapon)
{
    const struct toy_game_weapon_info *info =
        toy_game_weapon_info_or_null(weapon);
    return info ? info->content_id : -1;
}

int toy_game_weapon_from_content_id(int content_id)
{
    int i;
    for (i = 0; i < TOY_GAME_WEAPON_COUNT; i++)
        if (weapon_table[i].content_id == content_id) return i;
    return -1;
}

const char *toy_game_weapon_name(int weapon)
{
    const struct toy_game_weapon_info *info =
        toy_game_weapon_info_or_null(weapon);
    return info ? info->name : "UNKNOWN";
}

int toy_game_weapon_from_name(const char *name)
{
    int i;
    if (!name) return -1;
    for (i = 0; i < TOY_GAME_WEAPON_COUNT; i++)
        if (!strcmp(name, weapon_table[i].name) ||
            (i == TOY_GAME_WEAPON_PISTOL && !strcmp(name, "pistol")) ||
            (i == TOY_GAME_WEAPON_SMG && !strcmp(name, "smg")) ||
            (i == TOY_GAME_WEAPON_SHOTGUN && !strcmp(name, "shotgun")) ||
            (i == TOY_GAME_WEAPON_AK && !strcmp(name, "ak")) ||
            (i == TOY_GAME_WEAPON_AWP && !strcmp(name, "awp")))
            return i;
    return -1;
}

const struct toy_game_enemy_info *toy_game_enemy_info(int type)
{
    const struct toy_game_enemy_info *info =
        toy_game_enemy_info_or_null(type);
    if (!info)
        return &enemy_table[TOY_GAME_ENEMY_COMMON];
    return info;
}

const struct toy_game_enemy_info *toy_game_enemy_info_or_null(int type)
{
    if (type < 0 || type >= TOY_GAME_ENEMY_TYPE_COUNT) return NULL;
    return &enemy_table[type];
}

int toy_game_enemy_type_is_valid(int type)
{
    return toy_game_enemy_info_or_null(type) != NULL;
}

int toy_game_enemy_content_id(int type)
{
    const struct toy_game_enemy_info *info =
        toy_game_enemy_info_or_null(type);
    return info ? info->content_id : -1;
}

int toy_game_enemy_from_content_id(int content_id)
{
    int i;
    for (i = 0; i < TOY_GAME_ENEMY_TYPE_COUNT; i++)
        if (enemy_table[i].content_id == content_id) return i;
    return -1;
}

struct toy_game_actor *toy_game_actor_by_id(struct toy_game *g, int actor_id)
{
    int i;
    if (!g) return NULL;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++)
        if (g->actors[i].active && g->actors[i].actor_id == actor_id)
            return &g->actors[i];
    return NULL;
}

const struct toy_game_actor *toy_game_actor_by_id_const(const struct toy_game *g,
                                                        int actor_id)
{
    int i;
    if (!g) return NULL;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++)
        if (g->actors[i].active && g->actors[i].actor_id == actor_id)
            return &g->actors[i];
    return NULL;
}

static const struct toy_game_animation_info animation_table[TOY_GAME_ANIM_COUNT] = {
    { 0,   0 }, /* NONE */
    { 800, 1 }, /* IDLE */
    { 400, 1 }, /* MOVE */
    { 180, 0 }, /* FIRE */
    { 900, 0 }, /* RELOAD */
    { 0,   1 }, /* DOWNED */
    { 140, 0 }, /* HIT */
    { 700, 0 }, /* DEATH */
    { 700, 0 }, /* REVIVE */
    { TOY_CONFIG_SHOVE_ANIMATION_MS, 0 } /* SHOVE */
};

static const char *animation_names[TOY_GAME_ANIM_COUNT] = {
    "NONE", "IDLE", "MOVE", "FIRE", "RELOAD", "DOWNED", "HIT",
    "DEATH", "REVIVE", "SHOVE"
};

const struct toy_game_animation_info *toy_game_animation_info(int animation_id)
{
    if (animation_id < 0 || animation_id >= TOY_GAME_ANIM_COUNT)
        return &animation_table[TOY_GAME_ANIM_NONE];
    return &animation_table[animation_id];
}

const char *toy_game_animation_name(int animation_id)
{
    if (animation_id < 0 || animation_id >= TOY_GAME_ANIM_COUNT)
        return "UNKNOWN";
    return animation_names[animation_id];
}

/* Locomotion is the only animation family that the session layer may replace
 * every tick.  Keep this whitelist here so newly added action animations are
 * not accidentally cleared by movement-state bookkeeping in the host/client
 * session paths. */
int toy_game_animation_allows_locomotion(int animation_id)
{
    return animation_id == TOY_GAME_ANIM_NONE ||
           animation_id == TOY_GAME_ANIM_IDLE ||
           animation_id == TOY_GAME_ANIM_MOVE;
}

void toy_game_animation_set(struct toy_game_animation_state *state,
                            int animation_id)
{
    if (!state) return;
    if (animation_id < TOY_GAME_ANIM_NONE ||
        animation_id >= TOY_GAME_ANIM_COUNT)
        animation_id = TOY_GAME_ANIM_NONE;
    if (state->id != animation_id) {
        state->id = animation_id;
        state->time_ms = 0;
    }
}

void toy_game_animation_update(struct toy_game_animation_state *state,
                               int dt_ms)
{
    const struct toy_game_animation_info *info;
    if (!state || dt_ms <= 0) return;
    info = toy_game_animation_info(state->id);
    state->time_ms += dt_ms;
    /* Reload duration belongs to the equipped weapon, so the gameplay timer
     * owns its end instead of clamping this generic animation clock at 900ms. */
    if (state->id != TOY_GAME_ANIM_RELOAD && info->duration_ms > 0 &&
        state->time_ms >= info->duration_ms) {
        if (info->loop) state->time_ms %= info->duration_ms;
        else state->time_ms = info->duration_ms;
    } else if (state->time_ms > 60000) {
        state->time_ms %= 60000;
    }
}

void toy_game_actor_set_animation(struct toy_game_actor *actor, int animation_id)
{
    if (actor) toy_game_animation_set(&actor->animation, animation_id);
}

void toy_game_actor_update_animation(struct toy_game_actor *actor, int dt_ms)
{
    if (actor) toy_game_animation_update(&actor->animation, dt_ms);
}

static int segment_hits_box(int px, int pz, int qx, int qz,
                            const struct toy_game_box *b);

/* ── 事件队列 ──────────────────────────────────────────────────── */

static void push_event(struct toy_game *g, unsigned char event)
{
    if (g->event_count < TOY_GAME_MAX_EVENTS)
        g->events[g->event_count++] = event;
}

void toy_game_emit_event(struct toy_game *g, int event)
{
    if (g && event >= 0 && event <= 255)
        push_event(g, (unsigned char)event);
}

int toy_game_drain_events(struct toy_game *g, unsigned char *out, int max)
{
    int count = g->event_count;
    if (count > max) count = max;
    if (out && count > 0)
        memcpy(out, g->events, (unsigned long)count);
    g->event_count = 0;
    return count;
}

/* ── 初始化 / 世界 ─────────────────────────────────────────────── */

static int wave_combat_power(const struct toy_game *g)
{
    int i, power = 50; /* 玩家和基地自身的基础战斗力 */
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++)
        if (g->actors[i].active && g->actors[i].hired)
            power += toy_game_actor_combat_power(&g->actors[i]);
    return power;
}

static int wave_enemy_cost(int type)
{
    switch (type) {
    case TOY_GAME_ENEMY_TANK: return TOY_CONFIG_MONEY_TANK *
        TOY_CONFIG_WAVE_ENEMY_COST_MULTIPLIER;
    case TOY_GAME_ENEMY_SMOKER:
    case TOY_GAME_ENEMY_CHARGER: return TOY_CONFIG_MONEY_SPECIAL *
        TOY_CONFIG_WAVE_ENEMY_COST_MULTIPLIER;
    case TOY_GAME_ENEMY_PURSUIT_FAST: return TOY_CONFIG_MONEY_FAST *
        TOY_CONFIG_WAVE_ENEMY_COST_MULTIPLIER;
    case TOY_GAME_ENEMY_PURSUIT_HEAVY: return TOY_CONFIG_MONEY_HEAVY *
        TOY_CONFIG_WAVE_ENEMY_COST_MULTIPLIER;
    default: return TOY_CONFIG_MONEY_COMMON *
        TOY_CONFIG_WAVE_ENEMY_COST_MULTIPLIER;
    }
}

static void wave_build_plan(struct toy_game *g)
{
    int points = (g->wave * 50 + wave_combat_power(g)) *
                 TOY_GAME_WAVE_SCALE_PERCENT / 100;
    int remaining = points, i, count = 0, bucket;
    int common = 0, fast = 0, heavy = 0, special = 0, tank = 0;
    int tank_count = points > 1000 ? 2 : points >= 500 ? 1 : 0;
    g->wave_attack_points = points;
    for (i = 0; i < tank_count && remaining >= wave_enemy_cost(TOY_GAME_ENEMY_TANK); i++) {
        tank++;
        remaining -= wave_enemy_cost(TOY_GAME_ENEMY_TANK);
    }
    bucket = remaining * 50 / 100;
    while (bucket >= wave_enemy_cost(TOY_GAME_ENEMY_PURSUIT_COMMON)) {
        common++;
        bucket -= wave_enemy_cost(TOY_GAME_ENEMY_PURSUIT_COMMON);
    }
    if (points >= 200) {
        bucket = remaining * 20 / 100;
        while (bucket >= wave_enemy_cost(TOY_GAME_ENEMY_SMOKER)) {
            special++;
            bucket -= wave_enemy_cost(TOY_GAME_ENEMY_SMOKER);
        }
    }
    bucket = remaining * 15 / 100;
    while (bucket >= wave_enemy_cost(TOY_GAME_ENEMY_PURSUIT_FAST)) {
        fast++;
        bucket -= wave_enemy_cost(TOY_GAME_ENEMY_PURSUIT_FAST);
    }
    bucket = remaining * 15 / 100;
    while (bucket >= wave_enemy_cost(TOY_GAME_ENEMY_PURSUIT_HEAVY)) {
        heavy++;
        bucket -= wave_enemy_cost(TOY_GAME_ENEMY_PURSUIT_HEAVY);
    }
    /* The 64 enemy slots limit simultaneous actors only.  Do not scale this
     * plan down: excess enemies remain queued and spawn as slots are freed. */
    for (i = 0; i < tank && count < TOY_GAME_MAX_WAVE_QUEUE; i++)
        g->wave_spawn_types[count++] = TOY_GAME_ENEMY_TANK;
    for (i = 0; i < common && count < TOY_GAME_MAX_WAVE_QUEUE; i++)
        g->wave_spawn_types[count++] = TOY_GAME_ENEMY_PURSUIT_COMMON;
    for (i = 0; i < special && count < TOY_GAME_MAX_WAVE_QUEUE; i++)
        g->wave_spawn_types[count++] = rand_range(g, 0, 1) == 0 ?
            TOY_GAME_ENEMY_SMOKER : TOY_GAME_ENEMY_CHARGER;
    for (i = 0; i < fast && count < TOY_GAME_MAX_WAVE_QUEUE; i++)
        g->wave_spawn_types[count++] = TOY_GAME_ENEMY_PURSUIT_FAST;
    for (i = 0; i < heavy && count < TOY_GAME_MAX_WAVE_QUEUE; i++)
        g->wave_spawn_types[count++] = TOY_GAME_ENEMY_PURSUIT_HEAVY;
    for (i = count - 1; i > 0; i--) {
        int j = rand_range(g, 0, i), t = g->wave_spawn_types[i];
        g->wave_spawn_types[i] = g->wave_spawn_types[j];
        g->wave_spawn_types[j] = t;
    }
    g->to_spawn = count;
    g->wave_spawn_index = 0;
    g->wave_waiting_common = common;
    g->wave_waiting_fast = fast;
    g->wave_waiting_heavy = heavy;
    g->wave_waiting_special = special;
    g->wave_waiting_tank = tank;
    g->wave_spawn_interval_ms = count > 0 ?
        TOY_CONFIG_WAVE_SPAWN_DURATION_MS / count : 0;
}

void toy_game_init(struct toy_game *g, uint64_t seed)
{
    const struct toy_game_weapon_info *w;
    memset(g, 0, sizeof(struct toy_game));
    g->base_actor_index = -1;
    g->base_regen_timer_ms = TOY_CONFIG_BASE_REGEN_MS;
    g->rng = seed ? seed : 0x9E3779B97F4A7C15ULL;
    g->player_pull_enemy_index = -1;
    g->hp = TOY_GAME_PLAYER_HP;
    g->state = TOY_GAME_PLAYING;
    /* 槽 0 主武器为空；槽 1 默认为满弹匣手枪，开局出枪。 */
    g->slots[0].weapon = -1;
    g->slots[1].weapon = TOY_GAME_WEAPON_PISTOL;
    w = toy_game_weapon_info(TOY_GAME_WEAPON_PISTOL);
    g->slots[1].mag = w->mag_size;
    g->slots[1].reserve = w->reserve_max;
    g->current_slot = 1;
    g->money = TOY_GAME_INITIAL_MONEY;
    /* 手枪是基础装备；所有可购买主武器初始锁定。 */
    g->unlocked_weapons = 1u << TOY_GAME_WEAPON_PISTOL;
    g->wave = 0;
    g->to_spawn = 0;
    g->spawn_timer_ms = TOY_GAME_WAVE_FIRST_DELAY_MS;
    g->campaign_phase = TOY_GAME_PHASE_CALM;
    g->actor_id = 0;
    g->actor_kind = TOY_GAME_ACTOR_PLAYER;
    toy_game_set_player_name(g, "PLAYER");
    toy_game_set_ai_teammate(g, 1, -11000, -5800, "Jesus");
}

static void copy_name(char *dst, const char *src)
{
    int i;
    if (!src || !*src) src = "PLAYER";
    for (i = 0; i < TOY_GAME_MAX_NAME - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

void toy_game_set_player_name(struct toy_game *g, const char *name)
{
    copy_name(g->player_name, name);
}

void toy_game_set_ai_teammate_class(struct toy_game *g, int active, int class_id,
                                    int x, int z, const char *name)
{
    const struct toy_game_weapon_info *w;
    const struct toy_game_ai_info *info;
    int ai_weapon;
    if (class_id < 0 || class_id >= TOY_GAME_AI_CLASS_COUNT)
        class_id = TOY_GAME_AI_LEVEL_2;
    info = &ai_table[class_id];
    memset(g->ai_slots, 0, sizeof(g->ai_slots));
    g->ai_active = active != 0;
    g->ai_actor_id = 1;
    g->ai_x = x; g->ai_z = z;
    g->ai_sy = 0; g->ai_cy = 1024;
    g->ai_hp = info->max_hp;
    copy_name(g->ai_name, name ? name : "Jesus");
    ai_weapon = ai_random_weapon(g, class_id);
    w = toy_game_weapon_info(ai_weapon);
    g->ai_slots[w->slot].weapon = ai_weapon;
    g->ai_slots[w->slot].mag = w->mag_size;
    g->ai_slots[w->slot].reserve = TOY_GAME_AMMO_INFINITE;
    g->ai_slots[1 - w->slot].weapon = -1;
    g->ai_current_slot = w->slot;
    g->ai_reloading = 0;
    g->ai_reload_timer_ms = 0;
    g->ai_fire_cooldown_ms = 0;
    g->ai_muzzle_flash_ms = 0;
    g->ai_fire_seq = 0;
    g->ai_ray_count = 0;
    g->ai_context_actor_index = 0;
    memset(g->ai_rays, 0, sizeof(g->ai_rays));
    memset(&g->actors[0], 0, sizeof(g->actors[0]));
    g->actors[0].active = g->ai_active;
    g->actors[0].actor_id = g->ai_actor_id;
    g->actors[0].kind = TOY_GAME_ACTOR_AI;
    g->actors[0].class_id = class_id;
    g->actors[0].state = TOY_GAME_ACTOR_ALIVE;
    g->actors[0].x = g->ai_x; g->actors[0].z = g->ai_z;
    g->actors[0].deployment_x = g->ai_x;
    g->actors[0].deployment_z = g->ai_z;
    g->actors[0].sy = g->ai_sy; g->actors[0].cy = g->ai_cy;
    g->actors[0].hp = g->actors[0].max_hp = g->ai_hp;
    g->actors[0].fire_enabled = 1;
    g->actors[0].slots[0] = g->ai_slots[0];
    g->actors[0].slots[1] = g->ai_slots[1];
    g->actors[0].current_slot = g->ai_current_slot;
    g->actors[0].fire_seq = g->ai_fire_seq;
    g->actors[0].ray_count = g->ai_ray_count;
    memcpy(g->actors[0].name, g->ai_name, sizeof(g->actors[0].name));
}

void toy_game_set_ai_teammate(struct toy_game *g, int active, int x, int z,
                              const char *name)
{
    toy_game_set_ai_teammate_class(g, active, TOY_GAME_AI_LEVEL_2,
                                   x, z, name);
}

static void sync_ai_actor_from_legacy(struct toy_game *g)
{
    int index = g->ai_context_actor_index;
    struct toy_game_actor *a;
    if (index < 0 || index >= TOY_GAME_MAX_ACTORS) index = 0;
    a = &g->actors[index];
    a->active = g->ai_active;
    a->actor_id = g->ai_actor_id;
    a->x = g->ai_x; a->z = g->ai_z;
    a->sy = g->ai_sy; a->cy = g->ai_cy;
    a->hp = g->ai_hp;
    a->state = g->ai_down ? TOY_GAME_ACTOR_DOWNED : TOY_GAME_ACTOR_ALIVE;
    a->revive_progress_ms = g->ai_revive_progress_ms;
    memcpy(a->slots, g->ai_slots, sizeof(a->slots));
    a->current_slot = g->ai_current_slot;
    a->reloading = g->ai_reloading;
    a->reload_timer_ms = g->ai_reload_timer_ms;
    a->fire_cooldown_ms = g->ai_fire_cooldown_ms;
    a->muzzle_flash_ms = g->ai_muzzle_flash_ms;
    a->fire_seq = g->ai_fire_seq;
    a->ray_count = g->ai_ray_count;
    memcpy(a->rays, g->ai_rays, sizeof(a->rays));
    memcpy(a->name, g->ai_name, sizeof(a->name));
}

static void load_ai_actor_to_legacy(struct toy_game *g,
                                    const struct toy_game_actor *a)
{
    g->ai_active = a->active;
    g->ai_actor_id = a->actor_id;
    g->ai_x = a->x; g->ai_z = a->z;
    g->ai_sy = a->sy; g->ai_cy = a->cy;
    g->ai_hp = a->hp;
    g->ai_down = a->state == TOY_GAME_ACTOR_DOWNED;
    g->ai_revive_progress_ms = a->revive_progress_ms;
    memcpy(g->ai_slots, a->slots, sizeof(g->ai_slots));
    g->ai_current_slot = a->current_slot;
    g->ai_reloading = a->reloading;
    g->ai_reload_timer_ms = a->reload_timer_ms;
    g->ai_fire_cooldown_ms = a->fire_cooldown_ms;
    g->ai_muzzle_flash_ms = a->muzzle_flash_ms;
    g->ai_fire_seq = a->fire_seq;
    g->ai_ray_count = a->ray_count;
    memcpy(g->ai_rays, a->rays, sizeof(g->ai_rays));
    memcpy(g->ai_name, a->name, sizeof(g->ai_name));
}

int toy_game_add_ai(struct toy_game *g, int class_id, int x, int z,
                    const char *name)
{
    const struct toy_game_ai_info *info;
    struct toy_game_actor *a;
    int i, slot = -1;
    if (class_id < 0 || class_id >= TOY_GAME_AI_CLASS_COUNT) return -1;
    for (i = 0; i < TOY_GAME_REMOTE_ACTOR_BASE; i++)
        if (!g->actors[i].active) { slot = i; break; }
    if (slot < 0) return -1;
    info = &ai_table[class_id];
    a = &g->actors[slot];
    memset(a, 0, sizeof(*a));
    a->active = 1;
    a->actor_id = slot + 1;
    a->kind = TOY_GAME_ACTOR_AI;
    a->class_id = class_id;
    a->state = TOY_GAME_ACTOR_ALIVE;
    a->x = x; a->z = z; a->cy = 1024;
    a->deployment_x = x; a->deployment_z = z;
    a->flag_index = -1;
    a->hp = a->max_hp = info->max_hp;
    copy_name(a->name, name ? name : "AI");
    actor_set_weapon(a, ai_random_weapon(g, class_id));
    a->fire_enabled = 1;
    return a->actor_id;
}

int toy_game_assign_actor_deployment(struct toy_game *g, int actor_index,
                                     int x, int z, int flag_index)
{
    struct toy_game_actor *a;
    if (!g || actor_index < 0 || actor_index >= TOY_GAME_MAX_ACTORS)
        return 0;
    a = &g->actors[actor_index];
    if (!a->active || a->kind != TOY_GAME_ACTOR_AI) return 0;
    a->deployment_x = x;
    a->deployment_z = z;
    a->flag_index = flag_index;
    a->nav_active = 0;
    return 1;
}

int toy_game_add_hired_ai(struct toy_game *g, int weapon, int x, int z,
                          const char *name)
{
    struct toy_game_actor *a;
    int actor_id;
    if (!g || !toy_game_weapon_is_valid(weapon)) return -1;
    actor_id = toy_game_add_ai(g, TOY_GAME_AI_LEVEL_1, x, z, name);
    if (actor_id < 0) return -1;
    a = toy_game_actor_by_id(g, actor_id);
    if (!a) return -1;
    a->hired = 1;
    actor_set_weapon(a, weapon);
    return actor_id;
}

int toy_game_set_ai_weapon(struct toy_game *g, int actor_index, int weapon)
{
    struct toy_game_actor *a;
    if (!g || actor_index < 0 || actor_index >= TOY_GAME_MAX_ACTORS ||
        !toy_game_weapon_is_valid(weapon)) return 0;
    a = &g->actors[actor_index];
    if (!a->active || a->kind != TOY_GAME_ACTOR_AI || a->base_core) return 0;
    actor_set_weapon(a, weapon);
    if (actor_index == 0) {
        memcpy(g->ai_slots, a->slots, sizeof(g->ai_slots));
        g->ai_current_slot = a->current_slot;
    }
    return 1;
}

int toy_game_clear_hired_ai(struct toy_game *g)
{
    int i, count = 0;
    if (!g) return 0;
    for (i = 0; i < TOY_GAME_REMOTE_ACTOR_BASE; i++) {
        if (!g->actors[i].active || !g->actors[i].hired) continue;
        memset(&g->actors[i], 0, sizeof(g->actors[i]));
        count++;
    }
    if (g->ai_context_actor_index >= 0 &&
        g->ai_context_actor_index < TOY_GAME_MAX_ACTORS &&
        !g->actors[g->ai_context_actor_index].active)
        g->ai_context_actor_index = 0;
    return count;
}

int toy_game_upgrade_ai(struct toy_game *g, int actor_index)
{
    struct toy_game_actor *a;
    int price;
    if (!g || actor_index < 0 || actor_index >= TOY_GAME_MAX_ACTORS)
        return 0;
    a = &g->actors[actor_index];
    if (!a->active || a->kind != TOY_GAME_ACTOR_AI || !a->hired ||
        a->class_id >= TOY_GAME_AI_LEVEL_3) return 0;
    price = a->class_id == TOY_GAME_AI_LEVEL_1 ?
            TOY_CONFIG_AI_LEVEL_2_PRICE : TOY_CONFIG_AI_LEVEL_3_PRICE;
    if (g->money < price) return 0;
    g->money -= price;
    a->class_id++;
    a->max_hp = ai_table[a->class_id].max_hp;
    if (a->hp > 0) a->hp = a->max_hp;
    return 1;
}

int toy_game_set_remote_player(struct toy_game *g, int player_id,
                               int active, int x, int z, const char *name)
{
    const struct toy_game_weapon_info *w;
    struct toy_game_actor *a;
    int index;
    if (!g || player_id <= 0 || player_id >= TOY_GAME_MAX_PLAYERS)
        return -1;
    index = TOY_GAME_REMOTE_ACTOR_BASE + player_id - 1;
    if (!active) {
        memset(&g->actors[index], 0, sizeof(g->actors[index]));
        return index;
    }
    a = &g->actors[index];
    if (!a->active || a->kind != TOY_GAME_ACTOR_PLAYER) {
        memset(a, 0, sizeof(*a));
        a->actor_id = 100 + player_id;
        a->kind = TOY_GAME_ACTOR_PLAYER;
        a->class_id = TOY_GAME_AI_LEVEL_2;
        a->state = TOY_GAME_ACTOR_ALIVE;
        a->hp = a->max_hp = TOY_GAME_SECONDARY_PLAYER_HP;
        a->slots[0].weapon = -1;
        a->slots[1].weapon = TOY_GAME_WEAPON_PISTOL;
        w = toy_game_weapon_info(TOY_GAME_WEAPON_PISTOL);
        a->slots[1].mag = w->mag_size;
        a->slots[1].reserve = w->reserve_max;
        a->current_slot = 1;
    }
    a->active = 1;
    a->x = x; a->z = z;
    a->sy = 0; a->cy = 1024;
    copy_name(a->name, name ? name : "PLAYER");
    return index;
}

int toy_game_revive_ai(struct toy_game *g, int dt_ms)
{
    return toy_game_revive_actor(g, 0, dt_ms);
}

int toy_game_revive_actor(struct toy_game *g, int actor_index, int dt_ms)
{
    struct toy_game_actor *a;
    if (!g || actor_index < 0 || actor_index >= TOY_GAME_MAX_ACTORS ||
        dt_ms <= 0) return 0;
    a = &g->actors[actor_index];
    if (!a->active ||
        (a->kind != TOY_GAME_ACTOR_AI &&
         a->kind != TOY_GAME_ACTOR_PLAYER) ||
        a->state != TOY_GAME_ACTOR_DOWNED) return 0;
    if (a->base_core) return 0;
    a->revive_progress_ms += dt_ms;
    if (actor_index == 0) g->ai_revive_progress_ms = a->revive_progress_ms;
    if (a->revive_progress_ms < TOY_GAME_REVIVE_MS) return 0;
    a->revive_progress_ms = 0;
    a->hp = TOY_GAME_REVIVE_HP;
    a->state = TOY_GAME_ACTOR_ALIVE;
    toy_game_actor_set_animation(a, TOY_GAME_ANIM_REVIVE);
    if (actor_index == 0) {
        g->ai_hp = a->hp;
        g->ai_down = 0;
        g->ai_revive_progress_ms = 0;
    }
    push_event(g, TOY_GAME_EV_REVIVE);
    push_event(g, TOY_GAME_EV_ACTOR_REVIVE);
    return 1;
}

int toy_game_set_campaign_stage(struct toy_game *g, int stage)
{
    if (!g || stage < 0 || stage > 2 || stage < g->campaign_stage) return 0;
    g->campaign_stage = stage;
    push_event(g, TOY_GAME_EV_OBJECTIVE);
    return 1;
}

int toy_game_move_ai_actor(struct toy_game *g, int actor_index, int x, int z)
{
    struct toy_game_actor *a;
    if (!g || actor_index < 0 || actor_index >= TOY_GAME_MAX_ACTORS)
        return 0;
    a = &g->actors[actor_index];
    if (!a->active || (a->kind != TOY_GAME_ACTOR_AI &&
                       a->kind != TOY_GAME_ACTOR_PLAYER)) return 0;
    if (toy_game_position_blocked_at_height(g, x, z,
                                            TOY_GAME_PLAYER_RADIUS, 0))
        return 0;
    a->x = x; a->z = z;
    if (actor_index == 0) { g->ai_x = x; g->ai_z = z; }
    return 1;
}

void toy_game_set_world(struct toy_game *g,
                        const struct toy_game_box *boxes,
                        int box_count, int room_limit)
{
    g->world = boxes;
    g->world_count = box_count;
    g->room_limit = room_limit;
    toy_game_rebuild_navigation(g);
}

void toy_game_set_platforms(struct toy_game *g,
                            const struct toy_game_platform *platforms,
                            int platform_count)
{
    g->platforms = platforms;
    g->platform_count = platform_count;
    if (g->platform_count < 0) g->platform_count = 0;
    if (g->platform_count > TOY_GAME_MAX_PLATFORMS)
        g->platform_count = TOY_GAME_MAX_PLATFORMS;
}

int toy_game_ground_height(const struct toy_game *g, int x, int z,
                           int radius)
{
    int i, height = 0;
    if (!g || !g->platforms) return 0;
    for (i = 0; i < g->platform_count; i++) {
        const struct toy_game_platform *p = &g->platforms[i];
        if (x - radius < p->minx || x + radius > p->maxx ||
            z - radius < p->minz || z + radius > p->maxz) continue;
        if (p->height > height) height = p->height;
    }
    return height;
}

static int ground_height_overlapping(const struct toy_game *g,
                                     int x, int z, int radius)
{
    int i, height = 0;
    if (!g || !g->platforms) return 0;
    for (i = 0; i < g->platform_count; i++) {
        const struct toy_game_platform *p = &g->platforms[i];
        if (x + radius <= p->minx || x - radius >= p->maxx ||
            z + radius <= p->minz || z - radius >= p->maxz) continue;
        if (p->height > height) height = p->height;
    }
    return height;
}

int toy_game_position_blocked_at_height(const struct toy_game *g,
                                        int x, int z, int radius,
                                        int ground_height)
{
    int i;
    if (toy_game_position_blocked(g, x, z, radius)) return 1;
    for (i = 0; i < g->platform_count; i++) {
        const struct toy_game_platform *p = &g->platforms[i];
        if (p->height <= ground_height) continue;
        if (x + radius > p->minx && x - radius < p->maxx &&
            z + radius > p->minz && z - radius < p->maxz) return 1;
    }
    return 0;
}

void toy_game_update_player_ground(struct toy_game *g)
{
    int i, next_ground;
    if (!g || g->player_airborne_ms > 0) return;
    next_ground = toy_game_ground_height(g, g->px, g->pz,
                                         TOY_GAME_PLAYER_RADIUS);
    if (next_ground < g->player_ground_y) {
        /* Keep the current platform as support until the player's collision
         * circle has completely crossed its edge.  Without this hysteresis,
         * ground height drops while the circle still intersects the side and
         * the player becomes trapped inside the platform collision. */
        for (i = 0; i < g->platform_count; i++) {
            const struct toy_game_platform *p = &g->platforms[i];
            if (p->height != g->player_ground_y) continue;
            if (g->px + TOY_GAME_PLAYER_RADIUS > p->minx &&
                g->px - TOY_GAME_PLAYER_RADIUS < p->maxx &&
                g->pz + TOY_GAME_PLAYER_RADIUS > p->minz &&
                g->pz - TOY_GAME_PLAYER_RADIUS < p->maxz)
                return;
        }
        /* The circle is clear of the ledge: preserve the absolute height and
         * enter the ordinary airborne path instead of snapping to the floor. */
        g->player_airborne_y = g->player_ground_y - next_ground;
        g->player_ground_y = next_ground;
        g->player_airborne_ms = TOY_GAME_JUMP_MS;
        g->player_vertical_velocity = 0;
        g->player_air_x = 0;
        g->player_air_z = 0;
        return;
    }
    g->player_ground_y = next_ground;
}

int toy_game_point_in_box(int x, int z, const struct toy_game_box *box)
{
    return box && x >= box->minx && x <= box->maxx &&
           z >= box->minz && z <= box->maxz;
}

void toy_game_set_campaign(struct toy_game *g,
                           const struct toy_game_box *safe_rooms,
                           int safe_room_count,
                           const struct toy_game_box *spawn_zones,
                           int spawn_zone_count)
{
    g->safe_rooms = safe_rooms;
    g->safe_room_count = safe_room_count;
    g->safe_start_index = safe_room_count > 0 ? 0 : -1;
    g->safe_goal_index = safe_room_count > 0 ? safe_room_count - 1 : -1;
    g->spawn_zones = spawn_zones;
    g->spawn_zone_count = spawn_zone_count;
    g->campaign_mode = safe_room_count >= 2 && spawn_zone_count > 0;
    g->goal_hold_ms = 0;
    g->campaign_stage = 0;
    g->alarm_spawn_zone = -1;
    toy_game_rebuild_navigation(g);
    if (g->campaign_mode) {
        g->to_spawn = 0;
        g->campaign_phase = TOY_GAME_PHASE_BUILDUP;
        g->spawn_budget = TOY_GAME_CAMPAIGN_AMBIENT_BUDGET;
        g->phase_timer_ms = 0;
        g->active_attackers = 0;
        g->director_encounters = 0;
        g->spawn_timer_ms = 0;
    }
}

void toy_game_set_campaign_safe_indices(struct toy_game *g,
                                        int start_index, int goal_index)
{
    if (!g) return;
    if (start_index >= 0 && start_index < g->safe_room_count)
        g->safe_start_index = start_index;
    if (goal_index >= 0 && goal_index < g->safe_room_count)
        g->safe_goal_index = goal_index;
}

void toy_game_set_alarm(struct toy_game *g,
                        const struct toy_game_box *alarm_zone,
                        int spawn_zone_index)
{
    g->alarm_zone = alarm_zone;
    if (spawn_zone_index >= 0 && spawn_zone_index < g->spawn_zone_count)
        g->alarm_spawn_zone = spawn_zone_index;
    else
        g->alarm_spawn_zone = -1;
    g->alarm_triggered = 0;
    g->alarm_timer_ms = 0;
}

/* 圆形碰撞体 (x, z, radius) 是否与房间边界或障碍物重叠 */
int toy_game_position_blocked(const struct toy_game *g,
                              int x, int z, int radius)
{
    int i;
    if (x - radius < -g->room_limit || x + radius > g->room_limit ||
        z - radius < -g->room_limit || z + radius > g->room_limit) return 1;
    for (i = 0; i < g->world_count; i++) {
        const struct toy_game_box *b = &g->world[i];
        if (x + radius > b->minx && x - radius < b->maxx &&
            z + radius > b->minz && z - radius < b->maxz) return 1;
    }
    return 0;
}

static int nav_position_blocked(const struct toy_game *g, int x, int z)
{
    int i;
    /* A cell stands for the whole square, not just its center.  Expanding by
     * half a cell prevents a thin wall that falls between two sample points
     * from becoming an artificial bridge in the component map. */
    int radius = TOY_GAME_CHARGER_RADIUS + TOY_GAME_NAV_CELL_SIZE / 2;
    if (toy_game_position_blocked(g, x, z, radius)) return 1;
    for (i = 0; i < g->safe_room_count; i++) {
        const struct toy_game_box *b = &g->safe_rooms[i];
        if (x + radius > b->minx && x - radius < b->maxx &&
            z + radius > b->minz && z - radius < b->maxz) return 1;
    }
    return 0;
}

static int nav_cell_index(const struct toy_game *g, int x, int z)
{
    int cx = (x - g->nav_origin) / TOY_GAME_NAV_CELL_SIZE;
    int cz = (z - g->nav_origin) / TOY_GAME_NAV_CELL_SIZE;
    if (cx < 0 || cz < 0 || cx >= g->nav_width || cz >= g->nav_height)
        return -1;
    return cz * g->nav_width + cx;
}

/* The cell mask is intentionally conservative (it includes half a cell),
 * so an enemy standing close to a wall can be inside a blocked sample cell
 * even though its actual collision circle is valid.  Resolve such positions
 * to the nearest walkable sample instead of treating the actor as outside
 * the navigation graph. */
static int nav_component_at_position(const struct toy_game *g, int x, int z)
{
    int base = nav_cell_index(g, x, z);
    int base_x, base_z, dx, dz, cx, cz, index, best = 0;
    long long best_dist = 0, dist;
    if (base < 0) return 0;
    base_x = base % g->nav_width;
    base_z = base / g->nav_width;
    for (dz = -2; dz <= 2; dz++) {
        for (dx = -2; dx <= 2; dx++) {
            cx = base_x + dx; cz = base_z + dz;
            if (cx < 0 || cz < 0 || cx >= g->nav_width ||
                cz >= g->nav_height) continue;
            index = cz * g->nav_width + cx;
            if (!g->nav_walkable[index] || !g->nav_component[index]) continue;
            dist = (long long)(g->nav_origin +
                               cx * TOY_GAME_NAV_CELL_SIZE +
                               TOY_GAME_NAV_CELL_SIZE / 2 - x);
            dist *= dist;
            dist += (long long)(g->nav_origin +
                                cz * TOY_GAME_NAV_CELL_SIZE +
                                TOY_GAME_NAV_CELL_SIZE / 2 - z) *
                    (g->nav_origin + cz * TOY_GAME_NAV_CELL_SIZE +
                     TOY_GAME_NAV_CELL_SIZE / 2 - z);
            if (!best || dist < best_dist) {
                best = index;
                best_dist = dist;
            }
        }
    }
    return best ? g->nav_component[best] : 0;
}

static int nav_step_allowed(const struct toy_game *g, int cx, int cz,
                            int nx, int nz)
{
    int dx = nx - cx, dz = nz - cz;
    int index;
    if (nx < 0 || nz < 0 || nx >= g->nav_width || nz >= g->nav_height)
        return 0;
    index = nz * g->nav_width + nx;
    if (!g->nav_walkable[index]) return 0;
    if (dx != 0 && dz != 0) {
        if (!g->nav_walkable[cz * g->nav_width + nx] ||
            !g->nav_walkable[nz * g->nav_width + cx]) return 0;
    }
    return 1;
}

void toy_game_rebuild_navigation(struct toy_game *g)
{
    int i, x, z, index, component = 0;
    int queue[TOY_GAME_NAV_MAX_CELLS];
    int head, tail, cx, cz, nx, nz, dx, dz;
    if (!g || g->room_limit <= 0) return;
    g->nav_origin = -g->room_limit;
    g->nav_width = (g->room_limit * 2 + TOY_GAME_NAV_CELL_SIZE - 1) /
                   TOY_GAME_NAV_CELL_SIZE;
    g->nav_height = g->nav_width;
    if (g->nav_width > TOY_GAME_NAV_MAX_SIDE) {
        g->nav_width = TOY_GAME_NAV_MAX_SIDE;
        g->nav_height = TOY_GAME_NAV_MAX_SIDE;
    }
    for (z = 0; z < g->nav_height; z++) {
        for (x = 0; x < g->nav_width; x++) {
            int px = g->nav_origin + x * TOY_GAME_NAV_CELL_SIZE +
                     TOY_GAME_NAV_CELL_SIZE / 2;
            int pz = g->nav_origin + z * TOY_GAME_NAV_CELL_SIZE +
                     TOY_GAME_NAV_CELL_SIZE / 2;
            index = z * g->nav_width + x;
            g->nav_walkable[index] = !nav_position_blocked(g, px, pz);
            g->nav_component[index] = 0;
        }
    }
    for (i = 0; i < g->nav_width * g->nav_height; i++) {
        if (!g->nav_walkable[i] || g->nav_component[i]) continue;
        component++;
        if (component > 65535) component = 65535;
        head = 0; tail = 0; queue[tail++] = i;
        g->nav_component[i] = (unsigned short)component;
        while (head < tail) {
            index = queue[head++];
            cx = index % g->nav_width;
            cz = index / g->nav_width;
            for (dz = -1; dz <= 1; dz++) {
                for (dx = -1; dx <= 1; dx++) {
                    if (!dx && !dz) continue;
                    nx = cx + dx; nz = cz + dz;
                    if (!nav_step_allowed(g, cx, cz, nx, nz)) continue;
                    index = nz * g->nav_width + nx;
                    if (g->nav_component[index]) continue;
                    g->nav_component[index] = (unsigned short)component;
                    if (tail < TOY_GAME_NAV_MAX_CELLS) queue[tail++] = index;
                }
            }
        }
    }
}

static int enemy_position_blocked(const struct toy_game *g,
                                  int x, int z, int radius)
{
    int i;
    if (toy_game_position_blocked_at_height(g, x, z, radius, 0)) return 1;
    for (i = 0; i < g->safe_room_count; i++) {
        const struct toy_game_box *b = &g->safe_rooms[i];
        if (x + radius > b->minx && x - radius < b->maxx &&
            z + radius > b->minz && z - radius < b->maxz) return 1;
    }
    return 0;
}

static int enemy_radius(const struct toy_game_enemy *e)
{
    int ability = toy_game_enemy_info(e->type)->ability;
    return ability == TOY_GAME_ENEMY_ABILITY_TANK_SWEEP ? TOY_GAME_TANK_RADIUS :
           ability == TOY_GAME_ENEMY_ABILITY_CHARGER_RUSH ?
           TOY_GAME_CHARGER_RADIUS : TOY_GAME_ENEMY_RADIUS;
}

static int enemy_separation_distance(const struct toy_game_enemy *a,
                                     const struct toy_game_enemy *b)
{
    return enemy_radius(a) + enemy_radius(b) + 20;
}

static int player_in_safe_room(const struct toy_game *g)
{
    int i;
    for (i = 0; i < g->safe_room_count; i++)
        if (toy_game_point_in_box(g->px, g->pz, &g->safe_rooms[i])) return 1;
    return 0;
}

/* ── 敌人生成 ──────────────────────────────────────────────────── */

static int find_free_slot(struct toy_game *g)
{
    int i;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++)
        if (g->enemies[i].active == 0) return i;
    return -1;
}

static const int enemy_dir_x[8] = {1024, 724, 0, -724, -1024, -724, 0, 724};
static const int enemy_dir_z[8] = {0, 724, 1024, 724, 0, -724, -1024, -724};

static void init_enemy_ai(struct toy_game *g, struct toy_game_enemy *e)
{
    int direction = rand_range(g, 0, 7);
    e->ai_state = TOY_GAME_ENEMY_IDLE;
    e->ai_timer_ms = 0;
    e->target_x = e->x;
    e->target_z = e->z;
    e->last_seen_x = e->x;
    e->last_seen_z = e->z;
    e->lost_sight_ms = 0;
    e->target_kind = -1;
    e->retarget_timer_ms = TOY_GAME_RETARGET_MS;
    e->wander_timer_ms = rand_range(g, 600, 1800);
    e->dir_x = enemy_dir_x[direction];
    e->dir_z = enemy_dir_z[direction];
}

static void init_enemy_stats(struct toy_game *g, struct toy_game_enemy *e,
                             int type)
{
    const struct toy_game_enemy_info *info = toy_game_enemy_info(type);
    e->type = type >= 0 && type < TOY_GAME_ENEMY_TYPE_COUNT ? type : 0;
    e->speed = rand_range(g, info->speed_min, info->speed_max);
    e->hp = info->max_hp;
    e->ability.special_timer_ms = 0;
    e->ability.special_windup_ms = 0;
    e->ability.special_target_active = 0;
    e->ability.charge_active = 0;
    e->ability.charge_dir_x = 0;
    e->ability.charge_dir_z = 0;
    e->ability.charge_elapsed_ms = 0;
    e->ability.charge_hit_base = 0;
    e->ability.special_target_kind = -1;
    e->ability.special_target_index = -1;
    e->ability.special_pull_timer_ms = 0;
    e->shove_stun_ms = 0;
    e->airborne_ms = 0;
    e->vertical_velocity = 0;
    e->airborne_y = 0;
    e->knockback_x = 0;
    e->knockback_z = 0;
}

/* 在矩形区域内随机生成一个追踪型敌人；距玩家过近、压障碍或槽满
 * 返回 0。生成方向直接朝向玩家（追踪态首个逻辑步就会转脸）。 */
static int spawn_tracking_enemy(struct toy_game *g, int enemy_type,
                                int minx, int maxx, int minz, int maxz,
                                int min_dist2)
{
    int i, slot;
    for (i = 0; i < 24; i++) {
        int x, z, dx, dz;
        long long dist2, dist;
        x = rand_range(g, minx, maxx);
        z = rand_range(g, minz, maxz);
        dx = x - g->px;
        dz = z - g->pz;
        dist2 = (long long)dx * dx + (long long)dz * dz;
        if (dist2 < (long long)min_dist2) continue;
        if (enemy_position_blocked(g, x, z, TOY_GAME_ENEMY_RADIUS)) continue;
        slot = find_free_slot(g);
        if (slot < 0) return 0;
        g->enemies[slot].active = 1;
        g->enemies[slot].x = x;
        g->enemies[slot].z = z;
        init_enemy_stats(g, &g->enemies[slot], enemy_type);
        g->enemies[slot].bite_cooldown_ms = 0;
        g->enemies[slot].flash = 0;
        g->enemies[slot].hurt = 0;
        g->enemies[slot].dying_ms = 0;
        init_enemy_ai(g, &g->enemies[slot]);
        g->enemies[slot].ai_state = TOY_GAME_ENEMY_TRACKING;
        g->enemies[slot].target_x = g->px;
        g->enemies[slot].target_z = g->pz;
        g->enemies[slot].last_seen_x = g->px;
        g->enemies[slot].last_seen_z = g->pz;
        dist = isqrt(dist2);
        if (dist > 0) {
            g->enemies[slot].dir_x = (int)((long long)dx * 1024 / dist);
            g->enemies[slot].dir_z = (int)((long long)dz * 1024 / dist);
        }
        g->enemies_alive++;
        return 1;
    }
    return 0;
}

/* 召唤尸潮：从 points 中随机选 1-3 个互异刷怪点（最多 point_count
 * 个），把 [count_min, count_max] 只敌人均摊到各点矩形内，逐只找合法
 * 位置；槽位满则停止。返回实际生成数。 */
int toy_game_spawn_horde_type(struct toy_game *g, int enemy_type,
                              int count_min, int count_max,
                              const struct toy_game_box *points, int point_count,
                              int min_player_dist)
{
    int count, spawned = 0, i, j, n_points, per, extra;
    int min_dist2 = min_player_dist * min_player_dist;
    int order[8];
    if (g->state != TOY_GAME_PLAYING ||
        enemy_type < 0 || enemy_type >= TOY_GAME_ENEMY_TYPE_COUNT) return 0;
    if (!points || point_count <= 0) return 0;
    if (point_count > 8) point_count = 8;
    count = rand_range(g, count_min, count_max);
    if (count > TOY_GAME_MAX_ENEMIES) count = TOY_GAME_MAX_ENEMIES;
    n_points = 1 + rand_range(g, 0, 2);
    if (n_points > point_count) n_points = point_count;
    for (i = 0; i < point_count; i++) order[i] = i;
    for (i = 0; i < n_points; i++) {        /* 部分洗牌：选出互异刷怪点 */
        int k = rand_range(g, i, point_count - 1);
        int t = order[i];
        order[i] = order[k];
        order[k] = t;
    }
    per = count / n_points;
    extra = count % n_points;
    for (i = 0; i < n_points; i++) {
        const struct toy_game_box *p = &points[order[i]];
        int want = per + (i < extra ? 1 : 0);
        for (j = 0; j < want; j++) {
            if (spawn_tracking_enemy(g, enemy_type, p->minx, p->maxx,
                                     p->minz, p->maxz, min_dist2))
                spawned++;
            else if (find_free_slot(g) < 0)
                return spawned;             /* 槽满：本次召唤到此为止 */
        }
    }
    if (spawned > 0) push_event(g, TOY_GAME_EV_SPAWN);
    return spawned;
}

int toy_game_spawn_horde(struct toy_game *g, int count_min, int count_max,
                         const struct toy_game_box *points, int point_count,
                         int min_player_dist)
{
    return toy_game_spawn_horde_type(g, TOY_GAME_ENEMY_PURSUIT_COMMON, count_min,
                                     count_max, points, point_count,
                                     min_player_dist);
}

/* 从房间边界带随机选一个合法生成点；找不到返回 0 */
static int try_spawn(struct toy_game *g)
{
    int edge = g->room_limit - TOY_GAME_SPAWN_EDGE;
    int i, slot;
    int min_dist2 = TOY_GAME_MIN_SPAWN_DIST * TOY_GAME_MIN_SPAWN_DIST;
    for (i = 0; i < 16; i++) {
        int side = rand_range(g, 0, 3);
        int x, z, dx, dz;
        if (g->campaign_mode) {
            const struct toy_game_box *zone;
            int zone_index = rand_range(g, 0, g->spawn_zone_count - 1);
            if (zone_index == g->alarm_spawn_zone && g->alarm_timer_ms <= 0)
                continue;
            zone = &g->spawn_zones[zone_index];
            x = rand_range(g, zone->minx, zone->maxx);
            z = rand_range(g, zone->minz, zone->maxz);
        } else if (side < 2) {
            x = rand_range(g, -edge, edge);
            z = (side == 0) ? -edge : edge;
        } else {
            z = rand_range(g, -edge, edge);
            x = (side == 2) ? -edge : edge;
        }
        if (enemy_position_blocked(g, x, z, TOY_GAME_ENEMY_RADIUS)) continue;
        dx = x - g->px;
        dz = z - g->pz;
        if (dx * dx + dz * dz < min_dist2) continue;
        slot = find_free_slot(g);
        if (slot < 0) return 0;         /* 槽位满，等待死亡腾位 */
        g->enemies[slot].active = 1;
        g->enemies[slot].x = x;
        g->enemies[slot].z = z;
        {
            /* Keep the old PRNG sequence stable; type selection is derived
             * from the slot instead of consuming another random value. */
            int type;
            if (g->campaign_mode && g->director_encounters >= 2 &&
                slot % 8 == 0)
                type = TOY_GAME_ENEMY_CHARGER;
            else if (g->campaign_mode && g->director_encounters >= 1 &&
                     slot % 6 == 0)
                type = TOY_GAME_ENEMY_SMOKER;
            else
                type = slot % 10 == 0 ? TOY_GAME_ENEMY_HEAVY :
                       slot % 5 == 0 ? TOY_GAME_ENEMY_PURSUIT_COMMON :
                       TOY_GAME_ENEMY_COMMON;
            init_enemy_stats(g, &g->enemies[slot], type);
        }
        g->enemies[slot].bite_cooldown_ms = 0;
        g->enemies[slot].flash = 0;
        g->enemies[slot].hurt = 0;
        g->enemies[slot].dying_ms = 0;
        init_enemy_ai(g, &g->enemies[slot]);
        if (g->campaign_mode &&
            g->campaign_phase == TOY_GAME_PHASE_HORDE) {
            g->enemies[slot].ai_state = TOY_GAME_ENEMY_CHASE;
            g->enemies[slot].target_x = g->px;
            g->enemies[slot].target_z = g->pz;
            g->enemies[slot].last_seen_x = g->px;
            g->enemies[slot].last_seen_z = g->pz;
        }
        g->enemies_alive++;
        return 1;
    }
    return 0;
}

void toy_game_place_enemy(struct toy_game *g, int x, int z)
{
    int slot = find_free_slot(g);
    if (slot < 0 || enemy_position_blocked(g, x, z,
                                           TOY_GAME_ENEMY_RADIUS)) return;
    g->enemies[slot].active = 1;
    g->enemies[slot].x = x;
    g->enemies[slot].z = z;
    init_enemy_stats(g, &g->enemies[slot], TOY_GAME_ENEMY_COMMON);
    g->enemies[slot].bite_cooldown_ms = 0;
    g->enemies[slot].flash = 0;
    g->enemies[slot].hurt = 0;
    g->enemies[slot].dying_ms = 0;
    g->enemies[slot].shove_stun_ms = 0;
    init_enemy_ai(g, &g->enemies[slot]);
    g->enemies_alive++;
}

/* 推开面前敌人（L4D 式近战）：检测面朝方向（sy,cy，1024 定点）前
 * 120° 扇形、半径 TOY_CONFIG_SHOVE_RANGE 内所有存活敌人，沿面朝方向
 * 击退 TOY_GAME_SHOVE_PUSH 单位（撞到障碍依次退让 3/4、1/2、1/4），
 * 并让其僵直 TOY_CONFIG_SHOVE_STUN_MS 不移动不攻击。返回推开的数量。 */
static int toy_game_shove_at(struct toy_game *g, int origin_x, int origin_z,
                             int sy, int cy)
{
    int i, pushed = 0;
    long long range2 = (long long)TOY_CONFIG_SHOVE_RANGE * TOY_CONFIG_SHOVE_RANGE;
    if (g->state != TOY_GAME_PLAYING) return 0;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        struct toy_game_enemy *e = &g->enemies[i];
        long long dx, dz, dist2, dist, dot;
        int push[4], s, nx, nz;
        if (e->active != 1) continue;
        if (toy_game_enemy_info(e->type)->ability ==
                TOY_GAME_ENEMY_ABILITY_TANK_SWEEP)
            continue; /* Boss mass: player/AI shove cannot move or stun Tank. */
        dx = e->x - origin_x;
        dz = e->z - origin_z;
        dist2 = dx * dx + dz * dz;
        if (dist2 > range2 || dist2 == 0) continue;
        dist = isqrt(dist2);
        if (dist <= 0) continue;
        dot = dx * sy + dz * cy;
        if (dot * TOY_GAME_SHOVE_CONE < dist * 1024) continue;
        push[0] = TOY_GAME_SHOVE_PUSH;
        push[1] = TOY_GAME_SHOVE_PUSH * 3 / 4;
        push[2] = TOY_GAME_SHOVE_PUSH / 2;
        push[3] = TOY_GAME_SHOVE_PUSH / 4;
        for (s = 0; s < 4; s++) {
            nx = e->x + (int)((long long)sy * push[s] / 1024);
            nz = e->z + (int)((long long)cy * push[s] / 1024);
            if (!enemy_position_blocked(g, nx, nz, TOY_GAME_ENEMY_RADIUS)) {
                e->x = nx;
                e->z = nz;
                break;
            }
        }
        e->shove_stun_ms = e->type == TOY_GAME_ENEMY_CHARGER ?
                           TOY_CONFIG_CHARGER_SHOVE_STUN_MS :
                           TOY_CONFIG_SHOVE_STUN_MS;
        e->flash = 200;          /* 被推开瞬间闪白 */
        pushed++;
    }
    return pushed;
}

static int ai_try_shove(struct toy_game *g, struct toy_game_actor *actor)
{
    const struct toy_game_ai_info *info;
    int pushed;
    if (!g || !actor || actor->kind != TOY_GAME_ACTOR_AI) return 0;
    if (actor->class_id < 0 || actor->class_id >= TOY_GAME_AI_CLASS_COUNT)
        return 0;
    if (actor->ai_shove_cooldown_ms > 0) return 0;
    info = &ai_table[actor->class_id];
    pushed = toy_game_shove_at(g, actor->x, actor->z, actor->sy, actor->cy);
    actor->ai_shove_cooldown_ms = info->shove_cooldown_ms;
    return pushed;
}

int toy_game_shove(struct toy_game *g, int sy, int cy)
{
    int pushed;
    if (!g || g->state != TOY_GAME_PLAYING || g->player_down) return 0;
    g->animation.id = TOY_GAME_ANIM_SHOVE;
    g->animation.time_ms = 0;
    push_event(g, TOY_GAME_EV_SHOVE);
    pushed = toy_game_shove_at(g, g->px, g->pz, sy, cy);
    if (pushed > 0) push_event(g, TOY_GAME_EV_SHOVE_HIT);
    return pushed;
}

/* ── 波次状态机 ────────────────────────────────────────────────── */

static void update_waves(struct toy_game *g, int dt_ms)
{
    if (g->campaign_phase == TOY_GAME_PHASE_CALM) {
        g->spawn_timer_ms -= dt_ms;
        if (g->spawn_timer_ms <= 0) {
            if (g->wave >= TOY_GAME_WAVE_MAX) return;
            g->wave++;
            wave_build_plan(g);
            g->campaign_phase = TOY_GAME_PHASE_BUILDUP;
            g->phase_timer_ms = TOY_GAME_WAVE_ANNOUNCE_MS;
            push_event(g, TOY_GAME_EV_WAVE_START);
        }
    } else if (g->campaign_phase == TOY_GAME_PHASE_BUILDUP) {
        g->phase_timer_ms -= dt_ms;
        if (g->phase_timer_ms <= 0) {
            g->campaign_phase = TOY_GAME_PHASE_HORDE;
            g->spawn_timer_ms = 0;
        }
    } else if (g->campaign_phase == TOY_GAME_PHASE_HORDE && g->to_spawn > 0) {
        g->spawn_timer_ms -= dt_ms;
        if (g->spawn_timer_ms <= 0) {
            int type = g->wave_spawn_types[g->wave_spawn_index];
            int zone_index = g->spawn_zone_count > 0 ?
                rand_range(g, 0, g->spawn_zone_count - 1) : -1;
            const struct toy_game_box *zone = zone_index >= 0 ?
                &g->spawn_zones[zone_index] : NULL;
            if (zone && spawn_tracking_enemy(g, type, zone->minx, zone->maxx,
                                              zone->minz, zone->maxz,
                                              TOY_GAME_MIN_SPAWN_DIST *
                                              TOY_GAME_MIN_SPAWN_DIST)) {
                g->wave_spawn_index++;
                g->to_spawn--;
                if (type == TOY_GAME_ENEMY_PURSUIT_COMMON) g->wave_waiting_common--;
                else if (type == TOY_GAME_ENEMY_PURSUIT_FAST) g->wave_waiting_fast--;
                else if (type == TOY_GAME_ENEMY_PURSUIT_HEAVY) g->wave_waiting_heavy--;
                else if (type == TOY_GAME_ENEMY_TANK) g->wave_waiting_tank--;
                else g->wave_waiting_special--;
            }
            g->spawn_timer_ms = g->wave_spawn_interval_ms > 0 ?
                g->wave_spawn_interval_ms : 1;
        }
    } else if (g->campaign_phase == TOY_GAME_PHASE_HORDE && g->enemies_alive == 0) {
        g->money += g->wave * 100;
        if (g->wave >= TOY_GAME_WAVE_MAX) {
            g->state = TOY_GAME_WON;
            push_event(g, TOY_GAME_EV_LEVEL_WON);
        } else {
            g->campaign_phase = TOY_GAME_PHASE_CALM;
            g->spawn_timer_ms = TOY_GAME_WAVE_PAUSE_MS;
        }
    }
}

int toy_game_skip_wave_rest(struct toy_game *g)
{
    if (!g || g->state != TOY_GAME_PLAYING ||
        g->campaign_mode || g->campaign_phase != TOY_GAME_PHASE_CALM ||
        g->spawn_timer_ms <= 0) return 0;
    g->spawn_timer_ms = 0;
    return 1;
}

static void update_campaign_goal(struct toy_game *g, int dt_ms)
{
    const struct toy_game_box *goal;
    if (g->campaign_stage < 2) {
        g->goal_hold_ms = 0;
        return;
    }
    if (g->safe_goal_index < 0 || g->safe_goal_index >= g->safe_room_count)
        return;
    goal = &g->safe_rooms[g->safe_goal_index];
    if (toy_game_point_in_box(g->px, g->pz, goal)) {
        g->goal_hold_ms += dt_ms;
        if (g->goal_hold_ms >= TOY_GAME_GOAL_HOLD_MS) {
            g->goal_hold_ms = TOY_GAME_GOAL_HOLD_MS;
            g->state = TOY_GAME_WON;
            push_event(g, TOY_GAME_EV_LEVEL_WON);
        }
    } else {
        g->goal_hold_ms = 0;
    }
}

static void campaign_enter_relax(struct toy_game *g)
{
    g->campaign_phase = TOY_GAME_PHASE_RELAX;
    g->spawn_budget = 0;
    g->phase_timer_ms = TOY_GAME_CAMPAIGN_RELAX_MS;
    g->alarm_timer_ms = 0;
}

static void campaign_enter_calm(struct toy_game *g)
{
    g->campaign_phase = TOY_GAME_PHASE_CALM;
    g->spawn_budget = 0;
    g->phase_timer_ms = rand_range(g, TOY_GAME_DIRECTOR_MIN_DELAY_MS,
                                   TOY_GAME_DIRECTOR_MAX_DELAY_MS);
}

static void update_alarm_event(struct toy_game *g, int dt_ms)
{
    if (!g->alarm_triggered && g->alarm_zone &&
        toy_game_point_in_box(g->px, g->pz, g->alarm_zone)) {
        g->alarm_triggered = 1;
        g->campaign_phase = TOY_GAME_PHASE_HORDE;
        g->spawn_budget = TOY_GAME_ALARM_SPAWN_BUDGET;
        g->phase_timer_ms = TOY_GAME_ALARM_DURATION_MS;
        g->alarm_timer_ms = TOY_GAME_ALARM_DURATION_MS;
        g->spawn_timer_ms = 0;
        push_event(g, TOY_GAME_EV_ALARM_TRIGGERED);
    }
    if (g->campaign_phase == TOY_GAME_PHASE_HORDE) {
        g->phase_timer_ms -= dt_ms;
        g->alarm_timer_ms = g->phase_timer_ms;
        if (g->phase_timer_ms <= 0) campaign_enter_relax(g);
    } else if (g->campaign_phase == TOY_GAME_PHASE_RELAX) {
        g->phase_timer_ms -= dt_ms;
        if (g->phase_timer_ms <= 0) {
            campaign_enter_calm(g);
        }
    }
}

static void update_director(struct toy_game *g, int dt_ms)
{
    if (g->campaign_phase != TOY_GAME_PHASE_CALM) return;
    /* The director cooldown is a real clock.  Previously it was decremented
     * only when spawning was allowed, so a safe room or a busy battlefield
     * made the HUD countdown appear frozen forever. */
    if (g->phase_timer_ms > 0) {
        g->phase_timer_ms -= dt_ms;
        if (g->phase_timer_ms > 0) return;
    }
    /* A ready director still waits for a suitable moment before opening the
     * next encounter; phase_timer_ms remains zero and the HUD says READY. */
    if (player_in_safe_room(g) ||
        g->enemies_alive > TOY_GAME_DIRECTOR_ALIVE_LOW ||
        g->active_attackers > TOY_GAME_DIRECTOR_ATTACKER_LOW) return;
    g->campaign_phase = TOY_GAME_PHASE_BUILDUP;
    g->spawn_budget = rand_range(g, TOY_GAME_DIRECTOR_MIN_GROUP,
                                 TOY_GAME_DIRECTOR_MAX_GROUP);
    g->spawn_timer_ms = 0;
    g->phase_timer_ms = 0;
    g->director_encounters++;
}

static void update_campaign_spawning(struct toy_game *g, int dt_ms)
{
    int interval, batch, spawned;
    if (g->campaign_phase != TOY_GAME_PHASE_BUILDUP &&
        g->campaign_phase != TOY_GAME_PHASE_HORDE) return;
    if (g->spawn_budget <= 0) {
        if (g->campaign_phase == TOY_GAME_PHASE_HORDE)
            campaign_enter_relax(g);
        else
            campaign_enter_calm(g);
        return;
    }
    if (g->campaign_phase == TOY_GAME_PHASE_HORDE &&
        g->enemies_alive >= TOY_GAME_CAMPAIGN_ACTIVE_LIMIT) return;
    g->spawn_timer_ms -= dt_ms;
    if (g->spawn_timer_ms <= 0) {
        /* Spawn in small bursts.  WAIT therefore describes a real queue of
         * enemies still waiting for the next burst, not an opaque budget. */
        batch = 2;
        spawned = 0;
        while (spawned < batch && g->spawn_budget > 0) {
            if (g->campaign_phase == TOY_GAME_PHASE_HORDE &&
                g->enemies_alive >= TOY_GAME_CAMPAIGN_ACTIVE_LIMIT) break;
            if (!try_spawn(g)) break;
            g->spawn_budget--;
            spawned++;
        }
        interval = g->campaign_phase == TOY_GAME_PHASE_HORDE ?
                   TOY_GAME_ALARM_SPAWN_INTERVAL_MS :
                   TOY_GAME_CAMPAIGN_AMBIENT_SPAWN_INTERVAL_MS;
        g->spawn_timer_ms = interval * batch;
        if (g->spawn_budget <= 0) {
            if (g->campaign_phase == TOY_GAME_PHASE_HORDE)
                campaign_enter_relax(g);
            else
                campaign_enter_calm(g);
        }
    }
}

static void update_campaign(struct toy_game *g, int dt_ms)
{
    int i, phase_before;
    g->active_attackers = 0;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        const struct toy_game_enemy *e = &g->enemies[i];
        if (e->active == 1 &&
            (e->ai_state == TOY_GAME_ENEMY_ALERT ||
             e->ai_state == TOY_GAME_ENEMY_CHASE ||
             e->ai_state == TOY_GAME_ENEMY_TRACKING))
            g->active_attackers++;
    }
    /* The host owns the result.  A downed player can wait for a remote
     * teammate only when that teammate is actually present; in a solo game
     * (including an empty host room) nobody can perform the rescue. */
    /* Downed players remain in the world while waiting for a teammate or
     * choosing the paid respawn option.  The latter is handled by the
     * session layer, so a solo player must not be forced into Game Over. */
    update_campaign_goal(g, dt_ms);
    if (g->state != TOY_GAME_PLAYING) return;
    phase_before = g->campaign_phase;
    update_alarm_event(g, dt_ms);
    if (phase_before == TOY_GAME_PHASE_CALM &&
        g->campaign_phase == TOY_GAME_PHASE_CALM)
        update_director(g, dt_ms);
    update_campaign_spawning(g, dt_ms);
}

/* ── 僵尸 AI ───────────────────────────────────────────────────── */

static void bite_player(struct toy_game *g, struct toy_game_enemy *e)
{
    const struct toy_game_enemy_info *info = toy_game_enemy_info(e->type);
    if (e->bite_cooldown_ms > 0 || g->player_down) return;
    e->bite_cooldown_ms = TOY_GAME_BITE_MS;
    g->hp -= info->bite_damage;
    if (g->hp < 0) g->hp = 0;
    g->damage_flash_ms = TOY_GAME_DAMAGE_FLASH_MS;
    e->hurt = 150;
    push_event(g, TOY_GAME_EV_BITE);
    if (g->hp <= 0) {
        g->player_down = 1;
        g->player_revive_progress_ms = 0;
        toy_game_animation_set(&g->animation, TOY_GAME_ANIM_DEATH);
        push_event(g, TOY_GAME_EV_ACTOR_DOWN);
    } else toy_game_animation_set(&g->animation, TOY_GAME_ANIM_HIT);
}

static void bite_ai(struct toy_game *g, struct toy_game_enemy *e)
{
    const struct toy_game_enemy_info *info = toy_game_enemy_info(e->type);
    struct toy_game_actor *actor;
    int index = e->target_index;
    if (index < 0 || index >= TOY_GAME_MAX_ACTORS) index = 0;
    actor = &g->actors[index];
    if (e->bite_cooldown_ms > 0 || !actor->active ||
        (actor->kind != TOY_GAME_ACTOR_AI &&
         actor->kind != TOY_GAME_ACTOR_PLAYER) ||
        actor->state != TOY_GAME_ACTOR_ALIVE) return;
    e->bite_cooldown_ms = TOY_GAME_BITE_MS;
    actor->hp -= info->bite_damage;
    if (actor->hp < 0) actor->hp = 0;
    e->hurt = 150;
    push_event(g, TOY_GAME_EV_BITE);
    if (actor->hp <= 0) {
        actor->state = TOY_GAME_ACTOR_DOWNED;
        toy_game_actor_set_animation(actor, TOY_GAME_ANIM_DEATH);
        actor->revive_progress_ms = 0;
        if (index == 0) {
            g->ai_hp = 0;
            g->ai_down = 1;
            g->ai_revive_progress_ms = 0;
        }
        push_event(g, TOY_GAME_EV_ACTOR_DOWN);
    } else if (actor->kind == TOY_GAME_ACTOR_AI) {
        toy_game_actor_set_animation(actor, TOY_GAME_ANIM_SHOVE);
        actor->animation.time_ms = 0;
        ai_try_shove(g, actor);
    }
}

static struct toy_game_actor *base_core_actor(struct toy_game *g)
{
    if (!g || g->base_actor_index < 0 ||
        g->base_actor_index >= TOY_GAME_MAX_ACTORS)
        return NULL;
    if (!g->actors[g->base_actor_index].active)
        return NULL;
    return &g->actors[g->base_actor_index];
}

static void update_base_core(struct toy_game *g, int dt_ms)
{
    struct toy_game_actor *base = base_core_actor(g);
    if (!base) return;
    if (base->state == TOY_GAME_ACTOR_ALIVE && base->hp > 0) {
        g->base_regen_timer_ms -= dt_ms;
        while (g->base_regen_timer_ms <= 0) {
            if (base->hp < base->max_hp) base->hp++;
            g->base_regen_timer_ms += TOY_CONFIG_BASE_REGEN_MS;
        }
    }
    if (base->hp <= 0 || base->state != TOY_GAME_ACTOR_ALIVE) {
        base->hp = 0;
        base->state = TOY_GAME_ACTOR_DOWNED;
        g->state = TOY_GAME_OVER;
        push_event(g, TOY_GAME_EV_PLAYER_DEATH);
    }
}

/* AI 朝向使用整数单位向量。用叉积/点积估算夹角，避免 freestanding
 * 运行时依赖三角函数；实际转动只需处理每帧几度的小角度。 */
static int ai_angle_error(const struct toy_game_actor *a, int dx, int dz)
{
    long long dist2 = (long long)dx * dx + (long long)dz * dz;
    long long dist = isqrt(dist2);
    long long dot, cross, abs_cross;
    int ratio, angle;
    if (!a || dist <= 0) return 0;
    dx = (int)((long long)dx * 1024 / dist);
    dz = (int)((long long)dz * 1024 / dist);
    dot = (long long)a->sy * dx + (long long)a->cy * dz;
    cross = (long long)a->sy * dz - (long long)a->cy * dx;
    abs_cross = cross < 0 ? -cross : cross;
    if (dot >= 0) {
        ratio = dot > 0 ? (int)(abs_cross * 1024 / dot) : 1024;
        if (ratio > 1024) ratio = 1024;
        angle = ratio * (45 + 14 * (1024 - ratio) / 1024) / 1024;
    } else {
        ratio = -dot > 0 ? (int)(abs_cross * 1024 / -dot) : 1024;
        if (ratio > 1024) ratio = 1024;
        angle = 180 - ratio * (45 + 14 * (1024 - ratio) / 1024) / 1024;
    }
    return angle;
}

static int ai_turn_toward(struct toy_game_actor *a, int dx, int dz,
                          int speed_degree, int dt_ms)
{
    long long dist2 = (long long)dx * dx + (long long)dz * dz;
    long long dist = isqrt(dist2);
    long long cross, facing_len;
    int target_x, target_z, angle, step, radians, radians2;
    int sin_step, cos_step, next_x, next_z;
    if (!a || dist <= 0) return 0;
    target_x = (int)((long long)dx * 1024 / dist);
    target_z = (int)((long long)dz * 1024 / dist);
    if (a->sy == 0 && a->cy == 0) {
        a->sy = target_x;
        a->cy = target_z;
        return 0;
    }
    angle = ai_angle_error(a, dx, dz);
    cross = (long long)a->sy * target_z - (long long)a->cy * target_x;
    step = (speed_degree * dt_ms + a->ai_turn_remainder) / 1000;
    a->ai_turn_remainder = (speed_degree * dt_ms +
                            a->ai_turn_remainder) % 1000;
    if (step < 1) step = 1;
    if (step > angle) step = angle;
    if (step > 0) {
        radians = step * 179 * 1024 / (180 * 100);
        radians2 = radians * radians / 1024;
        sin_step = radians - radians2 * radians / (6 * 1024);
        cos_step = 1024 - radians2 / 2 + radians2 * radians2 / (24 * 1024);
        if (cross < 0) sin_step = -sin_step;
        next_x = (a->sy * cos_step - a->cy * sin_step) / 1024;
        next_z = (a->sy * sin_step + a->cy * cos_step) / 1024;
        /* 定点乘法每帧都会截断；不重新归一化时，sy/cy 的长度会
         * 逐渐小于 1024，渲染器把它们当旋转基向量后模型就会变扁。 */
        facing_len = isqrt((long long)next_x * next_x +
                           (long long)next_z * next_z);
        if (facing_len > 0) {
            a->sy = (int)((long long)next_x * 1024 / facing_len);
            a->cy = (int)((long long)next_z * 1024 / facing_len);
        }
    }
    return ai_angle_error(a, dx, dz);
}

static void turn_enemy_toward(struct toy_game_enemy *e, int dx, int dz)
{
    long long dist2 = (long long)dx * dx + (long long)dz * dz;
    long long dist = isqrt(dist2);
    long long facing_len;
    int target_x, target_z;
    if (dist == 0) return;
    target_x = (int)((long long)dx * 1024 / dist);
    target_z = (int)((long long)dz * 1024 / dist);
    e->dir_x += (target_x - e->dir_x) / 8;
    e->dir_z += (target_z - e->dir_z) / 8;
    facing_len = isqrt((long long)e->dir_x * e->dir_x +
                       (long long)e->dir_z * e->dir_z);
    if (facing_len > 0) {
        e->dir_x = (int)((long long)e->dir_x * 1024 / facing_len);
        e->dir_z = (int)((long long)e->dir_z * 1024 / facing_len);
    }
}

static void wander_enemy(struct toy_game *g, struct toy_game_enemy *e, int dt_ms)
{
    int step, nx, nz;
    e->wander_timer_ms -= dt_ms;
    if (e->wander_timer_ms <= 0) {
        int direction = rand_range(g, 0, 7);
        e->dir_x = enemy_dir_x[direction];
        e->dir_z = enemy_dir_z[direction];
        e->wander_timer_ms = rand_range(g, 600, 1800);
    }
    step = e->speed / 4;
    if (step < 1) step = 1;
    nx = e->dir_x * step / 1024;
    nz = e->dir_z * step / 1024;
    if (!enemy_position_blocked(g, e->x + nx, e->z, enemy_radius(e)))
        e->x += nx;
    else
        e->wander_timer_ms = 0;
    if (!enemy_position_blocked(g, e->x, e->z + nz, enemy_radius(e)))
        e->z += nz;
    else
        e->wander_timer_ms = 0;
}

static void chase_enemy(struct toy_game *g, struct toy_game_enemy *e,
                        int dx, int dz, long long dist, int target_kind)
{
    int nx, nz;
    if (dist < TOY_GAME_ATTACK_RANGE) {
        if (target_kind == 0 && !player_in_safe_room(g)) bite_player(g, e);
        else if (target_kind == TOY_GAME_TARGET_ACTOR) bite_ai(g, e);
        return;
    }
    if (dist == 0) return;
    nx = (int)((long long)dx * e->speed / dist);
    nz = (int)((long long)dz * e->speed / dist);
    if (!enemy_position_blocked(g, e->x + nx, e->z, enemy_radius(e)))
        e->x += nx;
    if (!enemy_position_blocked(g, e->x, e->z + nz, enemy_radius(e)))
        e->z += nz;
}

static void enemy_investigate_noise(struct toy_game_enemy *e, int x, int z)
{
    if (e->ai_state == TOY_GAME_ENEMY_ALERT ||
        e->ai_state == TOY_GAME_ENEMY_CHASE ||
        e->ai_state == TOY_GAME_ENEMY_TRACKING) return;
    e->ai_state = TOY_GAME_ENEMY_INVESTIGATE;
    e->ai_timer_ms = TOY_GAME_INVESTIGATE_MS;
    e->target_x = x;
    e->target_z = z;
}

static void enemy_remember_target(struct toy_game_enemy *e, int x, int z)
{
    e->last_seen_x = x;
    e->last_seen_z = z;
    e->target_x = x;
    e->target_z = z;
    e->lost_sight_ms = 0;
}

static void enemy_enter_search(struct toy_game_enemy *e)
{
    e->ai_state = TOY_GAME_ENEMY_SEARCH;
    e->ai_timer_ms = TOY_GAME_SEARCH_MS;
    e->wander_timer_ms = 0;
}

static int nearest_ai_position(const struct toy_game *g,
                               const struct toy_game_enemy *e,
                               int *out_x, int *out_z,
                               long long *out_dist2,
                               int *out_index)
{
    int i, found = 0;
    long long best = 0;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
        const struct toy_game_actor *a = &g->actors[i];
        long long dx, dz, d2;
        if (!enemy_target_valid(g, e, 1, i, NULL, NULL)) continue;
        dx = (long long)a->x - e->x;
        dz = (long long)a->z - e->z;
        d2 = dx * dx + dz * dz;
        if (!found || d2 < best) {
            found = 1; best = d2;
            *out_x = a->x; *out_z = a->z;
            if (out_index) *out_index = i;
        }
    }
    if (out_dist2) *out_dist2 = best;
    return found;
}

/* 特感在主机玩家和所有存活 actor 中选择最近目标。 */
static int nearest_special_target(const struct toy_game *g,
                                  const struct toy_game_enemy *e,
                                  int *out_x, int *out_z,
                                  int *out_player, int *out_actor)
{
    int found = 0, player = -1, actor = -1;
    long long best = 0, dx, dz, d2;
    if (enemy_target_valid(g, e, 0, -1, NULL, NULL)) {
        dx = (long long)g->px - e->x; dz = (long long)g->pz - e->z;
        best = dx * dx + dz * dz; found = 1; player = 0;
    }
    for (int i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
        const struct toy_game_actor *a = &g->actors[i];
        if (!enemy_target_valid(g, e, 1, i, NULL, NULL)) continue;
        dx = (long long)a->x - e->x; dz = (long long)a->z - e->z;
        d2 = dx * dx + dz * dz;
        if (!found || d2 < best) {
            best = d2; found = 1; player = 1; actor = i;
        }
    }
    if (!found) {
        if (out_player) *out_player = -1;
        if (out_actor) *out_actor = -1;
        return 0;
    }
    if (player == 1) { *out_x = g->actors[actor].x; *out_z = g->actors[actor].z; }
    else { *out_x = g->px; *out_z = g->pz; }
    if (out_player) *out_player = player;
    if (out_actor) *out_actor = actor;
    return 1;
}

static void emit_enemy_noise(struct toy_game *g, int x, int z, int range)
{
    int i;
    long long range2 = (long long)range * range;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        struct toy_game_enemy *e = &g->enemies[i];
        long long dx, dz;
        if (e->active != 1) continue;
        dx = e->x - x;
        dz = e->z - z;
        if (dx * dx + dz * dz <= range2)
            enemy_investigate_noise(e, x, z);
    }
}

static void alert_nearby_enemies(struct toy_game *g,
                                 struct toy_game_enemy *source)
{
    int i;
    long long alert2 = (long long)TOY_GAME_LOCAL_ALERT_RANGE *
                       TOY_GAME_LOCAL_ALERT_RANGE;
    long long alert_max2 = (long long)TOY_GAME_LOCAL_ALERT_MAX_RANGE *
                           TOY_GAME_LOCAL_ALERT_MAX_RANGE;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        struct toy_game_enemy *e = &g->enemies[i];
        long long dx, dz, dist2;
        if (e == source || e->active != 1) continue;
        if (e->ai_state == TOY_GAME_ENEMY_ALERT ||
            e->ai_state == TOY_GAME_ENEMY_CHASE ||
            e->ai_state == TOY_GAME_ENEMY_TRACKING) continue;
        dx = e->x - source->x;
        dz = e->z - source->z;
        dist2 = dx * dx + dz * dz;
        if (dist2 <= alert_max2) {
            e->ai_state = TOY_GAME_ENEMY_ALERT;
            if (dist2 <= alert2)
                e->ai_timer_ms = rand_range(g, TOY_GAME_PROPAGATED_ALERT_MIN_MS,
                                             TOY_GAME_PROPAGATED_ALERT_MAX_MS);
            else
                e->ai_timer_ms = rand_range(g,
                                             TOY_GAME_PROPAGATED_ALERT_MAX_MS + 200,
                                             TOY_GAME_PROPAGATED_ALERT_MAX_MS + 700);
            e->target_x = g->px;
            e->target_z = g->pz;
            e->last_seen_x = g->px;
            e->last_seen_z = g->pz;
            e->lost_sight_ms = 0;
        }
    }
    push_event(g, TOY_GAME_EV_ENEMY_ALERT);
}

static int enemy_has_line_of_sight(const struct toy_game *g,
                                   const struct toy_game_enemy *e,
                                   int target_x, int target_z)
{
    int i;
    for (i = 0; i < g->world_count; i++)
        if (segment_hits_box(e->x, e->z, target_x, target_z, &g->world[i]))
            return 0;
    return 1;
}

static int enemy_target_reachable(const struct toy_game *g,
                                  const struct toy_game_enemy *e,
                                  int x, int z)
{
    int from, to;
    /* A few legacy unit cases construct a world by directly clearing
     * world_count.  Keep those freestanding fixtures correct while normal
     * gameplay uses explicit topology rebuilds on state changes. */
    if (g->nav_width <= 0 || g->world_count == 0)
        toy_game_rebuild_navigation((struct toy_game *)g);
    from = nav_component_at_position(g, e->x, e->z);
    to = nav_component_at_position(g, x, z);
    return from != 0 && from == to;
}

/* Target validity is deliberately independent of LOS.  LOS belongs to
 * perception and special attacks; this rule answers whether an actor may be
 * retained as a navigation target at all. */
static int enemy_target_valid(const struct toy_game *g,
                              const struct toy_game_enemy *e,
                              int target_kind, int target_index,
                              int *out_x, int *out_z)
{
    int x, z;
    if (target_kind == 0) {
        if (g->player_down) return 0;
        x = g->px; z = g->pz;
    } else if (target_kind == 1 && target_index >= 0 &&
               target_index < TOY_GAME_MAX_ACTORS) {
        const struct toy_game_actor *a = &g->actors[target_index];
        if (!a->active || (a->kind != TOY_GAME_ACTOR_AI &&
                           a->kind != TOY_GAME_ACTOR_PLAYER) ||
            a->state != TOY_GAME_ACTOR_ALIVE || a->hp <= 0) return 0;
        x = a->x; z = a->z;
    } else return 0;
    if (!enemy_target_reachable(g, e, x, z)) return 0;
    if (out_x) *out_x = x;
    if (out_z) *out_z = z;
    return 1;
}

static int player_in_safe_room_at(const struct toy_game *g, int x, int z)
{
    int i;
    for (i = 0; i < g->safe_room_count; i++)
        if (toy_game_point_in_box(x, z, &g->safe_rooms[i])) return 1;
    return 0;
}

/* 0=不可见，1=侧面/边缘视野，2=正面或极近距离。 */
static int enemy_visual_stimulus(const struct toy_game *g,
                                 const struct toy_game_enemy *e,
                                 int target_x, int target_z,
                                 int dx, int dz, long long dist)
{
    long long dot;
    if (player_in_safe_room_at(g, target_x, target_z) ||
        dist > TOY_GAME_DETECT_RANGE) return 0;
    if (!enemy_has_line_of_sight(g, e, target_x, target_z)) return 0;
    if (dist <= TOY_GAME_CLOSE_DETECT_RANGE) return 2;
    dot = (long long)e->dir_x * dx + (long long)e->dir_z * dz;
    if (dot <= 0) return 0;
    /* 约 cos(70°)：正面约 140° 为强刺激，其余前半球为弱刺激。 */
    if (dot * 3 >= dist * 1024) return 2;
    return 1;
}

/* 特感技能拥有独立于普通索敌的远距离视野；普通敌人的察觉距离不能
 * 意外截断 Smoker/Charger 的技能距离。 */
static int enemy_special_visual_stimulus(const struct toy_game *g,
                                         const struct toy_game_enemy *e,
                                         int target_x, int target_z,
                                         int dx, int dz, long long dist,
                                         int range)
{
    if (player_in_safe_room_at(g, target_x, target_z) || dist > range ||
        !enemy_has_line_of_sight(g, e, target_x, target_z)) return 0;
    /* 技能索敌不受初始朝向影响；进入技能状态后由各自逻辑立即转身。 */
    (void)e;
    (void)dx;
    (void)dz;
    return dist <= TOY_GAME_CLOSE_DETECT_RANGE ? 2 : 1;
}

static void release_player_special(struct toy_game *g)
{
    g->player_control_disabled = 0;
    g->player_pull_enemy_index = -1;
}

static void move_player_forced(struct toy_game *g, int dx, int dz)
{
    int nx = g->px + dx;
    int nz = g->pz + dz;
    int height = g->player_ground_y + g->player_airborne_y;
    if (!toy_game_position_blocked_at_height(g, nx, g->pz,
                                             TOY_GAME_PLAYER_RADIUS, height))
        g->px = nx;
    if (!toy_game_position_blocked_at_height(g, g->px, nz,
                                             TOY_GAME_PLAYER_RADIUS, height))
        g->pz = nz;
}

static void move_enemy_forced(struct toy_game *g, struct toy_game_enemy *e,
                              int dx, int dz)
{
    int nx = e->x + dx;
    int nz = e->z + dz;
    int radius = enemy_radius(e);
    if (!enemy_position_blocked(g, nx, e->z, radius))
        e->x = nx;
    if (!enemy_position_blocked(g, e->x, nz, radius))
        e->z = nz;
}

static void move_actor_forced(struct toy_game *g, struct toy_game_actor *a,
                              int dx, int dz)
{
    int nx = a->x + dx;
    int nz = a->z + dz;
    if (!toy_game_position_blocked_at_height(g, nx, a->z,
                                             TOY_GAME_PLAYER_RADIUS, 0))
        a->x = nx;
    if (!toy_game_position_blocked_at_height(g, a->x, nz,
                                             TOY_GAME_PLAYER_RADIUS, 0))
        a->z = nz;
}

/* Visibility-graph step for AI teammates.  Direct travel is preferred; when
 * a wall blocks it, route through the cheapest reachable expanded corner.
 * Keeping the selected corner until it is reached prevents wall-edge jitter. */
static int actor_segment_blocked(const struct toy_game *g,
                                 int x0, int z0, int x1, int z1, int padding)
{
    int i;
    for (i = 0; i < g->world_count; i++) {
        struct toy_game_box box = g->world[i];
        box.minx -= padding; box.maxx += padding;
        box.minz -= padding; box.maxz += padding;
        if (segment_hits_box(x0, z0, x1, z1, &box)) return i + 1;
    }
    return 0;
}

static void actor_path_toward(struct toy_game *g, struct toy_game_actor *a,
                              int target_x, int target_z, int speed)
{
    int dx, dz, distance;
    if (a->nav_active) {
        dx = a->nav_x - a->x; dz = a->nav_z - a->z;
        if ((long long)dx * dx + (long long)dz * dz <=
            (long long)(speed + 24) * (speed + 24))
            a->nav_active = 0;
    }
    if (!a->nav_active &&
        actor_segment_blocked(g, a->x, a->z, target_x, target_z,
                              TOY_GAME_PLAYER_RADIUS)) {
        int hit = actor_segment_blocked(g, a->x, a->z, target_x, target_z,
                                        TOY_GAME_PLAYER_RADIUS) - 1;
        const struct toy_game_box *b = &g->world[hit];
        int pad = TOY_GAME_PLAYER_RADIUS + 96;
        int cx[4] = { b->minx - pad, b->minx - pad,
                      b->maxx + pad, b->maxx + pad };
        int cz[4] = { b->minz - pad, b->maxz + pad,
                      b->minz - pad, b->maxz + pad };
        long long best = 0;
        int i, best_i = -1;
        for (i = 0; i < 4; i++) {
            long long ax, az, tx, tz, cost;
            if (toy_game_position_blocked(g, cx[i], cz[i],
                                          TOY_GAME_PLAYER_RADIUS)) continue;
            if (actor_segment_blocked(g, a->x, a->z, cx[i], cz[i],
                                      TOY_GAME_PLAYER_RADIUS)) continue;
            ax = cx[i] - a->x; az = cz[i] - a->z;
            tx = target_x - cx[i]; tz = target_z - cz[i];
            cost = (long long)isqrt(ax * ax + az * az) +
                   (long long)isqrt(tx * tx + tz * tz);
            if (best_i < 0 || cost < best) { best = cost; best_i = i; }
        }
        if (best_i >= 0) {
            a->nav_x = cx[best_i]; a->nav_z = cz[best_i];
            a->nav_active = 1;
        }
    }
    dx = (a->nav_active ? a->nav_x : target_x) - a->x;
    dz = (a->nav_active ? a->nav_z : target_z) - a->z;
    distance = isqrt((long long)dx * dx + (long long)dz * dz);
    if (distance > 0)
        move_actor_forced(g, a, dx * speed / distance, dz * speed / distance);
}

static void update_motion_values(struct toy_game *g, int *x, int *z,
                                 int *airborne_ms, int *airborne_y,
                                 int *vertical_velocity, int *knockback_x,
                                 int *knockback_z, int radius, int dt_ms)
{
    if (*airborne_ms <= 0) return;
    *airborne_ms -= dt_ms;
    if (*airborne_ms < 0) *airborne_ms = 0;
    *airborne_y += *vertical_velocity;
    *vertical_velocity -= TOY_GAME_AIRBORNE_GRAVITY;
    if (*knockback_x || *knockback_z) {
        int nx = *x + *knockback_x;
        int nz = *z + *knockback_z;
        if (!toy_game_position_blocked_at_height(g, nx, *z, radius, 0))
            *x = nx;
        if (!toy_game_position_blocked_at_height(g, *x, nz, radius, 0))
            *z = nz;
        *knockback_x = *knockback_x * 3 / 4;
        *knockback_z = *knockback_z * 3 / 4;
    }
    if (*airborne_y <= 0 && *vertical_velocity < 0) {
        *airborne_y = 0;
        *airborne_ms = 0;
    }
    if (*airborne_ms == 0) {
        *vertical_velocity = 0;
        *knockback_x = 0;
        *knockback_z = 0;
        *airborne_y = 0;
    }
}

static void update_player_special_motion(struct toy_game *g, int dt_ms)
{
    if (g->player_airborne_ms > 0) {
        int landing_ground;
        g->player_airborne_ms -= dt_ms;
        if (g->player_airborne_ms < 0) g->player_airborne_ms = 0;
        g->player_airborne_y += g->player_vertical_velocity;
        g->player_vertical_velocity -= TOY_GAME_AIRBORNE_GRAVITY;
        /* Apply horizontal jump momentum before checking the landing surface,
         * so a jump can reach a platform during this frame. */
        if (g->player_air_x || g->player_air_z)
            move_player_forced(g, g->player_air_x, g->player_air_z);
        /* Descending onto even a partially covered edge must land on the
         * platform top.  Otherwise the player reaches ground height while the
         * collision circle still intersects the platform side and gets stuck. */
        landing_ground = ground_height_overlapping(g, g->px, g->pz,
                                                   TOY_GAME_PLAYER_RADIUS);
        if (g->player_vertical_velocity < 0 &&
            ((landing_ground >= g->player_ground_y &&
              g->player_airborne_y <= landing_ground - g->player_ground_y) ||
             (landing_ground < g->player_ground_y &&
              g->player_airborne_y <= 0))) {
            g->player_airborne_y = 0;
            g->player_airborne_ms = 0;
            g->player_ground_y = landing_ground;
        }
        if (g->player_knockback_x || g->player_knockback_z) {
            move_player_forced(g, g->player_knockback_x,
                               g->player_knockback_z);
            g->player_knockback_x = g->player_knockback_x * 3 / 4;
            g->player_knockback_z = g->player_knockback_z * 3 / 4;
        }
        if (g->player_airborne_ms == 0) {
            g->player_vertical_velocity = 0;
            g->player_knockback_x = 0;
            g->player_knockback_z = 0;
            g->player_airborne_y = 0;
            g->player_air_x = 0;
            g->player_air_z = 0;
            if (g->player_pull_enemy_index < 0)
                g->player_control_disabled = 0;
        }
    }
}

static void update_remote_player_motion(struct toy_game *g, int *x, int *z,
                                        int *airborne_ms, int *airborne_y,
                                        int *ground_y, int *vertical_velocity,
                                        int *air_x, int *air_z,
                                        int *knockback_x, int *knockback_z,
                                        int dt_ms)
{
    int landing_ground, nx, nz, height;
    if (*airborne_ms <= 0) return;
    *airborne_ms -= dt_ms;
    if (*airborne_ms < 0) *airborne_ms = 0;
    *airborne_y += *vertical_velocity;
    *vertical_velocity -= TOY_GAME_AIRBORNE_GRAVITY;
    height = *ground_y + *airborne_y;
    if (*air_x || *air_z) {
        nx = *x + *air_x;
        nz = *z + *air_z;
        if (!toy_game_position_blocked_at_height(g, nx, *z,
                                                  TOY_GAME_PLAYER_RADIUS,
                                                  height)) *x = nx;
        if (!toy_game_position_blocked_at_height(g, *x, nz,
                                                  TOY_GAME_PLAYER_RADIUS,
                                                  height)) *z = nz;
    }
    landing_ground = ground_height_overlapping(g, *x, *z,
                                                TOY_GAME_PLAYER_RADIUS);
    if (*vertical_velocity < 0 &&
        ((landing_ground >= *ground_y &&
          *airborne_y <= landing_ground - *ground_y) ||
         (landing_ground < *ground_y && *airborne_y <= 0))) {
        *airborne_y = 0;
        *airborne_ms = 0;
        *ground_y = landing_ground;
    }
    if (*knockback_x || *knockback_z) {
        nx = *x + *knockback_x;
        nz = *z + *knockback_z;
        if (!toy_game_position_blocked_at_height(g, nx, *z,
                                                  TOY_GAME_PLAYER_RADIUS,
                                                  height)) *x = nx;
        if (!toy_game_position_blocked_at_height(g, *x, nz,
                                                  TOY_GAME_PLAYER_RADIUS,
                                                  height)) *z = nz;
        *knockback_x = *knockback_x * 3 / 4;
        *knockback_z = *knockback_z * 3 / 4;
    }
    if (*airborne_ms == 0) {
        *vertical_velocity = 0;
        *airborne_y = 0;
        *air_x = 0;
        *air_z = 0;
        *knockback_x = 0;
        *knockback_z = 0;
    }
}

int toy_game_jump(struct toy_game *g)
{
    return toy_game_jump_with_velocity(g, 0, 0);
}

int toy_game_jump_with_velocity(struct toy_game *g, int dx, int dz)
{
    if (!g || g->state != TOY_GAME_PLAYING || g->player_down ||
        g->player_control_disabled || g->player_airborne_ms > 0)
        return 0;
    g->player_airborne_ms = TOY_GAME_JUMP_MS;
    g->player_airborne_y = 0;
    g->player_vertical_velocity = TOY_GAME_JUMP_VELOCITY;
    g->player_air_x = dx;
    g->player_air_z = dz;
    g->player_knockback_x = 0;
    g->player_knockback_z = 0;
    return 1;
}

void toy_game_update_player_motion(struct toy_game *g, int dt_ms)
{
    if (g) update_player_special_motion(g, dt_ms);
}

int toy_game_jump_actor(struct toy_game *g, int actor_index, int dx, int dz)
{
    struct toy_game_actor *actor;
    if (!g || actor_index < 0 || actor_index >= TOY_GAME_MAX_ACTORS)
        return 0;
    actor = &g->actors[actor_index];
    if (!actor->active || actor->kind != TOY_GAME_ACTOR_PLAYER ||
        actor->state != TOY_GAME_ACTOR_ALIVE || actor->airborne_ms > 0)
        return 0;
    actor->airborne_ms = TOY_GAME_JUMP_MS;
    actor->airborne_y = 0;
    actor->vertical_velocity = TOY_GAME_JUMP_VELOCITY;
    actor->air_x = dx;
    actor->air_z = dz;
    actor->knockback_x = 0;
    actor->knockback_z = 0;
    return 1;
}

void toy_game_update_actor_motion(struct toy_game *g, int actor_index, int dt_ms)
{
    struct toy_game_actor *actor;
    if (!g || actor_index < 0 || actor_index >= TOY_GAME_MAX_ACTORS)
        return;
    actor = &g->actors[actor_index];
    if (!actor->active || actor->kind != TOY_GAME_ACTOR_PLAYER ||
        actor->state != TOY_GAME_ACTOR_ALIVE)
        return;
    update_remote_player_motion(g, &actor->x, &actor->z,
        &actor->airborne_ms, &actor->airborne_y, &actor->ground_y,
        &actor->vertical_velocity, &actor->air_x, &actor->air_z,
        &actor->knockback_x, &actor->knockback_z, dt_ms);
    if (actor->airborne_ms == 0) {
        actor->air_x = 0;
        actor->air_z = 0;
        toy_game_update_actor_ground(g, actor_index);
    }
}

void toy_game_update_actor_ground(struct toy_game *g, int actor_index)
{
    struct toy_game_actor *actor;
    int i, next_ground;
    if (!g || actor_index < 0 || actor_index >= TOY_GAME_MAX_ACTORS) return;
    actor = &g->actors[actor_index];
    if (actor->airborne_ms > 0) return;
    next_ground = toy_game_ground_height(g, actor->x, actor->z,
                                         TOY_GAME_PLAYER_RADIUS);
    if (next_ground < actor->ground_y) {
        for (i = 0; i < g->platform_count; i++) {
            const struct toy_game_platform *p = &g->platforms[i];
            if (p->height != actor->ground_y) continue;
            if (actor->x + TOY_GAME_PLAYER_RADIUS > p->minx &&
                actor->x - TOY_GAME_PLAYER_RADIUS < p->maxx &&
                actor->z + TOY_GAME_PLAYER_RADIUS > p->minz &&
                actor->z - TOY_GAME_PLAYER_RADIUS < p->maxz) return;
        }
        actor->airborne_y = actor->ground_y - next_ground;
        actor->ground_y = next_ground;
        actor->airborne_ms = TOY_GAME_JUMP_MS;
        actor->vertical_velocity = 0;
        return;
    }
    actor->ground_y = next_ground;
}

static void update_smoker(struct toy_game *g, struct toy_game_enemy *e,
                          int index, int target_x, int target_z,
                          int target_kind, int target_index,
                          int dx, int dz, long long dist, int visual,
                          int dt_ms)
{
    long long pull_dist;
    int pull_dx, pull_dz, pull_x, pull_z, step;
    if (e->ability.special_timer_ms > 0) {
        e->ability.special_timer_ms -= dt_ms;
        if (e->ability.special_timer_ms < 0) e->ability.special_timer_ms = 0;
    }
    if (e->ability.special_target_active) {
        e->ability.special_target_active = 1;
        target_kind = e->ability.special_target_kind;
        target_index = e->ability.special_target_index;
        if (target_kind == 1 && target_index >= 0 &&
                   target_index < TOY_GAME_MAX_ACTORS) {
            pull_x = g->actors[target_index].x;
            pull_z = g->actors[target_index].z;
        } else {
            pull_x = g->px; pull_z = g->pz;
        }
        if (!enemy_target_valid(g, e, target_kind, target_index,
                                &pull_x, &pull_z)) {
            e->ability.special_target_active = 0;
            if (target_kind == 0) release_player_special(g);
            return;
        }
        if (target_kind == 0)
            e->ability.special_pull_timer_ms -= dt_ms;
        pull_dx = e->x - pull_x;
        pull_dz = e->z - pull_z;
        pull_dist = isqrt((long long)pull_dx * pull_dx +
                          (long long)pull_dz * pull_dz);
        if ((target_kind == 0 && e->ability.special_pull_timer_ms <= 0) ||
            (target_kind == 1 && target_index >= 0 &&
             target_index < TOY_GAME_MAX_ACTORS &&
             (!g->actors[target_index].active ||
              g->actors[target_index].state != TOY_GAME_ACTOR_ALIVE)) ||
            !enemy_has_line_of_sight(g, e, pull_x, pull_z)) {
            e->ability.special_target_active = 0;
            if (target_kind == 0) release_player_special(g);
            return;
        }
        /* 舌头把玩家拉到身边后保持束缚，并按接触间隔造成伤害。 */
        if (pull_dist < 420 && e->bite_cooldown_ms <= 0) {
            if (target_kind == 0) {
                g->hp -= TOY_GAME_SMOKER_DAMAGE;
                if (g->hp < 0) g->hp = 0;
                g->damage_flash_ms = TOY_GAME_DAMAGE_FLASH_MS;
                toy_game_animation_set(&g->animation,
                    g->hp <= 0 ? TOY_GAME_ANIM_DEATH : TOY_GAME_ANIM_HIT);
                if (g->hp <= 0) g->player_down = 1;
            } else if (target_kind == 1 && target_index >= 0 &&
                       target_index < TOY_GAME_MAX_ACTORS) {
                struct toy_game_actor *a = &g->actors[target_index];
                a->hp -= TOY_GAME_SMOKER_DAMAGE;
                if (a->hp <= 0) {
                    a->hp = 0; a->state = TOY_GAME_ACTOR_DOWNED;
                    toy_game_actor_set_animation(a, TOY_GAME_ANIM_DEATH);
                    a->revive_progress_ms = 0;
                } else toy_game_actor_set_animation(a, TOY_GAME_ANIM_HIT);
            }
            e->bite_cooldown_ms = 1000;
        }
        step = TOY_GAME_SMOKER_PULL_STEP;
        if (pull_dist > 420) {
            int mx = pull_dx * step / (int)pull_dist;
            int mz = pull_dz * step / (int)pull_dist;
            if (target_kind == 0)
                move_player_forced(g, mx, mz);
            else if (target_kind == 1 &&
                     !g->actors[target_index].base_core)
                toy_game_move_ai_actor(g, target_index, pull_x + mx,
                                       pull_z + mz);
        }
        if (pull_dist > 0) {
            e->dir_x = -pull_dx * 1024 / (int)pull_dist;
            e->dir_z = -pull_dz * 1024 / (int)pull_dist;
        }
        return;
    }
    e->ability.special_target_active = 0;
    if (g->player_down && target_kind == 0) return;
    if (e->ability.special_windup_ms > 0) {
        e->ability.special_windup_ms -= dt_ms;
        if (e->ability.special_windup_ms < 0) e->ability.special_windup_ms = 0;
        if (dist > 0) {
            e->dir_x = dx * 1024 / (int)dist;
            e->dir_z = dz * 1024 / (int)dist;
        }
        if (e->ability.special_windup_ms == 0) {
            e->ability.special_target_active = 1;
            e->ability.special_pull_timer_ms = TOY_GAME_SMOKER_PULL_MS;
            if (e->ability.special_target_kind == 1)
                e->ability.special_pull_timer_ms = -1;
            if (e->ability.special_target_kind == 0) {
                g->player_pull_enemy_index = index;
                g->player_pull_timer_ms = TOY_GAME_SMOKER_PULL_MS;
                g->player_control_disabled = 1;
            }
        }
        return;
    }
    if (visual && dist >= 700 && dist <= TOY_GAME_SMOKER_RANGE) {
        if (dist > 0) {
            e->dir_x = dx * 1024 / (int)dist;
            e->dir_z = dz * 1024 / (int)dist;
        }
        e->ability.special_target_active = 0;
        e->ability.special_timer_ms = TOY_GAME_SMOKER_COOLDOWN_MS;
        e->ability.special_windup_ms = TOY_GAME_SPECIAL_WINDUP_MS;
        e->ability.special_target_kind = target_kind;
        e->ability.special_target_index = target_index;
        return;
    }
    /* Smoker 会主动靠近以寻找玩家，但永远不调用普通近战攻击。 */
    if (dist > 0) turn_enemy_toward(e, dx, dz);
    if (dist > 700 && (!visual || dist > TOY_GAME_SMOKER_RANGE))
        chase_enemy(g, e, dx, dz, dist, target_kind);
    (void)target_x;
    (void)target_z;
    (void)dt_ms;
}

static int apply_entity_impact_with_knockback(struct toy_game *g, int kind,
                                              int index, int dx, int dz,
                                              int damage, int knockback)
{
    long long dist = isqrt((long long)dx * dx + (long long)dz * dz);
    if (!g || dist <= 0) { dx = 0; dz = 1024; dist = 1024; }
    dx = dx * knockback / (int)dist;
    dz = dz * knockback / (int)dist;
    if (kind == TOY_GAME_ENTITY_PLAYER) {
        if (g->player_down) return 0;
        g->hp -= damage; if (g->hp < 0) g->hp = 0;
        if (g->hp == 0) {
            g->player_down = 1;
            g->player_revive_progress_ms = 0;
            toy_game_animation_set(&g->animation, TOY_GAME_ANIM_DEATH);
        } else toy_game_animation_set(&g->animation, TOY_GAME_ANIM_HIT);
        g->damage_flash_ms = TOY_GAME_DAMAGE_FLASH_MS;
        g->player_airborne_ms = TOY_GAME_AIRBORNE_MS;
        g->player_airborne_y = 0;
        g->player_vertical_velocity = TOY_GAME_AIRBORNE_VELOCITY;
        g->player_knockback_x = dx; g->player_knockback_z = dz;
        g->player_control_disabled = 1;
        return 1;
    }
    if (kind == TOY_GAME_ENTITY_ACTOR) {
        struct toy_game_actor *a;
        if (index < 0 || index >= TOY_GAME_MAX_ACTORS) return 0;
        a = &g->actors[index];
        if (!a->active || a->state != TOY_GAME_ACTOR_ALIVE) return 0;
        a->hp -= damage; if (a->hp < 0) a->hp = 0;
        if (a->hp == 0) {
            a->state = TOY_GAME_ACTOR_DOWNED;
            toy_game_actor_set_animation(a, TOY_GAME_ANIM_DEATH);
            a->revive_progress_ms = 0;
        } else if (a->kind == TOY_GAME_ACTOR_AI && !a->base_core) {
            toy_game_actor_set_animation(a, TOY_GAME_ANIM_SHOVE);
            a->animation.time_ms = 0;
            ai_try_shove(g, a);
        } else toy_game_actor_set_animation(a, TOY_GAME_ANIM_HIT);
        if (a->base_core || a->hit_test_dummy) {
            /* The dedicated HIT_TEST actor is a stationary target: keep the
             * hit clip readable instead of hiding it inside knockback. */
            a->airborne_ms = 0;
            a->airborne_y = 0;
            a->vertical_velocity = 0;
            a->knockback_x = 0;
            a->knockback_z = 0;
        } else {
            a->airborne_ms = TOY_GAME_AIRBORNE_MS;
            a->airborne_y = 0;
            a->vertical_velocity = TOY_GAME_AIRBORNE_VELOCITY;
            a->knockback_x = dx;
            a->knockback_z = dz;
        }
        if (index == 0) load_ai_actor_to_legacy(g, a);
        return 1;
    }
    if (kind == TOY_GAME_ENTITY_ENEMY) {
        struct toy_game_enemy *e;
        if (index < 0 || index >= TOY_GAME_MAX_ENEMIES) return 0;
        e = &g->enemies[index];
        if (e->active != 1) return 0;
        e->hp -= damage;
        if (e->hp <= 0) { e->hp = 0; e->active = 2; e->dying_ms = TOY_GAME_DYING_MS; g->enemies_alive--; }
        e->ability.special_target_active = 0; e->ability.special_windup_ms = 0; e->ability.charge_active = 0;
        if (g->player_pull_enemy_index == index) release_player_special(g);
        e->airborne_ms = TOY_GAME_AIRBORNE_MS;
        e->vertical_velocity = TOY_GAME_AIRBORNE_VELOCITY; e->airborne_y = 0;
        e->knockback_x = dx; e->knockback_z = dz; e->hurt = 180;
        return 1;
    }
    return 0;
}

int toy_game_apply_entity_impact(struct toy_game *g, int kind, int index,
                                 int dx, int dz, int damage)
{
    return apply_entity_impact_with_knockback(g, kind, index, dx, dz,
                                               damage,
                                               TOY_GAME_CHARGER_KNOCKBACK);
}

static int charger_hit_entities(struct toy_game *g,
                                struct toy_game_enemy *charger)
{
    int i, hits = 0;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        struct toy_game_enemy *victim = &g->enemies[i];
        int dx, dz;
        long long dist2;
        if (victim == charger || victim->active != 1 ||
            victim->airborne_ms > 0) continue;
        dx = victim->x - charger->x;
        dz = victim->z - charger->z;
        dist2 = (long long)dx * dx + (long long)dz * dz;
        if (dist2 > (long long)TOY_GAME_CHARGER_IMPACT_RANGE *
                    TOY_GAME_CHARGER_IMPACT_RANGE) continue;
        if (toy_game_apply_entity_impact(g, TOY_GAME_ENTITY_ENEMY, i,
                                         dx, dz, TOY_GAME_CHARGER_IMPACT_DAMAGE)) hits++;
    }
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
        struct toy_game_actor *a = &g->actors[i];
        int dx, dz; long long dist2;
        if (!a->active ||
            (a->kind != TOY_GAME_ACTOR_AI &&
             a->kind != TOY_GAME_ACTOR_PLAYER) ||
            a->state != TOY_GAME_ACTOR_ALIVE || a->airborne_ms > 0) continue;
        dx = a->x - charger->x; dz = a->z - charger->z;
        dist2 = (long long)dx * dx + (long long)dz * dz;
        if (dist2 <= (long long)TOY_GAME_CHARGER_IMPACT_RANGE *
                    TOY_GAME_CHARGER_IMPACT_RANGE &&
            (!a->base_core || !charger->ability.charge_hit_base) &&
            toy_game_apply_entity_impact(g, TOY_GAME_ENTITY_ACTOR, i,
                                          dx, dz, TOY_GAME_CHARGER_IMPACT_DAMAGE)) hits++;
        if (a->base_core && a->active &&
            dist2 <= (long long)TOY_GAME_CHARGER_IMPACT_RANGE *
                     TOY_GAME_CHARGER_IMPACT_RANGE)
            charger->ability.charge_hit_base = 1;
    }
    return hits;
}

static void update_enemy_airborne(struct toy_game *g,
                                  struct toy_game_enemy *e, int dt_ms)
{
    e->airborne_ms -= dt_ms;
    if (e->airborne_ms < 0) e->airborne_ms = 0;
    e->airborne_y += e->vertical_velocity;
    e->vertical_velocity -= TOY_GAME_AIRBORNE_GRAVITY;
    if (e->knockback_x || e->knockback_z) {
        move_enemy_forced(g, e, e->knockback_x, e->knockback_z);
        e->knockback_x = e->knockback_x * 3 / 4;
        e->knockback_z = e->knockback_z * 3 / 4;
    }
    if (e->airborne_y <= 0 && e->vertical_velocity < 0) {
        e->airborne_y = 0;
        e->airborne_ms = 0;
        e->vertical_velocity = 0;
        e->knockback_x = 0;
        e->knockback_z = 0;
    }
}

static void update_charger(struct toy_game *g, struct toy_game_enemy *e,
                           int target_x, int target_z, int dx, int dz,
                           int target_kind, int target_index,
                           long long dist, int visual, int dt_ms)
{
    int nx, nz;
    (void)target_index;
    if (e->ability.charge_active) {
        e->dir_x = e->ability.charge_dir_x;
        e->dir_z = e->ability.charge_dir_z;
        if (e->ability.special_timer_ms > 0) {
            e->ability.special_timer_ms -= dt_ms;
            return;
        }
        e->ability.charge_elapsed_ms += dt_ms;
        if (e->ability.charge_elapsed_ms >= TOY_GAME_CHARGER_MAX_CHARGE_MS) {
            e->ability.charge_active = 0;
            e->ability.special_timer_ms = TOY_GAME_CHARGER_COOLDOWN_MS;
            return;
        }
        /* 沿锁定直线持续冲锋；同一轮可连续撞到多个敌人。 */
        charger_hit_entities(g, e);
        if (dist <= TOY_GAME_CHARGER_IMPACT_RANGE) {
            /* A target is launched by the impact, so do not damage the same
             * player again until they have landed.  The charge itself keeps
             * moving for its configured duration. */
            if (target_kind == 0 && g->player_airborne_ms <= 0) {
                toy_game_apply_entity_impact(g, TOY_GAME_ENTITY_PLAYER, 0,
                                             dx, dz, TOY_GAME_CHARGER_DAMAGE);
            } else if (target_kind == 1 && target_index >= 0 &&
                       target_index < TOY_GAME_MAX_ACTORS &&
                       g->actors[target_index].airborne_ms <= 0) {
                toy_game_apply_entity_impact(g, TOY_GAME_ENTITY_ACTOR,
                                             target_index, dx, dz,
                                             TOY_GAME_CHARGER_DAMAGE);
            }
            charger_hit_entities(g, e);
        }
        nx = e->ability.charge_dir_x * TOY_GAME_CHARGER_SPEED / 1024;
        nz = e->ability.charge_dir_z * TOY_GAME_CHARGER_SPEED / 1024;
        if (!enemy_position_blocked(g, e->x + nx, e->z,
                                    enemy_radius(e)))
            e->x += nx;
        if (!enemy_position_blocked(g, e->x, e->z + nz,
                                    enemy_radius(e)))
            e->z += nz;
        return;
    }
    if (e->ability.special_timer_ms > 0) {
        /* 冲锋后的恢复期主动离开玩家，再以低速徘徊等待下一轮。 */
        if (dist > 0 && dist < 3600) {
            nx = -dx * (e->speed / 4) / (int)dist;
            nz = -dz * (e->speed / 4) / (int)dist;
            if (!enemy_position_blocked(g, e->x + nx, e->z,
                                        enemy_radius(e))) e->x += nx;
            if (!enemy_position_blocked(g, e->x, e->z + nz,
                                        enemy_radius(e))) e->z += nz;
        } else {
            wander_enemy(g, e, dt_ms);
        }
        e->ability.special_timer_ms -= dt_ms;
        if (e->ability.special_timer_ms < 0) e->ability.special_timer_ms = 0;
        return;
    }
    if (visual && dist > TOY_GAME_CHARGER_IMPACT_RANGE &&
        dist <= TOY_GAME_CHARGER_RANGE) {
        if (dist > 0) {
            e->dir_x = dx * 1024 / (int)dist;
            e->dir_z = dz * 1024 / (int)dist;
            e->ability.charge_dir_x = e->dir_x;
            e->ability.charge_dir_z = e->dir_z;
        }
        e->ability.charge_active = 1;
        e->ability.charge_hit_base = 0;
        e->ability.special_timer_ms = TOY_GAME_CHARGER_WINDUP_MS;
        e->ability.charge_elapsed_ms = 0;
        e->target_x = target_x;
        e->target_z = target_z;
        return;
    }
    if (dist > TOY_GAME_ATTACK_RANGE) chase_enemy(g, e, dx, dz, dist, 0);
}

static int tank_target_in_sweep(const struct toy_game_enemy *tank,
                                int x, int z)
{
    int dx = x - tank->x, dz = z - tank->z;
    long long dist2 = (long long)dx * dx + (long long)dz * dz;
    long long dot;
    long long dist;
    if (dist2 > (long long)TOY_CONFIG_TANK_ATTACK_RANGE *
                TOY_CONFIG_TANK_ATTACK_RANGE) return 0;
    if (dist2 == 0) return 1;
    dist = isqrt(dist2);
    dot = (long long)dx * tank->dir_x + (long long)dz * tank->dir_z;
    return dot > 0 && dot * TOY_CONFIG_TANK_ATTACK_CONE >= dist * 1024;
}

static void tank_sweep_entities(struct toy_game *g,
                                struct toy_game_enemy *tank)
{
    int i, dx, dz;
    int actor_hit[TOY_GAME_MAX_ACTORS];
    int actor_dx[TOY_GAME_MAX_ACTORS], actor_dz[TOY_GAME_MAX_ACTORS];
    if (!g->player_down && tank_target_in_sweep(tank, g->px, g->pz)) {
        dx = g->px - tank->x; dz = g->pz - tank->z;
        apply_entity_impact_with_knockback(g, TOY_GAME_ENTITY_PLAYER, 0,
                                           dx, dz, TOY_CONFIG_TANK_DAMAGE,
                                           TOY_CONFIG_TANK_KNOCKBACK);
    }
    /* Freeze the cone result before applying any hit.  An AI hit reaction can
     * shove the attacker, but that must not change who was inside this one
     * simultaneous sweep. */
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
        struct toy_game_actor *a = &g->actors[i];
        actor_hit[i] = a->active && a->state == TOY_GAME_ACTOR_ALIVE &&
            (a->kind == TOY_GAME_ACTOR_AI ||
             a->kind == TOY_GAME_ACTOR_PLAYER) &&
            tank_target_in_sweep(tank, a->x, a->z);
        actor_dx[i] = a->x - tank->x;
        actor_dz[i] = a->z - tank->z;
    }
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
        struct toy_game_actor *a = &g->actors[i];
        int damage;
        if (!actor_hit[i] || !a->active ||
            a->state != TOY_GAME_ACTOR_ALIVE ||
            (a->kind != TOY_GAME_ACTOR_AI &&
             a->kind != TOY_GAME_ACTOR_PLAYER)) continue;
        damage = a->base_core ? TOY_CONFIG_TANK_BASE_DAMAGE :
                                TOY_CONFIG_TANK_DAMAGE;
        apply_entity_impact_with_knockback(g, TOY_GAME_ENTITY_ACTOR, i,
                                           actor_dx[i], actor_dz[i], damage,
                                           TOY_CONFIG_TANK_KNOCKBACK);
    }
}

static void update_tank(struct toy_game *g, struct toy_game_enemy *e,
                        int dx, int dz, long long dist, int dt_ms)
{
    if (e->ability.charge_active) {
        e->ability.charge_elapsed_ms += dt_ms;
        if (!e->ability.charge_hit_base &&
            e->ability.charge_elapsed_ms >= TOY_CONFIG_TANK_IMPACT_MS) {
            tank_sweep_entities(g, e);
            e->ability.charge_hit_base = 1; /* shared one-shot marker */
        }
        if (e->ability.charge_elapsed_ms >= TOY_CONFIG_TANK_WINDUP_MS) {
            e->ability.charge_active = 0;
            e->ability.charge_elapsed_ms = 0;
            e->ability.special_timer_ms = TOY_CONFIG_TANK_COOLDOWN_MS;
        }
        return;
    }
    if (e->ability.special_timer_ms > 0) {
        e->ability.special_timer_ms -= dt_ms;
        if (e->ability.special_timer_ms < 0) e->ability.special_timer_ms = 0;
        return;
    }
    if (dist <= TOY_CONFIG_TANK_ATTACK_START_RANGE) {
        if (dist > 0) {
            e->dir_x = dx * 1024 / (int)dist;
            e->dir_z = dz * 1024 / (int)dist;
        }
        e->ability.charge_active = 1;
        e->ability.charge_elapsed_ms = 0;
        e->ability.charge_hit_base = 0;
        return;
    }
    chase_enemy(g, e, dx, dz, dist, 0);
}

static void update_enemy_ai(struct toy_game *g, struct toy_game_enemy *e,
                            int dt_ms)
{
    int target_x, target_z, target_kind;
    int primary_dx = g->px - e->x;
    int primary_dz = g->pz - e->z;
    long long primary_dist2 = (long long)primary_dx * primary_dx +
                              (long long)primary_dz * primary_dz;
    int dx, dz;
    long long dist2;
    long long dist;
    long long ai_dist2 = 0;
    int ai_x = 0, ai_z = 0;
    int ai_index = 0;
    int special_x = g->px, special_z = g->pz;
    int special_player = -1, special_actor = -1;
    int visual;
    int primary_valid;
    int ai_available = nearest_ai_position(g, e, &ai_x, &ai_z, &ai_dist2,
                                           &ai_index);
    primary_valid = enemy_target_valid(g, e, 0, -1, NULL, NULL);
    visual = enemy_visual_stimulus(g, e, g->px, g->pz,
                                   primary_dx, primary_dz,
                                   isqrt(primary_dist2));

    if (toy_game_enemy_info(e->type)->ability ==
            TOY_GAME_ENEMY_ABILITY_SMOKER_TONGUE) {
        if (!nearest_special_target(g, e, &special_x, &special_z,
                                    &special_player, &special_actor)) {
            e->ability.special_target_active = 0;
            return;
        }
        dx = special_x - e->x; dz = special_z - e->z;
        dist = isqrt((long long)dx * dx + (long long)dz * dz);
        visual = enemy_special_visual_stimulus(g, e, special_x, special_z,
                                               dx, dz, dist,
                                               TOY_GAME_SMOKER_RANGE);
        update_smoker(g, e, (int)(e - g->enemies), special_x, special_z,
                      special_player, special_actor, dx, dz, dist, visual,
                      dt_ms);
        return;
    }
    if (toy_game_enemy_info(e->type)->ability ==
            TOY_GAME_ENEMY_ABILITY_CHARGER_RUSH) {
        if (!nearest_special_target(g, e, &special_x, &special_z,
                                    &special_player, &special_actor)) {
            e->ability.charge_active = 0;
            return;
        }
        dx = special_x - e->x; dz = special_z - e->z;
        dist = isqrt((long long)dx * dx + (long long)dz * dz);
        visual = enemy_special_visual_stimulus(g, e, special_x, special_z,
                                               dx, dz, dist,
                                               TOY_GAME_CHARGER_RANGE);
        update_charger(g, e, special_x, special_z, dx, dz,
                       special_player, special_actor, dist, visual, dt_ms);
        return;
    }
    if (toy_game_enemy_info(e->type)->ability ==
            TOY_GAME_ENEMY_ABILITY_TANK_SWEEP) {
        if (!nearest_special_target(g, e, &special_x, &special_z,
                                    &special_player, &special_actor)) {
            e->ability.charge_active = 0;
            return;
        }
        dx = special_x - e->x; dz = special_z - e->z;
        dist = isqrt((long long)dx * dx + (long long)dz * dz);
        update_tank(g, e, dx, dz, dist, dt_ms);
        return;
    }

    if (e->retarget_timer_ms > 0) e->retarget_timer_ms -= dt_ms;
    if (!enemy_target_valid(g, e, e->target_kind,
                            e->target_index, NULL, NULL)) {
        e->retarget_timer_ms = 0;
    }
    if (e->retarget_timer_ms > 0 &&
        enemy_target_valid(g, e, e->target_kind,
                           e->target_index, &target_x, &target_z)) {
        target_kind = e->target_kind;
    } else if (e->retarget_timer_ms <= 0 || e->target_kind < 0) {
        target_kind = primary_valid ? 0 : -1;
        if (ai_available) {
            if (target_kind < 0 || ai_dist2 < primary_dist2)
                target_kind = 1;
        }
        e->target_kind = target_kind;
        if (target_kind == 1) e->target_index = ai_index;
        else e->target_index = -1;
        e->retarget_timer_ms = TOY_GAME_RETARGET_MS;
    } else {
        target_kind = e->target_kind;
    }
    if (target_kind < 0 ||
        !enemy_target_valid(g, e, target_kind, e->target_index,
                            &target_x, &target_z)) {
        e->target_kind = -1;
        e->target_index = -1;
        e->retarget_timer_ms = 0;
        if (e->ai_state != TOY_GAME_ENEMY_TRACKING)
            wander_enemy(g, e, dt_ms);
        return;
    }
    dx = target_x - e->x;
    dz = target_z - e->z;
    dist2 = (long long)dx * dx + (long long)dz * dz;
    dist = isqrt(dist2);
    visual = enemy_visual_stimulus(g, e, target_x, target_z, dx, dz, dist);

    /* 尸潮追踪者：不走普通 AI 状态机，无视视线遮挡/声源/丢失目标，
     * 每帧直扑玩家当前位置（安全室仍能挡下，作为玩家最后的庇护）。 */
    if (e->ai_state == TOY_GAME_ENEMY_TRACKING) {
        enemy_remember_target(e, target_x, target_z);
        turn_enemy_toward(e, dx, dz);
        chase_enemy(g, e, dx, dz, dist, target_kind);
        return;
    }

    if (e->ai_state == TOY_GAME_ENEMY_IDLE) {
        wander_enemy(g, e, dt_ms);
        if (visual) {
            e->ai_state = TOY_GAME_ENEMY_NOTICE;
            e->ai_timer_ms = rand_range(g, TOY_GAME_NOTICE_MIN_MS,
                                        TOY_GAME_NOTICE_MAX_MS);
            if (visual == 1) e->ai_timer_ms = e->ai_timer_ms * 3 / 2;
            enemy_remember_target(e, target_x, target_z);
        }
        return;
    }
    if (e->ai_state == TOY_GAME_ENEMY_NOTICE) {
        if (!visual) {
            e->ai_state = TOY_GAME_ENEMY_IDLE;
            e->ai_timer_ms = 0;
            e->wander_timer_ms = rand_range(g, 600, 1800);
            return;
        }
        turn_enemy_toward(e, dx, dz);
        enemy_remember_target(e, target_x, target_z);
        e->ai_timer_ms -= dt_ms;
        if (e->ai_timer_ms <= 0) {
            e->ai_state = TOY_GAME_ENEMY_ALERT;
            e->ai_timer_ms = TOY_GAME_ALERT_MS;
            alert_nearby_enemies(g, e);
        }
        return;
    }
    if (e->ai_state == TOY_GAME_ENEMY_INVESTIGATE) {
        int tx = e->target_x - e->x;
        int tz = e->target_z - e->z;
        long long target_dist = isqrt((long long)tx * tx + (long long)tz * tz);
        if (visual) {
            e->ai_state = TOY_GAME_ENEMY_NOTICE;
            e->ai_timer_ms = TOY_GAME_NOTICE_MIN_MS;
            enemy_remember_target(e, target_x, target_z);
            return;
        }
        e->ai_timer_ms -= dt_ms;
        if (e->ai_timer_ms <= 0 || target_dist <= TOY_GAME_ATTACK_RANGE) {
            e->ai_state = TOY_GAME_ENEMY_IDLE;
            e->ai_timer_ms = 0;
            e->wander_timer_ms = rand_range(g, 600, 1800);
            return;
        }
        turn_enemy_toward(e, tx, tz);
        chase_enemy(g, e, tx, tz, target_dist, target_kind);
        return;
    }
    if (e->ai_state == TOY_GAME_ENEMY_ALERT) {
        int tx, tz;
        if (visual) enemy_remember_target(e, target_x, target_z);
        tx = e->target_x - e->x;
        tz = e->target_z - e->z;
        turn_enemy_toward(e, tx, tz);
        e->ai_timer_ms -= dt_ms;
        if (e->ai_timer_ms <= 0) {
            e->ai_state = TOY_GAME_ENEMY_CHASE;
            e->ai_timer_ms = 0;
        }
        return;
    }
    if (e->ai_state == TOY_GAME_ENEMY_CHASE) {
        int tx, tz;
        long long target_dist;
        if (visual) enemy_remember_target(e, target_x, target_z);
        else e->lost_sight_ms += dt_ms;
        tx = e->last_seen_x - e->x;
        tz = e->last_seen_z - e->z;
        target_dist = isqrt((long long)tx * tx + (long long)tz * tz);
        if (!visual && target_dist <= TOY_GAME_ATTACK_RANGE) {
            enemy_enter_search(e);
            return;
        }
        turn_enemy_toward(e, tx, tz);
        chase_enemy(g, e, tx, tz, target_dist, target_kind);
        return;
    }
    if (visual) {
        enemy_remember_target(e, target_x, target_z);
        e->ai_state = TOY_GAME_ENEMY_CHASE;
        return;
    }
    e->ai_timer_ms -= dt_ms;
    if (e->ai_timer_ms <= 0) {
        e->ai_state = TOY_GAME_ENEMY_IDLE;
        e->ai_timer_ms = 0;
        e->wander_timer_ms = rand_range(g, 600, 1800);
        return;
    }
    wander_enemy(g, e, dt_ms);
}

/* 敌人间分离：按两者碰撞半径留出少量余量，避免尸群挤成一团。 */
static void separate_enemies(struct toy_game *g)
{
    int i, j;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        struct toy_game_enemy *a = &g->enemies[i];
        if (a->active != 1) continue;
        for (j = i + 1; j < TOY_GAME_MAX_ENEMIES; j++) {
            struct toy_game_enemy *b = &g->enemies[j];
            int dx, dz;
            int separation = enemy_separation_distance(a, b);
            long long dist2, dist;
            if (b->active != 1) continue;
            dx = b->x - a->x;
            dz = b->z - a->z;
            dist2 = (long long)dx * dx + (long long)dz * dz;
            if (dist2 == 0 || dist2 >= (long long)separation * separation)
                continue;
            dist = isqrt(dist2);
            if (dist > 0) {
                int push = 12;
                int ax = a->x - (int)((long long)dx * push / dist);
                int az = a->z - (int)((long long)dz * push / dist);
                int bx = b->x + (int)((long long)dx * push / dist);
                int bz = b->z + (int)((long long)dz * push / dist);
                if (!enemy_position_blocked(g, ax, az, enemy_radius(a))) {
                    a->x = ax;
                    a->z = az;
                }
                if (!enemy_position_blocked(g, bx, bz, enemy_radius(b))) {
                    b->x = bx;
                    b->z = bz;
                }
            }
        }
    }
    /* 推挤可能越过房间边界，clamp 回界内（障碍重叠留待移动碰撞纠正） */
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        struct toy_game_enemy *e = &g->enemies[i];
        int limit = g->room_limit - enemy_radius(e);
        if (e->active != 1) continue;
        if (e->x < -limit) e->x = -limit;
        if (e->x > limit) e->x = limit;
        if (e->z < -limit) e->z = -limit;
        if (e->z > limit) e->z = limit;
    }
}

/* ── 射击命中（hitscan） ───────────────────────────────────────── */

/* 线段 [P, Q] 与 xz 平面轴对齐矩形相交（Liang-Barsky，定点 1<<20） */
static int segment_hits_box(int px, int pz, int qx, int qz,
                            const struct toy_game_box *b)
{
    long long dx = qx - px;
    long long dz = qz - pz;
    long long t0 = 0, t1 = 1 << 20;
    long long t_in, t_out, tmp;

    if (dx == 0) {
        if (px < b->minx || px > b->maxx) return 0;
    } else {
        t_in = ((long long)(b->minx - px) << 20) / dx;
        t_out = ((long long)(b->maxx - px) << 20) / dx;
        if (t_in > t_out) { tmp = t_in; t_in = t_out; t_out = tmp; }
        if (t_in > t0) t0 = t_in;
        if (t_out < t1) t1 = t_out;
        if (t0 > t1) return 0;
    }
    if (dz == 0) {
        if (pz < b->minz || pz > b->maxz) return 0;
    } else {
        t_in = ((long long)(b->minz - pz) << 20) / dz;
        t_out = ((long long)(b->maxz - pz) << 20) / dz;
        if (t_in > t_out) { tmp = t_in; t_in = t_out; t_out = tmp; }
        if (t_in > t0) t0 = t_in;
        if (t_out < t1) t1 = t_out;
        if (t0 > t1) return 0;
    }
    return t0 <= t1;
}

/* 射线 [px,pz] + (sy,cy) 与盒子的首个相交距离（u 单位 1<<20 定点，
 * 沿射线方向；u×1024 = 世界距离）。起点在盒内时返回出射距离；
 * 整盒在身后或射向不经过盒子返回 0。 */
static int ray_box_entry(int px, int pz, int sy, int cy,
                         const struct toy_game_box *b, long long *out)
{
    long long t0 = 0, t1 = 1LL << 62, t_in, t_out, tmp;
    if (sy == 0) {
        if (px < b->minx || px > b->maxx) return 0;
    } else {
        t_in = ((long long)(b->minx - px) << 20) / sy;
        t_out = ((long long)(b->maxx - px) << 20) / sy;
        if (t_in > t_out) { tmp = t_in; t_in = t_out; t_out = tmp; }
        if (t_in > t0) t0 = t_in;
        if (t_out < t1) t1 = t_out;
    }
    if (cy == 0) {
        if (pz < b->minz || pz > b->maxz) return 0;
    } else {
        t_in = ((long long)(b->minz - pz) << 20) / cy;
        t_out = ((long long)(b->maxz - pz) << 20) / cy;
        if (t_in > t_out) { tmp = t_in; t_in = t_out; t_out = tmp; }
        if (t_in > t0) t0 = t_in;
        if (t_out < t1) t1 = t_out;
    }
    if (t1 <= 0 || t0 > t1) return 0;
    if (t0 <= 0) t0 = t1;   /* 起点在盒内：取出射点 */
    *out = t0;
    return 1;
}

static void normalize_dir(int *sy, int *cy)
{
    long long length = isqrt((long long)*sy * *sy + (long long)*cy * *cy);
    if (length > 0) {
        *sy = (int)((long long)*sy * 1024 / length);
        *cy = (int)((long long)*cy * 1024 / length);
    }
}

/* 单发 hitscan：最近且未被障碍遮挡的敌人一枪毙命，命中返回 1。
 * 同时输出射线终点：命中敌人 → 敌人位置；未命中 → 首个墙交点或最大射程。 */
static int fire_ray(struct toy_game *g, int sy, int cy, int damage, int range,
                    int *out_ex, int *out_ez, int *out_hit_world)
{
    int best = -1, best_t = 0, i;
    int radius_times_1024 = TOY_GAME_HIT_RADIUS * 1024;
    long long world_t = (long long)range << 20; /* 世界距离定点 */
    for (i = 0; i < g->world_count; i++) {
        long long entry_u;
        if (ray_box_entry(g->px, g->pz, sy, cy, &g->world[i], &entry_u)) {
            long long entry_w = entry_u * 1024;
            if (entry_w < world_t) world_t = entry_w;
        }
    }
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        struct toy_game_enemy *e = &g->enemies[i];
        int lx, lz, proj, cross, t, hit_x, hit_z, occluded = 0, j;
        if (e->active != 1) continue;
        lx = e->x - g->px;
        lz = e->z - g->pz;
        proj = lx * sy + lz * cy;
        if (proj <= 0) continue;
        cross = lx * cy - lz * sy;
        if (cross > radius_times_1024 || cross < -radius_times_1024) continue;
        t = proj / 1024;
        if (best >= 0 && t >= best_t) continue;
        if ((long long)t << 20 >= world_t) continue;   /* 墙先于敌人 */
        hit_x = g->px + (sy * t) / 1024;
        hit_z = g->pz + (cy * t) / 1024;
        for (j = 0; j < g->world_count; j++) {
            if (segment_hits_box(g->px, g->pz, hit_x, hit_z, &g->world[j])) {
                occluded = 1;
                break;
            }
        }
        if (!occluded) {
            best = i;
            best_t = t;
        }
    }
    if (best >= 0) {
        struct toy_game_enemy *e = &g->enemies[best];
        int inflicted = damage < e->hp ? damage : e->hp;
        *out_ex = g->px + (sy * best_t) / 1024;
        *out_ez = g->pz + (cy * best_t) / 1024;
        *out_hit_world = 0;
        e->hp -= damage;
        if (inflicted > 0) g->damage_dealt += inflicted;
        if (e->hp <= 0) {
            e->hp = 0;
            e->active = 2;
            e->dying_ms = TOY_GAME_DYING_MS;
            e->flash = 120;
            g->enemies_alive--;
            g->kills++;
            if (e->type == TOY_GAME_ENEMY_TANK)
                g->money += TOY_CONFIG_MONEY_TANK;
            else if (e->type == TOY_GAME_ENEMY_SMOKER ||
                     e->type == TOY_GAME_ENEMY_CHARGER)
                g->money += TOY_CONFIG_MONEY_SPECIAL;
            else if (e->type == TOY_GAME_ENEMY_HEAVY ||
                     e->type == TOY_GAME_ENEMY_PURSUIT_HEAVY)
                g->money += TOY_CONFIG_MONEY_HEAVY;
            else if (e->type == TOY_GAME_ENEMY_PURSUIT_FAST)
                g->money += TOY_CONFIG_MONEY_FAST;
            else
                g->money += TOY_CONFIG_MONEY_COMMON;
            if (toy_game_enemy_info(e->type)->ability !=
                TOY_GAME_ENEMY_ABILITY_NONE)
                g->special_kills++;
            if (g->player_pull_enemy_index == best)
                release_player_special(g);
            push_event(g, TOY_GAME_EV_KILL);
        } else {
            e->hurt = 150;
        }
        return 1;
    }
    {
        long long dist_w = world_t >> 20;
        *out_ex = g->px + (int)(sy * dist_w / 1024);
        *out_ez = g->pz + (int)(cy * dist_w / 1024);
        *out_hit_world = world_t < ((long long)range << 20);
    }
    return 0;
}

int toy_game_fire(struct toy_game *g, int sy, int cy)
{
    struct toy_game_slot *s = &g->slots[g->current_slot];
    const struct toy_game_weapon_info *w = toy_game_weapon_info(s->weapon);
    int pellet, hit = 0;
    int spread;
    if (g->state != TOY_GAME_PLAYING || g->reloading ||
        (TOY_CONFIG_BLOCK_FIRE_DURING_SWITCH &&
         g->weapon_switch_timer_ms > 0)) return 0;
    g->fire_cooldown_ms = w->cooldown_ms;
    g->muzzle_flash_ms = TOY_GAME_MUZZLE_FLASH_MS;
    if (s->mag <= 0) {
        push_event(g, TOY_GAME_EV_DRY_FIRE);
        return 0;
    }
    s->mag--;
    g->weapon_spread_heat += TOY_CONFIG_SPREAD_SHOT_STEP;
    if (g->weapon_spread_heat > TOY_CONFIG_SPREAD_HEAT_MAX)
        g->weapon_spread_heat = TOY_CONFIG_SPREAD_HEAT_MAX;
    push_event(g, TOY_GAME_EV_SHOOT);
    emit_enemy_noise(g, g->px, g->pz, TOY_GAME_GUNSHOT_RANGE);
    if (s->mag == 0) {
        g->reloading = 1;
        g->reload_timer_ms = w->reload_ms;
        push_event(g, TOY_GAME_EV_RELOAD_START);
    }
    /* 每颗弹丸在 [-spread, +spread] 内随机偏转（1024 定点）：霰弹枪
     * 近距离密集、远距离发散；弹道记录供宿主渲染 tracer 与命中特效。 */
    spread = toy_game_current_spread(g);
    g->fire_seq++;
    g->ray_count = w->pellets;
    for (pellet = 0; pellet < w->pellets; pellet++) {
        int off_x, off_y;
        int ray_sy, ray_cy;
        /* 在准心周围取圆形散布，而不是只在水平线上散布。 */
        do {
            off_x = rand_range(g, -spread, spread);
            off_y = rand_range(g, -spread, spread);
        } while (off_x * off_x + off_y * off_y >
                 spread * spread);
        ray_sy = (sy * 1024 - cy * off_x) / 1024;
        ray_cy = (cy * 1024 + sy * off_x) / 1024;
        int ex, ez, hit_world, killed;
        normalize_dir(&ray_sy, &ray_cy);   /* 旋转后长度略偏，归一化保证判定一致 */
        killed = fire_ray(g, ray_sy, ray_cy, w->damage, w->range,
                          &ex, &ez, &hit_world);
        if (killed) hit = 1;
        g->rays[pellet].sy = ray_sy;
        g->rays[pellet].cy = ray_cy;
        g->rays[pellet].vy = off_y;
        g->rays[pellet].ex = ex;
        g->rays[pellet].ez = ez;
        g->rays[pellet].hit_enemy = killed;
        g->rays[pellet].hit_world = hit_world;
    }
    return hit;
}

/* 切枪：只允许切到有武器的槽位；换弹被打断 */
int toy_game_switch_weapon(struct toy_game *g, int slot)
{
    if (slot < 0 || slot >= TOY_GAME_WEAPON_SLOTS) return 0;
    if (slot == g->current_slot || g->slots[slot].weapon < 0) return 0;
    g->current_slot = slot;
    g->weapon_switch_timer_ms = TOY_CONFIG_WEAPON_SWITCH_MS;
    g->reloading = 0;
    g->reload_timer_ms = 0;
    push_event(g, TOY_GAME_EV_WEAPON_SWITCH);
    return 1;
}

static int toy_game_start_empty_reload(
    struct toy_game *g, struct toy_game_slot *s,
    const struct toy_game_weapon_info *w)
{
    if (!g || !s || !w || g->reloading || s->mag > 0 ||
        (s->reserve != TOY_GAME_AMMO_INFINITE && s->reserve <= 0))
        return 0;
    g->reloading = 1;
    g->reload_timer_ms = w->reload_ms;
    push_event(g, TOY_GAME_EV_RELOAD_START);
    return 1;
}

/* 拾取主武器（SMG/霰弹枪）。同武器 = 补满弹匣与备弹；新武器替换槽 0 并自动切出。 */
int toy_game_equip_weapon(struct toy_game *g, int weapon)
{
    const struct toy_game_weapon_info *w;
    int slot;
    if (weapon < 0 || weapon >= TOY_GAME_WEAPON_COUNT) return -1;
    w = toy_game_weapon_info(weapon);
    slot = w->slot;
    if (slot < 0 || slot >= TOY_GAME_WEAPON_SLOTS) return -1;
    if (g->slots[slot].weapon == weapon) {
        g->slots[slot].mag = w->mag_size;
        g->slots[slot].reserve = w->reserve_max;
        return 0;
    }
    g->slots[slot].weapon = weapon;
    g->slots[slot].mag = w->mag_size;
    g->slots[slot].reserve = w->reserve_max;
    toy_game_switch_weapon(g, slot);
    return 1;
}

int toy_game_weapon_price(int weapon)
{
    switch (weapon) {
    case TOY_GAME_WEAPON_SMG: return TOY_GAME_PRICE_SMG;
    case TOY_GAME_WEAPON_SHOTGUN: return TOY_GAME_PRICE_SHOTGUN;
    case TOY_GAME_WEAPON_AK: return TOY_GAME_PRICE_AK;
    case TOY_GAME_WEAPON_AWP: return TOY_GAME_PRICE_AWP;
    default: return 0;
    }
}

int toy_game_weapon_combat_dps(int weapon)
{
    const struct toy_game_weapon_info *w;
    long long numerator;
    long long denominator;
    if (!toy_game_weapon_is_valid(weapon)) return 0;
    w = toy_game_weapon_info(weapon);
    numerator = (long long)w->damage * w->pellets * w->mag_size * 1000;
    denominator = (long long)w->mag_size * w->cooldown_ms + w->reload_ms;
    if (denominator <= 0) return 0;
    return (int)(numerator / denominator);
}

int toy_game_weapon_spread_penalty(int weapon)
{
    int spread;
    if (!toy_game_weapon_is_valid(weapon)) return 0;
    spread = toy_game_weapon_info(weapon)->spread;
    if (spread <= TOY_CONFIG_COMBAT_SPREAD_LOW_MAX)
        return TOY_CONFIG_COMBAT_SPREAD_PENALTY_LOW;
    if (spread <= TOY_CONFIG_COMBAT_SPREAD_MEDIUM_MAX)
        return TOY_CONFIG_COMBAT_SPREAD_PENALTY_MEDIUM;
    if (spread <= TOY_CONFIG_COMBAT_SPREAD_HIGH_MAX)
        return TOY_CONFIG_COMBAT_SPREAD_PENALTY_HIGH;
    return TOY_CONFIG_COMBAT_SPREAD_PENALTY_EXTREME;
}

int toy_game_weapon_combat_power(int weapon)
{
    const struct toy_game_weapon_info *w;
    int power;
    if (!toy_game_weapon_is_valid(weapon)) return 0;
    w = toy_game_weapon_info(weapon);
    power = toy_game_weapon_combat_dps(weapon) /
            TOY_CONFIG_COMBAT_DPS_SCALE;
    power -= toy_game_weapon_spread_penalty(weapon);
    power += w->power_bias;
    if (power < TOY_CONFIG_COMBAT_WEAPON_POWER_MIN)
        power = TOY_CONFIG_COMBAT_WEAPON_POWER_MIN;
    if (power > TOY_CONFIG_COMBAT_WEAPON_POWER_MAX)
        power = TOY_CONFIG_COMBAT_WEAPON_POWER_MAX;
    return power;
}

int toy_game_ai_level_combat_power(int class_id)
{
    switch (class_id) {
    case TOY_GAME_AI_LEVEL_1: return TOY_CONFIG_COMBAT_AI_LEVEL_1_POINTS;
    case TOY_GAME_AI_LEVEL_2: return TOY_CONFIG_COMBAT_AI_LEVEL_2_POINTS;
    case TOY_GAME_AI_LEVEL_3: return TOY_CONFIG_COMBAT_AI_LEVEL_3_POINTS;
    default: return 0;
    }
}

int toy_game_actor_combat_power(const struct toy_game_actor *actor)
{
    int weapon;
    if (!actor) return 0;
    weapon = actor->current_slot >= 0 &&
             actor->current_slot < TOY_GAME_WEAPON_SLOTS ?
             actor->slots[actor->current_slot].weapon : -1;
    return toy_game_ai_level_combat_power(actor->class_id) +
           toy_game_weapon_combat_power(weapon);
}

int toy_game_weapon_unlocked(const struct toy_game *g, int weapon)
{
    if (!g || weapon < 0 || weapon >= TOY_GAME_WEAPON_COUNT) return 0;
    return (g->unlocked_weapons & (1u << weapon)) != 0;
}

int toy_game_buy_weapon(struct toy_game *g, int weapon)
{
    int price;
    if (!g || weapon <= TOY_GAME_WEAPON_PISTOL ||
        weapon >= TOY_GAME_WEAPON_COUNT) return -1;
    if (toy_game_weapon_unlocked(g, weapon))
        return toy_game_equip_weapon(g, weapon);
    price = toy_game_weapon_price(weapon);
    if (g->money < price) return 0;
    g->money -= price;
    g->unlocked_weapons |= 1u << weapon;
    return toy_game_equip_weapon(g, weapon) ? 2 : 1;
}

/* 弹药盒：补满已拥有武器的备弹（手枪无限备弹跳过），有变化返回 1 */
int toy_game_refill_ammo(struct toy_game *g)
{
    int i, changed = 0;
    for (i = 0; i < TOY_GAME_WEAPON_SLOTS; i++) {
        struct toy_game_slot *s = &g->slots[i];
        const struct toy_game_weapon_info *w;
        if (s->weapon < 0) continue;
        w = toy_game_weapon_info(s->weapon);
        if (w->reserve_max == TOY_GAME_AMMO_INFINITE) continue;
        if (s->reserve < w->reserve_max) {
            s->reserve = w->reserve_max;
            changed = 1;
        }
    }
    return changed;
}

/* ── 主更新 ────────────────────────────────────────────────────── */

void toy_game_set_player_moving(struct toy_game *g, int moving)
{
    if (g) g->moving = moving != 0;
}

int toy_game_current_spread(const struct toy_game *g)
{
    const struct toy_game_slot *slot;
    const struct toy_game_weapon_info *weapon;
    int spread;
    if (!g || g->current_slot < 0 ||
        g->current_slot >= TOY_GAME_WEAPON_SLOTS) return 0;
    slot = &g->slots[g->current_slot];
    weapon = toy_game_weapon_info_or_null(slot->weapon);
    if (!weapon) return 0;
    spread = weapon->spread * (g->moving ? TOY_CONFIG_SPREAD_MOVE_PERCENT :
                               TOY_CONFIG_SPREAD_STILL_PERCENT) / 100;
    if (g->ai_spread_percent > 0)
        spread = spread * g->ai_spread_percent / 100;
    spread += g->weapon_spread_heat;
    return spread < 1 ? 1 : spread;
}

void toy_game_update_weapon_held(struct toy_game *g,
                                 const unsigned char *keys_pressed,
                                 int fire_pressed, int fire_held,
                                 int sy, int cy, int dt_ms)
{
    struct toy_game_slot *s;
    const struct toy_game_weapon_info *w;
    if (g->state != TOY_GAME_PLAYING || g->player_down) return;
    if (g->weapon_spread_heat > 0) {
        int recover = TOY_CONFIG_SPREAD_RECOVER_PER_SEC * dt_ms / 1000;
        if (recover < 1) recover = 1;
        g->weapon_spread_heat -= recover;
        if (g->weapon_spread_heat < 0) g->weapon_spread_heat = 0;
    }

    if (g->weapon_switch_timer_ms > 0) {
        g->weapon_switch_timer_ms -= dt_ms;
        if (g->weapon_switch_timer_ms < 0)
            g->weapon_switch_timer_ms = 0;
    }

    /* 切枪键（1/2）：先于换弹与射击处理，切换即打断换弹 */
    if (keys_pressed) {
        if (keys_pressed[TOY_GAME_KEY_SLOT_1]) toy_game_switch_weapon(g, 0);
        if (keys_pressed[TOY_GAME_KEY_SLOT_2]) toy_game_switch_weapon(g, 1);
    }
    s = &g->slots[g->current_slot];
    w = toy_game_weapon_info(s->weapon);

    /* An empty weapon remains eligible for automatic reload after switching
     * back to it.  Switching intentionally cancels the old timer, so this
     * check must happen after the new slot has been selected. */
    toy_game_start_empty_reload(g, s, w);

    if (g->fire_cooldown_ms > 0) {
        g->fire_cooldown_ms -= dt_ms;
        if (g->fire_cooldown_ms < 0) g->fire_cooldown_ms = 0;
    }
    if (g->muzzle_flash_ms > 0) {
        g->muzzle_flash_ms -= dt_ms;
        if (g->muzzle_flash_ms < 0) g->muzzle_flash_ms = 0;
    }
    if (g->damage_flash_ms > 0) {
        g->damage_flash_ms -= dt_ms;
        if (g->damage_flash_ms < 0) g->damage_flash_ms = 0;
    }

    /* 换弹 */
    if (g->reloading) {
        g->reload_timer_ms -= dt_ms;
        if (g->reload_timer_ms <= 0) {
            int used = w->mag_size - s->mag;
            if (s->reserve != TOY_GAME_AMMO_INFINITE &&
                used > s->reserve) used = s->reserve;
            s->mag += used;
            if (s->reserve != TOY_GAME_AMMO_INFINITE)
                s->reserve -= used;
            g->reloading = 0;
            push_event(g, TOY_GAME_EV_RELOAD_DONE);
        }
    } else if (keys_pressed && keys_pressed[TOY_GAME_KEY_RELOAD] &&
               s->mag < w->mag_size &&
               (s->reserve > 0 || s->reserve == TOY_GAME_AMMO_INFINITE)) {
        g->reloading = 1;
        g->reload_timer_ms = w->reload_ms;
        push_event(g, TOY_GAME_EV_RELOAD_START);
    }

    /* 射击：半自动武器走边沿；全自动武器（SMG）按住连发 */
    if (!g->reloading && g->fire_cooldown_ms <= 0) {
        if (fire_pressed) toy_game_fire(g, sy, cy);
        else if (fire_held && w->full_auto) toy_game_fire(g, sy, cy);
    }

}

/* AI 队友只负责选择目标；实际扣弹、换弹、射线和枪声仍走玩家的
 * toy_game_update_weapon_held/toy_game_fire。临时切换操作者状态后恢复玩家
 * 状态，这样两名操作者共享同一套武器规则，同时不会覆盖玩家 tracer。 */
void toy_game_update_ai_teammate(struct toy_game *g, int dt_ms)
{
    const struct toy_game_ai_info *ai_info;
    struct toy_game_actor *actor;
    struct toy_game_slot player_slots[TOY_GAME_WEAPON_SLOTS];
    struct toy_game_ray player_rays[TOY_GAME_MAX_RAYS];
    int player_px, player_pz, player_current_slot;
    int player_reloading, player_reload_timer_ms, player_fire_cooldown_ms;
    int player_spread_heat, player_moving;
    int player_weapon_switch_timer_ms;
    int player_muzzle_flash_ms, player_ray_count;
    int player_kills, player_special_kills, player_damage_dealt;
    unsigned int player_fire_seq;
    int target = -1, best_dist = 0, i;
    int sy = 0, cy = 1024;
    int player_down;
    int ai_idle = 1;
    int fired = 0;
    int keep_animation = 0;
    int alert_range;
    int actor_weapon;
    int ai_can_fire;
    int facing_error = 180;
    sync_ai_actor_from_legacy(g);
    actor = &g->actors[g->ai_context_actor_index];
    ai_info = actor->class_id >= 0 && actor->class_id < TOY_GAME_AI_CLASS_COUNT ?
              &ai_table[actor->class_id] : &ai_table[TOY_GAME_AI_LEVEL_2];
    if (actor->ai_shove_cooldown_ms > 0) {
        actor->ai_shove_cooldown_ms -= dt_ms;
        if (actor->ai_shove_cooldown_ms < 0) actor->ai_shove_cooldown_ms = 0;
    }
    actor_weapon = actor->current_slot >= 0 &&
                   actor->current_slot < TOY_GAME_WEAPON_SLOTS ?
                   actor->slots[actor->current_slot].weapon : -1;
    alert_range = toy_game_weapon_info(actor_weapon)->alert_range;
    if (actor->hit_test_dummy) {
        /* The hit-test actor is a stationary melee frontline target: keep
         * its position and weapon logic frozen, but turn toward the nearest
         * living enemy so the front-facing hit/shove pose remains readable. */
        {
            int nearest_dx = 0, nearest_dz = 0;
            long long nearest_dist2 = 0;
            int found_enemy = 0;

            for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
                struct toy_game_enemy *enemy = &g->enemies[i];
                int dx, dz;
                long long dist2;
                if (enemy->active != 1 || enemy->hp <= 0) continue;
                dx = enemy->x - actor->x;
                dz = enemy->z - actor->z;
                dist2 = (long long)dx * dx + (long long)dz * dz;
                if (!found_enemy || dist2 < nearest_dist2) {
                    nearest_dx = dx;
                    nearest_dz = dz;
                    nearest_dist2 = dist2;
                    found_enemy = 1;
                }
            }
            if (found_enemy && (nearest_dx != 0 || nearest_dz != 0)) {
                int distance = isqrt(nearest_dist2);
                if (distance > 0) {
                    actor->sy = (int)((long long)nearest_dx * 1024 / distance);
                    actor->cy = (int)((long long)nearest_dz * 1024 / distance);
                }
            }
        }
        toy_game_actor_update_animation(actor, dt_ms);
        load_ai_actor_to_legacy(g, actor);
        return;
    }
    if (actor->airborne_ms > 0) {
        /* Knockback used to erase HIT immediately on the next AI tick. */
        if (actor->animation.id != TOY_GAME_ANIM_HIT &&
            actor->animation.id != TOY_GAME_ANIM_SHOVE &&
            actor->animation.id != TOY_GAME_ANIM_DEATH)
            toy_game_actor_set_animation(actor, TOY_GAME_ANIM_NONE);
        toy_game_actor_update_animation(actor, dt_ms);
        update_motion_values(g, &actor->x, &actor->z, &actor->airborne_ms,
                             &actor->airborne_y, &actor->vertical_velocity,
                             &actor->knockback_x, &actor->knockback_z,
                             TOY_GAME_PLAYER_RADIUS, dt_ms);
        load_ai_actor_to_legacy(g, actor);
        return; /* 被击飞时中断自己的行为，落地后恢复。 */
    }
    if (!g->ai_active || g->ai_down || g->state != TOY_GAME_PLAYING) {
        toy_game_actor_update_animation(actor, dt_ms);
        sync_ai_actor_from_legacy(g);
        return;
    }

    /* 自由状态下向部署点回位；回位只占用移动，不影响索敌和开火。 */
    {
        int home_dx = actor->deployment_x - actor->x;
        int home_dz = actor->deployment_z - actor->z;
        long long home_dist = isqrt((long long)home_dx * home_dx +
                                    (long long)home_dz * home_dz);
        if (home_dist > TOY_GAME_AI_DEPLOY_RADIUS) {
            ai_idle = 0;
            actor_path_toward(g, actor, actor->deployment_x,
                              actor->deployment_z, ai_info->move_speed);
            g->ai_x = actor->x; g->ai_z = actor->z;
        } else {
            actor->nav_active = 0;
        }
    }

    /* 警戒半径由当前武器决定，且只选择无遮挡目标。 */
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        struct toy_game_enemy *e = &g->enemies[i];
        int dx, dz, dist, j, blocked = 0;
        long long d2;
        if (e->active != 1) continue;
        dx = e->x - g->ai_x;
        dz = e->z - g->ai_z;
        d2 = (long long)dx * dx + (long long)dz * dz;
        if (d2 > (long long)alert_range * alert_range)
            continue;
        dist = (int)isqrt(d2);
        if (dist <= 0) continue;
        for (j = 0; j < g->world_count; j++)
            if (segment_hits_box(g->ai_x, g->ai_z, e->x, e->z,
                                 &g->world[j])) { blocked = 1; break; }
        if (blocked) continue;
        if (target < 0 || dist < best_dist) {
            target = i; best_dist = dist;
            sy = (int)((long long)dx * 1024 / dist);
            cy = (int)((long long)dz * 1024 / dist);
        }
    }

    if (target >= 0) {
        facing_error = ai_turn_toward(actor, sy, cy,
                                      ai_info->turn_speed_degree, dt_ms);
        sy = actor->sy;
        cy = actor->cy;
    }
    ai_can_fire = actor->fire_enabled && target >= 0 && facing_error <= 6;
    if (actor_weapon == TOY_GAME_WEAPON_AWP) {
        ai_can_fire = 0;
        if (target < 0) {
            actor->awp_aim_ms = 0;
            actor->awp_post_fire_ms = 0;
            actor->awp_aim_target = 0;
        } else if (actor->awp_post_fire_ms > 0) {
            actor->awp_post_fire_ms -= dt_ms;
            if (actor->awp_post_fire_ms <= 0) {
                actor->awp_post_fire_ms = 0;
                actor->awp_aim_ms = TOY_CONFIG_AWP_AI_AIM_MS;
                actor->awp_aim_target = target + 1;
            }
        } else {
            if (actor->awp_aim_target != target + 1) {
                actor->awp_aim_target = target + 1;
                actor->awp_aim_ms = TOY_CONFIG_AWP_AI_AIM_MS;
            } else if (actor->awp_aim_ms > 0) {
                actor->awp_aim_ms -= dt_ms;
                if (actor->awp_aim_ms < 0) actor->awp_aim_ms = 0;
            }
            if (actor->awp_aim_ms <= 0) ai_can_fire = actor->fire_enabled;
        }
    } else {
        actor->awp_aim_ms = 0;
        actor->awp_post_fire_ms = 0;
        actor->awp_aim_target = 0;
    }

    memcpy(player_slots, g->slots, sizeof(player_slots));
    memcpy(player_rays, g->rays, sizeof(player_rays));
    player_px = g->px; player_pz = g->pz;
    player_current_slot = g->current_slot;
    player_reloading = g->reloading;
    player_reload_timer_ms = g->reload_timer_ms;
    player_weapon_switch_timer_ms = g->weapon_switch_timer_ms;
    player_fire_cooldown_ms = g->fire_cooldown_ms;
    player_spread_heat = g->weapon_spread_heat;
    player_moving = g->moving;
    player_muzzle_flash_ms = g->muzzle_flash_ms;
    player_kills = g->kills;
    player_special_kills = g->special_kills;
    player_damage_dealt = g->damage_dealt;
    player_ray_count = g->ray_count;
    player_fire_seq = g->fire_seq;
    player_down = g->player_down;

    memcpy(g->slots, g->ai_slots, sizeof(g->slots));
    /* AI has infinite reserve ammo, but its magazine and reload timer are
     * real state so it must pause between magazines just like a player. */
    g->slots[0].reserve = TOY_GAME_AMMO_INFINITE;
    g->px = g->ai_x; g->pz = g->ai_z;
    g->current_slot = g->ai_current_slot;
    g->reloading = g->ai_reloading;
    g->reload_timer_ms = g->ai_reload_timer_ms;
    g->fire_cooldown_ms = g->ai_fire_cooldown_ms;
    g->weapon_spread_heat = actor->weapon_spread_heat;
    g->moving = !ai_idle;
    g->muzzle_flash_ms = g->ai_muzzle_flash_ms;
    g->kills = actor->kills;
    g->special_kills = actor->special_kills;
    g->damage_dealt = actor->damage_dealt;
    g->player_down = 0;
    g->ai_spread_percent = ai_info->spread_percent;
    toy_game_update_weapon_held(g, NULL,
                                ai_can_fire, ai_can_fire,
                                sy, cy, dt_ms);
    if (g->fire_seq != player_fire_seq && ai_info->fire_interval_percent != 100)
        g->fire_cooldown_ms = g->fire_cooldown_ms *
                              ai_info->fire_interval_percent / 100;
    /* Only reserve ammo is infinite; the current magazine is preserved. */
    g->slots[0].reserve = TOY_GAME_AMMO_INFINITE;
    memcpy(g->ai_slots, g->slots, sizeof(g->ai_slots));
    g->ai_current_slot = g->current_slot;
    g->ai_reloading = g->reloading;
    g->ai_reload_timer_ms = g->reload_timer_ms;
    g->ai_fire_cooldown_ms = g->fire_cooldown_ms;
    actor->weapon_spread_heat = g->weapon_spread_heat;
    actor->moving = g->moving;
    g->ai_muzzle_flash_ms = g->muzzle_flash_ms;
    actor->kills = g->kills;
    actor->special_kills = g->special_kills;
    actor->damage_dealt = g->damage_dealt;
    g->ai_sy = target >= 0 ? sy : g->ai_sy;
    g->ai_cy = target >= 0 ? cy : g->ai_cy;
    if (g->fire_seq != player_fire_seq) {
        fired = 1;
        if (actor_weapon == TOY_GAME_WEAPON_AWP) {
            actor->awp_post_fire_ms = TOY_CONFIG_AWP_AI_POST_FIRE_MS;
            actor->awp_aim_ms = 0;
        }
        g->ai_fire_seq++;
        g->ai_ray_count = g->ray_count;
        memcpy(g->ai_rays, g->rays, sizeof(g->ai_rays));
    }

    keep_animation =
        (actor->animation.id == TOY_GAME_ANIM_FIRE &&
         actor->animation.time_ms <
         toy_game_animation_info(TOY_GAME_ANIM_FIRE)->duration_ms) ||
        (actor->animation.id == TOY_GAME_ANIM_HIT &&
         actor->animation.time_ms <
         toy_game_animation_info(TOY_GAME_ANIM_HIT)->duration_ms) ||
        (actor->animation.id == TOY_GAME_ANIM_REVIVE &&
         actor->animation.time_ms <
         toy_game_animation_info(TOY_GAME_ANIM_REVIVE)->duration_ms) ||
        (actor->animation.id == TOY_GAME_ANIM_SHOVE &&
         actor->animation.time_ms <
         toy_game_animation_info(TOY_GAME_ANIM_SHOVE)->duration_ms) ||
        (actor->animation.id == TOY_GAME_ANIM_DEATH &&
         actor->state == TOY_GAME_ACTOR_DOWNED);
    if (g->reloading)
        toy_game_actor_set_animation(actor, TOY_GAME_ANIM_RELOAD);
    else if (fired) toy_game_actor_set_animation(actor, TOY_GAME_ANIM_FIRE);
    else if (!keep_animation) {
        if (target >= 0) ai_idle = 0;
        toy_game_actor_set_animation(actor, ai_idle ? TOY_GAME_ANIM_IDLE :
                                     TOY_GAME_ANIM_MOVE);
    }
    toy_game_actor_update_animation(actor, dt_ms);

    memcpy(g->slots, player_slots, sizeof(g->slots));
    memcpy(g->rays, player_rays, sizeof(g->rays));
    g->px = player_px; g->pz = player_pz;
    g->current_slot = player_current_slot;
    g->reloading = player_reloading;
    g->reload_timer_ms = player_reload_timer_ms;
    g->weapon_switch_timer_ms = player_weapon_switch_timer_ms;
    g->fire_cooldown_ms = player_fire_cooldown_ms;
    g->weapon_spread_heat = player_spread_heat;
    g->moving = player_moving;
    g->muzzle_flash_ms = player_muzzle_flash_ms;
    g->kills = player_kills;
    g->special_kills = player_special_kills;
    g->damage_dealt = player_damage_dealt;
    g->ray_count = player_ray_count;
    g->fire_seq = player_fire_seq;
    g->player_down = player_down;
    g->ai_spread_percent = 0;
    sync_ai_actor_from_legacy(g);
}

void toy_game_update_ai_teammates(struct toy_game *g, int dt_ms)
{
    static const int demo_animation_ids[] = {
        TOY_GAME_ANIM_IDLE, TOY_GAME_ANIM_MOVE, TOY_GAME_ANIM_FIRE,
        TOY_GAME_ANIM_RELOAD, TOY_GAME_ANIM_HIT, TOY_GAME_ANIM_DEATH,
        TOY_GAME_ANIM_REVIVE, TOY_GAME_ANIM_SHOVE
    };
    int i, old_context = g->ai_context_actor_index;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
        if (!g->actors[i].active || g->actors[i].kind != TOY_GAME_ACTOR_AI)
            continue;
        if (g->actors[i].animation_demo) {
            struct toy_game_actor *demo = &g->actors[i];
            const struct toy_game_animation_info *info =
                toy_game_animation_info(demo->animation.id);
            const int demo_duration_ms = 2000;
            int demo_index = 0;
            int j;
            for (j = 0; j < (int)(sizeof(demo_animation_ids) /
                                  sizeof(demo_animation_ids[0])); j++)
                if (demo_animation_ids[j] == demo->animation.id)
                    demo_index = j;
            demo->animation_demo_elapsed_ms += dt_ms;
            toy_game_actor_update_animation(demo, dt_ms);
            /* Repeat short non-looping clips inside the two-second window so
             * HIT/FIRE/DEATH remain observable instead of freezing early. */
            if (demo->animation.id != TOY_GAME_ANIM_RELOAD &&
                info->duration_ms > 0 &&
                demo->animation.time_ms >= info->duration_ms)
                demo->animation.time_ms %= info->duration_ms;
            if (demo->animation_demo_elapsed_ms >= demo_duration_ms) {
                demo_index = (demo_index + 1) %
                    (int)(sizeof(demo_animation_ids) /
                          sizeof(demo_animation_ids[0]));
                toy_game_actor_set_animation(
                    demo, demo_animation_ids[demo_index]);
                demo->animation_demo_elapsed_ms -= demo_duration_ms;
            }
            continue;
        }
        g->ai_context_actor_index = i;
        load_ai_actor_to_legacy(g, &g->actors[i]);
        toy_game_update_ai_teammate(g, dt_ms);
    }
    g->ai_context_actor_index = old_context;
    if (g->actors[0].active)
        load_ai_actor_to_legacy(g, &g->actors[0]);
}

void toy_game_update_held(struct toy_game *g,
                          const unsigned char *keys_pressed,
                          int fire_pressed, int fire_held,
                          int sy, int cy, int dt_ms)
{
    int i;
    unsigned int old_fire_seq;
    int old_reloading;
    if (g->state != TOY_GAME_PLAYING) return;
    old_fire_seq = g->fire_seq;
    old_reloading = g->reloading;
    toy_game_update_weapon_held(g, keys_pressed, fire_pressed, fire_held,
                                sy, cy, dt_ms);
    if (g->reloading && !old_reloading)
        toy_game_animation_set(&g->animation, TOY_GAME_ANIM_RELOAD);
    else if (g->fire_seq != old_fire_seq)
        toy_game_animation_set(&g->animation, TOY_GAME_ANIM_FIRE);
    else if (g->animation.id != TOY_GAME_ANIM_RELOAD || !g->reloading)
        if (g->animation.id != TOY_GAME_ANIM_FIRE ||
            g->animation.time_ms >=
                toy_game_animation_info(TOY_GAME_ANIM_FIRE)->duration_ms)
                if (g->animation.id != TOY_GAME_ANIM_HIT ||
                    g->animation.time_ms >=
                    toy_game_animation_info(TOY_GAME_ANIM_HIT)->duration_ms)
                if (g->animation.id != TOY_GAME_ANIM_DEATH &&
                    g->animation.id != TOY_GAME_ANIM_REVIVE &&
                    (g->animation.id != TOY_GAME_ANIM_SHOVE ||
                     g->animation.time_ms >=
                     toy_game_animation_info(TOY_GAME_ANIM_SHOVE)->duration_ms))
                    toy_game_animation_set(&g->animation, TOY_GAME_ANIM_NONE);
    toy_game_animation_update(&g->animation, dt_ms);
    toy_game_update_ai_teammates(g, dt_ms);
    /* 敌人计时器与移动/攻击/倒地 */
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        struct toy_game_enemy *e = &g->enemies[i];
        if (e->active == 1) {
            if (e->airborne_ms > 0) {
                update_enemy_airborne(g, e, dt_ms);
                continue;       /* 空中状态：落地前不执行自身 AI */
            }
            if (e->bite_cooldown_ms > 0) {
                e->bite_cooldown_ms -= dt_ms;
                if (e->bite_cooldown_ms < 0) e->bite_cooldown_ms = 0;
            }
            if (e->flash > 0) {
                e->flash -= dt_ms;
                if (e->flash < 0) e->flash = 0;
            }
            if (e->hurt > 0) {
                e->hurt -= dt_ms;
                if (e->hurt < 0) e->hurt = 0;
            }
            if (e->shove_stun_ms > 0) {
                e->shove_stun_ms -= dt_ms;
                if (e->shove_stun_ms < 0) e->shove_stun_ms = 0;
                continue;       /* 僵直中：不移动、不攻击、不换目标 */
            }
            update_enemy_ai(g, e, dt_ms);
        } else if (e->active == 2) {
            e->dying_ms -= dt_ms;
            if (e->dying_ms <= 0) e->active = 0;
        }
    }
    update_player_special_motion(g, dt_ms);
    separate_enemies(g);
    update_base_core(g, dt_ms);
    if (g->campaign_mode) update_campaign(g, dt_ms);
    else update_waves(g, dt_ms);
}

/* 半自动兼容入口：无按住连发（历史测试/宿主行为不变） */
void toy_game_update(struct toy_game *g, const unsigned char *keys_pressed,
                     int fire_pressed, int sy, int cy, int dt_ms)
{
    toy_game_update_held(g, keys_pressed, fire_pressed, 0, sy, cy, dt_ms);
}
