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

void rasterfall_effects_init(struct rasterfall_effects *effects)
{
    memset(effects, 0, sizeof(struct rasterfall_effects));
    effects->rng = 0x243F6A88;
}

void rasterfall_effects_reset_fire(struct rasterfall_effects *effects)
{
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
    }
}

void rasterfall_effects_update(struct rasterfall_effects *effects, int dt_ms)
{
    int i;
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
