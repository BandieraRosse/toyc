#ifndef RASTERFALL_ANIMATION_H
#define RASTERFALL_ANIMATION_H

#include "toy_game.h"
#include "math.h"

/* PMX/glTF/VMD-neutral runtime clip representation.  Importers only need to
 * resolve their source bone names to target_bone indices. */
struct rasterfall_animation_quaternion { double x, y, z, w; };
struct rasterfall_animation_keyframe {
    int time_ms;
    struct rasterfall_animation_quaternion rotation;
    float tx, ty, tz;
};
struct rasterfall_animation_track {
    int target_bone;
    const struct rasterfall_animation_keyframe *keys;
    int key_count;
};
struct rasterfall_animation_clip {
    int duration_ms;
    int loop;
    const struct rasterfall_animation_track *tracks;
    int track_count;
};
struct rasterfall_animation_rotation { int x, y, z; };
struct rasterfall_animation_player {
    const struct rasterfall_animation_clip *clip;
    int clip_id, time_ms, playing, loop, speed_milli;
};

static struct rasterfall_animation_quaternion
rasterfall_animation_quat_from_euler(int x, int y, int z)
{
    double sx = sin(x * M_PI / 360.0), cx = cos(x * M_PI / 360.0);
    double sy = sin(y * M_PI / 360.0), cy = cos(y * M_PI / 360.0);
    double sz = sin(z * M_PI / 360.0), cz = cos(z * M_PI / 360.0);
    struct rasterfall_animation_quaternion q;
    q.w = cx*cy*cz + sx*sy*sz; q.x = sx*cy*cz - cx*sy*sz;
    q.y = cx*sy*cz + sx*cy*sz; q.z = cx*cy*sz - sx*sy*cz;
    return q;
}

static struct rasterfall_animation_quaternion
rasterfall_animation_quat_normalize(struct rasterfall_animation_quaternion q)
{
    double n = sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (n < 0.0000001) q.x = q.y = q.z = 0.0, q.w = 1.0;
    else q.x /= n, q.y /= n, q.z /= n, q.w /= n;
    return q;
}

/* Normalized lerp is adequate for these short clips and avoids a trig-heavy
 * slerp path.  The sign fix preserves the shortest path. */
static struct rasterfall_animation_quaternion
rasterfall_animation_quat_nlerp(struct rasterfall_animation_quaternion a,
                                 struct rasterfall_animation_quaternion b,
                                 int factor_milli)
{
    double t = factor_milli / 1000.0;
    if (a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w < 0.0)
        b.x = -b.x, b.y = -b.y, b.z = -b.z, b.w = -b.w;
    a.x += (b.x-a.x)*t; a.y += (b.y-a.y)*t;
    a.z += (b.z-a.z)*t; a.w += (b.w-a.w)*t;
    return rasterfall_animation_quat_normalize(a);
}

static void rasterfall_animation_quat_to_euler(
    struct rasterfall_animation_quaternion q,
    struct rasterfall_animation_rotation *out)
{
    double sy, x, y, z;
    q = rasterfall_animation_quat_normalize(q);
    sy = 2.0 * (q.w*q.y - q.x*q.z);
    if (sy > 1.0) sy = 1.0;
    if (sy < -1.0) sy = -1.0;
    y = atan2(sy, sqrt(1.0 - sy*sy));
    x = atan2(2.0*(q.w*q.x + q.y*q.z),
              1.0 - 2.0*(q.x*q.x + q.y*q.y));
    z = atan2(2.0*(q.w*q.z + q.x*q.y),
              1.0 - 2.0*(q.y*q.y + q.z*q.z));
    out->x = (int)(x * 180.0 / M_PI + (x < 0 ? -0.5 : 0.5));
    out->y = (int)(y * 180.0 / M_PI + (y < 0 ? -0.5 : 0.5));
    out->z = (int)(z * 180.0 / M_PI + (z < 0 ? -0.5 : 0.5));
}

static void rasterfall_animation_sample(
    const struct rasterfall_animation_clip *clip, int time_ms,
    struct rasterfall_animation_rotation *out, int out_count)
{
    int i, k;
    if (!out || out_count <= 0) return;
    for (i = 0; i < out_count; i++) out[i].x = out[i].y = out[i].z = 0;
    if (!clip || clip->duration_ms < 0) return;
    if (time_ms < 0) time_ms = 0;
    if (clip->loop && clip->duration_ms > 0 && time_ms >= clip->duration_ms)
        time_ms %= clip->duration_ms;
    for (i = 0; i < clip->track_count; i++) {
        const struct rasterfall_animation_track *track = &clip->tracks[i];
        const struct rasterfall_animation_keyframe *a, *b;
        int factor = 0;
        if (track->target_bone < 0 || track->target_bone >= out_count ||
            !track->keys || track->key_count <= 0) continue;
        a = b = &track->keys[0];
        for (k = 1; k < track->key_count; k++) {
            if (time_ms < track->keys[k].time_ms) { b = &track->keys[k]; break; }
            a = &track->keys[k];
        }
        if (b != a && b->time_ms > a->time_ms)
            factor = (time_ms-a->time_ms)*1000/(b->time_ms-a->time_ms);
        /* VMD tracks often end before the clip duration.  Holding the last
         * key until the modulo wrap creates a visible discontinuity at every
         * loop.  For a looped track, connect its last key to its first key in
         * the next cycle.  Existing demo clips start/end at the duration, so
         * this is a no-op for them. */
        if (clip->loop && clip->duration_ms > 0 &&
            a == &track->keys[track->key_count - 1] &&
            track->key_count > 1 && time_ms > a->time_ms &&
            track->keys[0].time_ms < clip->duration_ms) {
            int next_time = clip->duration_ms;
            if (next_time > a->time_ms)
                factor = (time_ms-a->time_ms)*1000 /
                         (next_time-a->time_ms);
            b = &track->keys[0];
        }
        if (factor < 0) factor = 0;
        if (factor > 1000) factor = 1000;
        rasterfall_animation_quat_to_euler(
            rasterfall_animation_quat_nlerp(a->rotation, b->rotation, factor),
            &out[track->target_bone]);
    }
}

static void rasterfall_animation_player_update(
    struct rasterfall_animation_player *player, int dt_ms)
{
    long next;
    if (!player || !player->playing || !player->clip || dt_ms <= 0) return;
    next = player->time_ms + (long)dt_ms * player->speed_milli / 1000;
    if (player->clip->duration_ms <= 0) { player->time_ms = 0; player->playing = 0; return; }
    if (player->loop) player->time_ms = (int)(next % player->clip->duration_ms);
    else if (next >= player->clip->duration_ms)
        player->time_ms = player->clip->duration_ms, player->playing = 0;
    else player->time_ms = (int)next;
}

static int rasterfall_animation_logic_test(void)
{
    struct rasterfall_animation_keyframe k[3];
    struct rasterfall_animation_track t[2];
    struct rasterfall_animation_clip c;
    struct rasterfall_animation_player player;
    struct rasterfall_animation_rotation out[3];
    k[0].time_ms=0; k[0].rotation=rasterfall_animation_quat_from_euler(0,0,0);
    k[1].time_ms=50; k[1].rotation=rasterfall_animation_quat_from_euler(0,0,30);
    k[2].time_ms=100; k[2].rotation=rasterfall_animation_quat_from_euler(0,0,0);
    t[0].target_bone=0; t[0].keys=k; t[0].key_count=3;
    t[1].target_bone=1; t[1].keys=k; t[1].key_count=3;
    c.duration_ms=100; c.loop=1; c.tracks=t; c.track_count=2;
    rasterfall_animation_sample(&c,0,out,3); if (out[0].z || out[1].z || out[2].z) return 1;
    rasterfall_animation_sample(&c,50,out,3); if (out[0].z != 30 || out[1].z != 30) return 2;
    rasterfall_animation_sample(&c,100,out,3); if (out[0].z || out[1].z) return 3;
    rasterfall_animation_sample(&c,125,out,3); if (out[0].z != 15 || out[1].z != 15 || out[2].z) return 4;
    memset(&player, 0, sizeof(player));
    player.clip = &c; player.playing = 1; player.loop = 1;
    player.speed_milli = 1000; player.time_ms = 90;
    rasterfall_animation_player_update(&player, 20);
    if (player.time_ms != 10 || !player.playing) return 5;
    c.loop = 0; player.clip = &c; player.loop = 0; player.time_ms = 90;
    rasterfall_animation_sample(&c,130,out,3);
    if (out[0].z || out[1].z || out[2].z) return 6;
    rasterfall_animation_player_update(&player, 20);
    if (player.time_ms != 100 || player.playing) return 7;
    /* A looped VMD track may end before the clip duration.  It must blend
     * toward its first key instead of holding the last pose until wrap. */
    k[0].time_ms = 10; k[0].rotation = rasterfall_animation_quat_from_euler(0,0,0);
    k[1].time_ms = 20; k[1].rotation = rasterfall_animation_quat_from_euler(0,0,30);
    t[0].target_bone = 0; t[0].keys = k; t[0].key_count = 2;
    c.duration_ms = 30; c.loop = 1; c.tracks = t; c.track_count = 1;
    rasterfall_animation_sample(&c, 29, out, 3);
    if (out[0].z < 1 || out[0].z > 5) return 8;
    return 0;
}

/* Presentation tuning knobs.  Keep these here so reload and locomotion can
 * be adjusted without changing gameplay timings or animation state IDs. */
#define RASTERFALL_RELOAD_WEAPON_PITCH 780
#define RASTERFALL_MOVE_LEG_SWING 520

/* Presentation-only result of sampling a gameplay animation state.  The
 * renderer can later replace these procedural poses with skeleton channels
 * without changing actor state or network code. */
struct rasterfall_animation_pose {
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

static void rasterfall_animation_sample_duration(
    int animation_id, int time_ms, int duration_ms,
    struct rasterfall_animation_pose *pose)
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
    /* The left forearm drops toward the weapon receiver instead of lifting
     * away from it in the normal carry pose. */
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
        /* Recoil moves the actor slightly backward, not downward. */
        pose->body_lift = time_ms < 60 ? -6 : 0;
        pose->forward_shift = time_ms < 80 ? -55 : 0;
    } else if (animation_id == TOY_GAME_ANIM_HIT) {
        /* A hit compresses the torso and tips its upper edge forward. */
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
        /* During the first half the left hand leaves the weapon; during the
         * second half it rises toward the front of the weapon. */
        pose->left_upper_pitch = -30 + phase * 20 / 1000;
        pose->left_forearm_pitch = -30 + phase * 65 / 1000;
    } else if (animation_id == TOY_GAME_ANIM_SHOVE) {
        if (duration_ms <= 0)
            duration_ms = TOY_CONFIG_SHOVE_ANIMATION_MS;
        phase = time_ms * 1000 / duration_ms;
        if (phase > 1000) phase = 1000;
        if (phase < 500) phase *= 2;
        else phase = (1000 - phase) * 2;
        /* Rotate the complete left arm once around the shoulder/elbow plane.
         * Segment lengths remain fixed; only their Y/Z direction changes. */
        pose->left_upper_pitch = -30;
        pose->left_forearm_pitch = -30;
        pose->left_arm_rotation = phase * TOY_CONFIG_SHOVE_SWEEP_DEGREES / 1000;
    }
}

#endif
