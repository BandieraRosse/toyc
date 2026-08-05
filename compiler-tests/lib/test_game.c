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

/* 3: 追玩家（无障碍时距离单调收敛到攻击范围） */
static int test_chase_player(void)
{
    struct toy_game g;
    int i;
    long long dist;
    toy_game_init(&g, 3);
    toy_game_set_world(&g, NULL, 0, ROOM);
    g.px = 0;
    g.pz = 0;
    toy_game_place_enemy(&g, 2000, 0);
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
    for (i = 0; i < 120; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    /* 逐轴滑动允许圆缘擦过障碍（x≈750 贴墙），但中心绝不可越过 maxx 穿墙 */
    if (g.enemies[0].x < 1800) return 4;
    return 0;
}

/* 5: 攻击扣血 + 独立冷却（1000ms 只咬一次） */
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
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.hp != 85) return 5;
    n = toy_game_drain_events(&g, evs, 16);
    if (!has_event(evs, n, TOY_GAME_EV_BITE)) return 5;
    for (i = 0; i < 10; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.hp != 85) return 5;
    for (i = 0; i < 60; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.hp != 70) return 5;
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
    if (g.ammo_mag != 0 || g.ammo_reserve != TOY_GAME_AMMO_INFINITE ||
        !g.reloading) return 11;
    n = toy_game_drain_events(&g, evs, 16);
    if (!has_event(evs, n, TOY_GAME_EV_RELOAD_START)) return 11;
    for (i = 0; i < 94; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.reloading) return 11;
    if (g.ammo_mag != 30 ||
        g.ammo_reserve != TOY_GAME_AMMO_INFINITE) return 11;
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
    if (g.ammo_mag != 29) return 12;
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    toy_game_update(&g, NULL, 1, 0, 1024, 16);
    if (g.ammo_mag != 29) return 12;
    for (i = 0; i < 13; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    toy_game_update(&g, NULL, 1, 0, 1024, 16);
    if (g.ammo_mag != 28) return 12;
    return 0;
}

/* 13: 血量归零 → GAME_OVER 冻结 */
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
    toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.state != TOY_GAME_OVER || g.hp != 0) return 13;
    n = toy_game_drain_events(&g, evs, 16);
    if (!has_event(evs, n, TOY_GAME_EV_PLAYER_DEATH)) return 13;
    for (i = 0; i < 100; i++) toy_game_update(&g, NULL, 0, 0, 1024, 16);
    if (g.hp != 0 || g.state != TOY_GAME_OVER) return 13;
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
    int i, nonzero = 0, peak = 0;
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
        if (s < 0) s = -s;
        if (s > peak) peak = s;
    }
    if (peak > 32767) return 15;
    return 0;
}

/* 16: 闯关模式只从固定刷怪区生成，且玩家速度配置高于僵尸。 */
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
    return 0;
}
