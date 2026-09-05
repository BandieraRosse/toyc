#ifndef RASTERFALL_EFFECT_EVENT_H
#define RASTERFALL_EFFECT_EVENT_H

/* Presentation-only combat events.  These are deliberately not part of
 * toy_game state or the network snapshot format.  A gameplay/session or
 * network adapter may produce them; the effects module consumes them. */
enum rasterfall_effect_event_type {
    RASTERFALL_EFFECT_EVENT_WEAPON_FIRE,
    RASTERFALL_EFFECT_EVENT_TRACER,
    RASTERFALL_EFFECT_EVENT_BULLET_IMPACT,
    RASTERFALL_EFFECT_EVENT_ENTITY_HIT,
    RASTERFALL_EFFECT_EVENT_EXPLOSION,
    RASTERFALL_EFFECT_EVENT_CAMERA_SHAKE
};

enum rasterfall_effect_event_flags {
    RASTERFALL_EFFECT_EVENT_DEPTH_TEST = 1 << 0,
    /* Only a local presentation event may affect the viewer's camera. */
    RASTERFALL_EFFECT_EVENT_LOCAL_VIEW = 1 << 1
};

struct rasterfall_effect_event {
    int type;
    int flags;
    int source_id;
    int target_id;
    int weapon;
    unsigned int sequence;

    /* Fixed-point world-space payload.  Weapon fire uses sx/sy/sz as the
     * muzzle position; tracer uses start/end for one ray.  Impact/hit/
     * explosion use x/y/z as the event position. */
    int sx, sy, sz;
    int ex, ey, ez;
    int x, y, z;
    int dir_sy, dir_cy;
    int life_ms;

    /* Camera-shake amplitudes in view units.  These are presentation-only
     * and are ignored by all non-camera-shake event types. */
    int shake_side, shake_up, shake_forward;
    int shake_yaw, shake_pitch;
};

#endif
