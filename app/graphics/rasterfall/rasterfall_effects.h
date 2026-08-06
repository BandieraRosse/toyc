#ifndef RASTERFALL_EFFECTS_H
#define RASTERFALL_EFFECTS_H

#include "core.h"

#define RASTERFALL_TRACER_SLOTS 32
#define RASTERFALL_TRACER_LIFE_MS 160
#define RASTERFALL_TRACER_Y (-350)
#define RASTERFALL_PARTICLE_SLOTS 96
#define RASTERFALL_PARTICLE_LIFE_MS 240
#define RASTERFALL_PARTICLE_GRAVITY 4

struct rasterfall_tracer {
    int active;
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
    unsigned int last_network_fire_seq;
    uint32_t rng;
    int weapon_kick;
};

void rasterfall_effects_init(struct rasterfall_effects *effects);
void rasterfall_effects_update(struct rasterfall_effects *effects, int dt_ms);
void rasterfall_effects_reset_fire(struct rasterfall_effects *effects);
void rasterfall_effects_spawn_hit_particles(struct rasterfall_effects *effects,
                                            int x, int y, int z, int sy, int cy);

#endif
