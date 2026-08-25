#include "rasterfall_calibration.h"
#include "string.h"
#include "tlibc_everything.h"

static struct rasterfall_weapon_visual_profile profiles[TOY_GAME_WEAPON_COUNT];
static int profiles_ready;
static struct rasterfall_weapon_visual_profile runtime_profile;
static int runtime_profile_enabled;

static void profile_init(void)
{
    int i;
    static const char *paths[TOY_GAME_WEAPON_COUNT] = {
        "rasterfall/assets/models/pg_glock1.rmesh", "rasterfall/assets/models/smg_mac10.rmesh",
        "rasterfall/assets/models/sg_pump_action.rmesh", "rasterfall/assets/models/ar_ak47.rmesh",
        "rasterfall/assets/models/rf_AWP.rmesh", "rasterfall/assets/models/axe.rmesh",
        "rasterfall/assets/models/bomb.rmesh", "rasterfall/assets/models/molotov.rmesh"
    };
    if (profiles_ready) return;
    memset(profiles, 0, sizeof(profiles));
    for (i = 0; i < TOY_GAME_WEAPON_COUNT; i++) {
        profiles[i].model_path = paths[i]; profiles[i].scale_milli = 1000;
    }
    /* The imported AK is reversed in asset space.  This is a source-to-
     * canonical correction, not actor yaw.  750 makes its bbox a usable rifle
     * size under the existing RFU model scale. */
    profiles[TOY_GAME_WEAPON_AK].scale_milli = 750;
    profiles[TOY_GAME_WEAPON_AK].asset_basis = 2;
    profiles[TOY_GAME_WEAPON_AK].grip = (struct rasterfall_cal_vec3){-18, -8, 24};
    profiles[TOY_GAME_WEAPON_AK].foregrip = (struct rasterfall_cal_vec3){0, -4, 190};
    profiles[TOY_GAME_WEAPON_AK].muzzle = (struct rasterfall_cal_vec3){0, 0, 420};
    profiles[TOY_GAME_WEAPON_AK].stock = (struct rasterfall_cal_vec3){0, 4, -190};
    profiles[TOY_GAME_WEAPON_AWP].scale_milli = 820;
    profiles_ready = 1;
}

const struct rasterfall_weapon_visual_profile *rasterfall_weapon_visual_profile(int weapon)
{
    profile_init();
    if (weapon < 0 || weapon >= TOY_GAME_WEAPON_COUNT) weapon = TOY_GAME_WEAPON_PISTOL;
    if (runtime_profile_enabled && weapon == TOY_GAME_WEAPON_AK)
        return &runtime_profile;
    return &profiles[weapon];
}

void rasterfall_calibration_apply_runtime(
    const struct rasterfall_weapon_visual_profile *profile)
{
    if (!profile) { runtime_profile_enabled = 0; return; }
    runtime_profile = *profile; runtime_profile_enabled = 1;
}

struct rasterfall_weapon_visual_profile *rasterfall_calibration_weapon(
    struct rasterfall_calibration_state *state, int weapon)
{
    profile_init();
    if (weapon < 0 || weapon >= TOY_GAME_WEAPON_COUNT) return NULL;
    if (state && state->weapon == weapon) return &state->weapon_profile;
    return &profiles[weapon];
}

void rasterfall_calibration_reset(struct rasterfall_calibration_state *state)
{
    const struct rasterfall_weapon_visual_profile *p;
    memset(state, 0, sizeof(*state)); state->character = 0;
    state->weapon = TOY_GAME_WEAPON_AK; state->left_ik = 0;
    p = rasterfall_weapon_visual_profile(state->weapon);
    memcpy(&state->weapon_profile, p, sizeof(*p));
    state->character_profile.right_hand_bone = "右手首";
    state->character_profile.left_hand_bone = "左手首";
    state->locomotion = 0; state->fire_overlay = 0;
}

void rasterfall_calibration_init(struct rasterfall_calibration_state *state)
{ profile_init(); runtime_profile_enabled = 0; rasterfall_calibration_reset(state); }

void rasterfall_weapon_asset_to_canonical(int weapon, int x, int y, int z,
                                          int *ox, int *oy, int *oz)
{
    int basis = rasterfall_weapon_visual_profile(weapon)->asset_basis;
    if (basis == 1) { *ox = z; *oy = y; *oz = x; }
    else if (basis == 2) { *ox = -x; *oy = y; *oz = -z; }
    else { *ox = x; *oy = y; *oz = z; }
}

static void dump_vec(const char *name, struct rasterfall_cal_vec3 v)
{ __printf("  %s = %d %d %d\n", name, v.x, v.y, v.z); }

void rasterfall_calibration_dump(const struct rasterfall_calibration_state *s)
{
    int i;
    __printf("calibration character=eula weapon=ak active=%d\n", s->active);
    __printf("weapon scale=%d yaw=%d pitch=%d roll=%d\n", s->weapon_profile.scale_milli,
             s->weapon_profile.yaw_offset, s->weapon_profile.pitch_offset, s->weapon_profile.roll_offset);
    dump_vec("grip", s->weapon_profile.grip); dump_vec("foregrip", s->weapon_profile.foregrip);
    dump_vec("muzzle", s->weapon_profile.muzzle); dump_vec("stock", s->weapon_profile.stock);
    __printf("right hand anchor bone=%s offset=%d %d %d rotation=%d %d %d\n",
             s->character_profile.right_hand_bone, s->character_profile.right_grip_anchor.x,
             s->character_profile.right_grip_anchor.y, s->character_profile.right_grip_anchor.z,
             s->character_profile.right_yaw, s->character_profile.right_pitch, s->character_profile.right_roll);
    __printf("left hand anchor bone=%s offset=%d %d %d\n", s->character_profile.left_hand_bone,
             s->character_profile.left_grip_anchor.x, s->character_profile.left_grip_anchor.y,
             s->character_profile.left_grip_anchor.z);
    __printf("left-hand IK enabled=%d axes=%d anchors=%d locomotion=%d fire_overlay=%d\n",
             s->left_ik, s->axes, s->anchors, s->locomotion, s->fire_overlay);
    for (i = 0; i < 8; i++) if (s->stance[i][0] || s->stance[i][1] || s->stance[i][2])
        __printf("stance[%d] = %d %d %d\n", i, s->stance[i][0], s->stance[i][1], s->stance[i][2]);
}

int rasterfall_calibration_logic_test(void)
{
    struct rasterfall_calibration_state s;
    int x, y, z;
    rasterfall_calibration_init(&s);
    if (s.weapon != TOY_GAME_WEAPON_AK || s.weapon_profile.scale_milli != 750) return 1;
    rasterfall_weapon_asset_to_canonical(TOY_GAME_WEAPON_AK, 1, 2, 3, &x, &y, &z);
    if (x != -1 || y != 2 || z != -3) return 2;
    return 0;
}
