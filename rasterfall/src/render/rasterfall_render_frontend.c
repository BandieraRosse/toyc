#include "core.h"
#include "string.h"
#include "tlibc_everything.h"
#include "rasterfall_render_frontend.h"

static struct rasterfall_frontend_state frontend_default = {
    .toon_shared = -1, .toon_level = 255, .material_alpha = 255,
    .material_double_sided = 1, .gallery_cy = 1024
};

struct rasterfall_frontend_state *rasterfall_render_frontend_default(void)
{
    return &frontend_default;
}

void rasterfall_render_frontend_set_default_texture(
    const struct toy_texture_view *texture)
{
    frontend_default.texture_view = texture;
}

void rasterfall_render_frontend_set_override(
    struct toy_renderer *renderer, struct rasterfall_frontend_state *state)
{
    if (renderer) renderer->recording_context = state;
}

int rasterfall_render_frontend_bind_worker(
    struct toy_renderer *renderer, struct rasterfall_frontend_state *state)
{
    if (!renderer || !state) return -1;
    renderer->recording_context = state;
    return 0;
}

void rasterfall_render_frontend_unbind_worker(struct toy_renderer *renderer)
{
    if (renderer) renderer->recording_context = 0;
}
