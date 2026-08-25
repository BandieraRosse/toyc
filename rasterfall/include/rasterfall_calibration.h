#ifndef RASTERFALL_CALIBRATION_H
#define RASTERFALL_CALIBRATION_H

#include "toy_game.h"
#include "string.h"

/* Rasterfall canonical weapon space: +X right, +Y up, +Z muzzle/forward.
 * Coordinates are RFU; 512 RFU is one metre. */
struct rasterfall_cal_vec3 { int x, y, z; };
#define RASTERFALL_POSE_BODY_CHANNEL_COUNT 5
struct rasterfall_weapon_asset_profile {
    const char *model_path;
    int asset_basis;
    int skeletal;
    int base_scale_milli;
    /* Fixed source/asset attachment pivot.  Authored pose anchors are
     * evaluated relative to this pivot and must not move the mesh itself. */
    struct rasterfall_cal_vec3 attachment_grip;
};

struct rasterfall_pose_calibration {
    int character_id, weapon;
    int scale_milli;
    int yaw_offset, pitch_offset, roll_offset;
    struct rasterfall_cal_vec3 offset;
    struct rasterfall_cal_vec3 grip, foregrip, muzzle;
    /* UPPER_BODY, RIGHT_ARM, RIGHT_ELBOW, LEFT_ARM, LEFT_ELBOW. */
    int body_pose[RASTERFALL_POSE_BODY_CHANNEL_COUNT][3];
    int left_ik;
};

struct rasterfall_calibration_state {
    int active, axes, anchors, left_ik;
    int locomotion, fire_overlay;
    int animation_base, animation_overlay, animation_time_ms, animation_playing;
    int character, weapon;
    /* Presentation-layer Rifle Pose Editor state.  The old cal parser may
     * still edit the same values, but new authoring should use pose pages. */
    int page, selection, selected_bone, selected_axis, dirty;
    struct rasterfall_pose_calibration pose;
};

enum rasterfall_pose_editor_page {
    RASTERFALL_POSE_PAGE_BODY,
    RASTERFALL_POSE_PAGE_WEAPON,
    RASTERFALL_POSE_PAGE_ANCHORS,
    RASTERFALL_POSE_PAGE_ANIMATION,
    RASTERFALL_POSE_PAGE_COUNT
};

enum rasterfall_pose_editor_action {
    RASTERFALL_POSE_EDITOR_NONE,
    RASTERFALL_POSE_EDITOR_NEXT_PAGE,
    RASTERFALL_POSE_EDITOR_PREV_PAGE,
    RASTERFALL_POSE_EDITOR_NEXT_FIELD,
    RASTERFALL_POSE_EDITOR_PREV_FIELD,
    RASTERFALL_POSE_EDITOR_DECREASE,
    RASTERFALL_POSE_EDITOR_INCREASE,
    RASTERFALL_POSE_EDITOR_DECREASE_LARGE,
    RASTERFALL_POSE_EDITOR_INCREASE_LARGE,
    RASTERFALL_POSE_EDITOR_RESET,
    RASTERFALL_POSE_EDITOR_EXPORT,
    RASTERFALL_POSE_EDITOR_EXIT,
    RASTERFALL_POSE_EDITOR_TOGGLE_AXES,
    RASTERFALL_POSE_EDITOR_TOGGLE_ANCHORS,
    RASTERFALL_POSE_EDITOR_TOGGLE_IK,
    RASTERFALL_POSE_EDITOR_AXIS_X,
    RASTERFALL_POSE_EDITOR_AXIS_Y,
    RASTERFALL_POSE_EDITOR_AXIS_Z
    ,RASTERFALL_POSE_EDITOR_TOGGLE_ANIMATION_PLAY
};

void rasterfall_calibration_init(struct rasterfall_calibration_state *state);
void rasterfall_calibration_reset(struct rasterfall_calibration_state *state);
const struct rasterfall_weapon_asset_profile *rasterfall_weapon_asset_profile(
    int weapon);
const struct rasterfall_pose_calibration *rasterfall_pose_calibration_resolve(
    const struct rasterfall_calibration_state *editor,
    int character_id, int weapon);
const char *rasterfall_pose_character_name(int character_id);
const char *rasterfall_pose_weapon_name(int weapon);
void rasterfall_pose_export_path(const struct rasterfall_calibration_state *state,
                                 char *path, int path_size);
void rasterfall_calibration_dump(const struct rasterfall_calibration_state *state);
int rasterfall_calibration_editor_step(struct rasterfall_calibration_state *state,
                                       int action);
int rasterfall_calibration_export(const struct rasterfall_calibration_state *state);
int rasterfall_calibration_logic_test(void);

/* Convert an imported model point into canonical weapon space. */
void rasterfall_weapon_asset_to_canonical(int weapon, int x, int y, int z,
                                         int *out_x, int *out_y, int *out_z);

#endif
