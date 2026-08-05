/*
 * toy_game — 平台无关的僵尸潮射击游戏规则（供 wayland_fps 等窗口游戏使用）。
 *
 * 职责边界：本库只包含游戏规则 —— PRNG、世界碰撞查询、僵尸 AI/攻击、
 * 波次或闯关刷怪、安全室/终点、hitscan 射击与障碍遮挡、弹匣/换弹、玩家生命/死亡、事件队列
 * （供音效与 HUD 消费）。纯整数运算、零系统调用、零内存分配（固定数组），
 * 不依赖窗口/输入/渲染设施，可无窗口测试。
 *
 * 约定：所有计时字段单位 ms；朝向 sy/cy 为 1024 基准定点（同 wayland_fps
 * 的 camera）；玩家位置 px/pz 由宿主每帧同步。
 */

#ifndef TOYC_TOY_GAME_H
#define TOYC_TOY_GAME_H

#include "tlibc_types.h"

#define TOY_GAME_MAX_ENEMIES    32
#define TOY_GAME_MAG_SIZE       30
#define TOY_GAME_AMMO_INFINITE  (-1)
#define TOY_GAME_RELOAD_MS      1500
#define TOY_GAME_BITE_MS        1000
#define TOY_GAME_BITE_DAMAGE    2
#define TOY_GAME_FIRE_COOLDOWN_MS 200
#define TOY_GAME_MUZZLE_FLASH_MS 80
#define TOY_GAME_DAMAGE_FLASH_MS 250
#define TOY_GAME_DYING_MS       400
#define TOY_GAME_WAVE_FIRST_DELAY_MS 1500
#define TOY_GAME_WAVE_PAUSE_MS  2500
#define TOY_GAME_SPAWN_INTERVAL_MS 500
#define TOY_GAME_CAMPAIGN_AMBIENT_BUDGET 10
#define TOY_GAME_CAMPAIGN_AMBIENT_SPAWN_INTERVAL_MS 100
#define TOY_GAME_ALARM_SPAWN_INTERVAL_MS 1200
#define TOY_GAME_ALARM_DURATION_MS 20000
#define TOY_GAME_ALARM_SPAWN_BUDGET 16
#define TOY_GAME_CAMPAIGN_ACTIVE_LIMIT 20
#define TOY_GAME_CAMPAIGN_RELAX_MS 12000
#define TOY_GAME_DIRECTOR_MIN_DELAY_MS 7000
#define TOY_GAME_DIRECTOR_MAX_DELAY_MS 12000
#define TOY_GAME_DIRECTOR_MIN_GROUP 2
#define TOY_GAME_DIRECTOR_MAX_GROUP 4
#define TOY_GAME_DIRECTOR_ALIVE_LOW 5
#define TOY_GAME_DIRECTOR_ATTACKER_LOW 2
#define TOY_GAME_MAX_EVENTS     16

#define TOY_GAME_ENEMY_RADIUS   100     /* 敌人碰撞半径 */
#define TOY_GAME_HIT_RADIUS     150     /* 命中判定半径（覆盖渲染 box 半宽） */
#define TOY_GAME_ATTACK_RANGE   300     /* 敌我距离小于此值开始咬 */
#define TOY_GAME_ENEMY_HALF     120     /* 渲染 box 半宽（宿主用） */
#define TOY_GAME_ENEMY_HEIGHT   350     /* 敌人模型顶部略高于玩家视角 */
#define TOY_GAME_SPAWN_EDGE     250     /* 生成点距房间边界内缩量 */
#define TOY_GAME_MIN_SPAWN_DIST 1200    /* 生成点距玩家最小距离（防贴脸） */
#define TOY_GAME_GOAL_HOLD_MS   1500    /* 终点安全室内停留多久判定通关 */
#define TOY_GAME_DETECT_RANGE   2800    /* 视觉最远察觉距离 */
#define TOY_GAME_CLOSE_DETECT_RANGE 600 /* 极近距离无需处于正面视野 */
#define TOY_GAME_NOTICE_MIN_MS  700     /* 进入范围后至少观察多久 */
#define TOY_GAME_NOTICE_MAX_MS  1400
#define TOY_GAME_ALERT_MS       700     /* 发现玩家后转向、显示感叹号的停顿 */
#define TOY_GAME_GUNSHOT_RANGE  3600    /* 枪声调查半径，允许绕过视觉遮挡 */
#define TOY_GAME_LOCAL_ALERT_RANGE 1800 /* 尖叫直接警觉半径 */
#define TOY_GAME_LOCAL_ALERT_MAX_RANGE 3200
#define TOY_GAME_PROPAGATED_ALERT_MIN_MS 850
#define TOY_GAME_PROPAGATED_ALERT_MAX_MS 1100
#define TOY_GAME_INVESTIGATE_MS 5000
#define TOY_GAME_SEARCH_MS      4000

#define TOY_GAME_KEY_RELOAD     19      /* evdev KEY_R */

enum toy_game_state { TOY_GAME_PLAYING, TOY_GAME_OVER, TOY_GAME_WON };

enum toy_game_campaign_phase {
    TOY_GAME_PHASE_CALM,
    TOY_GAME_PHASE_BUILDUP,
    TOY_GAME_PHASE_HORDE,
    TOY_GAME_PHASE_RELAX
};

enum toy_game_enemy_ai {
    TOY_GAME_ENEMY_IDLE,
    TOY_GAME_ENEMY_NOTICE,
    TOY_GAME_ENEMY_INVESTIGATE,
    TOY_GAME_ENEMY_ALERT,
    TOY_GAME_ENEMY_CHASE,
    TOY_GAME_ENEMY_SEARCH
};

enum toy_game_event {
    TOY_GAME_EV_SHOOT,
    TOY_GAME_EV_DRY_FIRE,
    TOY_GAME_EV_RELOAD_START,
    TOY_GAME_EV_RELOAD_DONE,
    TOY_GAME_EV_KILL,
    TOY_GAME_EV_BITE,
    TOY_GAME_EV_PLAYER_DEATH,
    TOY_GAME_EV_WAVE_START,
    TOY_GAME_EV_LEVEL_WON,
    TOY_GAME_EV_ALARM_TRIGGERED,
    TOY_GAME_EV_ENEMY_ALERT,
};

/* 碰撞/命中共用的 xz 平面轴对齐盒（与房间障碍物同尺度） */
struct toy_game_box { int minx, maxx, minz, maxz; };

struct toy_game_enemy {
    int active;         /* 0=空槽 1=存活 2=倒地中 */
    int x, z;           /* 世界坐标（xz 平面） */
    int speed;          /* 每 16ms 逻辑步移动单位 */
    int hp;
    int bite_cooldown_ms;
    int flash;          /* 命中闪白计时 */
    int hurt;           /* 受击闪红计时 */
    int dying_ms;       /* 倒地压扁计时 */
    int ai_state;       /* enum toy_game_enemy_ai */
    int ai_timer_ms;    /* 侦测/警觉阶段倒计时 */
    int target_x, target_z; /* 调查目标或最后声源 */
    int last_seen_x, last_seen_z;
    int lost_sight_ms;
    int wander_timer_ms;
    int dir_x, dir_z;   /* 面向，1024 基准定点 */
};

struct toy_game {
    /* 玩家 */
    int px, pz;         /* 宿主每帧同步相机位置 */
    int hp;
    int state;          /* enum toy_game_state */
    int ammo_mag, ammo_reserve;
    int reloading, reload_timer_ms;
    int fire_cooldown_ms;
    int muzzle_flash_ms;
    int damage_flash_ms;
    int kills;

    /* 敌人与波次 */
    struct toy_game_enemy enemies[TOY_GAME_MAX_ENEMIES];
    int wave;
    int to_spawn;       /* 本波待生成配额 */
    int spawn_timer_ms;
    int enemies_alive;

    /* 经典闯关模式：固定刷怪区，安全室对敌人视为禁区。 */
    const struct toy_game_box *safe_rooms;
    int safe_room_count;
    const struct toy_game_box *spawn_zones;
    int spawn_zone_count;
    int campaign_mode;
    int campaign_phase;
    int spawn_budget;
    int phase_timer_ms;
    int active_attackers;
    int director_encounters;
    int goal_hold_ms;
    const struct toy_game_box *alarm_zone;
    int alarm_spawn_zone;
    int alarm_triggered;
    int alarm_timer_ms;

    /* 世界（宿主所有，只读借用） */
    const struct toy_game_box *world;
    int world_count;
    int room_limit;

    /* PRNG（xorshift64*，init 时播种） */
    uint64_t rng;

    /* 本帧事件队列（宿主每帧 drain） */
    int event_count;
    unsigned char events[TOY_GAME_MAX_EVENTS];
};

void toy_game_init(struct toy_game *g, uint64_t seed);      /* 初始化/重开共用 */
void toy_game_set_world(struct toy_game *g,
                        const struct toy_game_box *boxes,
                        int box_count, int room_limit);
void toy_game_set_campaign(struct toy_game *g,
                           const struct toy_game_box *safe_rooms,
                           int safe_room_count,
                           const struct toy_game_box *spawn_zones,
                           int spawn_zone_count);
void toy_game_set_alarm(struct toy_game *g,
                        const struct toy_game_box *alarm_zone,
                        int spawn_zone_index);
int  toy_game_point_in_box(int x, int z, const struct toy_game_box *box);
int  toy_game_position_blocked(const struct toy_game *g,
                               int x, int z, int radius);
void toy_game_update(struct toy_game *g,
                     const unsigned char *keys_pressed,     /* 可 NULL */
                     int fire_pressed, int sy, int cy, int dt_ms);
int  toy_game_fire(struct toy_game *g, int sy, int cy);     /* hitscan，命中返回 1 */
int  toy_game_drain_events(struct toy_game *g, unsigned char *out, int max);
void toy_game_place_enemy(struct toy_game *g, int x, int z); /* 测试钩子 */

/* ── SFX 引擎（程序合成，平台无关，可无设备单测） ──────────────── */

#define TOY_SFX_RATE        44100
#define TOY_SFX_MAX_VOICES  8

enum toy_sfx_kind {
    TOY_SFX_GUNSHOT,
    TOY_SFX_DRY_FIRE,
    TOY_SFX_RELOAD_START,
    TOY_SFX_RELOAD_DONE,
    TOY_SFX_HIT_MARKER,
    TOY_SFX_KILL,
    TOY_SFX_BITE,
    TOY_SFX_PLAYER_DEATH,
};

/* 每个 kind 可注册一段 PCM16 样本（TSND 资产）替代程序合成 */
struct toy_sfx_sample {
    const short *data;   /* NULL = 回退程序合成 */
    unsigned frames;     /* 帧数；rate 必须与 sfx->rate 一致（无重采样） */
};

struct toy_sfx_voice {
    int active;
    int kind;
    int pos, len;       /* 已生成/总样本数 */
    const short *sample; /* 非 NULL = 样本模式，直接播 PCM16 资产 */
    int phase;          /* DDS 相位累加器（16.16 定点） */
    int step0, step1;   /* 相位增量扫频端点（16.16 定点） */
    int step;           /* 当前相位增量 */
    int vol;            /* 初始振幅（样本单位 0-32768） */
    uint32_t seed;      /* 噪声源（xorshift32） */
    int lp;             /* 一阶低通状态（噪声上色） */
};

struct toy_sfx {
    int rate;
    int enabled;        /* 设备不可用时可关闭，渲染输出静音 */
    int music_enabled;  /* 程序化背景音乐，与 SFX 在同一 PCM 流混音 */
    unsigned int music_pos;
    unsigned int melody_phase;
    unsigned int bass_phase;
    struct toy_sfx_sample samples[TOY_SFX_PLAYER_DEATH + 1];
    struct toy_sfx_voice voices[TOY_SFX_MAX_VOICES];
};

void toy_sfx_init(struct toy_sfx *sfx, int rate);
void toy_sfx_play(struct toy_sfx *sfx, int kind);       /* 触发；满 8 voice 偷剩余最短者 */
void toy_sfx_set_sample(struct toy_sfx *sfx, int kind, const short *pcm, unsigned frames);
                                                        /* 注册样本音色；pcm/frames 为空则回退程序合成 */
void toy_sfx_music(struct toy_sfx *sfx, int enabled);
void toy_sfx_render(struct toy_sfx *sfx, short *out, int frames); /* 混音至 S16 立体声 */

#endif /* TOYC_TOY_GAME_H */
