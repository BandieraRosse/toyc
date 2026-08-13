#ifndef RASTERFALL_EFFECTS_H
#define RASTERFALL_EFFECTS_H

#include "core.h"
#include "toy_game.h"

#define RASTERFALL_TRACER_SLOTS 32
#define RASTERFALL_TRACER_LIFE_MS 160
#define RASTERFALL_TRACER_Y (-350)
#define RASTERFALL_PARTICLE_SLOTS 96
#define RASTERFALL_PARTICLE_LIFE_MS 240
#define RASTERFALL_PARTICLE_GRAVITY 4

/* Gameplay/network code emits these presentation cues; it does not own the
 * tracer/particle pools.  New weapons and abilities can add cue types here
 * without duplicating pool management in the main loop. */
enum rasterfall_effect_cue_type {
    RASTERFALL_EFFECT_CUE_TRACER,
    RASTERFALL_EFFECT_CUE_HIT_PARTICLES
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

struct rasterfall_effects {
    struct rasterfall_tracer tracers[RASTERFALL_TRACER_SLOTS];
    int tracer_next;
    struct rasterfall_particle particles[RASTERFALL_PARTICLE_SLOTS];
    int particle_next;
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

#endif
