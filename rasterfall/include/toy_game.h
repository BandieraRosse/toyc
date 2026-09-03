/*
 * toy_game — 平台无关的僵尸潮射击游戏规则（供 Rasterfall 等窗口游戏使用）。
 *
 * 职责边界：本库只包含游戏规则 —— PRNG、世界碰撞查询、僵尸 AI/攻击、
 * 波次或闯关刷怪、安全室/终点、hitscan 射击与障碍遮挡、弹匣/换弹、玩家生命/死亡、事件队列
 * （供音效与 HUD 消费）。纯整数运算、零系统调用、零内存分配（固定数组），
 * 不依赖窗口/输入/渲染设施，可无窗口测试。
 *
 * 约定：所有计时字段单位 ms；朝向 sy/cy 为 1024 基准定点（同 Rasterfall
 * 的 camera）；玩家位置 px/pz 由宿主每帧同步。
 */

#ifndef TOYC_TOY_GAME_H
#define TOYC_TOY_GAME_H

#include "tlibc_types.h"
#include "toy_game_config.h"
#include "rasterfall_colors.h"

#define TOY_GAME_MAX_ENEMIES    64
#define TOY_GAME_MAX_WAVE_QUEUE 512
#define TOY_GAME_MAX_ACTORS     64
#define TOY_GAME_MAX_PLAYERS    4
#define TOY_GAME_CHARACTER_COUNT 4 /* stable IDs interpreted by presentation */
#define TOY_GAME_REMOTE_ACTOR_BASE \
    (TOY_GAME_MAX_ACTORS - TOY_GAME_MAX_PLAYERS)
#define TOY_GAME_MAX_PLATFORMS  64
#define TOY_GAME_AMMO_INFINITE  (-1)
#define TOY_GAME_BITE_MS        1000
#define TOY_GAME_BITE_DAMAGE    2
#define TOY_GAME_MUZZLE_FLASH_MS 80
#define TOY_GAME_DAMAGE_FLASH_MS 250
#define TOY_GAME_DYING_MS       400
#define TOY_GAME_REVIVE_MS      3000
#define TOY_GAME_REVIVE_HP      100
#define TOY_GAME_WAVE_FIRST_DELAY_MS 512000
#define TOY_GAME_WAVE_PAUSE_MS  512000
#define TOY_GAME_WAVE_ANNOUNCE_MS 2000
#define TOY_GAME_WAVE_MAX 10
#define TOY_GAME_WAVE_SCALE_PERCENT 100 /* 袭击规模：100%=1 倍 */
#define TOY_GAME_CAMPAIGN_AMBIENT_BUDGET 10
#define TOY_GAME_CAMPAIGN_AMBIENT_SPAWN_INTERVAL_MS 100
#define TOY_GAME_ALARM_SPAWN_INTERVAL_MS 1200
#define TOY_GAME_ALARM_DURATION_MS 20000
#define TOY_GAME_ALARM_SPAWN_BUDGET 16
#define TOY_GAME_CAMPAIGN_ACTIVE_LIMIT 40
#define TOY_GAME_CAMPAIGN_RELAX_MS 12000
#define TOY_GAME_DIRECTOR_MIN_DELAY_MS 7000
#define TOY_GAME_DIRECTOR_MAX_DELAY_MS 12000
#define TOY_GAME_DIRECTOR_MIN_GROUP 2
#define TOY_GAME_DIRECTOR_MAX_GROUP 4
#define TOY_GAME_DIRECTOR_ALIVE_LOW 5
#define TOY_GAME_DIRECTOR_ATTACKER_LOW 2
#define TOY_GAME_MAX_EVENTS     16
#define TOY_GAME_PLAYER_HP      TOY_CONFIG_PLAYER_HP
#define TOY_GAME_SECONDARY_PLAYER_HP TOY_CONFIG_SECONDARY_PLAYER_HP

#define TOY_GAME_ENEMY_RADIUS   130     /* 敌人碰撞半径 */
#define TOY_GAME_CHARGER_RADIUS 145    /* Charger 略大的专用碰撞半径 */
#define TOY_GAME_TANK_RADIUS    210    /* Tank 宽大的专用碰撞半径 */
#define TOY_GAME_PLAYER_RADIUS  180
#define TOY_GAME_HIT_RADIUS     150     /* 命中判定半径（覆盖渲染 box 半宽） */
#define TOY_GAME_ATTACK_RANGE   300     /* 敌我距离小于此值开始咬 */
#define TOY_GAME_SHOVE_PUSH     500     /* 推开位移（沿面朝方向） */
#define TOY_GAME_SHOVE_CONE     2       /* 面前扇形 120°：点积*2 >= 距离 */
#define TOY_GAME_ENEMY_HALF     120     /* 渲染 box 半宽（宿主用） */
#define TOY_GAME_ENEMY_HEIGHT   350     /* 敌人模型顶部略高于玩家视角 */
#define TOY_GAME_SPAWN_EDGE     250     /* 生成点距房间边界内缩量 */
#define TOY_GAME_MIN_SPAWN_DIST 1200    /* 生成点距玩家最小距离（防贴脸） */
#define TOY_GAME_GOAL_HOLD_MS   1500    /* 终点安全室内停留多久判定通关 */
#define TOY_GAME_MAX_RANGE      11500   /* 弹丸最大射程（世界单位，≈地图尺度） */
#define TOY_GAME_MAX_RAYS       12      /* 单枪最大弹丸数（霰弹枪），弹道记录上限 */
#define TOY_GAME_MAX_NAME       32      /* 身份显示名（含结尾 NUL） */
#define TOY_GAME_DETECT_RANGE   5600    /* 视觉最远察觉距离（原值两倍） */
#define TOY_GAME_RETARGET_MS    500     /* 多玩家目标重新评估间隔 */
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
#define TOY_GAME_SMOKER_RANGE   13000
#define TOY_GAME_SMOKER_PULL_MS 4000
#define TOY_GAME_SMOKER_COOLDOWN_MS 6500
#define TOY_GAME_SMOKER_PULL_STEP 46
#define TOY_GAME_CHARGER_RANGE  9600
#define TOY_GAME_CHARGER_WINDUP_MS 700
#define TOY_GAME_CHARGER_MAX_CHARGE_MS TOY_CONFIG_CHARGER_DURATION_MS
#define TOY_GAME_CHARGER_COOLDOWN_MS TOY_CONFIG_CHARGER_COOLDOWN_MS
#define TOY_GAME_CHARGER_SPEED  TOY_CONFIG_CHARGER_SPEED
#define TOY_GAME_CHARGER_DAMAGE TOY_CONFIG_CHARGER_DAMAGE
#define TOY_GAME_CHARGER_KNOCKBACK 1050
#define TOY_GAME_SMOKER_DAMAGE     2
#define TOY_GAME_CHARGER_IMPACT_DAMAGE TOY_CONFIG_CHARGER_IMPACT_DAMAGE
#define TOY_GAME_CHARGER_IMPACT_RANGE  TOY_CONFIG_CHARGER_IMPACT_RANGE
#define TOY_GAME_SPECIAL_WINDUP_MS 1200
#define TOY_GAME_AIRBORNE_GRAVITY  TOY_CONFIG_AIRBORNE_GRAVITY
#define TOY_GAME_AIRBORNE_MS    TOY_CONFIG_AIRBORNE_MS
#define TOY_GAME_AIRBORNE_VELOCITY TOY_CONFIG_AIRBORNE_VELOCITY
#define TOY_GAME_FALL_TERMINAL_VELOCITY TOY_CONFIG_FALL_TERMINAL_VELOCITY
#define TOY_GAME_JUMP_MS         TOY_CONFIG_JUMP_MS
#define TOY_GAME_JUMP_VELOCITY   TOY_CONFIG_JUMP_VELOCITY
#define TOY_GAME_AI_RETURN_SPEED TOY_CONFIG_AI_RETURN_SPEED
#define TOY_GAME_AI_DEPLOY_RADIUS 180
#define TOY_GAME_MAX_FLAG_SLOTS 4
#define TOY_GAME_PLAYER_ACTOR_INDEX (-1)

#define TOY_GAME_KEY_RELOAD     19      /* evdev KEY_R */
#define TOY_GAME_KEY_SLOT_1     2       /* evdev KEY_1：主武器槽 */
#define TOY_GAME_KEY_SLOT_2     3       /* evdev KEY_2：副武器（手枪）槽 */
#define TOY_GAME_KEY_SLOT_3     4       /* evdev KEY_3：投掷物槽 */
#define TOY_GAME_KEY_SLOT_4     5       /* evdev KEY_4：药丸槽 */

/* 商店价格集中配置，便于后续调平衡。 */
#define TOY_GAME_PRICE_SMG 50
#define TOY_GAME_PRICE_SHOTGUN 50
#define TOY_GAME_PRICE_AK 100
#define TOY_GAME_PRICE_AWP 200
#define TOY_GAME_PRICE_BOMB 20
#define TOY_GAME_PRICE_MOLOTOV 50
#define TOY_GAME_PRICE_PILL 10
#define TOY_GAME_THROWABLE_MAX 5
#define TOY_GAME_PILL_MAX 10
#define TOY_GAME_INITIAL_MONEY 50

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
    TOY_GAME_ENEMY_SEARCH,
    TOY_GAME_ENEMY_TRACKING
};

/* 可同步的身份类型。AI 使用独立 actor_id，未来接入网络时不必把 AI
 * 伪装成玩家输入端；名字和类型可以直接作为快照元数据传输。 */
enum toy_game_actor_kind {
    TOY_GAME_ACTOR_PLAYER = 0,
    TOY_GAME_ACTOR_AI = 1,
    TOY_GAME_ACTOR_ANIME = 2
};

enum toy_game_actor_state {
    TOY_GAME_ACTOR_ALIVE,
    TOY_GAME_ACTOR_DOWNED,
    TOY_GAME_ACTOR_DEAD
};

enum toy_game_animation_id {
    TOY_GAME_ANIM_NONE,
    TOY_GAME_ANIM_IDLE,
    TOY_GAME_ANIM_MOVE,
    TOY_GAME_ANIM_FIRE,
    TOY_GAME_ANIM_RELOAD,
    TOY_GAME_ANIM_DOWNED,
    TOY_GAME_ANIM_HIT,
    TOY_GAME_ANIM_DEATH,
    TOY_GAME_ANIM_REVIVE,
    TOY_GAME_ANIM_SHOVE,
    TOY_GAME_ANIM_MELEE,
    TOY_GAME_ANIM_THROW,
    TOY_GAME_ANIM_COUNT
};

struct toy_game_animation_state {
    int id;
    int time_ms;
};

/* 所有可被技能影响的实体都通过这组类型进入通用受迫移动接口。 */
enum toy_game_entity_kind {
    TOY_GAME_ENTITY_PLAYER,
    TOY_GAME_ENTITY_ACTOR,
    TOY_GAME_ENTITY_ENEMY
};

enum toy_game_special_control {
    TOY_GAME_SPECIAL_CONTROL_NONE = 0,
    TOY_GAME_SPECIAL_CONTROL_SMOKER = 1
};

enum toy_game_target_kind {
    TOY_GAME_TARGET_NONE = -1,
    TOY_GAME_TARGET_HOST,
    TOY_GAME_TARGET_ACTOR
};

enum toy_game_ai_class {
    TOY_GAME_AI_LEVEL_1,
    TOY_GAME_AI_LEVEL_2,
    TOY_GAME_AI_LEVEL_3,
    TOY_GAME_AI_CLASS_COUNT
};

enum toy_game_enemy_type {
    TOY_GAME_ENEMY_COMMON,
    TOY_GAME_ENEMY_PURSUIT_COMMON,   /* PURSUIT_COMMON */
    TOY_GAME_ENEMY_HEAVY,
    TOY_GAME_ENEMY_PURSUIT_HEAVY,
    TOY_GAME_ENEMY_PURSUIT_FAST,
    TOY_GAME_ENEMY_SMOKER,
    TOY_GAME_ENEMY_CHARGER,
    TOY_GAME_ENEMY_TANK,
    TOY_GAME_ENEMY_TYPE_COUNT
};

/* Enemy catalog indexes are local implementation details.  These IDs are
 * stable across map data and network packets. */
enum toy_game_enemy_id {
    TOY_GAME_ENEMY_ID_COMMON = 100,
    TOY_GAME_ENEMY_ID_PURSUIT_COMMON = 110,
    TOY_GAME_ENEMY_ID_HEAVY = 120,
    TOY_GAME_ENEMY_ID_PURSUIT_HEAVY = 130,
    TOY_GAME_ENEMY_ID_PURSUIT_FAST = 140,
    TOY_GAME_ENEMY_ID_SMOKER = 200,
    TOY_GAME_ENEMY_ID_CHARGER = 210,
    TOY_GAME_ENEMY_ID_TANK = 220
};

enum toy_game_enemy_ability {
    TOY_GAME_ENEMY_ABILITY_NONE = 0,
    TOY_GAME_ENEMY_ABILITY_SMOKER_TONGUE = 10,
    TOY_GAME_ENEMY_ABILITY_CHARGER_RUSH = 20,
    TOY_GAME_ENEMY_ABILITY_TANK_SWEEP = 30
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
    TOY_GAME_EV_ACTOR_DOWN,
    TOY_GAME_EV_ACTOR_REVIVE,
    TOY_GAME_EV_PICKUP,
    TOY_GAME_EV_BUTTON,
    TOY_GAME_EV_REVIVE,
    TOY_GAME_EV_SPAWN,
    TOY_GAME_EV_OBJECTIVE,
    TOY_GAME_EV_WEAPON_SWITCH,
    TOY_GAME_EV_SHOVE,
    TOY_GAME_EV_SHOVE_HIT,
    TOY_GAME_EV_MELEE,
    TOY_GAME_EV_MELEE_HIT,
    TOY_GAME_EV_SHOOT_SMG,
    TOY_GAME_EV_SHOOT_SHOTGUN,
    TOY_GAME_EV_SHOOT_AK,
    TOY_GAME_EV_SHOOT_AWP,
    TOY_GAME_EV_BOMB_BEEP,
    TOY_GAME_EV_BOMB_EXPLODE,
    TOY_GAME_EV_MOLOTOV_BREAK,
};

/* Gameplay-owned one-shot event.  This captures the hit-time values instead
 * of asking a later network pass to infer them from actor motion state. */
struct toy_game_player_impulse_event {
    int target_id;
    int impulse_x, impulse_z;
    int vertical_velocity;
    int airborne_ms, airborne_y;
};

/* 碰撞/命中共用的 xz 平面轴对齐盒（与房间障碍物同尺度） */
struct toy_game_box { int minx, maxx, minz, maxz, miny, maxy; };
enum toy_game_ground_kind {
    TOY_GAME_GROUND_FLAT,
    TOY_GAME_GROUND_RAMP_X,
    TOY_GAME_GROUND_RAMP_Z
};
struct toy_game_platform {
    int minx, maxx, minz, maxz, height;
    int kind, end_height;
};
struct toy_game_ground_query {
    int has_support;               /* whole footprint is on a ground primitive */
    int has_landing;               /* footprint overlaps a ground primitive */
    int support_y;                 /* whole footprint is on this surface */
    int landing_y;                 /* footprint overlaps this surface */
    int touches_current_support;   /* current support still touches footprint */
    int support_is_ramp;
};

/* The world is small enough for a fixed connectivity grid.  This is
 * deliberately a component map, not a path-finding data structure. */
#define TOY_GAME_NAV_CELL_SIZE 300
#define TOY_GAME_NAV_MAX_SIDE 128
#define TOY_GAME_NAV_MAX_CELLS (TOY_GAME_NAV_MAX_SIDE * TOY_GAME_NAV_MAX_SIDE)

/* ── 武器槽：0=主武器，1=副武器/近战，2=投掷物 ─────────────── */

#define TOY_GAME_WEAPON_SLOTS 4

enum toy_game_weapon {
    TOY_GAME_WEAPON_PISTOL = 0,
    TOY_GAME_WEAPON_SMG,
    TOY_GAME_WEAPON_SHOTGUN,
    TOY_GAME_WEAPON_AK,
    TOY_GAME_WEAPON_AWP,
    TOY_GAME_WEAPON_AXE,
    TOY_GAME_WEAPON_BOMB,
    TOY_GAME_WEAPON_MOLOTOV,
    TOY_GAME_WEAPON_PILL,
    TOY_GAME_WEAPON_COUNT
};

/* Stable content IDs are deliberately separate from the local catalog index.
 * The index is allowed to change when the catalog is reorganised; the ID is
 * part of save/network/content data and must not be reused. */
enum toy_game_weapon_id {
    TOY_GAME_WEAPON_ID_PISTOL = 10,
    TOY_GAME_WEAPON_ID_SMG = 20,
    TOY_GAME_WEAPON_ID_SHOTGUN = 30,
    TOY_GAME_WEAPON_ID_AK = 40,
    TOY_GAME_WEAPON_ID_AWP = 50
    ,TOY_GAME_WEAPON_ID_AXE = 60
    ,TOY_GAME_WEAPON_ID_BOMB = 70
    ,TOY_GAME_WEAPON_ID_MOLOTOV = 80
    ,TOY_GAME_WEAPON_ID_PILL = 90
};

enum toy_game_weapon_muzzle_profile {
    TOY_GAME_MUZZLE_STANDARD = 0,
    TOY_GAME_MUZZLE_SHOTGUN
};

struct toy_game_weapon_info {
    int mag_size;      /* 弹匣容量 */
    int reserve_max;   /* 备弹上限；TOY_GAME_AMMO_INFINITE = 无限 */
    int cooldown_ms;   /* 两发最小间隔 */
    int reload_ms;     /* 换弹耗时 */
    int full_auto;     /* 按住连发 */
    int pellets;       /* 每次扣扳机弹丸数（霰弹枪散射） */
    int spread;        /* 每颗弹丸随机偏角上限（1024 定点，[-spread,+spread] 均匀） */
    int slot;          /* 装备槽：0=主武器，1=副武器 */
    int damage;        /* 单颗弹丸伤害；霰弹枪按每颗弹丸计算 */
    int content_id;    /* 稳定内容 ID；不要把数组下标当作网络 ID */
    const char *name;  /* 调试/配置名称 */
    const char *short_name; /* HUD 名称 */
    int muzzle_profile; /* 表现层使用的枪口布局 */
    int range;          /* 弹丸最远距离 */
    int alert_range;    /* AI 持有该武器时的索敌距离 */
    int power_bias;     /* CP 特殊修正；通常为 0 */
};

struct toy_game_enemy_info {
    int max_hp;
    int speed_min;
    int speed_max;
    int bite_damage;
    int model_id;
    unsigned int color;
    int content_id;    /* 稳定内容 ID */
    const char *name;  /* 调试/配置名称 */
    int ability;       /* enum toy_game_enemy_ability */
};

/* 弹丸射线记录：宿主据此渲染子弹轨迹（tracer）与命中特效 */
struct toy_game_ray {
    int sy, cy;        /* 水平弹丸方向（1024 定点，已归一化） */
    int vy;            /* 屏幕竖直扩散方向（1024 定点，向上为正） */
    int ex, ez;        /* 终点：命中敌人/墙体位置，或最大射程端点 */
    int hit_enemy;     /* 该弹丸击倒敌人 */
    int hit_world;     /* 该弹丸撞上障碍（终点为墙体交点） */
    int enemy_index;   /* 命中的敌人；未命中为 -1 */
    int damage;        /* 本次弹丸实际造成的伤害 */
};

struct toy_game_slot {
    int weapon;        /* enum toy_game_weapon；-1 = 空槽 */
    int mag;
    int reserve;
};

#define TOY_GAME_MAX_PROJECTILES 16
struct toy_game_projectile {
    int active, kind;
    int x, z, vx, vz, vy;
    int fuse_ms;
    int blink_timer_ms;             /* 距离下一次红灯/滴声 */
    int flash_ms;                   /* 当前红灯脉冲 */
    int age_ms;                    /* 已飞行时间，仅用于模型自转 */
    int y;                         /* 相对地面的高度 */
    int bounces, landed;
};

struct toy_game_burn_zone {
    int active;
    int x, z;
    int remaining_ms;
    int tick_ms;
    int elapsed_ms;
};

/* Runtime state owned by a special-infected ability.  Keeping this state
 * together means a new ability can grow without adding another cluster of
 * fields to the enemy's movement/health core. */
struct toy_game_enemy_ability_state {
    int special_timer_ms;
    int special_windup_ms;
    int special_target_active;
    int charge_active;
    int charge_dir_x, charge_dir_z;
    int charge_elapsed_ms;
    int charge_hit_base;
    uint64_t charge_hit_actor_mask;
    int special_target_kind;
    int special_target_index;
    int special_pull_timer_ms;
};

struct toy_game_enemy {
    int active;         /* 0=空槽 1=存活 2=倒地中 */
    int type;           /* enum toy_game_enemy_type */
    int x, z;           /* 世界坐标（xz 平面） */
    int speed;          /* 每 16ms 逻辑步移动单位 */
    int hp;
    int bite_cooldown_ms;
    int flash;          /* 命中闪白计时 */
    int hurt;           /* 受击闪红计时 */
    int shove_stun_ms;  /* 推开后的僵直计时：期间不移动不攻击 */
    int dying_ms;       /* 倒地压扁计时 */
    int ai_state;       /* enum toy_game_enemy_ai */
    int ai_timer_ms;    /* 侦测/警觉阶段倒计时 */
    int target_x, target_z; /* 调查目标或最后声源 */
    int last_seen_x, last_seen_z;
    int lost_sight_ms;
    int target_kind;         /* 0=主机，1=客户端，2=AI 队友 */
    int target_index;    /* target_kind=2 时的 actor 数组索引 */
    int retarget_timer_ms;
    int wander_timer_ms;
    int dir_x, dir_z;   /* 面向，1024 基准定点 */
    struct toy_game_enemy_ability_state ability;
    int airborne_ms;
    int vertical_velocity;
    int airborne_y;
    int ground_y;
    int knockback_x, knockback_z;
};

/* 固定容量 actor 容器。旧的 ai_* 字段暂时保留在 toy_game 中作为兼容
 * 镜像，本结构是后续多 AI 迁移的唯一目标。 */
struct toy_game_actor {
    int active;
    int actor_id;
    int kind;
    int class_id;
    int character_id;           /* stable presentation profile, not an asset index */
    int base_core;              /* BASE: fixed defense objective */
    int hired;                  /* 雇佣 AI：可由商店/开发者按钮清除 */
    int developer_only;         /* 开发者展示/测试角色，不可分配旗帜 */
    int companion;              /* 常驻副官：跟随玩家，永不驻守旗帜 */
    int flag_guard;             /* 固定旗帜驻守者：不可被商店重新分配 */
    int anime_character_id;     /* -1: low-poly AI; >=0: anime actor system */
    int anime_wander_timer_ms;
    int anime_wander_x, anime_wander_z;
    int state;
    int x, z;
    int sy, cy;
    int hp, max_hp;
    int revive_progress_ms;
    int airborne_ms;
    int vertical_velocity;
    int airborne_y;
    int ground_y;
    int air_x, air_z;
    int knockback_x, knockback_z;
    int control_disabled;       /* special attack currently owns movement */
    char name[TOY_GAME_MAX_NAME];
    struct toy_game_slot slots[TOY_GAME_WEAPON_SLOTS];
    int current_slot;
    int reloading, reload_timer_ms;
    int weapon_switch_timer_ms;
    int melee_timer_ms;
    int throw_timer_ms;
    int damage_flash_ms;
    int fire_cooldown_ms;
    int ai_shove_cooldown_ms;
    int ai_turn_remainder;
    int weapon_spread_heat;
    int moving;
    int muzzle_flash_ms;
    int kills;
    int special_kills;
    int damage_dealt;
    int throwable_damage_dealt;
    unsigned int fire_seq;
    int ray_count;
    struct toy_game_ray rays[TOY_GAME_MAX_RAYS];
    int deployment_x, deployment_z;
    int flag_index;
    int nav_x, nav_z;
    int nav_active;
    int fire_enabled;
    int hit_test_dummy;
    int animation_demo;
    int animation_demo_elapsed_ms;
    struct toy_game_animation_state animation;
    int locomotion_blend_ms;     /* idle/walk 过渡时钟 */
};

/* 一个 actor 在单帧中的可执行意图。决策层只填写这份值，规则层负责
 * 移动、换武器、换弹和射击；托管玩家和普通 AI 共用此入口。 */
struct toy_game_actor_command {
    int move_target_active;
    int move_x, move_z;
    int move_speed;
    int aim_active;
    int aim_sy, aim_cy;
    int fire_pressed;
    int fire_held;
    int reload;
    int switch_slot;           /* -1 表示不切枪 */
};

/* AI policy boundary.  The observation is a read-only snapshot assembled by
 * the rules layer; policy code can make decisions without touching renderer,
 * input, or the actor's mutable fields directly. */
struct toy_game_ai_observation {
    int actor_index;
    int actor_id;
    int state;
    int hp, max_hp;
    int health_percent;
    int current_weapon;
    int ammo_percent;
    int enemies_alive;
    int downed_actors;
    int nearest_enemy_index;
    int nearest_enemy_distance;
    int nearest_enemy_dx, nearest_enemy_dz;
    int wave;
    int money;
    int at_deployment;
};

/* A policy writes intent here.  The executor decides how much of it is legal
 * for the controlled actor and applies the normal game rules.  shop_* fields
 * intentionally use semantic values but remain independent of the session's
 * UI and network representations. */
struct toy_game_ai_decision {
    int target_enemy;
    int aim_sy, aim_cy;
    int move_x, move_z;
    int fire;
    int reload;
    int switch_weapon;
    int use_pill;
    int shop_action;
    int shop_item;
    int shop_target_actor;
    int shop_arg;
};

struct toy_game {
    /* 玩家 */
    int px, pz;         /* 宿主每帧同步相机位置 */
    int hp;
    int state;          /* enum toy_game_state */
    struct toy_game_slot slots[TOY_GAME_WEAPON_SLOTS];
    int current_slot;   /* 当前武器槽位 */
    int pitch_sy, pitch_cy; /* 玩家当前俯仰方向（1024 定点） */
    int view_y;         /* 玩家视角世界高度 */
    int weapon_switch_timer_ms; /* 切枪表现/禁射倒计时 */
    int melee_timer_ms;
    int throw_timer_ms;
    struct toy_game_projectile projectiles[TOY_GAME_MAX_PROJECTILES];
    struct toy_game_burn_zone burn_zones[TOY_CONFIG_MAX_BURN_ZONES];
    struct toy_game_animation_state animation;
    int reloading, reload_timer_ms;
    int fire_cooldown_ms;
    int weapon_spread_heat; /* 连射累积，停火后逐渐恢复 */
    int moving;            /* 宿主每帧同步的移动状态 */
    int muzzle_flash_ms;
    int damage_flash_ms;
    int kills;
    int special_kills;
    int damage_dealt;
    int throwable_damage_dealt;
    int money;
    unsigned int unlocked_weapons;
    int actor_id;
    int actor_kind;
    char player_name[TOY_GAME_MAX_NAME];

    struct toy_game_actor actors[TOY_GAME_MAX_ACTORS];
    int base_actor_index;
    int base_regen_timer_ms;


    /* 弹道记录：最近一次射击产生的弹丸射线（宿主渲染 tracer） */
    unsigned int fire_seq;   /* 每次实际开火 +1；宿主以此检测新弹道 */
    int ray_count;
    struct toy_game_ray rays[TOY_GAME_MAX_RAYS];

    /* 敌人与波次 */
    struct toy_game_enemy enemies[TOY_GAME_MAX_ENEMIES];
    int wave;
    int to_spawn;       /* 本波待生成配额 */
    int spawn_timer_ms;
    int enemies_alive;
    int wave_attack_points;
    int wave_attack_multiplier;
    int wave_spawn_index;
    int wave_spawn_interval_ms;
    int wave_spawn_types[TOY_GAME_MAX_WAVE_QUEUE];
    int wave_waiting_common;
    int wave_waiting_fast;
    int wave_waiting_heavy;
    int wave_waiting_special;
    int wave_waiting_tank;

    /* 经典闯关模式：固定刷怪区，安全室对敌人视为禁区。 */
    const struct toy_game_box *safe_rooms;
    int safe_room_count;
    int safe_start_index;
    int safe_goal_index;
    const struct toy_game_box *spawn_zones;
    int spawn_zone_count;
    int campaign_mode;
    int campaign_phase;
    int spawn_budget;
    int phase_timer_ms;
    int active_attackers;
    int director_encounters;
    int goal_hold_ms;
    int campaign_stage; /* 0=start, 1=first base cleared, 2=second base cleared */
    const struct toy_game_box *alarm_zone;
    int alarm_spawn_zone;
    int alarm_triggered;
    int alarm_timer_ms;

    /* 世界（宿主所有，只读借用） */
    const struct toy_game_box *world;
    int world_count;
    int room_limit;
    const struct toy_game_platform *platforms;
    int platform_count;

    int nav_origin;
    int nav_cell_size;
    int nav_width;
    int nav_height;
    unsigned char nav_walkable[TOY_GAME_NAV_MAX_CELLS];
    int nav_ground_y[TOY_GAME_NAV_MAX_CELLS];
    unsigned short nav_component[TOY_GAME_NAV_MAX_CELLS];

    int network_rescuer_available;
    int player_down;
    int player_revive_progress_ms;
    int player_control_disabled;
    int player_pull_enemy_index;
    int player_pull_timer_ms;
    int player_special_control;
    uint32_t player_special_control_id;
    int player_special_source;
    int player_special_pull_step;
    int player_airborne_ms;
    int player_airborne_y;
    int player_ground_y;
    int player_vertical_velocity;
    int player_air_x, player_air_z;
    int player_knockback_x;
    int player_knockback_z;
    int ai_context_actor_index;

    /* PRNG（xorshift64*，init 时播种） */
    uint64_t rng;

    /* 本帧事件队列（宿主每帧 drain） */
    int event_count;
    unsigned char events[TOY_GAME_MAX_EVENTS];
    int player_impulse_event_count;
    struct toy_game_player_impulse_event
        player_impulse_events[TOY_GAME_MAX_EVENTS];
};

void toy_game_init(struct toy_game *g, uint64_t seed);      /* 初始化/重开共用 */
void toy_game_emit_event(struct toy_game *g, int event);
void toy_game_set_player_name(struct toy_game *g, const char *name);
int  toy_game_ai_observe(const struct toy_game *g, int actor_index,
                         struct toy_game_ai_observation *out);
void toy_game_ai_decision_clear(struct toy_game_ai_decision *decision);
void toy_game_set_ai_teammate(struct toy_game *g, int active, int x, int z,
                              const char *name);
void toy_game_set_ai_teammate_class(struct toy_game *g, int active, int class_id,
                                    int x, int z, const char *name);
int  toy_game_add_ai(struct toy_game *g, int class_id, int x, int z,
                     const char *name);
int  toy_game_add_hired_ai(struct toy_game *g, int weapon, int x, int z,
                           const char *name);
int  toy_game_add_anime_actor(struct toy_game *g, int character_id,
                              int x, int z, const char *name);
int  toy_game_add_anime_flag_guard(struct toy_game *g, int character_id,
                                   int x, int z, const char *name,
                                   int flag_index);
int  toy_game_set_ai_weapon(struct toy_game *g, int actor_index, int weapon);
int  toy_game_clear_hired_ai(struct toy_game *g);
int  toy_game_upgrade_ai(struct toy_game *g, int actor_index);
int  toy_game_set_remote_player(struct toy_game *g, int player_id,
                                int active, int x, int z,
                                const char *name);
void toy_game_update_ai_teammate(struct toy_game *g, int dt_ms);
void toy_game_update_ai_teammates(struct toy_game *g, int dt_ms);
int  toy_game_assign_actor_deployment(struct toy_game *g, int actor_index,
                                      int x, int z, int flag_index);
int  toy_game_revive_ai(struct toy_game *g, int dt_ms);
int  toy_game_revive_actor(struct toy_game *g, int actor_index, int dt_ms);
int  toy_game_set_campaign_stage(struct toy_game *g, int stage);
int  toy_game_move_ai_actor(struct toy_game *g, int actor_index, int x, int z);
void toy_game_set_player_special_control(struct toy_game *g, int type,
                                         uint32_t control_id,
                                         int source_enemy, int pull_step);
void toy_game_clear_player_special_control(struct toy_game *g,
                                           uint32_t control_id);
void toy_game_update_player_special_control(struct toy_game *g, int dt_ms);
void toy_game_apply_player_impulse(struct toy_game *g, int impulse_x,
                                   int impulse_z, int vertical_velocity,
                                   int airborne_ms, int airborne_y);
/* 对指定实体施加统一的伤害、打断和击飞规则。dx/dz 是相对冲击方向。 */
int  toy_game_apply_entity_impact(struct toy_game *g, int kind, int index,
                                  int dx, int dz, int damage);
void toy_game_set_world(struct toy_game *g,
                        const struct toy_game_box *boxes,
                        int box_count, int room_limit);
void toy_game_rebuild_navigation(struct toy_game *g);
void toy_game_set_platforms(struct toy_game *g,
                            const struct toy_game_platform *platforms,
                            int platform_count);
struct toy_game_ground_query toy_game_query_ground(
    const struct toy_game *g, int x, int z, int radius, int current_ground_y);
int  toy_game_position_blocked_at_height(const struct toy_game *g,
                                         int x, int z, int radius,
                                         int ground_height);
int  toy_game_try_move_player(struct toy_game *g, int x, int z);
void toy_game_update_player_ground(struct toy_game *g);
void toy_game_set_campaign(struct toy_game *g,
                           const struct toy_game_box *safe_rooms,
                           int safe_room_count,
                           const struct toy_game_box *spawn_zones,
                           int spawn_zone_count);
void toy_game_set_campaign_safe_indices(struct toy_game *g,
                                        int start_index, int goal_index);
void toy_game_set_alarm(struct toy_game *g,
                        const struct toy_game_box *alarm_zone,
                        int spawn_zone_index);
int  toy_game_point_in_box(int x, int z, const struct toy_game_box *box);
int  toy_game_position_blocked(const struct toy_game *g,
                               int x, int z, int radius);
void toy_game_update(struct toy_game *g,
                     const unsigned char *keys_pressed,     /* 可 NULL */
                     int fire_pressed, int sy, int cy, int dt_ms);
int  toy_game_jump(struct toy_game *g);
int  toy_game_jump_with_velocity(struct toy_game *g, int dx, int dz);
void toy_game_update_player_motion(struct toy_game *g, int dt_ms);
int  toy_game_jump_actor(struct toy_game *g, int actor_index, int dx, int dz);
void toy_game_update_actor_motion(struct toy_game *g, int actor_index, int dt_ms);
void toy_game_update_actor_ground(struct toy_game *g, int actor_index);
void toy_game_update_held(struct toy_game *g,
                          const unsigned char *keys_pressed, /* 可 NULL */
                          int fire_pressed, int fire_held,
                          int sy, int cy, int dt_ms);       /* 全自动武器的按住连发 */
/* 只推进一名玩家的武器/换弹状态，不更新敌人、波次或世界。联机主机
 * 用它在同一份权威世界上验证远端射击。 */
void toy_game_update_weapon_held(struct toy_game *g,
                                 const unsigned char *keys_pressed,
                                 int fire_pressed, int fire_held,
                                 int sy, int cy, int dt_ms);
/* Execute one actor's weapon intent through the same weapon/fire rules as the
 * local player.  The actor is the source of truth; player fields are only a
 * temporary compatibility context for the existing hitscan implementation. */
int  toy_game_update_actor_weapon_held(
    struct toy_game *g, struct toy_game_actor *actor,
    const unsigned char *keys_pressed, int fire_pressed, int fire_held,
    int sy, int cy, int dt_ms, int spread_percent);
int  toy_game_execute_actor_command(
    struct toy_game *g, struct toy_game_actor *actor,
    const struct toy_game_actor_command *command,
    int dt_ms, int spread_percent);
void toy_game_set_player_pitch(struct toy_game *g, int pitch_sy, int pitch_cy,
                               int view_y);
void toy_game_set_player_moving(struct toy_game *g, int moving);
int  toy_game_current_spread(const struct toy_game *g);
int  toy_game_fire(struct toy_game *g, int sy, int cy);     /* hitscan，命中返回 1 */
int  toy_game_apply_reported_hit(struct toy_game *g,
                                 struct toy_game_actor *actor,
                                 int enemy_index, int damage);
int  toy_game_switch_weapon(struct toy_game *g, int slot);  /* 切枪；空槽/同槽返回 0 */
int  toy_game_equip_weapon(struct toy_game *g, int weapon); /* 按武器定义装备到对应槽；同武器=补充弹药返回 0，新武器返回 1，非法返回 -1 */
int  toy_game_refill_ammo(struct toy_game *g);              /* 弹药盒：补满已拥有武器的备弹，有变化返回 1 */
int  toy_game_weapon_price(int weapon);
int  toy_game_weapon_combat_dps(int weapon);
int  toy_game_weapon_spread_penalty(int weapon);
int  toy_game_weapon_combat_power(int weapon);
int  toy_game_ai_level_combat_power(int class_id);
int  toy_game_actor_combat_power(const struct toy_game_actor *actor);
/* Side-effect-free predicates for AI policy code. */
int  toy_game_actor_health_percent(const struct toy_game_actor *actor);
int  toy_game_actor_current_weapon(const struct toy_game_actor *actor);
int  toy_game_actor_has_weapon(const struct toy_game_actor *actor, int weapon);
int  toy_game_actor_ammo_percent(const struct toy_game_actor *actor);
int  toy_game_count_active_enemies(const struct toy_game *g);
int  toy_game_count_downed_actors(const struct toy_game *g);
int  toy_game_nearest_enemy_distance(const struct toy_game *g,
                                     const struct toy_game_actor *actor);
int  toy_game_weapon_unlocked(const struct toy_game *g, int weapon);
int  toy_game_buy_weapon(struct toy_game *g, int weapon);
const struct toy_game_weapon_info *toy_game_weapon_info(int weapon);
const struct toy_game_weapon_info *toy_game_weapon_info_or_null(int weapon);
int  toy_game_weapon_is_valid(int weapon);
int  toy_game_weapon_content_id(int weapon);
int  toy_game_weapon_from_content_id(int content_id);
const char *toy_game_weapon_name(int weapon);
int  toy_game_weapon_from_name(const char *name);
const struct toy_game_enemy_info *toy_game_enemy_info(int type);
const struct toy_game_enemy_info *toy_game_enemy_info_or_null(int type);
int  toy_game_enemy_type_is_valid(int type);
int  toy_game_skip_wave_rest(struct toy_game *g);
void toy_game_set_wave_attack_multiplier(struct toy_game *g, int multiplier);
int  toy_game_enemy_content_id(int type);
int  toy_game_enemy_from_content_id(int content_id);
struct toy_game_actor *toy_game_actor_by_id(struct toy_game *g, int actor_id);
const struct toy_game_actor *toy_game_actor_by_id_const(const struct toy_game *g,
                                                        int actor_id);
struct toy_game_animation_info {
    int duration_ms;
    int loop;
};
const struct toy_game_animation_info *toy_game_animation_info(int animation_id);
const char *toy_game_animation_name(int animation_id);
int toy_game_animation_allows_locomotion(int animation_id);
void toy_game_animation_set(struct toy_game_animation_state *state,
                            int animation_id);
void toy_game_animation_update(struct toy_game_animation_state *state,
                               int dt_ms);
void toy_game_actor_set_animation(struct toy_game_actor *actor, int animation_id);
void toy_game_actor_update_animation(struct toy_game_actor *actor, int dt_ms);
int  toy_game_drain_events(struct toy_game *g, unsigned char *out, int max);
int  toy_game_drain_player_impulses(
    struct toy_game *g, struct toy_game_player_impulse_event *out, int max);
void toy_game_place_enemy(struct toy_game *g, int x, int z); /* 测试钩子 */
int  toy_game_shove(struct toy_game *g, int sy, int cy);    /* 推开面前敌人，返回推开的数量 */
int  toy_game_use_pill(struct toy_game *g);
int  toy_game_spawn_horde(struct toy_game *g, int count_min, int count_max,
                          const struct toy_game_box *points, int point_count,
                          int min_player_dist);
int  toy_game_spawn_horde_type(struct toy_game *g, int enemy_type,
                               int count_min, int count_max,
                               const struct toy_game_box *points, int point_count,
                               int min_player_dist);
    /* 召唤尸潮：从 points 中随机选出 1-3 个互异刷怪点（不超过
     * point_count），把 count_min..count_max 个持续追踪型敌人
     * （TOY_GAME_ENEMY_TRACKING，无视遮挡与丢失目标，永远直扑玩家）
     * 均摊到各点矩形内随机生成，每个位置距玩家至少 min_player_dist
     * （单位同世界坐标）。受空槽位限制，返回实际生成数。 */

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
    TOY_SFX_SHOVE,
    TOY_SFX_SHOVE_HIT,
    TOY_SFX_MELEE,
    TOY_SFX_MELEE_HIT,
    TOY_SFX_SMG,
    TOY_SFX_SHOTGUN,
    TOY_SFX_AK,
    TOY_SFX_AWP,
    TOY_SFX_BOMB_BEEP,
    TOY_SFX_BOMB_EXPLODE,
    TOY_SFX_MOLOTOV_BREAK,
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
    struct toy_sfx_sample samples[TOY_SFX_MOLOTOV_BREAK + 1];
    struct toy_sfx_voice voices[TOY_SFX_MAX_VOICES];
};

void toy_sfx_init(struct toy_sfx *sfx, int rate);
void toy_sfx_play(struct toy_sfx *sfx, int kind);       /* 触发；满 8 voice 偷剩余最短者 */
void toy_sfx_set_sample(struct toy_sfx *sfx, int kind, const short *pcm, unsigned frames);
                                                        /* 注册样本音色；pcm/frames 为空则回退程序合成 */
void toy_sfx_music(struct toy_sfx *sfx, int enabled);
void toy_sfx_render(struct toy_sfx *sfx, short *out, int frames); /* 混音至 S16 立体声 */

#endif /* TOYC_TOY_GAME_H */
