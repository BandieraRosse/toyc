#ifndef RASTERFALL_RENDER_FRONTEND_H
#define RASTERFALL_RENDER_FRONTEND_H

#include "toy_assets.h"
#include "rasterfall_render.h"

struct gallery_cached_vertex;

/* Mutable model-frontend state belongs to one renderer path.  Worker jobs
 * bind distinct instances; only the render owner thread may use override. */
struct rasterfall_frontend_state {
    const struct toy_texture_view *texture_view;
    const struct toy_texture_view *sphere_texture;
    const struct toy_texture_view *toon_texture;
    int sphere_mode, toon_shared, toon_level;
    int material_alpha, material_double_sided;
    uint32_t material_ambient, material_specular, material_tint;
    int material_specular_power, material_specular_level;
    struct rasterfall_model_triangle_stats *model_triangle_stats;
    struct gallery_cached_vertex *vertex_cache;
    unsigned int vertex_cache_capacity;
    struct rasterfall_model_setup_timing timing;
    int reuse_skinned_vertices;
    int skinned_vertices_valid;
    int disable_edge;
    int disable_sphere;
    int disable_toon;
    int face_material;
    int skin_material;
    int gallery_facing, gallery_sy, gallery_cy;
};

struct rasterfall_frontend_state *rasterfall_render_frontend_current(void);
void rasterfall_render_frontend_set_owner(void);
void rasterfall_render_frontend_set_default_texture(
    const struct toy_texture_view *texture);
void rasterfall_render_frontend_set_override(
    struct rasterfall_frontend_state *state);
int rasterfall_render_frontend_bind_worker(
    int worker_id, struct rasterfall_frontend_state *state);
void rasterfall_render_frontend_unbind_worker(int worker_id);

#endif
