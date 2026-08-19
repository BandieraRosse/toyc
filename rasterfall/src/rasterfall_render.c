#include "core.h"
#include "string.h"
#include "tlibc_everything.h"
#include "toy_renderer.h"
#include "toy_assets.h"
#include "toy_platform.h"
#include "fb_draw.h"
#include "fb_font.h"
#include "toy_map.h"
#include "toy_game.h"
#include "rasterfall_session.h"
#include "rasterfall_effects.h"
#include "rasterfall_net.h"
#include "rasterfall_sky.h"
#include "rasterfall_render.h"

#define special_target_active ability.special_target_active
#define charge_active ability.charge_active
#define charge_elapsed_ms ability.charge_elapsed_ms
#define special_target_kind ability.special_target_kind
#define special_target_index ability.special_target_index
#include "rasterfall_model.h"
#include "rasterfall_viewmodel.h"
#include "rasterfall_animation.h"
#include "math.h"

#define NEAR_Z 192
#define ENEMY_RENDER_DISTANCE 24000
#define UV_ONE 65536
#define BAKED_LM_W 32
#define BAKED_LM_H 24

struct vec3 { int x, y, z; };
struct box { int minx, maxx, minz, maxz, height; uint32_t color; };

static struct rasterfall_render_context *render_ctx;
static struct rasterfall_session *active_session;
static struct rasterfall_effects *active_effects;
static const struct rasterfall_net *active_net;
static const struct toy_texture_view *active_wall_texture;
static const struct toy_texture_view *active_model_texture;
static const struct toy_texture_view *active_sphere_texture;
static int active_sphere_mode;
static const struct toy_texture_view *active_toon_texture;
static int active_toon_shared = -1;
static int active_toon_level = 255;
static int active_material_alpha = 255;
static int active_material_double_sided = 1;
static uint32_t active_material_ambient;
static uint32_t active_material_specular;
static int active_material_specular_power;
static int active_material_specular_level;
static unsigned short *active_lightmap;
static int active_textures;
static int active_fixed_floor_lighting;
static int active_enemy_lift;
static int active_actor_lift;
static int active_actor_roll_sin;
static int active_actor_roll_cos = 1024;
static int active_gallery_lighting;
static int active_disable_sphere;
static int active_disable_toon;
static int active_disable_edge;
static int active_disable_material_light;
static int active_emissive_projectile;
static int active_coordinate_axes;

#define level_map active_session->level
#define game active_session->game_state
#define interactables active_session->items
#define interactable_count active_session->item_count
#define highlighted active_session->highlight_index
#define baked_lightmap active_lightmap
#define baked_light_at_render(x, z) baked_light_at(x, z)
#define fixed_floor_lighting active_fixed_floor_lighting
#define textures_enabled active_textures
#define active_texture_view active_wall_texture
#define wall_texture_view active_wall_texture
#define effects (*active_effects)
typedef struct rasterfall_interactable interactable;

#define RULER_STEP 1000
#define RULER_GROUND_Y -900
#define RULER_AXIS_HALF 18
#define RULER_TICK_HALF 28

static int draw_world_triangle(struct toy_renderer *renderer,
                               const struct camera *camera,
                               const struct vec3 *a, const struct vec3 *b,
                               const struct vec3 *c, uint32_t color);
static void world_to_view(const struct camera *camera, const struct vec3 *world,
                          struct vec3 *view);
static int world_distance(const struct camera *camera, int x, int z);
static void project_vertex(const struct toy_surface *surface,
                           const struct vec3 *view,
                           struct toy_screen_vertex *screen);
struct world_uv_vertex;
static int draw_world_triangle_tex(struct toy_renderer *renderer,
                                   const struct camera *camera,
                                   const struct world_uv_vertex *a,
                                   const struct world_uv_vertex *b,
                                   const struct world_uv_vertex *c);
static void rotate_arm_xz(int x, int z, int degrees, int *out_x, int *out_z);

static struct rasterfall_model_asset gallery_models[RASTERFALL_MODEL_MAX_GALLERY];
static int gallery_loaded;
static struct rasterfall_model_asset private_character_model;
static int private_character_loaded;

#define RASTERFALL_MODEL_PATH_BYTES 160
#define RASTERFALL_GALLERY_COLUMNS 9
#define RASTERFALL_GALLERY_SLOT_X 1300
#define RASTERFALL_GALLERY_ROW_Y 700
#define RASTERFALL_GALLERY_BASE_Y 500
static char gallery_paths[RASTERFALL_MODEL_MAX_GALLERY]
                          [RASTERFALL_MODEL_PATH_BYTES];

struct world_uv_vertex { struct vec3 p; int u, v; int su, sv; };

static int clampi(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static void fill_rect(struct toy_surface *surface, int x, int y,
                      int width, int height, uint32_t color)
{
    int right = x + width, bottom = y + height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > surface->width) right = surface->width;
    if (bottom > surface->height) bottom = surface->height;
    for (int py = y; py < bottom; py++)
        for (int px = x; px < right; px++) {
            uint32_t *row = (uint32_t *)((unsigned char *)surface->pixels +
                                         py * surface->stride);
            row[px] = color;
        }
}

static uint32_t mix_color(uint32_t from, uint32_t to, int num, int den)
{
    unsigned long fr = (from >> 16) & 0xFF, fg = (from >> 8) & 0xFF, fb = from & 0xFF;
    unsigned long tr = (to >> 16) & 0xFF, tg = (to >> 8) & 0xFF, tb = to & 0xFF;
    return (uint32_t)(((fr * num + tr * (den - num)) / den) << 16 |
                      ((fg * num + tg * (den - num)) / den) << 8 |
                      ((fb * num + tb * (den - num)) / den));
}

static unsigned int model_u32(const unsigned char *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
}

static void gallery_load(void)
{
    int i, count;
    if (gallery_loaded) return;
    count = toy_platform_list_models(gallery_paths, RASTERFALL_MODEL_MAX_GALLERY);

    /* getdents order is filesystem-dependent; sort paths for a stable
     * gallery layout and for predictable inventory by the displayed index. */
    for (i = 1; i < count; i++) {
        int j = i;
        char path[RASTERFALL_MODEL_PATH_BYTES];
        strcpy(path, gallery_paths[i]);
        while (j > 0 && strcmp(path, gallery_paths[j - 1]) < 0) {
            strcpy(gallery_paths[j], gallery_paths[j - 1]);
            j--;
        }
        strcpy(gallery_paths[j], path);
    }
    for (i = 0; i < count; i++)
        rasterfall_model_load(&gallery_models[i], gallery_paths[i]);
    gallery_loaded = 1;
}

static struct rasterfall_model_asset *gallery_model_named(const char *name,
                                                          const char *fallback)
{
    int i;
    gallery_load();
    for (i = 0; i < RASTERFALL_MODEL_MAX_GALLERY; i++) {
        if (!gallery_models[i].data) continue;
        if (!strcmp(gallery_paths[i], name) ||
            (fallback && !strcmp(gallery_paths[i], fallback)))
            return &gallery_models[i];
    }
    return NULL;
}

/* Only the three imported GLBs share the extracted diffuse palette.  The
 * older RFM2 assets keep their original material-color gallery appearance. */
static int gallery_model_has_texture(const struct rasterfall_model_asset *model)
{
    int i;
    if (!model || !active_model_texture || !active_model_texture->data)
        return 0;
    for (i = 0; i < RASTERFALL_MODEL_MAX_GALLERY; i++) {
        if (model != &gallery_models[i]) continue;
        return !strcmp(gallery_paths[i],
                       "rasterfall/assets/models/axe.rmesh") ||
               !strcmp(gallery_paths[i],
                       "rasterfall/assets/models/bomb.rmesh") ||
               !strcmp(gallery_paths[i],
                       "rasterfall/assets/models/molotov.rmesh");
    }
    return 0;
}

static int pickup_model_scale(const struct rasterfall_model_asset *model)
{
    int width = model->max_x - model->min_x;
    int height = model->max_y - model->min_y;
    int depth = model->max_z - model->min_z;
    int size = width > height ? width : height;
    if (depth > size) size = depth;
    if (size <= 0) return 1;
    return 380000 / size > 1 ? 380000 / size : 1;
}

static int gallery_model_scale(const struct rasterfall_model_asset *model)
{
    int width = model->max_x - model->min_x;
    int height = model->max_y - model->min_y;
    int scale = 1000;
    if (width > 0) scale = 520000 / width;
    if (height > 0 && 350000 / height < scale) scale = 350000 / height;
    if (scale < 1) scale = 1;
    return scale;
}

static void gallery_vertex(const struct rasterfall_model_asset *model,
                           unsigned int index, int center_x, int base_y,
                           int center_z, int scale, struct vec3 *out)
{
    const unsigned char *p = model->vertices +
                             index * model->vertex_bytes;
    out->x = center_x + (int)((long)*(const int *)(p) * scale / 1000);
    out->y = base_y + (int)((long)(*(const int *)(p + 4) - model->min_y) * scale / 1000);
    out->z = center_z + (int)((long)*(const int *)(p + 8) * scale / 1000);
}

static void gallery_edge_vertex(const struct rasterfall_model_asset *model,
                                unsigned int index, int center_x, int base_y,
                                int center_z, int scale, int edge_size,
                                struct vec3 *out)
{
    const unsigned char *p = model->vertices + index * model->vertex_bytes;
    gallery_vertex(model, index, center_x, base_y, center_z, scale, out);
    out->x += (int)((long long)*(const short *)(p + 12) * edge_size * scale /
                        (32767LL * 1000));
    out->y += (int)((long long)*(const short *)(p + 14) * edge_size * scale /
                        (32767LL * 1000));
    out->z += (int)((long long)*(const short *)(p + 16) * edge_size * scale /
                        (32767LL * 1000));
}

static void gallery_uv_vertex(const struct rasterfall_model_asset *model,
                              const struct camera *camera,
                              unsigned int index, int center_x, int base_y,
                              int center_z, int scale,
                              int sphere_mode,
                              struct world_uv_vertex *out)
{
    const unsigned char *p = model->vertices +
                             index * model->vertex_bytes;
    gallery_vertex(model, index, center_x, base_y, center_z, scale, &out->p);
    out->u = *(const unsigned short *)(p + 18);
    out->v = *(const unsigned short *)(p + 20);
    if (sphere_mode == 3 && model->vertex_bytes >=
                            RASTERFALL_MODEL_VERTEX_BYTES_ADDITIONAL_UV) {
        out->su = *(const int *)(p + 24);
        out->sv = *(const int *)(p + 28);
    } else if (sphere_mode == 1 || sphere_mode == 2) {
        int nx = *(const short *)(p + 12);
        int ny = *(const short *)(p + 14);
        int nz = *(const short *)(p + 16);
        int view_nx = (nx * camera->cy - nz * camera->sy) / 1024;
        int view_nz = (nx * camera->sy + nz * camera->cy) / 1024;
        int view_ny = (ny * camera->pitch_cy - view_nz * camera->pitch_sy) / 1024;
        long long sphere_length = isqrt((long long)view_nx * view_nx +
                                        (long long)view_ny * view_ny +
                                        (long long)(view_nz + 32767) *
                                        (view_nz + 32767));
        if (sphere_length > 0) {
            out->su = 32768 + (int)((long long)view_nx * 32768 / sphere_length);
            /* Texture images use a top-down V axis. */
            out->sv = 32768 - (int)((long long)view_ny * 32768 / sphere_length);
        } else {
            out->su = 32768;
            out->sv = 32768;
        }
        if (out->su < 0) out->su = 0;
        if (out->su > 65535) out->su = 65535;
        if (out->sv < 0) out->sv = 0;
        if (out->sv > 65535) out->sv = 65535;
    } else {
        out->su = 0;
        out->sv = 0;
    }
}

static int render_gallery_model(struct toy_renderer *renderer,
                                const struct camera *camera,
                                const struct rasterfall_model_asset *model,
                                int center_x, int base_y, int center_z,
                                int scale)
{
    int drawn = 0, i;
    const struct toy_texture_view *previous_texture = active_texture_view;
    const struct toy_texture_view *previous_sphere = active_sphere_texture;
    const struct toy_texture_view *previous_toon = active_toon_texture;
    int previous_sphere_mode = active_sphere_mode;
    int previous_toon_shared = active_toon_shared;
    int previous_toon_level = active_toon_level;
    int previous_material_alpha = active_material_alpha;
    int previous_material_double_sided = active_material_double_sided;
    uint32_t previous_material_ambient = active_material_ambient;
    uint32_t previous_material_specular = active_material_specular;
    int previous_material_specular_power = active_material_specular_power;
    int previous_material_specular_level = active_material_specular_level;
    int shared_texture = gallery_model_has_texture(model);
    active_sphere_texture = 0;
    active_sphere_mode = 0;
    active_toon_texture = 0;
    active_toon_shared = -1;
    if (shared_texture)
        active_texture_view = active_model_texture;
    for (i = 0; i < (int)model->primitive_count; i++) {
        const unsigned char *primitive = model->primitives + i * RASTERFALL_MODEL_PRIMITIVE_BYTES;
        const unsigned char *indices = model->indices + model_u32(primitive) * 4;
        unsigned int index_count = model_u32(primitive + 4);
        unsigned int material = model_u32(primitive + 8);
        const struct toy_texture_view *texture = 0;
        const struct toy_texture_view *sphere_texture = 0;
        const struct toy_texture_view *toon_texture = 0;
        active_texture_view = 0;
        active_sphere_texture = 0;
        active_sphere_mode = 0;
        active_toon_texture = 0;
        active_toon_shared = -1;
        active_material_alpha = material < model->material_count &&
            model->format_version >= 4 ?
            model->materials[material * model->material_bytes + 4] : 255;
        active_material_double_sided = material < model->material_count &&
            model->format_version >= 7 ?
            (model->materials[material * model->material_bytes + 7] & 1) != 0 : 1;
        active_material_ambient = !active_disable_material_light &&
            material < model->material_count &&
            model->format_version >= 9 ?
            model_u32(model->materials + material * model->material_bytes + 24) : 0;
        active_material_specular = !active_disable_material_light &&
            material < model->material_count &&
            model->format_version >= 9 ?
            model_u32(model->materials + material * model->material_bytes + 28) : 0;
        active_material_specular_power = material < model->material_count &&
            model->format_version >= 9 ?
            (int)model_u32(model->materials + material * model->material_bytes + 32) : 0;
        uint32_t color = material < model->material_count ?
                         model_u32(model->materials + material * model->material_bytes) :
                         RF_COLOR_UI_TEXT_MUTED;
        unsigned int j;
        if (!active_disable_edge && material < model->material_count &&
            model->format_version >= 8) {
            const unsigned char *material_data = model->materials +
                material * model->material_bytes;
            unsigned int edge = model_u32(material_data + 16);
            int edge_size = (int)model_u32(material_data + 20);
            if ((material_data[7] & 0x10) && edge_size > 0 && (edge >> 24)) {
                const struct toy_texture_view *saved_texture = active_texture_view;
                const struct toy_texture_view *saved_sphere = active_sphere_texture;
                const struct toy_texture_view *saved_toon = active_toon_texture;
                int saved_alpha = active_material_alpha;
                int saved_double_sided = active_material_double_sided;
                active_texture_view = 0;
                active_sphere_texture = 0;
                active_toon_texture = 0;
                active_material_alpha = (int)(edge >> 24);
                active_material_double_sided = 0;
                for (j = 0; j + 2 < index_count; j += 3) {
                    unsigned int ia = model_u32(indices + j * 4);
                    unsigned int ib = model_u32(indices + (j + 1) * 4);
                    unsigned int ic = model_u32(indices + (j + 2) * 4);
                    struct vec3 a, b, c;
                    if (ia >= model->vertex_count || ib >= model->vertex_count ||
                        ic >= model->vertex_count) continue;
                    gallery_edge_vertex(model, ia, center_x, base_y, center_z,
                                        scale, edge_size, &a);
                    gallery_edge_vertex(model, ic, center_x, base_y, center_z,
                                        scale, edge_size, &b);
                    gallery_edge_vertex(model, ib, center_x, base_y, center_z,
                                        scale, edge_size, &c);
                    drawn += draw_world_triangle(renderer, camera, &a, &b, &c,
                                                 edge & 0xffffffU);
                }
                active_texture_view = saved_texture;
                active_sphere_texture = saved_sphere;
                active_toon_texture = saved_toon;
                active_material_alpha = saved_alpha;
                active_material_double_sided = saved_double_sided;
            }
        }
        if (material < model->material_count && model->texture_assets) {
            unsigned int texture_index = model_u32(model->materials + material * model->material_bytes + 8);
            if (texture_index < model->texture_count && model->texture_assets[texture_index].data) {
                texture = &model->texture_views[texture_index];
                active_texture_view = texture;
            }
            if (model->format_version >= 3) {
                unsigned int packed = model_u32(model->materials + material * model->material_bytes + 12);
                unsigned int sphere_index = packed & 0xffffU;
                unsigned int sphere_mode = (packed >> 16) & 3;
                /* PMX modes 1/2 are multiplicative/additive environment maps.
                 * Mode 0 disables sphere mapping; mode 3 uses the first
                 * additional UV channel retained by RFM2 v6 and newer. */
                if (!active_disable_sphere &&
                    (sphere_mode == 1 || sphere_mode == 2 ||
                     (sphere_mode == 3 && model->vertex_bytes >=
                      RASTERFALL_MODEL_VERTEX_BYTES_ADDITIONAL_UV)) &&
                    sphere_index != 0xffffU && sphere_index < model->texture_count &&
                    model->texture_assets[sphere_index].data) {
                    sphere_texture = &model->texture_views[sphere_index];
                    active_sphere_texture = sphere_texture;
                    active_sphere_mode = sphere_mode;
                } else active_sphere_texture = 0;
            }
            if (model->format_version >= 5 && !active_disable_toon) {
                unsigned int toon_index = model->materials[material * model->material_bytes + 5];
                unsigned int toon_kind = model->materials[material * model->material_bytes + 6];
                if (toon_kind == 1 && toon_index < model->texture_count &&
                    model->texture_assets[toon_index].data) {
                    toon_texture = &model->texture_views[toon_index];
                    active_toon_texture = toon_texture;
                } else if (toon_kind == 2) active_toon_shared = (int)toon_index;
            }
        }
        if (!texture && shared_texture) active_texture_view = active_model_texture;
        for (j = 0; j + 2 < index_count; j += 3) {
            unsigned int ia = model_u32(indices + j * 4);
            unsigned int ib = model_u32(indices + (j + 1) * 4);
            unsigned int ic = model_u32(indices + (j + 2) * 4);
            struct vec3 a, b, c;
            struct world_uv_vertex ta, tb, tc;
            if (ia >= model->vertex_count || ib >= model->vertex_count || ic >= model->vertex_count) continue;
            {
                const unsigned char *na = model->vertices + ia * model->vertex_bytes + 12;
                const unsigned char *nb = model->vertices + ib * model->vertex_bytes + 12;
                const unsigned char *nc = model->vertices + ic * model->vertex_bytes + 12;
                int nx = *(const short *)na + *(const short *)nb + *(const short *)nc;
                int ny = *(const short *)(na + 2) + *(const short *)(nb + 2) + *(const short *)(nc + 2);
                int nz = *(const short *)(na + 4) + *(const short *)(nb + 4) + *(const short *)(nc + 4);
                int dot = (-nx + ny * 2 - nz) / 12;
                active_toon_level = 160 + dot * 95 / 32767;
                if (active_toon_level < 0) active_toon_level = 0;
                if (active_toon_level > 255) active_toon_level = 255;
                active_material_specular_level = dot > 0 ? dot * 255 / 32767 : 0;
                if (active_material_specular_power > 512)
                    active_material_specular_level = active_material_specular_level *
                        active_material_specular_level / 255;
                if (active_material_specular_power > 1024)
                    active_material_specular_level = active_material_specular_level *
                        active_material_specular_level / 255;
                if (active_material_specular_power > 2048)
                    active_material_specular_level = active_material_specular_level *
                        active_material_specular_level / 255;
                if (active_material_specular_power > 4096)
                    active_material_specular_level = active_material_specular_level *
                        active_material_specular_level / 255;
            }
            gallery_vertex(model, ia, center_x, base_y, center_z, scale, &a);
            gallery_vertex(model, ib, center_x, base_y, center_z, scale, &b);
            gallery_vertex(model, ic, center_x, base_y, center_z, scale, &c);
            if (texture || shared_texture) {
                gallery_uv_vertex(model, camera, ia, center_x, base_y, center_z, scale,
                                  active_sphere_texture ? active_sphere_mode : 0, &ta);
                gallery_uv_vertex(model, camera, ib, center_x, base_y, center_z, scale,
                                  active_sphere_texture ? active_sphere_mode : 0, &tb);
                gallery_uv_vertex(model, camera, ic, center_x, base_y, center_z, scale,
                                  active_sphere_texture ? active_sphere_mode : 0, &tc);
                drawn += draw_world_triangle_tex(renderer, camera, &ta, &tb, &tc);
            } else {
                drawn += draw_world_triangle(renderer, camera, &a, &b, &c, color);
            }
        }
    }
    active_texture_view = previous_texture;
    active_sphere_texture = previous_sphere;
    active_sphere_mode = previous_sphere_mode;
    active_toon_texture = previous_toon;
    active_toon_shared = previous_toon_shared;
    active_toon_level = previous_toon_level;
    active_material_alpha = previous_material_alpha;
    active_material_double_sided = previous_material_double_sided;
    active_material_ambient = previous_material_ambient;
    active_material_specular = previous_material_specular;
    active_material_specular_power = previous_material_specular_power;
    active_material_specular_level = previous_material_specular_level;
    return drawn;
}

int rasterfall_render_model_preview(struct toy_renderer *renderer,
                                    const struct camera *camera,
                                    const struct rasterfall_model_asset *model,
                                    int use_sphere, int use_toon, int use_edge,
                                    int use_material_light)
{
    int width, height, depth, size, scale;
    int center_x, center_z, base_y, pixels;
    if (!renderer || !camera || !model || !model->data) return -1;
    width = model->max_x - model->min_x;
    height = model->max_y - model->min_y;
    depth = model->max_z - model->min_z;
    size = width > height ? width : height;
    if (depth > size) size = depth;
    if (size <= 0) return -1;

    /* Fit every model into the same 900-unit inspection volume and center
     * its bounds at the origin.  A fixed camera distance then makes views
     * directly comparable across future imported character models. */
    scale = 900000 / size;
    if (scale < 1) scale = 1;
    center_x = -(int)((long)(model->min_x + model->max_x) * scale / 2000);
    center_z = -(int)((long)(model->min_z + model->max_z) * scale / 2000);
    base_y = -(int)((long)height * scale / 2000);
    active_gallery_lighting = 1;
    active_disable_sphere = !use_sphere;
    active_disable_toon = !use_toon;
    active_disable_edge = !use_edge;
    active_disable_material_light = !use_material_light;
    pixels = render_gallery_model(renderer, camera, model, center_x, base_y,
                                  center_z, scale);
    active_disable_sphere = 0;
    active_disable_toon = 0;
    active_disable_edge = 0;
    active_disable_material_light = 0;
    active_gallery_lighting = 0;
    return pixels;
}

/* Static developer display: nine models per row, in front of the north
 * wall. These assets are visual-only; they never enter map collision or AI. */
static int render_model_gallery(struct toy_renderer *renderer,
                                const struct camera *camera)
{
    int i, pixels = 0;
    gallery_load();
    active_gallery_lighting = 1;
    for (i = 0; i < RASTERFALL_MODEL_MAX_GALLERY; i++) {
        struct rasterfall_model_asset *model = &gallery_models[i];
        int row = i / RASTERFALL_GALLERY_COLUMNS;
        int column = i % RASTERFALL_GALLERY_COLUMNS;
        int width, height, scale, x, base_y;
        if (!model->data) continue;
        width = model->max_x - model->min_x;
        height = model->max_y - model->min_y;
        if (width <= 0 || height <= 0) continue;
        scale = gallery_model_scale(model);
        x = -5200 + column * RASTERFALL_GALLERY_SLOT_X;
        base_y = RASTERFALL_GALLERY_BASE_Y - row * RASTERFALL_GALLERY_ROW_Y;
        pixels += render_gallery_model(renderer, camera, model, x, base_y,
                                       -13200, scale);
    }
    active_gallery_lighting = 0;
    return pixels;
}

/* Local-only copyright-restricted character sample.  It is deliberately
 * outside the public asset gallery and is never embedded into release builds. */
static int render_private_character(struct toy_renderer *renderer,
                                    const struct camera *camera)
{
    const char path[] = "rasterfall/private-assets/models/yola.rmesh";
    int scale;
    if (!private_character_loaded) {
        rasterfall_model_load(&private_character_model, path);
        private_character_loaded = 1;
    }
    if (!private_character_model.data ||
        world_distance(camera, -13000, -10000) > ENEMY_RENDER_DISTANCE)
        return 0;
    scale = gallery_model_scale(&private_character_model) * 3;
    if (scale < 1) scale = 1;
    return render_gallery_model(renderer, camera, &private_character_model,
                                -13000, -900, -10000, scale);
}

/* The gallery is a fixed developer display rather than an interactable map
 * object.  Treat the model nearest the crosshair as the selected entry so
 * its source asset can be inventoried without adding gameplay interaction. */
static int gallery_selected_model(const struct toy_surface *surface,
                                  const struct camera *camera)
{
    int i, selected = -1, best_distance = 41 * 41;
    if (!surface || !camera) return -1;
    gallery_load();
    for (i = 0; i < RASTERFALL_MODEL_MAX_GALLERY; i++) {
        struct rasterfall_model_asset *model = &gallery_models[i];
        struct vec3 center, view;
        struct toy_screen_vertex screen;
        int row, column, scale, width, height, dx, dy, distance;
        if (!model->data) continue;
        width = model->max_x - model->min_x;
        height = model->max_y - model->min_y;
        if (width <= 0 || height <= 0) continue;
        row = i / RASTERFALL_GALLERY_COLUMNS;
        column = i % RASTERFALL_GALLERY_COLUMNS;
        scale = gallery_model_scale(model);
        center.x = -5200 + column * RASTERFALL_GALLERY_SLOT_X;
        center.y = RASTERFALL_GALLERY_BASE_Y - row * RASTERFALL_GALLERY_ROW_Y + height * scale / 2000;
        center.z = -13200;
        world_to_view(camera, &center, &view);
        if (view.z < NEAR_Z) continue;
        project_vertex(surface, &view, &screen);
        dx = screen.x - surface->width / 2;
        dy = screen.y - surface->height / 2;
        distance = dx * dx + dy * dy;
        if (distance < best_distance) {
            best_distance = distance;
            selected = i;
        }
    }
    return selected;
}

static void render_gallery_selection(struct toy_surface *surface,
                                     const struct camera *camera)
{
    int selected, width, x;
    char line[96];
    if (!surface || !surface->pixels) return;
    selected = gallery_selected_model(surface, camera);
    if (selected < 0) return;
    snprintf(line, sizeof(line), "MODEL PATH: %s", gallery_paths[selected]);
    width = (int)strlen(line) * FB_FONT_W;
    x = (surface->width - width) / 2;
    if (x < 8) x = 8;
    fill_rect(surface, x - 8, 10, width + 16, FB_FONT_H + 10,
              RF_COLOR_UI_BACKGROUND);
    fill_rect(surface, x - 8, 10, width + 16, 2,
              RF_COLOR_UI_ACCENT);
    fb_draw_string((unsigned char *)surface->pixels, x, 15, line,
                   RF_COLOR_UI_ACCENT_BRIGHT, surface->stride);
}

static void bake_static_lightmap(void)
{
    int x, z, i;
    for (z = 0; z < BAKED_LM_H; z++) for (x = 0; x < BAKED_LM_W; x++) {
        int wx = level_map.minx + (level_map.maxx - level_map.minx) * (x * 2 + 1) /
                 (BAKED_LM_W * 2);
        int wz = level_map.minz + (level_map.maxz - level_map.minz) * (z * 2 + 1) /
                 (BAKED_LM_H * 2);
        int light = 270 + (wx - level_map.minx) * 4 /
                    (level_map.maxx - level_map.minx ? level_map.maxx - level_map.minx : 1);
        for (i = 0; i < level_map.box_count; i++) {
            const struct toy_map_box *b = &level_map.boxes[i];
            int dx = wx < b->minx ? b->minx - wx : wx > b->maxx ? wx - b->maxx : 0;
            int dz = wz < b->minz ? b->minz - wz : wz > b->maxz ? wz - b->maxz : 0;
            int dist = dx > dz ? dx : dz;
            if (!b->air && dist < 900)
                light -= (900 - dist) * 24 / 900;
        }
        /* Warm point light baked from the small lamp in the east corner. */
        {
            int ldx = wx - 4000, ldz = wz - 160;
            int ldist = (int)isqrt((long long)ldx * ldx +
                                   (long long)ldz * ldz);
            if (ldist < 2600)
                light += (2600 - ldist) * 26 / 2600;
        }
        if (light < 150) light = 150;
        if (light > 286) light = 286;
        baked_lightmap[z * BAKED_LM_W + x] = (unsigned short)light;
    }
}

static int baked_light_at(int x, int z)
{
    int ix = (x - level_map.minx) * BAKED_LM_W /
             (level_map.maxx - level_map.minx ? level_map.maxx - level_map.minx : 1);
    int iz = (z - level_map.minz) * BAKED_LM_H /
             (level_map.maxz - level_map.minz ? level_map.maxz - level_map.minz : 1);
    if (ix < 0) ix = 0;
    if (ix >= BAKED_LM_W) ix = BAKED_LM_W - 1;
    if (iz < 0) iz = 0;
    if (iz >= BAKED_LM_H) iz = BAKED_LM_H - 1;
    return baked_lightmap[iz * BAKED_LM_W + ix];
}

static int baked_fog_at(int distance)
{
    if (distance <= 12000) return 0;
    if (distance >= 24000) return 210;
    return (distance - 12000) * 210 / 12000;
}

static int world_distance(const struct camera *camera, int x, int z)
{
    long long dx = (long long)x - camera->x;
    long long dz = (long long)z - camera->z;
    return (int)isqrt(dx * dx + dz * dz);
}

/* Map floor paint is an authored area colour, not a lightmap preview. */
static void put_pixel(struct toy_surface *surface, int x, int y, uint32_t color)
{
    uint32_t *row = (uint32_t *)((unsigned char *)surface->pixels +
                                 y * surface->stride);
    row[x] = color;
}

static void copy_vec3(struct vec3 *out, const struct vec3 *in)
{
    out->x = in->x; out->y = in->y; out->z = in->z;
}

/* 偏航（绕 Y 轴）后再做俯仰（绕 X 轴）的真实 3D 旋转：
 * pitch 为正 = 上仰，视平线以下的点投影到屏幕下方。 */
static void world_to_view(const struct camera *camera, const struct vec3 *world,
                          struct vec3 *view)
{
    int dx = world->x - camera->x;
    int dz = world->z - camera->z;
    int wx = (dx * camera->cy - dz * camera->sy) / 1024;
    int wz = (dx * camera->sy + dz * camera->cy) / 1024;
    view->x = wx;
    {
        int dy = world->y - camera->y;
        view->y = (dy * camera->pitch_cy - wz * camera->pitch_sy) / 1024;
        view->z = (dy * camera->pitch_sy + wz * camera->pitch_cy) / 1024;
    }
}

static void near_intersection(const struct vec3 *a, const struct vec3 *b,
                              struct vec3 *out)
{
    long numerator = NEAR_Z - a->z;
    long denominator = b->z - a->z;
    /* Toyc-safe widening: cast operands before subtraction so a negative
     * signed-int delta is sign-extended instead of zero-extended. */
    out->x = a->x + (int)(((long)b->x - (long)a->x) *
                          numerator / denominator);
    out->y = a->y + (int)(((long)b->y - (long)a->y) *
                          numerator / denominator);
    out->z = NEAR_Z;
}

static int clip_near(const struct vec3 *input, int count, struct vec3 *output)
{
    int out_count = 0;
    struct vec3 previous;
    copy_vec3(&previous, &input[count - 1]);
    int previous_inside = previous.z >= NEAR_Z;
    for (int i = 0; i < count; i++) {
        struct vec3 current;
        copy_vec3(&current, &input[i]);
        int current_inside = current.z >= NEAR_Z;
        if (current_inside != previous_inside)
            near_intersection(&previous, &current, &output[out_count++]);
        if (current_inside) copy_vec3(&output[out_count++], &current);
        copy_vec3(&previous, &current);
        previous_inside = current_inside;
    }
    return out_count;
}

static void copy_world_uv(struct world_uv_vertex *out,
                          const struct world_uv_vertex *in)
{
    copy_vec3(&out->p, &in->p);
    out->u = in->u; out->v = in->v;
    out->su = in->su; out->sv = in->sv;
}

static void near_intersection_uv(const struct world_uv_vertex *a,
                                 const struct world_uv_vertex *b,
                                 struct world_uv_vertex *out)
{
    long numerator = NEAR_Z - a->p.z;
    long denominator = b->p.z - a->p.z;
    near_intersection(&a->p, &b->p, &out->p);
    out->u = a->u + (int)(((long)b->u - a->u) * numerator / denominator);
    out->v = a->v + (int)(((long)b->v - a->v) * numerator / denominator);
    out->su = a->su + (int)(((long)b->su - a->su) * numerator / denominator);
    out->sv = a->sv + (int)(((long)b->sv - a->sv) * numerator / denominator);
}

static int clip_near_uv(const struct world_uv_vertex *input, int count,
                        struct world_uv_vertex *output)
{
    int out_count = 0;
    struct world_uv_vertex previous;
    copy_world_uv(&previous, &input[count - 1]);
    int previous_inside = previous.p.z >= NEAR_Z;
    for (int i = 0; i < count; i++) {
        struct world_uv_vertex current;
        copy_world_uv(&current, &input[i]);
        int current_inside = current.p.z >= NEAR_Z;
        if (current_inside != previous_inside)
            near_intersection_uv(&previous, &current, &output[out_count++]);
        if (current_inside) copy_world_uv(&output[out_count++], &current);
        copy_world_uv(&previous, &current);
        previous_inside = current_inside;
    }
    return out_count;
}

static void project_vertex(const struct toy_surface *surface,
                           const struct vec3 *view,
                           struct toy_screen_vertex *screen)
{
    int focal = surface->width * 3 / 4;
    int z = view->z < NEAR_Z ? NEAR_Z : view->z;
    screen->x = surface->width / 2 +
                (int)((long)view->x * focal / z);
    screen->y = surface->height / 2 -
                (int)((long)view->y * focal / z);
    screen->z = z;
    /* 渲染器深度缓冲使用逆深度，纯色路径也读该字段；必须在投影时填好。 */
    screen->inv_z = (long)1048576 / z;
    screen->light = 256;
    screen->fog = 0;
}

static void project_uv_vertex(const struct toy_surface *surface,
                              const struct vec3 *view,
                              int u, int v,
                              int su, int sv,
                              struct toy_screen_vertex *screen)
{
    int z = view->z < NEAR_Z ? NEAR_Z : view->z;
    project_vertex(surface, view, screen);
    screen->u = u; screen->v = v;
    screen->inv_z = (long)1048576 / z;
    screen->u_over_z = (long)u * 1048576L / z;
    screen->v_over_z = (long)v * 1048576L / z;
    screen->u2 = su; screen->v2 = sv;
    screen->u2_over_z = (long)su * 1048576L / z;
    screen->v2_over_z = (long)sv * 1048576L / z;
}

static int draw_world_triangle(struct toy_renderer *renderer,
                               const struct camera *camera,
                               const struct vec3 *a, const struct vec3 *b,
                               const struct vec3 *c,
                               uint32_t color)
{
    struct vec3 input[3], clipped[4];
    int count, drawn = 0;
    world_to_view(camera, a, &input[0]);
    world_to_view(camera, b, &input[1]);
    world_to_view(camera, c, &input[2]);
    count = clip_near(input, 3, clipped);
    for (int i = 1; i + 1 < count; i++) {
        struct toy_screen_vertex sa, sb, sc;
        long long area;
        project_vertex(&renderer->surface, &clipped[0], &sa);
        project_vertex(&renderer->surface, &clipped[i], &sb);
        project_vertex(&renderer->surface, &clipped[i + 1], &sc);
        /* Projected coordinates are bounded here, so 32-bit area is safe and
         * avoids Toyc's signed int-to-long promotion bug. */
        area = ((long long)sc.x - sa.x) * ((long long)sb.y - sa.y) -
               ((long long)sc.y - sa.y) * ((long long)sb.x - sa.x);
        if (area >= 0) {
            struct toy_screen_vertex swap;
            if (!active_material_double_sided) continue;
            swap.x = sb.x; swap.y = sb.y; swap.z = sb.z;
            swap.inv_z = sb.inv_z;
            sb.x = sc.x; sb.y = sc.y; sb.z = sc.z;
            sb.inv_z = sc.inv_z;
            sc.x = swap.x; sc.y = swap.y; sc.z = swap.z;
            sc.inv_z = swap.inv_z;
        }
        int center_x = (a->x + b->x + c->x) / 3;
        int center_z = (a->z + b->z + c->z) / 3;
        int light = active_emissive_projectile ? 384 :
                    active_gallery_lighting ? 256 :
                    fixed_floor_lighting ? 256 : baked_light_at(center_x, center_z);
        int fog = active_emissive_projectile ? 0 :
                  active_gallery_lighting ? 0 : fixed_floor_lighting ? 0 :
                  baked_fog_at(world_distance(camera, center_x, center_z));
        /* 区域涂色（fixed_floor_lighting）与地砖仅差 6 个世界单位，掠射角下
         * 插值深度误差会盖过真实差值导致 z-fight；涂色按覆盖层绘制，
         * 依赖"地砖先画、墙后画"的记录顺序保证遮挡正确。 */
        if (fixed_floor_lighting)
            drawn += toy_renderer_triangle_lit(renderer, &sa, &sb, &sc,
                                               color, light, fog);
        else
            drawn += toy_renderer_triangle_lit(renderer, &sa, &sb, &sc,
                                               color, light, fog);
    }
    return drawn;
}

static int draw_world_triangle_tex(struct toy_renderer *renderer,
                                    const struct camera *camera,
                                    const struct world_uv_vertex *a,
                                    const struct world_uv_vertex *b,
                                    const struct world_uv_vertex *c)
{
    struct world_uv_vertex input[3], clipped[4];
    int count, drawn = 0;
    world_to_view(camera, &a->p, &input[0].p);
    world_to_view(camera, &b->p, &input[1].p);
    world_to_view(camera, &c->p, &input[2].p);
    input[0].u = a->u; input[0].v = a->v;
    input[1].u = b->u; input[1].v = b->v;
    input[2].u = c->u; input[2].v = c->v;
    input[0].su = a->su; input[0].sv = a->sv;
    input[1].su = b->su; input[1].sv = b->sv;
    input[2].su = c->su; input[2].sv = c->sv;
    count = clip_near_uv(input, 3, clipped);
    for (int i = 1; i + 1 < count; i++) {
        struct toy_screen_vertex sa, sb, sc;
        long long area;
        project_uv_vertex(&renderer->surface, &clipped[0].p,
                          clipped[0].u, clipped[0].v,
                          clipped[0].su, clipped[0].sv, &sa);
        project_uv_vertex(&renderer->surface, &clipped[i].p,
                          clipped[i].u, clipped[i].v,
                          clipped[i].su, clipped[i].sv, &sb);
        project_uv_vertex(&renderer->surface, &clipped[i + 1].p,
                          clipped[i + 1].u, clipped[i + 1].v,
                          clipped[i + 1].su, clipped[i + 1].sv, &sc);
        area = ((long long)sc.x - sa.x) * ((long long)sb.y - sa.y) -
               ((long long)sc.y - sa.y) * ((long long)sb.x - sa.x);
        if (area >= 0) {
            struct toy_screen_vertex swap;
            if (!active_material_double_sided) continue;
            swap.x=sb.x; swap.y=sb.y; swap.z=sb.z;
            swap.u=sb.u; swap.v=sb.v; swap.inv_z=sb.inv_z;
            swap.u_over_z=sb.u_over_z; swap.v_over_z=sb.v_over_z;
            swap.u2=sb.u2; swap.v2=sb.v2; swap.u2_over_z=sb.u2_over_z; swap.v2_over_z=sb.v2_over_z;
            sb.x=sc.x; sb.y=sc.y; sb.z=sc.z;
            sb.u=sc.u; sb.v=sc.v; sb.inv_z=sc.inv_z;
            sb.u_over_z=sc.u_over_z; sb.v_over_z=sc.v_over_z;
            sb.u2=sc.u2; sb.v2=sc.v2; sb.u2_over_z=sc.u2_over_z; sb.v2_over_z=sc.v2_over_z;
            sc.x=swap.x; sc.y=swap.y; sc.z=swap.z;
            sc.u=swap.u; sc.v=swap.v; sc.inv_z=swap.inv_z;
            sc.u_over_z=swap.u_over_z; sc.v_over_z=swap.v_over_z;
            sc.u2=swap.u2; sc.v2=swap.v2; sc.u2_over_z=swap.u2_over_z; sc.v2_over_z=swap.v2_over_z;
        }
        int center_x = (a->p.x + b->p.x + c->p.x) / 3;
        int center_z = (a->p.z + b->p.z + c->p.z) / 3;
        int light = active_gallery_lighting ? 256 :
                    fixed_floor_lighting ? 256 : baked_light_at(center_x, center_z);
        int fog = active_gallery_lighting ? 0 :
                  fixed_floor_lighting ? 0 : baked_fog_at(world_distance(camera, center_x, center_z));
        sa.light = sb.light = sc.light = light;
        sa.fog = sb.fog = sc.fog = fog;
        if (active_sphere_texture || active_toon_texture ||
            active_toon_shared >= 0 || active_material_ambient ||
            active_material_specular)
            drawn += toy_renderer_triangle_textured_material_lit(
                renderer, &sa, &sb, &sc, active_texture_view,
                active_sphere_texture, active_sphere_mode,
                active_toon_texture, active_toon_shared, active_toon_level,
                active_material_alpha, active_material_ambient,
                active_material_specular, active_material_specular_level,
                1, 0xFF202020U, light, fog);
        else
            drawn += toy_renderer_triangle_textured_lit(renderer, &sa, &sb, &sc,
                                                         active_texture_view, 1,
                                                         0xFF202020U, light, fog);
    }
    return drawn;
}

static int draw_quad_tex(struct toy_renderer *renderer,
                         const struct camera *camera,
                         const struct world_uv_vertex *a,
                         const struct world_uv_vertex *b,
                         const struct world_uv_vertex *c,
                         const struct world_uv_vertex *d,
                         uint32_t color)
{
    if (!textures_enabled)
        return draw_world_triangle(renderer, camera, &a->p, &b->p, &c->p, color) +
               draw_world_triangle(renderer, camera, &a->p, &c->p, &d->p, color);
    return draw_world_triangle_tex(renderer, camera, a, b, c) +
           draw_world_triangle_tex(renderer, camera, a, c, d);
}

static int draw_position_quad_tex(struct toy_renderer *renderer,
                                  const struct camera *camera,
                                  const struct vec3 *a, const struct vec3 *b,
                                  const struct vec3 *c, const struct vec3 *d,
                                  int u1, int v1, uint32_t color)
{
    struct world_uv_vertex wa, wb, wc, wd;
    copy_vec3(&wa.p, a); copy_vec3(&wb.p, b);
    copy_vec3(&wc.p, c); copy_vec3(&wd.p, d);
    wa.u=0; wa.v=0; wb.u=u1; wb.v=0;
    wc.u=u1; wc.v=v1; wd.u=0; wd.v=v1;
    return draw_quad_tex(renderer, camera, &wa, &wb, &wc, &wd, color);
}

static int draw_quad(struct toy_renderer *renderer, const struct camera *camera,
                     const struct vec3 *a, const struct vec3 *b,
                     const struct vec3 *c, const struct vec3 *d,
                     uint32_t color)
{
    return draw_world_triangle(renderer, camera, a, b, c, color) +
           draw_world_triangle(renderer, camera, a, c, d, color);
}

static int draw_floor_zone(struct toy_renderer *renderer,
                           const struct camera *camera,
                           const struct toy_game_box *zone, uint32_t color)
{
    struct vec3 a, b, c, d;
    a.x = zone->minx; a.y = -900; a.z = zone->minz;
    b.x = zone->maxx; b.y = -900; b.z = zone->minz;
    c.x = zone->maxx; c.y = -900; c.z = zone->maxz;
    d.x = zone->minx; d.y = -900; d.z = zone->maxz;
    return draw_quad(renderer, camera, &a, &b, &c, &d, color);
}

#define FLOOR_SPLIT_MAX 48

static void floor_split_add(int *values, int *count, int value,
                            int min_value, int max_value)
{
    int i;
    if (value <= min_value || value >= max_value) return;
    for (i = 0; i < *count; i++)
        if (values[i] == value) return;
    if (*count >= FLOOR_SPLIT_MAX) return;
    values[(*count)++] = value;
}

static void floor_split_sort(int *values, int count)
{
    int i, j, value;
    for (i = 1; i < count; i++) {
        value = values[i];
        j = i - 1;
        while (j >= 0 && values[j] > value) {
            values[j + 1] = values[j];
            j--;
        }
        values[j + 1] = value;
    }
}

static int floor_contains(const struct toy_game_box *box, int x, int z)
{
    return x >= box->minx && x < box->maxx &&
           z >= box->minz && z < box->maxz;
}

static int floor_draw_contains(const struct toy_map_draw *draw, int x, int z)
{
    struct toy_game_box outer;
    outer.minx = draw->a; outer.maxx = draw->b;
    outer.minz = draw->c; outer.maxz = draw->d;
    if (!floor_contains(&outer, x, z)) return 0;
    if (draw->type != TOY_MAP_DRAW_BORDER || draw->e <= 0) return 1;
    return x < draw->a + draw->e || x >= draw->b - draw->e ||
           z < draw->c + draw->e || z >= draw->d - draw->e;
}

/* Render the checkerboard and authored floor colours as one tessellated
 * plane.  A colour region changes the colour of the affected sub-rectangles;
 * it never creates a second, nearly coplanar surface. */
static int draw_partitioned_floor(struct toy_renderer *renderer,
                                  const struct camera *camera)
{
    int base_x, base_z, i, j, k, pixels = 0;
    int xs[FLOOR_SPLIT_MAX], zs[FLOOR_SPLIT_MAX];
    for (base_z = level_map.minz; base_z < level_map.maxz; base_z += 1000) {
        for (base_x = level_map.minx; base_x < level_map.maxx; base_x += 1000) {
            int x_count = 0, z_count = 0;
            xs[x_count++] = base_x;
            xs[x_count++] = base_x + 1000;
            zs[z_count++] = base_z;
            zs[z_count++] = base_z + 1000;
            for (i = 0; i < level_map.draw_count; i++) {
                struct toy_map_draw *draw = &level_map.draw[i];
                if (draw->type != TOY_MAP_DRAW_FLOOR &&
                    draw->type != TOY_MAP_DRAW_BORDER) continue;
                if (draw->b <= base_x || draw->a >= base_x + 1000 ||
                    draw->d <= base_z || draw->c >= base_z + 1000) continue;
                floor_split_add(xs, &x_count, draw->a, base_x, base_x + 1000);
                floor_split_add(xs, &x_count, draw->b, base_x, base_x + 1000);
                floor_split_add(zs, &z_count, draw->c, base_z, base_z + 1000);
                floor_split_add(zs, &z_count, draw->d, base_z, base_z + 1000);
                if (draw->type == TOY_MAP_DRAW_BORDER) {
                    floor_split_add(xs, &x_count, draw->a + draw->e,
                                    base_x, base_x + 1000);
                    floor_split_add(xs, &x_count, draw->b - draw->e,
                                    base_x, base_x + 1000);
                    floor_split_add(zs, &z_count, draw->c + draw->e,
                                    base_z, base_z + 1000);
                    floor_split_add(zs, &z_count, draw->d - draw->e,
                                    base_z, base_z + 1000);
                }
            }
            for (i = 0; i < level_map.spawn_count; i++) {
                struct toy_game_box *spawn = &level_map.spawn_zones[i].box;
                if (spawn->maxx <= base_x || spawn->minx >= base_x + 1000 ||
                    spawn->maxz <= base_z || spawn->minz >= base_z + 1000) continue;
                floor_split_add(xs, &x_count, spawn->minx, base_x, base_x + 1000);
                floor_split_add(xs, &x_count, spawn->maxx, base_x, base_x + 1000);
                floor_split_add(zs, &z_count, spawn->minz, base_z, base_z + 1000);
                floor_split_add(zs, &z_count, spawn->maxz, base_z, base_z + 1000);
            }
            floor_split_sort(xs, x_count);
            floor_split_sort(zs, z_count);
            for (i = 0; i + 1 < x_count; i++) {
                for (j = 0; j + 1 < z_count; j++) {
                    int minx = xs[i], maxx = xs[i + 1];
                    int minz = zs[j], maxz = zs[j + 1];
                    int center_x = (minx + maxx) / 2;
                    int center_z = (minz + maxz) / 2;
                    int has_spawn = 0;
                    uint32_t base_color = (((base_x + base_z) / 1000) & 1) ?
                                           0x30343A : 0x272B31;
                    uint32_t color = base_color;
                    /* Spawn zones are the strongest authored region. */
                    for (k = 0; k < level_map.spawn_count; k++) {
                        struct toy_map_zone *spawn = &level_map.spawn_zones[k];
                        if (floor_contains(&spawn->box, center_x, center_z)) {
                            color = spawn->color;
                            has_spawn = 1;
                            break;
                        }
                    }
                    if (!has_spawn) {
                        for (k = 0; k < level_map.draw_count; k++) {
                            struct toy_map_draw *draw = &level_map.draw[k];
                            if (draw->type != TOY_MAP_DRAW_FLOOR &&
                                draw->type != TOY_MAP_DRAW_BORDER) continue;
                            if (floor_draw_contains(draw, center_x, center_z)) {
                                color = draw->color;
                                break;
                            }
                        }
                    }
                    {
                        struct toy_game_box patch;
                        patch.minx = minx; patch.maxx = maxx;
                        patch.minz = minz; patch.maxz = maxz;
                        pixels += draw_floor_zone(renderer, camera, &patch, color);
                    }
                }
            }
        }
    }
    return pixels;
}

static void draw_world_label(struct toy_renderer *renderer,
                             const struct camera *camera,
                             const struct toy_game_box *zone,
                             const char *label, uint32_t color)
{
    struct vec3 world, view;
    struct toy_screen_vertex screen;
    int width = (int)strlen(label) * FB_FONT_W;
    world.x = (zone->minx + zone->maxx) / 2;
    world.y = -650;
    world.z = (zone->minz + zone->maxz) / 2;
    world_to_view(camera, &world, &view);
    if (view.z < NEAR_Z) return;
    project_vertex(&renderer->surface, &view, &screen);
    screen.x -= width / 2;
    if (screen.x < 0 || screen.x + width >= renderer->surface.width ||
        screen.y < 0 || screen.y + FB_FONT_H >= renderer->surface.height) return;
    fb_draw_string((unsigned char *)renderer->surface.pixels,
                   screen.x, screen.y, label, color, renderer->surface.stride);
}

static void draw_coordinate_label(struct toy_surface *surface,
                                  const struct camera *camera,
                                  const struct vec3 *world,
                                  const char *label, uint32_t color)
{
    struct vec3 view;
    struct toy_screen_vertex screen;
    int width;
    if (!surface || !surface->pixels || !camera || !world || !label) return;
    world_to_view(camera, world, &view);
    if (view.z < NEAR_Z) return;
    project_vertex(surface, &view, &screen);
    width = (int)strlen(label) * FB_FONT_W;
    screen.x -= width / 2;
    if (screen.x < 2 || screen.x + width >= surface->width - 2 ||
        screen.y < 2 || screen.y + FB_FONT_H >= surface->height - 2) return;
    /* A small shadow keeps labels readable over the authored floor. */
    fb_draw_string((unsigned char *)surface->pixels, screen.x + 1,
                   screen.y + 1, label, 0x10151D, surface->stride);
    fb_draw_string((unsigned char *)surface->pixels, screen.x, screen.y,
                   label, color, surface->stride);
}

/* Labels are drawn after the world flush so the ruler remains readable even
 * when its thin ticks are over a similarly coloured floor tile. */
void rasterfall_render_coordinate_labels(struct toy_surface *surface,
                                         const struct camera *camera)
{
    int i;
    char label[32];
    struct vec3 world;
    if (!surface || !camera) return;

    for (i = level_map.minx / RULER_STEP * RULER_STEP;
         i <= level_map.maxx; i += RULER_STEP) {
        snprintf(label, sizeof(label), "X=%d", i);
        world.x = i; world.y = -790; world.z = -100;
        draw_coordinate_label(surface, camera, &world, label, 0xFFB0B0);
    }
    for (i = level_map.minz / RULER_STEP * RULER_STEP;
         i <= level_map.maxz; i += RULER_STEP) {
        snprintf(label, sizeof(label), "Z=%d", i);
        world.x = -100; world.y = -790; world.z = i;
        draw_coordinate_label(surface, camera, &world, label, 0xB0D8FF);
    }
    for (i = -2000; i <= 5000; i += RULER_STEP) {
        snprintf(label, sizeof(label), "Y=%d", i);
        world.x = 90; world.y = i; world.z = 0;
        draw_coordinate_label(surface, camera, &world, label, 0xB0FFB8);
    }
    world.x = 120; world.y = 0; world.z = 0;
    draw_coordinate_label(surface, camera, &world, "ORIGIN (0,0,0)",
                          0xFFF0B0);
    world.x = 120; world.y = RULER_GROUND_Y; world.z = 0;
    draw_coordinate_label(surface, camera, &world, "CROSS Y=-900",
                          0xFFF0B0);
}

static int draw_cuboid(struct toy_renderer *renderer,
                       const struct camera *camera,
                       int x0, int x1, int y0, int y1,
                       int z0, int z1, uint32_t color);

static int render_world_sign(struct toy_renderer *renderer,
                             const struct camera *camera,
                             const struct toy_map_draw *sign)
{
    int x = (sign->a + sign->b) / 2;
    int z = (sign->c + sign->d) / 2;
    int pixels = 0;
    pixels += draw_cuboid(renderer, camera, x - 18, x + 18,
                          sign->e - 220, sign->e, z - 18, z + 18, 0x4B3526);
    pixels += draw_cuboid(renderer, camera, sign->a, sign->b,
                          sign->e, sign->f, sign->c, sign->d, sign->color);
    /* The label is drawn after the world geometry has flushed.  Drawing it
     * here writes directly to the framebuffer while the board is still in
     * the command buffer, so the board overwrites the text. */
    return pixels;
}

/* Submit bitmap-font pixels as tiny world-space quads.  This deliberately
 * uses the existing depth-tested flat-triangle path: the text is part of the
 * board/flag plane instead of a framebuffer billboard. */
static int render_world_text_plane(struct toy_renderer *renderer,
                                   const struct camera *camera,
                                   int x, int y, int z,
                                   int width, int height,
                                   const char *text, uint32_t color,
                                   int mirror)
{
    int chars, cell, start_x, start_y, i, row, col, run, px, total_px;
    int drawn = 0;
    struct vec3 a, b, c, d;
    if (!renderer || !camera || !text || !text[0]) return 0;
    chars = (int)strlen(text);
    if (chars <= 0) return 0;
    cell = width / (chars * FB_FONT_W);
    if (height / FB_FONT_H < cell) cell = height / FB_FONT_H;
    if (cell < 1) return 0;
    start_x = x + (width - chars * FB_FONT_W * cell) / 2;
    start_y = y - (height - FB_FONT_H * cell) / 2;
    total_px = chars * FB_FONT_W;
    for (i = 0; i < chars; i++) {
        unsigned char ch = (unsigned char)text[i];
        for (row = 0; row < FB_FONT_H; row++) {
            unsigned char bits = fb_font_glyph_row(ch, row);
            for (col = 0; col < FB_FONT_W; col++) {
                if (!(bits & (unsigned char)(0x80 >> col))) continue;
                run = col;
                while (run + 1 < FB_FONT_W &&
                       (bits & (unsigned char)(0x80 >> (run + 1)))) run++;
                /* The original screen-space implementation happened to
                 * display these world planes with a reversed horizontal
                 * orientation.  Keep the correction explicit here; the
                 * reverse-side copy uses the opposite orientation naturally
                 * when viewed from behind the plane. */
                if (mirror)
                    px = total_px - (i * FB_FONT_W + run + 1);
                else
                    px = i * FB_FONT_W + col;
                a.x = start_x + px * cell;
                a.y = start_y - row * cell;
                a.z = z;
                if (mirror)
                    b.x = start_x +
                          (total_px - (i * FB_FONT_W + col)) * cell;
                else
                    b.x = start_x + (i * FB_FONT_W + run + 1) * cell;
                b.y = a.y; b.z = z;
                c.x = b.x; c.y = a.y - cell; c.z = z;
                d.x = a.x; d.y = c.y; d.z = z;
                drawn += draw_world_triangle(renderer, camera, &a, &b, &c, color);
                drawn += draw_world_triangle(renderer, camera, &a, &c, &d, color);
                col = run;
            }
        }
    }
    return drawn;
}

static int render_block_enemy(struct toy_renderer *, const struct camera *,
                              const struct toy_game_enemy *, int, uint32_t);
static int render_round_enemy(struct toy_renderer *, const struct camera *,
                              const struct toy_game_enemy *, int, uint32_t);
static int render_smoker_enemy(struct toy_renderer *, const struct camera *,
                               const struct toy_game_enemy *, int, uint32_t);
static int render_charger_enemy(struct toy_renderer *, const struct camera *,
                                const struct toy_game_enemy *, int, uint32_t);
static int render_tank_enemy(struct toy_renderer *, const struct camera *,
                             const struct toy_game_enemy *, int, uint32_t);

static int draw_box(struct toy_renderer *renderer, const struct camera *camera,
                    const struct box *box)
{
    struct vec3 a, b, c, d, e, f, g, h;
    int pixels = 0;
    a.x = box->minx; a.y = -900; a.z = box->minz;
    b.x = box->maxx; b.y = -900; b.z = box->minz;
    c.x = box->maxx; c.y = -900; c.z = box->maxz;
    d.x = box->minx; d.y = -900; d.z = box->maxz;
    e.x = a.x; e.y = box->height; e.z = a.z;
    f.x = b.x; f.y = box->height; f.z = b.z;
    g.x = c.x; g.y = box->height; g.z = c.z;
    h.x = d.x; h.y = box->height; h.z = d.z;
    pixels += draw_quad(renderer,camera,&a,&b,&f,&e,box->color);
    pixels += draw_quad(renderer,camera,&b,&c,&g,&f,box->color + 0x080808);
    pixels += draw_quad(renderer,camera,&c,&d,&h,&g,box->color);
    pixels += draw_quad(renderer,camera,&d,&a,&e,&h,box->color + 0x080808);
    pixels += draw_quad(renderer,camera,&e,&f,&g,&h,box->color + 0x181818);
    return pixels;
}

/* 敌人模型使用任意底面高度的盒体，而场景障碍物仍走上面的落地盒体。 */
static int draw_cuboid(struct toy_renderer *renderer, const struct camera *camera,
                       int minx, int maxx, int miny, int maxy,
                       int minz, int maxz, uint32_t color)
{
    struct vec3 a, b, c, d, e, f, g, h;
    int pixels = 0;
    a.x = minx; a.y = miny; a.z = minz;
    b.x = maxx; b.y = miny; b.z = minz;
    c.x = maxx; c.y = miny; c.z = maxz;
    d.x = minx; d.y = miny; d.z = maxz;
    e.x = minx; e.y = maxy; e.z = minz;
    f.x = maxx; f.y = maxy; f.z = minz;
    g.x = maxx; g.y = maxy; g.z = maxz;
    h.x = minx; h.y = maxy; h.z = maxz;
    pixels += draw_quad(renderer, camera, &a, &b, &f, &e, color);
    pixels += draw_quad(renderer, camera, &b, &c, &g, &f, color + 0x080808);
    pixels += draw_quad(renderer, camera, &c, &d, &h, &g, color);
    pixels += draw_quad(renderer, camera, &d, &a, &e, &h, color + 0x080808);
    pixels += draw_quad(renderer, camera, &e, &f, &g, &h, color + 0x181818);
    return pixels;
}

    /* Developer coordinate ruler.  Keep it in world space so it follows the
     * map, is depth-tested against walls, and remains useful while spectating.
     * All three axes intersect at the world origin (0,0,0). */
static int render_coordinate_ruler(struct toy_renderer *renderer,
                                   const struct camera *camera)
{
    int i, pixels = 0;
    const uint32_t x_color = 0xE06060;
    const uint32_t y_color = 0x60D080;
    const uint32_t z_color = 0x60A8E0;

    if (!renderer || !camera) return 0;

    /* Main X and Z axes sit on the authored map floor for a readable visual
     * reference.  The vertical Y axis still carries the true world heights. */
    pixels += draw_cuboid(renderer, camera, level_map.minx, level_map.maxx,
                          RULER_GROUND_Y - RULER_AXIS_HALF,
                          RULER_GROUND_Y + RULER_AXIS_HALF,
                          -RULER_AXIS_HALF, RULER_AXIS_HALF, x_color);
    pixels += draw_cuboid(renderer, camera, -RULER_AXIS_HALF, RULER_AXIS_HALF,
                          RULER_GROUND_Y - RULER_AXIS_HALF,
                          RULER_GROUND_Y + RULER_AXIS_HALF,
                          level_map.minz, level_map.maxz, z_color);

    /* Y is vertical and deliberately extends above the normal map geometry. */
    pixels += draw_cuboid(renderer, camera, -RULER_AXIS_HALF, RULER_AXIS_HALF,
                          -2000, 5000, -RULER_AXIS_HALF, RULER_AXIS_HALF,
                          y_color);

    for (i = level_map.minx / RULER_STEP * RULER_STEP;
         i <= level_map.maxx; i += RULER_STEP)
        pixels += draw_cuboid(renderer, camera, i - RULER_TICK_HALF,
                              i + RULER_TICK_HALF,
                              RULER_GROUND_Y - RULER_TICK_HALF,
                              RULER_GROUND_Y + RULER_TICK_HALF, -70, 70,
                              x_color);
    for (i = level_map.minz / RULER_STEP * RULER_STEP;
         i <= level_map.maxz; i += RULER_STEP)
        pixels += draw_cuboid(renderer, camera, -70, 70,
                              RULER_GROUND_Y - RULER_TICK_HALF,
                              RULER_GROUND_Y + RULER_TICK_HALF,
                              i - RULER_TICK_HALF,
                              i + RULER_TICK_HALF, z_color);
    for (i = -2000; i <= 5000; i += RULER_STEP)
        pixels += draw_cuboid(renderer, camera, -70, 70, i - RULER_TICK_HALF,
                              i + RULER_TICK_HALF, -70, 70, y_color);
    return pixels;
}

static int render_flag_one(struct toy_renderer *renderer,
                           const struct camera *camera,
                           const struct rasterfall_flag *flag, int selected)
{
    int pixels = 0;
    uint32_t cloth = (uint32_t)flag->color;
    if (selected) cloth += 0x202020;
    /* 3600 world units is approximately four player heights in Rasterfall. */
    pixels += draw_cuboid(renderer, camera, flag->x - 16, flag->x + 16,
                          -900, 2700, flag->z - 16, flag->z + 16, 0x5A6470);
    /* Keep the cloth's vertical band compact; the extra size is horizontal. */
    pixels += draw_cuboid(renderer, camera, flag->x + 12, flag->x + 700,
                          1950, 2450, flag->z - 10, flag->z + 10, cloth);
    return pixels;
}

int rasterfall_render_flags(struct toy_renderer *renderer,
                            const struct camera *camera)
{
    int i, pixels = 0;
    if (!active_session) return 0;
    for (i = 0; i < active_session->flag_count; i++)
        if (active_session->flags[i].active)
            pixels += render_flag_one(renderer, camera, &active_session->flags[i],
                                      i == active_session->carried_flag);
    return pixels;
}

int rasterfall_render_flag_text(struct toy_renderer *renderer,
                                const struct camera *camera)
{
    int i, drawn = 0;
    if (!renderer || !active_session) return 0;
    for (i = 0; i < active_session->flag_count; i++) {
        const struct rasterfall_flag *f = &active_session->flags[i];
        if (!f->active || !f->label[0]) continue;
        /* The flag cloth spans x+12..x+700 and y=1950..2450.  Its visible
         * face is +Z, matching the sign convention below. */
        drawn += render_world_text_plane(renderer, camera,
                                          f->x + 32, 2418, f->z + 14,
                                          640, 430, f->label, 0xFFF0C0, 1);
        drawn += render_world_text_plane(renderer, camera,
                                          f->x + 32, 2418, f->z - 14,
                                          640, 430, f->label, 0xFFF0C0, 0);
    }
    return drawn;
}

static void actor_world_point(int x, int z, int sy, int cy,
                              int lx, int ly, int lz, struct vec3 *out)
{
    int ry = -900 + ((ly + 900) * active_actor_roll_cos +
                     lz * active_actor_roll_sin) / 1024;
    int rz = (-(ly + 900) * active_actor_roll_sin +
              lz * active_actor_roll_cos) / 1024;
    out->x = x + (cy * lx + sy * rz) / 1024;
    out->y = ry + active_actor_lift;
    out->z = z + (-sy * lx + cy * rz) / 1024;
}

/* 角色局部坐标的立体盒：local X 为角色右侧，local Z 为面朝方向。
 * 与 draw_cuboid 不同，它的八个顶点会随 actor 的 sy/cy 一起旋转。 */
static int draw_actor_box(struct toy_renderer *renderer,
                          const struct camera *camera, int x, int z,
                          int sy, int cy, int x0, int x1,
                          int y0, int y1, int z0, int z1,
                          uint32_t color)
{
    struct vec3 a, b, c, d, e, f, g, h;
    int pixels = 0;
    actor_world_point(x,z,sy,cy,x0,y0,z0,&a);
    actor_world_point(x,z,sy,cy,x1,y0,z0,&b);
    actor_world_point(x,z,sy,cy,x1,y0,z1,&c);
    actor_world_point(x,z,sy,cy,x0,y0,z1,&d);
    actor_world_point(x,z,sy,cy,x0,y1,z0,&e);
    actor_world_point(x,z,sy,cy,x1,y1,z0,&f);
    actor_world_point(x,z,sy,cy,x1,y1,z1,&g);
    actor_world_point(x,z,sy,cy,x0,y1,z1,&h);
    pixels += draw_quad(renderer,camera,&a,&b,&f,&e,color);
    pixels += draw_quad(renderer,camera,&b,&c,&g,&f,color+0x080808);
    pixels += draw_quad(renderer,camera,&c,&d,&h,&g,color);
    pixels += draw_quad(renderer,camera,&d,&a,&e,&h,color+0x080808);
    pixels += draw_quad(renderer,camera,&e,&f,&g,&h,color+0x181818);
    pixels += draw_quad(renderer,camera,&a,&b,&c,&d,color-0x0C0C0C);
    return pixels;
}

/* Small local-Z shear used for the hit reaction.  It is intentionally a
 * simple box approximation rather than a new skeletal/body system. */
static int draw_actor_tilted_box(struct toy_renderer *renderer,
                                 const struct camera *camera, int x, int z,
                                 int sy, int cy, int x0, int x1,
                                 int y0, int y1, int z0, int z1,
                                 int pitch, uint32_t color)
{
    int pivot_y = -620;
    int tilted_z0 = z0 + (y0 - pivot_y) * pitch / 1024;
    int tilted_z1 = z1 + (y1 - pivot_y) * pitch / 1024;
    return draw_actor_box(renderer, camera, x, z, sy, cy,
                          x0, x1, y0, y1, tilted_z0, tilted_z1, color);
}

static const int circle_x[8] = {1024, 724, 0, -724, -1024, -724, 0, 724};
static const int circle_z[8] = {0, 724, 1024, 724, 0, -724, -1024, -724};

static int draw_cylinder(struct toy_renderer *renderer,
                         const struct camera *camera, int x, int z,
                         int radius, int bottom, int top, uint32_t color)
{
    int pixels = 0;
    struct vec3 lo[8], hi[8], cap;
    cap.x = x; cap.y = top; cap.z = z;
    for (int i = 0; i < 8; i++) {
        lo[i].x = x + circle_x[i] * radius / 1024;
        lo[i].y = bottom;
        lo[i].z = z + circle_z[i] * radius / 1024;
        hi[i].x = lo[i].x; hi[i].y = top; hi[i].z = lo[i].z;
    }
    for (int i = 0; i < 8; i++) {
        int next = (i + 1) & 7;
        uint32_t shade = color + ((i & 3) * 0x030303);
        pixels += draw_quad(renderer, camera, &lo[i], &lo[next],
                            &hi[next], &hi[i], shade);
        pixels += draw_world_triangle(renderer, camera, &cap,
                                      &hi[i], &hi[next], color + 0x181818);
    }
    return pixels;
}

static int draw_tongue_segment(struct toy_renderer *renderer,
                               const struct camera *camera,
                               int x0, int y0, int z0,
                               int x1, int y1, int z1)
{
    long dx = x1 - x0, dz = z1 - z0;
    long len = isqrt(dx * dx + dz * dz);
    int width = 18;
    struct vec3 a, b, c, d;
    if (len <= 0) return 0;
    a.x = x0 - (int)(dz * width / len); a.y = y0; a.z = z0 + (int)(dx * width / len);
    b.x = x0 + (int)(dz * width / len); b.y = y0; b.z = z0 - (int)(dx * width / len);
    c.x = x1 + (int)(dz * width / len); c.y = y1; c.z = z1 - (int)(dx * width / len);
    d.x = x1 - (int)(dz * width / len); d.y = y1; d.z = z1 + (int)(dx * width / len);
    return draw_quad(renderer, camera, &a, &b, &c, &d, 0xB98B62);
}

/* 三圈八边面组成的低多边形椭圆头。 */
static int draw_ellipsoid_head(struct toy_renderer *renderer,
                               const struct camera *camera, int x, int z,
                               int center_y, int rx, int ry, uint32_t color)
{
    static const int ring_r[5] = {0, 724, 1024, 724, 0};
    static const int ring_y[5] = {-1024, -724, 0, 724, 1024};
    struct vec3 ring[5][8];
    int pixels = 0;
    for (int r = 0; r < 5; r++) {
        for (int i = 0; i < 8; i++) {
            ring[r][i].x = x + circle_x[i] * rx * ring_r[r] / 1048576;
            ring[r][i].y = center_y + ring_y[r] * ry / 1024;
            ring[r][i].z = z + circle_z[i] * rx * ring_r[r] / 1048576;
        }
    }
    for (int r = 0; r < 4; r++) {
        for (int i = 0; i < 8; i++) {
            int next = (i + 1) & 7;
            pixels += draw_quad(renderer, camera, &ring[r][i], &ring[r][next],
                                &ring[r + 1][next], &ring[r + 1][i],
                                color + ((i & 3) * 0x030303));
        }
    }
    return pixels;
}

/* Avatar heads use the same local frame as the torso.  Keeping the head in
 * that frame is important during the fall: an upright world-space ellipse
 * otherwise looks like a long cylinder sliding through the body. */
static int draw_actor_ellipsoid_head(struct toy_renderer *renderer,
                                     const struct camera *camera, int x, int z,
                                     int sy, int cy, int center_y,
                                     int rx, int ry, uint32_t color)
{
    static const int ring_r[5] = {0, 724, 1024, 724, 0};
    static const int ring_y[5] = {-1024, -724, 0, 724, 1024};
    struct vec3 ring[5][8];
    int r, i, pixels = 0;
    for (r = 0; r < 5; r++) {
        for (i = 0; i < 8; i++) {
            int lx = circle_x[i] * rx * ring_r[r] / 1048576;
            int ly = center_y + ring_y[r] * ry / 1024;
            int lz = circle_z[i] * rx * ring_r[r] / 1048576;
            actor_world_point(x, z, sy, cy, lx, ly, lz, &ring[r][i]);
        }
    }
    for (r = 0; r < 4; r++) {
        for (i = 0; i < 8; i++) {
            int next = (i + 1) & 7;
            pixels += draw_quad(renderer, camera, &ring[r][i],
                                &ring[r][next], &ring[r + 1][next],
                                &ring[r + 1][i], color + ((i & 3) * 0x030303));
        }
    }
    return pixels;
}

static int draw_actor_face_rect(struct toy_renderer *renderer,
                                const struct camera *camera, int x, int z,
                                int sy, int cy, int radius, int h0, int h1,
                                int y0, int y1, uint32_t color)
{
    struct vec3 a, b, c, d;
    actor_world_point(x, z, sy, cy, -h0, y0, radius + 3, &a);
    actor_world_point(x, z, sy, cy, -h1, y0, radius + 3, &b);
    actor_world_point(x, z, sy, cy, -h1, y1, radius + 3, &c);
    actor_world_point(x, z, sy, cy, -h0, y1, radius + 3, &d);
    return draw_quad(renderer, camera, &a, &b, &c, &d, color);
}

/* 沿敌人当前朝向在头部平面画小矩形，转身过程因此清晰可见。 */
static int draw_face_rect(struct toy_renderer *renderer,
                          const struct camera *camera, int x, int z, int radius,
                          int face_x, int face_z, int h0, int h1,
                          int y0, int y1, uint32_t color)
{
    struct vec3 a, b, c, d;
    int cx = x + face_x * (radius + 3) / 1024;
    int cz = z + face_z * (radius + 3) / 1024;
    a.x = cx - face_z * h0 / 1024; a.y = y0;
    a.z = cz + face_x * h0 / 1024;
    b.x = cx - face_z * h1 / 1024; b.y = y0;
    b.z = cz + face_x * h1 / 1024;
    c.x = b.x; c.y = y1; c.z = b.z;
    d.x = a.x; d.y = y1; d.z = a.z;
    return draw_quad(renderer, camera, &a, &b, &c, &d, color);
}

/* ── 可交互拾取物渲染：桌上武器与弹药盒（准星对准时整体提亮） ── */

static uint32_t highlight_tint(uint32_t color, int on)
{
    return on ? color + 0x383838 : color;
}

/* 平放的 SMG：机匣 + 枪管 + 弹匣 + 木托 + 照门，枪口朝 +z */
static int render_ammo_box(struct toy_renderer *renderer,
                           const struct camera *camera,
                           int x, int y, int z, int on);

static int render_smg(struct toy_renderer *renderer, const struct camera *camera,
                      int x, int y, int z, int on)
{
    struct rasterfall_model_asset *model = gallery_model_named(
        "rasterfall/assets/models/smg_mac10.rmesh", NULL);
    if (model)
        return render_gallery_model(renderer, camera, model, x, y, z,
                                     pickup_model_scale(model));
    int pixels = 0;
    pixels += draw_cuboid(renderer, camera, x - 35, x + 35, y - 13, y + 13,
                          z - 60, z + 60, highlight_tint(0x3B4148, on));
    pixels += draw_cuboid(renderer, camera, x - 14, x + 14, y - 2, y + 10,
                          z + 60, z + 150, highlight_tint(0x2F343B, on));
    pixels += draw_cuboid(renderer, camera, x - 20, x + 20, y - 48, y - 16,
                          z - 20, z + 10, highlight_tint(0x4A4438, on));
    pixels += draw_cuboid(renderer, camera, x - 24, x + 24, y - 9, y + 7,
                          z - 118, z - 60, highlight_tint(0x6B4A30, on));
    pixels += draw_cuboid(renderer, camera, x - 3, x + 3, y + 13, y + 21,
                          z - 15, z + 15, highlight_tint(0x2A2E34, on));
    return pixels;
}

static int render_rifle_pickup(struct toy_renderer *renderer,
                               const struct camera *camera,
                               int x, int y, int z, int on,
                               const char *path)
{
    struct rasterfall_model_asset *model = gallery_model_named(path, NULL);
    int scale;
    (void)on;
    if (!model) return render_ammo_box(renderer, camera, x, y, z, on);
    scale = pickup_model_scale(model) * 2;
    if (!strcmp(path, "rasterfall/assets/models/bomb.rmesh") ||
        !strcmp(path, "rasterfall/assets/models/molotov.rmesh"))
        scale = pickup_model_scale(model) / 2;
    if (scale < 1) scale = 1;
    return render_gallery_model(renderer, camera, model, x, y, z,
                                scale);
}

/* 平放的霰弹枪：机匣 + 长枪管 + 护木 + 木托，枪口朝 +z */
static int render_shotgun(struct toy_renderer *renderer, const struct camera *camera,
                          int x, int y, int z, int on)
{
    struct rasterfall_model_asset *model = gallery_model_named(
        "rasterfall/assets/models/sg_pump_action.rmesh",
        "rasterfall/assets/models/sg_punp_action.rmesh");
    if (model)
        return render_gallery_model(renderer, camera, model, x, y, z,
                                     pickup_model_scale(model) * 2);
    int pixels = 0;
    pixels += draw_cuboid(renderer, camera, x - 32, x + 32, y - 13, y + 13,
                          z - 45, z + 45, highlight_tint(0x46505A, on));
    pixels += draw_cuboid(renderer, camera, x - 11, x + 11, y - 2, y + 10,
                          z + 45, z + 150, highlight_tint(0x3A434D, on));
    pixels += draw_cuboid(renderer, camera, x - 17, x + 17, y - 19, y - 4,
                          z + 45, z + 100, highlight_tint(0x2C3138, on));
    pixels += draw_cuboid(renderer, camera, x - 24, x + 24, y - 8, y + 8,
                          z - 100, z - 45, highlight_tint(0x6B4A30, on));
    return pixels;
}

/* 弹药盒：橄榄绿箱体 + 浅色箱盖 */
static int render_ammo_box(struct toy_renderer *renderer, const struct camera *camera,
                           int x, int y, int z, int on)
{
    int pixels = 0;
    pixels += draw_cuboid(renderer, camera, x - 60, x + 60, y - 40, y + 40,
                          z - 80, z + 80, highlight_tint(0x555F3F, on));
    pixels += draw_cuboid(renderer, camera, x - 60, x + 60, y + 16, y + 40,
                          z - 80, z + 80, highlight_tint(0x6A7550, on));
    return pixels;
}

/* 召唤按钮：从墙面凸出的暗色面板 + 正面 LED。每次都可再次触发；
 * 准星对准时面板整体提亮。 */
static int render_button(struct toy_renderer *renderer, const struct camera *camera,
                         int x, int y, int z, int on, int side_wall)
{
    struct vec3 a, b, c, d;
    int pixels = 0;
    if (side_wall == 1) {
        pixels += draw_cuboid(renderer, camera, x - 55, x + 55, y - 55, y + 55,
                              z - 45, z + 55, highlight_tint(0x2E333B, on));
        pixels += draw_cuboid(renderer, camera, x + 55, x + 60, y - 45, y + 45,
                              z - 45, z + 45, highlight_tint(0x1E2229, on));
        /* 西侧墙按钮朝向房间（+x） */
        a.x = x + 61; a.y = y - 22; a.z = z - 24;
        b.x = x + 61; b.y = y + 22; b.z = z - 24;
        c.x = x + 61; c.y = y + 22; c.z = z + 24;
        d.x = x + 61; d.y = y - 22; d.z = z + 24;
    } else if (side_wall == 2) {
        pixels += draw_cuboid(renderer, camera, x - 55, x + 55, y - 55, y + 55,
                              z - 45, z + 55, highlight_tint(0x2E333B, on));
        pixels += draw_cuboid(renderer, camera, x - 60, x - 55, y - 45, y + 45,
                              z - 45, z + 45, highlight_tint(0x1E2229, on));
        /* 东侧墙按钮朝向房间（-x） */
        a.x = x - 61; a.y = y - 22; a.z = z + 24;
        b.x = x - 61; b.y = y + 22; b.z = z + 24;
        c.x = x - 61; c.y = y + 22; c.z = z - 24;
        d.x = x - 61; d.y = y - 22; d.z = z - 24;
    } else {
        pixels += draw_cuboid(renderer, camera, x - 55, x + 55, y - 55, y + 55,
                              z - 45, z + 55, highlight_tint(0x2E333B, on));
        pixels += draw_cuboid(renderer, camera, x - 45, x + 45, y - 45, y + 45,
                              z + 55, z + 60, highlight_tint(0x1E2229, on));
        /* 正面 LED 小方块，面向房间（+z） */
        a.x = x - 24; a.y = y - 22; a.z = z + 61;
        b.x = x + 24; b.y = y - 22; b.z = z + 61;
        c.x = x + 24; c.y = y + 22; c.z = z + 61;
        d.x = x - 24; d.y = y + 22; d.z = z + 61;
    }
    pixels += draw_quad(renderer, camera, &a, &b, &c, &d,
                        0xFF3030);
    return pixels;
}

/* 开发者区的特感测试按钮：矮圆柱底座和略低于视线的按键帽，便于连续
 * 添加新的特感而不再占用墙面按钮。 */
static int render_special_button(struct toy_renderer *renderer,
                                 const struct camera *camera,
                                 int x, int y, int z, int on, int special)
{
    uint32_t color = special == 2 ? 0x65713D :
                     special == 1 ? 0x9B5528 : 0x3E7462;
    int pixels = draw_cylinder(renderer, camera, x, z, 190, -900, y, color);
    pixels += draw_cuboid(renderer, camera, x - 125, x + 125,
                          y, y + 35, z - 125, z + 125,
                          highlight_tint(0x252B31, on));
    pixels += draw_cylinder(renderer, camera, x, z, 62, y + 35, y + 78,
                            on ? 0xFFE080 :
                            (special == 2 ? 0x91A54C :
                             special == 1 ? 0xD43A28 : 0x38CFA0));
    return pixels;
}

static int render_pill_pickup(struct toy_renderer *renderer,
                              const struct camera *camera,
                              int x, int y, int z, int on)
{
    int pixels = draw_cylinder(renderer, camera, x, z, 135, y, y + 250,
                               on ? 0xD8E8D8 : 0xB7C7B7);
    pixels += draw_cuboid(renderer, camera, x - 8, x + 8, y + 90, y + 160,
                          z - 142, z - 132, 0x20B84B);
    pixels += draw_cuboid(renderer, camera, x - 42, x + 42, y + 120, y + 135,
                          z - 142, z - 132, 0x20B84B);
    return pixels;
}

static int render_interactables(struct toy_renderer *renderer,
                                const struct camera *camera)
{
    int i, pixels = 0;
    for (i = 0; i < interactable_count; i++) {
        const interactable *it = &interactables[i];
        int on = i == highlighted;
        if (it->kind == TOY_MAP_PICKUP_SMG ||
            (it->kind == TOY_MAP_PICKUP_WEAPON &&
             it->weapon == TOY_GAME_WEAPON_SMG))
            pixels += render_smg(renderer, camera, it->x, it->y, it->z, on);
        else if (it->kind == TOY_MAP_PICKUP_SHOTGUN ||
                 (it->kind == TOY_MAP_PICKUP_WEAPON &&
                  it->weapon == TOY_GAME_WEAPON_SHOTGUN))
            pixels += render_shotgun(renderer, camera, it->x, it->y, it->z, on);
        else if (it->kind == TOY_MAP_PICKUP_WEAPON &&
                 it->weapon == TOY_GAME_WEAPON_AK)
            pixels += render_rifle_pickup(renderer, camera, it->x, it->y,
                                          it->z, on,
                                          "rasterfall/assets/models/ar_ak47.rmesh");
        else if (it->kind == TOY_MAP_PICKUP_WEAPON &&
                 it->weapon == TOY_GAME_WEAPON_AWP)
            pixels += render_rifle_pickup(renderer, camera, it->x, it->y,
                                          it->z, on,
                                          "rasterfall/assets/models/rf_AWP.rmesh");
        else if (it->kind == TOY_MAP_PICKUP_WEAPON &&
                 it->weapon == TOY_GAME_WEAPON_AXE)
            pixels += render_rifle_pickup(renderer, camera, it->x, it->y,
                                          it->z, on,
                                          "rasterfall/assets/models/axe.rmesh");
        else if (it->kind == TOY_MAP_PICKUP_THROWABLE)
            pixels += render_rifle_pickup(renderer, camera, it->x, it->y,
                                          it->z, on,
                                          it->weapon == TOY_GAME_WEAPON_BOMB ?
                                          "rasterfall/assets/models/bomb.rmesh" :
                                          "rasterfall/assets/models/molotov.rmesh");
        else if (it->kind == TOY_MAP_PICKUP_PILL)
            pixels += render_pill_pickup(renderer, camera, it->x, it->y,
                                         it->z, on);
        else if (it->kind == TOY_MAP_PICKUP_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_AIR_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_ALARM_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_HEAVY_HORDE_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_FAST_HORDE_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_BASE_1_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_BASE_2_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_WAVE_SKIP_BUTTON)
            pixels += render_button(renderer, camera, it->x, it->y, it->z, on,
                                    it->x < -10000 ? 1 : it->x > 10000 ? 2 : 0);
        else if (it->kind == TOY_MAP_PICKUP_SMOKER_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_CHARGER_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_TANK_BUTTON)
            pixels += render_special_button(renderer, camera, it->x, it->y,
                                             it->z, on,
                                             it->kind == TOY_MAP_PICKUP_TANK_BUTTON ? 2 :
                                             it->kind == TOY_MAP_PICKUP_CHARGER_BUTTON);
        else if (it->kind == TOY_MAP_PICKUP_ATTACK_X2_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_ATTACK_X3_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_ATTACK_X4_BUTTON)
            pixels += render_special_button(renderer, camera, it->x, it->y,
                                             it->z, on,
                                             it->kind == TOY_MAP_PICKUP_ATTACK_X4_BUTTON ? 2 :
                                             it->kind == TOY_MAP_PICKUP_ATTACK_X3_BUTTON);
        else if (it->kind == TOY_MAP_PICKUP_SHOP)
            pixels += render_button(renderer, camera, it->x, it->y, it->z, on, 2);
        else if (it->kind == TOY_MAP_PICKUP_MONEY_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_CLEAR_HIRED_BUTTON)
            pixels += render_special_button(renderer, camera, it->x, it->y,
                                             it->z, on,
                                             it->kind == TOY_MAP_PICKUP_CLEAR_HIRED_BUTTON);
        else
            pixels += render_ammo_box(renderer, camera, it->x, it->y, it->z, on);
    }
    return pixels;
}

/* Thrown items are world geometry rather than HUD sprites, so walls and
 * enemies can occlude them naturally through the normal depth buffer. */
static int render_projectiles(struct toy_renderer *renderer,
                              const struct camera *camera)
{
    const struct toy_texture_view *previous_texture = active_texture_view;
    int i, pixels = 0;
    /* World gallery rendering temporarily selects the model texture.  This
     * pass runs later, so select it explicitly before submitting projectile
     * triangles; otherwise the UV path would sample the wall texture. */
    if (active_model_texture && active_model_texture->data)
        active_texture_view = active_model_texture;
    for (i = 0; i < TOY_GAME_MAX_PROJECTILES; i++) {
        const struct toy_game_projectile *p = &game.projectiles[i];
        const char *path;
        struct rasterfall_model_asset *model;
        int width, height, depth, length, scale, j;
        if (!p->active) continue;
        active_emissive_projectile =
            p->flash_ms && p->kind == TOY_GAME_WEAPON_BOMB;
        path = p->kind == TOY_GAME_WEAPON_BOMB ?
               "rasterfall/assets/models/bomb.rmesh" :
               "rasterfall/assets/models/molotov.rmesh";
        model = gallery_model_named(path, NULL);
        if (!model) continue;
        width = model->max_x - model->min_x;
        height = model->max_y - model->min_y;
        depth = model->max_z - model->min_z;
        length = width > height ? width : height;
        if (depth > length) length = depth;
        scale = 240000 * TOY_CONFIG_THROW_MODEL_SCALE / 1000 /
                (length > 0 ? length : 1);
        if (scale < 1) scale = 1;
        for (j = 0; j < (int)model->primitive_count; j++) {
            const unsigned char *primitive = model->primitives +
                j * RASTERFALL_MODEL_PRIMITIVE_BYTES;
            const unsigned char *indices = model->indices +
                model_u32(primitive) * 4;
            unsigned int index_count = model_u32(primitive + 4);
            unsigned int material = model_u32(primitive + 8);
            uint32_t color = material < model->material_count ?
                model_u32(model->materials +
                          material * model->material_bytes) :
                RF_COLOR_UI_TEXT_MUTED;
            unsigned int k;
            for (k = 0; k + 2 < index_count; k += 3) {
                unsigned int ids[3] = { model_u32(indices + k * 4),
                                        model_u32(indices + (k + 1) * 4),
                                        model_u32(indices + (k + 2) * 4) };
                struct vec3 v[3];
                int n;
                for (n = 0; n < 3; n++) {
                    const unsigned char *q;
                    int x, y, z, rotated_x, rotated_z;
                    if (ids[n] >= model->vertex_count) break;
                    q = model->vertices + ids[n] * model->vertex_bytes;
                    x = (*(const int *)q - (model->min_x + model->max_x) / 2) * scale / 1000;
                    y = (*(const int *)(q + 4) - (model->min_y + model->max_y) / 2) * scale / 1000;
                    z = (*(const int *)(q + 8) - (model->min_z + model->max_z) / 2) * scale / 1000;
                    rotate_arm_xz(x, z, p->age_ms / 5,
                                  &rotated_x, &rotated_z);
                    v[n].x = p->x + rotated_x;
                    v[n].y = -900 + p->y + 120 + y;
                    v[n].z = p->z + rotated_z;
                }
                if (n == 3) {
                    if (gallery_model_has_texture(model) && !p->flash_ms &&
                        active_model_texture && active_model_texture->data) {
                        struct world_uv_vertex uv[3];
                        int m;
                        for (m = 0; m < 3; m++) {
                            const unsigned char *q = model->vertices +
                                ids[m] * model->vertex_bytes;
                            uv[m].p = v[m];
                            uv[m].u = *(const unsigned short *)(q + 18);
                            uv[m].v = *(const unsigned short *)(q + 20);
                        }
                        pixels += draw_world_triangle_tex(renderer, camera,
                                                          &uv[0], &uv[1],
                                                          &uv[2]);
                    } else {
                        if (p->flash_ms && p->kind == TOY_GAME_WEAPON_BOMB)
                            color = 0xFF0000;
                        pixels += draw_world_triangle(renderer, camera,
                                                      &v[0], &v[1], &v[2],
                                                      color);
                    }
                }
            }
        }
    }
    active_emissive_projectile = 0;
    active_texture_view = previous_texture;
    return pixels;
}

/* ── 第一人称武器模型：视图空间盒体固定在镜头右下方，开火后坐后移 ── */

static void fill_rect(struct toy_surface *surface, int x, int y,
                      int width, int height, uint32_t color);
static uint32_t mix_color(uint32_t from, uint32_t to, int num, int den);

static int render_scene(struct toy_renderer *renderer, const struct camera *camera)
{
    int pixels = 0;
    struct vec3 a, b, c, d;
    /* 自由俯仰下先铺天空/地面：地平线由俯仰角决定，墙面与地板随后覆盖 */
    rasterfall_sky_draw(&renderer->surface, camera);
    fixed_floor_lighting = 1;
    pixels += draw_partitioned_floor(renderer, camera);
    fixed_floor_lighting = 0;
    if (active_coordinate_axes)
        pixels += render_coordinate_ruler(renderer, camera);
    for (int i=0; i<level_map.draw_count; i++) {
        struct toy_map_draw *x=&level_map.draw[i];
        if (x->type==TOY_MAP_DRAW_FLOOR || x->type==TOY_MAP_DRAW_BORDER) {
            /* Floor colours are already part of the tessellated base plane.
             * Keeping a second floor command here would reintroduce the
             * coplanar depth competition this pass is designed to remove. */
        } else if (x->type==TOY_MAP_DRAW_WALL) {
            if (x->c==x->d) { a.x=x->a;a.y=-900;a.z=x->c;b.x=x->b;b.y=-900;b.z=x->c;c.x=x->b;c.y=x->e;c.z=x->c;d.x=x->a;d.y=x->e;d.z=x->c; }
            else { a.x=x->a;a.y=-900;a.z=x->c;b.x=x->a;b.y=-900;b.z=x->d;c.x=x->a;c.y=x->e;c.z=x->d;d.x=x->a;d.y=x->e;d.z=x->c; }
            pixels+=draw_quad(renderer,camera,&a,&b,&c,&d,x->color);
        } else if (x->type==TOY_MAP_DRAW_MODEL) {
            if (x->style) {
                struct toy_game_enemy model;
                memset(&model,0,sizeof(model)); model.active=1;
                model.x=(x->a+x->b)/2; model.z=(x->c+x->d)/2; model.dir_z=-1024;
                if (x->style==1) pixels+=render_block_enemy(renderer,camera,&model,1000,x->color);
                else if (x->style==2) pixels+=render_round_enemy(renderer,camera,&model,1000,x->color);
                else if (x->style==3) {
                    model.type = TOY_GAME_ENEMY_SMOKER;
                    pixels += render_smoker_enemy(renderer, camera, &model,
                                                  1000, x->color);
                } else if (x->style==4) {
                    model.type = TOY_GAME_ENEMY_CHARGER;
                    pixels += render_charger_enemy(renderer, camera, &model,
                                                   1120, x->color);
                } else if (x->style==5) {
                    model.type = TOY_GAME_ENEMY_TANK;
                    pixels += render_tank_enemy(renderer, camera, &model,
                                                1600, x->color);
                }
            } else {
                struct box model={x->a,x->b,x->c,x->d,x->f,x->color};
                pixels+=draw_cuboid(renderer,camera,model.minx,model.maxx,x->e,x->f,model.minz,model.maxz,model.color);
            }
        } else if (x->type==TOY_MAP_DRAW_TEXTURE) {
            a.x=x->a;a.y=-900;a.z=x->c;b.x=x->b;b.y=-900;b.z=x->c;c.x=x->b;c.y=x->e;c.z=x->c;d.x=x->a;d.y=x->e;d.z=x->c;
            pixels+=draw_position_quad_tex(renderer,camera,&a,&b,&c,&d,x->texture_u*UV_ONE,x->texture_v*UV_ONE,x->color);
        } else if (x->type==TOY_MAP_DRAW_LABEL) {
            struct toy_game_box zone={x->a,x->b,x->c,x->d}; draw_world_label(renderer,camera,&zone,x->text,x->color);
        } else if (x->type==TOY_MAP_DRAW_SIGN) {
            pixels += render_world_sign(renderer, camera, x);
        }
    }
    for (int i=0; i<level_map.box_count; i++) if (level_map.boxes[i].visible) {
        struct box obstacle={level_map.boxes[i].minx,level_map.boxes[i].maxx,level_map.boxes[i].minz,level_map.boxes[i].maxz,level_map.boxes[i].height,level_map.boxes[i].color};
        pixels+=draw_box(renderer,camera,&obstacle);
    }
    pixels += render_model_gallery(renderer, camera);
    pixels += render_private_character(renderer, camera);
    pixels += render_projectiles(renderer, camera);
    return pixels;
}
static int enemy_y(int y, int scale)
{
    return -900 + (y + 900) * scale / 1000 + active_enemy_lift;
}

/* 偶数槽位：方块人。分离的靴子、腿、躯干和头保持 Minecraft 式轮廓。 */
static int render_block_enemy(struct toy_renderer *renderer,
                              const struct camera *camera,
                              const struct toy_game_enemy *e,
                              int scale, uint32_t color)
{
    int x = e->x, z = e->z, pixels = 0;
    pixels += draw_cuboid(renderer, camera, x - 105, x - 15,
                          enemy_y(-900, scale), enemy_y(-760, scale),
                          z - 105, z + 105, RF_COLOR_AI_HEAVY);
    pixels += draw_cuboid(renderer, camera, x + 15, x + 105,
                          enemy_y(-900, scale), enemy_y(-760, scale),
                          z - 105, z + 105, RF_COLOR_AI_HEAVY);
    pixels += draw_cuboid(renderer, camera, x - 100, x - 10,
                          enemy_y(-760, scale), enemy_y(-450, scale),
                          z - 85, z + 85, color - 0x101008);
    pixels += draw_cuboid(renderer, camera, x + 10, x + 100,
                          enemy_y(-760, scale), enemy_y(-450, scale),
                          z - 85, z + 85, color - 0x101008);
    pixels += draw_cuboid(renderer, camera, x - 175, x + 175,
                          enemy_y(-470, scale), enemy_y(20, scale),
                          z - 105, z + 105, color);
    pixels += draw_cuboid(renderer, camera, x - 155, x + 155,
                          enemy_y(0, scale), enemy_y(320, scale),
                          z - 155, z + 155, color + 0x202010);
    /* 像素化愤怒脸：下压的双眉、亮眼和紧闭嘴。 */
    pixels += draw_face_rect(renderer, camera, x, z, 155, e->dir_x, e->dir_z,
                             -105, -18, enemy_y(210, scale),
                             enemy_y(235, scale), 0x4A1010);
    pixels += draw_face_rect(renderer, camera, x, z, 155, e->dir_x, e->dir_z,
                             18, 105, enemy_y(210, scale),
                             enemy_y(235, scale), 0x4A1010);
    pixels += draw_face_rect(renderer, camera, x, z, 155, e->dir_x, e->dir_z,
                             -82, -28, enemy_y(160, scale),
                             enemy_y(200, scale), 0xFFF0A0);
    pixels += draw_face_rect(renderer, camera, x, z, 155, e->dir_x, e->dir_z,
                             28, 82, enemy_y(160, scale),
                             enemy_y(200, scale), 0xFFF0A0);
    pixels += draw_face_rect(renderer, camera, x, z, 155, e->dir_x, e->dir_z,
                             -70, 70, enemy_y(80, scale),
                             enemy_y(105, scale), 0x4A1010);
    return pixels;
}

/* 奇数槽位：Madness 风格无臂人，圆柱躯干、椭圆头和厚底短靴。 */
static int render_round_enemy(struct toy_renderer *renderer,
                              const struct camera *camera,
                              const struct toy_game_enemy *e,
                              int scale, uint32_t color)
{
    int x = e->x, z = e->z, pixels = 0;
    pixels += draw_cuboid(renderer, camera, x - 145, x - 12,
                          enemy_y(-900, scale), enemy_y(-760, scale),
                          z - 180, z + 95, 0x202328);
    pixels += draw_cuboid(renderer, camera, x + 12, x + 145,
                          enemy_y(-900, scale), enemy_y(-760, scale),
                          z - 180, z + 95, 0x202328);
    pixels += draw_cylinder(renderer, camera, x, z, 180,
                            enemy_y(-770, scale), enemy_y(20, scale), color);
    pixels += draw_ellipsoid_head(renderer, camera, x, z,
                                  enemy_y(155, scale), 205,
                                  (enemy_y(350, scale) - enemy_y(-40, scale)) / 2,
                                  color + 0x181810);
    /* 普通敌人面部只有一个醒目的十字。 */
    pixels += draw_face_rect(renderer, camera, x, z, 205, e->dir_x, e->dir_z,
                             -25, 25, enemy_y(35, scale),
                             enemy_y(275, scale), 0x251F20);
    pixels += draw_face_rect(renderer, camera, x, z, 205, e->dir_x, e->dir_z,
                             -120, 120, enemy_y(135, scale),
                             enemy_y(175, scale), 0x251F20);
    return pixels;
}

static int render_charger_enemy(struct toy_renderer *renderer,
                                const struct camera *camera,
                                const struct toy_game_enemy *e, int scale,
                                uint32_t color)
{
    int x = e->x, z = e->z, pixels = 0;
    int sx = scale * 100 / 1120;
    pixels += draw_cuboid(renderer, camera, x - 135 * sx / 100, x - 20 * sx / 100,
                          enemy_y(-900, scale), enemy_y(-760, scale),
                          z - 150 * sx / 100, z + 110 * sx / 100, 0x30261F);
    pixels += draw_cuboid(renderer, camera, x + 20 * sx / 100, x + 135 * sx / 100,
                          enemy_y(-900, scale), enemy_y(-760, scale),
                          z - 150 * sx / 100, z + 110 * sx / 100, 0x30261F);
    pixels += draw_cuboid(renderer, camera, x - 235 * sx / 100, x + 235 * sx / 100,
                          enemy_y(-760, scale), enemy_y(95, scale),
                          z - 135 * sx / 100, z + 135 * sx / 100, color);
    pixels += draw_cuboid(renderer, camera, x - 205 * sx / 100, x + 205 * sx / 100,
                          enemy_y(75, scale), enemy_y(360, scale),
                          z - 145 * sx / 100, z + 145 * sx / 100, color + 0x18100A);
    /* Charger 面部是一个粗像素 C。 */
    pixels += draw_face_rect(renderer, camera, x, z, 150, e->dir_x, e->dir_z,
                             -120, -82, enemy_y(105, scale), enemy_y(315, scale),
                             0x2A1710);
    pixels += draw_face_rect(renderer, camera, x, z, 150, e->dir_x, e->dir_z,
                             -82, 100, enemy_y(280, scale), enemy_y(315, scale),
                             0x2A1710);
    pixels += draw_face_rect(renderer, camera, x, z, 150, e->dir_x, e->dir_z,
                             -82, 100, enemy_y(105, scale), enemy_y(140, scale),
                             0x2A1710);
    return pixels;
}

static int draw_tank_arm_box(struct toy_renderer *renderer,
                             const struct camera *camera, int x, int z,
                             int sy, int cy, int side_x,
                             int shoulder_y, int shoulder_z,
                             int fist_y, int fist_z, uint32_t color)
{
    static const int faces[36] = {
        0,1,2, 0,2,3, 4,6,5, 4,7,6,
        0,4,5, 0,5,1, 3,2,6, 3,6,7,
        0,3,7, 0,7,4, 1,5,6, 1,6,2
    };
    struct vec3 vertices[8];
    int local_x[8], local_y[8], local_z[8];
    int dy = fist_y - shoulder_y, dz = fist_z - shoulder_z;
    int length = isqrt((long long)dy * dy + (long long)dz * dz);
    int py, pz, i, pixels = 0;
    const int half_width = 165, half_thickness = 150;
    if (length < 1) length = 1;
    py = -dz * half_thickness / length;
    pz = dy * half_thickness / length;
    for (i = 0; i < 8; i++) {
        int endpoint = i >= 4;
        int corner = i & 3;
        int center_y = endpoint ? fist_y : shoulder_y;
        int center_z = endpoint ? fist_z : shoulder_z;
        local_x[i] = side_x +
            ((corner == 0 || corner == 3) ? -half_width : half_width);
        local_y[i] = center_y + (corner < 2 ? -py : py);
        local_z[i] = center_z + (corner < 2 ? -pz : pz);
        actor_world_point(x, z, sy, cy, local_x[i], local_y[i], local_z[i],
                          &vertices[i]);
    }
    for (i = 0; i < 36; i += 3)
        pixels += draw_world_triangle(renderer, camera,
                                      &vertices[faces[i]],
                                      &vertices[faces[i + 1]],
                                      &vertices[faces[i + 2]], color);
    return pixels;
}

static int render_tank_enemy(struct toy_renderer *renderer,
                             const struct camera *camera,
                             const struct toy_game_enemy *e, int scale,
                             uint32_t color)
{
    int x = e->x, z = e->z, pixels = 0;
    int swing = 0, fist_y, fist_z;
    if (e->charge_active) {
        swing = e->charge_elapsed_ms * 1000 / TOY_CONFIG_TANK_WINDUP_MS;
        if (swing > 1000) swing = 1000;
    }
    fist_y = -500 + swing * 800 / 1000;
    fist_z = 80 + swing * 720 / 1000;
    /* A broad torso, high shoulders and two oversized arms distinguish the
     * boss even at long range.  During windup each arm rotates from a hanging
     * pose into the facing direction; its height rises instead of translating
     * the whole rectangular arm downward. */
    pixels += draw_cuboid(renderer, camera, x - 150, x - 35,
                          enemy_y(-900, scale), enemy_y(-700, scale),
                          z - 175, z + 120, 0x29291F);
    pixels += draw_cuboid(renderer, camera, x + 35, x + 150,
                          enemy_y(-900, scale), enemy_y(-700, scale),
                          z - 175, z + 120, 0x29291F);
    pixels += draw_cuboid(renderer, camera, x - 285, x + 285,
                          enemy_y(-720, scale), enemy_y(170, scale),
                          z - 210, z + 210, color);
    active_actor_lift = active_enemy_lift;
    pixels += draw_tank_arm_box(renderer, camera, x, z,
                                e->dir_x, e->dir_z, -390,
                                450, 0, fist_y, fist_z,
                                color + 0x101008);
    pixels += draw_tank_arm_box(renderer, camera, x, z,
                                e->dir_x, e->dir_z, 390,
                                450, 0, fist_y, fist_z,
                                color + 0x101008);
    active_actor_lift = 0;
    pixels += draw_ellipsoid_head(renderer, camera, x, z,
                                  enemy_y(330, scale), 225,
                                  (enemy_y(570, scale) -
                                   enemy_y(140, scale)) / 2,
                                  color + 0x18180C);
    /* Pixel T on the face. */
    pixels += draw_face_rect(renderer, camera, x, z, 225, e->dir_x, e->dir_z,
                             -125, 125, enemy_y(360, scale),
                             enemy_y(410, scale), 0x202016);
    pixels += draw_face_rect(renderer, camera, x, z, 225, e->dir_x, e->dir_z,
                             -28, 28, enemy_y(180, scale),
                             enemy_y(410, scale), 0x202016);
    return pixels;
}

static int render_smoker_enemy(struct toy_renderer *renderer,
                               const struct camera *camera,
                               const struct toy_game_enemy *e, int scale,
                               uint32_t color)
{
    int x = e->x, z = e->z, pixels = 0;
    pixels += draw_cuboid(renderer, camera, x - 105, x - 25,
                          enemy_y(-900, scale), enemy_y(-760, scale),
                          z - 105, z + 75, 0x30261F);
    pixels += draw_cuboid(renderer, camera, x + 25, x + 105,
                          enemy_y(-900, scale), enemy_y(-760, scale),
                          z - 105, z + 75, 0x30261F);
    pixels += draw_cylinder(renderer, camera, x, z, 145,
                            enemy_y(-770, scale), enemy_y(180, scale), color);
    pixels += draw_ellipsoid_head(renderer, camera, x, z,
                                  enemy_y(315, scale), 165,
                                  (enemy_y(560, scale) - enemy_y(130, scale)) / 2,
                                  color + 0x18100A);
    /* 5x7 像素字模，保证 SM 在正常体型的脸上仍完整可辨。 */
    {
        static const char *letters[2][7] = {
            { "11111", "10000", "10000", "11111", "00001", "00001", "11111" },
            { "10001", "11011", "10101", "10101", "10001", "10001", "10001" }
        };
        int letter, row, col;
        for (letter = 0; letter < 2; letter++)
            for (row = 0; row < 7; row++)
                for (col = 0; col < 5; col++)
                    if (letters[letter][row][col] == '1') {
                        /* 面部平面从外侧看会发生镜像，反向布置列才能让
                         * 玩家看到正常顺序的 S 和 M。 */
                        int h0 = -145 + (letter * 6 + col) * 24;
                        pixels += draw_face_rect(renderer, camera, x, z, 165,
                                                 e->dir_x, e->dir_z,
                                                 h0, h0 + 17,
                                                 enemy_y(170 + (6 - row) * 38, scale),
                                                 enemy_y(200 + (6 - row) * 38, scale),
                                                 0xFFD070);
                    }
    }
    return pixels;
}

static int render_smoker_tongue(struct toy_renderer *renderer,
                                const struct camera *camera,
                                const struct toy_game_enemy *e)
{
    int target_x, target_z, target_lift = 0, pixels;
    if (active_net && active_net->mode == RASTERFALL_NET_CLIENT &&
        e->special_target_kind == 0 && active_net->players[0].active) {
        target_x = active_net->players[0].camera.x;
        target_z = active_net->players[0].camera.z;
        target_lift = active_net->players[0].airborne_y;
    } else if (e->special_target_kind == 1 &&
               e->special_target_index >= 0 &&
               e->special_target_index < TOY_GAME_MAX_ACTORS &&
               game.actors[e->special_target_index].active) {
        const struct toy_game_actor *actor =
            &game.actors[e->special_target_index];
        target_x = actor->x;
        target_z = actor->z;
        target_lift = actor->airborne_y;
    } else {
        target_x = game.px;
        target_z = game.pz;
        target_lift = game.player_airborne_y;
    }
    pixels = draw_tongue_segment(renderer, camera, e->x, 270, e->z,
                                 target_x, -360 + target_lift, target_z);
    /* 两个水平束缚圈表现舌头在目标身上的缠绕。 */
    pixels += draw_cylinder(renderer, camera, target_x, target_z, 225,
                            -430 + target_lift, -395 + target_lift, 0xB98B62);
    pixels += draw_cylinder(renderer, camera, target_x, target_z, 205,
                            -280 + target_lift, -245 + target_lift, 0xB98B62);
    return pixels;
}

static void render_enemy_alert(struct toy_renderer *renderer,
                               const struct camera *camera,
                               const struct toy_game_enemy *e, int scale)
{
    struct vec3 world, view;
    struct toy_screen_vertex screen;
    int x, y;
    /* 追踪尸潮也常显红色惊叹号：它们已知晓玩家位置，无需侦测。 */
    if (e->ai_state != TOY_GAME_ENEMY_NOTICE &&
        e->ai_state != TOY_GAME_ENEMY_ALERT &&
        e->ai_state != TOY_GAME_ENEMY_TRACKING) return;
    world.x = e->x;
    world.y = enemy_y(500, scale);
    world.z = e->z;
    world_to_view(camera, &world, &view);
    if (view.z < NEAR_Z) return;
    project_vertex(&renderer->surface, &view, &screen);
    x = screen.x - 5;
    y = screen.y;
    if (x < 1 || x + 11 >= renderer->surface.width ||
        y < 1 || y + 27 >= renderer->surface.height)
        return;
    fill_rect(&renderer->surface, x - 1, y - 1, 11, 18, 0x301010);
    fill_rect(&renderer->surface, x + 1, y + 1, 7, 14, 0xFF3030);
    fill_rect(&renderer->surface, x - 1, y + 19, 11, 8, 0x301010);
    fill_rect(&renderer->surface, x + 1, y + 21, 7, 4, 0xFF3030);
}

/* 两种低多边形敌人；受击闪红/命中闪白，倒地时整体纵向压扁。 */
static int render_blob_shadow(struct toy_renderer *renderer,
                              const struct camera *camera,
                              const struct toy_game_enemy *e, int scale)
{
    struct vec3 a, b, c, d;
    int rx = 230 * scale / 1000;
    int rz = 150 * scale / 1000;
    int y = -886; /* just above the baked floor */
    if (rx < 18) rx = 18;
    if (rz < 12) rz = 12;
    a.x = e->x - rx; a.y = y; a.z = e->z - rz;
    b.x = e->x + rx; b.y = y; b.z = e->z - rz;
    c.x = e->x + rx; c.y = y; c.z = e->z + rz;
    d.x = e->x - rx; d.y = y; d.z = e->z + rz;
    /* Two nested, opaque low-alpha-style tones approximate a soft penumbra. */
    return draw_quad(renderer, camera, &a, &b, &c, &d, 0x17151A);
}

static int render_enemies(struct toy_renderer *renderer,
                          const struct camera *camera)
{
    int pixels = 0;
    for (int i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        const struct toy_game_enemy *e = &game.enemies[i];
        struct vec3 center, view;
        uint32_t color;
        int scale = 1000;
        if (e->active == 0) continue;
        center.x = e->x;
        center.y = 0;
        center.z = e->z;
        world_to_view(camera, &center, &view);
        if (view.z > ENEMY_RENDER_DISTANCE) continue;
        if (e->active == 2) {
            scale = e->dying_ms * 1000 / TOY_GAME_DYING_MS;
            color = 0x5A1A1A;
        } else {
            const struct toy_game_enemy_info *info = toy_game_enemy_info(e->type);
            color = info->color;
            if (e->type == TOY_GAME_ENEMY_HEAVY ||
                e->type == TOY_GAME_ENEMY_PURSUIT_HEAVY) scale = 1350;
            if (info->ability == TOY_GAME_ENEMY_ABILITY_SMOKER_TONGUE)
                color = RF_COLOR_ENEMY_SMOKER;
            if (info->ability == TOY_GAME_ENEMY_ABILITY_CHARGER_RUSH) {
                color = e->charge_active ? 0xB06A36 : RF_COLOR_ENEMY_CHARGER;
                scale = 1180;
            }
            if (info->ability == TOY_GAME_ENEMY_ABILITY_TANK_SWEEP) {
                color = RF_COLOR_ENEMY_TANK;
                scale = 1600;
            }
            if (info->ability == TOY_GAME_ENEMY_ABILITY_SMOKER_TONGUE)
                scale = 1000;
            if (e->hurt > 0) color = 0xBB3333;
            else if (e->flash > 0) color = 0xDFDFDF;
            else if (e->type == TOY_GAME_ENEMY_PURSUIT_HEAVY)
                color = RF_COLOR_ENEMY_PURSUIT_HEAVY;
            else if (e->type == TOY_GAME_ENEMY_PURSUIT_FAST)
                color = RF_COLOR_ENEMY_PURSUIT_FAST;
            else if (e->ai_state == TOY_GAME_ENEMY_TRACKING)
                color = RF_COLOR_ENEMY_COMMON; /* PURSUIT_COMMON：沿用普通敌人颜色 */
        }
        pixels += render_blob_shadow(renderer, camera, e, scale);
        if (toy_game_enemy_info(e->type)->ability ==
                TOY_GAME_ENEMY_ABILITY_SMOKER_TONGUE &&
            e->special_target_active)
            pixels += render_smoker_tongue(renderer, camera, e);
        active_enemy_lift = e->airborne_y;
        if (toy_game_enemy_info(e->type)->ability ==
                TOY_GAME_ENEMY_ABILITY_TANK_SWEEP)
            pixels += render_tank_enemy(renderer, camera, e, scale, color);
        else if (toy_game_enemy_info(e->type)->ability ==
                TOY_GAME_ENEMY_ABILITY_CHARGER_RUSH)
            pixels += render_charger_enemy(renderer, camera, e, scale, color);
        else if (toy_game_enemy_info(e->type)->ability ==
                TOY_GAME_ENEMY_ABILITY_SMOKER_TONGUE)
            pixels += render_smoker_enemy(renderer, camera, e, scale, color);
        else if (e->type == TOY_GAME_ENEMY_HEAVY ||
            e->type == TOY_GAME_ENEMY_PURSUIT_HEAVY || (i & 1) == 0)
            pixels += render_block_enemy(renderer, camera, e, scale, color);
        else
            pixels += render_round_enemy(renderer, camera, e, scale, color);
        active_enemy_lift = 0;
        render_enemy_alert(renderer, camera, e, scale);
    }
    return pixels;
}

static void render_actor_name(struct toy_renderer *renderer,
                              const struct camera *camera, int x, int z,
                              int y, const char *name, uint32_t color)
{
    struct vec3 world, view;
    struct toy_screen_vertex screen;
    int width;
    if (!name || !*name) return;
    world.x = x; world.y = y; world.z = z;
    world_to_view(camera, &world, &view);
    if (view.z < NEAR_Z || view.z > ENEMY_RENDER_DISTANCE) return;
    project_vertex(&renderer->surface, &view, &screen);
    width = (int)strlen(name) * FB_FONT_W;
    screen.x -= width / 2;
    if (screen.x < 0 || screen.x + width >= renderer->surface.width ||
        screen.y < 0 || screen.y + FB_FONT_H >= renderer->surface.height) return;
    fb_draw_string((unsigned char *)renderer->surface.pixels, screen.x,
                   screen.y, name, color, renderer->surface.stride);
}

static void render_actor_status(struct toy_renderer *renderer,
                                const struct camera *camera, int x, int z,
                                int y, const char *name, int hp, int max_hp, int downed,
                                int revive_ms, uint32_t name_color)
{
    struct vec3 world, view;
    struct toy_screen_vertex screen;
    int width, bar_x, bar_y, fill;
    int is_base = name && !strcmp(name, "BASE");
    uint32_t hp_color;
    (void)revive_ms;
    world.x = x; world.y = y; world.z = z;
    world_to_view(camera, &world, &view);
    if (view.z < NEAR_Z || view.z > ENEMY_RENDER_DISTANCE) return;
    project_vertex(&renderer->surface, &view, &screen);
    if (is_base) screen.y -= 6;
    width = (int)strlen(name) * FB_FONT_W * (is_base ? 2 : 1);
    screen.x -= width / 2;
    if (screen.x < 0 || screen.x + width >= renderer->surface.width ||
        screen.y < 0 || screen.y + FB_FONT_H * (is_base ? 2 : 1) >=
        renderer->surface.height) return;
    if (is_base) {
        fb_draw_string_scaled((unsigned char *)renderer->surface.pixels,
                              screen.x, screen.y, name, 0xFF2020,
                              renderer->surface.stride, 2);
    } else {
        render_actor_name(renderer, camera, x, z, y, name, name_color);
    }
    bar_x = screen.x + (width - 64) / 2;
    bar_y = screen.y + FB_FONT_H * (is_base ? 2 : 1) + 4;
    if (bar_x < 2) bar_x = 2;
    if (bar_x + 64 >= renderer->surface.width) bar_x = renderer->surface.width - 66;
    fill_rect(&renderer->surface, bar_x - 2, bar_y - 2, 68, 7, RF_COLOR_UI_PANEL);
    if (is_base) hp_color = 0xFF2020;
    else if (hp < 10) hp_color = RF_COLOR_UI_DANGER;
    else if (hp < 40) hp_color = RF_COLOR_UI_WARNING;
    else hp_color = RF_COLOR_UI_SUCCESS;
    if (max_hp <= 0) max_hp = 100;
    fill = hp * 64 / max_hp;
    if (fill < 0) fill = 0;
    if (fill > 64) fill = 64;
    if (fill > 0) fill_rect(&renderer->surface, bar_x, bar_y, fill, 3, hp_color);
    if (is_base) {
        char hp_text[16];
        int hp_width;
        snprintf(hp_text, sizeof(hp_text), "%d", hp);
        hp_width = (int)strlen(hp_text) * FB_FONT_W * 2;
        if (bar_y + FB_FONT_H * 2 + 8 < renderer->surface.height)
            fb_draw_string_scaled((unsigned char *)renderer->surface.pixels,
                                  bar_x + (64 - hp_width) / 2, bar_y + 7,
                                  hp_text, hp_color, renderer->surface.stride, 2);
    }
    (void)downed;
}

static int render_player_avatar(struct toy_renderer *renderer,
                                const struct camera *camera, int x, int z,
                                int sy, int cy, int weapon, int muzzle_flash,
                                uint32_t body_color, int downed,
                                int animation_id, int animation_time_ms);

static void sample_actor_fall_roll(int progress, int *out_sin, int *out_cos)
{
    int phase;
    if (progress < 0) progress = 0;
    if (progress > 1000) progress = 1000;
    if (progress < 500) {
        phase = progress * 2;
        *out_sin = 724 * phase / 1000;
        *out_cos = 1024 - 300 * phase / 1000;
    } else {
        phase = (progress - 500) * 2;
        *out_sin = 724 + 300 * phase / 1000;
        *out_cos = 724 - 724 * phase / 1000;
    }
}

static int draw_limb_segment(struct toy_renderer *renderer,
                             const struct camera *camera, int x, int z,
                             int sy, int cy,
                             int ax, int ay, int az,
                             int bx, int by, int bz, int radius,
                             uint32_t color)
{
    static const int faces[36] = {
        0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6,
        0, 4, 5, 0, 5, 1, 3, 2, 6, 3, 6, 7,
        0, 3, 7, 0, 7, 4, 1, 5, 6, 1, 6, 2
    };
    struct vec3 vertices[8];
    int dx = bx - ax, dy = by - ay, dz = bz - az;
    int length = isqrt((long long)dy * dy + (long long)dz * dz);
    int py, pz, i, pixels = 0;
    (void)dx; /* Arm segments currently stay in the local Y-Z plane. */
    if (length < 1) length = 1;
    py = -dz * radius / length;
    pz = dy * radius / length;
    for (i = 0; i < 8; i++) {
        int endpoint = i >= 4;
        int corner = i & 3;
        int center_x = endpoint ? bx : ax;
        int lx = center_x + ((corner == 0 || corner == 3) ?
                            -radius : radius);
        int ly = endpoint ? by : ay;
        int lz = endpoint ? bz : az;
        if (corner == 0 || corner == 1) ly -= py;
        else ly += py;
        if (corner == 0 || corner == 3) lz -= pz;
        else lz += pz;
        actor_world_point(x, z, sy, cy, lx, ly, lz, &vertices[i]);
    }
    for (i = 0; i < 36; i += 3)
        pixels += draw_world_triangle(renderer, camera,
                                       &vertices[faces[i]],
                                       &vertices[faces[i + 1]],
                                       &vertices[faces[i + 2]], color);
    return pixels;
}

/* A leg is a solid local cuboid rotating around its hip.  This is separate
 * from draw_limb_segment because a line extrusion loses its volume when the
 * camera sees the leg nearly edge-on. */
static int draw_actor_leg_box(struct toy_renderer *renderer,
                              const struct camera *camera, int x, int z,
                              int sy, int cy, int x0, int x1,
                              int y0, int y1, int swing_sin,
                              uint32_t color)
{
    struct vec3 v[8];
    static const int faces[36] = {
        0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6,
        0, 4, 5, 0, 5, 1, 3, 2, 6, 3, 6, 7,
        0, 3, 7, 0, 7, 4, 1, 5, 6, 1, 6, 2
    };
    int swing_cos = 1024 - (swing_sin < 0 ? -swing_sin : swing_sin) *
                    (swing_sin < 0 ? -swing_sin : swing_sin) / 2048;
    int i, pixels = 0;
    int xs[8] = {x0, x1, x1, x0, x0, x1, x1, x0};
    int ys[8] = {y0, y0, y1, y1, y0, y0, y1, y1};
    int zs[8] = {-75, -75, -75, -75, 75, 75, 75, 75};
    const int hip_y = -610;
    for (i = 0; i < 8; i++) {
        int dy = ys[i] - hip_y;
        int ly = hip_y + (dy * swing_cos + zs[i] * swing_sin) / 1024;
        int lz = (-dy * swing_sin + zs[i] * swing_cos) / 1024;
        actor_world_point(x, z, sy, cy, xs[i], ly, lz, &v[i]);
    }
    for (i = 0; i < 36; i += 3)
        pixels += draw_world_triangle(renderer, camera, &v[faces[i]],
                                      &v[faces[i + 1]], &v[faces[i + 2]],
                                      color + ((i / 3) & 3) * 0x050505);
    return pixels;
}

static void limb_direction(int pitch, int *dy, int *dz)
{
    /* TODO: replace these compatibility buckets with continuous fixed-point
     * sin/cos when the animation pose gains arbitrary arm angles. */
    if (pitch <= -45) { *dy = -866; *dz = 500; }
    else if (pitch < 0) { *dy = -500; *dz = 866; }
    else if (pitch < 45) { *dy = 500; *dz = 866; }
    else { *dy = 866; *dz = 500; }
}

/* Coarse fixed-point rotation is sufficient for the low-poly actor arm. */
static void rotate_arm_xz(int x, int z, int degrees, int *out_x, int *out_z)
{
    static const int sin16[16] = {
        0, 391, 724, 946, 1024, 946, 724, 391,
        0, -391, -724, -946, -1024, -946, -724, -391
    };
    static const int cos16[16] = {
        1024, 946, 724, 391, 0, -391, -724, -946,
        -1024, -946, -724, -391, 0, 391, 724, 946
    };
    int index = degrees * 16 / 360;
    int s, c;
    if (index < 0) index = 0;
    index &= 15;
    s = sin16[index];
    c = cos16[index];
    *out_x = (x * c - z * s) / 1024;
    *out_z = (x * s + z * c) / 1024;
}

static int render_actor_model_weapon(struct toy_renderer *renderer,
                                     const struct camera *camera, int x, int z,
                                     int sy, int cy, int weapon,
                                     int muzzle_flash, int animation_id,
                                     int animation_time_ms,
                                     uint32_t body_color)
{
    const char *path = rasterfall_weapon_model_path(weapon);
    struct rasterfall_model_asset *model;
    struct rasterfall_animation_pose pose;
    int width, height, depth, length, scale, anchor_x, anchor_z;
    int i, pixels = 0;
    if (!path) return 0;
    model = gallery_model_named(path, NULL);
    if (!model) return 0;
    rasterfall_animation_sample_duration(
        animation_id, animation_time_ms,
        animation_id == TOY_GAME_ANIM_RELOAD ?
            toy_game_weapon_info(weapon)->reload_ms :
            toy_game_animation_info(animation_id)->duration_ms, &pose);
    width = model->max_x - model->min_x;
    height = model->max_y - model->min_y;
    depth = model->max_z - model->min_z;
    length = width > height ? width : height;
    if (depth > length) length = depth;
    if (length <= 0) return 0;
    scale = (weapon == TOY_GAME_WEAPON_SHOTGUN ? 760000 :
             weapon == TOY_GAME_WEAPON_AWP ? 920000 :
             weapon == TOY_GAME_WEAPON_AK ? 760000 : 380000) / length;
    if (scale < 1) scale = 1;
    /* Keep horizontal/depth placement proportional, but preserve the shared
     * vertical grip height.  Scaling the Y anchor moves the rifle down to
     * the floor because actor_world_point already applies actor elevation. */
    if (weapon == TOY_GAME_WEAPON_AK || weapon == TOY_GAME_WEAPON_AWP) {
        anchor_x = 170;
        anchor_z = 260;
    } else {
        anchor_x = 195;
        anchor_z = 170;
    }
    for (i = 0; i < (int)model->primitive_count; i++) {
        const unsigned char *primitive = model->primitives +
            i * RASTERFALL_MODEL_PRIMITIVE_BYTES;
        const unsigned char *indices = model->indices + model_u32(primitive) * 4;
        unsigned int index_count = model_u32(primitive + 4);
        unsigned int material = model_u32(primitive + 8);
        uint32_t color = material < model->material_count ?
            model_u32(model->materials + material * model->material_bytes) :
            RF_COLOR_UI_TEXT_MUTED;
        unsigned int j;
        for (j = 0; j + 2 < index_count; j += 3) {
            unsigned int ids[3] = { model_u32(indices + j * 4),
                                    model_u32(indices + (j + 1) * 4),
                                    model_u32(indices + (j + 2) * 4) };
            struct vec3 v[3];
            int k;
            for (k = 0; k < 3; k++) {
                const unsigned char *p;
                int mx, my, mz, lx, ly, lz;
                if (ids[k] >= model->vertex_count) break;
                p = model->vertices + ids[k] * model->vertex_bytes;
                mx = *(const int *)p;
                my = *(const int *)(p + 4);
                mz = *(const int *)(p + 8);
                if (weapon == TOY_GAME_WEAPON_PISTOL) {
                    lx = mx - (model->min_x + model->max_x) / 2;
                    ly = my - (model->min_y + model->max_y) / 2;
                    lz = mz - model->min_z;
                } else if (weapon == TOY_GAME_WEAPON_SHOTGUN) {
                    lx = mz - (model->min_z + model->max_z) / 2;
                    ly = my - (model->min_y + model->max_y) / 2;
                    lz = mx - model->min_x;
                } else if (weapon == TOY_GAME_WEAPON_AK ||
                           weapon == TOY_GAME_WEAPON_AWP) {
                    lx = -mx + (model->min_x + model->max_x) / 2;
                    ly = my - (model->min_y + model->max_y) / 2;
                    lz = -mz + (model->min_z + model->max_z) / 2;
                } else {
                    lx = mx - (model->min_x + model->max_x) / 2;
                    ly = my - (model->min_y + model->max_y) / 2;
                    lz = mz - (model->min_z + model->max_z) / 2;
                }
                /* Local +X is the avatar's right side.  Keep the weapon clear
                 * of the torso and put its muzzle in front of the body. */
                /* Shared grip-area anchor: right of the torso and slightly
                 * forward, so both wrists can meet around the weapon rear. */
                lx = lx * scale / 1000 + anchor_x;
                ly = ly * scale / 1000 - 395;
                lz = lz * scale / 1000 + anchor_z;
                ly += lz * pose.weapon_pitch / 1000;
                actor_world_point(x, z, sy, cy, lx, ly, lz, &v[k]);
            }
            if (k == 3)
                pixels += draw_world_triangle(renderer, camera, &v[0], &v[1],
                                              &v[2], color);
        }
    }
    if (muzzle_flash > 0)
        pixels += draw_actor_box(renderer, camera, x, z, sy, cy,
                                 163, 227, -395, -355, 420, 495,
                                 RF_COLOR_UI_ACCENT);
    /* Two overlapping cuboids per arm.  Both segments remain in the arm's
     * local Y-Z plane; draw_limb_segment performs the actor yaw transform. */
    {
        int ruy, ruz, rfy, rfz, luy, luz, lfy, lfz;
        int rex, rey, rez, rwx, rwy, rwz;
        int lex, ley, lez, lwx, lwy, lwz;
        int lux2, luy2, luz2, lfx2, lfy2, lfz2;
        const int right_shoulder_x = 205, left_shoulder_x = -205;
        const int left_hand_x = 175;
        const int upper_len = 160, forearm_len = 160, overlap = 20;
        int right_recoil = animation_id == TOY_GAME_ANIM_FIRE &&
                           animation_time_ms < 70 ? -24 : 0;
        limb_direction(pose.right_upper_pitch, &ruy, &ruz);
        limb_direction(pose.right_forearm_pitch, &rfy, &rfz);
        limb_direction(pose.left_upper_pitch, &luy, &luz);
        limb_direction(pose.left_forearm_pitch, &lfy, &lfz);
        rex = right_shoulder_x;
        rey = -180 + ruy * upper_len / 1024 + right_recoil;
        rez = ruz * upper_len / 1024;
        rwx = rex;
        rwy = rey + rfy * forearm_len / 1024;
        rwz = rez + rfz * forearm_len / 1024;
        lex = left_shoulder_x;
        lux2 = 0;
        luy2 = luy * upper_len / 1024;
        luz2 = luz * upper_len / 1024;
        lfx2 = left_hand_x - left_shoulder_x;
        lfy2 = lfy * forearm_len / 1024;
        lfz2 = lfz * forearm_len / 1024;
        if (pose.left_arm_rotation) {
            rotate_arm_xz(lux2, luz2, pose.left_arm_rotation,
                          &lux2, &luz2);
            rotate_arm_xz(lfx2, lfz2, pose.left_arm_rotation,
                          &lfx2, &lfz2);
        }
        lex = left_shoulder_x + lux2;
        ley = -180 + luy2;
        lez = luz2;
        /* The left hand reaches inward toward the rear/receiver area of the
         * weapon; the forearm is therefore the only arm segment that changes
         * local X. */
        lwx = lex + lfx2;
        lwy = ley + lfy2;
        lwz = lez + lfz2;
        pixels += draw_limb_segment(renderer, camera, x, z, sy, cy,
                                    right_shoulder_x, -180 + right_recoil, 0,
                                    rex, rey, rez, 40, body_color);
        pixels += draw_limb_segment(renderer, camera, x, z, sy, cy,
                                    rex - rfy * overlap / 1024,
                                    rey - rfy * overlap / 1024,
                                    rez - rfz * overlap / 1024,
                                    rwx, rwy, rwz, 42, 0xC08A68);
        pixels += draw_limb_segment(renderer, camera, x, z, sy, cy,
                                    left_shoulder_x, -180, 0,
                                    lex, ley, lez, 40, body_color);
        pixels += draw_limb_segment(renderer, camera, x, z, sy, cy,
                                    lex - lfy * overlap / 1024,
                                    ley - lfy * overlap / 1024,
                                    lez - lfz * overlap / 1024,
                                    lwx, lwy, lwz, 42, 0xC08A68);
    }
    return pixels;
}

static int render_actor_weapon(struct toy_renderer *renderer,
                               const struct camera *camera, int x, int z,
                               int sy, int cy, int weapon, int muzzle_flash,
                               int animation_id, int animation_time_ms,
                               uint32_t body_color)
{
    if (weapon < 0) return 0;
    return render_actor_model_weapon(renderer, camera, x, z, sy, cy,
                                     weapon, muzzle_flash, animation_id,
                                     animation_time_ms, body_color);
}

static int network_actor_lift(int x, int z, int airborne_y)
{
    int lift = airborne_y;
    int i;
    for (i = 0; i < level_map.platform_count; i++) {
        const struct toy_game_platform *platform = &level_map.platforms[i];
        if (x >= platform->minx && x <= platform->maxx &&
            z >= platform->minz && z <= platform->maxz &&
            platform->height > lift)
            lift = platform->height;
    }
    return lift;
}

static void render_ai_teammate_name(struct toy_renderer *renderer,
                                    const struct camera *camera)
{
    int i;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
        const struct toy_game_actor *actor = &game.actors[i];
        char label[64];
        const char *display_name;
        long dx, dz, d2, dist, dot;
        uint32_t color = actor->class_id == TOY_GAME_AI_LEVEL_3 ? RF_COLOR_UI_ACCENT :
                         actor->class_id == TOY_GAME_AI_LEVEL_2 ? RF_COLOR_UI_AI :
                         RF_COLOR_UI_PLAYER;
        if (!actor->active || actor->kind != TOY_GAME_ACTOR_AI) continue;
        dx = (long)actor->x - camera->x;
        dz = (long)actor->z - camera->z;
        d2 = dx * dx + dz * dz;
        if (d2 > 10800L * 10800L) continue;
        dist = isqrt(d2);
        if (dist <= 0) continue;
        dot = dx * camera->sy + dz * camera->cy;
        if (dot < dist * 650) continue;
        display_name = actor->name;
        if (actor->animation_demo) {
            snprintf(label, sizeof(label), "%s [%s]", actor->name,
                     toy_game_animation_name(actor->animation.id));
            display_name = label;
        }
        render_actor_status(renderer, camera, actor->x, actor->z,
                            actor->state == TOY_GAME_ACTOR_DOWNED ? -350 : 700,
                            display_name, actor->hp, actor->max_hp,
                            actor->state == TOY_GAME_ACTOR_DOWNED,
                            actor->revive_progress_ms, color);
    }
}

static int render_ai_teammate(struct toy_renderer *renderer,
                              const struct camera *camera)
{
    int i, pixels = 0;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
        const struct toy_game_actor *actor = &game.actors[i];
        struct vec3 center, view;
        uint32_t color;
        if (!actor->active || actor->kind != TOY_GAME_ACTOR_AI) continue;
        center.x = actor->x; center.y = 0; center.z = actor->z;
        world_to_view(camera, &center, &view);
        if (view.z < NEAR_Z || view.z > ENEMY_RENDER_DISTANCE) continue;
        color = actor->class_id == TOY_GAME_AI_LEVEL_3 ? RF_COLOR_AI_HEAVY :
                actor->class_id == TOY_GAME_AI_LEVEL_2 ? RF_COLOR_AI_RIFLE :
                RF_COLOR_AI_BASIC;
        active_actor_lift = actor->airborne_y;
        for (int p = 0; p < level_map.platform_count; p++) {
            const struct toy_game_platform *platform = &level_map.platforms[p];
            if (actor->x >= platform->minx && actor->x <= platform->maxx &&
                actor->z >= platform->minz && actor->z <= platform->maxz &&
                platform->height > active_actor_lift)
                active_actor_lift = platform->height;
        }
        pixels += render_player_avatar(renderer, camera, actor->x, actor->z,
                                       actor->sy, actor->cy,
                                       actor->current_slot >= 0 &&
                                       actor->current_slot < TOY_GAME_WEAPON_SLOTS ?
                                       actor->slots[actor->current_slot].weapon : -1,
                                       actor->muzzle_flash_ms, color,
                                       actor->state == TOY_GAME_ACTOR_DOWNED,
                                       actor->animation.id,
                                       actor->animation.time_ms);
        active_actor_lift = 0;
    }
    return pixels;
}

static int render_player_avatar(struct toy_renderer *renderer,
                                const struct camera *camera, int x, int z,
                                int sy, int cy, int weapon, int muzzle_flash,
                                uint32_t body_color, int downed,
                                int animation_id, int animation_time_ms)
{
    struct rasterfall_animation_pose pose;
    int pixels = 0, face_y0, face_y1, animation_lift;
    int pose_x, pose_z;
    int death_progress = 0;
    int show_fall_gear = 0;
    if (!renderer || !camera) return 0;
    rasterfall_animation_sample_duration(
        animation_id, animation_time_ms,
        animation_id == TOY_GAME_ANIM_RELOAD && weapon >= 0 ?
            toy_game_weapon_info(weapon)->reload_ms :
            toy_game_animation_info(animation_id)->duration_ms, &pose);
    animation_lift = pose.body_lift;
    pose_x = x + sy * pose.forward_shift / 1024;
    pose_z = z + cy * pose.forward_shift / 1024;
    active_actor_roll_sin = 0;
    active_actor_roll_cos = 1024;
    active_actor_lift += animation_lift;
    if (animation_id == TOY_GAME_ANIM_DEATH ||
        animation_id == TOY_GAME_ANIM_REVIVE) {
        death_progress = animation_time_ms * 1000 /
                         toy_game_animation_info(TOY_GAME_ANIM_DEATH)->duration_ms;
        if (death_progress > 1000) death_progress = 1000;
        if (animation_id == TOY_GAME_ANIM_REVIVE)
            death_progress = 1000 - death_progress;
        sample_actor_fall_roll(death_progress, &active_actor_roll_sin,
                               &active_actor_roll_cos);
        show_fall_gear = animation_time_ms <
                         toy_game_animation_info(animation_id)->duration_ms;
        /* The legs are part of the same rigid character.  They are built in
         * local space first, then receive the body fall rotation below. */
        pixels += draw_actor_leg_box(renderer, camera, pose_x, pose_z,
                                     sy, cy, -95, -10, -900, -610,
                                     0, 0x25354A);
        pixels += draw_actor_leg_box(renderer, camera, pose_x, pose_z,
                                     sy, cy, 10, 95, -900, -610,
                                     0, 0x25354A);
        pixels += draw_actor_box(renderer, camera, pose_x, pose_z, sy, cy,
                                 -155, 155, -620, -100, -100, 100,
                                 body_color);
        pixels += draw_actor_ellipsoid_head(renderer, camera, pose_x, pose_z,
                                            sy, cy, 50, 145, 150,
                                            0xD2A878);
        face_y0 = -35; face_y1 = 185;
    } else if (downed) {
        pixels += draw_cuboid(renderer, camera, pose_x - 170, pose_x + 170,
                              -850 + active_actor_lift, -650 + active_actor_lift,
                              pose_z - 100, pose_z + 100, body_color);
        pixels += draw_ellipsoid_head(renderer, camera, pose_x, pose_z,
                                      -550 + active_actor_lift, 145, 100, 0xD2A878);
        face_y0 = -650; face_y1 = -470;
    } else {
        int left_leg_shift = pose.leg_swing;
        int right_leg_shift = -left_leg_shift;
        /* Rotate two solid cuboids around their hips; the roots remain
         * attached to the torso while the whole volume swings. */
        pixels += draw_actor_leg_box(renderer, camera, pose_x, pose_z,
                                     sy, cy, -95, -10, -900, -610,
                                     left_leg_shift, 0x25354A);
        pixels += draw_actor_leg_box(renderer, camera, pose_x, pose_z,
                                     sy, cy, 10, 95, -900, -610,
                                     right_leg_shift, 0x25354A);
        pixels += draw_actor_tilted_box(renderer, camera, pose_x, pose_z,
                                        sy, cy, -155, 155, -620, -100,
                                        -100, 100, pose.body_pitch,
                                        body_color);
        pixels += draw_actor_ellipsoid_head(renderer, camera, pose_x, pose_z,
                                            sy, cy, 50 + pose.body_pitch * 2 / 3,
                                            145, 150, 0xD2A878);
        face_y0 = -35; face_y1 = 185;
    }
    if (!downed || animation_id == TOY_GAME_ANIM_DEATH ||
        animation_id == TOY_GAME_ANIM_REVIVE) {
        pixels += render_actor_weapon(renderer, camera, pose_x, pose_z, sy, cy,
                                      weapon, muzzle_flash, animation_id,
                                      animation_time_ms, body_color);
    }
    pixels += draw_actor_face_rect(renderer, camera, pose_x, pose_z, sy, cy, 145,
                             -72, 72, face_y0, face_y1, RF_COLOR_AI_HEAVY);
    pixels += draw_actor_face_rect(renderer, camera, pose_x, pose_z, sy, cy, 145,
                             -16, 16, face_y0 + 40,
                             face_y1 - 40, 0xE8D2A8);
    pixels += draw_actor_face_rect(renderer, camera, pose_x, pose_z, sy, cy, 145,
                             -72, 72, face_y0 + 90,
                             face_y0 + 115, 0xE8D2A8);
    if (muzzle_flash > 0 &&
        ((animation_id != TOY_GAME_ANIM_DEATH &&
          animation_id != TOY_GAME_ANIM_REVIVE) || show_fall_gear))
        pixels += draw_cuboid(renderer, camera, pose_x - 45, pose_x + 45,
                              -560 + active_actor_lift, -430 + active_actor_lift,
                              pose_z - 120, pose_z + 120, RF_COLOR_UI_ACCENT);
    active_actor_lift -= animation_lift;
    active_actor_roll_sin = 0;
    active_actor_roll_cos = 1024;
    return pixels;
}

static int render_network_teammate(struct toy_renderer *renderer,
                                   const struct camera *camera,
                                   const struct rasterfall_net *net)
{
    static const uint32_t colors[RASTERFALL_NET_PLAYER_MAX] = {
        RF_COLOR_AI_RIFLE, RF_COLOR_UI_PLAYER, 0xD59BFF, 0x66D9D9
    };
    int pixels = 0, i;
    if (net->mode == RASTERFALL_NET_CLIENT) {
        for (i = 0; i < RASTERFALL_NET_PLAYER_MAX; i++) {
            const struct rasterfall_net_player *player = &net->players[i];
            const struct camera *render_camera;
            int render_airborne;
            if (!player->active || i == net->local_player_id) continue;
            /* Keep a downed body at its authoritative death position;
             * extrapolation can otherwise move it away from rescue range. */
            if (player->downed) {
                render_camera = &player->camera;
                render_airborne = 0;
            } else {
                render_camera = rasterfall_net_remote_render_camera(
                    net, i, &render_airborne);
            }
            active_actor_lift = network_actor_lift(render_camera->x,
                                                   render_camera->z,
                                                   render_airborne);
            pixels += render_player_avatar(renderer, camera,
                render_camera->x, render_camera->z, render_camera->sy,
                render_camera->cy, player->weapon, player->muzzle_flash_ms,
                colors[i], player->downed, player->animation.id,
                player->animation.time_ms);
            active_actor_lift = 0;
        }
        return pixels;
    }
    for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++) {
        const struct rasterfall_net_client *client = &net->clients[i];
        int weapon;
        if (!client->active || !client->connected) continue;
        weapon = client->current_slot >= 0 &&
                 client->current_slot < TOY_GAME_WEAPON_SLOTS ?
                 client->slots[client->current_slot].weapon : -1;
        active_actor_lift = network_actor_lift(client->camera.x,
                                               client->camera.z,
                                               client->airborne_y);
        pixels += render_player_avatar(renderer, camera, client->camera.x,
            client->camera.z, client->camera.sy, client->camera.cy, weapon,
            client->muzzle_flash_ms, colors[client->client_id], client->down,
            client->animation.id, client->animation.time_ms);
        active_actor_lift = 0;
    }
    return pixels;
}

static void render_network_teammate_status(struct toy_renderer *renderer,
                                           const struct camera *camera,
                                           const struct rasterfall_net *net)
{
    int i;
    char name[16];
    if (net->mode == RASTERFALL_NET_CLIENT) {
        for (i = 0; i < RASTERFALL_NET_PLAYER_MAX; i++) {
            const struct rasterfall_net_player *player = &net->players[i];
            const struct camera *render_camera;
            if (!player->active || i == net->local_player_id) continue;
            render_camera = player->downed ? &player->camera :
                rasterfall_net_remote_render_camera(net, i, NULL);
            snprintf(name, sizeof(name), "PLAYER %d", i + 1);
            render_actor_status(renderer, camera, render_camera->x,
                render_camera->z, 700, name, player->hp,
                TOY_GAME_SECONDARY_PLAYER_HP, player->downed,
                player->revive_progress_ms, RF_COLOR_UI_PLAYER);
        }
        return;
    }
    for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++) {
        const struct rasterfall_net_client *client = &net->clients[i];
        if (!client->active || !client->connected) continue;
        snprintf(name, sizeof(name), "PLAYER %d", client->client_id + 1);
        render_actor_status(renderer, camera, client->camera.x,
            client->camera.z, 700, name, client->hp,
            TOY_GAME_SECONDARY_PLAYER_HP, client->down,
            client->revive_progress_ms, RF_COLOR_UI_PLAYER);
    }
}

/* ── 子弹轨迹与命中粒子（纯视觉；逻辑步进 16ms 推进） ──────────── */

static void draw_effect_line(struct toy_surface *surface,
                             int x0, int y0, int x1, int y1,
                             uint32_t color)
{
    int bound_x = surface->width * 4;
    int bound_y = surface->height * 4;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int steps = 0;
    int max_steps = (surface->width + surface->height) * 4;
    x0 = clampi(x0, -bound_x, bound_x);
    x1 = clampi(x1, -bound_x, bound_x);
    y0 = clampi(y0, -bound_y, bound_y);
    y1 = clampi(y1, -bound_y, bound_y);
    dx = x1 > x0 ? x1 - x0 : x0 - x1;
    dy = y1 > y0 ? y0 - y1 : y1 - y0;
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    err = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < surface->width &&
            y0 >= 0 && y0 < surface->height)
            put_pixel(surface, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        if (++steps > max_steps) break;
        {
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}

static void draw_depth_effect_line(struct toy_renderer *renderer,
                                   int x0, int y0, long inv0,
                                   int x1, int y1, long inv1,
                                   uint32_t color)
{
    struct toy_surface *surface = &renderer->surface;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy, step = 0;
    int total = dx > dy ? dx : dy;
    int max_steps = surface->width + surface->height;
    for (;;) {
        if (x0 >= 0 && x0 < surface->width && y0 >= 0 && y0 < surface->height) {
            long inv = total > 0 ? inv0 + (inv1 - inv0) * step / total : inv0;
            int at = y0 * surface->width + x0;
            if (!renderer->depth || inv >= renderer->depth[at])
                put_pixel(surface, x0, y0, color);
        }
        if (x0 == x1 && y0 == y1) break;
        {
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx) { err += dx; y0 += sy; }
        }
        step++;
        if (step > max_steps) break;
    }
}

/* 弹道投影为屏幕线段：起点（枪口）与终点（命中点）都从世界空间投影，
 * 先裁剪到近平面，再 Liang-Barsky 裁剪到屏幕。 */
static void draw_tracer_line(struct toy_renderer *renderer,
                             const struct camera *camera,
                             int sx, int sy, int sz, int ex, int ey, int ez,
                             uint32_t color, int depth_test)
{
    struct toy_surface *surface = &renderer->surface;
    struct vec3 a, b, clipped;
    struct toy_screen_vertex pa, pb;
    int x0, y0, x1, y1, t0, t1, t_in, t_out, tmp;
    a.x = sx; a.y = sy; a.z = sz;
    b.x = ex; b.y = ey; b.z = ez;
    world_to_view(camera, &a, &a);
    world_to_view(camera, &b, &b);
    if (a.z < NEAR_Z && b.z < NEAR_Z) return;
    if (a.z < NEAR_Z) {
        near_intersection(&a, &b, &clipped);
        a.x = clipped.x;
        a.y = clipped.y;
        a.z = clipped.z;
    } else if (b.z < NEAR_Z) {
        near_intersection(&b, &a, &clipped);
        b.x = clipped.x;
        b.y = clipped.y;
        b.z = clipped.z;
    }
    project_vertex(surface, &a, &pa);
    project_vertex(surface, &b, &pb);
    x0 = pa.x; y0 = pa.y; x1 = pb.x; y1 = pb.y;
    /* 16.16 定点 Liang-Barsky；投影坐标有界但可能远超屏幕。
     * 裁剪到 [0, w-2]×[0, h-2]：第二条偏移线 (+1,+1) 也必须在界内。 */
    t0 = 0;
    t1 = 1 << 16;
    if (x0 != x1) {
        t_in = (int)((long long)(0 - x0) * 65536 / (x1 - x0));
        t_out = (int)((long long)(surface->width - 2 - x0) * 65536 / (x1 - x0));
        if (t_in > t_out) { tmp = t_in; t_in = t_out; t_out = tmp; }
        if (t_in > t0) t0 = t_in;
        if (t_out < t1) t1 = t_out;
        if (t0 > t1) return;
    } else if (x0 < 0 || x0 >= surface->width - 1) {
        return;   /* 竖直段整体在屏幕外 */
    }
    if (y0 != y1) {
        t_in = (int)((long long)(0 - y0) * 65536 / (y1 - y0));
        t_out = (int)((long long)(surface->height - 2 - y0) * 65536 / (y1 - y0));
        if (t_in > t_out) { tmp = t_in; t_in = t_out; t_out = tmp; }
        if (t_in > t0) t0 = t_in;
        if (t_out < t1) t1 = t_out;
        if (t0 > t1) return;
    } else if (y0 < 0 || y0 >= surface->height - 1) {
        return;   /* 水平段整体在屏幕外 */
    }
    if (t0 < 0) t0 = 0;
    if (t1 > (1 << 16)) t1 = 1 << 16;
    if (t0 > t1) return;
    if (t0 != 0 || t1 != (1 << 16)) {
        long old_inv0 = pa.inv_z, old_inv1 = pb.inv_z;
        int nx0 = x0 + (int)((long long)(x1 - x0) * t0 / 65536);
        int ny0 = y0 + (int)((long long)(y1 - y0) * t0 / 65536);
        pa.inv_z = old_inv0 + (old_inv1 - old_inv0) * t0 / 65536;
        pb.inv_z = old_inv0 + (old_inv1 - old_inv0) * t1 / 65536;
        x1 = nx0 + (int)((long long)(x1 - x0) * (t1 - t0) / 65536);
        y1 = ny0 + (int)((long long)(y1 - y0) * (t1 - t0) / 65536);
        x0 = nx0;
        y0 = ny0;
    }
    /* 双线 = 2px 粗的射线；偏移像素同色，无需额外混合 */
    if (depth_test) {
        draw_depth_effect_line(renderer, x0, y0, pa.inv_z,
                               x1, y1, pb.inv_z, color);
        draw_depth_effect_line(renderer, x0 + 1, y0 + 1, pa.inv_z,
                               x1 + 1, y1 + 1, pb.inv_z, color);
    } else {
        draw_effect_line(surface, x0, y0, x1, y1, color);
        draw_effect_line(surface, x0 + 1, y0 + 1, x1 + 1, y1 + 1, color);
    }
}

static int render_tracers(struct toy_renderer *renderer, const struct camera *camera)
{
    int i, pixels = 0;
    for (i = 0; i < RASTERFALL_TRACER_SLOTS; i++) {
        const struct rasterfall_tracer *t = &effects.tracers[i];
        uint32_t color;
        if (!t->active) continue;
        color = mix_color(0xFFE060, 0x3A2C14,
                          t->life_ms * 256 / RASTERFALL_TRACER_LIFE_MS, 256);
        draw_tracer_line(renderer, camera, t->sx, t->sy, t->sz,
                         t->ex, t->ey, t->ez, color, t->depth_test);
        pixels++;
    }
    return pixels;
}

static int render_fire_point(struct toy_renderer *renderer,
                             const struct camera *camera,
                             int x, int y, int z, int size, uint32_t color)
{
    struct vec3 world, view;
    struct toy_screen_vertex screen;
    world.x = x; world.y = y; world.z = z;
    world_to_view(camera, &world, &view);
    if (view.z < NEAR_Z) return 0;
    project_vertex(&renderer->surface, &view, &screen);
    if (screen.x < -size || screen.x >= renderer->surface.width ||
        screen.y < -size || screen.y >= renderer->surface.height) return 0;
    fill_rect(&renderer->surface, screen.x - size / 2, screen.y - size,
              size, size * 2, color);
    return 1;
}

/* Molotov flames are deliberately drawn as a small procedural billboard
 * field.  This keeps the effect freestanding and gives the whole burn zone
 * a readable footprint without adding a texture asset. */
static int render_fire_zones(struct toy_renderer *renderer,
                             const struct camera *camera)
{
    static const int ring[16][2] = {
        { 2500, 0 }, { 2310, 956 }, { 1768, 1768 }, { 956, 2310 },
        { 0, 2500 }, { -956, 2310 }, { -1768, 1768 }, { -2310, 956 },
        { -2500, 0 }, { -2310, -956 }, { -1768, -1768 }, { -956, -2310 },
        { 0, -2500 }, { 956, -2310 }, { 1768, -1768 }, { 2310, -956 }
    };
    int i, j, pixels = 0;
    for (i = 0; i < TOY_CONFIG_MAX_BURN_ZONES; i++) {
        const struct toy_game_burn_zone *zone = &game.burn_zones[i];
        if (!zone->active) continue;
        for (j = 0; j < 16; j++) {
            int density;
            for (density = 0; density < 3; density++) {
                int pulse = (zone->elapsed_ms / 90 + j * 37 + density * 11) % 5;
                int x = zone->x + ring[j][0] + (density - 1) * 90;
                int z = zone->z + ring[j][1] + (density - 1) * 70;
                int height = 180 + pulse * 55;
                pixels += render_fire_point(renderer, camera, x, -890, z,
                                            3 + pulse / 2, 0xD84A08);
                if ((j + density + zone->elapsed_ms / 120) % 3 == 0)
                    pixels += render_fire_point(renderer, camera,
                                                x - ring[j][1] / 20,
                                                -890 + height,
                                                z + ring[j][0] / 20,
                                                3 + pulse / 2, 0xFFB51A);
            }
        }
        for (j = 0; j < 24; j++) {
            int pulse = (zone->elapsed_ms / 75 + j * 19) % 6;
            int ox = (j * 733 % 1500) - 750;
            int oz = (j * 947 % 1500) - 750;
            pixels += render_fire_point(renderer, camera,
                                        zone->x + ox, -890 + 100 + pulse * 45,
                                        zone->z + oz, 4 + pulse / 2,
                                        j & 1 ? 0xFFB51A : 0xFF6A08);
        }
    }
    return pixels;
}

static int render_particles(struct toy_renderer *renderer, const struct camera *camera)
{
    int i, pixels = 0;
    for (i = 0; i < RASTERFALL_PARTICLE_SLOTS; i++) {
        const struct rasterfall_particle *p = &effects.particles[i];
        struct vec3 world, view;
        struct toy_screen_vertex screen;
        int k;
        if (!p->active) continue;
        world.x = p->x;
        world.y = p->y;
        world.z = p->z;
        world_to_view(camera, &world, &view);
        if (view.z < NEAR_Z) continue;
        project_vertex(&renderer->surface, &view, &screen);
        if (screen.x < 0 || screen.x + 2 >= renderer->surface.width ||
            screen.y < 0 || screen.y + 2 >= renderer->surface.height) continue;
        k = p->life_ms * 256 / RASTERFALL_PARTICLE_LIFE_MS;
        if (k > 256) k = 256;   /* 寿命随机 +40ms 可能超出基准，钳制插值比例 */
        fill_rect(&renderer->surface, screen.x, screen.y, 2, 2,
                  mix_color(0xFFC860, 0x4A2008, k, 256));
        pixels++;
    }
    return pixels;
}

#undef effects
#undef textures_enabled
#undef fixed_floor_lighting
void rasterfall_render_bind(struct rasterfall_render_context *ctx)
{
    render_ctx = ctx;
    active_session = ctx->session;
    active_effects = ctx->effects;
    active_net = ctx->net;
    active_wall_texture = ctx->wall_texture;
    active_model_texture = ctx->model_texture;
    active_lightmap = ctx->lightmap;
    active_textures = ctx->textures_enabled;
    active_fixed_floor_lighting = ctx->fixed_floor_lighting;
}

void rasterfall_render_set_coordinate_axes(int enabled)
{
    active_coordinate_axes = enabled != 0;
}

void rasterfall_render_bake_lightmap(void)
{
    bake_static_lightmap();
}

int rasterfall_render_scene(struct toy_renderer *renderer,
                            const struct camera *camera)
{
    return render_scene(renderer, camera);
}

int rasterfall_render_sign_text(struct toy_renderer *renderer,
                                const struct camera *camera)
{
    int i, drawn = 0;
    if (!renderer || !camera) return 0;
    for (i = 0; i < level_map.draw_count; i++) {
        if (level_map.draw[i].type == TOY_MAP_DRAW_SIGN) {
            drawn += render_world_text_plane(renderer, camera,
                level_map.draw[i].a + 40,
                level_map.draw[i].f - 24,
                level_map.draw[i].d + 24,
                level_map.draw[i].b - level_map.draw[i].a - 80,
                level_map.draw[i].f - level_map.draw[i].e - 48,
                level_map.draw[i].text, 0xFFF0C0, 1);
            drawn += render_world_text_plane(renderer, camera,
                level_map.draw[i].a + 40,
                level_map.draw[i].f - 24,
                level_map.draw[i].c - 24,
                level_map.draw[i].b - level_map.draw[i].a - 80,
                level_map.draw[i].f - level_map.draw[i].e - 48,
                level_map.draw[i].text, 0xFFF0C0, 0);
        }
    }
    return drawn;
}

void rasterfall_render_gallery_selection(struct toy_surface *surface,
                                         const struct camera *camera)
{
    render_gallery_selection(surface, camera);
}

int rasterfall_render_interactables(struct toy_renderer *renderer,
                                    const struct camera *camera)
{
    return render_interactables(renderer, camera);
}

int rasterfall_render_enemies(struct toy_renderer *renderer,
                              const struct camera *camera)
{
    return render_enemies(renderer, camera);
}

int rasterfall_render_ai_teammate(struct toy_renderer *renderer,
                                  const struct camera *camera)
{
    return render_ai_teammate(renderer, camera);
}

int rasterfall_render_managed_player(struct toy_renderer *renderer,
                                     const struct camera *viewer,
                                     const struct camera *body_camera)
{
    if (!renderer || !viewer || !body_camera || game.player_down) return 0;
    return render_player_avatar(renderer, viewer, body_camera->x, body_camera->z,
                                 body_camera->sy, body_camera->cy, -1, 0,
                                 RF_COLOR_UI_PLAYER, 0,
                                 game.animation.id, game.animation.time_ms);
}

int rasterfall_render_network_teammate(struct toy_renderer *renderer,
                                       const struct camera *camera,
                                       const struct rasterfall_net *net)
{
    return render_network_teammate(renderer, camera, net);
}

void rasterfall_render_ai_teammate_name(struct toy_renderer *renderer,
                                        const struct camera *camera)
{
    render_ai_teammate_name(renderer, camera);
}

void rasterfall_render_network_teammate_status(
    struct toy_renderer *renderer, const struct camera *camera,
    const struct rasterfall_net *net)
{
    render_network_teammate_status(renderer, camera, net);
}

int rasterfall_render_tracers(struct toy_renderer *renderer,
                              const struct camera *camera)
{
    return render_tracers(renderer, camera);
}

int rasterfall_render_particles(struct toy_renderer *renderer,
                                const struct camera *camera)
{
    return render_fire_zones(renderer, camera) +
           render_particles(renderer, camera);
}
