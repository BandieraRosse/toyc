/*
 * Rasterfall gameplay tuning.
 *
 * Edit the values in this file to rebalance the game.  All distances and
 * movement values use the existing world units; timers are milliseconds.
 * The public TOY_GAME_* names are aliases in toy_game.h for compatibility.
 */
#ifndef RASTERFALL_TOY_GAME_CONFIG_H
#define RASTERFALL_TOY_GAME_CONFIG_H

/* General health and movement. */
#define TOY_CONFIG_PLAYER_HP                 100
#define TOY_CONFIG_SECONDARY_PLAYER_HP       100
#define TOY_CONFIG_PLAYER_MOVE_STEP          76
#define TOY_CONFIG_AI_RETURN_SPEED            38

/* Jump and forced-motion values. */
#define TOY_CONFIG_JUMP_MS                   900
#define TOY_CONFIG_JUMP_VELOCITY             110
#define TOY_CONFIG_AIRBORNE_MS               700
#define TOY_CONFIG_AIRBORNE_VELOCITY        220
#define TOY_CONFIG_AIRBORNE_GRAVITY           10
#define TOY_CONFIG_CHARGER_KNOCKBACK        1050

/* Weapon: mag, reserve, fire cooldown, reload, full-auto, pellets, spread,
 * slot, damage. */
#define TOY_CONFIG_PISTOL_MAG                30
#define TOY_CONFIG_PISTOL_RESERVE            (-1)
#define TOY_CONFIG_PISTOL_COOLDOWN_MS       200
#define TOY_CONFIG_PISTOL_RELOAD_MS        1500
#define TOY_CONFIG_PISTOL_FULL_AUTO           0
#define TOY_CONFIG_PISTOL_PELLETS             1
#define TOY_CONFIG_PISTOL_SPREAD             12
#define TOY_CONFIG_PISTOL_SLOT                1
#define TOY_CONFIG_PISTOL_DAMAGE             30

#define TOY_CONFIG_SMG_MAG                   50
#define TOY_CONFIG_SMG_RESERVE              650
#define TOY_CONFIG_SMG_COOLDOWN_MS          100
#define TOY_CONFIG_SMG_RELOAD_MS           2000
#define TOY_CONFIG_SMG_FULL_AUTO              1
#define TOY_CONFIG_SMG_PELLETS                1
#define TOY_CONFIG_SMG_SPREAD                90
#define TOY_CONFIG_SMG_SLOT                   0
#define TOY_CONFIG_SMG_DAMAGE                25

#define TOY_CONFIG_SHOTGUN_MAG                8
#define TOY_CONFIG_SHOTGUN_RESERVE           64
#define TOY_CONFIG_SHOTGUN_COOLDOWN_MS      600
#define TOY_CONFIG_SHOTGUN_RELOAD_MS       2500
#define TOY_CONFIG_SHOTGUN_FULL_AUTO          0
#define TOY_CONFIG_SHOTGUN_PELLETS           10
#define TOY_CONFIG_SHOTGUN_SPREAD           230
#define TOY_CONFIG_SHOTGUN_SLOT                0
#define TOY_CONFIG_SHOTGUN_DAMAGE             20

/* Enemy: hp, random movement speed range, bite damage. */
#define TOY_CONFIG_COMMON_HP                 50
#define TOY_CONFIG_COMMON_SPEED_MIN         38
#define TOY_CONFIG_COMMON_SPEED_MAX         56
#define TOY_CONFIG_COMMON_BITE_DAMAGE        2
#define TOY_CONFIG_FAST_HP                   50
#define TOY_CONFIG_FAST_SPEED_MIN           66
#define TOY_CONFIG_FAST_SPEED_MAX           82
#define TOY_CONFIG_FAST_BITE_DAMAGE          2
#define TOY_CONFIG_HEAVY_HP                 200
#define TOY_CONFIG_HEAVY_SPEED_MIN          24
#define TOY_CONFIG_HEAVY_SPEED_MAX          34
#define TOY_CONFIG_HEAVY_BITE_DAMAGE         4
#define TOY_CONFIG_PURSUIT_HEAVY_HP         200
#define TOY_CONFIG_PURSUIT_HEAVY_SPEED_MIN  30
#define TOY_CONFIG_PURSUIT_HEAVY_SPEED_MAX  42
#define TOY_CONFIG_PURSUIT_HEAVY_BITE_DAMAGE 4
#define TOY_CONFIG_PURSUIT_FAST_HP           60
#define TOY_CONFIG_PURSUIT_FAST_SPEED_MIN   92
#define TOY_CONFIG_PURSUIT_FAST_SPEED_MAX  112
#define TOY_CONFIG_PURSUIT_FAST_BITE_DAMAGE  2
#define TOY_CONFIG_SMOKER_HP                 90
#define TOY_CONFIG_SMOKER_SPEED_MIN          34
#define TOY_CONFIG_SMOKER_SPEED_MAX          46
#define TOY_CONFIG_SMOKER_BITE_DAMAGE        0
#define TOY_CONFIG_CHARGER_HP               180
#define TOY_CONFIG_CHARGER_SPEED_MIN         42
#define TOY_CONFIG_CHARGER_SPEED_MAX         58
#define TOY_CONFIG_CHARGER_BITE_DAMAGE       4

/* Charger special. */
#define TOY_CONFIG_CHARGER_WINDUP_MS        700
#define TOY_CONFIG_CHARGER_DURATION_MS     7000
#define TOY_CONFIG_CHARGER_COOLDOWN_MS    15000
#define TOY_CONFIG_CHARGER_SPEED             95
#define TOY_CONFIG_CHARGER_DAMAGE             6
#define TOY_CONFIG_CHARGER_IMPACT_DAMAGE      6
#define TOY_CONFIG_CHARGER_IMPACT_RANGE     320

/* AI teammate health by class. */
#define TOY_CONFIG_AI_LEVEL_1_HP             80
#define TOY_CONFIG_AI_LEVEL_2_HP            120
#define TOY_CONFIG_AI_LEVEL_3_HP            160

#endif
