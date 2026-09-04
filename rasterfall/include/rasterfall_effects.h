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
#define RASTERFALL_EFFECT_INSTANCE_SLOTS 2048
#define RASTERFALL_EFFECT_EMITTER_SLOTS 32

/* Runtime component types describe the low-level renderer primitive.  They
 * are intentionally separate from event types: one event may eventually
 * produce a different component or several instances. */
enum rasterfall_effect_instance_type {
    RASTERFALL_EFFECT_INSTANCE_PARTICLE,
    RASTERFALL_EFFECT_INSTANCE_RAY,
    RASTERFALL_EFFECT_INSTANCE_BILLBOARD,
    RASTERFALL_EFFECT_INSTANCE_OVERLAY,
    RASTERFALL_EFFECT_INSTANCE_EMITTER,
    RASTERFALL_EFFECT_INSTANCE_MATERIAL
};

/* Semantic variants remain separate from the low-level component.  These
 * names let the migration preserve existing behavior while renderers move to
 * the shared primitives one effect family at a time. */
enum rasterfall_effect_instance_kind {
    RASTERFALL_EFFECT_INSTANCE_KIND_NONE,
    RASTERFALL_EFFECT_INSTANCE_KIND_MUZZLE_FLASH,
    RASTERFALL_EFFECT_INSTANCE_KIND_TRACER,
    RASTERFALL_EFFECT_INSTANCE_KIND_HIT_PARTICLE,
    RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION,
    RASTERFALL_EFFECT_INSTANCE_KIND_FIRE,
    RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION_PARTICLE,
    RASTERFALL_EFFECT_INSTANCE_KIND_ENTITY_HIT_RAY,
    RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION_RAY,
    RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION_FLASH,
    RASTERFALL_EFFECT_INSTANCE_KIND_PROJECTILE_FLASH,
    RASTERFALL_EFFECT_INSTANCE_KIND_DAMAGE_FLASH,
    RASTERFALL_EFFECT_INSTANCE_KIND_ENEMY_HURT_TINT,
    RASTERFALL_EFFECT_INSTANCE_KIND_INTERACTION_HIGHLIGHT
};

/* Compatibility aliases for the first runtime prototype. */
enum rasterfall_effect_instance_compat_type {
    RASTERFALL_EFFECT_INSTANCE_MUZZLE_FLASH = RASTERFALL_EFFECT_INSTANCE_BILLBOARD,
    RASTERFALL_EFFECT_INSTANCE_TRACER = RASTERFALL_EFFECT_INSTANCE_RAY,
    RASTERFALL_EFFECT_INSTANCE_EXPLOSION = RASTERFALL_EFFECT_INSTANCE_EMITTER
};

/* Fixed-point runtime state.  Positions and velocities use the same integer
 * world units as the existing effect pools; velocity and gravity are applied
 * in Rasterfall's fixed 16ms simulation ticks.  Size is milli-scale and alpha
 * is 0..256.  This is deliberately a data-only foundation for future emitters.
 */
struct rasterfall_effect_instance {
    int active;
    int type;
    int kind;
    int flags;
    int source_id;
    int target_id;
    int weapon;
    unsigned int sequence;
    int x, y, z;
    /* Overlay components use x/y as screen coordinates and width/height as
     * a clipped screen-space rectangle. */
    int width, height;
    /* Ray components use an explicit endpoint; other components leave it
     * unused. */
    int ex, ey, ez;
    int dir_x, dir_y, dir_z;
    int vx, vy, vz;
    int gravity_y;
    int stretch_y;
    uint32_t color;
    int lifetime_ms;
    int age_ms;
    int size;
    int alpha;
};

/* Emitter configuration is presentation-only and fixed-capacity. It emits
 * instances into the shared runtime pool and never owns gameplay state. */
struct rasterfall_effect_emitter {
    int active;
    int source_id;
    int x, y, z;
    int dir_x, dir_y, dir_z;
    int lifetime_ms;
    int age_ms;
    int spawn_interval_ms;
    int spawn_accum_ms;
    int burst_count;
    int spawned_count;
    int spawn_limit;
    int child_type;
    int child_kind;
    int spread;
    int vx, vy, vz;
    int gravity_y;
    int size;
    int alpha;
    int phase_ms;
    int pattern;
    uint32_t color;
};

struct rasterfall_effects {
    struct rasterfall_effect_instance instances[RASTERFALL_EFFECT_INSTANCE_SLOTS];
    int instance_next;
    struct rasterfall_effect_emitter emitters[RASTERFALL_EFFECT_EMITTER_SLOTS];
    int emitter_next;
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
void rasterfall_effects_sync_fire_zones(
    struct rasterfall_effects *effects, const struct toy_game *game);
void rasterfall_effects_sync_projectile_flashes(
    struct rasterfall_effects *effects, const struct toy_game *game);
void rasterfall_effects_sync_damage_flash(
    struct rasterfall_effects *effects, const struct toy_game *game);
void rasterfall_effects_sync_enemy_feedback(
    struct rasterfall_effects *effects, const struct toy_game *game);
void rasterfall_effects_sync_interaction_highlight(
    struct rasterfall_effects *effects, int target_id,
    int x, int y, int z, int active);
struct rasterfall_effect_instance *rasterfall_effects_spawn_instance(
    struct rasterfall_effects *effects,
    const struct rasterfall_effect_instance *seed);
struct rasterfall_effect_emitter *rasterfall_effects_spawn_emitter(
    struct rasterfall_effects *effects,
    const struct rasterfall_effect_emitter *seed);
void rasterfall_effects_spawn_hit_particles(struct rasterfall_effects *effects,
                                            int x, int y, int z, int sy, int cy);
void rasterfall_effects_consume(struct rasterfall_effects *effects,
                                const struct rasterfall_effect_event *event);

#endif
