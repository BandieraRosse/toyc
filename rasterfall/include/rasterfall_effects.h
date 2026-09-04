#ifndef RASTERFALL_EFFECTS_H
#define RASTERFALL_EFFECTS_H

#include "core.h"
#include "toy_game.h"
#include "rasterfall_effect_event.h"

#define RASTERFALL_TRACER_SLOTS 32
#define RASTERFALL_TRACER_LIFE_MS 160
#define RASTERFALL_TRACER_Y (-350)
#define RASTERFALL_PARTICLE_SLOTS 96
#define RASTERFALL_PARTICLE_LIFE_MS 240
#define RASTERFALL_PARTICLE_GRAVITY 4
#define RASTERFALL_MUZZLE_FLASH_SLOTS 16
#define RASTERFALL_MUZZLE_FLASH_LIFE_MS 70

/* Compatibility names for callers that still use the original pool-specific
 * cue.  New code should use rasterfall_effect_event directly. */
enum rasterfall_effect_cue_type {
    RASTERFALL_EFFECT_CUE_TRACER = RASTERFALL_EFFECT_EVENT_WEAPON_FIRE,
    RASTERFALL_EFFECT_CUE_HIT_PARTICLES = RASTERFALL_EFFECT_EVENT_BULLET_IMPACT
};

struct rasterfall_effect_cue {
    int type;
    int depth_test;
    int sx, sy, sz;
    int ex, ey, ez;
    int dir_sy, dir_cy;
    int life_ms;
};

struct rasterfall_tracer {
    int active;
    int depth_test;
    int sx, sy, sz;
    int ex, ey, ez;
    int life_ms;
};

struct rasterfall_particle {
    int active;
    int x, y, z;
    int vx, vy, vz;
    int life_ms;
};

struct rasterfall_muzzle_flash {
    int active;
    int x, y, z;
    int sy, cy;
    int weapon;
    int life_ms;
};

struct rasterfall_effects {
    struct rasterfall_tracer tracers[RASTERFALL_TRACER_SLOTS];
    int tracer_next;
    struct rasterfall_particle particles[RASTERFALL_PARTICLE_SLOTS];
    int particle_next;
    struct rasterfall_muzzle_flash muzzle_flashes[RASTERFALL_MUZZLE_FLASH_SLOTS];
    int muzzle_flash_next;
    unsigned int last_fire_seq;
    unsigned int last_network_fire_seq[4];
    unsigned int last_ai_fire_seq;
    unsigned int last_actor_fire_seq[TOY_GAME_MAX_ACTORS];
    uint32_t rng;
    int weapon_kick;
};

void rasterfall_effects_init(struct rasterfall_effects *effects);
void rasterfall_effects_update(struct rasterfall_effects *effects, int dt_ms);
void rasterfall_effects_reset_fire(struct rasterfall_effects *effects);
void rasterfall_effects_spawn_hit_particles(struct rasterfall_effects *effects,
                                            int x, int y, int z, int sy, int cy);
void rasterfall_effects_emit(struct rasterfall_effects *effects,
                             const struct rasterfall_effect_cue *cue);
void rasterfall_effects_consume(struct rasterfall_effects *effects,
                                const struct rasterfall_effect_event *event);

#endif
