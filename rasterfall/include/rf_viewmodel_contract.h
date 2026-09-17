#ifndef RASTERFALL_RF_VIEWMODEL_CONTRACT_H
#define RASTERFALL_RF_VIEWMODEL_CONTRACT_H

#include "toy_renderer.h"

/* Viewmodel Render Contract V1.  Coordinates are camera/view space: X is
 * right, Y is up, and positive Z points away from the camera. */
#define RF_VIEWMODEL_NEAR_Z_V1 192
#define RF_VIEWMODEL_FOCAL_NUMERATOR_V1 3
#define RF_VIEWMODEL_FOCAL_DENOMINATOR_V1 4

struct rf_viewmodel_vertex_v1 {
    int x, y, z;
    int u, v;
    int light, fog;
};

/* A clipped triangle can become a quad.  The returned vertices are ordered
 * for fan triangulation and every returned z is >= RF_VIEWMODEL_NEAR_Z_V1. */
int rf_viewmodel_clip_triangle_v1(
    const struct rf_viewmodel_vertex_v1 input[3],
    struct rf_viewmodel_vertex_v1 output[4]);

/* Project one already-clipped view-space vertex using the frozen V1 policy. */
int rf_viewmodel_project_vertex_v1(
    const struct rf_viewmodel_vertex_v1 *view,
    int framebuffer_width, int framebuffer_height,
    struct toy_screen_vertex *screen);

/* Apply the Post V1 depth fog policy to world pixels.  Viewmodel-covered
 * pixels are deliberately copied unchanged: their world depth is not a
 * viewmodel depth source.  This small reference helper is also the fixture
 * oracle for CPU/GPU Post implementations. */
int rf_viewmodel_post_fog_reference_v1(
    uint32_t *color, const int *world_depth,
    const unsigned char *viewmodel_coverage,
    int width, int height, int stride,
    int fog_near_inv_z, int fog_far_inv_z,
    uint32_t fog_color, int max_density_q8);

/* Contract-only fixture used by --logic-test. */
int rf_viewmodel_contract_logic_test(void);

#endif
