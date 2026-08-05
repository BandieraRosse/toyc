/*
 * game.c — 僵尸潮射击游戏规则（平台无关）。
 *
 * 全部规则集中于此：xorshift64* PRNG、世界碰撞查询、僵尸 AI（追逐/
 * 攻击/分离/倒地）、波次或固定区域生成、安全室与终点、hitscan 射击与
 * 障碍遮挡、弹匣/换弹、玩家生命/死亡/通关冻结、事件队列。
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
    memset(g, 0, sizeof(struct toy_game));
    g->rng = seed ? seed : 0x9E3779B97F4A7C15ULL;
    g->hp = 100;
    g->state = TOY_GAME_PLAYING;
    g->ammo_mag = TOY_GAME_MAG_SIZE;
    g->ammo_reserve = TOY_GAME_AMMO_INFINITE;
    g->wave = 1;
    g->to_spawn = wave_quota(1);
    g->spawn_timer_ms = TOY_GAME_WAVE_FIRST_DELAY_MS;
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
        g->spawn_timer_ms = TOY_GAME_CAMPAIGN_FIRST_SPAWN_MS;
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
    e->wander_timer_ms = rand_range(g, 600, 1800);
    e->dir_x = enemy_dir_x[direction];
    e->dir_z = enemy_dir_z[direction];
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
        g->enemies[slot].speed = rand_range(g, 38, 56);
        g->enemies[slot].hp = 1;
        g->enemies[slot].bite_cooldown_ms = 0;
        g->enemies[slot].flash = 0;
        g->enemies[slot].hurt = 0;
        g->enemies[slot].dying_ms = 0;
        init_enemy_ai(g, &g->enemies[slot]);
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
    g->enemies[slot].speed = 50;
    g->enemies[slot].hp = 1;
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

static void update_campaign(struct toy_game *g, int dt_ms)
{
    const struct toy_game_box *goal;
    int spawn_interval = TOY_GAME_CAMPAIGN_SPAWN_INTERVAL_MS;
    if (!g->alarm_triggered && g->alarm_zone &&
        toy_game_point_in_box(g->px, g->pz, g->alarm_zone)) {
        g->alarm_triggered = 1;
        g->alarm_timer_ms = TOY_GAME_ALARM_DURATION_MS;
        g->spawn_timer_ms = 0;
        push_event(g, TOY_GAME_EV_ALARM_TRIGGERED);
    }
    if (g->alarm_timer_ms > 0) {
        g->alarm_timer_ms -= dt_ms;
        if (g->alarm_timer_ms < 0) g->alarm_timer_ms = 0;
        spawn_interval = TOY_GAME_ALARM_SPAWN_INTERVAL_MS;
    }
    g->spawn_timer_ms -= dt_ms;
    if (g->spawn_timer_ms <= 0) {
        try_spawn(g);
        g->spawn_timer_ms = spawn_interval;
    }
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

/* ── 僵尸 AI ───────────────────────────────────────────────────── */

static void bite_player(struct toy_game *g, struct toy_game_enemy *e)
{
    if (e->bite_cooldown_ms > 0) return;
    e->bite_cooldown_ms = TOY_GAME_BITE_MS;
    g->hp -= TOY_GAME_BITE_DAMAGE;
    if (g->hp < 0) g->hp = 0;
    g->damage_flash_ms = TOY_GAME_DAMAGE_FLASH_MS;
    e->hurt = 150;
    push_event(g, TOY_GAME_EV_BITE);
    if (g->hp <= 0) {
        g->state = TOY_GAME_OVER;
        push_event(g, TOY_GAME_EV_PLAYER_DEATH);
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
                        int dx, int dz, long long dist)
{
    int nx, nz;
    if (dist < TOY_GAME_ATTACK_RANGE) {
        if (!player_in_safe_room(g)) bite_player(g, e);
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

static void update_enemy_ai(struct toy_game *g, struct toy_game_enemy *e,
                            int dt_ms)
{
    int dx = g->px - e->x;
    int dz = g->pz - e->z;
    long long dist2 = (long long)dx * dx + (long long)dz * dz;
    long long dist = isqrt(dist2);
    int in_range = dist <= TOY_GAME_DETECT_RANGE && !player_in_safe_room(g);

    if (e->ai_state == TOY_GAME_ENEMY_IDLE) {
        wander_enemy(g, e, dt_ms);
        if (in_range) {
            e->ai_state = TOY_GAME_ENEMY_NOTICE;
            e->ai_timer_ms = rand_range(g, TOY_GAME_NOTICE_MIN_MS,
                                        TOY_GAME_NOTICE_MAX_MS);
        }
        return;
    }
    if (e->ai_state == TOY_GAME_ENEMY_NOTICE) {
        if (!in_range) {
            e->ai_state = TOY_GAME_ENEMY_IDLE;
            e->ai_timer_ms = 0;
            e->wander_timer_ms = rand_range(g, 600, 1800);
            return;
        }
        turn_enemy_toward(e, dx, dz);
        e->ai_timer_ms -= dt_ms;
        if (e->ai_timer_ms <= 0) {
            e->ai_state = TOY_GAME_ENEMY_ALERT;
            e->ai_timer_ms = TOY_GAME_ALERT_MS;
        }
        return;
    }
    turn_enemy_toward(e, dx, dz);
    if (e->ai_state == TOY_GAME_ENEMY_ALERT) {
        e->ai_timer_ms -= dt_ms;
        if (e->ai_timer_ms <= 0) {
            e->ai_state = TOY_GAME_ENEMY_CHASE;
            e->ai_timer_ms = 0;
        }
        return;
    }
    chase_enemy(g, e, dx, dz, dist);
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

int toy_game_fire(struct toy_game *g, int sy, int cy)
{
    int best = -1, best_t = 0, i;
    int radius_times_1024 = TOY_GAME_HIT_RADIUS * 1024;
    if (g->state != TOY_GAME_PLAYING || g->reloading) return 0;
    g->fire_cooldown_ms = TOY_GAME_FIRE_COOLDOWN_MS;
    g->muzzle_flash_ms = TOY_GAME_MUZZLE_FLASH_MS;
    if (g->ammo_mag <= 0) {
        push_event(g, TOY_GAME_EV_DRY_FIRE);
        return 0;
    }
    g->ammo_mag--;
    push_event(g, TOY_GAME_EV_SHOOT);
    if (g->ammo_mag == 0) {
        g->reloading = 1;
        g->reload_timer_ms = TOY_GAME_RELOAD_MS;
        push_event(g, TOY_GAME_EV_RELOAD_START);
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
        e->hp = 0;
        e->active = 2;
        e->dying_ms = TOY_GAME_DYING_MS;
        e->flash = 120;
        g->enemies_alive--;
        g->kills++;
        push_event(g, TOY_GAME_EV_KILL);
        return 1;
    }
    return 0;
}

/* ── 主更新 ────────────────────────────────────────────────────── */

void toy_game_update(struct toy_game *g, const unsigned char *keys_pressed,
                     int fire_pressed, int sy, int cy, int dt_ms)
{
    int i;
    if (g->state != TOY_GAME_PLAYING) return;

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
            int used = TOY_GAME_MAG_SIZE - g->ammo_mag;
            if (g->ammo_reserve != TOY_GAME_AMMO_INFINITE &&
                used > g->ammo_reserve) used = g->ammo_reserve;
            g->ammo_mag += used;
            if (g->ammo_reserve != TOY_GAME_AMMO_INFINITE)
                g->ammo_reserve -= used;
            g->reloading = 0;
            push_event(g, TOY_GAME_EV_RELOAD_DONE);
        }
    } else if (keys_pressed && keys_pressed[TOY_GAME_KEY_RELOAD] &&
               g->ammo_mag < TOY_GAME_MAG_SIZE &&
               (g->ammo_reserve > 0 ||
                g->ammo_reserve == TOY_GAME_AMMO_INFINITE)) {
        g->reloading = 1;
        g->reload_timer_ms = TOY_GAME_RELOAD_MS;
        push_event(g, TOY_GAME_EV_RELOAD_START);
    }

    /* 射击（半自动：fire_pressed 边沿 + 冷却） */
    if (fire_pressed && !g->reloading && g->fire_cooldown_ms <= 0)
        toy_game_fire(g, sy, cy);

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
