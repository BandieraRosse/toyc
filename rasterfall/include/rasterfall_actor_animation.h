#ifndef TOYC_RASTERFALL_ACTOR_ANIMATION_H
#define TOYC_RASTERFALL_ACTOR_ANIMATION_H

#include "toy_game.h"

/* Gameplay presentation is deliberately separate from imported skeletal
 * clips.  It can be replaced by authored actor clips without changing the
 * session state machine or the format-neutral animation API. */
#define RASTERFALL_RELOAD_WEAPON_PITCH 780
#define RASTERFALL_MOVE_LEG_SWING 520

struct rasterfall_actor_pose {
    int body_lift;
    int forward_shift;
    int leg_swing;
    int weapon_pitch;
    int body_pitch;
    int right_upper_pitch;
    int right_forearm_pitch;
    int left_upper_pitch;
    int left_forearm_pitch;
    int left_arm_rotation;
};

static inline void rasterfall_actor_animation_sample(
    int animation_id, int time_ms, int duration_ms,
    struct rasterfall_actor_pose *pose)
{
    int phase;
    if (!pose) return;
    pose->body_lift = 0;
    pose->forward_shift = 0;
    pose->leg_swing = 0;
    pose->weapon_pitch = 0;
    pose->body_pitch = 0;
    pose->right_upper_pitch = -60;
    pose->right_forearm_pitch = -30;
    pose->left_upper_pitch = -45;
    pose->left_forearm_pitch = -30;
    pose->left_arm_rotation = 0;
    if (time_ms < 0) time_ms = 0;
    if (animation_id == TOY_GAME_ANIM_IDLE) {
        phase = (time_ms / 100) % 8;
        pose->body_lift = (phase < 4 ? phase : 7 - phase) * 4;
    } else if (animation_id == TOY_GAME_ANIM_MOVE) {
        static const int leg_wave[16] = {
            0, 200, 380, 480, 520, 480, 380, 200,
            0, -200, -380, -480, -520, -480, -380, -200
        };
        phase = (time_ms / 25) % 16;
        pose->body_lift = phase < 8 ? phase * 3 / 2 : (15 - phase) * 3 / 2;
        pose->leg_swing = leg_wave[phase];
    } else if (animation_id == TOY_GAME_ANIM_FIRE) {
        pose->body_lift = time_ms < 60 ? -6 : 0;
        pose->forward_shift = time_ms < 80 ? -55 : 0;
    } else if (animation_id == TOY_GAME_ANIM_HIT) {
        pose->body_lift = time_ms < 100 ? -32 : 0;
        pose->body_pitch = time_ms < 140 ? 24 - time_ms * 24 / 140 : 0;
    } else if (animation_id == TOY_GAME_ANIM_RELOAD) {
        if (duration_ms <= 0)
            duration_ms = toy_game_animation_info(TOY_GAME_ANIM_RELOAD)->duration_ms;
        phase = time_ms * 1000 / duration_ms;
        if (phase > 1000) phase = 1000;
        if (phase < 500) phase *= 2;
        else phase = (1000 - phase) * 2;
        pose->weapon_pitch = phase * RASTERFALL_RELOAD_WEAPON_PITCH / 1000;
        pose->left_upper_pitch = -30 + phase * 20 / 1000;
        pose->left_forearm_pitch = -30 + phase * 65 / 1000;
    } else if (animation_id == TOY_GAME_ANIM_SHOVE) {
        if (duration_ms <= 0) duration_ms = TOY_CONFIG_SHOVE_ANIMATION_MS;
        phase = time_ms * 1000 / duration_ms;
        if (phase > 1000) phase = 1000;
        if (phase < 500) phase *= 2;
        else phase = (1000 - phase) * 2;
        pose->left_upper_pitch = -30;
        pose->left_forearm_pitch = -30;
        pose->left_arm_rotation = phase * TOY_CONFIG_SHOVE_SWEEP_DEGREES / 1000;
    } else if (animation_id == TOY_GAME_ANIM_MELEE) {
        if (duration_ms <= 0) duration_ms = TOY_CONFIG_MELEE_SWING_MS;
        phase = time_ms * 1000 / duration_ms;
        if (phase > 1000) phase = 1000;
        /* Wind up, strike across the body, then recover. */
        pose->body_pitch = phase < 350 ? -phase * 18 / 350 :
                           phase < 650 ? -18 + (phase - 350) * 42 / 300 :
                           24 - (phase - 650) * 24 / 350;
        pose->right_upper_pitch = -35 + phase * 45 / 1000;
        pose->right_forearm_pitch = -20 + phase * 55 / 1000;
    } else if (animation_id == TOY_GAME_ANIM_THROW) {
        if (duration_ms <= 0)
            duration_ms = toy_game_animation_info(TOY_GAME_ANIM_THROW)->duration_ms;
        phase = time_ms * 1000 / duration_ms;
        if (phase > 1000) phase = 1000;
        pose->right_upper_pitch = phase < 500 ? -60 - phase * 75 / 500 :
                                  -135 + (phase - 500) * 150 / 500;
        pose->right_forearm_pitch = phase < 500 ? -30 - phase * 45 / 500 :
                                    -75 + (phase - 500) * 75 / 500;
        pose->forward_shift = phase > 450 && phase < 750 ? 45 : 0;
    } else if (animation_id == TOY_GAME_ANIM_DOWNED) {
        pose->body_lift = -110;
        pose->body_pitch = 35;
    } else if (animation_id == TOY_GAME_ANIM_REVIVE) {
        if (duration_ms <= 0)
            duration_ms = toy_game_animation_info(TOY_GAME_ANIM_REVIVE)->duration_ms;
        phase = time_ms * 1000 / duration_ms;
        if (phase > 1000) phase = 1000;
        pose->body_lift = -(1000 - phase) * 110 / 1000;
        pose->body_pitch = (1000 - phase) * 35 / 1000;
    }
}

#endif
