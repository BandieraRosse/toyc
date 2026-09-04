#include "tlibc_everything.h"
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
                                          const struct toy_game *game)
{
    struct rasterfall_effect_instance instance;
    int i;
    if (!effects || !game) return;
    for (i = 0; i < RASTERFALL_EFFECT_INSTANCE_SLOTS; i++)
        if (effects->instances[i].kind ==
            RASTERFALL_EFFECT_INSTANCE_KIND_DAMAGE_FLASH)
            effects->instances[i].active = 0;
    if (game->damage_flash_ms <= 0) return;
    memset(&instance, 0, sizeof(instance));
    instance.type = RASTERFALL_EFFECT_INSTANCE_OVERLAY;
    instance.kind = RASTERFALL_EFFECT_INSTANCE_KIND_DAMAGE_FLASH;
    instance.width = 0;
    instance.height = 0;
    instance.lifetime_ms = game->damage_flash_ms;
    instance.alpha = game->damage_flash_ms * 128 / TOY_GAME_DAMAGE_FLASH_MS;
    instance.color = 0xAA0000;
    rasterfall_effects_spawn_instance(effects, &instance);
}

void rasterfall_effects_sync_enemy_feedback(struct rasterfall_effects *effects,
                                            const struct toy_game *game)
{
    int i;
    if (!effects || !game) return;
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
        rasterfall_effects_spawn_instance(effects, &instance);
    }
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
    } else if (event->type == RASTERFALL_EFFECT_EVENT_EXPLOSION) {
        struct rasterfall_effect_emitter emitter;
        init_explosion_emitter(&emitter, event);
        rasterfall_effects_spawn_emitter(effects, &emitter);
        spawn_event_instance(effects, event,
                             RASTERFALL_EFFECT_INSTANCE_EMITTER,
                             RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION,
                             event->x, event->y, event->z, 0, 0, 0);
    }
}

void rasterfall_effects_update(struct rasterfall_effects *effects, int dt_ms)
{
    int i, steps;
    steps = dt_ms / 16;
    if (dt_ms > 0 && steps < 1) steps = 1;
    for (i = 0; i < RASTERFALL_EFFECT_INSTANCE_SLOTS; i++) {
        struct rasterfall_effect_instance *instance = &effects->instances[i];
        if (!instance->active) continue;
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
    for (i = 0; i < RASTERFALL_EFFECT_EMITTER_SLOTS; i++) {
        struct rasterfall_effect_emitter *emitter = &effects->emitters[i];
        if (!emitter->active) continue;
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
