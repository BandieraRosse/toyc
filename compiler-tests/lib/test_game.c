/* test_game.c — 僵尸潮游戏规则与 SFX 合成回归
 * EXPECT: 0
 */

#include "toy_game.h"
#include "string.h"
#include "math.h"

static const struct toy_game_box world[3] = {
    { -1700, -700,  300, 1700 },
    {  600, 1800, -900,  100 },
    { 2300, 3200, 1800, 3000 },
};
static const struct toy_game_box test_safe_rooms[2] = {
    {-1000, 1000, -5000, -4000},
    {-1000, 1000,  4000,  5000},
};
static const struct toy_game_box test_spawn_zones[2] = {
    {-4000, -3000, -1000, 1000},
    { 3000,  4000, -1000, 1000},
};
#define ROOM 5700

static int count_alive(const struct toy_game *g)
{
    int i, n = 0;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++)
        if (g->enemies[i].active == 1) n++;
    return n;
}

static int has_event(unsigned char *evs, int count, unsigned char want)
{
    int i;
    for (i = 0; i < count; i++)
        if (evs[i] == want) return 1;
    return 0;
}

/* 1: PRNG 与游戏推进确定性（同 seed 两实例逐步完全一致） */
static int test_prng_determinism(void)
{
    struct toy_game a, b;
    unsigned char evs[16];
    int i;
    toy_game_init(&a, 42);
    toy_game_init(&b, 42);
    toy_game_set_world(&a, world, 3, ROOM);
    toy_game_set_world(&b, world, 3, ROOM);
    for (i = 0; i < 400; i++) {
        toy_game_update(&a, NULL, 0, 0, 1024, 16);
        toy_game_update(&b, NULL, 0, 0, 1024, 16);
        toy_game_drain_events(&a, evs, 16);
        toy_game_drain_events(&b, evs, 16);
    }
    if (a.hp != b.hp || a.state != b.state || a.wave != b.wave ||
        a.kills != b.kills || a.enemies_alive != b.enemies_alive) return 1;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        if (a.enemies[i].active != b.enemies[i].active) return 1;
        if (a.enemies[i].x != b.enemies[i].x) return 1;
        if (a.enemies[i].z != b.enemies[i].z) return 1;
        if (a.enemies[i].hp != b.enemies[i].hp) return 1;
        if (a.enemies[i].ai_state != b.enemies[i].ai_state) return 1;
        if (a.enemies[i].dir_x != b.enemies[i].dir_x ||
            a.enemies[i].dir_z != b.enemies[i].dir_z) return 1;
    }
    return 0;
}

/* 2: 首波生成合法（房间内、不压障碍、距玩家足够远） */
static int test_wave_spawn(void)
{
    struct toy_game g;
    int i;
    toy_game_init(&g, 7);
    toy_game_set_world(&g, world, 3, ROOM);
    for (i = 0; i < 100; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (count_alive(&g) == 0) return 2;
    if (g.wave != 1) return 2;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        const struct toy_game_enemy *e = &g.enemies[i];
        int dx, dz;
        if (e->active != 1) continue;
        if (e->x < -ROOM || e->x > ROOM || e->z < -ROOM || e->z > ROOM) return 2;
        if (toy_game_position_blocked(&g, e->x, e->z,
                                      TOY_GAME_ENEMY_RADIUS)) return 2;
        dx = e->x - g.px;
        dz = e->z - g.pz;
        if (dx * dx + dz * dz <
            TOY_GAME_MIN_SPAWN_DIST * TOY_GAME_MIN_SPAWN_DIST) return 2;
    }
    return 0;
}

/* 3: 先侦测/警觉，再追玩家；不会生成后立刻扑向玩家 */
static int test_chase_player(void)
{
    struct toy_game g;
    int i, saw_alert = 0;
    long long dist;
    toy_game_init(&g, 3);
    toy_game_set_world(&g, NULL, 0, ROOM);
    g.px = 0;
    g.pz = 0;
    toy_game_place_enemy(&g, 2000, 0);
    g.enemies[0].dir_x = -1024;
    g.enemies[0].dir_z = 0;
    for (i = 0; i < 60; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.enemies[0].ai_state != TOY_GAME_ENEMY_NOTICE) return 3;
    dist = (long long)g.enemies[0].x * g.enemies[0].x +
           (long long)g.enemies[0].z * g.enemies[0].z;
    if (isqrt(dist) < 1500) return 3;
    for (i = 0; i < 170; i++) {
        toy_game_update(&g, NULL, 0, 0, 1024, 16);
        if (g.enemies[0].ai_state == TOY_GAME_ENEMY_ALERT) saw_alert = 1;
    }
    if (!saw_alert || g.enemies[0].ai_state != TOY_GAME_ENEMY_CHASE) return 3;
    for (i = 0; i < 60; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    dist = (long long)g.enemies[0].x * g.enemies[0].x +
           (long long)g.enemies[0].z * g.enemies[0].z;
    if (isqrt(dist) >= 500) return 3;
    return 0;
}

/* 4: 障碍挡住追兵（不穿墙） */
static int test_wall_blocks(void)
{
    struct toy_game g;
    int i;
    toy_game_init(&g, 5);
    toy_game_set_world(&g, &world[1], 1, ROOM);
    g.px = -1000;
    g.pz = 0;
    toy_game_place_enemy(&g, 3000, 0);
    g.enemies[0].ai_state = TOY_GAME_ENEMY_CHASE;
    for (i = 0; i < 120; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    /* 逐轴滑动允许圆缘擦过障碍（x≈750 贴墙），但中心绝不可越过 maxx 穿墙 */
    if (g.enemies[0].x < 1800) return 4;
    return 0;
}

/* 5: 每次攻击扣 2 点血 + 独立冷却（1000ms 只咬一次） */
static int test_bite_damage(void)
{
    struct toy_game g;
    unsigned char evs[16];
    int i, n;
    toy_game_init(&g, 9);
    toy_game_set_world(&g, NULL, 0, ROOM);
    g.px = 0;
    g.pz = 0;
    toy_game_place_enemy(&g, 250, 0);
    g.enemies[0].ai_state = TOY_GAME_ENEMY_CHASE;
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.hp != 98) return 5;
    n = toy_game_drain_events(&g, evs, 16);
    if (!has_event(evs, n, TOY_GAME_EV_BITE)) return 5;
    for (i = 0; i < 10; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.hp != 98) return 5;
    for (i = 0; i < 60; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.hp != 96) return 5;
    return 0;
}

/* 6: 命中即死 → 倒地 → 移除，kills 与事件正确 */
static int test_kill_and_removal(void)
{
    struct toy_game g;
    unsigned char evs[16];
    int i, n;
    toy_game_init(&g, 11);
    toy_game_set_world(&g, NULL, 0, ROOM);
    g.px = 0;
    g.pz = 0;
    toy_game_place_enemy(&g, 1000, 0);
    if (!toy_game_fire(&g, 1024, 0)) return 6;
    if (g.enemies[0].active != 2) return 6;
    if (g.kills != 1 || g.enemies_alive != 0) return 6;
    n = toy_game_drain_events(&g, evs, 16);
    if (!has_event(evs, n, TOY_GAME_EV_KILL)) return 6;
    if (!has_event(evs, n, TOY_GAME_EV_SHOOT)) return 6;
    for (i = 0; i < 26; i++) toy_game_update(&g, NULL, 0, 1024, 0, 16);
    if (g.enemies[0].active != 0) return 6;
    return 0;
}

/* 7: 波次推进（清空后进入下一波） */
static int test_wave_progression(void)
{
    struct toy_game g;
    int i;
    toy_game_init(&g, 13);
    toy_game_set_world(&g, NULL, 0, ROOM);
    g.px = 0;
    g.pz = 0;
    g.to_spawn = 0;   /* 模拟首波已清空 */
    for (i = 0; i < 160; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.wave != 2) return 7;
    if (g.to_spawn <= 0 || count_alive(&g) == 0) return 7;
    return 0;
}

/* 8: 命中判定三朝向（正中 / 偏侧 / 背向） */
static int test_hit_direction(void)
{
    struct toy_game g;
    toy_game_init(&g, 15);
    toy_game_set_world(&g, NULL, 0, ROOM);
    g.px = 0;
    g.pz = -1000;
    toy_game_place_enemy(&g, 0, 0);
    if (!toy_game_fire(&g, 0, 1024)) return 8;        /* 朝 +z 命中 */
    g.enemies[0].active = 0;
    g.enemies_alive--;
    toy_game_place_enemy(&g, 0, 0);
    if (toy_game_fire(&g, 1024, 0)) return 8;         /* 朝 +x 未命中 */
    g.enemies[0].active = 0;
    g.enemies_alive--;
    toy_game_place_enemy(&g, 0, 0);
    if (toy_game_fire(&g, 0, -1024)) return 8;        /* 背向未命中 */
    return 0;
}

/* 9: 同射线前后两敌取近者 */
static int test_hit_priority(void)
{
    struct toy_game g;
    toy_game_init(&g, 17);
    toy_game_set_world(&g, NULL, 0, ROOM);
    g.px = 0;
    g.pz = 0;
    toy_game_place_enemy(&g, 0, 600);
    toy_game_place_enemy(&g, 0, 2200);
    if (!toy_game_fire(&g, 0, 1024)) return 9;
    if (g.enemies[0].active != 2 || g.enemies[1].active != 1) return 9;
    return 0;
}

/* 10: 障碍物遮挡子弹（遮挡不命中，障碍前命中）。
 * 方向 (788,727) 精确穿过 world[2]（x 2300..3200, z 1800..3000）中心。 */
static int test_obstacle_occlusion(void)
{
    struct toy_game g;
    toy_game_init(&g, 19);
    toy_game_set_world(&g, &world[2], 1, ROOM);
    g.px = 0;
    g.pz = 0;
    toy_game_place_enemy(&g, 1970, 1817);             /* 障碍前 → 命中 */
    if (!toy_game_fire(&g, 788, 727)) return 10;
    g.enemies[0].active = 0;
    g.enemies_alive--;
    toy_game_place_enemy(&g, 3940, 3635);             /* 障碍后 → 被挡 */
    if (toy_game_fire(&g, 788, 727)) return 10;
    return 0;
}

/* 11: 弹匣打空自动换弹，手枪备用弹药无限。 */
static int test_ammo_reload(void)
{
    struct toy_game g;
    unsigned char evs[16];
    int i, n;
    toy_game_init(&g, 21);
    toy_game_set_world(&g, NULL, 0, ROOM);
    g.px = 0;
    g.pz = 0;
    for (i = 0; i < 30; i++) {
        toy_game_fire(&g, 0, 1024);
        if ((i % 5) == 4 && i < 29)
            toy_game_drain_events(&g, evs, 16);  /* 防止 SHOOT 灌满队列 */
    }
    if (g.slots[1].mag != 0 || g.slots[1].reserve != TOY_GAME_AMMO_INFINITE ||
        !g.reloading) return 11;
    n = toy_game_drain_events(&g, evs, 16);
    if (!has_event(evs, n, TOY_GAME_EV_RELOAD_START)) return 11;
    for (i = 0; i < 94; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.reloading) return 11;
    if (g.slots[1].mag != 30 ||
        g.slots[1].reserve != TOY_GAME_AMMO_INFINITE) return 11;
    n = toy_game_drain_events(&g, evs, 16);
    if (!has_event(evs, n, TOY_GAME_EV_RELOAD_DONE)) return 11;
    return 0;
}

/* 12: 射速限制（200ms 冷却内连击只消耗 1 发） */
static int test_fire_rate(void)
{
    struct toy_game g;
    int i;
    toy_game_init(&g, 23);
    toy_game_set_world(&g, NULL, 0, ROOM);
    g.px = 0;
    g.pz = 0;
    toy_game_update(&g, NULL, 1, 0, 1024, 16);
    if (g.slots[1].mag != 29) return 12;
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    toy_game_update(&g, NULL, 1, 0, 1024, 16);
    if (g.slots[1].mag != 29) return 12;
    for (i = 0; i < 13; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    toy_game_update(&g, NULL, 1, 0, 1024, 16);
    if (g.slots[1].mag != 28) return 12;
    return 0;
}

/* 13: 血量归零 → 倒地冻结，等待复活 */
static int test_game_over(void)
{
    struct toy_game g;
    unsigned char evs[16];
    int i, n;
    toy_game_init(&g, 25);
    toy_game_set_world(&g, NULL, 0, ROOM);
    g.px = 0;
    g.pz = 0;
    g.to_spawn = 0;
    g.hp = 5;
    toy_game_place_enemy(&g, 250, 0);
    g.enemies[0].ai_state = TOY_GAME_ENEMY_CHASE;
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.state != TOY_GAME_PLAYING || g.hp != 3) return 13;
    for (i = 0; i < 126; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.state != TOY_GAME_PLAYING || !g.player_down || g.hp != 0) return 13;
    n = toy_game_drain_events(&g, evs, 16);
    if (!has_event(evs, n, TOY_GAME_EV_ACTOR_DOWN)) return 13;
    for (i = 0; i < 100; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.hp != 0 || !g.player_down || g.state != TOY_GAME_PLAYING) return 13;
    return 0;
}

/* 14: 容量 32 封顶，死亡后补位 */
static int test_capacity(void)
{
    struct toy_game g;
    int i;
    toy_game_init(&g, 27);
    toy_game_set_world(&g, NULL, 0, ROOM);
    g.px = 0;
    g.pz = 0;
    g.hp = 1000000;               /* 防测试中被咬死导致冻结 */
    g.to_spawn = 1000;
    g.spawn_timer_ms = 0;
    for (i = 0; i < 1700; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (count_alive(&g) != TOY_GAME_MAX_ENEMIES) return 14;
    g.enemies[0].active = 0;
    g.enemies_alive--;
    for (i = 0; i < 40; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.enemies[0].active != 1) return 14;
    return 0;
}

/* 15: SFX 静音 / 触发 / 多 voice 混音不爆 */
static int test_sfx(void)
{
    struct toy_sfx sfx;
    short buf[2048];
    int i, nonzero = 0;
    toy_sfx_init(&sfx, 44100);
    toy_sfx_render(&sfx, buf, 1024);
    for (i = 0; i < 1024; i++)
        if (buf[2 * i] != 0 || buf[2 * i + 1] != 0) return 15;
    toy_sfx_play(&sfx, TOY_SFX_GUNSHOT);
    toy_sfx_render(&sfx, buf, 1024);
    for (i = 0; i < 1024; i++)
        if (buf[2 * i] != 0) nonzero = 1;
    if (!nonzero) return 15;
    for (i = 0; i < TOY_SFX_MAX_VOICES; i++)
        toy_sfx_play(&sfx, TOY_SFX_PLAYER_DEATH);
    toy_sfx_render(&sfx, buf, 1024);
    for (i = 0; i < 1024; i++) {
        int s = buf[2 * i];
        if (s > 32767 || s < -32768) return 15;
    }
    return 0;
}

/* 16: 闯关环境感染者只从固定刷怪区生成。 */
static int test_campaign_spawn_zone(void)
{
    struct toy_game g;
    int i, in_zone = 0;
    toy_game_init(&g, 31);
    toy_game_set_world(&g, NULL, 0, ROOM);
    toy_game_set_campaign(&g, test_safe_rooms, 2, test_spawn_zones, 2);
    g.px = 0;
    g.pz = -4500;
    g.spawn_timer_ms = 0;
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (count_alive(&g) != 1) return 16;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        const struct toy_game_enemy *e = &g.enemies[i];
        int j;
        if (e->active != 1) continue;
        if (e->speed >= 76) return 16;
        for (j = 0; j < 2; j++)
            if (toy_game_point_in_box(e->x, e->z, &test_spawn_zones[j]))
                in_zone = 1;
    }
    return in_zone ? 0 : 16;
}

/* 17: 僵尸追到安全室边界后不能进入，也不能隔着边界伤害玩家。 */
static int test_safe_room_blocks_enemy(void)
{
    struct toy_game g;
    int i;
    toy_game_init(&g, 33);
    toy_game_set_world(&g, NULL, 0, ROOM);
    toy_game_set_campaign(&g, test_safe_rooms, 2, test_spawn_zones, 2);
    g.spawn_timer_ms = 1000000;
    g.px = 0;
    g.pz = -4500;
    toy_game_place_enemy(&g, 0, -3800);
    for (i = 0; i < 100; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.enemies[0].z < -3900) return 17;
    if (g.hp != 100) return 17;
    return 0;
}

/* 18: 在终点安全室连续停留 1.5 秒后通关并产生事件。 */
static int test_goal_hold_and_win(void)
{
    struct toy_game g;
    unsigned char evs[16];
    int n;
    toy_game_init(&g, 35);
    toy_game_set_world(&g, NULL, 0, ROOM);
    toy_game_set_campaign(&g, test_safe_rooms, 2, test_spawn_zones, 2);
    g.spawn_timer_ms = 1000000;
    g.px = 0;
    g.pz = 4500;
    toy_game_update(&g, NULL, 0, 0, 1024, 1000);
    g.pz = 0;
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.goal_hold_ms != 0) return 18;
    g.pz = 4500;
    toy_game_update(&g, NULL, 0, 0, 1024, 1490);
    if (g.state != TOY_GAME_PLAYING) return 18;
    toy_game_update(&g, NULL, 0, 0, 1024, 10);
    if (g.state != TOY_GAME_WON ||
        g.goal_hold_ms != TOY_GAME_GOAL_HOLD_MS) return 18;
    n = toy_game_drain_events(&g, evs, 16);
    if (!has_event(evs, n, TOY_GAME_EV_LEVEL_WON)) return 18;
    return 0;
}

/* 19: 警告区触发前专属刷怪区关闭，触发后限时开启且只触发一次。 */
static int test_alarm_spawn_window(void)
{
    static const struct toy_game_box warning = {-500, 500, -500, 500};
    struct toy_game g;
    unsigned char evs[16];
    int i, n;
    toy_game_init(&g, 37);
    toy_game_set_world(&g, NULL, 0, ROOM);
    toy_game_set_campaign(&g, test_safe_rooms, 2, &test_spawn_zones[1], 1);
    toy_game_set_alarm(&g, &warning, 0);
    g.px = 0;
    g.pz = -4500;
    g.spawn_timer_ms = 0;
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (count_alive(&g) != 0 || g.alarm_triggered) return 19;
    g.pz = 0;
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (count_alive(&g) != 1 || !g.alarm_triggered ||
        g.alarm_timer_ms <= 0) return 19;
    if (g.enemies[0].ai_state != TOY_GAME_ENEMY_CHASE ||
        g.enemies[0].last_seen_x != g.px ||
        g.enemies[0].last_seen_z != g.pz) return 19;
    n = toy_game_drain_events(&g, evs, 16);
    if (!has_event(evs, n, TOY_GAME_EV_ALARM_TRIGGERED)) return 19;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) g.enemies[i].active = 0;
    g.enemies_alive = 0;
    g.pz = -4500;
    toy_game_update(&g, NULL, 0, 0, 1024, TOY_GAME_ALARM_DURATION_MS);
    if (g.alarm_timer_ms != 0) return 19;
    g.spawn_timer_ms = 0;
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (count_alive(&g) != 0) return 19;
    return 0;
}

/* 20: 开局环境感染者用尽固定预算后进入 CALM，不再定时补充。 */
static int test_campaign_ambient_budget(void)
{
    struct toy_game g;
    int i;
    toy_game_init(&g, 39);
    toy_game_set_world(&g, NULL, 0, ROOM);
    toy_game_set_campaign(&g, test_safe_rooms, 2, test_spawn_zones, 2);
    g.px = 0;
    g.pz = -4500;
    for (i = 0; i < TOY_GAME_CAMPAIGN_AMBIENT_BUDGET; i++) {
        g.spawn_timer_ms = 0;
        toy_game_update(&g, NULL, 0, 0, 1024, 16);
    }
    if (count_alive(&g) != TOY_GAME_CAMPAIGN_AMBIENT_BUDGET) return 20;
    if (g.spawn_budget != 0 || g.campaign_phase != TOY_GAME_PHASE_CALM)
        return 20;
    for (i = 0; i < 100; i++)
        toy_game_update(&g, NULL, 0, 0, 1024, 100);
    if (count_alive(&g) != TOY_GAME_CAMPAIGN_AMBIENT_BUDGET) return 20;
    return 0;
}

/* 21: 警报尸潮有独立配额，配额耗尽后进入 RELAX 并禁止补怪。 */
static int test_alarm_budget_and_relax(void)
{
    static const struct toy_game_box warning = {-500, 500, -500, 500};
    struct toy_game g;
    int i;
    toy_game_init(&g, 41);
    toy_game_set_world(&g, NULL, 0, ROOM);
    toy_game_set_campaign(&g, test_safe_rooms, 2, &test_spawn_zones[1], 1);
    toy_game_set_alarm(&g, &warning, 0);
    g.px = 0;
    g.pz = 0;
    for (i = 0; i < TOY_GAME_ALARM_SPAWN_BUDGET; i++) {
        g.spawn_timer_ms = 0;
        toy_game_update(&g, NULL, 0, 0, 1024, 16);
    }
    if (count_alive(&g) != TOY_GAME_ALARM_SPAWN_BUDGET) return 21;
    if (g.spawn_budget != 0 || g.campaign_phase != TOY_GAME_PHASE_RELAX ||
        g.phase_timer_ms != TOY_GAME_CAMPAIGN_RELAX_MS) return 21;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) g.enemies[i].active = 0;
    g.enemies_alive = 0;
    g.spawn_timer_ms = 0;
    toy_game_update(&g, NULL, 0, 0, 1024, 1000);
    if (count_alive(&g) != 0 || g.campaign_phase != TOY_GAME_PHASE_RELAX)
        return 21;
    toy_game_update(&g, NULL, 0, 0, 1024, TOY_GAME_CAMPAIGN_RELAX_MS);
    if (count_alive(&g) != 0 || g.campaign_phase != TOY_GAME_PHASE_CALM)
        return 21;
    return 0;
}

/* 22: 尸潮达到存活上限时保留预算，腾出槽位后才继续生成。 */
static int test_alarm_active_limit(void)
{
    static const struct toy_game_box warning = {-500, 500, -500, 500};
    struct toy_game g;
    int i;
    toy_game_init(&g, 43);
    toy_game_set_world(&g, NULL, 0, ROOM);
    toy_game_set_campaign(&g, test_safe_rooms, 2, &test_spawn_zones[1], 1);
    toy_game_set_alarm(&g, &warning, 0);
    g.px = 0;
    g.pz = 0;
    for (i = 0; i < TOY_GAME_CAMPAIGN_ACTIVE_LIMIT; i++)
        toy_game_place_enemy(&g, 3000 + i * 10, 3000);
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (count_alive(&g) != TOY_GAME_CAMPAIGN_ACTIVE_LIMIT ||
        g.spawn_budget != TOY_GAME_ALARM_SPAWN_BUDGET) return 22;
    g.enemies[0].active = 0;
    g.enemies_alive--;
    g.spawn_timer_ms = 0;
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (count_alive(&g) != TOY_GAME_CAMPAIGN_ACTIVE_LIMIT ||
        g.spawn_budget != TOY_GAME_ALARM_SPAWN_BUDGET - 1) return 22;
    return 0;
}

/* 23: 远处背后的玩家不可见；转入正面后开始 NOTICE，极近背后仍能察觉。 */
static int test_enemy_view_cone(void)
{
    struct toy_game g;
    int i;
    toy_game_init(&g, 45);
    toy_game_set_world(&g, NULL, 0, ROOM);
    g.px = 1000;
    g.pz = 0;
    toy_game_place_enemy(&g, 0, 0);
    g.enemies[0].dir_x = -1024;
    g.enemies[0].dir_z = 0;
    g.enemies[0].wander_timer_ms = 100000;
    for (i = 0; i < 100; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.enemies[0].ai_state != TOY_GAME_ENEMY_IDLE) return 23;
    g.enemies[0].dir_x = 1024;
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.enemies[0].ai_state != TOY_GAME_ENEMY_NOTICE) return 23;

    toy_game_init(&g, 47);
    toy_game_set_world(&g, NULL, 0, ROOM);
    g.px = 300;
    g.pz = 0;
    toy_game_place_enemy(&g, 0, 0);
    g.enemies[0].dir_x = -1024;
    g.enemies[0].dir_z = 0;
    g.enemies[0].wander_timer_ms = 100000;
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.enemies[0].ai_state != TOY_GAME_ENEMY_NOTICE) return 23;
    return 0;
}

/* 24: 障碍物切断视觉，感染者不能隔墙进入 NOTICE。 */
static int test_enemy_sight_occlusion(void)
{
    static const struct toy_game_box wall = {400, 600, -500, 500};
    struct toy_game g;
    int i;
    toy_game_init(&g, 49);
    toy_game_set_world(&g, &wall, 1, ROOM);
    g.px = 1000;
    g.pz = 0;
    toy_game_place_enemy(&g, 0, 0);
    g.enemies[0].dir_x = 1024;
    g.enemies[0].dir_z = 0;
    g.enemies[0].wander_timer_ms = 100000;
    for (i = 0; i < 100; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.enemies[0].ai_state != TOY_GAME_ENEMY_IDLE) return 24;
    return 0;
}

/* 25: 枪声穿过视觉遮挡，只让范围内敌人调查开枪位置。 */
static int test_gunshot_investigation(void)
{
    static const struct toy_game_box wall = {400, 600, -500, 500};
    struct toy_game g;
    toy_game_init(&g, 51);
    toy_game_set_world(&g, &wall, 1, ROOM);
    g.px = 0;
    g.pz = 0;
    toy_game_place_enemy(&g, 1000, 0);
    g.enemies[0].dir_x = 1024;
    g.enemies[0].dir_z = 0;
    toy_game_update(&g, NULL, 1, 0, -1024, 16);
    if (g.enemies[0].ai_state != TOY_GAME_ENEMY_INVESTIGATE) return 25;
    if (g.enemies[0].target_x != 0 || g.enemies[0].target_z != 0) return 25;
    return 0;
}

/* 26: 局部同伴一起 ALERT 并追玩家，但传播后的启动延迟略长。 */
static int test_local_alert_propagation(void)
{
    struct toy_game g;
    unsigned char evs[16];
    int n;
    toy_game_init(&g, 53);
    toy_game_set_world(&g, NULL, 0, ROOM);
    g.px = 1000;
    g.pz = 0;
    toy_game_place_enemy(&g, 0, 0);
    toy_game_place_enemy(&g, 0, 1000);
    toy_game_place_enemy(&g, 0, 2500);
    g.enemies[0].ai_state = TOY_GAME_ENEMY_NOTICE;
    g.enemies[0].ai_timer_ms = 1;
    g.enemies[0].dir_x = 1024;
    g.enemies[0].dir_z = 0;
    g.enemies[1].dir_x = -1024;
    g.enemies[1].dir_z = 0;
    g.enemies[2].dir_x = -1024;
    g.enemies[2].dir_z = 0;
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.enemies[0].ai_state != TOY_GAME_ENEMY_ALERT) return 26;
    if (g.enemies[1].ai_state != TOY_GAME_ENEMY_ALERT) return 26;
    if (g.enemies[2].ai_state != TOY_GAME_ENEMY_ALERT) return 26;
    if (g.enemies[1].ai_timer_ms <= g.enemies[0].ai_timer_ms ||
        g.enemies[2].ai_timer_ms <= g.enemies[1].ai_timer_ms) return 26;
    if (g.enemies[1].target_x != g.px || g.enemies[1].target_z != g.pz ||
        g.enemies[2].target_x != g.px || g.enemies[2].target_z != g.pz)
        return 26;
    n = toy_game_drain_events(&g, evs, 16);
    if (!has_event(evs, n, TOY_GAME_EV_ENEMY_ALERT)) return 26;
    return 0;
}

/* 27: 低压力时导演补充 2~4 只；安全室内暂停遭遇倒计时。 */
static int test_director_small_group(void)
{
    struct toy_game g;
    int i, calm_timer, group_budget;
    toy_game_init(&g, 55);
    toy_game_set_world(&g, NULL, 0, ROOM);
    toy_game_set_campaign(&g, test_safe_rooms, 2, test_spawn_zones, 2);
    g.px = 0;
    g.pz = -4500;
    for (i = 0; i < TOY_GAME_CAMPAIGN_AMBIENT_BUDGET; i++) {
        g.spawn_timer_ms = 0;
        toy_game_update(&g, NULL, 0, 0, 1024, 16);
    }
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) g.enemies[i].active = 0;
    g.enemies_alive = 0;
    calm_timer = g.phase_timer_ms;
    toy_game_update(&g, NULL, 0, 0, 1024, 1000);
    if (g.phase_timer_ms != calm_timer || g.director_encounters != 0) return 27;

    g.pz = 0;
    g.phase_timer_ms = 1;
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.campaign_phase != TOY_GAME_PHASE_BUILDUP ||
        g.director_encounters != 1 || count_alive(&g) != 1) return 27;
    group_budget = g.spawn_budget + 1;
    if (group_budget < TOY_GAME_DIRECTOR_MIN_GROUP ||
        group_budget > TOY_GAME_DIRECTOR_MAX_GROUP) return 27;
    while (g.campaign_phase == TOY_GAME_PHASE_BUILDUP) {
        g.spawn_timer_ms = 0;
        toy_game_update(&g, NULL, 0, 0, 1024, 16);
    }
    if (count_alive(&g) != group_budget ||
        g.campaign_phase != TOY_GAME_PHASE_CALM) return 27;
    return 0;
}

/* 28: CHASE 丢失视线后只追最后目击点，搜索失败回到 IDLE；可重新发现。 */
static int test_last_seen_and_search(void)
{
    static const struct toy_game_box wall = {1100, 1300, -5700, 5700};
    struct toy_game g;
    int i;
    toy_game_init(&g, 57);
    toy_game_set_world(&g, NULL, 0, ROOM);
    g.px = 1000;
    g.pz = 0;
    toy_game_place_enemy(&g, 0, 0);
    g.enemies[0].ai_state = TOY_GAME_ENEMY_CHASE;
    g.enemies[0].dir_x = 1024;
    g.enemies[0].dir_z = 0;
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.enemies[0].last_seen_x != 1000) return 28;
    toy_game_set_world(&g, &wall, 1, ROOM);
    g.px = 2000;
    for (i = 0; i < 180; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.enemies[0].ai_state != TOY_GAME_ENEMY_SEARCH) return 28;
    if (g.enemies[0].last_seen_x != 1000 || g.enemies[0].x >= 1100) return 28;
    for (i = 0; i < 260; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.enemies[0].ai_state != TOY_GAME_ENEMY_IDLE) return 28;

    toy_game_set_world(&g, NULL, 0, ROOM);
    g.enemies[0].dir_x = 1024;
    g.enemies[0].dir_z = 0;
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.enemies[0].ai_state != TOY_GAME_ENEMY_NOTICE ||
        g.enemies[0].last_seen_x != 2000) return 28;
    return 0;
}

/* 29: 武器槽规则：手枪默认槽 2、主武器拾取/换弹/全自动/霰弹散射 */
static int test_weapon_slots(void)
{
    struct toy_game g;
    unsigned char keys[TOY_GAME_KEY_RELOAD + 1];
    int i;
    toy_game_init(&g, 29);
    toy_game_set_world(&g, world, 3, ROOM);
    g.px = 0;
    g.pz = 0;
    if (g.current_slot != 1 || g.slots[1].weapon != TOY_GAME_WEAPON_PISTOL ||
        g.slots[1].mag != 30 ||
        g.slots[1].reserve != TOY_GAME_AMMO_INFINITE ||
        g.slots[0].weapon != -1) return 29;
    if (toy_game_switch_weapon(&g, 0)) return 29;
    /* 拾取 SMG：50/650 并自动切出；同武器再拾取 = 补充弹药 */
    if (toy_game_equip_weapon(&g, TOY_GAME_WEAPON_SMG) != 1) return 29;
    if (g.current_slot != 0 || g.slots[0].mag != 50 ||
        g.slots[0].reserve != 650) return 29;
    g.slots[0].mag = 7;
    g.slots[0].reserve = 120;
    if (toy_game_equip_weapon(&g, TOY_GAME_WEAPON_SMG) != 0 ||
        g.slots[0].mag != 50 || g.slots[0].reserve != 650) return 29;
    /* 切枪保留各自弹药状态；弹药盒补备弹不碰弹匣 */
    if (!toy_game_switch_weapon(&g, 1) || g.slots[1].mag != 30) return 29;
    if (!toy_game_switch_weapon(&g, 0) ||
        g.slots[0].mag != 50 || g.slots[0].reserve != 650) return 29;
    g.slots[0].mag = 3;
    g.slots[0].reserve = 5;
    if (!toy_game_refill_ammo(&g) || g.slots[0].reserve != 650 ||
        g.slots[0].mag != 3) return 29;
    /* SMG 全自动：按住 10 步（160ms，100ms 冷却）消耗 2 发 */
    g.slots[0].mag = 50;
    for (i = 0; i < 10; i++)
        toy_game_update_held(&g, NULL, 0, 1, 0, 1024, 16);
    if (g.slots[0].mag != 48) return 29;
    /* 手枪半自动：按住不连发 */
    toy_game_switch_weapon(&g, 1);
    for (i = 0; i < 5; i++)
        toy_game_update_held(&g, NULL, 0, 1, 0, 1024, 16);
    if (g.slots[1].mag != 30) return 29;
    /* 有限备弹换弹：SMG 2000ms，消耗备弹 */
    memset(keys, 0, sizeof(keys));
    keys[TOY_GAME_KEY_RELOAD] = 1;
    toy_game_switch_weapon(&g, 0);
    toy_game_update(&g, keys, 0, 0, 1024, 16);
    if (!g.reloading) return 29;
    for (i = 0; i < 125; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.reloading || g.slots[0].mag != 50 ||
        g.slots[0].reserve != 648) return 29;
    /* 霰弹枪 8/64：4 弹丸随机散射，整匣内应放倒并排两敌 */
    if (toy_game_equip_weapon(&g, TOY_GAME_WEAPON_SHOTGUN) != 1 ||
        g.slots[0].mag != 8 || g.slots[0].reserve != 64) return 29;
    memset(g.enemies, 0, sizeof(g.enemies));
    g.enemies[0].active = 1;
    g.enemies[0].x = 120;
    g.enemies[0].z = 900;
    g.enemies[1].active = 1;
    g.enemies[1].x = -120;
    g.enemies[1].z = 900;
    g.enemies_alive = 2;
    g.kills = 0;
    {
        unsigned int seq_before = g.fire_seq;
        for (i = 0; i < 8 && g.kills < 2; i++)
            toy_game_fire(&g, 0, 1024);
        if (g.kills != 2 || g.slots[0].mag != 8 - i) return 29;
        /* 弹道记录：每枪 4 条射线，方向已归一化，终点不越最大射程 */
        if (g.ray_count != 4 ||
            g.fire_seq != seq_before + (unsigned int)i) return 29;
        for (i = 0; i < g.ray_count; i++) {
            const struct toy_game_ray *r = &g.rays[i];
            long long len_sq = (long long)r->sy * r->sy +
                               (long long)r->cy * r->cy;
            if (len_sq < 1022 * 1022 || len_sq > 1026 * 1026) return 29;
            if (r->ex < -TOY_GAME_MAX_RANGE || r->ex > TOY_GAME_MAX_RANGE ||
                r->ez < -TOY_GAME_MAX_RANGE || r->ez > TOY_GAME_MAX_RANGE) return 29;
        }
    }
    return 0;
}

/* 30: 尸潮召唤：数量/位置合法、全部持续追踪、均匀落在所选刷怪点内；
 * 隔墙遮挡不丢失目标、不转入搜索；死亡结算后不可再召唤。 */
static int test_horde_tracking(void)
{
    static const struct toy_game_box wall = {-600, -400, -500, 500};
    static const struct toy_game_box points[3] = {
        {-3000, -2600, 1000, 1400}, {-2400, -2000, 2200, 2600},
        {-1400, -1000, 1000, 3000},
    };
    struct toy_game g;
    int i, n, spawned;
    toy_game_init(&g, 61);
    toy_game_set_world(&g, &wall, 1, ROOM);
    g.px = 0;
    g.pz = 0;
    spawned = toy_game_spawn_horde(&g, 15, 20, points, 3, 700);
    if (spawned < 15 || spawned > 20) return 30;
    n = count_alive(&g);
    if (n != spawned || g.enemies_alive != spawned) return 30;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        const struct toy_game_enemy *e = &g.enemies[i];
        if (e->active != 1) continue;
        if (e->ai_state != TOY_GAME_ENEMY_TRACKING) return 30;
        if (!toy_game_point_in_box(e->x, e->z, &points[0]) &&
            !toy_game_point_in_box(e->x, e->z, &points[1]) &&
            !toy_game_point_in_box(e->x, e->z, &points[2])) return 30;
    }
    /* 玩家与尸潮之间隔墙：追踪者无视视线遮挡，状态永不退化，直扑玩家
     * 并最终咬到（HP 下降证明穿越了遮挡）。 */
    for (i = 0; i < 200; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        const struct toy_game_enemy *e = &g.enemies[i];
        if (e->active != 1) continue;
        if (e->ai_state != TOY_GAME_ENEMY_TRACKING) return 30;
    }
    if (g.hp >= 100) return 30;
    /* 游戏结束后召唤无效 */
    g.state = TOY_GAME_OVER;
    if (toy_game_spawn_horde(&g, 15, 20, points, 3, 700) != 0)
        return 30;
    return 0;
}

/* 31: AI 队友固定位置、身份名、同一套 SMG 开火/无限备弹换弹规则。 */
static int test_ai_teammate(void)
{
    struct toy_game g;
    int i;
    toy_game_init(&g, 67);
    toy_game_set_world(&g, NULL, 0, ROOM);
    if (!g.ai_active || g.ai_x != -11000 || g.ai_z != -5800 ||
        strcmp(g.ai_name, "Jesus") != 0 ||
        g.ai_slots[0].weapon != TOY_GAME_WEAPON_SMG ||
        g.ai_slots[0].reserve != TOY_GAME_AMMO_INFINITE) return 31;
    g.px = 0; g.pz = 5000;
    g.enemies[0].active = 1;
    g.enemies[0].x = 0; g.enemies[0].z = -4500;
    g.enemies[0].hp = 1;
    g.enemies_alive = 1;
    g.ai_slots[0].mag = 1;
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.enemies[0].active != 2 || !g.ai_reloading) return 31;
    for (i = 0; i < 130; i++)
        toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.ai_reloading || g.ai_slots[0].mag != 50 ||
        g.px != 0 || g.pz != 5000) return 31;

    /* 敌人也能把 AI 选为目标；倒地后完成 3 秒复活。 */
    memset(g.enemies, 0, sizeof(g.enemies));
    g.enemies[0].active = 1;
    g.enemies[0].x = -10800;
    g.enemies[0].z = -5800;
    g.enemies[0].ai_state = TOY_GAME_ENEMY_CHASE;
    g.enemies[0].bite_cooldown_ms = 0;
    g.enemies[0].target_player = -1;
    g.enemies[0].retarget_timer_ms = 0;
    g.enemies_alive = 1;
    g.ai_hp = 2;
    g.ai_down = 0;
    g.ai_slots[0].mag = 0;
    g.ai_reloading = 1;
    g.ai_reload_timer_ms = 2000;
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (!g.ai_down || g.ai_hp != 0) return 31;
    g.enemies[0].active = 0;
    g.enemies_alive = 0;
    for (i = 0; i < 188; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (!g.ai_down) return 31;
    for (i = 0; i < 188; i++) toy_game_revive_ai(&g, 16);
    if (g.ai_down || g.ai_hp != TOY_GAME_REVIVE_HP) return 31;
    return 0;
}

int main(void)
{
    if (test_prng_determinism()) return 1;
    if (test_wave_spawn()) return 2;
    if (test_chase_player()) return 3;
    if (test_wall_blocks()) return 4;
    if (test_bite_damage()) return 5;
    if (test_kill_and_removal()) return 6;
    if (test_wave_progression()) return 7;
    if (test_hit_direction()) return 8;
    if (test_hit_priority()) return 9;
    if (test_obstacle_occlusion()) return 10;
    if (test_ammo_reload()) return 11;
    if (test_fire_rate()) return 12;
    if (test_game_over()) return 13;
    if (test_capacity()) return 14;
    if (test_sfx()) return 15;
    if (test_campaign_spawn_zone()) return 16;
    if (test_safe_room_blocks_enemy()) return 17;
    if (test_goal_hold_and_win()) return 18;
    if (test_alarm_spawn_window()) return 19;
    if (test_campaign_ambient_budget()) return 20;
    if (test_alarm_budget_and_relax()) return 21;
    if (test_alarm_active_limit()) return 22;
    if (test_enemy_view_cone()) return 23;
    if (test_enemy_sight_occlusion()) return 24;
    if (test_gunshot_investigation()) return 25;
    if (test_local_alert_propagation()) return 26;
    if (test_director_small_group()) return 27;
    if (test_last_seen_and_search()) return 28;
    if (test_weapon_slots()) return 29;
    if (test_horde_tracking()) return 30;
    if (test_ai_teammate()) return 31;
    return 0;
}
