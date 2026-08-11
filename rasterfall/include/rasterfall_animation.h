#ifndef RASTERFALL_ANIMATION_H
#define RASTERFALL_ANIMATION_H

#include "toy_game.h"

/* Presentation-only result of sampling a gameplay animation state.  The
 * renderer can later replace these procedural poses with skeleton channels
 * without changing actor state or network code. */
struct rasterfall_animation_pose {
    int body_lift;
    int forward_shift;
    int leg_swing;
    int weapon_pitch;
};

static void rasterfall_animation_sample(int animation_id, int time_ms,
                                        struct rasterfall_animation_pose *pose)
{
    int phase;
    if (!pose) return;
    pose->body_lift = 0;
    pose->forward_shift = 0;
    pose->leg_swing = 0;
    pose->weapon_pitch = 0;
    if (time_ms < 0) time_ms = 0;
    if (animation_id == TOY_GAME_ANIM_IDLE) {
        phase = (time_ms / 100) % 8;
        pose->body_lift = (phase < 4 ? phase : 7 - phase) * 4;
    } else if (animation_id == TOY_GAME_ANIM_MOVE) {
        phase = (time_ms / 50) % 8;
        pose->body_lift = (phase < 4 ? phase : 7 - phase) * 3;
        pose->leg_swing = phase < 4 ? 1 : -1;
    } else if (animation_id == TOY_GAME_ANIM_FIRE) {
        pose->body_lift = time_ms < 60 ? -24 : 0;
    } else if (animation_id == TOY_GAME_ANIM_HIT) {
        pose->body_lift = time_ms < 70 ? -18 : 0;
        pose->forward_shift = time_ms < 140 ? 90 - time_ms * 3 / 5 : 0;
    } else if (animation_id == TOY_GAME_ANIM_RELOAD) {
        phase = time_ms * 1000 / 900;
        if (phase > 1000) phase = 1000;
        if (phase < 500) phase *= 2;
        else phase = (1000 - phase) * 2;
        pose->weapon_pitch = phase * 260 / 1000;
    }
}

#endif
