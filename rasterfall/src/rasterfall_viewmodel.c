#include "rasterfall_viewmodel.h"
#include "core.h"
#include "tlibc_everything.h"
#include "rasterfall_model.h"
#include "math.h"

struct view_vec3 { int x, y, z; };

#define VIEWMODEL_ORIGIN_X 255
#define VIEWMODEL_ORIGIN_Y (-160)
#define VIEWMODEL_ORIGIN_Z 600

/* Adjustable visual-only reload lift; gameplay reload duration is unchanged. */
#define RASTERFALL_RELOAD_VIEWMODEL_PITCH 9
#define RASTERFALL_VIEWMODEL_HAND_DEBUG 0

/* Tuning offsets for the procedural hands relative to the weapon viewmodel.
 * X is screen-right, Y is up and Z is farther from the camera.  Keep these
 * separate from the three weapon profiles so the whole arm group can be
 * nudged without retuning pistol/SMG/shotgun grip poses. */
#define RASTERFALL_VIEWMODEL_HAND_OFFSET_X 110
#define RASTERFALL_VIEWMODEL_HAND_OFFSET_Y 0
#define RASTERFALL_VIEWMODEL_HAND_OFFSET_Z 0

struct rasterfall_viewmodel_hand_pose {
    int right_wrist_x, right_wrist_y, right_wrist_z;
    int left_wrist_x, left_wrist_y, left_wrist_z;
    int right_elbow_x, left_elbow_x;
};

static struct rasterfall_model_asset viewmodel_models[TOY_GAME_WEAPON_COUNT];
static int viewmodel_models_loaded;
static const struct toy_texture_view *viewmodel_texture;

void rasterfall_viewmodel_set_texture(const struct toy_texture_view *texture)
{
    viewmodel_texture = texture;
}

static unsigned int viewmodel_model_u32(const unsigned char *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
}

static const struct rasterfall_viewmodel_hand_pose *viewmodel_hand_pose(int weapon)
{
    static const struct rasterfall_viewmodel_hand_pose pistol_pose =
        { 155, -120, 500, 80, -130, 500, 190, -85 };
    static const struct rasterfall_viewmodel_hand_pose axe_pose =
        { 115, -150, 505, -35, -185, 510, 185, -105 };
    if (weapon == TOY_GAME_WEAPON_AXE) return &axe_pose;
    return &pistol_pose;
}

static void viewmodel_bob(const struct toy_game *game, int *x, int *y)
{
    static const int wave[16] = {
        0, 8, 15, 20, 22, 20, 15, 8,
        0, -8, -15, -20, -22, -20, -15, -8
    };
    int phase;
    *x = 0; *y = 0;
    if (!game || game->animation.id != TOY_GAME_ANIM_MOVE) return;
    phase = (game->animation.time_ms / 25) & 15;
    *y = wave[phase];
    *x = wave[(phase + 4) & 15] / 2;
}

static void rotate_view_xz(int x, int z, int degrees, int *out_x, int *out_z)
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

static void viewmodel_load_models(void)
{
    static const char *paths[TOY_GAME_WEAPON_COUNT] = {
        "rasterfall/assets/models/pg_glock1.rmesh",
        "rasterfall/assets/models/smg_mac10.rmesh",
        "rasterfall/assets/models/sg_pump_action.rmesh",
        "rasterfall/assets/models/ar_ak47.rmesh",
        "rasterfall/assets/models/rf_AWP.rmesh",
        "rasterfall/assets/models/axe.rmesh",
        "rasterfall/assets/models/bomb.rmesh",
        "rasterfall/assets/models/molotov.rmesh"
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
        "rasterfall/assets/models/sg_pump_action.rmesh",
        "rasterfall/assets/models/ar_ak47.rmesh",
        "rasterfall/assets/models/rf_AWP.rmesh",
        "rasterfall/assets/models/axe.rmesh",
        "rasterfall/assets/models/bomb.rmesh",
        "rasterfall/assets/models/molotov.rmesh"
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
    int origin_x, origin_y, origin_z, origin_scale;
    /* The origin is the camera-space anchor, not part of the mesh scale.
     * Keep it fixed like the existing SMG/SG viewmodel path; moving Z here
     * changes perspective distance and cancels the visual enlargement. */
    origin_scale = 1;
    origin_x = VIEWMODEL_ORIGIN_X * origin_scale;
    origin_y = VIEWMODEL_ORIGIN_Y * origin_scale;
    origin_z = VIEWMODEL_ORIGIN_Z * origin_scale;
    viewmodel_load_models();
    if (!toy_game_weapon_is_valid(weapon) ||
        !(model = &viewmodel_models[weapon])->data) {
        *x = origin_x;
        *y = origin_y + 24 * origin_scale;
        *z = origin_z;
    } else {
        width = model->max_x - model->min_x;
        height = model->max_y - model->min_y;
        depth = model->max_z - model->min_z;
        length = width > height ? width : height;
        if (depth > length) length = depth;
        scale = (toy_game_weapon_info(weapon)->muzzle_profile ==
                 TOY_GAME_MUZZLE_SHOTGUN ? 360000 :
                 weapon == TOY_GAME_WEAPON_AWP ? 460000 :
                 weapon == TOY_GAME_WEAPON_AK ? 360000 : 180000) /
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
        *x = origin_x - muzzle_distance * 3 / 10;
        /* The mesh muzzle opening sits slightly above its bounding-box
         * center; use the upper part of the collision box as a heuristic. */
        *y = origin_y + muzzle_height + muzzle_distance * 2 / 10;
        *z = origin_z + muzzle_distance * 9 / 10;
    }
    *x += kick / 3;
    *y -= kick / 2;
    *z += kick;
}

void rasterfall_viewmodel_actor_muzzle(int x, int z, int sy, int cy,
                                       int lift, int weapon,
                                       int *out_x, int *out_y, int *out_z)
{
    /* No muzzle marker exists in the mesh format yet.  These distances are
     * measured from the weapon model's avatar-space center used by the
     * renderer, so the tracer begins near the visible barrel rather than at
     * the actor origin. */
    int distance = weapon == TOY_GAME_WEAPON_SHOTGUN ? 620 :
                   weapon == TOY_GAME_WEAPON_AWP ? 800 :
                   weapon == TOY_GAME_WEAPON_AK ? 650 :
                   weapon == TOY_GAME_WEAPON_SMG ? 500 : 430;
    *out_x = x + sy * distance / 1024;
    *out_y = -395 + lift;
    *out_z = z + cy * distance / 1024;
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

/* First-person arms are deliberately procedural for now.  Keeping the two
 * limbs as small rigid pieces gives us useful hand/weapon motion without
 * committing RFM2 to bones or skinning.  Coordinates are view-space: X is
 * right, Y is up and Z points away from the camera. */
static int draw_view_limb(struct toy_surface *surface,
                          int x0, int y0, int z0,
                          int x1, int y1, int z1,
                          int radius, uint32_t color)
{
    static const int faces[36] = {
        0,1,2, 0,2,3, 4,6,5, 4,7,6,
        0,4,5, 0,5,1, 3,2,6, 3,6,7,
        0,3,7, 0,7,4, 1,5,6, 1,6,2
    };
    struct view_vec3 v[8];
    int dy = y1 - y0, dz = z1 - z0;
    int length = (int)isqrt((long long)dy * dy + (long long)dz * dz);
    int py, pz, i, drawn = 0;
    if (length <= 0) return 0;
    /* A perpendicular in the Y/Z plane makes a rounded-looking rectangular
     * forearm while preserving the cheap triangle-only renderer. */
    py = -dz * radius / length;
    pz = dy * radius / length;
    v[0].x = x0 - radius; v[1].x = x0 + radius;
    v[2].x = x0 + radius; v[3].x = x0 - radius;
    v[4].x = x1 - radius; v[5].x = x1 + radius;
    v[6].x = x1 + radius; v[7].x = x1 - radius;
    v[0].y = y0 + py; v[0].z = z0 + pz;
    v[1].y = y0 + py; v[1].z = z0 + pz;
    v[2].y = y0 - py; v[2].z = z0 - pz;
    v[3].y = y0 - py; v[3].z = z0 - pz;
    v[4].y = y1 + py; v[4].z = z1 + pz;
    v[5].y = y1 + py; v[5].z = z1 + pz;
    v[6].y = y1 - py; v[6].z = z1 - pz;
    v[7].y = y1 - py; v[7].z = z1 - pz;
    for (i = 0; i < 36; i += 3) {
        int n, sx[3], sy[3];
        const struct view_vec3 *a = &v[faces[i]];
        const struct view_vec3 *b = &v[faces[i + 1]];
        const struct view_vec3 *c = &v[faces[i + 2]];
        const struct view_vec3 *points[3] = { a, b, c };
        for (n = 0; n < 3; n++) {
            if (points[n]->z < 192) break;
            sx[n] = surface->width / 2 + points[n]->x *
                    (surface->width * 3 / 4) / points[n]->z;
            sy[n] = surface->height / 2 - points[n]->y *
                    (surface->width * 3 / 4) / points[n]->z;
        }
        if (n == 3) {
            int face = i / 6;
            uint32_t shade = color + (face & 3) * 0x040404;
            drawn += fill_triangle_2d(surface, sx[0], sy[0], sx[1], sy[1],
                                      sx[2], sy[2], shade);
        }
    }
    return drawn;
}

static int render_viewmodel_hands(struct toy_surface *surface,
                                  const struct toy_game *game,
                                  int kick)
{
    const struct rasterfall_viewmodel_hand_pose *profile;
    int bob_y, bob_x, reload = 0, weapon;
    int r_elbow_y, r_wrist_y, l_elbow_y, l_wrist_y;
    int r_wrist_x, l_wrist_x, r_wrist_z, l_wrist_z;
    int r_elbow_x, l_elbow_x;
    int r_elbow_z = 570, l_elbow_z = 560;
    int reload_ms = 900;
    int hand_kick_right, hand_kick_left;
    int fire_pulse = 0, reload_slap = 0;
    int drawn = 0;
    if (!surface || !game) return 0;
    (void)kick;
    weapon = rasterfall_viewmodel_weapon(game);
    profile = viewmodel_hand_pose(weapon);
    r_wrist_x = profile->right_wrist_x;
    r_wrist_y = profile->right_wrist_y;
    r_wrist_z = profile->right_wrist_z;
    l_wrist_x = profile->left_wrist_x;
    l_wrist_y = profile->left_wrist_y;
    l_wrist_z = profile->left_wrist_z;
    r_elbow_x = profile->right_elbow_x;
    l_elbow_x = profile->left_elbow_x;
    r_elbow_y = -212;
    l_elbow_y = -210;
    r_wrist_x += RASTERFALL_VIEWMODEL_HAND_OFFSET_X;
    l_wrist_x += RASTERFALL_VIEWMODEL_HAND_OFFSET_X;
    r_wrist_y += RASTERFALL_VIEWMODEL_HAND_OFFSET_Y;
    l_wrist_y += RASTERFALL_VIEWMODEL_HAND_OFFSET_Y;
    r_wrist_z += RASTERFALL_VIEWMODEL_HAND_OFFSET_Z;
    l_wrist_z += RASTERFALL_VIEWMODEL_HAND_OFFSET_Z;
    r_elbow_x += RASTERFALL_VIEWMODEL_HAND_OFFSET_X;
    l_elbow_x += RASTERFALL_VIEWMODEL_HAND_OFFSET_X;
    r_elbow_y += RASTERFALL_VIEWMODEL_HAND_OFFSET_Y;
    l_elbow_y += RASTERFALL_VIEWMODEL_HAND_OFFSET_Y;
    r_elbow_z += RASTERFALL_VIEWMODEL_HAND_OFFSET_Z;
    l_elbow_z += RASTERFALL_VIEWMODEL_HAND_OFFSET_Z;
    viewmodel_bob(game, &bob_x, &bob_y);
    if (game->animation.id == TOY_GAME_ANIM_SHOVE) {
        int phase = game->animation.time_ms * 1000 /
                    toy_game_animation_info(TOY_GAME_ANIM_SHOVE)->duration_ms;
        int dx, dz, angle;
        if (phase > 1000) phase = 1000;
        angle = phase * TOY_CONFIG_SHOVE_SWEEP_DEGREES / 1000;
        dx = l_wrist_x - l_elbow_x;
        dz = l_wrist_z - l_elbow_z;
        rotate_view_xz(dx, dz, angle, &dx, &dz);
        l_wrist_x = l_elbow_x + dx;
        l_wrist_z = l_elbow_z + dz;
    } else if (game->animation.id == TOY_GAME_ANIM_MELEE) {
        int phase = game->animation.time_ms * 1000 /
                    TOY_CONFIG_MELEE_SWING_MS;
        if (phase > 1000) phase = 1000;
        r_wrist_x -= phase * 230 / 1000;
        r_wrist_y += phase * 150 / 1000;
        r_wrist_z += phase * 410 / 1000;
        l_wrist_x -= phase * 145 / 1000;
        l_wrist_y += phase * 115 / 1000;
        l_wrist_z += phase * 300 / 1000;
    } else if (game->animation.id == TOY_GAME_ANIM_THROW) {
        int phase = game->animation.time_ms * 1000 /
                    TOY_CONFIG_THROW_HANDOFF_MS;
        if (phase > 1000) phase = 1000;
        r_wrist_y += phase * 240 / 1000;
        r_wrist_z += phase * 260 / 1000;
        r_elbow_y += phase * 120 / 1000;
    } else if (game->animation.id == TOY_GAME_ANIM_RELOAD) {
        if (weapon >= 0) reload_ms = toy_game_weapon_info(weapon)->reload_ms;
        reload = game->animation.time_ms * 1000 / reload_ms;
        if (reload > 1000) reload = 1000;
        /* Smooth up/down reload arc. */
        {
            int phase = reload < 500 ? reload * 2 : (1000 - reload) * 2;
            r_wrist_y += phase * 35 / 1000;
            l_wrist_x -= phase * 145 / 1000;
            l_wrist_y = -330 + phase * 105 / 1000;
            l_wrist_z -= phase * 35 / 1000;
            l_elbow_y -= phase * 35 / 1000;
        }
        /* Final reload beat: the left hand gives the receiver/body a short
         * confirming slap, then returns to the grip before the clip ends. */
        if (reload > 820) {
            int tap = (reload - 820) * 1000 / 180;
            reload_slap = tap < 500 ? tap * 2 : (1000 - tap) * 2;
            l_wrist_x += 42 * reload_slap / 1000;
            l_wrist_y += 18 * reload_slap / 1000;
            l_wrist_z -= 58 * reload_slap / 1000;
        }
    }
    /* Keep firearm recoil on the weapon itself.  Hands get only a small
     * upward visual pulse during FIRE, so they stay attached to the grip. */
    if (game->animation.id == TOY_GAME_ANIM_FIRE) {
        int pulse = game->animation.time_ms < 45 ?
                    game->animation.time_ms * 1000 / 45 :
                    game->animation.time_ms < 110 ?
                    (110 - game->animation.time_ms) * 1000 / 65 : 0;
        fire_pulse = pulse * 12 / 1000;
    }
    hand_kick_right = fire_pulse;
    hand_kick_left = fire_pulse * 85 / 100;
    r_elbow_y += bob_y + hand_kick_right * 40 / 100;
    l_elbow_y += bob_y + hand_kick_left * 40 / 100;
    r_wrist_y += bob_y + hand_kick_right * 70 / 100;
    l_wrist_y += bob_y + hand_kick_left * 65 / 100;
    r_wrist_x += bob_x + hand_kick_right / 3;
    l_wrist_x += bob_x + hand_kick_left / 4;
    r_elbow_x += bob_x;
    l_elbow_x += bob_x;
    r_wrist_z += hand_kick_right;
    l_wrist_z += hand_kick_left;
    /* Forearms. */
    drawn += draw_view_limb(surface, r_elbow_x, r_elbow_y, r_elbow_z,
                            r_wrist_x, r_wrist_y, r_wrist_z, 31, 0xC58B6C);
    drawn += draw_view_limb(surface, l_elbow_x, l_elbow_y, l_elbow_z,
                            l_wrist_x, l_wrist_y, l_wrist_z, 31, 0xC58B6C);
#if RASTERFALL_VIEWMODEL_HAND_DEBUG
    {
        int focal = surface->width * 3 / 4;
        int sx, sy;
        sx = surface->width / 2 + r_elbow_x * focal / r_elbow_z;
        sy = surface->height / 2 - r_elbow_y * focal / r_elbow_z;
        fill_rect(surface, sx - 2, sy - 2, 5, 5, 0xFF3030);
        sx = surface->width / 2 + r_wrist_x * focal / r_wrist_z;
        sy = surface->height / 2 - r_wrist_y * focal / r_wrist_z;
        fill_rect(surface, sx - 2, sy - 2, 5, 5, 0xFFFF40);
        sx = surface->width / 2 + l_elbow_x * focal / l_elbow_z;
        sy = surface->height / 2 - l_elbow_y * focal / l_elbow_z;
        fill_rect(surface, sx - 2, sy - 2, 5, 5, 0x40A0FF);
        sx = surface->width / 2 + l_wrist_x * focal / l_wrist_z;
        sy = surface->height / 2 - l_wrist_y * focal / l_wrist_z;
        fill_rect(surface, sx - 2, sy - 2, 5, 5, 0x40FFFF);
    }
#endif
    return drawn;
}

static int render_model_weapon(struct toy_renderer *renderer,
                               const struct rasterfall_model_asset *model,
                               int weapon, int kick,
                               int animation_id, int animation_time_ms,
                               int bob_x, int bob_y, int switch_pitch)
{
    int i, drawn = 0, width, height, depth, length, scale;
    int origin_x, origin_y, origin_z, origin_scale;
    int reload_pitch = 0;
    int axe_rotation = 0;
    int axe_swing_x = 0, axe_swing_z = 0;
    struct toy_surface *surface = &renderer->surface;
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
        reload_pitch = arc * RASTERFALL_RELOAD_VIEWMODEL_PITCH / 1000;
    }
    if (weapon == TOY_GAME_WEAPON_AXE) {
        /* Present the axe handle across the chest; during the swing it
         * advances and turns left to read as a real slash. */
        axe_rotation = 90;
        if (animation_id == TOY_GAME_ANIM_MELEE) {
            int phase = animation_time_ms * 1000 / TOY_CONFIG_MELEE_SWING_MS;
            if (phase > 1000) phase = 1000;
            axe_rotation += phase * 70 / 1000;
            axe_swing_x = -phase * 190 / 1000;
            axe_swing_z = phase * 300 / 1000;
        }
    }
    if (switch_pitch > reload_pitch)
        reload_pitch = switch_pitch;
    /* Keep the imported model in 3D view space.  The gun starts at the
     * lower-right and its forward axis travels left/up toward the crosshair. */
    scale = (weapon == TOY_GAME_WEAPON_AXE ? 450000 :
             weapon == TOY_GAME_WEAPON_SHOTGUN ? 360000 :
             weapon == TOY_GAME_WEAPON_AWP ? 460000 :
             weapon == TOY_GAME_WEAPON_AK ? 360000 : 180000) / length;
    if (scale < 1) scale = 1;
    /* AK/AWP are rendered at twice the normal profile size.  Keep the
     * hand-tuned viewmodel origin in the same scale, so enlarging the mesh
     * does not leave its empirical grip offset behind. */
    /* Keep the camera-space origin fixed while scaling the mesh vertices. */
    origin_scale = 1;
    origin_x = (weapon == TOY_GAME_WEAPON_AXE ? 120 : VIEWMODEL_ORIGIN_X) *
               origin_scale;
    origin_y = VIEWMODEL_ORIGIN_Y * origin_scale;
    origin_z = VIEWMODEL_ORIGIN_Z * origin_scale;
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
                } else if (weapon == TOY_GAME_WEAPON_AK ||
                           weapon == TOY_GAME_WEAPON_AWP) {
                    /* Reverse the asset's forward direction: rotate 180°
                     * around Y, so X/Z flip while vertical Y stays intact. */
                    v[k].x = -mx + (model->min_x + model->max_x) / 2;
                    v[k].y = my - (model->min_y + model->max_y) / 2;
                    v[k].z = -mz + (model->min_z + model->max_z) / 2;
                } else {
                    v[k].x = mx - (model->min_x + model->max_x) / 2;
                    v[k].y = my - (model->min_y + model->max_y) / 2;
                    v[k].z = mz - (model->min_z + model->max_z) / 2;
                }
                {
                    int local_x = v[k].x * scale / 1000;
                    int local_y = v[k].y * scale / 1000;
                    int local_z = v[k].z * scale / 1000;
                    if (weapon == TOY_GAME_WEAPON_AXE) {
                        rotate_view_xz(local_x, local_z, axe_rotation,
                                       &local_x, &local_z);
                        local_x += axe_swing_x;
                        local_z += axe_swing_z;
                    }
                    /* A forward point moves toward screen center and upward;
                     * this is a real view-space yaw/pitch, not a flat 2D
                     * side-profile shear. */
                    v[k].x = origin_x + bob_x + local_x -
                             local_z * 3 / 10 + kick / 3;
                    v[k].y = origin_y + bob_y + local_y +
                             local_z * (2 + reload_pitch) / 10 - kick / 2;
                    v[k].z = origin_z + local_z * 9 / 10 + local_x / 8 + kick;
                }
            }
            if (k == 3) {
                int sx[3], sy[3], n;
                struct toy_screen_vertex sv[3];
                for (n = 0; n < 3; n++) {
                    if (v[n].z < 192) break;
                    sx[n] = surface->width / 2 + v[n].x * focal / v[n].z;
                    sy[n] = surface->height / 2 - v[n].y * focal / v[n].z;
                    sv[n].x = sx[n]; sv[n].y = sy[n]; sv[n].z = v[n].z;
                    sv[n].u = *(const unsigned short *)(model->vertices +
                                      ids[n] * RASTERFALL_MODEL_VERTEX_BYTES + 18);
                    sv[n].v = *(const unsigned short *)(model->vertices +
                                      ids[n] * RASTERFALL_MODEL_VERTEX_BYTES + 20);
                    sv[n].inv_z = (long)1048576 / v[n].z;
                    sv[n].u_over_z = (long)sv[n].u * 1048576L / v[n].z;
                    sv[n].v_over_z = (long)sv[n].v * 1048576L / v[n].z;
                    sv[n].light = 256; sv[n].fog = 0;
                }
                if (n == 3) {
                    if (weapon >= TOY_GAME_WEAPON_AXE && viewmodel_texture &&
                        viewmodel_texture->data)
                        drawn += toy_renderer_triangle_textured_lit(
                            renderer, &sv[0], &sv[1], &sv[2],
                            viewmodel_texture, 1, color, 256, 0);
                    else
                        drawn += fill_triangle_2d(surface, sx[0], sy[0],
                                                  sx[1], sy[1], sx[2], sy[2],
                                                  color);
                }
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
    int bob_x, bob_y, switch_pitch = 0;
    int drawn = 0;
    viewmodel_load_models();
    viewmodel_bob(game, &bob_x, &bob_y);
    if (game->weapon_switch_timer_ms > 0)
        switch_pitch = game->weapon_switch_timer_ms *
                       RASTERFALL_RELOAD_VIEWMODEL_PITCH /
                       TOY_CONFIG_WEAPON_SWITCH_MS;
    drawn += render_viewmodel_hands(&renderer->surface, game, kick);
    if (weapon >= TOY_GAME_WEAPON_PISTOL &&
        weapon < TOY_GAME_WEAPON_COUNT &&
        viewmodel_models[weapon].data) {
        drawn += render_model_weapon(renderer,
                                     &viewmodel_models[weapon], weapon, kick,
                                     game->animation.id,
                                     game->animation.time_ms, bob_x, bob_y,
                                     switch_pitch);
    }
    return drawn;
}
