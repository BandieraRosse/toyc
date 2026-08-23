#ifndef RASTERFALL_ANIMATION_H
#define RASTERFALL_ANIMATION_H

#include "core.h"
#include "string.h"
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
    /* Converts source translation units to model/world units.  Importers own
     * this boundary; the model solver must not know whether a clip was VMD,
     * glTF, or generated at runtime. */
    float translation_scale;
};
struct rasterfall_animation_rotation { int x, y, z; };
struct rasterfall_animation_player {
    const struct rasterfall_animation_clip *clip;
    int clip_id, time_ms, playing, loop, speed_milli;
};

static inline struct rasterfall_animation_quaternion
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

static inline struct rasterfall_animation_quaternion
rasterfall_animation_quat_normalize(struct rasterfall_animation_quaternion q)
{
    double n = sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (n < 0.0000001) q.x = q.y = q.z = 0.0, q.w = 1.0;
    else q.x /= n, q.y /= n, q.z /= n, q.w /= n;
    return q;
}

/* Normalized lerp is adequate for these short clips and avoids a trig-heavy
 * slerp path.  The sign fix preserves the shortest path. */
static inline struct rasterfall_animation_quaternion
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

static inline void rasterfall_animation_quat_to_euler(
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

static inline void rasterfall_animation_sample(
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

static inline void rasterfall_animation_player_update(
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

static inline int rasterfall_animation_logic_test(void)
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

#endif
