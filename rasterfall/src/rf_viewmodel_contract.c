#include "rf_viewmodel_contract.h"
#include "tlibc_everything.h"

static int vm_lerp(int a, int b, int numerator, int denominator)
{
    long long delta = (long long)b - a;
    long long value = (long long)a + delta * numerator / denominator;
    return (int)value;
}

static struct rf_viewmodel_vertex_v1 vm_intersection(
    const struct rf_viewmodel_vertex_v1 *a,
    const struct rf_viewmodel_vertex_v1 *b)
{
    struct rf_viewmodel_vertex_v1 result;
    int denominator = b->z - a->z;
    int numerator = RF_VIEWMODEL_NEAR_Z_V1 - a->z;
    if (!denominator) return *a;
    result.x = vm_lerp(a->x, b->x, numerator, denominator);
    result.y = vm_lerp(a->y, b->y, numerator, denominator);
    result.z = RF_VIEWMODEL_NEAR_Z_V1;
    result.u = vm_lerp(a->u, b->u, numerator, denominator);
    result.v = vm_lerp(a->v, b->v, numerator, denominator);
    result.light = vm_lerp(a->light, b->light, numerator, denominator);
    result.fog = vm_lerp(a->fog, b->fog, numerator, denominator);
    return result;
}

int rf_viewmodel_clip_triangle_v1(
    const struct rf_viewmodel_vertex_v1 input[3],
    struct rf_viewmodel_vertex_v1 output[4])
{
    struct rf_viewmodel_vertex_v1 polygon[4];
    int count = 0, i;
    if (!input || !output) return 0;
    for (i = 0; i < 3; ++i) polygon[i] = input[i];
    count = 3;
    /* Sutherland-Hodgman against z >= near.  With one plane a triangle can
     * produce at most four vertices, so no dynamic allocation is needed. */
    {
        struct rf_viewmodel_vertex_v1 clipped[4];
        int out_count = 0;
        for (i = 0; i < count; ++i) {
            const struct rf_viewmodel_vertex_v1 *a = &polygon[i];
            const struct rf_viewmodel_vertex_v1 *b = &polygon[(i + 1) % count];
            int a_inside = a->z >= RF_VIEWMODEL_NEAR_Z_V1;
            int b_inside = b->z >= RF_VIEWMODEL_NEAR_Z_V1;
            if (a_inside && b_inside) {
                clipped[out_count++] = *b;
            } else if (a_inside && !b_inside) {
                clipped[out_count++] = vm_intersection(a, b);
            } else if (!a_inside && b_inside) {
                clipped[out_count++] = vm_intersection(a, b);
                clipped[out_count++] = *b;
            }
        }
        for (i = 0; i < out_count; ++i) output[i] = clipped[i];
        return out_count;
    }
}

int rf_viewmodel_project_vertex_v1(
    const struct rf_viewmodel_vertex_v1 *view,
    int framebuffer_width, int framebuffer_height,
    struct toy_screen_vertex *screen)
{
    int focal;
    if (!view || !screen || framebuffer_width <= 0 || framebuffer_height <= 0 ||
        view->z < RF_VIEWMODEL_NEAR_Z_V1)
        return -1;
    focal = framebuffer_width * RF_VIEWMODEL_FOCAL_NUMERATOR_V1 /
            RF_VIEWMODEL_FOCAL_DENOMINATOR_V1;
    memset(screen, 0, sizeof(*screen));
    screen->x = framebuffer_width / 2 + view->x * focal / view->z;
    screen->y = framebuffer_height / 2 - view->y * focal / view->z;
    screen->z = view->z;
    screen->u = view->u;
    screen->v = view->v;
    screen->inv_z = (long)1048576 / view->z;
    screen->u_over_z = (long)view->u * 1048576L / view->z;
    screen->v_over_z = (long)view->v * 1048576L / view->z;
    screen->light = view->light;
    screen->fog = view->fog;
    return 0;
}

static uint32_t vm_fog_color(uint32_t scene, int depth, int near_inv_z,
                             int far_inv_z, uint32_t fog_color,
                             int max_density_q8)
{
    unsigned int density, inverse;
    unsigned int r, g, b;
    if (max_density_q8 < 0) max_density_q8 = 0;
    if (max_density_q8 > 256) max_density_q8 = 256;
    if (depth <= far_inv_z) density = (unsigned int)max_density_q8;
    else if (depth < near_inv_z && near_inv_z > far_inv_z)
        density = (unsigned int)(near_inv_z - depth) *
                  (unsigned int)max_density_q8 /
                  (unsigned int)(near_inv_z - far_inv_z);
    else density = 0;
    if (density > 256) density = 256;
    inverse = 256U - density;
    r = (((scene >> 16) & 255U) * inverse +
         ((fog_color >> 16) & 255U) * density) >> 8;
    g = (((scene >> 8) & 255U) * inverse +
         ((fog_color >> 8) & 255U) * density) >> 8;
    b = ((scene & 255U) * inverse + (fog_color & 255U) * density) >> 8;
    return 0xff000000U | (r << 16) | (g << 8) | b;
}

int rf_viewmodel_post_fog_reference_v1(
    uint32_t *color, const int *world_depth,
    const unsigned char *viewmodel_coverage,
    int width, int height, int stride,
    int fog_near_inv_z, int fog_far_inv_z,
    uint32_t fog_color, int max_density_q8)
{
    int x, y;
    if (!color || !world_depth || !viewmodel_coverage || width <= 0 ||
        height <= 0 || stride < width || fog_far_inv_z > fog_near_inv_z)
        return -1;
    for (y = 0; y < height; ++y)
        for (x = 0; x < width; ++x) {
            unsigned long p = (unsigned long)y * (unsigned long)stride + x;
            if (!viewmodel_coverage[p])
                color[p] = vm_fog_color(color[p], world_depth[p],
                                        fog_near_inv_z, fog_far_inv_z,
                                        fog_color, max_density_q8);
        }
    return 0;
}

static int vm_triangle(struct toy_renderer *renderer,
                       const struct toy_screen_vertex *a,
                       const struct toy_screen_vertex *b,
                       const struct toy_screen_vertex *c,
                       uint32_t color)
{
    toy_renderer_triangle_lit(renderer, a, b, c, color, 256, 0);
    return 0;
}

int rf_viewmodel_contract_logic_test(void)
{
    struct rf_viewmodel_vertex_v1 input[3], clipped[4];
    struct toy_screen_vertex a;
    uint32_t color[32 * 24], world_color[32 * 24];
    int world_depth[32 * 24], vm_depth[32 * 24];
    unsigned char coverage[32 * 24];
    struct toy_surface surface;
    struct toy_renderer renderer;
    int n, i, before;
    int *renderer_depth;

    memset(input, 0, sizeof(input));
    input[0].x = -100; input[0].y = -40; input[0].z = 128;
    input[1].x = 100; input[1].y = -40; input[1].z = 256;
    input[2].x = 0; input[2].y = 100; input[2].z = 384;
    n = rf_viewmodel_clip_triangle_v1(input, clipped);
    if (n != 4) return 1;
    for (i = 0; i < n; ++i)
        if (clipped[i].z < RF_VIEWMODEL_NEAR_Z_V1) return 2;
    if (rf_viewmodel_project_vertex_v1(&clipped[0], 32, 24, &a) < 0 ||
        a.x != 16 + clipped[0].x * 24 / clipped[0].z ||
        a.y != 12 - clipped[0].y * 24 / clipped[0].z)
        return 3;

    memset(color, 0x11, sizeof(color));
    memset(world_color, 0x11, sizeof(world_color));
    for (i = 0; i < 32 * 24; ++i) world_depth[i] = 777;
    memset(vm_depth, 0, sizeof(vm_depth));
    memset(coverage, 0, sizeof(coverage));
    surface.pixels = color; surface.width = 32; surface.height = 24;
    surface.stride = 32 * (int)sizeof(uint32_t);
    toy_renderer_init(&renderer);
    toy_renderer_set_worker_count(&renderer, -1);
    if (toy_renderer_begin(&renderer, &surface, 0x111111U) < 0) return 4;
    renderer_depth = renderer.depth;
    renderer.depth = world_depth;
    {
        struct toy_screen_vertex w0 = {4, 4, 300, 0, 0, 3000, 0, 0,
                                       0, 0, 0, 0, 256, 0};
        struct toy_screen_vertex w1 = {27, 4, 300, 0, 0, 3000, 0, 0,
                                       0, 0, 0, 0, 256, 0};
        struct toy_screen_vertex w2 = {16, 20, 300, 0, 0, 3000, 0, 0,
                                       0, 0, 0, 0, 256, 0};
        vm_triangle(&renderer, &w0, &w1, &w2, 0x102030U);
    }
    if (toy_renderer_flush(&renderer) < 0) return 5;
    memcpy(world_color, color, sizeof(color));
    memcpy(world_depth, renderer.depth, sizeof(world_depth));
    before = world_depth[12 * 32 + 16];
    memset(vm_depth, 0, sizeof(vm_depth));
    renderer.depth = vm_depth;
    renderer.coverage = coverage;
    renderer.coverage_stride = 32;
    {
        struct toy_screen_vertex v0 = {4, 4, 300, 0, 0, 2000, 0, 0,
                                       0, 0, 0, 0, 256, 0};
        struct toy_screen_vertex v1 = {27, 4, 300, 0, 0, 2000, 0, 0,
                                       0, 0, 0, 0, 256, 0};
        struct toy_screen_vertex v2 = {16, 20, 300, 0, 0, 2000, 0, 0,
                                       0, 0, 0, 0, 256, 0};
        struct toy_screen_vertex n0 = {4, 4, 300, 0, 0, 3000, 0, 0,
                                       0, 0, 0, 0, 256, 0};
        struct toy_screen_vertex n1 = {27, 4, 300, 0, 0, 3000, 0, 0,
                                       0, 0, 0, 0, 256, 0};
        struct toy_screen_vertex n2 = {16, 20, 300, 0, 0, 3000, 0, 0,
                                       0, 0, 0, 0, 256, 0};
        struct toy_screen_vertex f0 = {4, 4, 300, 0, 0, 1000, 0, 0,
                                       0, 0, 0, 0, 256, 0};
        struct toy_screen_vertex f1 = {27, 4, 300, 0, 0, 1000, 0, 0,
                                       0, 0, 0, 0, 256, 0};
        struct toy_screen_vertex f2 = {16, 20, 300, 0, 0, 1000, 0, 0,
                                       0, 0, 0, 0, 256, 0};
        vm_triangle(&renderer, &v0, &v1, &v2, 0x203040U);
        vm_triangle(&renderer, &n0, &n1, &n2, 0x405060U);
        vm_triangle(&renderer, &f0, &f1, &f2, 0x708090U);
    }
    if (toy_renderer_flush(&renderer) < 0 ||
        (color[12 * 32 + 16] & 0xffffffU) != 0x405060U ||
        world_depth[12 * 32 + 16] != before || !coverage[12 * 32 + 16]) {
        return 6;
    }
    renderer.depth = world_depth;
    renderer.coverage = NULL;
    renderer.coverage_stride = 0;
    if (memcmp(world_color, color, sizeof(color)) == 0) return 7;
    {
        int post_depth[32 * 24];
        unsigned char post_coverage[32 * 24];
        uint32_t post_color[32 * 24];
        memset(post_depth, 0, sizeof(post_depth));
        memset(post_coverage, 0, sizeof(post_coverage));
        for (i = 0; i < 32 * 24; ++i) post_color[i] = 0xff404040U;
        post_coverage[0] = 255;
        if (rf_viewmodel_post_fog_reference_v1(
                post_color, post_depth, post_coverage, 32, 24, 32,
                2048, 256, 0xff90a0b0U, 192) < 0 ||
            post_color[0] != 0xff404040U || post_color[1] == 0xff404040U)
            return 8;
    }
    renderer.depth = renderer_depth;
    toy_renderer_destroy(&renderer);
    return 0;
}
