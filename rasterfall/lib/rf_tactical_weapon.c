#include "rf_tactical_weapon.h"
#include "toy_game.h"
#include "rasterfall_units.h"
float rf_tac_baseline(int field)
{
    struct toy_game_actor a={0};struct toy_game_capabilities caps;
    struct toy_game_gameplay_config rules;
    a.class_id=TOY_GAME_AI_LEVEL_2;
    toy_game_actor_capabilities(&a,TOY_GAME_WEAPON_AK,&caps);
    switch(field) {
    case 0:return caps.max_hp;
    case 1:return caps.evasion_capacity;
    case 2:return caps.evasion_per_second;
    case 3:return TOY_GAME_HIT_RADIUS*2/512.0f;
    case 4:return RASTERFALL_HUMAN_HEIGHT_RFU/512.0f;
    case 5:return toy_game_actor_move_step(&a,TOY_CONFIG_AI_RETURN_SPEED)*1000.0f/(16*512);
    default:toy_game_gameplay_defaults(&rules);return rules.evasion_recovery_delay_ms;
    }
}
unsigned int rf_tw_rng_next(unsigned int *state)
{
    unsigned x=*state?*state:0xa341316cu;x^=x<<13;x^=x>>17;x^=x<<5;*state=x;return x;
}
const char *rf_tw_fire_mode_name(int mode)
{return mode==RF_TW_SINGLE?"single":mode==RF_TW_BURST?"burst":"auto";}
