#include "core.h"
#include "string.h"
#include "tlibc_everything.h"
#include "toy_platform.h"
#include "rasterfall_render_frontend.h"

struct rasterfall_frontend_slot {
    pthread_t thread;
    struct rasterfall_frontend_state *state;
};

#define RASTERFALL_FRONTEND_SLOT_COUNT 32

static struct rasterfall_frontend_state frontend_default = {
    .toon_shared = -1, .toon_level = 255, .material_alpha = 255,
    .material_double_sided = 1, .gallery_cy = 1024
};
static struct rasterfall_frontend_slot
    frontend_slots[RASTERFALL_FRONTEND_SLOT_COUNT];
static struct rasterfall_frontend_state *frontend_override;
static pthread_t frontend_owner_thread;

struct rasterfall_frontend_state *rasterfall_render_frontend_current(void)
{
    pthread_t self = pthread_self();
    int i;
    /* Worker binding is more specific than the owner-thread override. */
    for (i = 0; i < RASTERFALL_FRONTEND_SLOT_COUNT; i++) {
        pthread_t thread = __atomic_load_n(&frontend_slots[i].thread,
                                           __ATOMIC_ACQUIRE);
        if (thread == self) {
            struct rasterfall_frontend_state *state =
                __atomic_load_n(&frontend_slots[i].state, __ATOMIC_ACQUIRE);
            if (state) return state;
        }
    }
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

int rasterfall_render_frontend_bind_worker(
    int worker_id, struct rasterfall_frontend_state *state)
{
    pthread_t self = pthread_self();
    int i;
    (void)worker_id;
    for (i = 0; i < RASTERFALL_FRONTEND_SLOT_COUNT; i++) {
        pthread_t thread = __atomic_load_n(&frontend_slots[i].thread,
                                           __ATOMIC_ACQUIRE);
        if (thread == self || (!thread && __atomic_compare_exchange_n(
                &frontend_slots[i].thread, &thread, self, 0,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))) {
            __atomic_store_n(&frontend_slots[i].state, state,
                             __ATOMIC_RELEASE);
            return 0;
        }
    }
    __fprintf(2, "rasterfall: no free frontend thread slot\n");
    return -1;
}

void rasterfall_render_frontend_unbind_worker(int worker_id)
{
    pthread_t self = pthread_self();
    int i;
    (void)worker_id;
    for (i = 0; i < RASTERFALL_FRONTEND_SLOT_COUNT; i++)
        if (__atomic_load_n(&frontend_slots[i].thread, __ATOMIC_ACQUIRE) == self) {
            __atomic_store_n(&frontend_slots[i].state, 0, __ATOMIC_RELEASE);
            __atomic_store_n(&frontend_slots[i].thread, 0, __ATOMIC_RELEASE);
            return;
        }
}
