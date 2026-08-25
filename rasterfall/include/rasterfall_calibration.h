#ifndef RASTERFALL_CALIBRATION_H
#define RASTERFALL_CALIBRATION_H

#include "toy_game.h"

/* Rasterfall canonical weapon space: +X right, +Y up, +Z muzzle/forward.
 * Coordinates are RFU; 512 RFU is one metre. */
struct rasterfall_cal_vec3 { int x, y, z; };
struct rasterfall_weapon_visual_profile {
    const char *model_path;
    int scale_milli;
    int yaw_offset, pitch_offset, roll_offset;
    struct rasterfall_cal_vec3 grip, foregrip, muzzle, stock;
    /* Asset-space basis conversion, kept here instead of renderer branches. */
    int asset_basis;
};

struct rasterfall_character_attachment_profile {
    const char *right_hand_bone;
    const char *left_hand_bone;
    struct rasterfall_cal_vec3 right_grip_anchor;
    struct rasterfall_cal_vec3 left_grip_anchor;
    int right_yaw, right_pitch, right_roll;
    int left_yaw, left_pitch, left_roll;
};

struct rasterfall_calibration_state {
    int active, axes, anchors, left_ik;
    int locomotion, fire_overlay;
    int character, weapon;
    struct rasterfall_weapon_visual_profile weapon_profile;
    struct rasterfall_character_attachment_profile character_profile;
    int stance[8][3];
};

void rasterfall_calibration_init(struct rasterfall_calibration_state *state);
void rasterfall_calibration_reset(struct rasterfall_calibration_state *state);
struct rasterfall_weapon_visual_profile *rasterfall_calibration_weapon(
    struct rasterfall_calibration_state *state, int weapon);
const struct rasterfall_weapon_visual_profile *rasterfall_weapon_visual_profile(
    int weapon);
void rasterfall_calibration_apply_runtime(
    const struct rasterfall_weapon_visual_profile *profile);
void rasterfall_calibration_dump(const struct rasterfall_calibration_state *state);
int rasterfall_calibration_logic_test(void);

/* Convert an imported model point into canonical weapon space. */
void rasterfall_weapon_asset_to_canonical(int weapon, int x, int y, int z,
                                         int *out_x, int *out_y, int *out_z);

#endif
