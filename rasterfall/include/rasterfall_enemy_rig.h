#ifndef RASTERFALL_ENEMY_RIG_H
#define RASTERFALL_ENEMY_RIG_H

/* Presentation only. RFU, +Y up, +Z forward; angles in degrees.
 * These rigid channels are not RFCHAR roles, bones, or skin weights. */
enum enemy_rig_channel {
    ER_ROOT, ER_TORSO, ER_HEAD,
    ER_ARM_L, ER_FOREARM_L, ER_HAND_L,
    ER_ARM_R, ER_FOREARM_R, ER_HAND_R,
    ER_LEG_L, ER_LEG_R, ER_COUNT
};
struct enemy_rig_transform { int x, y, z, pitch, yaw, roll; };
struct enemy_rig_pose { struct enemy_rig_transform channel[ER_COUNT]; };
struct enemy_rig_part {
    int channel, x, y, z, hx, hy, hz;
    unsigned color;
};
struct enemy_rig_profile {
    int type;
    struct enemy_rig_transform bind[ER_COUNT];
    const struct enemy_rig_part *parts;
    int count, stride, leg_swing, arm_swing, sway, size_milli;
};
struct enemy_rig_input {
    int type, stride_phase, moving;
    int windup_q10, attack_ms, recovery_ms, charging, impact_age_ms;
    int tongue_active;
};
#endif
