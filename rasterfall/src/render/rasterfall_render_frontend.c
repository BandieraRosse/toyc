#include "core.h"
#include "string.h"
#include "tlibc_everything.h"
#include "toy_platform.h"
#include "rasterfall_render_frontend.h"

struct rasterfall_frontend_slot {
    pthread_t volatile thread;
    struct rasterfall_frontend_state *volatile state;
};

static struct rasterfall_frontend_state frontend_default = {
    .toon_shared = -1, .toon_level = 255, .material_alpha = 255,
    .material_double_sided = 1, .gallery_cy = 1024
};
static struct rasterfall_frontend_slot frontend_slots[8];
static struct rasterfall_frontend_state *frontend_override;
static pthread_t frontend_owner_thread;

struct rasterfall_frontend_state *rasterfall_render_frontend_current(void)
{
    pthread_t self = pthread_self();
    int i;
    /* Worker binding is more specific than the owner-thread override. */
    for (i = 0; i < 8; i++)
        if (frontend_slots[i].thread == self && frontend_slots[i].state)
            return frontend_slots[i].state;
    if (frontend_owner_thread == self && frontend_override)
        return frontend_override;
    return &frontend_default;
}

void rasterfall_render_frontend_set_owner(void)
{
    frontend_owner_thread = pthread_self();
}

void rasterfall_render_frontend_set_default_texture(
    const struct toy_texture_view *texture)
{
    frontend_default.texture_view = texture;
}

void rasterfall_render_frontend_set_override(
    struct rasterfall_frontend_state *state)
{
    frontend_override = state;
}

void rasterfall_render_frontend_bind_worker(
    int worker_id, struct rasterfall_frontend_state *state)
{
    if (worker_id < 0 || worker_id >= 8) return;
    frontend_slots[worker_id].thread = pthread_self();
    __sync_synchronize();
    frontend_slots[worker_id].state = state;
    __sync_synchronize();
}

void rasterfall_render_frontend_unbind_worker(int worker_id)
{
    if (worker_id < 0 || worker_id >= 8) return;
    __sync_synchronize();
    frontend_slots[worker_id].state = 0;
    __sync_synchronize();
}
