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
    if (instance->alpha <= 0) instance->alpha = 256;
    return instance;
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
    memset(effects->tracers, 0, sizeof(effects->tracers));
    memset(effects->particles, 0, sizeof(effects->particles));
    effects->tracer_next = 0;
    effects->particle_next = 0;
    memset(effects->muzzle_flashes, 0, sizeof(effects->muzzle_flashes));
    effects->muzzle_flash_next = 0;
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
        struct rasterfall_particle *p = &effects->particles[effects->particle_next];
        effects->particle_next = (effects->particle_next + 1) % RASTERFALL_PARTICLE_SLOTS;
        p->active = 1;
        p->x = x;
        p->y = y + effect_rand(effects, -10, 10);
        p->z = z;
        p->vx = sy * 22 / 1024 + effect_rand(effects, -24, 24);
        p->vy = effect_rand(effects, 8, 30);
        p->vz = cy * 22 / 1024 + effect_rand(effects, -24, 24);
        p->life_ms = RASTERFALL_PARTICLE_LIFE_MS + effect_rand(effects, -40, 40);
        {
            struct rasterfall_effect_instance instance;
            memset(&instance, 0, sizeof(instance));
            instance.type = RASTERFALL_EFFECT_INSTANCE_PARTICLE;
            instance.kind = RASTERFALL_EFFECT_INSTANCE_KIND_HIT_PARTICLE;
            instance.x = p->x; instance.y = p->y; instance.z = p->z;
            /* The legacy particle pool advances once per 16ms fixed step;
             * normalize its velocity for the runtime's per-millisecond API. */
            instance.vx = p->vx / 16; instance.vy = p->vy / 16;
            instance.vz = p->vz / 16;
            instance.lifetime_ms = p->life_ms;
            rasterfall_effects_spawn_instance(effects, &instance);
        }
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
    struct rasterfall_tracer *tracer;
    struct rasterfall_muzzle_flash *flash;
    if (!effects || !event) return;
    if (event->type == RASTERFALL_EFFECT_EVENT_WEAPON_FIRE) {
        spawn_event_instance(effects, event,
                             RASTERFALL_EFFECT_INSTANCE_BILLBOARD,
                             RASTERFALL_EFFECT_INSTANCE_KIND_MUZZLE_FLASH,
                             event->sx, event->sy, event->sz, 0, 0, 0);
        flash = &effects->muzzle_flashes[effects->muzzle_flash_next];
        effects->muzzle_flash_next =
            (effects->muzzle_flash_next + 1) % RASTERFALL_MUZZLE_FLASH_SLOTS;
        flash->active = 1;
        flash->x = event->sx; flash->y = event->sy; flash->z = event->sz;
        flash->sy = event->dir_sy; flash->cy = event->dir_cy;
        flash->weapon = event->weapon;
        flash->life_ms = event->life_ms > 0 ? event->life_ms :
                         RASTERFALL_MUZZLE_FLASH_LIFE_MS;
    } else if (event->type == RASTERFALL_EFFECT_EVENT_TRACER) {
        {
            spawn_event_instance(effects, event,
                                 RASTERFALL_EFFECT_INSTANCE_RAY,
                                 RASTERFALL_EFFECT_INSTANCE_KIND_TRACER,
                                 event->sx, event->sy, event->sz,
                                 0, 0, 0);
        }
        tracer = &effects->tracers[effects->tracer_next];
        effects->tracer_next =
            (effects->tracer_next + 1) % RASTERFALL_TRACER_SLOTS;
        tracer->active = 1;
        tracer->depth_test = (event->flags & RASTERFALL_EFFECT_EVENT_DEPTH_TEST) != 0;
        tracer->sx = event->sx; tracer->sy = event->sy; tracer->sz = event->sz;
        tracer->ex = event->ex; tracer->ey = event->ey; tracer->ez = event->ez;
        tracer->life_ms = event->life_ms > 0 ? event->life_ms :
                          RASTERFALL_TRACER_LIFE_MS;
    } else if (event->type == RASTERFALL_EFFECT_EVENT_BULLET_IMPACT ||
               event->type == RASTERFALL_EFFECT_EVENT_ENTITY_HIT) {
        rasterfall_effects_spawn_hit_particles(effects, event->x, event->y,
                                                event->z, event->dir_sy,
                                                event->dir_cy);
    } else if (event->type == RASTERFALL_EFFECT_EVENT_EXPLOSION) {
        spawn_event_instance(effects, event,
                             RASTERFALL_EFFECT_INSTANCE_EMITTER,
                             RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION,
                             event->x, event->y, event->z, 0, 0, 0);
    }
}

void rasterfall_effects_update(struct rasterfall_effects *effects, int dt_ms)
{
    int i;
    for (i = 0; i < RASTERFALL_EFFECT_INSTANCE_SLOTS; i++) {
        struct rasterfall_effect_instance *instance = &effects->instances[i];
        if (!instance->active) continue;
        instance->age_ms += dt_ms;
        instance->x += instance->vx * dt_ms;
        instance->y += instance->vy * dt_ms;
        instance->z += instance->vz * dt_ms;
        if (instance->lifetime_ms > 0)
            instance->alpha = (instance->lifetime_ms - instance->age_ms) *
                              256 / instance->lifetime_ms;
        if (instance->alpha < 0) instance->alpha = 0;
        if (instance->age_ms >= instance->lifetime_ms)
            instance->active = 0;
    }
    effects->weapon_kick -= dt_ms * 2;
    if (effects->weapon_kick < 0) effects->weapon_kick = 0;
    for (i = 0; i < RASTERFALL_TRACER_SLOTS; i++) {
        struct rasterfall_tracer *t = &effects->tracers[i];
        if (!t->active) continue;
        t->life_ms -= dt_ms;
        if (t->life_ms <= 0) t->active = 0;
    }
    for (i = 0; i < RASTERFALL_PARTICLE_SLOTS; i++) {
        struct rasterfall_particle *p = &effects->particles[i];
        if (!p->active) continue;
        p->x += p->vx;
        p->y += p->vy;
        p->z += p->vz;
        p->vy -= RASTERFALL_PARTICLE_GRAVITY;
        p->life_ms -= dt_ms;
        if (p->life_ms <= 0 || p->y < -880) p->active = 0;
    }
    for (i = 0; i < RASTERFALL_MUZZLE_FLASH_SLOTS; i++) {
        struct rasterfall_muzzle_flash *f = &effects->muzzle_flashes[i];
        if (!f->active) continue;
        f->life_ms -= dt_ms;
        if (f->life_ms <= 0) f->active = 0;
    }
}
