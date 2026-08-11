/* Rasterfall gameplay tuning; distances use world units and timers use ms. */
#ifndef RASTERFALL_TOY_GAME_CONFIG_H
#define RASTERFALL_TOY_GAME_CONFIG_H  /* 防止配置头重复包含 */

/* General health and movement. */
#define TOY_CONFIG_PLAYER_HP                  100  /* 主玩家生命值 */
#define TOY_CONFIG_SECONDARY_PLAYER_HP        100  /* 第二玩家生命值 */
#define TOY_CONFIG_PLAYER_MOVE_STEP            76  /* 玩家每逻辑步移动量 */
#define TOY_CONFIG_AI_RETURN_SPEED             38  /* AI 队友回位速度 */

/* Weapon switching: duration and whether firing is blocked during it. */
#define TOY_CONFIG_WEAPON_SWITCH_MS           200  /* 切枪动画/禁射时长 */
#define TOY_CONFIG_BLOCK_FIRE_DURING_SWITCH    1  /* 切枪期间是否禁射 */
#define TOY_CONFIG_SHOVE_ANIMATION_MS        300  /* 推搡动画时长 */
#define TOY_CONFIG_SHOVE_SWEEP_DEGREES       240  /* 推搡手臂旋转角度 */
#define TOY_CONFIG_SHOVE_RANGE               900  /* 推搡有效范围 */
#define TOY_CONFIG_SHOVE_STUN_MS             1500  /* 敌人僵直时长 */
#define TOY_CONFIG_CHARGER_SHOVE_STUN_MS     400  /* Charger 推搡僵直时长 */

/* Jump and forced motion. */
#define TOY_CONFIG_JUMP_MS                    900  /* 跳跃持续时间 */
#define TOY_CONFIG_JUMP_VELOCITY              110  /* 跳跃初速度 */
#define TOY_CONFIG_AIRBORNE_MS                700  /* 击飞持续时间 */
#define TOY_CONFIG_AIRBORNE_VELOCITY         220  /* 击飞初速度 */
#define TOY_CONFIG_AIRBORNE_GRAVITY            10  /* 空中重力 */
#define TOY_CONFIG_CHARGER_KNOCKBACK         1050  /* Charger 击退强度 */

/* Pistol: magazine, reserve, cooldown, reload, automatic, pellets, spread, slot, damage. */
#define TOY_CONFIG_PISTOL_MAG                  30  /* 弹匣容量 */
#define TOY_CONFIG_PISTOL_RESERVE              (-1) /* 备弹上限 */
#define TOY_CONFIG_PISTOL_COOLDOWN_MS         200  /* 射击间隔 */
#define TOY_CONFIG_PISTOL_RELOAD_MS          1500  /* 换弹时间 */
#define TOY_CONFIG_PISTOL_FULL_AUTO             0  /* 是否全自动 */
#define TOY_CONFIG_PISTOL_PELLETS               1  /* 每次弹丸数 */
#define TOY_CONFIG_PISTOL_SPREAD               12  /* 散布范围 */
#define TOY_CONFIG_PISTOL_SLOT                  1  /* 武器槽位 */
#define TOY_CONFIG_PISTOL_DAMAGE               30  /* 单颗伤害 */

/* SMG weapon tuning. */
#define TOY_CONFIG_SMG_MAG                     50  /* 弹匣容量 */
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
#define TOY_CONFIG_SHOTGUN_COOLDOWN_MS        600  /* 射击间隔 */
#define TOY_CONFIG_SHOTGUN_RELOAD_MS         2500  /* 换弹时间 */
#define TOY_CONFIG_SHOTGUN_FULL_AUTO            0  /* 是否全自动 */
#define TOY_CONFIG_SHOTGUN_PELLETS             12  /* 每次弹丸数 */
#define TOY_CONFIG_SHOTGUN_SPREAD             150  /* 散布范围 */
#define TOY_CONFIG_SHOTGUN_SLOT                  0  /* 武器槽位 */
#define TOY_CONFIG_SHOTGUN_DAMAGE               25  /* 单颗伤害 */

/* Common enemy tuning. */
#define TOY_CONFIG_COMMON_HP                   50  /* 生命值 */
#define TOY_CONFIG_COMMON_SPEED_MIN           38  /* 最低速度 */
#define TOY_CONFIG_COMMON_SPEED_MAX           56  /* 最高速度 */
#define TOY_CONFIG_COMMON_BITE_DAMAGE          2  /* 咬伤 */
#define TOY_CONFIG_FAST_HP                     50  /* 生命值 */
#define TOY_CONFIG_FAST_SPEED_MIN             66  /* 最低速度 */
#define TOY_CONFIG_FAST_SPEED_MAX             82  /* 最高速度 */
#define TOY_CONFIG_FAST_BITE_DAMAGE            2  /* 咬伤 */
#define TOY_CONFIG_HEAVY_HP                  200  /* 生命值 */
#define TOY_CONFIG_HEAVY_SPEED_MIN            24  /* 最低速度 */
#define TOY_CONFIG_HEAVY_SPEED_MAX            34  /* 最高速度 */
#define TOY_CONFIG_HEAVY_BITE_DAMAGE           4  /* 咬伤 */
#define TOY_CONFIG_PURSUIT_HEAVY_HP          200  /* 生命值 */
#define TOY_CONFIG_PURSUIT_HEAVY_SPEED_MIN    30  /* 最低速度 */
#define TOY_CONFIG_PURSUIT_HEAVY_SPEED_MAX    42  /* 最高速度 */
#define TOY_CONFIG_PURSUIT_HEAVY_BITE_DAMAGE   4  /* 咬伤 */
#define TOY_CONFIG_PURSUIT_FAST_HP            60  /* 生命值 */
#define TOY_CONFIG_PURSUIT_FAST_SPEED_MIN     92  /* 最低速度 */
#define TOY_CONFIG_PURSUIT_FAST_SPEED_MAX    112  /* 最高速度 */
#define TOY_CONFIG_PURSUIT_FAST_BITE_DAMAGE    2  /* 咬伤 */
#define TOY_CONFIG_SMOKER_HP                  90  /* 生命值 */
#define TOY_CONFIG_SMOKER_SPEED_MIN           34  /* 最低速度 */
#define TOY_CONFIG_SMOKER_SPEED_MAX           46  /* 最高速度 */
#define TOY_CONFIG_SMOKER_BITE_DAMAGE          0  /* 咬伤 */
#define TOY_CONFIG_CHARGER_HP                180  /* 生命值 */
#define TOY_CONFIG_CHARGER_SPEED_MIN          42  /* 最低速度 */
#define TOY_CONFIG_CHARGER_SPEED_MAX          58  /* 最高速度 */
#define TOY_CONFIG_CHARGER_BITE_DAMAGE         4  /* 咬伤 */

/* Charger special tuning. */
#define TOY_CONFIG_CHARGER_WINDUP_MS         700  /* 蓄力时间 */
#define TOY_CONFIG_CHARGER_DURATION_MS      7000  /* 冲锋时长 */
#define TOY_CONFIG_CHARGER_COOLDOWN_MS     15000  /* 技能冷却 */
#define TOY_CONFIG_CHARGER_SPEED              95  /* 冲锋速度 */
#define TOY_CONFIG_CHARGER_DAMAGE              6  /* 撞击伤害 */
#define TOY_CONFIG_CHARGER_IMPACT_DAMAGE       6  /* 范围撞击伤害 */
#define TOY_CONFIG_CHARGER_IMPACT_RANGE      320  /* 撞击范围 */

/* AI teammate health by class. */
#define TOY_CONFIG_AI_LEVEL_1_HP              80  /* 一级生命值 */
#define TOY_CONFIG_AI_LEVEL_2_HP             120  /* 二级生命值 */
#define TOY_CONFIG_AI_LEVEL_3_HP             160  /* 三级生命值 */

/* Developer display options. */
#define TOY_CONFIG_SHOW_MODEL_PATHS            0  /* 是否显示开发者模型路径 */

#endif
