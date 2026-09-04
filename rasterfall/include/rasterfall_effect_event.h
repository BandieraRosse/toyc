#ifndef RASTERFALL_EFFECT_EVENT_H
#define RASTERFALL_EFFECT_EVENT_H

/* Presentation-only combat events.  These are deliberately not part of
 * toy_game state or the network snapshot format.  A gameplay/session or
 * network adapter may produce them; the effects module consumes them. */
enum rasterfall_effect_event_type {
    RASTERFALL_EFFECT_EVENT_WEAPON_FIRE,
    RASTERFALL_EFFECT_EVENT_BULLET_IMPACT,
    RASTERFALL_EFFECT_EVENT_ENTITY_HIT,
    RASTERFALL_EFFECT_EVENT_EXPLOSION
};

enum rasterfall_effect_event_flags {
    RASTERFALL_EFFECT_EVENT_DEPTH_TEST = 1 << 0
};

struct rasterfall_effect_event {
    int type;
    int flags;
    int source_id;
    int target_id;
    int weapon;
    unsigned int sequence;

    /* Fixed-point world-space payload.  For weapon fire, start/end describe
     * one ray.  For impact/hit/explosion, x/y/z is the event position. */
    int sx, sy, sz;
    int ex, ey, ez;
    int x, y, z;
    int dir_sy, dir_cy;
    int life_ms;
};

#endif
