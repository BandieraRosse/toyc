#ifndef RF_TACTICAL_WEAPON_H
#define RF_TACTICAL_WEAPON_H
/* Scenario labels and read-only metadata. Weapon authority is toy_game. */
enum rf_tw_kind { RF_TW_RIFLE, RF_TW_SMG, RF_TW_KIND_COUNT };
enum rf_tw_fire_mode { RF_TW_SINGLE, RF_TW_BURST, RF_TW_AUTO };
typedef struct rf_tw_state {
    int ammo, cooldown_ms, reload_remaining_ms;
} rf_tw_state;
float rf_tac_baseline(int field);
#define RF_TW_BASE_HP rf_tac_baseline(0)
#define RF_TW_BASE_RISK rf_tac_baseline(1)
#define RF_TW_BASE_RISK_REGEN rf_tac_baseline(2)
#define RF_TW_BASE_BODY_WIDTH_M rf_tac_baseline(3)
#define RF_TW_BASE_BODY_HEIGHT_M rf_tac_baseline(4)
#define RF_TW_BASE_MOVE_SPEED_MPS rf_tac_baseline(5)
#define RF_TW_BASE_RECOVERY_DELAY_MS ((int)rf_tac_baseline(6))
unsigned int rf_tw_rng_next(unsigned int *state);
const char *rf_tw_fire_mode_name(int mode);
#endif
