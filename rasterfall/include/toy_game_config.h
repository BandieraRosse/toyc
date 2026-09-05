/* Rasterfall gameplay tuning; distances use world units and timers use ms. */
#ifndef RASTERFALL_TOY_GAME_CONFIG_H
#define RASTERFALL_TOY_GAME_CONFIG_H  /* 防止配置头重复包含 */

/* General health and movement. */
#define TOY_CONFIG_PLAYER_HP                  100  /* 主玩家生命值 */
#define TOY_CONFIG_SECONDARY_PLAYER_HP        100  /* 第二玩家生命值 */
#define TOY_CONFIG_PLAYER_MOVE_STEP            76  /* 玩家每逻辑步移动量 */
#define TOY_CONFIG_GROUND_STEP_HEIGHT          120 /* 可直接跨越的最大地面高差 */
#define TOY_CONFIG_BASE_HP                     500 /* 基地核心初始/最大生命 */
#define TOY_CONFIG_BASE_REGEN_MS               1000 /* 基地核心每秒回复 */
#define TOY_CONFIG_AI_RETURN_SPEED             38  /* AI 队友回位速度 */

/* Wave spawning: the planned enemies are distributed evenly across this
 * duration.  Keep this as a tuning value so wave pacing can change without
 * touching the wave state machine. */
#define TOY_CONFIG_WAVE_SPAWN_DURATION_MS   20000  /* 每波刷怪持续时间 */
#define TOY_CONFIG_WAVE_ENEMY_COST_MULTIPLIER 2   /* 袭击点数/击杀奖励倍率 */

/* Weapon switching: duration and whether firing is blocked during it. */
#define TOY_CONFIG_WEAPON_SWITCH_MS           200  /* 切枪动画/禁射时长 */
#define TOY_CONFIG_BLOCK_FIRE_DURING_SWITCH    1  /* 切枪期间是否禁射 */
#define TOY_CONFIG_SPREAD_STILL_PERCENT        45  /* 静止时基础散布系数 */
#define TOY_CONFIG_SPREAD_MOVE_PERCENT        135  /* 移动时基础散布系数 */
#define TOY_CONFIG_SPREAD_SHOT_STEP             9  /* 每次开火增加的散布 */
#define TOY_CONFIG_SPREAD_HEAT_MAX            120  /* 连射散布上限 */
#define TOY_CONFIG_SPREAD_RECOVER_PER_SEC      70  /* 每秒恢复的散布 */
/* 玩家专属武器增强；武器定义仍保留标准参数，AI 与平衡计算不变。 */
#define TOY_CONFIG_PLAYER_FIRE_RATE_PERCENT   200  /* 玩家射速倍率 */
#define TOY_CONFIG_PLAYER_RELOAD_TIME_PERCENT  60  /* 玩家换弹时间倍率 */
#define TOY_CONFIG_SHOVE_ANIMATION_MS        300  /* 推搡动画时长 */
#define TOY_CONFIG_SHOVE_SWEEP_DEGREES       240  /* 推搡手臂旋转角度 */
#define TOY_CONFIG_SHOVE_RANGE               900  /* 推搡有效范围 */
#define TOY_CONFIG_SHOVE_STUN_MS             1500  /* 敌人僵直时长 */
#define TOY_CONFIG_MELEE_SWING_MS            500   /* 近战挥舞时间 */
#define TOY_CONFIG_MELEE_RANGE               TOY_CONFIG_SHOVE_RANGE
#define TOY_CONFIG_MELEE_DAMAGE              100
#define TOY_CONFIG_THROW_HANDOFF_MS          300   /* 投掷出手/第一人称动作 */
#define TOY_CONFIG_THROW_COOLDOWN_MS         450
#define TOY_CONFIG_EXPLOSIVE_RADIUS          3000  /* Bomb 爆炸半径 */
#define TOY_CONFIG_MOLOTOV_RADIUS            1100
#define TOY_CONFIG_MOLOTOV_BURN_RADIUS       2500  /* Molotov 燃烧半径 */
#define TOY_CONFIG_MOLOTOV_BURN_MS           8000  /* 燃烧持续时间 */
#define TOY_CONFIG_MOLOTOV_TICK_MS           1000  /* 燃烧伤害间隔 */
#define TOY_CONFIG_MOLOTOV_DAMAGE              40  /* 每次燃烧伤害 */
#define TOY_CONFIG_MAX_BURN_ZONES              16
#define TOY_CONFIG_BOMB_DAMAGE               100   /* Bomb 对敌方单位的伤害 */
#define TOY_CONFIG_BOMB_FUSE_MS              4000
#define TOY_CONFIG_THROW_SPEED               15200  /* 投掷速度，影响距离 */
#define TOY_CONFIG_THROW_GRAVITY             6000  /* 投掷物重力 */
#define TOY_CONFIG_THROW_BOUNCE_RESTITUTION   700  /* 反弹后速度保留比例 */
#define TOY_CONFIG_THROW_MAX_BOUNCES            3  /* 最大墙体反弹次数 */
#define TOY_CONFIG_THROW_COLLISION_RADIUS      55  /* 投掷物碰撞半径 */
#define TOY_CONFIG_THROW_MODEL_SCALE         2000  /* 投掷物模型缩放：1000=100% */
#define TOY_CONFIG_CHARGER_SHOVE_STUN_MS     400  /* Charger 推搡僵直时长 */

/* Jump and forced motion. */
#define TOY_CONFIG_JUMP_MS                    900  /* 跳跃持续时间 */
#define TOY_CONFIG_JUMP_VELOCITY              110  /* 跳跃初速度 */
#define TOY_CONFIG_AIRBORNE_MS                700  /* 击飞持续时间 */
#define TOY_CONFIG_AIRBORNE_VELOCITY         220  /* 击飞初速度 */
#define TOY_CONFIG_AIRBORNE_GRAVITY            10  /* 空中重力 */
#define TOY_CONFIG_FALL_TERMINAL_VELOCITY     120  /* 最大单逻辑步下落速度 */
#define TOY_CONFIG_CHARGER_KNOCKBACK         1050  /* Charger 击退强度 */
#define TOY_CONFIG_PLAYER_KNOCKBACK_COOLDOWN_MS 5000 /* 玩家击飞冷却 */

/* Pistol: magazine, reserve, cooldown, reload, automatic, pellets, spread, slot, damage. */
#define TOY_CONFIG_PISTOL_MAG                  15  /* 弹匣容量 */
#define TOY_CONFIG_PISTOL_RESERVE              (-1) /* 备弹上限 */
#define TOY_CONFIG_PISTOL_COOLDOWN_MS         200  /* 射击间隔 */
#define TOY_CONFIG_PISTOL_RELOAD_MS          1500  /* 换弹时间 */
#define TOY_CONFIG_PISTOL_FULL_AUTO             0  /* 是否全自动 */
#define TOY_CONFIG_PISTOL_PELLETS               1  /* 每次弹丸数 */
#define TOY_CONFIG_PISTOL_SPREAD               12  /* 散布范围 */
#define TOY_CONFIG_PISTOL_SLOT                  1  /* 武器槽位 */
#define TOY_CONFIG_PISTOL_DAMAGE               20  /* 单颗伤害 */

/* SMG weapon tuning. */
#define TOY_CONFIG_SMG_MAG                     40  /* 弹匣容量 */
#define TOY_CONFIG_SMG_RESERVE                650  /* 备弹上限 */
#define TOY_CONFIG_SMG_COOLDOWN_MS            100  /* 射击间隔 */
#define TOY_CONFIG_SMG_RELOAD_MS             2000  /* 换弹时间 */
#define TOY_CONFIG_SMG_FULL_AUTO                1  /* 是否全自动 */
#define TOY_CONFIG_SMG_PELLETS                  1  /* 每次弹丸数 */
#define TOY_CONFIG_SMG_SPREAD                  90  /* 散布范围 */
#define TOY_CONFIG_SMG_SLOT                     0  /* 武器槽位 */
#define TOY_CONFIG_SMG_DAMAGE                  25  /* 单颗伤害 */

/* Shotgun weapon tuning. */
#define TOY_CONFIG_SHOTGUN_MAG                  8  /* 弹匣容量 */
#define TOY_CONFIG_SHOTGUN_RESERVE             64  /* 备弹上限 */
#define TOY_CONFIG_SHOTGUN_COOLDOWN_MS        800  /* 射击间隔 */
#define TOY_CONFIG_SHOTGUN_RELOAD_MS         2500  /* 换弹时间 */
#define TOY_CONFIG_SHOTGUN_FULL_AUTO            0  /* 是否全自动 */
#define TOY_CONFIG_SHOTGUN_PELLETS             10  /* 每次弹丸数 */
#define TOY_CONFIG_SHOTGUN_SPREAD             150  /* 散布范围 */
#define TOY_CONFIG_SHOTGUN_SLOT                  0  /* 武器槽位 */
#define TOY_CONFIG_SHOTGUN_DAMAGE               25  /* 单颗伤害 */

/* AK rifle weapon tuning. */
#define TOY_CONFIG_AK_MAG                       40  /* 弹匣容量 */
#define TOY_CONFIG_AK_RESERVE                  360  /* 备弹上限 */
#define TOY_CONFIG_AK_COOLDOWN_MS              180  /* 射击间隔 */
#define TOY_CONFIG_AK_RELOAD_MS              2200  /* 换弹时间 */
#define TOY_CONFIG_AK_FULL_AUTO                  1  /* 是否全自动 */
#define TOY_CONFIG_AK_PELLETS                    1  /* 每次弹丸数 */
#define TOY_CONFIG_AK_SPREAD                    50  /* 散布范围 */
#define TOY_CONFIG_AK_SLOT                       0  /* 武器槽位 */
#define TOY_CONFIG_AK_DAMAGE                    60  /* 单颗伤害 */

/* AWP sniper weapon tuning. */
#define TOY_CONFIG_AWP_MAG                      10  /* 弹匣容量 */
#define TOY_CONFIG_AWP_RESERVE                   64  /* 备弹上限 */
#define TOY_CONFIG_AWP_COOLDOWN_MS              1200  /* 射击间隔 */
#define TOY_CONFIG_AWP_RELOAD_MS              3200  /* 换弹时间 */
#define TOY_CONFIG_AWP_FULL_AUTO                 0  /* 是否全自动 */
#define TOY_CONFIG_AWP_PELLETS                   1  /* 每次弹丸数 */
#define TOY_CONFIG_AWP_SPREAD                    4  /* 散布范围 */
#define TOY_CONFIG_AWP_SLOT                      0  /* 武器槽位 */
#define TOY_CONFIG_AWP_DAMAGE                  200  /* 单颗伤害 */

/* Weapon reach and AI awareness.  The legacy common reach is 11500. */
#define TOY_CONFIG_PISTOL_RANGE              11500  /* 最大射程 */
#define TOY_CONFIG_PISTOL_ALERT_RANGE         8400  /* AI 警觉范围 */
#define TOY_CONFIG_SMG_RANGE                 11500  /* 最大射程 */
#define TOY_CONFIG_SMG_ALERT_RANGE            8400  /* AI 警觉范围 */
#define TOY_CONFIG_SHOTGUN_RANGE             11500  /* 最大射程 */
#define TOY_CONFIG_SHOTGUN_ALERT_RANGE        5600  /* AI 警觉范围 */
#define TOY_CONFIG_AK_RANGE                  17250  /* 最大射程 */
#define TOY_CONFIG_AK_ALERT_RANGE            11500  /* AI 警觉范围 */
#define TOY_CONFIG_AWP_RANGE                 23000  /* 最大射程 */
#define TOY_CONFIG_AWP_ALERT_RANGE           23000  /* AI 警觉范围 */

/* Pursuit enemy tuning. */
#define TOY_CONFIG_PURSUIT_COMMON_HP           50  /* 生命值 */
#define TOY_CONFIG_PURSUIT_COMMON_SPEED_MIN   66  /* 最低速度 */
#define TOY_CONFIG_PURSUIT_COMMON_SPEED_MAX   82  /* 最高速度 */
#define TOY_CONFIG_PURSUIT_COMMON_BITE_DAMAGE  2  /* 咬伤 */
#define TOY_CONFIG_PURSUIT_HEAVY_HP          200  /* 生命值 */
#define TOY_CONFIG_PURSUIT_HEAVY_SPEED_MIN    30  /* 最低速度 */
#define TOY_CONFIG_PURSUIT_HEAVY_SPEED_MAX    42  /* 最高速度 */
#define TOY_CONFIG_PURSUIT_HEAVY_BITE_DAMAGE   4  /* 咬伤 */
#define TOY_CONFIG_PURSUIT_FAST_HP            60  /* 生命值 */
#define TOY_CONFIG_PURSUIT_FAST_SPEED_MIN     92  /* 最低速度 */
#define TOY_CONFIG_PURSUIT_FAST_SPEED_MAX    112  /* 最高速度 */
#define TOY_CONFIG_PURSUIT_FAST_BITE_DAMAGE    2  /* 咬伤 */
#define TOY_CONFIG_SMOKER_HP                 100  /* 生命值 */
#define TOY_CONFIG_SMOKER_SPEED_MIN           34  /* 最低速度 */
#define TOY_CONFIG_SMOKER_SPEED_MAX           46  /* 最高速度 */
#define TOY_CONFIG_SMOKER_BITE_DAMAGE          0  /* 咬伤 */
#define TOY_CONFIG_CHARGER_HP                300  /* 生命值 */
#define TOY_CONFIG_CHARGER_SPEED_MIN          42  /* 最低速度 */
#define TOY_CONFIG_CHARGER_SPEED_MAX          58  /* 最高速度 */
#define TOY_CONFIG_CHARGER_BITE_DAMAGE         4  /* 咬伤 */
#define TOY_CONFIG_TANK_HP                   4000  /* Boss 生命值 */
#define TOY_CONFIG_TANK_SPEED_MIN              38  /* 与普通感染者一致 */
#define TOY_CONFIG_TANK_SPEED_MAX              56
#define TOY_CONFIG_TANK_BITE_DAMAGE             0  /* Tank 只使用范围挥击 */

/* Money rewards for killing enemies. */
#define TOY_CONFIG_MONEY_COMMON                1  /* 普通敌人击杀奖励 */
#define TOY_CONFIG_MONEY_HEAVY                 2  /* 重型敌人击杀奖励 */
#define TOY_CONFIG_MONEY_FAST                  2  /* 快速敌人击杀奖励 */
#define TOY_CONFIG_MONEY_SPECIAL               5  /* 特殊敌人击杀奖励 */
#define TOY_CONFIG_MONEY_TANK                  50  /* Tank 击杀奖励 */

/* Charger special tuning. */
#define TOY_CONFIG_CHARGER_WINDUP_MS         700  /* 蓄力时间 */
#define TOY_CONFIG_CHARGER_DURATION_MS      7000  /* 冲锋时长 */
#define TOY_CONFIG_CHARGER_COOLDOWN_MS     15000  /* 技能冷却 */
#define TOY_CONFIG_CHARGER_SPEED              95  /* 冲锋速度 */
#define TOY_CONFIG_CHARGER_DAMAGE              6  /* 撞击伤害 */
#define TOY_CONFIG_CHARGER_IMPACT_DAMAGE       6  /* 范围撞击伤害 */
#define TOY_CONFIG_CHARGER_IMPACT_RANGE      320  /* 撞击范围 */

/* Tank arm-sweep tuning.  Impact occurs near the end of the windup. */
#define TOY_CONFIG_TANK_ATTACK_RANGE         2850  /* 挥击伤害扇形半径 */
#define TOY_CONFIG_TANK_ATTACK_START_DIVISOR    2  /* 走到伤害半径的 1/2 后出手 */
#define TOY_CONFIG_TANK_ATTACK_CONE             2  /* 120°：dot*2 >= distance */
#define TOY_CONFIG_TANK_WINDUP_MS             750  /* 抬手/挥臂时间缩短一半 */
#define TOY_CONFIG_TANK_IMPACT_MS             625  /* 仍在动画即将结束时结算 */
#define TOY_CONFIG_TANK_COOLDOWN_MS          1000  /* 出手后冷却 */
#define TOY_CONFIG_TANK_DAMAGE                  8
#define TOY_CONFIG_TANK_BASE_DAMAGE            24
#define TOY_CONFIG_TANK_KNOCKBACK             1050 /* 普通单位击飞强度 */
#define TOY_CONFIG_TANK_ATTACK_START_RANGE \
    (TOY_CONFIG_TANK_ATTACK_RANGE / \
     TOY_CONFIG_TANK_ATTACK_START_DIVISOR)         /* 进入指定比例后出手 */

/* AI teammate health by class. */
#define TOY_CONFIG_AI_LEVEL_1_HP              80  /* 一级生命值 */
#define TOY_CONFIG_AI_LEVEL_2_HP             120  /* 二级生命值 */
#define TOY_CONFIG_AI_LEVEL_3_HP             160  /* 三级生命值 */

/* AI 强度：射击间隔倍率以百分比表示，转身速度为角度/秒。 */
#define TOY_CONFIG_AI_LEVEL_1_FIRE_INTERVAL_PERCENT 150
#define TOY_CONFIG_AI_LEVEL_2_FIRE_INTERVAL_PERCENT 100
#define TOY_CONFIG_AI_LEVEL_3_FIRE_INTERVAL_PERCENT 80
#define TOY_CONFIG_AI_LEVEL_1_TURN_SPEED_DEGREE      240
#define TOY_CONFIG_AI_LEVEL_2_TURN_SPEED_DEGREE      380
#define TOY_CONFIG_AI_LEVEL_3_TURN_SPEED_DEGREE      720
#define TOY_CONFIG_AI_LEVEL_1_SHOVE_COOLDOWN_MS     2000
#define TOY_CONFIG_AI_LEVEL_2_SHOVE_COOLDOWN_MS     1000
#define TOY_CONFIG_AI_LEVEL_3_SHOVE_COOLDOWN_MS        0
#define TOY_CONFIG_AI_LEVEL_1_MOVE_SPEED              30  /* 一级移动速度 */
#define TOY_CONFIG_AI_LEVEL_2_MOVE_SPEED              38  /* 二级移动速度 */
#define TOY_CONFIG_AI_LEVEL_3_MOVE_SPEED              46  /* 三级移动速度 */
#define TOY_CONFIG_AI_LEVEL_1_SPREAD_PERCENT         160  /* 一级散布系数 */
#define TOY_CONFIG_AI_LEVEL_2_SPREAD_PERCENT         125  /* 二级散布系数 */
#define TOY_CONFIG_AI_LEVEL_3_SPREAD_PERCENT         100  /* 三级散布系数 */
#define TOY_CONFIG_AI_HIRE_PRICE                    100  /* 一级 AI 雇佣价 */
#define TOY_CONFIG_AI_HIRE_PISTOL_WEAPON_PRICE      100  /* AI 手枪价 */
#define TOY_CONFIG_AI_HIRE_WEAPON_PRICE_MULTIPLIER    10  /* AI 武器价倍率 */
#define TOY_CONFIG_AI_LEVEL_2_PRICE                 400  /* 1→2 升级价 */
#define TOY_CONFIG_AI_LEVEL_3_PRICE                 800  /* 2→3 升级价 */

/* 粗略战斗力（Combat Power）评分。CP 不使用经济价格，也不试图模拟
 * 实战中的走位和目标选择，只给导演系统一个稳定的相对强度指标。 */
#define TOY_CONFIG_COMBAT_DPS_SCALE                 20
#define TOY_CONFIG_COMBAT_SPREAD_LOW_MAX             50
#define TOY_CONFIG_COMBAT_SPREAD_MEDIUM_MAX         100
#define TOY_CONFIG_COMBAT_SPREAD_HIGH_MAX           150
#define TOY_CONFIG_COMBAT_SPREAD_PENALTY_LOW          0
#define TOY_CONFIG_COMBAT_SPREAD_PENALTY_MEDIUM       1
#define TOY_CONFIG_COMBAT_SPREAD_PENALTY_HIGH         2
#define TOY_CONFIG_COMBAT_SPREAD_PENALTY_EXTREME      3
#define TOY_CONFIG_COMBAT_WEAPON_POWER_MIN            1
#define TOY_CONFIG_COMBAT_WEAPON_POWER_MAX           16
#define TOY_CONFIG_COMBAT_AI_LEVEL_1_POINTS          10
#define TOY_CONFIG_COMBAT_AI_LEVEL_2_POINTS          18
#define TOY_CONFIG_COMBAT_AI_LEVEL_3_POINTS          28
/* AWP 的低射速已被 DPS 计入；该小偏置只补偿其精准远程单发价值，
 * 未来特感/Tank 优先索敌可继续在这里调整。 */
#define TOY_CONFIG_COMBAT_AWP_POWER_BIAS              5

/* Developer display options. */
#define TOY_CONFIG_SHOW_MODEL_PATHS            0  /* 是否显示开发者模型路径 */

#endif
