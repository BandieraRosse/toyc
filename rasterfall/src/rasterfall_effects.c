#include "tlibc_everything.h"
#include "math.h"
#include "rasterfall_effects.h"

static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int effect_rand(struct rasterfall_effects *effects, int lo, int hi)
{
    int span = hi - lo + 1;
    return lo + (int)(xorshift32(&effects->rng) % (uint32_t)span);
}

static int shake_noise(uint32_t seed)
{
    seed ^= seed >> 16;
    seed *= 0x7FEB352D;
    seed ^= seed >> 15;
    seed *= 0x846CA68B;
    seed ^= seed >> 16;
    return (int)(seed % 2001u) - 1000;
}

static int shake_sample(int amplitude, uint32_t seed, int age_ms,
                        int lifetime_ms)
{
    int envelope;
    if (amplitude == 0 || lifetime_ms <= 0 || age_ms >= lifetime_ms)
        return 0;
    envelope = (lifetime_ms - age_ms) * 256 / lifetime_ms;
    return amplitude * shake_noise(seed + (uint32_t)(age_ms / 16) * 2654435761u) *
           envelope / (1000 * 256);
}

static int shake_recoil_sample(int amplitude, uint32_t seed, int age_ms,
                               int lifetime_ms)
{
    int envelope;
    if (amplitude == 0 || lifetime_ms <= 0 || age_ms >= lifetime_ms)
        return 0;
    envelope = (lifetime_ms - age_ms) * 256 / lifetime_ms;
    /* Pitch is recoil-biased: it rises on every shot while noise keeps the
     * result from looking perfectly mechanical. */
    return amplitude * (1000 + shake_noise(seed)) * envelope /
           (1000 * 256);
}

static int damage_shake_angle(void)
{
    return RASTERFALL_DAMAGE_CAMERA_SHAKE_ANGLE_DEGREES * 1024 / 360;
}

static int damage_shake_sample(int amplitude, int age_ms)
{
    int decay_ms = RASTERFALL_DAMAGE_CAMERA_SHAKE_LIFE_MS -
                   RASTERFALL_DAMAGE_CAMERA_SHAKE_RISE_MS -
                   RASTERFALL_DAMAGE_CAMERA_SHAKE_HOLD_MS;
    if (age_ms < 0 || age_ms >= RASTERFALL_DAMAGE_CAMERA_SHAKE_LIFE_MS)
        return 0;
    if (age_ms < RASTERFALL_DAMAGE_CAMERA_SHAKE_RISE_MS)
        return amplitude * age_ms / RASTERFALL_DAMAGE_CAMERA_SHAKE_RISE_MS;
    if (age_ms < RASTERFALL_DAMAGE_CAMERA_SHAKE_RISE_MS +
                  RASTERFALL_DAMAGE_CAMERA_SHAKE_HOLD_MS)
        return amplitude;
    return amplitude * (RASTERFALL_DAMAGE_CAMERA_SHAKE_LIFE_MS - age_ms) /
           decay_ms;
}

static int shake_clamp(int value, int limit)
{
    if (value < -limit) return -limit;
    if (value > limit) return limit;
    return value;
}

static int shake_approach(int current, int target, int dt_ms)
{
    int amount = dt_ms * RASTERFALL_CAMERA_SHAKE_SMOOTHING / 16;
    int delta, step;
    if (amount < 1) amount = 1;
    if (amount > 256) amount = 256;
    delta = target - current;
    step = delta * amount / 256;
    if (delta && step == 0) step = delta > 0 ? 1 : -1;
    return current + step;
}

static int camera_shake_lifetime(int weapon)
{
    if (weapon == TOY_GAME_WEAPON_SMG)
        return RASTERFALL_CAMERA_SHAKE_SMG_LIFE_MS;
    if (weapon == TOY_GAME_WEAPON_AK)
        return RASTERFALL_CAMERA_SHAKE_AK_LIFE_MS;
    if (weapon == TOY_GAME_WEAPON_SHOTGUN)
        return RASTERFALL_CAMERA_SHAKE_SHOTGUN_LIFE_MS;
    if (weapon == TOY_GAME_WEAPON_AWP)
        return RASTERFALL_CAMERA_SHAKE_AWP_LIFE_MS;
    return RASTERFALL_CAMERA_SHAKE_PISTOL_LIFE_MS;
}

enum camera_shake_axis {
    CAMERA_SHAKE_AXIS_SIDE,
    CAMERA_SHAKE_AXIS_UP,
    CAMERA_SHAKE_AXIS_FORWARD,
    CAMERA_SHAKE_AXIS_YAW,
    CAMERA_SHAKE_AXIS_PITCH
};

static int camera_shake_max(int weapon, int axis)
{
    if (weapon < 0)
        return axis == CAMERA_SHAKE_AXIS_YAW ? damage_shake_angle() : 0;
    if (weapon == TOY_GAME_WEAPON_SMG) {
        if (axis == CAMERA_SHAKE_AXIS_SIDE) return RASTERFALL_CAMERA_SHAKE_SMG_MAX_SIDE;
        if (axis == CAMERA_SHAKE_AXIS_UP) return RASTERFALL_CAMERA_SHAKE_SMG_MAX_UP;
        if (axis == CAMERA_SHAKE_AXIS_FORWARD) return RASTERFALL_CAMERA_SHAKE_SMG_MAX_FORWARD;
        if (axis == CAMERA_SHAKE_AXIS_YAW) return RASTERFALL_CAMERA_SHAKE_SMG_MAX_YAW;
        return RASTERFALL_CAMERA_SHAKE_SMG_MAX_PITCH;
    }
    if (weapon == TOY_GAME_WEAPON_AK) {
        if (axis == CAMERA_SHAKE_AXIS_SIDE) return RASTERFALL_CAMERA_SHAKE_AK_MAX_SIDE;
        if (axis == CAMERA_SHAKE_AXIS_UP) return RASTERFALL_CAMERA_SHAKE_AK_MAX_UP;
        if (axis == CAMERA_SHAKE_AXIS_FORWARD) return RASTERFALL_CAMERA_SHAKE_AK_MAX_FORWARD;
        if (axis == CAMERA_SHAKE_AXIS_YAW) return RASTERFALL_CAMERA_SHAKE_AK_MAX_YAW;
        return RASTERFALL_CAMERA_SHAKE_AK_MAX_PITCH;
    }
    if (weapon == TOY_GAME_WEAPON_SHOTGUN) {
        if (axis == CAMERA_SHAKE_AXIS_SIDE) return RASTERFALL_CAMERA_SHAKE_SHOTGUN_MAX_SIDE;
        if (axis == CAMERA_SHAKE_AXIS_UP) return RASTERFALL_CAMERA_SHAKE_SHOTGUN_MAX_UP;
        if (axis == CAMERA_SHAKE_AXIS_FORWARD) return RASTERFALL_CAMERA_SHAKE_SHOTGUN_MAX_FORWARD;
        if (axis == CAMERA_SHAKE_AXIS_YAW) return RASTERFALL_CAMERA_SHAKE_SHOTGUN_MAX_YAW;
        return RASTERFALL_CAMERA_SHAKE_SHOTGUN_MAX_PITCH;
    }
    if (weapon == TOY_GAME_WEAPON_AWP) {
        if (axis == CAMERA_SHAKE_AXIS_SIDE) return RASTERFALL_CAMERA_SHAKE_AWP_MAX_SIDE;
        if (axis == CAMERA_SHAKE_AXIS_UP) return RASTERFALL_CAMERA_SHAKE_AWP_MAX_UP;
        if (axis == CAMERA_SHAKE_AXIS_FORWARD) return RASTERFALL_CAMERA_SHAKE_AWP_MAX_FORWARD;
        if (axis == CAMERA_SHAKE_AXIS_YAW) return RASTERFALL_CAMERA_SHAKE_AWP_MAX_YAW;
        return RASTERFALL_CAMERA_SHAKE_AWP_MAX_PITCH;
    }
    if (axis == CAMERA_SHAKE_AXIS_SIDE) return RASTERFALL_CAMERA_SHAKE_PISTOL_MAX_SIDE;
    if (axis == CAMERA_SHAKE_AXIS_UP) return RASTERFALL_CAMERA_SHAKE_PISTOL_MAX_UP;
    if (axis == CAMERA_SHAKE_AXIS_FORWARD) return RASTERFALL_CAMERA_SHAKE_PISTOL_MAX_FORWARD;
    if (axis == CAMERA_SHAKE_AXIS_YAW) return RASTERFALL_CAMERA_SHAKE_PISTOL_MAX_YAW;
    return RASTERFALL_CAMERA_SHAKE_PISTOL_MAX_PITCH;
}

static void apply_camera_angle(int *sy, int *cy, int amount)
{
    int old_sy = *sy, old_cy = *cy;
    int length;
    *sy = (old_sy * 1024 + old_cy * amount) / 1024;
    *cy = (old_cy * 1024 - old_sy * amount) / 1024;
    length = isqrt((long long)*sy * *sy + (long long)*cy * *cy);
    if (length > 0) {
        *sy = (int)((long long)*sy * 1024 / length);
        *cy = (int)((long long)*cy * 1024 / length);
    }
}

static void apply_camera_pitch(int *sy, int *cy, int amount)
{
    int old_sy = *sy, old_cy = *cy;
    int length;
    *sy = (old_sy * 1024 + old_cy * amount) / 1024;
    *cy = (old_cy * 1024 - old_sy * amount) / 1024;
    length = isqrt((long long)*sy * *sy + (long long)*cy * *cy);
    if (length > 0) {
        *sy = (int)((long long)*sy * 1024 / length);
        *cy = (int)((long long)*cy * 1024 / length);
    }
    if (*cy < RASTERFALL_PITCH_LIMIT_CY) {
        *sy = *sy < 0 ? -RASTERFALL_PITCH_LIMIT_SY : RASTERFALL_PITCH_LIMIT_SY;
        *cy = RASTERFALL_PITCH_LIMIT_CY;
    }
}

static const int effect_fire_ring[16][2] = {
    { 2500, 0 }, { 2310, 956 }, { 1768, 1768 }, { 956, 2310 },
    { 0, 2500 }, { -956, 2310 }, { -1768, 1768 }, { -2310, 956 },
    { -2500, 0 }, { -2310, -956 }, { -1768, -1768 }, { -956, -2310 },
    { 0, -2500 }, { 956, -2310 }, { 1768, -1768 }, { 2310, -956 }
};

static int effect_default_lifetime(int type)
{
    if (type == RASTERFALL_EFFECT_INSTANCE_RAY)
        return RASTERFALL_TRACER_LIFE_MS;
    if (type == RASTERFALL_EFFECT_INSTANCE_BILLBOARD)
        return RASTERFALL_MUZZLE_FLASH_LIFE_MS;
    return RASTERFALL_PARTICLE_LIFE_MS;
}

struct rasterfall_effect_instance *rasterfall_effects_spawn_instance(
    struct rasterfall_effects *effects,
    const struct rasterfall_effect_instance *seed)
{
    struct rasterfall_effect_instance *instance;
    if (!effects || !seed) return NULL;
    instance = &effects->instances[effects->instance_next];
    effects->instance_next =
        (effects->instance_next + 1) % RASTERFALL_EFFECT_INSTANCE_SLOTS;
    memcpy(instance, seed, sizeof(*instance));
    instance->active = 1;
    instance->age_ms = 0;
    if (instance->lifetime_ms <= 0)
        instance->lifetime_ms = effect_default_lifetime(instance->type);
    if (instance->size <= 0) instance->size = 1000;
    if (instance->stretch_y <= 0) instance->stretch_y = 1000;
    if (instance->alpha <= 0) instance->alpha = 256;
    if (instance->type == RASTERFALL_EFFECT_INSTANCE_CAMERA_SHAKE &&
        instance->shake_seed == 0)
        instance->shake_seed = effects->rng++;
    return instance;
}

struct rasterfall_effect_emitter *rasterfall_effects_spawn_emitter(
    struct rasterfall_effects *effects,
    const struct rasterfall_effect_emitter *seed)
{
    struct rasterfall_effect_emitter *emitter;
    if (!effects || !seed) return NULL;
    emitter = &effects->emitters[effects->emitter_next];
    effects->emitter_next =
        (effects->emitter_next + 1) % RASTERFALL_EFFECT_EMITTER_SLOTS;
    memcpy(emitter, seed, sizeof(*emitter));
    emitter->active = 1;
    emitter->age_ms = 0;
    emitter->spawned_count = 0;
    if (emitter->lifetime_ms <= 0) emitter->lifetime_ms = 160;
    if (emitter->spawn_interval_ms <= 0) emitter->spawn_interval_ms = 16;
    if (emitter->spawn_limit < 0) emitter->spawn_limit = 0;
    if (emitter->burst_count < 0) emitter->burst_count = 0;
    if (emitter->spread < 0) emitter->spread = 0;
    if (emitter->child_count < 0) emitter->child_count = 0;
    if (emitter->child_count > RASTERFALL_EFFECT_EMITTER_CHILD_SLOTS)
        emitter->child_count = RASTERFALL_EFFECT_EMITTER_CHILD_SLOTS;
    {
        int child_index;
        for (child_index = 0; child_index < emitter->child_count; child_index++) {
            if (emitter->children[child_index].spawn_limit < 0)
                emitter->children[child_index].spawn_limit = 0;
            emitter->children[child_index].spawned_count = 0;
        }
    }
    if (emitter->alpha <= 0) emitter->alpha = 256;
    return emitter;
}

static const int explosion_velocity[16][3] = {
    { 52,  0,  0 }, { -52,  0,  0 }, { 0,  0, 52 }, { 0,  0, -52 },
    { 36, 28,  0 }, { -36, 28,  0 }, { 0, 28, 36 }, { 0, 28, -36 },
    { 28, -18, 28 }, { -28, -18, 28 }, { 28, -18, -28 }, { -28, -18, -28 },
    { 18, 42, 18 }, { -18, 42, 18 }, { 18, 42, -18 }, { -18, 42, -18 }
};

static void spawn_enemy_death_presentation(
    struct rasterfall_effects *effects, int enemy_index,
    const struct toy_game_enemy *enemy)
{
    struct rasterfall_effect_emitter emitter;
    struct rasterfall_effect_emitter_child *child;
    int fragment_count, dust_count, impulse, fragment_size;
    int hit_x, hit_z;
    uint32_t color = 0xC43A3A;
    const struct toy_game_enemy_info *info;
    if (!effects || !enemy) return;
    if (enemy->type == TOY_GAME_ENEMY_PURSUIT_FAST) {
        fragment_count = 18; dust_count = 22; impulse = 128;
        fragment_size = 1350;
    } else if (enemy->type == TOY_GAME_ENEMY_PURSUIT_HEAVY) {
        fragment_count = 30; dust_count = 34; impulse = 92;
        fragment_size = 2300;
    } else {
        fragment_count = 24; dust_count = 28; impulse = 108;
        fragment_size = 1750;
    }
    info = toy_game_enemy_info_or_null(enemy->type);
    if (info && info->color) color = info->color;
    memset(&emitter, 0, sizeof(emitter));
    emitter.source_id = enemy_index;
    emitter.x = enemy->x;
    emitter.y = enemy->ground_y + enemy->airborne_y - 520;
    emitter.z = enemy->z;
    /* The final hit direction is presentation state.  Fall back to facing
     * only when a network snapshot did not carry the hit event locally. */
    hit_x = effects->enemy_hit_dir_x[enemy_index];
    hit_z = effects->enemy_hit_dir_z[enemy_index];
    if (hit_x == 0 && hit_z == 0) {
        hit_x = enemy->dir_x;
        hit_z = enemy->dir_z;
    }
    emitter.lifetime_ms = 900;
    emitter.spawn_interval_ms = 28;
    emitter.burst_count = 1;
    emitter.child_count = 2;
    emitter.alpha = 256;
    emitter.size = fragment_size;
    emitter.color = color;
    emitter.vx = hit_x * impulse / 1024;
    emitter.vy = 22;
    emitter.vz = hit_z * impulse / 1024;
    emitter.gravity_y = 1;
    child = &emitter.children[0];
    child->type = RASTERFALL_EFFECT_INSTANCE_PARTICLE;
    child->kind = RASTERFALL_EFFECT_INSTANCE_KIND_ENEMY_DEATH_FRAGMENT;
    child->spawn_limit = fragment_count;
    child->lifetime_ms = RASTERFALL_ENEMY_DEATH_FRAGMENT_LIFE_MS;
    child->size = fragment_size;
    child->alpha = 256;
    child->spread = enemy->type == TOY_GAME_ENEMY_PURSUIT_HEAVY ? 30 : 38;
    child->vx = emitter.vx;
    child->vy = enemy->type == TOY_GAME_ENEMY_PURSUIT_HEAVY ? 34 : 42;
    child->vz = emitter.vz;
    child->gravity_y = 3;
    child->pattern = RASTERFALL_EFFECT_EMITTER_PATTERN_DEFAULT;
    child->color = color;
    child = &emitter.children[1];
    child->type = RASTERFALL_EFFECT_INSTANCE_PARTICLE;
    child->kind = RASTERFALL_EFFECT_INSTANCE_KIND_ENEMY_DEATH_DUST;
    child->spawn_limit = dust_count;
    child->lifetime_ms = RASTERFALL_ENEMY_DEATH_DUST_LIFE_MS;
    child->size = fragment_size * 2 / 3;
    child->alpha = 208;
    child->spread = 52;
    child->vx = hit_x * (impulse * 3 / 4) / 1024;
    child->vy = 28;
    child->vz = hit_z * (impulse * 3 / 4) / 1024;
    child->gravity_y = 2;
    child->pattern = RASTERFALL_EFFECT_EMITTER_PATTERN_DEFAULT;
    child->color = color + 0x181010;
    rasterfall_effects_spawn_emitter(effects, &emitter);
    {
        struct rasterfall_effect_instance marker;
        memset(&marker, 0, sizeof(marker));
        marker.type = RASTERFALL_EFFECT_INSTANCE_EMITTER;
        marker.kind = RASTERFALL_EFFECT_INSTANCE_KIND_ENEMY_DEATH;
        marker.source_id = enemy_index;
        marker.x = emitter.x; marker.y = emitter.y; marker.z = emitter.z;
        marker.lifetime_ms = TOY_GAME_DYING_MS;
        rasterfall_effects_spawn_instance(effects, &marker);
    }
}

static void knockback_trail_push(struct rasterfall_effect_instance *trajectory,
                                 int x, int y, int z)
{
    struct rasterfall_knockback_trail_point *point;
    int next;
    if (!trajectory) return;
    if (trajectory->trail_count > 0) {
        int last = (trajectory->trail_head +
                    RASTERFALL_KNOCKBACK_TRAIL_POINTS - 1) %
                   RASTERFALL_KNOCKBACK_TRAIL_POINTS;
        point = &trajectory->trail[last];
        if (point->age_ms < RASTERFALL_KNOCKBACK_TRAIL_SAMPLE_MS)
            return;
    }
    next = trajectory->trail_head;
    point = &trajectory->trail[next];
    point->x = x; point->y = y; point->z = z; point->age_ms = 0;
    trajectory->trail_head = (next + 1) % RASTERFALL_KNOCKBACK_TRAIL_POINTS;
    if (trajectory->trail_count < RASTERFALL_KNOCKBACK_TRAIL_POINTS)
        trajectory->trail_count++;
}

/* The gameplay impulse remains authoritative.  This presentation-only
 * instance now follows the actor's actual sampled world position instead of
 * reconstructing a future parabola. */
static void spawn_knockback_trajectory(struct rasterfall_effects *effects,
                                       const struct toy_game_actor *actor,
                                       int actor_index, int enemy_type)
{
    struct rasterfall_effect_instance trajectory;
    int i;
    if (!effects || !actor || actor->airborne_ms <= 0 ||
        (!actor->knockback_x && !actor->knockback_z)) return;
    (void)enemy_type;
    for (i = 0; i < RASTERFALL_EFFECT_INSTANCE_SLOTS; i++)
        if (effects->instances[i].active &&
            effects->instances[i].kind ==
                RASTERFALL_EFFECT_INSTANCE_KIND_KNOCKBACK_TRAJECTORY &&
            effects->instances[i].target_id == actor_index)
            effects->instances[i].active = 0;
    memset(&trajectory, 0, sizeof(trajectory));
    trajectory.type = RASTERFALL_EFFECT_INSTANCE_RAY;
    trajectory.kind = RASTERFALL_EFFECT_INSTANCE_KIND_KNOCKBACK_TRAJECTORY;
    trajectory.flags = RASTERFALL_EFFECT_EVENT_DEPTH_TEST |
                       RASTERFALL_EFFECT_TRAJECTORY_IN_FLIGHT;
    trajectory.target_id = actor_index;
    trajectory.x = actor->x;
    trajectory.y = -900 + actor->ground_y + actor->airborne_y;
    trajectory.z = actor->z;
    trajectory.ex = actor->x;
    trajectory.ey = trajectory.y;
    trajectory.ez = actor->z;
    trajectory.lifetime_ms = RASTERFALL_KNOCKBACK_TRAIL_FADE_MS;
    trajectory.ray_width = 7;
    trajectory.color = 0xFFFFFF;
    knockback_trail_push(&trajectory, trajectory.x, trajectory.y, trajectory.z);
    rasterfall_effects_spawn_instance(effects, &trajectory);
}

static void sync_knockback_trajectories(struct rasterfall_effects *effects,
                                        const struct toy_game *game)
{
    int i;
    if (!effects || !game) return;
    for (i = 0; i < RASTERFALL_EFFECT_INSTANCE_SLOTS; i++) {
        struct rasterfall_effect_instance *trajectory = &effects->instances[i];
        const struct toy_game_actor *actor;
        if (!trajectory->active ||
            trajectory->kind != RASTERFALL_EFFECT_INSTANCE_KIND_KNOCKBACK_TRAJECTORY)
            continue;
        if (trajectory->target_id < 0 ||
            trajectory->target_id >= TOY_GAME_MAX_ACTORS) {
            trajectory->active = 0;
            continue;
        }
        actor = &game->actors[trajectory->target_id];
        if (!actor->active) {
            trajectory->active = 0;
            continue;
        }
        trajectory->ex = actor->x;
        trajectory->ey = -900 + actor->ground_y + actor->airborne_y;
        trajectory->ez = actor->z;
        if (trajectory->flags & RASTERFALL_EFFECT_TRAJECTORY_IN_FLIGHT) {
            if (actor->airborne_ms > 0) {
                knockback_trail_push(trajectory, trajectory->ex,
                                     trajectory->ey, trajectory->ez);
            } else {
                trajectory->flags &= ~RASTERFALL_EFFECT_TRAJECTORY_IN_FLIGHT;
                trajectory->age_ms = 0;
            }
        }
    }
}

enum rasterfall_effect_emitter_preset_id {
    RASTERFALL_EFFECT_EMITTER_PRESET_FIRE,
    RASTERFALL_EFFECT_EMITTER_PRESET_EXPLOSION,
    RASTERFALL_EFFECT_EMITTER_PRESET_COUNT
};

struct rasterfall_effect_emitter_preset {
    int lifetime_ms;
    int spawn_interval_ms;
    int burst_count;
    int spawn_limit;
    int alpha;
    int child_count;
    struct rasterfall_effect_emitter_child children[
        RASTERFALL_EFFECT_EMITTER_CHILD_SLOTS];
};

static const struct rasterfall_effect_emitter_preset emitter_preset_table[
    RASTERFALL_EFFECT_EMITTER_PRESET_COUNT] = {
    [RASTERFALL_EFFECT_EMITTER_PRESET_FIRE] = {
        160, 16, 88, 0, 256, 1,
        {
            { RASTERFALL_EFFECT_INSTANCE_PARTICLE,
              RASTERFALL_EFFECT_INSTANCE_KIND_FIRE,
              0, 0, 0, 0, 0, 256, 0, 0, 0, 0, 0,
              RASTERFALL_EFFECT_EMITTER_PATTERN_FIRE, 0, 0, 0, 0 },
            { 0 }, { 0 }
        }
    },
    [RASTERFALL_EFFECT_EMITTER_PRESET_EXPLOSION] = {
        180, 1, 0, 0, 256, 3,
        {
            { RASTERFALL_EFFECT_INSTANCE_PARTICLE,
              RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION_PARTICLE,
              0, 16, 0, 0, 4500, 256, 1000, 0, 0, 0, 2,
              RASTERFALL_EFFECT_EMITTER_PATTERN_EXPLOSION, 0, 0, 0, 0xFFD050 },
            { RASTERFALL_EFFECT_INSTANCE_RAY,
              RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION_RAY,
              0, 1, 0, 80, 0, 256, 0, 0, 0, 0, 0,
              RASTERFALL_EFFECT_EMITTER_PATTERN_DEFAULT, 0, 0, 0, 0 },
            { RASTERFALL_EFFECT_INSTANCE_BILLBOARD,
              RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION_FLASH,
              0, 1, 0, RASTERFALL_MUZZLE_FLASH_LIFE_MS, 1000, 256, 0,
              0, 0, 0, 0, RASTERFALL_EFFECT_EMITTER_PATTERN_DEFAULT,
              0, 0, 0, 0xFFF4A0 }
        }
    }
};

static void spawn_emitter_child(struct rasterfall_effects *effects,
                                struct rasterfall_effect_emitter *emitter)
{
    struct rasterfall_effect_instance instance;
    int index = emitter->spawned_count % 16;
    int spread = emitter->spread;
    memset(&instance, 0, sizeof(instance));
    instance.type = emitter->child_type;
    instance.kind = emitter->child_kind;
    instance.x = emitter->x; instance.y = emitter->y; instance.z = emitter->z;
    if (emitter->pattern == RASTERFALL_EFFECT_EMITTER_PATTERN_FIRE) {
        int fire_index = emitter->spawned_count % 88;
        int pulse;
        if (fire_index < 48) {
            int ring_index = fire_index / 3;
            int density = fire_index % 3;
            pulse = (emitter->phase_ms / 90 + ring_index * 37 + density * 11) % 5;
            instance.x += effect_fire_ring[ring_index][0] + (density - 1) * 90;
            instance.y = -890;
            instance.z += effect_fire_ring[ring_index][1] + (density - 1) * 70;
            instance.size = (3 + pulse / 2) * 500;
            instance.color = 0xD84A08;
        } else if (fire_index < 64) {
            int ring_index = fire_index - 48;
            pulse = (emitter->phase_ms / 120 + ring_index * 37) % 5;
            instance.x += effect_fire_ring[ring_index][0];
            instance.y = -890 + 180 + pulse * 55;
            instance.z += effect_fire_ring[ring_index][1];
            instance.size = (3 + pulse / 2) * 500;
            instance.color = 0xFFB51A;
        } else {
            int outer_index = fire_index - 64;
            pulse = (emitter->phase_ms / 75 + outer_index * 19) % 6;
            instance.x += (outer_index * 733 % 1500) - 750;
            instance.y = -890 + 100 + pulse * 45;
            instance.z += (outer_index * 947 % 1500) - 750;
            instance.size = (4 + pulse / 2) * 500;
            instance.color = outer_index & 1 ? 0xFFB51A : 0xFF6A08;
        }
        instance.lifetime_ms = 16;
        instance.stretch_y = 2000;
    } else if (emitter->pattern == RASTERFALL_EFFECT_EMITTER_PATTERN_EXPLOSION) {
        instance.vx = emitter->vx + explosion_velocity[index][0] * spread / 1000;
        instance.vy = emitter->vy + explosion_velocity[index][1] * spread / 1000;
        instance.vz = emitter->vz + explosion_velocity[index][2] * spread / 1000;
    } else {
        instance.vx = emitter->vx + effect_rand(effects, -spread, spread);
        instance.vy = emitter->vy + effect_rand(effects, -spread, spread);
        instance.vz = emitter->vz + effect_rand(effects, -spread, spread);
    }
    instance.gravity_y = emitter->gravity_y;
    if (instance.lifetime_ms <= 0) instance.lifetime_ms = emitter->lifetime_ms;
    if (instance.size <= 0) instance.size = emitter->size;
    instance.alpha = emitter->alpha;
    if (!instance.color) instance.color = emitter->color;
    rasterfall_effects_spawn_instance(effects, &instance);
    emitter->spawned_count++;
}

static void spawn_emitter_descriptor(
    struct rasterfall_effects *effects,
    struct rasterfall_effect_emitter *emitter,
    struct rasterfall_effect_emitter_child *child)
{
    struct rasterfall_effect_instance instance;
    int index = child->spawned_count % 16;
    int spread = child->spread < 0 ? 0 : child->spread;
    memset(&instance, 0, sizeof(instance));
    instance.type = child->type;
    instance.kind = child->kind;
    instance.flags = child->flags;
    instance.x = emitter->x; instance.y = emitter->y; instance.z = emitter->z;
    if (child->pattern == RASTERFALL_EFFECT_EMITTER_PATTERN_FIRE) {
        int fire_index = child->spawned_count % 88;
        int pulse;
        if (fire_index < 48) {
            int ring_index = fire_index / 3;
            int density = fire_index % 3;
            pulse = (emitter->phase_ms / 90 + ring_index * 37 + density * 11) % 5;
            instance.x += effect_fire_ring[ring_index][0] + (density - 1) * 90;
            instance.y = -890;
            instance.z += effect_fire_ring[ring_index][1] + (density - 1) * 70;
            instance.size = (3 + pulse / 2) * 500;
            instance.color = 0xD84A08;
        } else if (fire_index < 64) {
            int ring_index = fire_index - 48;
            pulse = (emitter->phase_ms / 120 + ring_index * 37) % 5;
            instance.y = -890 + 180 + pulse * 55;
            instance.x += effect_fire_ring[ring_index][0];
            instance.z += effect_fire_ring[ring_index][1];
            instance.size = (3 + pulse / 2) * 500;
            instance.color = 0xFFB51A;
        } else {
            int outer_index = fire_index - 64;
            pulse = (emitter->phase_ms / 75 + outer_index * 19) % 6;
            instance.x += (outer_index * 733 % 1500) - 750;
            instance.y = -890 + 100 + pulse * 45;
            instance.z += (outer_index * 947 % 1500) - 750;
            instance.size = (4 + pulse / 2) * 500;
            instance.color = outer_index & 1 ? 0xFFB51A : 0xFF6A08;
        }
        instance.lifetime_ms = 16;
        instance.stretch_y = 2000;
    } else if (child->pattern == RASTERFALL_EFFECT_EMITTER_PATTERN_EXPLOSION) {
        instance.vx = child->vx + explosion_velocity[index][0] * spread / 1000;
        instance.vy = child->vy + explosion_velocity[index][1] * spread / 1000;
        instance.vz = child->vz + explosion_velocity[index][2] * spread / 1000;
    } else {
        instance.vx = child->vx + effect_rand(effects, -spread, spread);
        instance.vy = child->vy + effect_rand(effects, -spread, spread);
        instance.vz = child->vz + effect_rand(effects, -spread, spread);
    }
    instance.ex = child->ex; instance.ey = child->ey; instance.ez = child->ez;
    instance.source_id = emitter->source_id;
    instance.gravity_y = child->gravity_y;
    instance.lifetime_ms = child->lifetime_ms > 0 ? child->lifetime_ms :
                           emitter->lifetime_ms;
    instance.size = child->size > 0 ? child->size : emitter->size;
    instance.alpha = child->alpha > 0 ? child->alpha : emitter->alpha;
    instance.color = child->color ? child->color : emitter->color;
    rasterfall_effects_spawn_instance(effects, &instance);
    child->spawned_count++;
}

void rasterfall_effects_sync_fire_zones(struct rasterfall_effects *effects,
                                        const struct toy_game *game)
{
    int i, j;
    int seen[TOY_CONFIG_MAX_BURN_ZONES];
    if (!effects || !game) return;
    memset(seen, 0, sizeof(seen));
    for (i = 0; i < TOY_CONFIG_MAX_BURN_ZONES; i++) {
        const struct toy_game_burn_zone *zone = &game->burn_zones[i];
        struct rasterfall_effect_emitter *emitter = NULL;
        if (!zone->active) continue;
        for (j = 0; j < RASTERFALL_EFFECT_EMITTER_SLOTS; j++) {
            if (effects->emitters[j].child_count > 0 &&
                effects->emitters[j].children[0].kind ==
                    RASTERFALL_EFFECT_INSTANCE_KIND_FIRE &&
                effects->emitters[j].source_id == i) {
                emitter = &effects->emitters[j];
                break;
            }
        }
        if (!emitter) {
            struct rasterfall_effect_emitter seed;
            const struct rasterfall_effect_emitter_preset *preset =
                &emitter_preset_table[RASTERFALL_EFFECT_EMITTER_PRESET_FIRE];
            memset(&seed, 0, sizeof(seed));
            seed.source_id = i;
            seed.x = zone->x; seed.y = -890; seed.z = zone->z;
            seed.lifetime_ms = zone->remaining_ms > 0 ? zone->remaining_ms :
                               preset->lifetime_ms;
            seed.spawn_interval_ms = preset->spawn_interval_ms;
            seed.burst_count = preset->burst_count;
            seed.spawn_limit = preset->spawn_limit;
            seed.alpha = preset->alpha;
            seed.child_count = preset->child_count;
            memcpy(seed.children, preset->children, sizeof(seed.children));
            emitter = rasterfall_effects_spawn_emitter(effects, &seed);
            if (emitter) emitter->spawn_accum_ms = emitter->spawn_interval_ms;
        }
        if (!emitter) continue;
        emitter->active = 1;
        emitter->x = zone->x; emitter->z = zone->z;
        emitter->phase_ms = zone->elapsed_ms;
        emitter->lifetime_ms = zone->remaining_ms;
        seen[i] = 1;
    }
    for (i = 0; i < RASTERFALL_EFFECT_EMITTER_SLOTS; i++) {
        struct rasterfall_effect_emitter *emitter = &effects->emitters[i];
        if (emitter->child_count > 0 &&
            emitter->children[0].kind == RASTERFALL_EFFECT_INSTANCE_KIND_FIRE &&
            (emitter->source_id < 0 || emitter->source_id >= TOY_CONFIG_MAX_BURN_ZONES ||
             !seen[emitter->source_id]))
            emitter->active = 0;
    }
}

void rasterfall_effects_sync_projectile_flashes(
    struct rasterfall_effects *effects, const struct toy_game *game)
{
    int i;
    if (!effects || !game) return;
    for (i = 0; i < RASTERFALL_EFFECT_INSTANCE_SLOTS; i++)
        if (effects->instances[i].kind ==
            RASTERFALL_EFFECT_INSTANCE_KIND_PROJECTILE_FLASH)
            effects->instances[i].active = 0;
    for (i = 0; i < TOY_GAME_MAX_PROJECTILES; i++) {
        const struct toy_game_projectile *projectile = &game->projectiles[i];
        struct rasterfall_effect_instance instance;
        if (!projectile->active || projectile->kind != TOY_GAME_WEAPON_BOMB ||
            projectile->flash_ms <= 0)
            continue;
        memset(&instance, 0, sizeof(instance));
        instance.type = RASTERFALL_EFFECT_INSTANCE_BILLBOARD;
        instance.kind = RASTERFALL_EFFECT_INSTANCE_KIND_PROJECTILE_FLASH;
        instance.x = projectile->x;
        instance.y = -900 + projectile->y + 120;
        instance.z = projectile->z;
        instance.lifetime_ms = 16;
        instance.size = 7000;
        instance.alpha = projectile->flash_ms * 256 / 100;
        instance.color = 0xFF2020;
        rasterfall_effects_spawn_instance(effects, &instance);
    }
}

void rasterfall_effects_sync_damage_flash(struct rasterfall_effects *effects,
                                          const struct toy_game *game,
                                          const struct camera *camera)
{
    const struct toy_game_actor *player =
        toy_game_local_player_actor_const(game);
    struct rasterfall_effect_instance instance;
    int i, nearest = -1, took_damage = 0, flash_active = 0;
    long long nearest_dist = 0;
    if (!effects || !player) return;
    if (effects->last_player_hp < 0) {
        effects->last_player_hp = player->hp;
    } else if (player->hp < effects->last_player_hp) {
        took_damage = 1;
    }
    if (took_damage && effects->damage_shake_cooldown_ms <= 0) {
        struct rasterfall_effect_instance shake;
        int shake_i;
        for (shake_i = 0; shake_i < RASTERFALL_EFFECT_INSTANCE_SLOTS;
             shake_i++)
            if (effects->instances[shake_i].active &&
                effects->instances[shake_i].type ==
                    RASTERFALL_EFFECT_INSTANCE_CAMERA_SHAKE &&
                effects->instances[shake_i].weapon < 0)
                effects->instances[shake_i].active = 0;
        memset(&shake, 0, sizeof(shake));
        shake.type = RASTERFALL_EFFECT_INSTANCE_CAMERA_SHAKE;
        shake.kind = RASTERFALL_EFFECT_INSTANCE_KIND_CAMERA_SHAKE;
        shake.weapon = -1; /* damage preset, not weapon recoil */
        shake.lifetime_ms = RASTERFALL_DAMAGE_CAMERA_SHAKE_LIFE_MS;
        shake.shake_yaw = effect_rand(effects, 0, 1) ?
                          damage_shake_angle() : -damage_shake_angle();
        rasterfall_effects_spawn_instance(effects, &shake);
        /* Restarting damage shake returns the aggregate to neutral first. */
        effects->camera_shake_yaw = 0;
        effects->camera_shake_pitch = 0;
        effects->damage_shake_cooldown_ms =
            RASTERFALL_DAMAGE_CAMERA_SHAKE_MIN_INTERVAL_MS;
    }
    effects->last_player_hp = player->hp;
    for (i = 0; i < RASTERFALL_EFFECT_INSTANCE_SLOTS; i++)
        if (effects->instances[i].active && effects->instances[i].kind ==
            RASTERFALL_EFFECT_INSTANCE_KIND_DAMAGE_FLASH) {
            flash_active = 1;
            if (took_damage) effects->instances[i].active = 0;
        }
    /* The HP edge also covers clients, where damage_flash_ms is not part of
     * the authoritative snapshot.  A live instance owns its own fade. */
    if (!took_damage && (player->damage_flash_ms <= 0 || flash_active)) return;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        const struct toy_game_enemy *enemy = &game->enemies[i];
        long long dx, dz, dist;
        if (!enemy->active || enemy->hp <= 0) continue;
        dx = (long long)enemy->x - player->x;
        dz = (long long)enemy->z - player->z;
        dist = dx * dx + dz * dz;
        if (nearest < 0 || dist < nearest_dist) {
            nearest = i;
            nearest_dist = dist;
        }
    }
    memset(&instance, 0, sizeof(instance));
    instance.type = RASTERFALL_EFFECT_INSTANCE_OVERLAY;
    instance.kind = RASTERFALL_EFFECT_INSTANCE_KIND_DAMAGE_FLASH;
    instance.width = 0;
    instance.height = 0;
    instance.lifetime_ms = RASTERFALL_DAMAGE_FLASH_LIFE_MS;
    instance.alpha = 24;
    instance.color = 0xF03030;
    if (nearest >= 0 && camera) {
        long long dx = (long long)game->enemies[nearest].x - player->x;
        long long dz = (long long)game->enemies[nearest].z - player->z;
        long long right = dx * camera->cy - dz * camera->sy;
        long long forward = dx * camera->sy + dz * camera->cy;
        long long ar = right < 0 ? -right : right;
        long long af = forward < 0 ? -forward : forward;
        if (ar * 5 < af * 2) instance.dir_x = 0;
        else instance.dir_x = right < 0 ? -1 : 1;
        if (af * 5 < ar * 2) instance.dir_y = 0;
        else instance.dir_y = forward < 0 ? 1 : -1;
    }
    rasterfall_effects_spawn_instance(effects, &instance);
}

void rasterfall_effects_sync_enemy_feedback(struct rasterfall_effects *effects,
                                            const struct toy_game *game)
{
    int i;
    if (!effects || !game) return;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        const struct toy_game_enemy *enemy = &game->enemies[i];
        if (enemy->active == 1 &&
            (enemy->type == TOY_GAME_ENEMY_CHARGER || enemy->type == TOY_GAME_ENEMY_TANK)) {
            uint64_t mask=enemy->ability.charge_hit_actor_mask;
            /* Charger/Tank clear this mask at the start of every attack.
             * Treat a bit removal as a new attack generation so repeated
             * hits from the same enemy can retrigger particles and the arc. */
            if ((mask & effects->enemy_special_hit_seen[i]) != mask)
                effects->enemy_special_hit_seen[i] = 0;
            uint64_t hits=mask & ~effects->enemy_special_hit_seen[i];
            /* Only authoritative successful hits produce target particles.
             * Camera shake remains the local player's existing HP-edge hook. */
            for (int target=0;target<TOY_GAME_MAX_ACTORS;target++)
                if ((hits & (1ULL<<target)) && game->actors[target].active) {
                    const struct toy_game_actor *a=&game->actors[target];
                    rasterfall_effects_spawn_hit_particles(effects,a->x,
                        a->ground_y+a->airborne_y-350,a->z,enemy->dir_x,enemy->dir_z);
                    spawn_knockback_trajectory(effects, a, target, enemy->type);
                }
            effects->enemy_special_hit_seen[i]=mask;
        } else effects->enemy_special_hit_seen[i]=0;
        if (enemy->active == 0) {
            effects->enemy_death_seen[i] = 0;
            effects->enemy_death_style[i] = RASTERFALL_ENEMY_DEATH_STYLE_NONE;
            effects->enemy_hit_dir_x[i] = 0;
            effects->enemy_hit_dir_z[i] = 0;
            effects->enemy_hit_strength[i] = 0;
        } else if (enemy->active == 2 && !effects->enemy_death_seen[i]) {
            /* Presentation-only choice: keep the old squash death as a rare
             * variation, while the normal path rasterizes the intact body. */
            effects->enemy_death_style[i] = effect_rand(effects, 0, 9) == 0 ?
                RASTERFALL_ENEMY_DEATH_STYLE_LEGACY :
                RASTERFALL_ENEMY_DEATH_STYLE_DISSOLVE;
            if (effects->enemy_death_style[i] ==
                    RASTERFALL_ENEMY_DEATH_STYLE_DISSOLVE)
                spawn_enemy_death_presentation(effects, i, enemy);
            effects->enemy_death_seen[i] = 1;
        }
    }
    for (i = 0; i < RASTERFALL_EFFECT_INSTANCE_SLOTS; i++)
        if (effects->instances[i].kind ==
            RASTERFALL_EFFECT_INSTANCE_KIND_ENEMY_HURT_TINT)
            effects->instances[i].active = 0;
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        const struct toy_game_enemy *enemy = &game->enemies[i];
        struct rasterfall_effect_instance instance;
        if (enemy->active == 0 || (enemy->hurt <= 0 && enemy->flash <= 0))
            continue;
        memset(&instance, 0, sizeof(instance));
        instance.type = RASTERFALL_EFFECT_INSTANCE_MATERIAL;
        instance.kind = RASTERFALL_EFFECT_INSTANCE_KIND_ENEMY_HURT_TINT;
        instance.target_id = i;
        instance.lifetime_ms = 16;
        instance.alpha = 256;
        instance.color = enemy->hurt > 0 ? 0xBB3333 : 0xDFDFDF;
        instance.dir_x = effects->enemy_hit_dir_x[i];
        instance.dir_z = effects->enemy_hit_dir_z[i];
        instance.size = effects->enemy_hit_strength[i];
        rasterfall_effects_spawn_instance(effects, &instance);
    }
    sync_knockback_trajectories(effects, game);
}

void rasterfall_effects_sync_interaction_highlight(
    struct rasterfall_effects *effects, int target_id,
    int x, int y, int z, int active)
{
    struct rasterfall_effect_instance instance;
    int i;
    if (!effects) return;
    for (i = 0; i < RASTERFALL_EFFECT_INSTANCE_SLOTS; i++)
        if (effects->instances[i].kind ==
            RASTERFALL_EFFECT_INSTANCE_KIND_INTERACTION_HIGHLIGHT)
            effects->instances[i].active = 0;
    if (!active || target_id < 0) return;
    memset(&instance, 0, sizeof(instance));
    instance.type = RASTERFALL_EFFECT_INSTANCE_BILLBOARD;
    instance.kind = RASTERFALL_EFFECT_INSTANCE_KIND_INTERACTION_HIGHLIGHT;
    instance.target_id = target_id;
    instance.x = x; instance.y = y + 260; instance.z = z;
    instance.lifetime_ms = 16;
    instance.size = 1000;
    instance.alpha = 256;
    instance.color = 0xFFE070;
    rasterfall_effects_spawn_instance(effects, &instance);
}

static struct rasterfall_effect_instance *spawn_event_instance(
    struct rasterfall_effects *effects,
    const struct rasterfall_effect_event *event,
    int type, int kind, int x, int y, int z, int vx, int vy, int vz)
{
    struct rasterfall_effect_instance instance;
    memset(&instance, 0, sizeof(instance));
    instance.type = type;
    instance.kind = kind;
    instance.flags = event->flags;
    instance.source_id = event->source_id;
    instance.target_id = event->target_id;
    instance.weapon = event->weapon;
    instance.sequence = event->sequence;
    instance.x = x; instance.y = y; instance.z = z;
    if (type == RASTERFALL_EFFECT_INSTANCE_RAY) {
        instance.ex = event->ex;
        instance.ey = event->ey;
        instance.ez = event->ez;
    }
    instance.dir_x = event->dir_sy;
    instance.dir_z = event->dir_cy;
    instance.vx = vx; instance.vy = vy; instance.vz = vz;
    instance.lifetime_ms = event->life_ms;
    if (kind == RASTERFALL_EFFECT_INSTANCE_KIND_TRACER) {
        const struct toy_game_weapon_info *weapon =
            toy_game_weapon_info_or_null(event->weapon);
        if (weapon) {
            instance.color = weapon->tracer_start_color;
            instance.ray_end_color = weapon->tracer_end_color;
            instance.ray_width = weapon->tracer_width;
            instance.ray_tail_percent = weapon->tracer_tail_percent;
            if (instance.lifetime_ms <= 0)
                instance.lifetime_ms = weapon->tracer_lifetime_ms;
        }
        if (instance.lifetime_ms <= 0)
            instance.lifetime_ms = RASTERFALL_TRACER_LIFE_MS;
        if (instance.ray_width <= 0) instance.ray_width = 1;
        if (instance.ray_tail_percent <= 0) instance.ray_tail_percent = 14;
    }
    return rasterfall_effects_spawn_instance(effects, &instance);
}

static void init_explosion_emitter(struct rasterfall_effect_emitter *emitter,
                                   const struct rasterfall_effect_event *event)
{
    const struct rasterfall_effect_emitter_preset *preset =
        &emitter_preset_table[RASTERFALL_EFFECT_EMITTER_PRESET_EXPLOSION];
    memset(emitter, 0, sizeof(*emitter));
    emitter->x = event->x; emitter->y = event->y; emitter->z = event->z;
    emitter->lifetime_ms = preset->lifetime_ms;
    emitter->spawn_interval_ms = preset->spawn_interval_ms;
    emitter->child_count = preset->child_count;
    emitter->burst_count = preset->burst_count;
    emitter->spawn_limit = preset->spawn_limit;
    emitter->alpha = preset->alpha;
    memcpy(emitter->children, preset->children, sizeof(emitter->children));
    emitter->children[1].ex = event->x + 1400;
    emitter->children[1].ey = event->y;
    emitter->children[1].ez = event->z;
}

void rasterfall_effects_init(struct rasterfall_effects *effects)
{
    memset(effects, 0, sizeof(struct rasterfall_effects));
    effects->rng = 0x243F6A88;
}

void rasterfall_effects_reset_fire(struct rasterfall_effects *effects)
{
    memset(effects->instances, 0, sizeof(effects->instances));
    effects->instance_next = 0;
    memset(effects->emitters, 0, sizeof(effects->emitters));
    effects->emitter_next = 0;
    effects->last_fire_seq = 0;
    memset(effects->last_network_fire_seq, 0,
           sizeof(effects->last_network_fire_seq));
    effects->last_ai_fire_seq = 0;
    effects->weapon_kick = 0;
    effects->camera_shake_side = 0;
    effects->camera_shake_up = 0;
    effects->camera_shake_forward = 0;
    effects->camera_shake_yaw = 0;
    effects->camera_shake_pitch = 0;
    effects->last_player_hp = -1;
    effects->damage_shake_cooldown_ms = 0;
    memset(effects->enemy_death_seen, 0, sizeof(effects->enemy_death_seen));
    memset(effects->enemy_special_hit_seen, 0, sizeof(effects->enemy_special_hit_seen));
    memset(effects->enemy_death_style, 0, sizeof(effects->enemy_death_style));
    memset(effects->enemy_hit_dir_x, 0, sizeof(effects->enemy_hit_dir_x));
    memset(effects->enemy_hit_dir_z, 0, sizeof(effects->enemy_hit_dir_z));
    memset(effects->enemy_hit_strength, 0, sizeof(effects->enemy_hit_strength));
}

void rasterfall_effects_spawn_hit_particles(struct rasterfall_effects *effects,
                                            int x, int y, int z, int sy, int cy)
{
    int i;
    for (i = 0; i < 7; i++) {
        struct rasterfall_effect_instance instance;
        memset(&instance, 0, sizeof(instance));
        instance.type = RASTERFALL_EFFECT_INSTANCE_PARTICLE;
        instance.kind = RASTERFALL_EFFECT_INSTANCE_KIND_HIT_PARTICLE;
        instance.x = x;
        instance.y = y + effect_rand(effects, -10, 10);
        instance.z = z;
        instance.vx = sy * 22 / 1024 + effect_rand(effects, -24, 24);
        instance.vy = effect_rand(effects, 8, 30);
        instance.vz = cy * 22 / 1024 + effect_rand(effects, -24, 24);
        instance.gravity_y = RASTERFALL_PARTICLE_GRAVITY;
        instance.lifetime_ms = RASTERFALL_PARTICLE_LIFE_MS +
                               effect_rand(effects, -40, 40);
        rasterfall_effects_spawn_instance(effects, &instance);
    }
}

void rasterfall_effects_consume(struct rasterfall_effects *effects,
                                const struct rasterfall_effect_event *event)
{
    if (!effects || !event) return;
    if (event->type == RASTERFALL_EFFECT_EVENT_WEAPON_FIRE) {
        struct rasterfall_effect_instance *instance;
        int weapon_scale = event->weapon == TOY_GAME_WEAPON_SHOTGUN ? 1 : 0;
        instance = spawn_event_instance(
            effects, event, RASTERFALL_EFFECT_INSTANCE_BILLBOARD,
            RASTERFALL_EFFECT_INSTANCE_KIND_MUZZLE_FLASH_CORE,
            event->sx, event->sy, event->sz, 0, 0, 0);
        if (instance) {
            instance->lifetime_ms = 28;
            instance->size = (weapon_scale ? 7 : 5) +
                             effect_rand(effects, -1, 1);
            instance->alpha = 256;
        }
        instance = spawn_event_instance(
            effects, event, RASTERFALL_EFFECT_INSTANCE_BILLBOARD,
            RASTERFALL_EFFECT_INSTANCE_KIND_MUZZLE_FLASH_OUTER,
            event->sx, event->sy, event->sz, 0, 0, 0);
        if (instance) {
            instance->lifetime_ms = event->life_ms > 0 ? event->life_ms : 70;
            instance->size = (weapon_scale ? 16 : 12) +
                             effect_rand(effects, -1, 1);
            instance->alpha = 205 + effect_rand(effects, -16, 16);
        }
        instance = spawn_event_instance(
            effects, event, RASTERFALL_EFFECT_INSTANCE_BILLBOARD,
            RASTERFALL_EFFECT_INSTANCE_KIND_MUZZLE_FLASH_LOBE,
            event->sx, event->sy, event->sz, 0, 0, 0);
        if (instance) {
            instance->lifetime_ms = 48;
            instance->size = (weapon_scale ? 9 : 7) +
                             effect_rand(effects, -1, 1);
            instance->alpha = 220 + effect_rand(effects, -12, 12);
        }
        if (event->flags & RASTERFALL_EFFECT_EVENT_LOCAL_VIEW) {
            struct rasterfall_effect_instance shake;
            memset(&shake, 0, sizeof(shake));
            shake.type = RASTERFALL_EFFECT_INSTANCE_CAMERA_SHAKE;
            shake.kind = RASTERFALL_EFFECT_INSTANCE_KIND_CAMERA_SHAKE;
            shake.weapon = event->weapon;
            shake.lifetime_ms = camera_shake_lifetime(event->weapon);
            shake.shake_side = event->weapon == TOY_GAME_WEAPON_SHOTGUN ?
                               RASTERFALL_CAMERA_SHAKE_SHOTGUN_SIDE :
                               event->weapon == TOY_GAME_WEAPON_AWP ?
                               RASTERFALL_CAMERA_SHAKE_AWP_SIDE :
                               event->weapon == TOY_GAME_WEAPON_AK ?
                               RASTERFALL_CAMERA_SHAKE_AK_SIDE :
                               RASTERFALL_CAMERA_SHAKE_DEFAULT_SIDE;
            shake.shake_up = event->weapon == TOY_GAME_WEAPON_SHOTGUN ?
                             RASTERFALL_CAMERA_SHAKE_SHOTGUN_UP :
                             event->weapon == TOY_GAME_WEAPON_AWP ?
                             RASTERFALL_CAMERA_SHAKE_AWP_UP :
                             event->weapon == TOY_GAME_WEAPON_AK ?
                             RASTERFALL_CAMERA_SHAKE_AK_UP :
                             RASTERFALL_CAMERA_SHAKE_DEFAULT_UP;
            shake.shake_forward = event->weapon == TOY_GAME_WEAPON_AWP ?
                                  RASTERFALL_CAMERA_SHAKE_AWP_FORWARD :
                                  event->weapon == TOY_GAME_WEAPON_SHOTGUN ?
                                  RASTERFALL_CAMERA_SHAKE_SHOTGUN_FORWARD :
                                  event->weapon == TOY_GAME_WEAPON_AK ?
                                  RASTERFALL_CAMERA_SHAKE_AK_FORWARD :
                                  RASTERFALL_CAMERA_SHAKE_DEFAULT_FORWARD;
            shake.shake_yaw = event->weapon == TOY_GAME_WEAPON_SHOTGUN ?
                              RASTERFALL_CAMERA_SHAKE_SHOTGUN_YAW :
                              event->weapon == TOY_GAME_WEAPON_AWP ?
                              RASTERFALL_CAMERA_SHAKE_AWP_YAW :
                              event->weapon == TOY_GAME_WEAPON_AK ?
                              RASTERFALL_CAMERA_SHAKE_AK_YAW :
                              RASTERFALL_CAMERA_SHAKE_DEFAULT_YAW;
            shake.shake_pitch = event->weapon == TOY_GAME_WEAPON_SHOTGUN ?
                                RASTERFALL_CAMERA_SHAKE_SHOTGUN_PITCH :
                                event->weapon == TOY_GAME_WEAPON_AWP ?
                                RASTERFALL_CAMERA_SHAKE_AWP_PITCH :
                                event->weapon == TOY_GAME_WEAPON_AK ?
                                RASTERFALL_CAMERA_SHAKE_AK_PITCH :
                                RASTERFALL_CAMERA_SHAKE_DEFAULT_PITCH;
            rasterfall_effects_spawn_instance(effects, &shake);
        }
    } else if (event->type == RASTERFALL_EFFECT_EVENT_TRACER) {
        {
            spawn_event_instance(effects, event,
                                 RASTERFALL_EFFECT_INSTANCE_RAY,
                                 RASTERFALL_EFFECT_INSTANCE_KIND_TRACER,
                                 event->sx, event->sy, event->sz,
                                 0, 0, 0);
        }
    } else if (event->type == RASTERFALL_EFFECT_EVENT_BULLET_IMPACT ||
               event->type == RASTERFALL_EFFECT_EVENT_ENTITY_HIT) {
        rasterfall_effects_spawn_hit_particles(effects, event->x, event->y,
                                                event->z, event->dir_sy,
                                                event->dir_cy);
        if (event->type == RASTERFALL_EFFECT_EVENT_ENTITY_HIT &&
            event->target_id >= 0 && event->target_id < TOY_GAME_MAX_ENEMIES) {
            effects->enemy_hit_dir_x[event->target_id] = event->dir_sy;
            effects->enemy_hit_dir_z[event->target_id] = event->dir_cy;
            effects->enemy_hit_strength[event->target_id] = event->damage;
        }
    } else if (event->type == RASTERFALL_EFFECT_EVENT_EXPLOSION) {
        struct rasterfall_effect_emitter emitter;
        init_explosion_emitter(&emitter, event);
        rasterfall_effects_spawn_emitter(effects, &emitter);
        spawn_event_instance(effects, event,
                             RASTERFALL_EFFECT_INSTANCE_EMITTER,
                             RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION,
                             event->x, event->y, event->z, 0, 0, 0);
    } else if (event->type == RASTERFALL_EFFECT_EVENT_CAMERA_SHAKE) {
        struct rasterfall_effect_instance shake;
        memset(&shake, 0, sizeof(shake));
        shake.type = RASTERFALL_EFFECT_INSTANCE_CAMERA_SHAKE;
        shake.kind = RASTERFALL_EFFECT_INSTANCE_KIND_CAMERA_SHAKE;
        shake.weapon = event->weapon;
        shake.lifetime_ms = event->life_ms > 0 ? event->life_ms : 120;
        shake.shake_side = event->shake_side;
        shake.shake_up = event->shake_up;
        shake.shake_forward = event->shake_forward;
        shake.shake_yaw = event->shake_yaw;
        shake.shake_pitch = event->shake_pitch;
        rasterfall_effects_spawn_instance(effects, &shake);
    }
}

void rasterfall_effects_apply_camera_shake(
    const struct rasterfall_effects *effects, struct camera *render_camera)
{
    int side, up, forward, yaw, pitch;
    int base_x, base_y, base_z, base_sy, base_cy;
    if (!effects || !render_camera) return;
    side = effects->camera_shake_side;
    up = effects->camera_shake_up;
    forward = effects->camera_shake_forward;
    yaw = effects->camera_shake_yaw;
    pitch = effects->camera_shake_pitch;
    base_x = render_camera->x;
    base_y = render_camera->y;
    base_z = render_camera->z;
    base_sy = render_camera->sy;
    base_cy = render_camera->cy;
    render_camera->x = base_x + (side * base_cy + forward * base_sy) / 1024;
    render_camera->z = base_z + (-side * base_sy + forward * base_cy) / 1024;
    render_camera->y = base_y + up;
    apply_camera_angle(&render_camera->sy, &render_camera->cy, yaw);
    apply_camera_pitch(&render_camera->pitch_sy, &render_camera->pitch_cy,
                       pitch);
}

static void update_camera_shake(struct rasterfall_effects *effects, int dt_ms)
{
    int i;
    int side = 0, up = 0, forward = 0, yaw = 0, pitch = 0;
    int max_side = RASTERFALL_CAMERA_SHAKE_PISTOL_MAX_SIDE;
    int max_up = RASTERFALL_CAMERA_SHAKE_PISTOL_MAX_UP;
    int max_forward = RASTERFALL_CAMERA_SHAKE_PISTOL_MAX_FORWARD;
    int max_yaw = RASTERFALL_CAMERA_SHAKE_PISTOL_MAX_YAW;
    int max_pitch = RASTERFALL_CAMERA_SHAKE_PISTOL_MAX_PITCH;
    if (!effects) return;
    for (i = 0; i < RASTERFALL_EFFECT_INSTANCE_SLOTS; i++) {
        const struct rasterfall_effect_instance *shake = &effects->instances[i];
        if (!shake->active ||
            shake->type != RASTERFALL_EFFECT_INSTANCE_CAMERA_SHAKE)
            continue;
        if (camera_shake_max(shake->weapon, CAMERA_SHAKE_AXIS_SIDE) > max_side)
            max_side = camera_shake_max(shake->weapon, CAMERA_SHAKE_AXIS_SIDE);
        if (camera_shake_max(shake->weapon, CAMERA_SHAKE_AXIS_UP) > max_up)
            max_up = camera_shake_max(shake->weapon, CAMERA_SHAKE_AXIS_UP);
        if (camera_shake_max(shake->weapon, CAMERA_SHAKE_AXIS_FORWARD) > max_forward)
            max_forward = camera_shake_max(shake->weapon, CAMERA_SHAKE_AXIS_FORWARD);
        if (camera_shake_max(shake->weapon, CAMERA_SHAKE_AXIS_YAW) > max_yaw)
            max_yaw = camera_shake_max(shake->weapon, CAMERA_SHAKE_AXIS_YAW);
        if (camera_shake_max(shake->weapon, CAMERA_SHAKE_AXIS_PITCH) > max_pitch)
            max_pitch = camera_shake_max(shake->weapon, CAMERA_SHAKE_AXIS_PITCH);
        if (shake->weapon < 0) {
            yaw += damage_shake_sample(shake->shake_yaw, shake->age_ms);
            continue;
        }
        side += shake_sample(shake->shake_side, shake->shake_seed,
                             shake->age_ms, shake->lifetime_ms);
        up += shake_sample(shake->shake_up, shake->shake_seed + 17,
                           shake->age_ms, shake->lifetime_ms);
        forward += shake_sample(shake->shake_forward, shake->shake_seed + 31,
                                shake->age_ms, shake->lifetime_ms);
        yaw += shake_sample(shake->shake_yaw, shake->shake_seed + 47,
                            shake->age_ms, shake->lifetime_ms);
        pitch += shake_recoil_sample(shake->shake_pitch, shake->shake_seed + 61,
                                     shake->age_ms, shake->lifetime_ms);
    }
    side = shake_clamp(side, max_side);
    up = shake_clamp(up, max_up);
    forward = shake_clamp(forward, max_forward);
    yaw = shake_clamp(yaw, max_yaw);
    pitch = shake_clamp(pitch, max_pitch);
    effects->camera_shake_side = shake_approach(
        effects->camera_shake_side, side, dt_ms);
    effects->camera_shake_up = shake_approach(
        effects->camera_shake_up, up, dt_ms);
    effects->camera_shake_forward = shake_approach(
        effects->camera_shake_forward, forward, dt_ms);
    effects->camera_shake_yaw = shake_approach(
        effects->camera_shake_yaw, yaw, dt_ms);
    effects->camera_shake_pitch = shake_approach(
        effects->camera_shake_pitch, pitch, dt_ms);
}

void rasterfall_effects_update(struct rasterfall_effects *effects, int dt_ms)
{
    int i, steps;
    if (!effects) return;
    steps = dt_ms / 16;
    if (dt_ms > 0 && steps < 1) steps = 1;
    if (effects->damage_shake_cooldown_ms > 0) {
        effects->damage_shake_cooldown_ms -= dt_ms;
        if (effects->damage_shake_cooldown_ms < 0)
            effects->damage_shake_cooldown_ms = 0;
    }
    for (i = 0; i < RASTERFALL_EFFECT_INSTANCE_SLOTS; i++) {
        struct rasterfall_effect_instance *instance = &effects->instances[i];
        if (!instance->active) continue;
        if (instance->kind ==
                RASTERFALL_EFFECT_INSTANCE_KIND_KNOCKBACK_TRAJECTORY) {
            int p;
            for (p = 0; p < instance->trail_count; p++)
                instance->trail[p].age_ms += dt_ms;
        }
        /* The short post-landing lifetime begins only after the target lands. */
        if (instance->kind == RASTERFALL_EFFECT_INSTANCE_KIND_KNOCKBACK_TRAJECTORY &&
            (instance->flags & RASTERFALL_EFFECT_TRAJECTORY_IN_FLIGHT))
            continue;
        instance->age_ms += dt_ms;
        instance->x += instance->vx * steps;
        instance->y += instance->vy * steps;
        instance->z += instance->vz * steps;
        if (instance->gravity_y) instance->vy -= instance->gravity_y * steps;
        if (instance->lifetime_ms > 0)
            instance->alpha = (instance->lifetime_ms - instance->age_ms) *
                              256 / instance->lifetime_ms;
        if (instance->alpha < 0) instance->alpha = 0;
        if (instance->age_ms >= instance->lifetime_ms ||
            (instance->gravity_y && instance->y < -880))
            instance->active = 0;
    }
    update_camera_shake(effects, dt_ms);
    for (i = 0; i < RASTERFALL_EFFECT_EMITTER_SLOTS; i++) {
        struct rasterfall_effect_emitter *emitter = &effects->emitters[i];
        if (!emitter->active) continue;
        if (emitter->child_count > 0 &&
            (emitter->children[0].kind ==
                 RASTERFALL_EFFECT_INSTANCE_KIND_ENEMY_DEATH_FRAGMENT ||
             emitter->children[0].kind ==
                 RASTERFALL_EFFECT_INSTANCE_KIND_ENEMY_DEATH_DUST)) {
            emitter->x += emitter->vx * steps;
            emitter->y += emitter->vy * steps;
            emitter->z += emitter->vz * steps;
            emitter->vy -= emitter->gravity_y * steps;
        }
        emitter->age_ms += dt_ms;
        emitter->spawn_accum_ms += dt_ms;
        while (emitter->spawn_accum_ms >= emitter->spawn_interval_ms &&
               (emitter->child_count > 0 || emitter->spawn_limit <= 0 ||
                emitter->spawned_count < emitter->spawn_limit)) {
            int burst = emitter->burst_count > 0 ? emitter->burst_count : 1;
            int emitted;
            for (emitted = 0; emitted < burst; emitted++) {
                int child_index;
                if (emitter->child_count > 0) {
                    for (child_index = 0; child_index < emitter->child_count;
                         child_index++) {
                        struct rasterfall_effect_emitter_child *child =
                            &emitter->children[child_index];
                        if (child->spawn_limit <= 0 ||
                            child->spawned_count < child->spawn_limit)
                            spawn_emitter_descriptor(effects, emitter, child);
                    }
                    emitter->spawned_count++;
                } else if (emitter->spawn_limit <= 0 ||
                           emitter->spawned_count < emitter->spawn_limit) {
                    spawn_emitter_child(effects, emitter);
                }
            }
            emitter->spawn_accum_ms -= emitter->spawn_interval_ms;
        }
        {
            int exhausted = emitter->child_count > 0;
            int child_index;
            for (child_index = 0; exhausted &&
                 child_index < emitter->child_count; child_index++)
                if (emitter->children[child_index].spawn_limit <= 0 ||
                    emitter->children[child_index].spawned_count <
                    emitter->children[child_index].spawn_limit)
                    exhausted = 0;
            if (emitter->age_ms >= emitter->lifetime_ms ||
                (emitter->child_count > 0 && exhausted) ||
                (emitter->child_count == 0 && emitter->spawn_limit > 0 &&
                 emitter->spawned_count >= emitter->spawn_limit))
            emitter->active = 0;
        }
    }
    effects->weapon_kick -= dt_ms * 2;
    if (effects->weapon_kick < 0) effects->weapon_kick = 0;
}
