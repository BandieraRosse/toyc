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

/* 固定散射：每颗弹丸在 [-spread, +spread] 内随机偏转（1024 定点）。
 * 手枪 ±12（≈±0.7°，几乎精准）；SMG ±90（≈±5°，连发略散）；
 * 霰弹枪 ±230（≈±12.7°，近距离密集、远距离发散）。 */
static const struct toy_game_weapon_info weapon_table[TOY_GAME_WEAPON_COUNT] = {
    { 30, TOY_GAME_AMMO_INFINITE, 200, 1500, 0, 1, 12, 1 },
    { 50, 650, 100, 2000, 1, 1, 90, 0 },
    { 8, 64, 600, 2500, 0, 10, 230, 0 },
};

static const char *weapon_names[TOY_GAME_WEAPON_COUNT] = {
    "PISTOL", "SMG", "SHOTGUN"
};

static const struct toy_game_enemy_info enemy_table[TOY_GAME_ENEMY_TYPE_COUNT] = {
    /* max hp, speed range, bite damage, model, base color */
    { 1, 38, 56, 2, 0, 0x4A5D3A },
    { 1, 66, 82, 2, 1, 0x6A8A42 },
    { 4, 24, 34, 4, 2, 0x624A3A }
};

const struct toy_game_weapon_info *toy_game_weapon_info(int weapon)
{
    if (weapon < 0 || weapon >= TOY_GAME_WEAPON_COUNT)
        return &weapon_table[TOY_GAME_WEAPON_PISTOL];
    return &weapon_table[weapon];
}

const char *toy_game_weapon_name(int weapon)
{
    if (weapon < 0 || weapon >= TOY_GAME_WEAPON_COUNT) return "UNKNOWN";
    return weapon_names[weapon];
}

const struct toy_game_enemy_info *toy_game_enemy_info(int type)
{
    if (type < 0 || type >= TOY_GAME_ENEMY_TYPE_COUNT)
        return &enemy_table[TOY_GAME_ENEMY_COMMON];
    return &enemy_table[type];
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

static int segment_hits_box(int px, int pz, int qx, int qz,
                            const struct toy_game_box *b);

/* ── 事件队列 ──────────────────────────────────────────────────── */

static void push_event(struct toy_game *g, unsigned char event)
{
    if (g->event_count < TOY_GAME_MAX_EVENTS)
        g->events[g->event_count++] = event;
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

static int wave_quota(int wave)
{
    int quota = 5 + (wave - 1) * 2;
    if (quota > TOY_GAME_MAX_ENEMIES) quota = TOY_GAME_MAX_ENEMIES;
    return quota;
}

void toy_game_init(struct toy_game *g, uint64_t seed)
{
    const struct toy_game_weapon_info *w;
    memset(g, 0, sizeof(struct toy_game));
    g->rng = seed ? seed : 0x9E3779B97F4A7C15ULL;
    g->hp = 100;
    g->state = TOY_GAME_PLAYING;
    /* 槽 0 主武器为空；槽 1 默认为满弹匣手枪，开局出枪。 */
    g->slots[0].weapon = -1;
    g->slots[1].weapon = TOY_GAME_WEAPON_PISTOL;
    w = toy_game_weapon_info(TOY_GAME_WEAPON_PISTOL);
    g->slots[1].mag = w->mag_size;
    g->slots[1].reserve = w->reserve_max;
    g->current_slot = 1;
    g->wave = 1;
    g->to_spawn = wave_quota(1);
    g->spawn_timer_ms = TOY_GAME_WAVE_FIRST_DELAY_MS;
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

void toy_game_set_ai_teammate(struct toy_game *g, int active, int x, int z,
                              const char *name)
{
    const struct toy_game_weapon_info *w;
    memset(g->ai_slots, 0, sizeof(g->ai_slots));
    g->ai_active = active != 0;
    g->ai_actor_id = 1;
    g->ai_x = x; g->ai_z = z;
    g->ai_sy = 0; g->ai_cy = 1024;
    g->ai_hp = 100;
    copy_name(g->ai_name, name ? name : "Jesus");
    g->ai_slots[0].weapon = TOY_GAME_WEAPON_SMG;
    w = toy_game_weapon_info(TOY_GAME_WEAPON_SMG);
    g->ai_slots[0].mag = w->mag_size;
    g->ai_slots[0].reserve = TOY_GAME_AMMO_INFINITE;
    g->ai_slots[1].weapon = -1;
    g->ai_current_slot = 0;
    g->ai_reloading = 0;
    g->ai_reload_timer_ms = 0;
    g->ai_fire_cooldown_ms = 0;
    g->ai_muzzle_flash_ms = 0;
    g->ai_fire_seq = 0;
    g->ai_ray_count = 0;
    memset(g->ai_rays, 0, sizeof(g->ai_rays));
    memset(&g->actors[0], 0, sizeof(g->actors[0]));
    g->actors[0].active = g->ai_active;
    g->actors[0].actor_id = g->ai_actor_id;
    g->actors[0].kind = TOY_GAME_ACTOR_AI;
    g->actors[0].class_id = TOY_GAME_AI_LEVEL_1;
    g->actors[0].state = TOY_GAME_ACTOR_ALIVE;
    g->actors[0].x = g->ai_x; g->actors[0].z = g->ai_z;
    g->actors[0].sy = g->ai_sy; g->actors[0].cy = g->ai_cy;
    g->actors[0].hp = g->actors[0].max_hp = g->ai_hp;
    g->actors[0].slots[0] = g->ai_slots[0];
    g->actors[0].slots[1] = g->ai_slots[1];
    g->actors[0].current_slot = g->ai_current_slot;
    g->actors[0].fire_seq = g->ai_fire_seq;
    g->actors[0].ray_count = g->ai_ray_count;
    memcpy(g->actors[0].name, g->ai_name, sizeof(g->actors[0].name));
}

static void sync_ai_actor_from_legacy(struct toy_game *g)
{
    struct toy_game_actor *a = &g->actors[0];
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

int toy_game_revive_ai(struct toy_game *g, int dt_ms)
{
    if (!g->ai_active || !g->ai_down || dt_ms <= 0) return 0;
    g->ai_revive_progress_ms += dt_ms;
    if (g->ai_revive_progress_ms < TOY_GAME_REVIVE_MS) return 0;
    g->ai_revive_progress_ms = 0;
    g->ai_hp = TOY_GAME_REVIVE_HP;
    g->ai_down = 0;
    sync_ai_actor_from_legacy(g);
    push_event(g, TOY_GAME_EV_ACTOR_REVIVE);
    return 1;
}

void toy_game_set_world(struct toy_game *g,
                        const struct toy_game_box *boxes,
                        int box_count, int room_limit)
{
    g->world = boxes;
    g->world_count = box_count;
    g->room_limit = room_limit;
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
    g->spawn_zones = spawn_zones;
    g->spawn_zone_count = spawn_zone_count;
    g->campaign_mode = safe_room_count >= 2 && spawn_zone_count > 0;
    g->goal_hold_ms = 0;
    g->alarm_spawn_zone = -1;
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

void toy_game_set_secondary_player(struct toy_game *g, int active,
                                   int px, int pz)
{
    g->secondary_player_active = active != 0;
    g->secondary_px = px;
    g->secondary_pz = pz;
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

static int enemy_position_blocked(const struct toy_game *g,
                                  int x, int z, int radius)
{
    int i;
    if (toy_game_position_blocked(g, x, z, radius)) return 1;
    for (i = 0; i < g->safe_room_count; i++) {
        const struct toy_game_box *b = &g->safe_rooms[i];
        if (x + radius > b->minx && x - radius < b->maxx &&
            z + radius > b->minz && z - radius < b->maxz) return 1;
    }
    return 0;
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
    e->target_player = 0;
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
}

/* 在矩形区域内随机生成一个追踪型敌人；距玩家过近、压障碍或槽满
 * 返回 0。生成方向直接朝向玩家（追踪态首个逻辑步就会转脸）。 */
static int spawn_tracking_enemy(struct toy_game *g, int minx, int maxx,
                                int minz, int maxz, int min_dist2)
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
        /* 追踪尸潮使用快速敌人的数值，但保留独立 tracking 行为。 */
        init_enemy_stats(g, &g->enemies[slot], TOY_GAME_ENEMY_FAST);
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
int toy_game_spawn_horde(struct toy_game *g, int count_min, int count_max,
                         const struct toy_game_box *points, int point_count,
                         int min_player_dist)
{
    int count, spawned = 0, i, j, n_points, per, extra;
    int min_dist2 = min_player_dist * min_player_dist;
    int order[8];
    if (g->state != TOY_GAME_PLAYING) return 0;
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
            if (spawn_tracking_enemy(g, p->minx, p->maxx, p->minz, p->maxz,
                                     min_dist2))
                spawned++;
            else if (find_free_slot(g) < 0)
                return spawned;             /* 槽满：本次召唤到此为止 */
        }
    }
    return spawned;
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
            int type = slot % 10 == 0 ? TOY_GAME_ENEMY_HEAVY :
                       slot % 5 == 0 ? TOY_GAME_ENEMY_FAST :
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
        if (!g->campaign_mode) g->to_spawn--;
        g->enemies_alive++;
        return 1;
    }
    return 0;
}

void toy_game_place_enemy(struct toy_game *g, int x, int z)
{
    int slot = find_free_slot(g);
    if (slot < 0) return;
    g->enemies[slot].active = 1;
    g->enemies[slot].x = x;
    g->enemies[slot].z = z;
    init_enemy_stats(g, &g->enemies[slot], TOY_GAME_ENEMY_COMMON);
    g->enemies[slot].bite_cooldown_ms = 0;
    g->enemies[slot].flash = 0;
    g->enemies[slot].hurt = 0;
    g->enemies[slot].dying_ms = 0;
    init_enemy_ai(g, &g->enemies[slot]);
    g->enemies_alive++;
}

/* ── 波次状态机 ────────────────────────────────────────────────── */

static void update_waves(struct toy_game *g, int dt_ms)
{
    if (g->to_spawn == 0 && g->enemies_alive == 0) {
        /* 本波清空：进入波间间隔 */
        g->spawn_timer_ms -= dt_ms;
        if (g->spawn_timer_ms <= 0) {
            g->wave++;
            g->to_spawn = wave_quota(g->wave);
            g->spawn_timer_ms = 0;
            push_event(g, TOY_GAME_EV_WAVE_START);
        }
    } else if (g->to_spawn > 0) {
        g->spawn_timer_ms -= dt_ms;
        if (g->spawn_timer_ms <= 0) {
            try_spawn(g);
            g->spawn_timer_ms = TOY_GAME_SPAWN_INTERVAL_MS;
        }
    }
}

static void update_campaign_goal(struct toy_game *g, int dt_ms)
{
    const struct toy_game_box *goal;
    goal = &g->safe_rooms[g->safe_room_count - 1];
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
    if (player_in_safe_room(g)) return;
    if (g->enemies_alive > TOY_GAME_DIRECTOR_ALIVE_LOW ||
        g->active_attackers > TOY_GAME_DIRECTOR_ATTACKER_LOW) return;
    g->phase_timer_ms -= dt_ms;
    if (g->phase_timer_ms > 0) return;
    g->campaign_phase = TOY_GAME_PHASE_BUILDUP;
    g->spawn_budget = rand_range(g, TOY_GAME_DIRECTOR_MIN_GROUP,
                                 TOY_GAME_DIRECTOR_MAX_GROUP);
    g->spawn_timer_ms = 0;
    g->phase_timer_ms = 0;
    g->director_encounters++;
}

static void update_campaign_spawning(struct toy_game *g, int dt_ms)
{
    int interval;
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
        if (try_spawn(g)) g->spawn_budget--;
        interval = g->campaign_phase == TOY_GAME_PHASE_HORDE ?
                   TOY_GAME_ALARM_SPAWN_INTERVAL_MS :
                   TOY_GAME_CAMPAIGN_AMBIENT_SPAWN_INTERVAL_MS;
        g->spawn_timer_ms = interval;
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
        push_event(g, TOY_GAME_EV_ACTOR_DOWN);
    }
}

static void bite_ai(struct toy_game *g, struct toy_game_enemy *e)
{
    const struct toy_game_enemy_info *info = toy_game_enemy_info(e->type);
    if (e->bite_cooldown_ms > 0 || !g->ai_active || g->ai_down) return;
    e->bite_cooldown_ms = TOY_GAME_BITE_MS;
    g->ai_hp -= info->bite_damage;
    if (g->ai_hp < 0) g->ai_hp = 0;
    e->hurt = 150;
    push_event(g, TOY_GAME_EV_BITE);
    if (g->ai_hp <= 0) {
        g->ai_down = 1;
        g->ai_revive_progress_ms = 0;
        push_event(g, TOY_GAME_EV_ACTOR_DOWN);
    }
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
    if (!enemy_position_blocked(g, e->x + nx, e->z, TOY_GAME_ENEMY_RADIUS))
        e->x += nx;
    else
        e->wander_timer_ms = 0;
    if (!enemy_position_blocked(g, e->x, e->z + nz, TOY_GAME_ENEMY_RADIUS))
        e->z += nz;
    else
        e->wander_timer_ms = 0;
}

static void chase_enemy(struct toy_game *g, struct toy_game_enemy *e,
                        int dx, int dz, long long dist, int target_player)
{
    int nx, nz;
    if (dist < TOY_GAME_ATTACK_RANGE) {
        if (target_player == 0 && !player_in_safe_room(g)) bite_player(g, e);
        else if (target_player == 2) bite_ai(g, e);
        return;
    }
    if (dist == 0) return;
    nx = (int)((long long)dx * e->speed / dist);
    nz = (int)((long long)dz * e->speed / dist);
    if (!enemy_position_blocked(g, e->x + nx, e->z, TOY_GAME_ENEMY_RADIUS))
        e->x += nx;
    if (!enemy_position_blocked(g, e->x, e->z + nz, TOY_GAME_ENEMY_RADIUS))
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

static void update_enemy_ai(struct toy_game *g, struct toy_game_enemy *e,
                            int dt_ms)
{
    int target_x, target_z, target_player;
    int primary_dx = g->px - e->x;
    int primary_dz = g->pz - e->z;
    int secondary_dx = g->secondary_px - e->x;
    int secondary_dz = g->secondary_pz - e->z;
    long long primary_dist2 = (long long)primary_dx * primary_dx +
                              (long long)primary_dz * primary_dz;
    long long secondary_dist2 = (long long)secondary_dx * secondary_dx +
                                (long long)secondary_dz * secondary_dz;
    int dx, dz;
    long long dist2;
    long long dist;
    int visual;
    int ai_available = g->ai_active && !g->ai_down;

    if (e->retarget_timer_ms > 0) e->retarget_timer_ms -= dt_ms;
    if ((e->target_player == 0 && g->player_down) ||
        (e->target_player == 1 && !g->secondary_player_active) ||
        (e->target_player == 2 && !ai_available) ||
        (e->retarget_timer_ms > 0 && e->target_player != 0 &&
         e->target_player != 1 && e->target_player != 2)) {
        e->retarget_timer_ms = 0;
    }
    if (e->retarget_timer_ms > 0) {
        target_player = e->target_player;
    } else if (e->retarget_timer_ms <= 0 || e->target_player < 0) {
        target_player = 0;
        if (g->player_down) target_player = -1;
        if (g->secondary_player_active &&
            (target_player < 0 || secondary_dist2 < primary_dist2)) {
            target_player = 1;
        }
        if (ai_available) {
            long long ai_dx = (long long)g->ai_x - e->x;
            long long ai_dz = (long long)g->ai_z - e->z;
            long long ai_dist2 = ai_dx * ai_dx + ai_dz * ai_dz;
            if (target_player < 0 ||
                (target_player == 1 ? ai_dist2 < secondary_dist2 :
                 ai_dist2 < primary_dist2))
                target_player = 2;
        }
        if (target_player < 0) target_player = 0;
        e->target_player = target_player;
        e->retarget_timer_ms = TOY_GAME_RETARGET_MS;
    } else {
        target_player = e->target_player;
    }
    if (target_player == 1) {
        target_x = g->secondary_px; target_z = g->secondary_pz;
    } else if (target_player == 2) {
        target_x = g->ai_x; target_z = g->ai_z;
    } else {
        target_x = g->px; target_z = g->pz;
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
        chase_enemy(g, e, dx, dz, dist, target_player);
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
        chase_enemy(g, e, tx, tz, target_dist, target_player);
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
        chase_enemy(g, e, tx, tz, target_dist, target_player);
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

/* 敌人间分离：距离 < 260 时沿连线各推 8 单位 */
static void separate_enemies(struct toy_game *g)
{
    int i, j;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        struct toy_game_enemy *a = &g->enemies[i];
        if (a->active != 1) continue;
        for (j = i + 1; j < TOY_GAME_MAX_ENEMIES; j++) {
            struct toy_game_enemy *b = &g->enemies[j];
            int dx, dz;
            long long dist2, dist;
            if (b->active != 1) continue;
            dx = b->x - a->x;
            dz = b->z - a->z;
            dist2 = (long long)dx * dx + (long long)dz * dz;
            if (dist2 == 0 || dist2 >= 260 * 260) continue;
            dist = isqrt(dist2);
            if (dist > 0) {
                int ax = a->x - (int)((long long)dx * 8 / dist);
                int az = a->z - (int)((long long)dz * 8 / dist);
                int bx = b->x + (int)((long long)dx * 8 / dist);
                int bz = b->z + (int)((long long)dz * 8 / dist);
                if (!enemy_position_blocked(g, ax, az, TOY_GAME_ENEMY_RADIUS)) {
                    a->x = ax;
                    a->z = az;
                }
                if (!enemy_position_blocked(g, bx, bz, TOY_GAME_ENEMY_RADIUS)) {
                    b->x = bx;
                    b->z = bz;
                }
            }
        }
    }
    /* 推挤可能越过房间边界，clamp 回界内（障碍重叠留待移动碰撞纠正） */
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        struct toy_game_enemy *e = &g->enemies[i];
        int limit = g->room_limit - TOY_GAME_ENEMY_RADIUS;
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
static int fire_ray(struct toy_game *g, int sy, int cy,
                    int *out_ex, int *out_ez, int *out_hit_world)
{
    int best = -1, best_t = 0, i;
    int radius_times_1024 = TOY_GAME_HIT_RADIUS * 1024;
    long long world_t = (long long)TOY_GAME_MAX_RANGE << 20; /* 世界距离定点 */
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
        *out_ex = g->px + (sy * best_t) / 1024;
        *out_ez = g->pz + (cy * best_t) / 1024;
        *out_hit_world = 0;
        e->hp = 0;
        e->active = 2;
        e->dying_ms = TOY_GAME_DYING_MS;
        e->flash = 120;
        g->enemies_alive--;
        g->kills++;
        push_event(g, TOY_GAME_EV_KILL);
        return 1;
    }
    {
        long long dist_w = world_t >> 20;
        *out_ex = g->px + (int)(sy * dist_w / 1024);
        *out_ez = g->pz + (int)(cy * dist_w / 1024);
        *out_hit_world = world_t < ((long long)TOY_GAME_MAX_RANGE << 20);
    }
    return 0;
}

int toy_game_fire(struct toy_game *g, int sy, int cy)
{
    struct toy_game_slot *s = &g->slots[g->current_slot];
    const struct toy_game_weapon_info *w = toy_game_weapon_info(s->weapon);
    int pellet, hit = 0;
    if (g->state != TOY_GAME_PLAYING || g->reloading) return 0;
    g->fire_cooldown_ms = w->cooldown_ms;
    g->muzzle_flash_ms = TOY_GAME_MUZZLE_FLASH_MS;
    if (s->mag <= 0) {
        push_event(g, TOY_GAME_EV_DRY_FIRE);
        return 0;
    }
    s->mag--;
    push_event(g, TOY_GAME_EV_SHOOT);
    emit_enemy_noise(g, g->px, g->pz, TOY_GAME_GUNSHOT_RANGE);
    if (s->mag == 0) {
        g->reloading = 1;
        g->reload_timer_ms = w->reload_ms;
        push_event(g, TOY_GAME_EV_RELOAD_START);
    }
    /* 每颗弹丸在 [-spread, +spread] 内随机偏转（1024 定点）：霰弹枪
     * 近距离密集、远距离发散；弹道记录供宿主渲染 tracer 与命中特效。 */
    g->fire_seq++;
    g->ray_count = w->pellets;
    for (pellet = 0; pellet < w->pellets; pellet++) {
        int off_x, off_y;
        int ray_sy, ray_cy;
        /* 在准心周围取圆形散布，而不是只在水平线上散布。 */
        do {
            off_x = rand_range(g, -w->spread, w->spread);
            off_y = rand_range(g, -w->spread, w->spread);
        } while (off_x * off_x + off_y * off_y >
                 w->spread * w->spread);
        ray_sy = (sy * 1024 - cy * off_x) / 1024;
        ray_cy = (cy * 1024 + sy * off_x) / 1024;
        int ex, ez, hit_world, killed;
        normalize_dir(&ray_sy, &ray_cy);   /* 旋转后长度略偏，归一化保证判定一致 */
        killed = fire_ray(g, ray_sy, ray_cy, &ex, &ez, &hit_world);
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
    g->reloading = 0;
    g->reload_timer_ms = 0;
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

void toy_game_update_weapon_held(struct toy_game *g,
                                 const unsigned char *keys_pressed,
                                 int fire_pressed, int fire_held,
                                 int sy, int cy, int dt_ms)
{
    struct toy_game_slot *s;
    const struct toy_game_weapon_info *w;
    if (g->state != TOY_GAME_PLAYING || g->player_down) return;

    /* 切枪键（1/2）：先于换弹与射击处理，切换即打断换弹 */
    if (keys_pressed) {
        if (keys_pressed[TOY_GAME_KEY_SLOT_1]) toy_game_switch_weapon(g, 0);
        if (keys_pressed[TOY_GAME_KEY_SLOT_2]) toy_game_switch_weapon(g, 1);
    }
    s = &g->slots[g->current_slot];
    w = toy_game_weapon_info(s->weapon);

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
    struct toy_game_slot player_slots[TOY_GAME_WEAPON_SLOTS];
    struct toy_game_ray player_rays[TOY_GAME_MAX_RAYS];
    int player_px, player_pz, player_current_slot;
    int player_reloading, player_reload_timer_ms, player_fire_cooldown_ms;
    int player_muzzle_flash_ms, player_ray_count;
    unsigned int player_fire_seq;
    int target = -1, best_dist = 0, i;
    int sy = 0, cy = 1024;
    int player_down;
    sync_ai_actor_from_legacy(g);
    if (!g->ai_active || g->ai_down || g->state != TOY_GAME_PLAYING) return;

    /* 与普通敌人察觉距离同量级，且只选择无遮挡目标。 */
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        struct toy_game_enemy *e = &g->enemies[i];
        int dx, dz, dist, j, blocked = 0;
        long long d2;
        if (e->active != 1) continue;
        dx = e->x - g->ai_x;
        dz = e->z - g->ai_z;
        d2 = (long long)dx * dx + (long long)dz * dz;
        if (d2 > (long long)TOY_GAME_DETECT_RANGE * TOY_GAME_DETECT_RANGE)
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

    memcpy(player_slots, g->slots, sizeof(player_slots));
    memcpy(player_rays, g->rays, sizeof(player_rays));
    player_px = g->px; player_pz = g->pz;
    player_current_slot = g->current_slot;
    player_reloading = g->reloading;
    player_reload_timer_ms = g->reload_timer_ms;
    player_fire_cooldown_ms = g->fire_cooldown_ms;
    player_muzzle_flash_ms = g->muzzle_flash_ms;
    player_ray_count = g->ray_count;
    player_fire_seq = g->fire_seq;
    player_down = g->player_down;

    memcpy(g->slots, g->ai_slots, sizeof(g->slots));
    g->px = g->ai_x; g->pz = g->ai_z;
    g->current_slot = g->ai_current_slot;
    g->reloading = g->ai_reloading;
    g->reload_timer_ms = g->ai_reload_timer_ms;
    g->fire_cooldown_ms = g->ai_fire_cooldown_ms;
    g->muzzle_flash_ms = g->ai_muzzle_flash_ms;
    g->player_down = 0;
    toy_game_update_weapon_held(g, NULL, target >= 0, target >= 0,
                                sy, cy, dt_ms);
    memcpy(g->ai_slots, g->slots, sizeof(g->ai_slots));
    g->ai_current_slot = g->current_slot;
    g->ai_reloading = g->reloading;
    g->ai_reload_timer_ms = g->reload_timer_ms;
    g->ai_fire_cooldown_ms = g->fire_cooldown_ms;
    g->ai_muzzle_flash_ms = g->muzzle_flash_ms;
    g->ai_sy = target >= 0 ? sy : g->ai_sy;
    g->ai_cy = target >= 0 ? cy : g->ai_cy;
    if (g->fire_seq != player_fire_seq) {
        g->ai_fire_seq++;
        g->ai_ray_count = g->ray_count;
        memcpy(g->ai_rays, g->rays, sizeof(g->ai_rays));
    }

    memcpy(g->slots, player_slots, sizeof(g->slots));
    memcpy(g->rays, player_rays, sizeof(g->rays));
    g->px = player_px; g->pz = player_pz;
    g->current_slot = player_current_slot;
    g->reloading = player_reloading;
    g->reload_timer_ms = player_reload_timer_ms;
    g->fire_cooldown_ms = player_fire_cooldown_ms;
    g->muzzle_flash_ms = player_muzzle_flash_ms;
    g->ray_count = player_ray_count;
    g->fire_seq = player_fire_seq;
    g->player_down = player_down;
    sync_ai_actor_from_legacy(g);
}

void toy_game_update_held(struct toy_game *g,
                          const unsigned char *keys_pressed,
                          int fire_pressed, int fire_held,
                          int sy, int cy, int dt_ms)
{
    int i;
    if (g->state != TOY_GAME_PLAYING) return;
    toy_game_update_weapon_held(g, keys_pressed, fire_pressed, fire_held,
                                sy, cy, dt_ms);
    toy_game_update_ai_teammate(g, dt_ms);

    /* 敌人计时器与移动/攻击/倒地 */
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        struct toy_game_enemy *e = &g->enemies[i];
        if (e->active == 1) {
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
            update_enemy_ai(g, e, dt_ms);
        } else if (e->active == 2) {
            e->dying_ms -= dt_ms;
            if (e->dying_ms <= 0) e->active = 0;
        }
    }
    separate_enemies(g);
    if (g->campaign_mode) update_campaign(g, dt_ms);
    else update_waves(g, dt_ms);
}

/* 半自动兼容入口：无按住连发（历史测试/宿主行为不变） */
void toy_game_update(struct toy_game *g, const unsigned char *keys_pressed,
                     int fire_pressed, int sy, int cy, int dt_ms)
{
    toy_game_update_held(g, keys_pressed, fire_pressed, 0, sy, cy, dt_ms);
}
