#ifndef RASTERFALL_EFFECTS_H
#define RASTERFALL_EFFECTS_H

#include "core.h"
#include "toy_game.h"
#include "rasterfall_camera.h"
#include "rasterfall_effect_event.h"

#define RASTERFALL_TRACER_LIFE_MS 72
#define RASTERFALL_TRACER_Y (-350)
#define RASTERFALL_PARTICLE_LIFE_MS 240
#define RASTERFALL_PARTICLE_GRAVITY 4
#define RASTERFALL_ENEMY_DEATH_FADE_MS 380
#define RASTERFALL_ENEMY_DEATH_FRAGMENT_LIFE_MS 1350
#define RASTERFALL_ENEMY_DEATH_DUST_LIFE_MS 1550
#define RASTERFALL_MUZZLE_FLASH_LIFE_MS 70
#define RASTERFALL_KNOCKBACK_TRAJECTORY_HISTORY_MS 400
#define RASTERFALL_KNOCKBACK_TRAIL_FADE_MS 200
#define RASTERFALL_KNOCKBACK_TRAIL_POINTS 16
#define RASTERFALL_KNOCKBACK_TRAIL_SAMPLE_MS 24
#define RASTERFALL_EFFECT_TRAJECTORY_IN_FLIGHT (1 << 2)
#define RASTERFALL_EFFECT_INSTANCE_SLOTS 2048
#define RASTERFALL_EFFECT_EMITTER_SLOTS 32

enum rasterfall_enemy_death_style {
    RASTERFALL_ENEMY_DEATH_STYLE_NONE,
    RASTERFALL_ENEMY_DEATH_STYLE_LEGACY,
    RASTERFALL_ENEMY_DEATH_STYLE_DISSOLVE
};

/* Local camera-shake tuning.  Translation values are view-space units;
 * yaw/pitch values use the camera's 1024 fixed-point angular units. */
#define RASTERFALL_CAMERA_SHAKE_PISTOL_LIFE_MS 110
#define RASTERFALL_CAMERA_SHAKE_SMG_LIFE_MS 150
#define RASTERFALL_CAMERA_SHAKE_AK_LIFE_MS 240
#define RASTERFALL_CAMERA_SHAKE_SHOTGUN_LIFE_MS 170
#define RASTERFALL_CAMERA_SHAKE_AWP_LIFE_MS 110
#define RASTERFALL_CAMERA_SHAKE_DEFAULT_SIDE 3
#define RASTERFALL_CAMERA_SHAKE_DEFAULT_UP 2
#define RASTERFALL_CAMERA_SHAKE_DEFAULT_FORWARD 3
#define RASTERFALL_CAMERA_SHAKE_DEFAULT_YAW 6
#define RASTERFALL_CAMERA_SHAKE_DEFAULT_PITCH 8
#define RASTERFALL_CAMERA_SHAKE_AK_SIDE 4
#define RASTERFALL_CAMERA_SHAKE_AK_UP 3
#define RASTERFALL_CAMERA_SHAKE_AK_FORWARD 4
#define RASTERFALL_CAMERA_SHAKE_AK_YAW 8
#define RASTERFALL_CAMERA_SHAKE_AK_PITCH 18
#define RASTERFALL_CAMERA_SHAKE_AWP_SIDE 2
#define RASTERFALL_CAMERA_SHAKE_AWP_UP 1
#define RASTERFALL_CAMERA_SHAKE_AWP_FORWARD 6
#define RASTERFALL_CAMERA_SHAKE_AWP_YAW 3
#define RASTERFALL_CAMERA_SHAKE_AWP_PITCH 5
#define RASTERFALL_CAMERA_SHAKE_SHOTGUN_SIDE 7
#define RASTERFALL_CAMERA_SHAKE_SHOTGUN_UP 6
#define RASTERFALL_CAMERA_SHAKE_SHOTGUN_FORWARD 3
#define RASTERFALL_CAMERA_SHAKE_SHOTGUN_YAW 12
#define RASTERFALL_CAMERA_SHAKE_SHOTGUN_PITCH 16
#define RASTERFALL_CAMERA_SHAKE_PISTOL_MAX_SIDE 8
#define RASTERFALL_CAMERA_SHAKE_PISTOL_MAX_UP 6
#define RASTERFALL_CAMERA_SHAKE_PISTOL_MAX_FORWARD 5
#define RASTERFALL_CAMERA_SHAKE_PISTOL_MAX_YAW 14
#define RASTERFALL_CAMERA_SHAKE_PISTOL_MAX_PITCH 18
#define RASTERFALL_CAMERA_SHAKE_SMG_MAX_SIDE 8
#define RASTERFALL_CAMERA_SHAKE_SMG_MAX_UP 6
#define RASTERFALL_CAMERA_SHAKE_SMG_MAX_FORWARD 6
#define RASTERFALL_CAMERA_SHAKE_SMG_MAX_YAW 16
#define RASTERFALL_CAMERA_SHAKE_SMG_MAX_PITCH 20
#define RASTERFALL_CAMERA_SHAKE_AK_MAX_SIDE 12
#define RASTERFALL_CAMERA_SHAKE_AK_MAX_UP 10
#define RASTERFALL_CAMERA_SHAKE_AK_MAX_FORWARD 8
#define RASTERFALL_CAMERA_SHAKE_AK_MAX_YAW 24
#define RASTERFALL_CAMERA_SHAKE_AK_MAX_PITCH 32
#define RASTERFALL_CAMERA_SHAKE_SHOTGUN_MAX_SIDE 10
#define RASTERFALL_CAMERA_SHAKE_SHOTGUN_MAX_UP 9
#define RASTERFALL_CAMERA_SHAKE_SHOTGUN_MAX_FORWARD 6
#define RASTERFALL_CAMERA_SHAKE_SHOTGUN_MAX_YAW 18
#define RASTERFALL_CAMERA_SHAKE_SHOTGUN_MAX_PITCH 28
#define RASTERFALL_CAMERA_SHAKE_AWP_MAX_SIDE 5
#define RASTERFALL_CAMERA_SHAKE_AWP_MAX_UP 4
#define RASTERFALL_CAMERA_SHAKE_AWP_MAX_FORWARD 6
#define RASTERFALL_CAMERA_SHAKE_AWP_MAX_YAW 8
#define RASTERFALL_CAMERA_SHAKE_AWP_MAX_PITCH 10
#define RASTERFALL_CAMERA_SHAKE_SMOOTHING 160

/* Damage feedback preset. Angles are degrees; the camera uses 1024 units. */
#define RASTERFALL_DAMAGE_CAMERA_SHAKE_ANGLE_DEGREES 15
#define RASTERFALL_DAMAGE_CAMERA_SHAKE_RISE_MS 45
#define RASTERFALL_DAMAGE_CAMERA_SHAKE_HOLD_MS 55
#define RASTERFALL_DAMAGE_CAMERA_SHAKE_LIFE_MS 500
#define RASTERFALL_DAMAGE_CAMERA_SHAKE_MIN_INTERVAL_MS 120
#define RASTERFALL_DAMAGE_FLASH_LIFE_MS 140

/* Runtime component types describe the low-level renderer primitive.  They
 * are intentionally separate from event types: one event may eventually
 * produce a different component or several instances. */
enum rasterfall_effect_instance_type {
    RASTERFALL_EFFECT_INSTANCE_PARTICLE,
    RASTERFALL_EFFECT_INSTANCE_RAY,
    RASTERFALL_EFFECT_INSTANCE_BILLBOARD,
    RASTERFALL_EFFECT_INSTANCE_OVERLAY,
    RASTERFALL_EFFECT_INSTANCE_EMITTER,
    RASTERFALL_EFFECT_INSTANCE_MATERIAL,
    RASTERFALL_EFFECT_INSTANCE_CAMERA_SHAKE
};

/* Semantic variants remain separate from the low-level component.  These
 * names let the migration preserve existing behavior while renderers move to
 * the shared primitives one effect family at a time. */
enum rasterfall_effect_instance_kind {
    RASTERFALL_EFFECT_INSTANCE_KIND_NONE,
    RASTERFALL_EFFECT_INSTANCE_KIND_MUZZLE_FLASH,
    RASTERFALL_EFFECT_INSTANCE_KIND_MUZZLE_FLASH_CORE,
    RASTERFALL_EFFECT_INSTANCE_KIND_MUZZLE_FLASH_OUTER,
    RASTERFALL_EFFECT_INSTANCE_KIND_MUZZLE_FLASH_LOBE,
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
    RASTERFALL_EFFECT_INSTANCE_KIND_ENEMY_DEATH,
    RASTERFALL_EFFECT_INSTANCE_KIND_ENEMY_DEATH_FRAGMENT,
    RASTERFALL_EFFECT_INSTANCE_KIND_ENEMY_DEATH_DUST,
    RASTERFALL_EFFECT_INSTANCE_KIND_INTERACTION_HIGHLIGHT,
    RASTERFALL_EFFECT_INSTANCE_KIND_CAMERA_SHAKE,
    RASTERFALL_EFFECT_INSTANCE_KIND_KNOCKBACK_TRAJECTORY
};

/* Emitter child placement is a runtime policy, not an asset format. */
enum rasterfall_effect_emitter_pattern {
    RASTERFALL_EFFECT_EMITTER_PATTERN_DEFAULT,
    RASTERFALL_EFFECT_EMITTER_PATTERN_FIRE,
    RASTERFALL_EFFECT_EMITTER_PATTERN_EXPLOSION
};

#define RASTERFALL_EFFECT_EMITTER_CHILD_SLOTS 3

struct rasterfall_effect_emitter_child {
    int type;
    int kind;
    int flags;
    int spawn_limit;
    int spawned_count;
    int lifetime_ms;
    int size;
    int alpha;
    int spread;
    int vx, vy, vz;
    int gravity_y;
    int pattern;
    int ex, ey, ez;
    uint32_t color;
};

struct rasterfall_knockback_trail_point {
    int x, y, z;
    int age_ms;
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
    /* Tracer-only presentation controls.  The ray endpoints remain the
     * gameplay/presentation payload; these fields only shape its rendering. */
    int ray_tail_percent;
    int ray_width;
    uint32_t ray_end_color;
    int dir_x, dir_y, dir_z;
    int vx, vy, vz;
    int gravity_y;
    int curve_duration_ms;
    int curve_flight_ms;
    int trail_count;
    int trail_head;
    struct rasterfall_knockback_trail_point
        trail[RASTERFALL_KNOCKBACK_TRAIL_POINTS];
    int stretch_y;
    uint32_t color;
    int lifetime_ms;
    int age_ms;
    int size;
    int alpha;
    /* Camera-shake components use these as view-space amplitudes. */
    int shake_side, shake_up, shake_forward;
    int shake_yaw, shake_pitch;
    uint32_t shake_seed;
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
    int child_count;
    struct rasterfall_effect_emitter_child
        children[RASTERFALL_EFFECT_EMITTER_CHILD_SLOTS];
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
    /* Smoothed aggregate camera-shake state; never part of gameplay state. */
    int camera_shake_side, camera_shake_up, camera_shake_forward;
    int camera_shake_yaw, camera_shake_pitch;
    int last_player_hp;
    int damage_shake_cooldown_ms;
    unsigned char enemy_death_seen[TOY_GAME_MAX_ENEMIES];
    uint64_t enemy_special_hit_seen[TOY_GAME_MAX_ENEMIES];
    unsigned char enemy_death_style[TOY_GAME_MAX_ENEMIES];
    int enemy_hit_dir_x[TOY_GAME_MAX_ENEMIES];
    int enemy_hit_dir_z[TOY_GAME_MAX_ENEMIES];
    int enemy_hit_strength[TOY_GAME_MAX_ENEMIES];
};

void rasterfall_effects_init(struct rasterfall_effects *effects);
void rasterfall_effects_update(struct rasterfall_effects *effects, int dt_ms);
void rasterfall_effects_apply_camera_shake(
    const struct rasterfall_effects *effects, struct camera *render_camera);
void rasterfall_effects_reset_fire(struct rasterfall_effects *effects);
void rasterfall_effects_sync_fire_zones(
    struct rasterfall_effects *effects, const struct toy_game *game);
void rasterfall_effects_sync_projectile_flashes(
    struct rasterfall_effects *effects, const struct toy_game *game);
void rasterfall_effects_sync_damage_flash(
    struct rasterfall_effects *effects, const struct toy_game *game,
    const struct camera *camera);
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
