#include "rasterfall_viewmodel.h"
#include "core.h"
#include "tlibc_everything.h"
#include "rasterfall_model.h"

struct view_vec3 { int x, y, z; };

#define VIEWMODEL_ORIGIN_X 255
#define VIEWMODEL_ORIGIN_Y (-160)
#define VIEWMODEL_ORIGIN_Z 600

static struct rasterfall_model_asset viewmodel_models[TOY_GAME_WEAPON_COUNT];
static int viewmodel_models_loaded;

static unsigned int viewmodel_model_u32(const unsigned char *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
}

static void viewmodel_load_models(void)
{
    static const char *paths[TOY_GAME_WEAPON_COUNT] = {
        "rasterfall/assets/models/pg_glock1.rmesh",
        "rasterfall/assets/models/smg_mac10.rmesh",
        "rasterfall/assets/models/sg_pump_action.rmesh"
    };
    int i;
    if (viewmodel_models_loaded) return;
    for (i = 0; i < TOY_GAME_WEAPON_COUNT; i++)
        if (paths[i]) rasterfall_model_load(&viewmodel_models[i], paths[i]);
    viewmodel_models_loaded = 1;
}

const char *rasterfall_weapon_model_path(int weapon)
{
    static const char *paths[TOY_GAME_WEAPON_COUNT] = {
        "rasterfall/assets/models/pg_glock1.rmesh",
        "rasterfall/assets/models/smg_mac10.rmesh",
        "rasterfall/assets/models/sg_pump_action.rmesh"
    };
    if (weapon < 0 || weapon >= TOY_GAME_WEAPON_COUNT) return NULL;
    return paths[weapon];
}

int rasterfall_viewmodel_weapon(const struct toy_game *game)
{
    int slot = game->current_slot;
    if (slot < 0 || slot >= TOY_GAME_WEAPON_SLOTS) return -1;
    return game->slots[slot].weapon;
}

void rasterfall_viewmodel_muzzle_offset(int weapon, int kick,
                                        int *x, int *y, int *z)
{
    struct rasterfall_model_asset *model;
    int width, height, depth, length, scale, muzzle_distance, muzzle_height;
    viewmodel_load_models();
    if (!toy_game_weapon_is_valid(weapon) ||
        !(model = &viewmodel_models[weapon])->data) {
        *x = VIEWMODEL_ORIGIN_X;
        *y = VIEWMODEL_ORIGIN_Y + 24;
        *z = VIEWMODEL_ORIGIN_Z;
    } else {
        width = model->max_x - model->min_x;
        height = model->max_y - model->min_y;
        depth = model->max_z - model->min_z;
        length = width > height ? width : height;
        if (depth > length) length = depth;
        scale = (toy_game_weapon_info(weapon)->muzzle_profile ==
                 TOY_GAME_MUZZLE_SHOTGUN ? 360000 : 180000) /
                (length > 0 ? length : 1);
        if (scale < 1) scale = 1;
        /* All three imported meshes use their positive forward extreme as
         * the muzzle: Z for PG/SMG, X for SG.  There is no muzzle marker in
         * RFM2 yet, so put the origin near the top of the model bounds: this
         * is a useful empirical approximation for the visible barrel. */
        muzzle_distance = toy_game_weapon_info(weapon)->muzzle_profile ==
            TOY_GAME_MUZZLE_SHOTGUN ?
            (model->max_x - model->min_x) / 2 :
            (model->max_z - model->min_z) / 2;
        muzzle_distance = muzzle_distance * scale / 1000;
        muzzle_height = (model->max_y -
                         (model->min_y + model->max_y) / 2) * scale * 9 /
                        10 / 1000;
        *x = VIEWMODEL_ORIGIN_X - muzzle_distance * 3 / 10;
        /* The mesh muzzle opening sits slightly above its bounding-box
         * center; use the upper part of the collision box as a heuristic. */
        *y = VIEWMODEL_ORIGIN_Y + muzzle_height + muzzle_distance * 2 / 10;
        *z = VIEWMODEL_ORIGIN_Z + muzzle_distance * 9 / 10;
    }
    *x += kick / 3;
    *y -= kick / 2;
    *z += kick;
}

void rasterfall_viewmodel_actor_muzzle(int x, int z, int sy, int cy,
                                       int lift, int weapon,
                                       int *out_x, int *out_y, int *out_z)
{
    /* Third-person weapons use the same forward distance as the avatar's
     * fixed side mount.  The vertical origin follows airborne lift, so a
     * platform/jump cannot leave the tracer hanging at ground height. */
    (void)weapon;
    *out_x = x + sy * 130 / 1024;
    *out_y = -430 + lift;
    *out_z = z + cy * 130 / 1024;
}

static int fill_triangle_2d(struct toy_surface *surface,
                            int x0, int y0, int x1, int y1, int x2, int y2,
                            uint32_t color)
{
    int y, tmp, xa, xb, ymin, ymax, drawn = 0;
    long dx01 = 0, dx02, dx12 = 0, xl, xr, lt;
    if (y0 > y1) { tmp=x0; x0=x1; x1=tmp; tmp=y0; y0=y1; y1=tmp; }
    if (y1 > y2) { tmp=x1; x1=x2; x2=tmp; tmp=y1; y1=y2; y2=tmp; }
    if (y0 > y1) { tmp=x0; x0=x1; x1=tmp; tmp=y0; y0=y1; y1=tmp; }
    if (y0 == y2 || y2 < 0 || y0 >= surface->height) return 0;
    if (y1 > y0) dx01 = (long)(x1-x0) * 65536 / (y1-y0);
    dx02 = (long)(x2-x0) * 65536 / (y2-y0);
    if (y2 > y1) dx12 = (long)(x2-x1) * 65536 / (y2-y1);
    ymin = y0 < 0 ? 0 : y0;
    ymax = y2 >= surface->height ? surface->height - 1 : y2;
    for (y = ymin; y <= ymax; y++) {
        if (y <= y1) {
            xl = (long)x0 * 65536 + dx01 * (y-y0);
            xr = (long)x0 * 65536 + dx02 * (y-y0);
        } else {
            xl = (long)x1 * 65536 + dx12 * (y-y1);
            xr = (long)x0 * 65536 + dx02 * (y-y0);
        }
        if (xl > xr) { lt=xl; xl=xr; xr=lt; }
        xa = (int)(xl >> 16); xb = (int)(xr >> 16);
        if (xa < 0) xa = 0;
        if (xb >= surface->width) xb = surface->width - 1;
        if (xa <= xb) {
            uint32_t *row = (uint32_t *)((unsigned char *)surface->pixels +
                                         y * surface->stride);
            int x;
            for (x = xa; x <= xb; x++) row[x] = color;
            drawn += xb - xa + 1;
        }
    }
    return drawn;
}

static int render_model_weapon(struct toy_surface *surface,
                               const struct rasterfall_model_asset *model,
                               int weapon, int kick,
                               int animation_id, int animation_time_ms)
{
    int i, drawn = 0, width, height, depth, length, scale;
    int reload_pitch = 0;
    int focal = surface->width * 3 / 4;
    if (!model || !model->data) return 0;
    width = model->max_x - model->min_x;
    height = model->max_y - model->min_y;
    depth = model->max_z - model->min_z;
    length = width > height ? width : height;
    if (depth > length) length = depth;
    if (length <= 0) return 0;
    if (animation_id == TOY_GAME_ANIM_RELOAD) {
        int phase = animation_time_ms * 1000 /
                    toy_game_weapon_info(weapon)->reload_ms;
        int arc = phase < 500 ? phase * 2 : (1000 - phase) * 2;
        if (arc < 0) arc = 0;
        if (arc > 1000) arc = 1000;
        reload_pitch = arc * 3 / 1000;
    }
    /* Keep the imported model in 3D view space.  The gun starts at the
     * lower-right and its forward axis travels left/up toward the crosshair. */
    scale = (weapon == TOY_GAME_WEAPON_SHOTGUN ? 360000 : 180000) / length;
    if (scale < 1) scale = 1;
    for (i = 0; i < (int)model->primitive_count; i++) {
        const unsigned char *primitive = model->primitives +
            i * RASTERFALL_MODEL_PRIMITIVE_BYTES;
        const unsigned char *indices = model->indices +
            viewmodel_model_u32(primitive) * 4;
        unsigned int index_count = viewmodel_model_u32(primitive + 4);
        unsigned int material = viewmodel_model_u32(primitive + 8);
        uint32_t color = material < model->material_count ?
            viewmodel_model_u32(model->materials +
                                material * RASTERFALL_MODEL_MATERIAL_BYTES) :
            0xA0A0A0;
        unsigned int j;
        for (j = 0; j + 2 < index_count; j += 3) {
            unsigned int ids[3] = {
                viewmodel_model_u32(indices + j * 4),
                viewmodel_model_u32(indices + (j + 1) * 4),
                viewmodel_model_u32(indices + (j + 2) * 4)
            };
            struct view_vec3 v[3];
            int k;
            for (k = 0; k < 3; k++) {
                const unsigned char *p;
                int mx, my, mz;
                if (ids[k] >= model->vertex_count) break;
                p = model->vertices + ids[k] * RASTERFALL_MODEL_VERTEX_BYTES;
                mx = *(const int *)(p);
                my = *(const int *)(p + 4);
                mz = *(const int *)(p + 8);
                /* Convert each asset's native axes to: X=right, Y=up,
                 * Z=forward.  The native forward axis is Z for PG/SMG and X
                 * for SG. */
                if (weapon == TOY_GAME_WEAPON_PISTOL) {
                    v[k].x = mx - (model->min_x + model->max_x) / 2;
                    v[k].y = my - (model->min_y + model->max_y) / 2;
                    v[k].z = mz - (model->min_z + model->max_z) / 2;
                } else if (weapon == TOY_GAME_WEAPON_SHOTGUN) {
                    v[k].x = mz - (model->min_z + model->max_z) / 2;
                    v[k].y = my - (model->min_y + model->max_y) / 2;
                    v[k].z = mx - (model->min_x + model->max_x) / 2;
                } else {
                    v[k].x = mx - (model->min_x + model->max_x) / 2;
                    v[k].y = my - (model->min_y + model->max_y) / 2;
                    v[k].z = mz - (model->min_z + model->max_z) / 2;
                }
                {
                    int local_x = v[k].x * scale / 1000;
                    int local_y = v[k].y * scale / 1000;
                    int local_z = v[k].z * scale / 1000;
                    /* A forward point moves toward screen center and upward;
                     * this is a real view-space yaw/pitch, not a flat 2D
                     * side-profile shear. */
                    v[k].x = VIEWMODEL_ORIGIN_X + local_x - local_z * 3 / 10 + kick / 3;
                    v[k].y = VIEWMODEL_ORIGIN_Y + local_y + local_z * (2 + reload_pitch) / 10 -
                             kick / 2;
                    v[k].z = VIEWMODEL_ORIGIN_Z + local_z * 9 / 10 + local_x / 8 + kick;
                }
            }
            if (k == 3) {
                int sx[3], sy[3], n;
                for (n = 0; n < 3; n++) {
                    if (v[n].z < 192) break;
                    sx[n] = surface->width / 2 + v[n].x * focal / v[n].z;
                    sy[n] = surface->height / 2 - v[n].y * focal / v[n].z;
                }
                if (n == 3)
                    drawn += fill_triangle_2d(surface, sx[0], sy[0],
                                              sx[1], sy[1], sx[2], sy[2],
                                              color);
            }
        }
    }
    return drawn;
}

int rasterfall_viewmodel_render(struct toy_renderer *renderer,
                                const struct toy_game *game,
                                const struct rasterfall_effects *effects)
{
    int weapon = rasterfall_viewmodel_weapon(game);
    int kick = effects->weapon_kick;
    viewmodel_load_models();
    if (weapon >= TOY_GAME_WEAPON_PISTOL &&
        weapon <= TOY_GAME_WEAPON_SHOTGUN &&
        viewmodel_models[weapon].data) {
        return render_model_weapon(&renderer->surface,
                                   &viewmodel_models[weapon], weapon, kick,
                                   game->animation.id,
                                   game->animation.time_ms);
    }
    return 0;
}
