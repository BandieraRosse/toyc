#ifndef RASTERFALL_DRAW_H
#define RASTERFALL_DRAW_H

#include "toy_renderer.h"
#include "rasterfall_session.h"
#include "rasterfall_model.h"
#include "rasterfall_render_resources.h"

/* CPU-backed Draw V0. Normal props use HG-1B registry identity and frame pins;
 * borrowed backing is only for synchronous diagnostic fixtures. No retained
 * DrawSpan or GPU cache yet. WORLD triangle lists, nearest/repeat textures,
 * bottom pivot, no primitive fog, rigid props only. */
struct rasterfall_draw_view {
    struct camera camera;
    int width, height, focal, near_z;
};

struct rasterfall_draw_instance {
    const struct rasterfall_model_asset *mesh;
    /* Registered props validate identity before dereferencing CPU backing.
     * Zero generation is reserved for synchronous diagnostic fixtures. */
    struct rasterfall_resource_handle mesh_handle;
    int asset_id;
    int x, y, z, scale_milli;
    int yaw_sin_q10, yaw_cos_q10;
    int scene_light_q8, form_lighting;
    int force_backface_culling, disable_material_light;
};

struct rasterfall_draw_material {
    const struct toy_texture_view *texture;
    uint32_t color, ambient, specular;
    int specular_power, double_sided;
    /* V0 eligibility requires opaque alpha, no sphere/toon/edge/character
     * policy. Unsupported instances stay wholly on the legacy producer. */
};

struct rasterfall_draw_item {
    unsigned int primitive, first_index, index_count;
    struct rasterfall_draw_material material;
};

enum rasterfall_draw_reject {
    RASTERFALL_DRAW_ACCEPTED,
    RASTERFALL_DRAW_SCOPE,
    RASTERFALL_DRAW_DEFORMATION,
    RASTERFALL_DRAW_MATERIAL,
    RASTERFALL_DRAW_TRANSPARENT,
    RASTERFALL_DRAW_RANGE,
    RASTERFALL_DRAW_REJECT_COUNT
};

#endif
