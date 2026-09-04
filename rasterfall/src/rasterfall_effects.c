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
    if (emitter->alpha <= 0) emitter->alpha = 256;
    return emitter;
}

static const int explosion_velocity[16][3] = {
    { 52,  0,  0 }, { -52,  0,  0 }, { 0,  0, 52 }, { 0,  0, -52 },
    { 36, 28,  0 }, { -36, 28,  0 }, { 0, 28, 36 }, { 0, 28, -36 },
    { 28, -18, 28 }, { -28, -18, 28 }, { 28, -18, -28 }, { -28, -18, -28 },
    { 18, 42, 18 }, { -18, 42, 18 }, { 18, 42, -18 }, { -18, 42, -18 }
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
    if (emitter->pattern == 1) {
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
    } else if (emitter->child_kind == RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION_PARTICLE) {
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
            if (effects->emitters[j].child_kind ==
                    RASTERFALL_EFFECT_INSTANCE_KIND_FIRE &&
                effects->emitters[j].source_id == i) {
                emitter = &effects->emitters[j];
                break;
            }
        }
        if (!emitter) {
            struct rasterfall_effect_emitter seed;
            memset(&seed, 0, sizeof(seed));
            seed.source_id = i;
            seed.x = zone->x; seed.y = -890; seed.z = zone->z;
            seed.lifetime_ms = zone->remaining_ms;
            seed.spawn_interval_ms = 16;
            seed.burst_count = 88;
            seed.child_type = RASTERFALL_EFFECT_INSTANCE_PARTICLE;
            seed.child_kind = RASTERFALL_EFFECT_INSTANCE_KIND_FIRE;
            seed.spawn_limit = 0;
            seed.pattern = 1;
            seed.alpha = 256;
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
        if (emitter->child_kind == RASTERFALL_EFFECT_INSTANCE_KIND_FIRE &&
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

static void spawn_event_instance(struct rasterfall_effects *effects,
                                 const struct rasterfall_effect_event *event,
                                 int type, int kind, int x, int y, int z,
                                 int vx, int vy, int vz)
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
    rasterfall_effects_spawn_instance(effects, &instance);
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

void rasterfall_effects_emit(struct rasterfall_effects *effects,
                             const struct rasterfall_effect_cue *cue)
{
    struct rasterfall_effect_event event;
    if (!cue) return;
    memset(&event, 0, sizeof(event));
    event.type = cue->type;
    if (event.type == RASTERFALL_EFFECT_CUE_TRACER)
        event.type = RASTERFALL_EFFECT_EVENT_TRACER;
    event.flags = cue->depth_test ? RASTERFALL_EFFECT_EVENT_DEPTH_TEST : 0;
    event.sx = cue->sx; event.sy = cue->sy; event.sz = cue->sz;
    event.ex = cue->ex; event.ey = cue->ey; event.ez = cue->ez;
    event.x = cue->ex; event.y = cue->ey; event.z = cue->ez;
    event.dir_sy = cue->dir_sy; event.dir_cy = cue->dir_cy;
    event.life_ms = cue->life_ms;
    rasterfall_effects_consume(effects, &event);
}

void rasterfall_effects_consume(struct rasterfall_effects *effects,
                                const struct rasterfall_effect_event *event)
{
    if (!effects || !event) return;
    if (event->type == RASTERFALL_EFFECT_EVENT_WEAPON_FIRE) {
        spawn_event_instance(effects, event,
                             RASTERFALL_EFFECT_INSTANCE_BILLBOARD,
                             RASTERFALL_EFFECT_INSTANCE_KIND_MUZZLE_FLASH,
                             event->sx, event->sy, event->sz, 0, 0, 0);
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
        if (event->type == RASTERFALL_EFFECT_EVENT_ENTITY_HIT) {
            struct rasterfall_effect_event hit_ray = *event;
            hit_ray.ex = event->x;
            hit_ray.ey = event->y;
            hit_ray.ez = event->z;
            hit_ray.life_ms = 45;
            spawn_event_instance(effects, &hit_ray,
                                 RASTERFALL_EFFECT_INSTANCE_RAY,
                                 RASTERFALL_EFFECT_INSTANCE_KIND_ENTITY_HIT_RAY,
                                 event->sx, event->sy, event->sz, 0, 0, 0);
        }
    } else if (event->type == RASTERFALL_EFFECT_EVENT_EXPLOSION) {
        struct rasterfall_effect_emitter emitter;
        struct rasterfall_effect_event shockwave = *event;
        memset(&emitter, 0, sizeof(emitter));
        emitter.x = event->x; emitter.y = event->y; emitter.z = event->z;
        emitter.lifetime_ms = 180;
        emitter.spawn_interval_ms = 1;
        emitter.spawn_limit = 16;
        emitter.child_type = RASTERFALL_EFFECT_INSTANCE_PARTICLE;
        emitter.child_kind = RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION_PARTICLE;
        emitter.spread = 1000;
        emitter.gravity_y = 2;
        emitter.size = 4500;
        emitter.color = 0xFFD050;
        rasterfall_effects_spawn_emitter(effects, &emitter);
        spawn_event_instance(effects, event,
                             RASTERFALL_EFFECT_INSTANCE_EMITTER,
                             RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION,
                             event->x, event->y, event->z, 0, 0, 0);
        shockwave.sx = event->x; shockwave.sy = event->y; shockwave.sz = event->z;
        shockwave.ex = event->x + 1400; shockwave.ey = event->y;
        shockwave.ez = event->z;
        shockwave.life_ms = 80;
        spawn_event_instance(effects, &shockwave,
                             RASTERFALL_EFFECT_INSTANCE_RAY,
                             RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION_RAY,
                             event->x, event->y, event->z, 0, 0, 0);
        spawn_event_instance(effects, event,
                             RASTERFALL_EFFECT_INSTANCE_BILLBOARD,
                             RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION_FLASH,
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
               (emitter->spawn_limit <= 0 ||
                emitter->spawned_count < emitter->spawn_limit)) {
            int burst = emitter->burst_count > 0 ? emitter->burst_count : 1;
            int emitted;
            for (emitted = 0; emitted < burst &&
                 (emitter->spawn_limit <= 0 ||
                  emitter->spawned_count < emitter->spawn_limit); emitted++)
                spawn_emitter_child(effects, emitter);
            emitter->spawn_accum_ms -= emitter->spawn_interval_ms;
        }
        if (emitter->age_ms >= emitter->lifetime_ms ||
            (emitter->spawn_limit > 0 &&
             emitter->spawned_count >= emitter->spawn_limit))
            emitter->active = 0;
    }
    effects->weapon_kick -= dt_ms * 2;
    if (effects->weapon_kick < 0) effects->weapon_kick = 0;
}
