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
#include "rasterfall_render_frontend.h"

#define special_target_active ability.special_target_active
#define charge_active ability.charge_active
#define charge_elapsed_ms ability.charge_elapsed_ms
#define special_target_kind ability.special_target_kind
#define special_target_index ability.special_target_index
#include "rasterfall_model.h"
#include "rasterfall_viewmodel.h"
#include "rasterfall_animation.h"
#include "rasterfall_actor_animation.h"
#include "rasterfall_animation_composition.h"
#include "rasterfall_character.h"
#include "rasterfall_units.h"
#include "rasterfall_glb_animation.h"
#include "rasterfall_glb_preview.h"
#include "rasterfall_vmd.h"
#include "math.h"

#define NEAR_Z RASTERFALL_NEAR_Z
#define ENEMY_RENDER_DISTANCE 24000
#define UV_ONE 65536
#define BAKED_LM_W 32
#define BAKED_LM_H 24
#define RASTERFALL_CHARACTER_NEAR_RFU 1536
#define RASTERFALL_CHARACTER_MID_RFU 4096
#define RASTERFALL_CHARACTER_FAR_RFU 15360

struct vec3 { int x, y, z; };
struct box { int minx, maxx, minz, maxz, height; uint32_t color; };

static struct rasterfall_render_context *render_ctx;
static struct rasterfall_scene_stats scene_stats;
struct gallery_cached_vertex;
#define frontend_state() rasterfall_render_frontend_current(renderer)
#define frontend_set_override rasterfall_render_frontend_set_override
#define frontend_bind_worker rasterfall_render_frontend_bind_worker
#define frontend_unbind_worker rasterfall_render_frontend_unbind_worker
#define model_setup_timing (frontend_state()->timing)
#define active_sphere_texture (frontend_state()->sphere_texture)
#define active_sphere_mode (frontend_state()->sphere_mode)
#define active_toon_texture (frontend_state()->toon_texture)
#define active_toon_shared (frontend_state()->toon_shared)
#define active_toon_level (frontend_state()->toon_level)
#define active_material_alpha (frontend_state()->material_alpha)
#define active_material_double_sided (frontend_state()->material_double_sided)
#define active_material_ambient (frontend_state()->material_ambient)
#define active_material_specular (frontend_state()->material_specular)
#define active_material_specular_power (frontend_state()->material_specular_power)
#define active_material_specular_level (frontend_state()->material_specular_level)
#define active_face_material (frontend_state()->face_material)
#define active_skin_material (frontend_state()->skin_material)
#define active_material_tint (frontend_state()->material_tint)
#define active_model_triangle_stats (frontend_state()->model_triangle_stats)
#define gallery_vertex_cache (frontend_state()->vertex_cache)
#define gallery_vertex_cache_capacity (frontend_state()->vertex_cache_capacity)
#define active_disable_edge (frontend_state()->disable_edge)
#define active_disable_sphere (frontend_state()->disable_sphere)
#define active_disable_toon (frontend_state()->disable_toon)
#define active_gallery_facing (frontend_state()->gallery_facing)
#define active_gallery_sy (frontend_state()->gallery_sy)
#define active_gallery_cy (frontend_state()->gallery_cy)

static long render_monotonic_us(void)
{
    struct timespec now;
    if (__clock_gettime(CLOCK_MONOTONIC, &now) < 0) return 0;
    return now.tv_sec * 1000000L + now.tv_nsec / 1000;
}
static struct rasterfall_session *active_session;
static int active_pose_preview;
static int edge_pass_enabled = 1;
static struct rasterfall_effects *active_effects;
static const struct rasterfall_net *active_net;
static const struct toy_texture_view *active_wall_texture;
static const struct toy_texture_view *active_model_texture;
static unsigned short *active_lightmap;
static int active_textures;
static int active_fixed_floor_lighting;
static int active_enemy_lift;
static int active_actor_lift;
static int active_actor_roll_sin;
static int active_actor_roll_cos = 1024;
static int active_gallery_lighting;
static int active_disable_material_light;
static int active_coordinate_axes;
static struct rasterfall_model_render_stats model_render_stats;
static int collect_model_render_stats;

#define level_map active_session->level
#define game active_session->game_state
#define interactables active_session->items
#define interactable_count active_session->item_count
#define baked_lightmap active_lightmap
#define baked_light_at_render(x, z) baked_light_at(x, z)
#define fixed_floor_lighting active_fixed_floor_lighting
#define textures_enabled active_textures
#define active_texture_view (frontend_state()->texture_view)
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
static int draw_world_triangle_views(struct toy_renderer *renderer,
                               const struct camera *camera,
                               const struct vec3 *a, const struct vec3 *b,
                               const struct vec3 *c,
                               const struct vec3 *va, const struct vec3 *vb,
                               const struct vec3 *vc, uint32_t color);
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
static int draw_world_triangle_tex_views(struct toy_renderer *renderer,
                                   const struct camera *camera,
                                   const struct world_uv_vertex *a,
                                   const struct world_uv_vertex *b,
                                   const struct world_uv_vertex *c,
                                   const struct vec3 *va,
                                   const struct vec3 *vb,
                                   const struct vec3 *vc);
static void rotate_arm_xz(int x, int z, int degrees, int *out_x, int *out_z);
static int gallery_model_scale(const struct rasterfall_model_asset *model);
static int character_model_scale(const struct rasterfall_model_asset *model,
                                 int target_height_mm);
static int render_gallery_model(struct toy_renderer *renderer,
                                const struct camera *camera,
                                const struct rasterfall_model_asset *model,
                                int center_x, int base_y, int center_z,
                                int scale);
struct rasterfall_skeletal_actor_profile;
static void weapon_profile_local(const struct rasterfall_pose_calibration *p,
                                 const int in[3], int out[3]);
static const struct rasterfall_pose_calibration *render_pose_calibration(int weapon);
static int rifle_frame_transform(const struct rasterfall_model_asset *, struct rasterfall_model_attachment_transform *);
static void rifle_solve_hands(struct rasterfall_model_asset *, const struct rasterfall_model_attachment_transform *, const struct rasterfall_pose_calibration *, const struct rasterfall_weapon_asset_profile *, int);
static int render_skeletal_rifle(
    struct toy_renderer *renderer, const struct camera *camera,
    const struct rasterfall_model_asset *character,
    const struct rasterfall_model_attachment_transform *rifle_frame,
    const struct rasterfall_skeletal_actor_profile *profile,
    const struct rasterfall_pose_calibration *calibration,
    int actor_x, int base_y, int actor_z, int actor_sy, int actor_cy,
    int weapon, int muzzle_flash);

static struct rasterfall_model_asset gallery_models[RASTERFALL_MODEL_MAX_GALLERY];
static int gallery_loaded;
static struct rasterfall_model_asset private_character_model;
static struct rasterfall_model_asset private_character_lod_model;
static int private_character_lod_loaded;
static struct rasterfall_model_asset private_character_lod2_model;
static int private_character_lod2_loaded;
static struct rasterfall_vmd_clip private_character_vmd;
static struct rasterfall_animation_clip private_character_vmd_clip;
static struct rasterfall_animation_track private_character_vmd_tracks[RASTERFALL_VMD_MAX_BONES];
static const char *private_character_override_path;
static const char *private_character_vmd_path;
static char private_character_vmd_loaded_path[512];
static int private_character_vmd_forced;
static int private_character_vmd_freeze_head;
static int private_character_vmd_freeze_torso;
static int private_character_vmd_ik_enabled = 1;
static int private_character_vmd_grant_enabled = 1;
static int private_character_vmd_legacy_root_offset;
static int private_character_vmd_legacy_knee_ccd;
static int private_character_vmd_skin_trace;
static int private_character_vmd_loaded;
static struct rasterfall_glb_rotation_clip private_character_glb[3];
static struct rasterfall_glb_rotation_reference private_character_glb_reference;
static struct rasterfall_animation_clip private_character_timing[3];
static int private_character_glb_loaded[3];
static struct rasterfall_glb_preview quaternius_preview;
static int quaternius_preview_loaded;
static int quaternius_preview_clip = -1;

static const char *private_character_clip_names[3] = {
    "Idle_Loop", "Walk_Loop", "Jog_Fwd_Loop"
};

static int private_character_load_glb_clip(int index)
{
    static const char *paths[] = {
        "rasterfall/private-assets/models/UAL1_Standard.glb",
        "rasterfall/private-assets/UAL1_Standard.glb"
    };
    int path;
    if (index < 0 || index >= 3) return -1;
    if (private_character_glb_loaded[index])
        return private_character_glb[index].duration_ms > 0 ? 0 : -1;
    private_character_glb_loaded[index] = 1;
    for (path = 0; path < 2; path++)
        if (rasterfall_glb_rotation_clip_load(&private_character_glb[index],
                                              paths[path],
                                              private_character_clip_names[index]) == 0)
            break;
    if (path == 2) return -1;
    if (index == 0 && rasterfall_glb_rotation_reference_build(
            &private_character_glb[0],&private_character_glb_reference) < 0)
        return -1;
    private_character_timing[index].duration_ms = private_character_glb[index].duration_ms;
    private_character_timing[index].loop = 1;
    private_character_timing[index].tracks = NULL;
    private_character_timing[index].track_count = 0;
    return 0;
}
static int private_character_loaded;
static struct toy_renderer private_character_commands;
static struct rasterfall_frontend_state private_character_frontend;
static int private_character_commands_initialized;
static long private_character_animation_us;

static void sync_private_character_lod_pose(void)
{
    if (!private_character_lod_loaded || !private_character_model.bone_transforms ||
        !private_character_lod_model.bone_transforms ||
        private_character_model.bone_count != private_character_lod_model.bone_count)
        return;
    memcpy(private_character_lod_model.bone_transforms,
           private_character_model.bone_transforms,
           private_character_model.bone_count *
               sizeof(*private_character_model.bone_transforms));
}
static void sync_private_character_lod2_pose(void)
{
    if (!private_character_lod2_loaded || !private_character_model.bone_transforms ||
        !private_character_lod2_model.bone_transforms ||
        private_character_model.bone_count != private_character_lod2_model.bone_count)
        return;
    memcpy(private_character_lod2_model.bone_transforms,
           private_character_model.bone_transforms,
           private_character_model.bone_count *
               sizeof(*private_character_model.bone_transforms));
}
struct rasterfall_skeletal_actor_profile {
    const char *model_path;
    const char *walk_path;
    const char *right_hand_bone;
    int target_height_mm;
    int forward_sy, forward_cy;
};

/* Asset-space facts live in the profile rather than actor-specific renderer
 * branches.  PMX Eula faces -Z, hence the 180-degree forward correction. */
static const struct rasterfall_skeletal_actor_profile eula_actor_profile = {
    "rasterfall/private-assets/models/eula.rmesh",
    "rasterfall/private-assets/animations/walk04_loop5.vmd",
    "右手首", 1736, 0, -1024
};
static const struct rasterfall_skeletal_actor_profile maid_actor_profile = {
    "rasterfall/private-assets/models/maid.rmesh",
    "rasterfall/private-assets/animations/walk04_loop5.vmd",
    "右手首", 1736, 0, 1024
};

struct rasterfall_developer_character {
    const char *name, *model_path, *lod_model_path, *lod2_model_path, *height_source;
    int x, z, base_y, target_height_mm;
    struct rasterfall_model_asset model;
    struct rasterfall_model_asset lod_model;
    struct rasterfall_model_asset lod2_model;
    struct rasterfall_vmd_clip vmd;
    struct rasterfall_animation_clip walk;
    struct rasterfall_animation_track tracks[RASTERFALL_VMD_MAX_BONES];
    struct toy_renderer commands;
    struct rasterfall_frontend_state frontend;
    int commands_initialized;
    int load_attempted, lod_loaded, lod2_loaded, vmd_loaded;
    int vmd_mapped;
    long load_us;
};

#define RASTERFALL_CHARACTER_DISPLAY_MEAN_HEIGHT_MM 1750

/* Presentation-only lineup.  Per-model placement belongs here rather than in
 * toy_game: these objects have no gameplay actor, collision, AI or network
 * identity.  The metadata is also the intended home for future scale/yaw or
 * ground-offset corrections discovered during content validation. */
static struct rasterfall_developer_character developer_characters[] = {
    { .name = "Maid",
      .model_path = "rasterfall/private-assets/models/maid.rmesh",
      .lod_model_path = "rasterfall/private-assets/models/maid_lod1.rmesh",
      .lod2_model_path = "rasterfall/private-assets/models/maid_lod2.rmesh",
      .height_source = "MAID_PREVIEW_MATCHED_TO_EULA",
      .x = -14200, .z = -10000, .base_y = -900, .target_height_mm = 1736 }
};

#define RASTERFALL_DEVELOPER_CHARACTER_COUNT \
    ((int)(sizeof(developer_characters) / sizeof(developer_characters[0])))

static void load_developer_character_job(int worker_id, int task, void *opaque)
{
    static const char walk_path[] =
        "rasterfall/private-assets/animations/walk04_loop5.vmd";
    struct rasterfall_developer_character *entries = opaque;
    struct rasterfall_developer_character *entry = &entries[task];
    long start = render_monotonic_us();
    (void)worker_id;
    if (rasterfall_model_load(&entry->model, entry->model_path) == 0 &&
        rasterfall_vmd_load(&entry->vmd, walk_path) == 0) {
        entry->vmd_mapped = rasterfall_vmd_map_model(&entry->vmd,
                                                      &entry->model);
        rasterfall_model_bind_root_motion(
            &entry->model,
            rasterfall_model_find_bone(&entry->model, "センター"),
            rasterfall_model_find_bone(&entry->model, "グルーブ"));
        rasterfall_vmd_build_animation(&entry->vmd, &entry->walk,
            entry->tracks, RASTERFALL_VMD_MAX_BONES);
        entry->vmd_loaded = 1;
        if (entry->lod_model_path &&
            rasterfall_model_load(&entry->lod_model,
                                  entry->lod_model_path) == 0) {
            rasterfall_model_bind_root_motion(
                &entry->lod_model,
                rasterfall_model_find_bone(&entry->lod_model, "センター"),
                rasterfall_model_find_bone(&entry->lod_model, "グルーブ"));
            entry->lod_loaded = 1;
        }
        if (entry->lod2_model_path &&
            rasterfall_model_load(&entry->lod2_model,
                                  entry->lod2_model_path) == 0) {
            rasterfall_model_bind_root_motion(
                &entry->lod2_model,
                rasterfall_model_find_bone(&entry->lod2_model, "センター"),
                rasterfall_model_find_bone(&entry->lod2_model, "グルーブ"));
            entry->lod2_loaded = 1;
        }
    }
    entry->load_us = render_monotonic_us() - start;
}

static void load_developer_characters(struct toy_renderer *renderer)
{
    int i, pending = 0, task = -1;
    long start = render_monotonic_us();
    (void)renderer;
    for (i = 0; i < (int)(sizeof(developer_characters) /
                          sizeof(developer_characters[0])); i++)
        if (!developer_characters[i].load_attempted) pending++;
    if (!pending) return;
    /* Warm the one optional Maid display off the first-visible path. */
    for (i = 0; i < RASTERFALL_DEVELOPER_CHARACTER_COUNT; i++)
        if (!developer_characters[i].load_attempted) {
        task = i; break;
    }
    if (task < 0) return;
    developer_characters[task].load_attempted = 1;
    load_developer_character_job(0, task, developer_characters);
    for (i = 0; i < RASTERFALL_DEVELOPER_CHARACTER_COUNT; i++) {
        struct rasterfall_developer_character *entry =
            &developer_characters[i];
        if (!entry->load_attempted) continue;
        if (!entry->vmd_loaded) {
            __fprintf(2, "rasterfall: cannot load developer character %s\n",
                      entry->name);
            continue;
        }
        if (entry->lod_loaded)
            __printf("rasterfall: developer character %s LOD vertices=%u triangles=%u\n",
                entry->name, entry->lod_model.vertex_count,
                entry->lod_model.index_count / 3);
        __printf("rasterfall: developer character %s target_height_mm=%d height_source=%s scale_milli=%d vertices=%u triangles=%u bones=%d VMD_mapped=%d VMD_missing=%d duration_ms=%d load_us=%ld\n",
            entry->name, entry->target_height_mm, entry->height_source,
            character_model_scale(&entry->model, entry->target_height_mm),
            entry->model.vertex_count, entry->model.index_count / 3,
            entry->model.bone_count, entry->vmd_mapped,
            entry->vmd.track_count - entry->vmd_mapped,
            entry->walk.duration_ms, entry->load_us);
    }
    __printf("rasterfall: developer character warmup_step_us=%ld remaining=%d display_mean_height_mm=%d\n",
             render_monotonic_us() - start,
             pending - 1,
             RASTERFALL_CHARACTER_DISPLAY_MEAN_HEIGHT_MM);
}

static void sample_developer_character(struct rasterfall_developer_character *entry)
{
    struct rasterfall_animation_player *player = active_session ?
        &active_session->skeletal_demo_player : NULL;
    if (player && entry->vmd_loaded &&
        (player->clip_id == 9 || player->clip_id == 11)) {
            int center[3], groove[3];
            rasterfall_model_set_ik_enabled(&entry->model,
                                            private_character_vmd_ik_enabled);
            rasterfall_model_set_grant_enabled(
                &entry->model, private_character_vmd_grant_enabled);
            rasterfall_model_set_legacy_knee_ccd(
                &entry->model, private_character_vmd_legacy_knee_ccd);
            rasterfall_vmd_sample_bone_translation(
                &entry->vmd, "センター", player->time_ms, center);
            rasterfall_vmd_sample_bone_translation(
                &entry->vmd, "グルーブ", player->time_ms, groove);
            rasterfall_model_set_root_motion(&entry->model, center, groove, 1);
            if (player->clip_id == 11) {
                struct rasterfall_animation_composition composition = {
                    &entry->walk, player->time_ms, 1000, 0, 0,
                    (player->time_ms / 1200) & 1 ?
                        RASTERFALL_COMPOSITION_OVERLAY_HIT :
                        RASTERFALL_COMPOSITION_OVERLAY_FIRE,
                    player->time_ms % 1200, NULL, NULL, 0
                };
                rasterfall_animation_compose(&entry->model, &composition);
            } else rasterfall_model_sample_clip(&entry->model, &entry->walk,
                                                player->time_ms);
            if (entry->lod_loaded) {
                rasterfall_model_set_ik_enabled(&entry->lod_model,
                                                private_character_vmd_ik_enabled);
                rasterfall_model_set_grant_enabled(&entry->lod_model,
                                                   private_character_vmd_grant_enabled);
                rasterfall_model_set_legacy_knee_ccd(
                    &entry->lod_model, private_character_vmd_legacy_knee_ccd);
                rasterfall_model_set_root_motion(&entry->lod_model,
                                                  center, groove, 1);
                if (player->clip_id == 11) {
                    struct rasterfall_animation_composition composition = {
                        &entry->walk, player->time_ms, 1000, 0, 0,
                        (player->time_ms / 1200) & 1 ?
                            RASTERFALL_COMPOSITION_OVERLAY_HIT :
                            RASTERFALL_COMPOSITION_OVERLAY_FIRE,
                        player->time_ms % 1200, NULL, NULL, 0
                    };
                    rasterfall_animation_compose(&entry->lod_model,&composition);
                } else rasterfall_model_sample_clip(&entry->lod_model,
                                                    &entry->walk,player->time_ms);
            }
    } else {
        int zero[3] = {0, 0, 0};
        rasterfall_model_set_root_motion(&entry->model, zero, zero, 0);
        rasterfall_model_sample_clip(&entry->model, NULL, 0);
        if (entry->lod_loaded) {
            rasterfall_model_set_root_motion(&entry->lod_model, zero, zero, 0);
            rasterfall_model_sample_clip(&entry->lod_model, NULL, 0);
        }
    }
    if (entry->lod2_loaded && entry->model.bone_transforms &&
        entry->lod2_model.bone_transforms &&
        entry->model.bone_count == entry->lod2_model.bone_count)
        memcpy(entry->lod2_model.bone_transforms,
               entry->model.bone_transforms,
               entry->model.bone_count * sizeof(*entry->model.bone_transforms));
}

static int render_maid_pose_preview(struct toy_renderer *renderer,
                                    const struct camera *camera)
{
    struct rasterfall_developer_character *entry = &developer_characters[0];
    struct rasterfall_calibration_state *editor = &active_session->pose_editor;
    struct rasterfall_rifle_pose preview_pose;
    struct rasterfall_animation_composition composition;
    struct rasterfall_model_attachment_transform rifle_frame;
    const struct rasterfall_pose_calibration *wp;
    const struct rasterfall_weapon_asset_profile *asset;
    int zero[3] = {0, 0, 0};
    int scale, pixels = 0;

    if (!entry->model.data || !entry->vmd_loaded) return 0;
    rasterfall_model_set_ik_enabled(&entry->model, private_character_vmd_ik_enabled);
    rasterfall_model_set_grant_enabled(&entry->model, private_character_vmd_grant_enabled);
    rasterfall_model_set_root_motion(&entry->model, zero, zero, 0);
    memcpy(preview_pose.rotation, editor->pose.body_pose,
           sizeof(preview_pose.rotation));
    memset(&composition, 0, sizeof(composition));
    composition.rifle_stance = 1;
    composition.upper_body_lock = editor->upper_body_lock;
    composition.rifle_pose = &preview_pose;
    if (editor->animation_base) {
        int center[3], groove[3];
        rasterfall_vmd_sample_bone_translation(&entry->vmd, "センター",
                                               editor->animation_time_ms, center);
        rasterfall_vmd_sample_bone_translation(&entry->vmd, "グルーブ",
                                               editor->animation_time_ms, groove);
        rasterfall_model_set_root_motion(&entry->model, center, groove, 1);
        composition.locomotion = &entry->walk;
        composition.locomotion_time_ms = editor->animation_time_ms;
        composition.locomotion_weight_milli = 1000;
        composition.overlay = editor->animation_overlay == 1 ?
            RASTERFALL_COMPOSITION_OVERLAY_FIRE :
            editor->animation_overlay == 2 ?
            RASTERFALL_COMPOSITION_OVERLAY_HIT :
            RASTERFALL_COMPOSITION_OVERLAY_NONE;
        composition.overlay_time_ms = editor->animation_time_ms % 1200;
    } else {
        rasterfall_model_sample_clip(&entry->model, NULL, 0);
    }
    /* The editor pose is useful in the stationary bind pose too.  Keep the
     * composition call outside the animation branch so body/arm edits are
     * visible before the author enables a locomotion preview. */
    rasterfall_animation_compose(&entry->model, &composition);
    if (rifle_frame_transform(&entry->model, &rifle_frame) < 0) return 0;
    wp = rasterfall_pose_calibration_resolve(editor, 1, TOY_GAME_WEAPON_AK);
    asset = rasterfall_weapon_asset_profile(TOY_GAME_WEAPON_AK);
    scale = character_model_scale(&entry->model, maid_actor_profile.target_height_mm);
    if (wp->left_ik)
        rifle_solve_hands(&entry->model, &rifle_frame, wp, asset, scale);
    active_pose_preview = 1;
    active_gallery_facing = 1;
    active_gallery_sy = 0;
    active_gallery_cy = maid_actor_profile.forward_cy;
    pixels += render_gallery_model(renderer, camera, &entry->model,
                                   -13000, -900, -10000, scale);
    pixels += render_skeletal_rifle(renderer, camera, &entry->model,
                                    &rifle_frame, &maid_actor_profile, wp,
                                    -13000, -900, -10000, 0, 1024,
                                    TOY_GAME_WEAPON_AK, 0);
    active_gallery_facing = 0;
    active_pose_preview = 0;
    return pixels;
}

int rasterfall_render_set_vmd_walk(const char *model_path, const char *vmd_path)
{
    private_character_override_path = model_path;
    private_character_vmd_path = vmd_path;
    private_character_vmd_forced = 1;
    return 0;
}

void rasterfall_render_set_vmd_freeze(int freeze_head, int freeze_torso)
{
    private_character_vmd_freeze_head = freeze_head;
    private_character_vmd_freeze_torso = freeze_torso;
}

void rasterfall_render_set_vmd_ik_enabled(int enabled)
{
    private_character_vmd_ik_enabled = enabled ? 1 : 0;
}

void rasterfall_render_set_vmd_grant_enabled(int enabled)
{
    private_character_vmd_grant_enabled = enabled ? 1 : 0;
}

void rasterfall_render_set_vmd_legacy_root_offset(int enabled)
{
    private_character_vmd_legacy_root_offset = enabled ? 1 : 0;
}

void rasterfall_render_set_vmd_legacy_knee_ccd(int enabled)
{
    private_character_vmd_legacy_knee_ccd = enabled ? 1 : 0;
}

void rasterfall_render_set_vmd_skin_trace(int enabled)
{
    private_character_vmd_skin_trace = enabled ? 1 : 0;
}

static int render_quaternius_preview(struct toy_renderer *renderer,
                                     const struct camera *camera,int clip_id,
                                     int time_ms)
{
    const char path[]="rasterfall/private-assets/models/UAL1_Standard.glb";
    int i,pixels=0,scale=520,origin_x=-14500,origin_y=-900,origin_z=-8800;
    if(!quaternius_preview_loaded){
        quaternius_preview_loaded=1;
        if(rasterfall_glb_preview_load(&quaternius_preview,path)<0)return 0;
    }
    if(clip_id>=0&&clip_id<3&&quaternius_preview_clip!=clip_id){
        if(rasterfall_glb_preview_select_animation(&quaternius_preview,
           private_character_clip_names[clip_id])<0)return 0;
        quaternius_preview_clip=clip_id;
    }
    if(rasterfall_glb_preview_sample(&quaternius_preview,
       clip_id>=0&&clip_id<3?time_ms:0)<0)return 0;
    for(i=0;i+2<quaternius_preview.index_count;i+=3){
        unsigned int ia=quaternius_preview.indices[i],ib=quaternius_preview.indices[i+1],ic=quaternius_preview.indices[i+2];
        struct vec3 a,b,c;if(ia>=(unsigned)quaternius_preview.vertex_count||ib>=(unsigned)quaternius_preview.vertex_count||ic>=(unsigned)quaternius_preview.vertex_count)continue;
        a.x=origin_x+(int)(quaternius_preview.positions[ia*3]*scale);a.y=origin_y+(int)(quaternius_preview.positions[ia*3+1]*scale);a.z=origin_z+(int)(quaternius_preview.positions[ia*3+2]*scale);
        b.x=origin_x+(int)(quaternius_preview.positions[ib*3]*scale);b.y=origin_y+(int)(quaternius_preview.positions[ib*3+1]*scale);b.z=origin_z+(int)(quaternius_preview.positions[ib*3+2]*scale);
        c.x=origin_x+(int)(quaternius_preview.positions[ic*3]*scale);c.y=origin_y+(int)(quaternius_preview.positions[ic*3+1]*scale);c.z=origin_z+(int)(quaternius_preview.positions[ic*3+2]*scale);
        pixels+=draw_world_triangle(renderer,camera,&a,&b,&c,0xff68a8d8u);
    }
    return pixels;
}

#define RASTERFALL_MODEL_PATH_BYTES 160
#define RASTERFALL_GALLERY_COLUMNS 9
#define RASTERFALL_GALLERY_SLOT_X 1300
#define RASTERFALL_GALLERY_ROW_Y 700
#define RASTERFALL_GALLERY_BASE_Y 500
static char gallery_paths[RASTERFALL_MODEL_MAX_GALLERY]
                          [RASTERFALL_MODEL_PATH_BYTES];

struct world_uv_vertex { struct vec3 p; int u, v; int su, sv; int light; };
struct gallery_cached_vertex {
    struct world_uv_vertex uv;
    struct vec3 view;
    int sphere_u, sphere_v;
    int sphere_ready;
    struct vec3 edge_world, edge_view;
    int edge_size;
    int edge_ready;
    int model_position[3];
    int normal[3];
    int source_normal[3];
};

enum rasterfall_character_distance_quality {
    RASTERFALL_CHARACTER_NEAR,
    RASTERFALL_CHARACTER_MID,
    RASTERFALL_CHARACTER_FAR,
    RASTERFALL_CHARACTER_HIDDEN
};

static unsigned long rasterfall_render_frame;
static struct rasterfall_frontend_state ai_character_frontends[TOY_GAME_MAX_ACTORS];
static unsigned char ai_character_frontends_initialized[TOY_GAME_MAX_ACTORS];
/* Per-frame budget for nearby, visible original skeletal meshes.  IDs 0..4
 * are the gallery Eula/developer displays; 100+i identifies an AI actor. */
static int near_original_model_owner = -1;

/* Distance policy for high-detail characters.  Keep the decision in one
 * place: distance is horizontal Euclidean distance squared, while all
 * projection/frustum work remains in the normal model renderer. */
static enum rasterfall_character_distance_quality character_distance_policy(
    const struct camera *camera, int x, int z, int actor_index,
    struct rasterfall_frontend_state *state)
{
    long long dx = (long long)x - camera->x;
    long long dz = (long long)z - camera->z;
    long long distance_sq = dx * dx + dz * dz;
    long long near_sq = (long long)RASTERFALL_CHARACTER_NEAR_RFU *
                        RASTERFALL_CHARACTER_NEAR_RFU;
    long long mid_sq = (long long)RASTERFALL_CHARACTER_MID_RFU *
                       RASTERFALL_CHARACTER_MID_RFU;
    long long far_sq = (long long)RASTERFALL_CHARACTER_FAR_RFU *
                       RASTERFALL_CHARACTER_FAR_RFU;
    enum rasterfall_character_distance_quality quality;
    int interval, update;
    if (distance_sq < near_sq) quality = RASTERFALL_CHARACTER_NEAR;
    else if (distance_sq < mid_sq) quality = RASTERFALL_CHARACTER_MID;
    else if (distance_sq < far_sq) quality = RASTERFALL_CHARACTER_FAR;
    else quality = RASTERFALL_CHARACTER_HIDDEN;
    if (!state) return quality;
    state->disable_edge = quality != RASTERFALL_CHARACTER_NEAR;
    state->disable_sphere = quality != RASTERFALL_CHARACTER_NEAR;
    state->disable_toon = quality == RASTERFALL_CHARACTER_FAR ||
                          quality == RASTERFALL_CHARACTER_HIDDEN;
    interval = quality == RASTERFALL_CHARACTER_NEAR ? 1 :
               quality == RASTERFALL_CHARACTER_MID ? 2 : 4;
    update = quality != RASTERFALL_CHARACTER_HIDDEN &&
             ((rasterfall_render_frame + (unsigned long)actor_index) %
              (unsigned long)interval) == 0;
    if (quality != RASTERFALL_CHARACTER_HIDDEN &&
        !state->skinned_vertices_valid) update = 1;
    state->reuse_skinned_vertices = !update;
    return quality;
}

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

static int character_model_scale(const struct rasterfall_model_asset *model,
                                 int target_height_mm)
{
    int height;
    if (!model || target_height_mm <= 0) return 1;
    height = model->max_y - model->min_y;
    if (height <= 0) return 1;
    /* Convert mm to milli-scale in one division.  Quantizing target height to
     * whole RFU first is enough to make adjacent models disagree by 1‰. */
    return (int)(((long long)target_height_mm * RASTERFALL_RFU_PER_METER +
                  height / 2) / height);
}

static int prepare_gallery_vertex_cache(
    struct toy_renderer *renderer, const struct rasterfall_model_asset *model,
    const struct camera *camera,
    int center_x, int base_y, int center_z, int scale)
{
    unsigned int i;
    long phase_start;
    if (gallery_vertex_cache_capacity < model->vertex_count) {
        struct gallery_cached_vertex *vertices =
            tlibc_malloc((size_t)model->vertex_count * sizeof(*vertices));
        if (!vertices) return -1;
        if (gallery_vertex_cache) tlibc_free(gallery_vertex_cache);
        gallery_vertex_cache = vertices;
        gallery_vertex_cache_capacity = model->vertex_count;
        frontend_state()->skinned_vertices_valid = 0;
    }
    if (model->skinning_enabled && model->bone_count &&
        !(frontend_state()->reuse_skinned_vertices &&
          frontend_state()->skinned_vertices_valid)) {
        phase_start = render_monotonic_us();
        if (rasterfall_model_update_bones(
                (struct rasterfall_model_asset *)model) < 0) return -1;
        model_setup_timing.bone_hierarchy_us +=
            render_monotonic_us() - phase_start;
    }
    if (model->skinning_enabled && model->bone_count &&
        !(frontend_state()->reuse_skinned_vertices &&
          frontend_state()->skinned_vertices_valid)) {
        phase_start = render_monotonic_us();
        for (i = 0; i < model->vertex_count; i++)
            if (rasterfall_model_skin_vertex(model, i,
                    gallery_vertex_cache[i].model_position,
                    gallery_vertex_cache[i].normal) < 0) return -1;
        for (i = 0; i < model->vertex_count; i++) {
            gallery_vertex_cache[i].source_normal[0] =
                gallery_vertex_cache[i].normal[0];
            gallery_vertex_cache[i].source_normal[1] =
                gallery_vertex_cache[i].normal[1];
            gallery_vertex_cache[i].source_normal[2] =
                gallery_vertex_cache[i].normal[2];
        }
        model_setup_timing.skinning_us += render_monotonic_us() - phase_start;
        frontend_state()->skinned_vertices_valid = 1;
    }
    for (i = 0; i < model->vertex_count; i++) {
        const unsigned char *p = model->vertices + i * model->vertex_bytes;
        struct gallery_cached_vertex *cached = &gallery_vertex_cache[i];
        int *position = cached->model_position;
        if (!model->skinning_enabled || !model->bone_count) {
            position[0] = *(const int *)p;
            position[1] = *(const int *)(p + 4);
            position[2] = *(const int *)(p + 8);
            cached->normal[0] = *(const short *)(p + 12);
            cached->normal[1] = *(const short *)(p + 14);
            cached->normal[2] = *(const short *)(p + 16);
            cached->source_normal[0] = cached->normal[0];
            cached->source_normal[1] = cached->normal[1];
            cached->source_normal[2] = cached->normal[2];
        }
        {
        int render_x = position[0], render_z = position[2];
        if (active_gallery_facing) {
            int model_x = position[0], model_z = position[2];
            int normal_x = cached->source_normal[0];
            int normal_z = cached->source_normal[2];
            render_x = (int)(((long long)model_x * active_gallery_cy +
                              (long long)model_z * active_gallery_sy) / 1024);
            render_z = (int)(((long long)model_z * active_gallery_cy -
                              (long long)model_x * active_gallery_sy) / 1024);
            cached->normal[0] = (short)(((long long)normal_x * active_gallery_cy +
                                         (long long)normal_z * active_gallery_sy) / 1024);
            cached->normal[2] = (short)(((long long)normal_z * active_gallery_cy -
                                         (long long)normal_x * active_gallery_sy) / 1024);
        } else {
            cached->normal[0] = cached->source_normal[0];
            cached->normal[1] = cached->source_normal[1];
            cached->normal[2] = cached->source_normal[2];
        }
        cached->uv.p.x = center_x +
            (int)((long long)render_x * scale / 1000);
        cached->uv.p.y = base_y +
            (int)((long long)(position[1] - model->min_y) * scale / 1000);
        cached->uv.p.z = center_z +
            (int)((long long)render_z * scale / 1000);
        }
        world_to_view(camera, &cached->uv.p, &cached->view);
        cached->uv.u = *(const unsigned short *)(p + 18);
        cached->uv.v = *(const unsigned short *)(p + 20);
        if (model->vertex_bytes >= RASTERFALL_MODEL_VERTEX_BYTES_ADDITIONAL_UV) {
            cached->uv.su = *(const int *)(p + 24);
            cached->uv.sv = *(const int *)(p + 28);
        } else {
            cached->uv.su = 0;
            cached->uv.sv = 0;
        }
        cached->sphere_ready = 0;
        cached->edge_ready = 0;
    }
    return 0;
}

/* Inspector-only capture remains in this translation unit so it can observe
 * the final worker-local vertex cache without widening the runtime API. */
#include "dev-tests/rasterfall_skin_trace.inc"

/* Conservative model-level frustum test.  The eight transformed AABB corners
 * are tested against the same projection planes as project_vertex().  A box
 * crossing the near plane is retained so near clipping remains authoritative. */
static int gallery_model_visible(const struct toy_surface *surface,
                                 const struct camera *camera,
                                 const struct rasterfall_model_asset *model,
                                 int center_x, int base_y, int center_z,
                                 int scale)
{
    struct vec3 view[8];
    int xs[2], ys[2], zs[2], n = 0;
    int focal = surface->width * 3 / 4;
    /* Keep a small screen-space guard for PMX Edge shell expansion and
     * integer projection rounding at the viewport boundary. */
    int half_width = surface->width / 2 + 32;
    int half_height = surface->height / 2 + 32;
    int all_left = 1, all_right = 1, all_above = 1, all_below = 1;
    int crosses_near = 0, max_z = -2147483647;
    {
        int margin_x = model->skinning_enabled ?
            (model->max_x - model->min_x) / 2 : 0;
        int margin_y = model->skinning_enabled ?
            (model->max_y - model->min_y) / 4 : 0;
        int margin_z = model->skinning_enabled ?
            (model->max_z - model->min_z) / 2 : 0;
        xs[0] = center_x + (int)((long long)(model->min_x - margin_x) * scale / 1000);
        xs[1] = center_x + (int)((long long)(model->max_x + margin_x) * scale / 1000);
        ys[0] = base_y - (int)((long long)margin_y * scale / 1000);
        ys[1] = base_y + (int)((long long)(model->max_y - model->min_y + margin_y) *
                               scale / 1000);
        zs[0] = center_z + (int)((long long)(model->min_z - margin_z) * scale / 1000);
        zs[1] = center_z + (int)((long long)(model->max_z + margin_z) * scale / 1000);
    }
    for (int xi = 0; xi < 2; xi++) for (int yi = 0; yi < 2; yi++)
        for (int zi = 0; zi < 2; zi++) {
            struct vec3 world = {xs[xi], ys[yi], zs[zi]};
            world_to_view(camera, &world, &view[n]);
            if (view[n].z < NEAR_Z) crosses_near = 1;
            if (view[n].z > max_z) max_z = view[n].z;
            n++;
        }
    if (max_z < NEAR_Z) return 0;
    if (crosses_near) return 1;
    for (n = 0; n < 8; n++) {
        long long xf = (long long)view[n].x * focal;
        long long yf = (long long)view[n].y * focal;
        long long x_limit = (long long)view[n].z * half_width;
        long long y_limit = (long long)view[n].z * half_height;
        if (xf >= -x_limit) all_left = 0;
        if (xf <= x_limit) all_right = 0;
        if (yf <= y_limit) all_above = 0;
        if (yf >= -y_limit) all_below = 0;
    }
    return !(all_left || all_right || all_above || all_below);
}

static int character_view_depth(const struct camera *camera, int x, int z)
{
    long long dx = (long long)x - camera->x;
    long long dz = (long long)z - camera->z;
    long long depth = dx * camera->sy + dz * camera->cy;
    if (depth < 0) return -1;
    return depth > 2147483647LL ? 2147483647 : (int)depth;
}

static void select_near_original_model(const struct camera *camera,
                                       const struct toy_surface *surface)
{
    int best_depth = 2147483647;
    int i;
    near_original_model_owner = -1;
    if (active_pose_preview) return;

#define CONSIDER_NEAR_CHARACTER(owner_id, asset, px, py, pz, pscale) \
    do { \
        if ((asset)->data && \
            character_distance_policy(camera, (px), (pz), (owner_id), NULL) == \
                RASTERFALL_CHARACTER_NEAR && \
            gallery_model_visible((surface), camera, (asset), (px), (py), \
                                  (pz), (pscale))) { \
            int candidate_depth = character_view_depth(camera, (px), (pz)); \
            if (candidate_depth >= 0 && candidate_depth < best_depth) { \
                best_depth = candidate_depth; \
                near_original_model_owner = (owner_id); \
            } \
        } \
    } while (0)

    CONSIDER_NEAR_CHARACTER(0, &private_character_model,
        -13000, -900, -10000,
        character_model_scale(&private_character_model,
                              eula_actor_profile.target_height_mm));
    for (i = 0; i < RASTERFALL_DEVELOPER_CHARACTER_COUNT; i++) {
        struct rasterfall_developer_character *entry = &developer_characters[i];
        CONSIDER_NEAR_CHARACTER(i + 1, &entry->model, entry->x, entry->base_y,
            entry->z, character_model_scale(&entry->model,
                                            entry->target_height_mm));
    }
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
        const struct toy_game_actor *actor = &game.actors[i];
        struct rasterfall_developer_character *entry;
        const struct rasterfall_model_asset *asset;
        int scale;
        if (!actor->active || actor->kind != TOY_GAME_ACTOR_AI ||
            !actor->anime_character_id) continue;
        entry = actor->anime_character_id >= 2 && actor->anime_character_id <= 5 ?
            &developer_characters[0] : NULL;
        asset = entry && entry->vmd_loaded ? &entry->model :
            &private_character_model;
        scale = character_model_scale(asset, eula_actor_profile.target_height_mm);
        CONSIDER_NEAR_CHARACTER(100 + i, asset, actor->x, -900,
            actor->z, scale);
    }
#undef CONSIDER_NEAR_CHARACTER
}

static void gallery_edge_vertex(struct toy_renderer *renderer,
                                const struct rasterfall_model_asset *model,
                                const struct camera *camera,
                                unsigned int index, int center_x, int base_y,
                                int center_z, int scale, int edge_size,
                                struct vec3 *out, struct vec3 *view_out)
{
    const unsigned char *p = model->vertices + index * model->vertex_bytes;
    struct gallery_cached_vertex *cached = &gallery_vertex_cache[index];
    unsigned int vertex_edge_scale = model->format_version >= 10 ?
        model_u32(p + 32) : 65536U;
    (void)center_x; (void)base_y; (void)center_z;
    if (cached->edge_ready && cached->edge_size == edge_size) {
        *out = cached->edge_world;
        *view_out = cached->edge_view;
        return;
    }
    cached->edge_world = cached->uv.p;
    cached->edge_world.x += (int)((long long)cached->normal[0] * edge_size * scale *
                        vertex_edge_scale / (32767LL * 1000 * 65536));
    cached->edge_world.y += (int)((long long)cached->normal[1] * edge_size * scale *
                        vertex_edge_scale / (32767LL * 1000 * 65536));
    cached->edge_world.z += (int)((long long)cached->normal[2] * edge_size * scale *
                        vertex_edge_scale / (32767LL * 1000 * 65536));
    world_to_view(camera, &cached->edge_world, &cached->edge_view);
    cached->edge_size = edge_size;
    cached->edge_ready = 1;
    *out = cached->edge_world;
    *view_out = cached->edge_view;
}

static void gallery_uv_vertex(struct toy_renderer *renderer,
                              const struct rasterfall_model_asset *model,
                              const struct camera *camera,
                              unsigned int index, int center_x, int base_y,
                              int center_z, int scale,
                              int sphere_mode,
                              struct world_uv_vertex *out)
{
    (void)center_x; (void)base_y; (void)center_z; (void)scale;
    out->p = gallery_vertex_cache[index].uv.p;
    out->u = gallery_vertex_cache[index].uv.u;
    out->v = gallery_vertex_cache[index].uv.v;
    {
        int nx = gallery_vertex_cache[index].normal[0];
        int ny = gallery_vertex_cache[index].normal[1];
        int nz = gallery_vertex_cache[index].normal[2];
        int dot = (-nx + ny * 2 - nz) / 4;
        out->light = 264 + dot * 32 / 32767;
        if (out->light < 232) out->light = 232;
        if (out->light > 288) out->light = 288;
    }
    if (sphere_mode == 3 && model->vertex_bytes >=
                            RASTERFALL_MODEL_VERTEX_BYTES_ADDITIONAL_UV) {
        out->su = gallery_vertex_cache[index].uv.su;
        out->sv = gallery_vertex_cache[index].uv.sv;
    } else if (sphere_mode == 1 || sphere_mode == 2) {
        struct gallery_cached_vertex *cached = &gallery_vertex_cache[index];
        if (!cached->sphere_ready) {
            int nx = cached->normal[0];
            int ny = cached->normal[1];
            int nz = cached->normal[2];
            int view_nx = (nx * camera->cy - nz * camera->sy) / 1024;
            int view_nz = (nx * camera->sy + nz * camera->cy) / 1024;
            int view_ny = (ny * camera->pitch_cy -
                           view_nz * camera->pitch_sy) / 1024;
            long long sphere_length = isqrt((long long)view_nx * view_nx +
                                            (long long)view_ny * view_ny +
                                            (long long)(view_nz + 32767) *
                                            (view_nz + 32767));
            if (sphere_length > 0) {
                cached->sphere_u = 32768 +
                    (int)((long long)view_nx * 32768 / sphere_length);
                /* Texture images use a top-down V axis. */
                cached->sphere_v = 32768 -
                    (int)((long long)view_ny * 32768 / sphere_length);
            } else {
                cached->sphere_u = 32768;
                cached->sphere_v = 32768;
            }
            if (cached->sphere_u < 0) cached->sphere_u = 0;
            if (cached->sphere_u > 65535) cached->sphere_u = 65535;
            if (cached->sphere_v < 0) cached->sphere_v = 0;
            if (cached->sphere_v > 65535) cached->sphere_v = 65535;
            cached->sphere_ready = 1;
        }
        out->su = cached->sphere_u;
        out->sv = cached->sphere_v;
    } else {
        out->su = 0;
        out->sv = 0;
    }
}

/* The four imported PMX layouts are stable across their full and index-only
 * LOD meshes.  Keep this content policy beside the renderer until RFM2
 * carries authored material roles: face/eyes receive filtering and smooth
 * lighting, hair keeps its silhouette, and clothing/equipment relinquish
 * sphere, toon and edge budget. */
enum character_perceptual_model {
    CHARACTER_PERCEPTUAL_NONE,
    CHARACTER_PERCEPTUAL_AR15,
    CHARACTER_PERCEPTUAL_G11,
    CHARACTER_PERCEPTUAL_VECTOR,
    CHARACTER_PERCEPTUAL_UMP45
};

enum rasterfall_character_visual_class {
    RASTERFALL_VISUAL_DEFAULT,
    RASTERFALL_VISUAL_FACE,
    RASTERFALL_VISUAL_EYES,
    RASTERFALL_VISUAL_HAIR,
    RASTERFALL_VISUAL_SKIN,
    RASTERFALL_VISUAL_CLOTHING,
    RASTERFALL_VISUAL_EQUIPMENT
};

struct rasterfall_character_render_policy {
    int use_sphere;
    int use_toon;
    int use_edge;
    int base_texture_bilinear;
    int edge_width_milli;
};

static int character_perceptual_model(
    const struct rasterfall_model_asset *model)
{
    if (!model) return CHARACTER_PERCEPTUAL_NONE;
    if (model->vertex_count == 86537 && model->primitive_count == 29)
        return CHARACTER_PERCEPTUAL_AR15;
    if (model->vertex_count == 34699 && model->primitive_count == 20)
        return CHARACTER_PERCEPTUAL_G11;
    if (model->vertex_count == 32964 && model->primitive_count == 20)
        return CHARACTER_PERCEPTUAL_VECTOR;
    if (model->vertex_count == 55843 && model->primitive_count == 22)
        return CHARACTER_PERCEPTUAL_UMP45;
    return CHARACTER_PERCEPTUAL_NONE;
}

static enum rasterfall_character_visual_class
character_visual_class(int profile, int primitive, unsigned int material)
{
    (void)material;
    if (profile == CHARACTER_PERCEPTUAL_AR15) {
        if (primitive <= 8 || primitive == 25 || primitive == 28)
            return RASTERFALL_VISUAL_FACE;
        if (primitive == 23 || primitive == 24)
            return RASTERFALL_VISUAL_HAIR;
        if ((primitive >= 10 && primitive <= 22) ||
            primitive == 26 || primitive == 27)
            return RASTERFALL_VISUAL_EQUIPMENT;
    } else if (profile == CHARACTER_PERCEPTUAL_G11) {
        if (primitive <= 10 || primitive >= 17)
            return RASTERFALL_VISUAL_FACE;
        if (primitive == 15 || primitive == 16)
            return RASTERFALL_VISUAL_HAIR;
        if (primitive >= 11 && primitive <= 14)
            return RASTERFALL_VISUAL_EQUIPMENT;
    } else if (profile == CHARACTER_PERCEPTUAL_VECTOR) {
        if (primitive <= 5 || (primitive >= 7 && primitive <= 12) ||
            primitive >= 18)
            return RASTERFALL_VISUAL_FACE;
        if (primitive == 16 || primitive == 17)
            return RASTERFALL_VISUAL_HAIR;
        if (primitive >= 13 && primitive <= 15)
            return RASTERFALL_VISUAL_EQUIPMENT;
    } else if (profile == CHARACTER_PERCEPTUAL_UMP45) {
        if (primitive <= 10 || primitive >= 20)
            return RASTERFALL_VISUAL_FACE;
        if (primitive == 13 || primitive == 14 || primitive == 16)
            return RASTERFALL_VISUAL_HAIR;
        if (primitive == 11 || primitive == 12 ||
            (primitive >= 17 && primitive <= 19))
            return RASTERFALL_VISUAL_EQUIPMENT;
    }
    return RASTERFALL_VISUAL_DEFAULT;
}

static enum rasterfall_character_visual_class
authored_visual_class(const struct rasterfall_model_asset *model,
                      unsigned int material)
{
    unsigned int role;
    if (!model || model->format_version < 9 || material >= model->material_count)
        return RASTERFALL_VISUAL_DEFAULT;
    role = model->materials[material * model->material_bytes +
                            RASTERFALL_MODEL_MATERIAL_ROLE_OFFSET];
    if (role == RASTERFALL_MODEL_MATERIAL_ROLE_FACE) return RASTERFALL_VISUAL_FACE;
    if (role == RASTERFALL_MODEL_MATERIAL_ROLE_EYES) return RASTERFALL_VISUAL_EYES;
    if (role == RASTERFALL_MODEL_MATERIAL_ROLE_HAIR) return RASTERFALL_VISUAL_HAIR;
    if (role == RASTERFALL_MODEL_MATERIAL_ROLE_SKIN) return RASTERFALL_VISUAL_SKIN;
    if (role == RASTERFALL_MODEL_MATERIAL_ROLE_CLOTHING) return RASTERFALL_VISUAL_CLOTHING;
    if (role == RASTERFALL_MODEL_MATERIAL_ROLE_EQUIPMENT) return RASTERFALL_VISUAL_EQUIPMENT;
    return RASTERFALL_VISUAL_DEFAULT;
}

static struct rasterfall_character_render_policy
character_render_policy(enum rasterfall_character_visual_class visual_class)
{
    struct rasterfall_character_render_policy policy = {1, 1, 1, 0, 1000};
    if (visual_class == RASTERFALL_VISUAL_FACE ||
        visual_class == RASTERFALL_VISUAL_EYES) {
        policy.use_toon = 0;
        policy.base_texture_bilinear = 1;
    } else if (visual_class == RASTERFALL_VISUAL_HAIR) {
        policy.use_sphere = 0;
        policy.use_edge = 0;
    } else if (visual_class == RASTERFALL_VISUAL_CLOTHING ||
               visual_class == RASTERFALL_VISUAL_EQUIPMENT) {
        policy.use_sphere = 0;
        policy.use_toon = 0;
        policy.use_edge = 0;
    }
    return policy;
}

static int render_gallery_model_range(struct toy_renderer *renderer,
                                const struct camera *camera,
                                const struct rasterfall_model_asset *model,
                                int center_x, int base_y, int center_z,
                                int scale, int first_primitive,
                                int primitive_count, int first_index,
                                int slice_index_count, int prepare_vertices)
{
    int primitive_end = first_primitive + primitive_count;
    if (first_primitive < 0) first_primitive = 0;
    if (primitive_end > (int)model->primitive_count)
        primitive_end = (int)model->primitive_count;
    if (prepare_vertices) {
        __sync_fetch_and_add(&scene_stats.models_tested, 1);
        if (!gallery_model_visible(&renderer->surface, camera, model, center_x,
                                   base_y, center_z, scale)) {
            __sync_fetch_and_add(&scene_stats.models_culled, 1);
            __sync_fetch_and_add(&scene_stats.model_triangles_culled,
                                 model->index_count / 3);
            return 0;
        }
    }
    long render_start = render_monotonic_us();
    long phase_start;
    int drawn = 0, i;
    int command_overflow_before = renderer->cmd_overflow;
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
    int previous_face_material = active_face_material;
    int previous_skin_material = active_skin_material;
    uint32_t previous_material_tint = active_material_tint;
    int previous_base_bilinear =
        renderer->recording_base_texture_bilinear;
    int shared_texture = gallery_model_has_texture(model);
    int perceptual_profile = character_perceptual_model(model);
    if (prepare_vertices) {
        long bone_before, skin_before;
        phase_start = render_monotonic_us();
        bone_before = model_setup_timing.bone_hierarchy_us;
        skin_before = model_setup_timing.skinning_us;
        if (prepare_gallery_vertex_cache(renderer, model, camera, center_x, base_y,
                                         center_z, scale) < 0)
            return 0;
        skin_trace_capture(renderer, model);
        model_setup_timing.vertex_cache_us += render_monotonic_us() - phase_start -
            (model_setup_timing.bone_hierarchy_us - bone_before) -
            (model_setup_timing.skinning_us - skin_before);
    }
    active_sphere_texture = 0;
    active_sphere_mode = 0;
    active_toon_texture = 0;
    active_toon_shared = -1;
    if (shared_texture)
        active_texture_view = active_model_texture;
    for (i = first_primitive; i < primitive_end; i++) {
        long material_start = render_monotonic_us();
        long edge_us = 0, body_us;
        const unsigned char *primitive = model->primitives + i * RASTERFALL_MODEL_PRIMITIVE_BYTES;
        const unsigned char *indices = model->indices + model_u32(primitive) * 4;
        unsigned int index_count = model_u32(primitive + 4);
        unsigned int index_begin = 0, index_end = index_count;
        unsigned int material = model_u32(primitive + 8);
        const struct toy_texture_view *texture = 0;
        const struct toy_texture_view *sphere_texture = 0;
        const struct toy_texture_view *toon_texture = 0;
        enum rasterfall_character_visual_class visual_class =
            authored_visual_class(model, material);
        if (visual_class == RASTERFALL_VISUAL_DEFAULT)
            visual_class = character_visual_class(perceptual_profile, i, material);
        struct rasterfall_character_render_policy policy =
            character_render_policy(visual_class);
        active_face_material = visual_class == RASTERFALL_VISUAL_FACE ||
            visual_class == RASTERFALL_VISUAL_EYES;
        active_skin_material = visual_class == RASTERFALL_VISUAL_SKIN;
        active_material_tint = active_skin_material ? 0x00fff5edU : 0x00ffffffU;
        toy_renderer_set_base_texture_bilinear(renderer,
            policy.base_texture_bilinear);
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
        if (primitive_count == 1 && slice_index_count >= 0) {
            if (first_index > 0) index_begin = (unsigned int)first_index;
            if (index_begin > index_count) index_begin = index_count;
            index_end = index_begin + (unsigned int)slice_index_count;
            if (index_end > index_count) index_end = index_count;
        }
        phase_start = render_monotonic_us();
        if (edge_pass_enabled && !active_disable_edge && policy.use_edge &&
            material < model->material_count &&
            model->format_version >= 8) {
            const unsigned char *material_data = model->materials +
                material * model->material_bytes;
            unsigned int edge = model_u32(material_data + 16);
            int edge_size = (int)model_u32(material_data + 20);
            edge_size = (int)((long long)edge_size *
                              policy.edge_width_milli / 1000);
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
                toy_renderer_set_recording_edge(renderer, 1);
                active_model_triangle_stats = collect_model_render_stats ?
                    &model_render_stats.edge : 0;
                for (j = index_begin; j + 2 < index_end; j += 3) {
                    if ((j & 255U) == 0 &&
                        toy_renderer_job_cancelled(renderer)) break;
                    unsigned int ia = model_u32(indices + j * 4);
                    unsigned int ib = model_u32(indices + (j + 1) * 4);
                    unsigned int ic = model_u32(indices + (j + 2) * 4);
                    struct vec3 a, b, c, va, vb, vc;
                    if (ia >= model->vertex_count || ib >= model->vertex_count ||
                        ic >= model->vertex_count) continue;
                    gallery_edge_vertex(renderer, model, camera, ia, center_x, base_y,
                                        center_z, scale, edge_size, &a, &va);
                    gallery_edge_vertex(renderer, model, camera, ic, center_x, base_y,
                                        center_z, scale, edge_size, &b, &vb);
                    gallery_edge_vertex(renderer, model, camera, ib, center_x, base_y,
                                        center_z, scale, edge_size, &c, &vc);
                    drawn += draw_world_triangle_views(renderer, camera,
                        &a, &b, &c, &va, &vb, &vc, edge & 0xffffffU);
                }
                active_texture_view = saved_texture;
                active_sphere_texture = saved_sphere;
                active_toon_texture = saved_toon;
                active_material_alpha = saved_alpha;
                active_material_double_sided = saved_double_sided;
                toy_renderer_set_recording_edge(renderer, 0);
            }
        }
        edge_us = render_monotonic_us() - phase_start;
        model_setup_timing.edge_triangles_us += edge_us;
        active_model_triangle_stats = collect_model_render_stats ?
            &model_render_stats.body : 0;
        if (material < model->material_count && model->textures.assets) {
            unsigned int texture_index = model_u32(model->materials + material * model->material_bytes + 8);
            if (texture_index < model->textures.count && model->textures.assets[texture_index].data) {
                texture = &model->textures.views[texture_index];
                active_texture_view = texture;
            }
            if (model->format_version >= 3) {
                unsigned int packed = model_u32(model->materials + material * model->material_bytes + 12);
                unsigned int sphere_index = packed & 0xffffU;
                unsigned int sphere_mode = (packed >> 16) & 3;
                /* PMX modes 1/2 are multiplicative/additive environment maps.
                 * Mode 0 disables sphere mapping; mode 3 uses the first
                 * additional UV channel retained by RFM2 v6 and newer. */
                if (!active_disable_sphere && policy.use_sphere &&
                    (sphere_mode == 1 || sphere_mode == 2 ||
                     (sphere_mode == 3 && model->vertex_bytes >=
                      RASTERFALL_MODEL_VERTEX_BYTES_ADDITIONAL_UV)) &&
                    sphere_index != 0xffffU && sphere_index < model->textures.count &&
                    model->textures.assets[sphere_index].data) {
                    sphere_texture = &model->textures.views[sphere_index];
                    active_sphere_texture = sphere_texture;
                    active_sphere_mode = sphere_mode;
                } else active_sphere_texture = 0;
            }
            if (model->format_version >= 5 && !active_disable_toon &&
                policy.use_toon) {
                unsigned int toon_index = model->materials[material * model->material_bytes + 5];
                unsigned int toon_kind = model->materials[material * model->material_bytes + 6];
                if (toon_kind == 1 && toon_index < model->textures.count &&
                    model->textures.assets[toon_index].data) {
                    toon_texture = &model->textures.views[toon_index];
                    active_toon_texture = toon_texture;
                } else if (toon_kind == 2) active_toon_shared = (int)toon_index;
            }
        }
        if (!texture && shared_texture) active_texture_view = active_model_texture;
        phase_start = render_monotonic_us();
        for (j = index_begin; j + 2 < index_end; j += 3) {
            if ((j & 255U) == 0 &&
                toy_renderer_job_cancelled(renderer)) break;
            unsigned int ia = model_u32(indices + j * 4);
            unsigned int ib = model_u32(indices + (j + 1) * 4);
            unsigned int ic = model_u32(indices + (j + 2) * 4);
            struct vec3 a, b, c;
            struct world_uv_vertex ta, tb, tc;
            if (ia >= model->vertex_count || ib >= model->vertex_count || ic >= model->vertex_count) continue;
            {
                int nx = gallery_vertex_cache[ia].normal[0] +
                         gallery_vertex_cache[ib].normal[0] +
                         gallery_vertex_cache[ic].normal[0];
                int ny = gallery_vertex_cache[ia].normal[1] +
                         gallery_vertex_cache[ib].normal[1] +
                         gallery_vertex_cache[ic].normal[1];
                int nz = gallery_vertex_cache[ia].normal[2] +
                         gallery_vertex_cache[ib].normal[2] +
                         gallery_vertex_cache[ic].normal[2];
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
            if (texture || shared_texture) {
                gallery_uv_vertex(renderer, model, camera, ia, center_x, base_y, center_z, scale,
                                  active_sphere_texture ? active_sphere_mode : 0, &ta);
                gallery_uv_vertex(renderer, model, camera, ib, center_x, base_y, center_z, scale,
                                  active_sphere_texture ? active_sphere_mode : 0, &tb);
                gallery_uv_vertex(renderer, model, camera, ic, center_x, base_y, center_z, scale,
                                  active_sphere_texture ? active_sphere_mode : 0, &tc);
                drawn += draw_world_triangle_tex_views(renderer, camera,
                    &ta, &tb, &tc, &gallery_vertex_cache[ia].view,
                    &gallery_vertex_cache[ib].view,
                    &gallery_vertex_cache[ic].view);
            } else {
                a = gallery_vertex_cache[ia].uv.p;
                b = gallery_vertex_cache[ib].uv.p;
                c = gallery_vertex_cache[ic].uv.p;
                drawn += draw_world_triangle_views(renderer, camera,
                    &a, &b, &c, &gallery_vertex_cache[ia].view,
                    &gallery_vertex_cache[ib].view,
                    &gallery_vertex_cache[ic].view, color);
            }
        }
        body_us = render_monotonic_us() - phase_start;
        model_setup_timing.body_triangles_us += body_us;
        model_setup_timing.material_us += render_monotonic_us() -
            material_start - edge_us - body_us;
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
    active_face_material = previous_face_material;
    active_skin_material = previous_skin_material;
    active_material_tint = previous_material_tint;
    toy_renderer_set_base_texture_bilinear(renderer,
                                           previous_base_bilinear);
    active_model_triangle_stats = 0;
    if (collect_model_render_stats)
        model_render_stats.command_overflow += renderer->cmd_overflow -
                                               command_overflow_before;
    model_setup_timing.total_us += render_monotonic_us() - render_start;
    return drawn;
}

static int render_gallery_model(struct toy_renderer *renderer,
                                const struct camera *camera,
                                const struct rasterfall_model_asset *model,
                                int center_x, int base_y, int center_z,
                                int scale)
{
    return render_gallery_model_range(renderer, camera, model, center_x,
        base_y, center_z, scale, 0, (int)model->primitive_count, 0, -1, 1);
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
    __memset(&model_render_stats, 0, sizeof(model_render_stats));
    __memset(&model_setup_timing, 0, sizeof(model_setup_timing));
    collect_model_render_stats = 1;

    /* Fit every model into the same 900-unit inspection volume and center
     * its bounds at the origin.  A fixed camera distance then makes views
     * directly comparable across future imported character models. */
    scale = 900000 / size;
    if (scale < 1) scale = 1;
    center_x = -(int)(((long long)model->min_x + model->max_x) * scale / 2000);
    center_z = -(int)(((long long)model->min_z + model->max_z) * scale / 2000);
    base_y = -(int)((long long)height * scale / 2000);
    active_gallery_lighting = 1;
    active_disable_sphere = !use_sphere;
    active_disable_toon = !use_toon;
    active_disable_edge = !use_edge;
    active_disable_material_light = !use_material_light;
    pixels = render_gallery_model(renderer, camera, model, center_x, base_y,
                                  center_z, scale);
    collect_model_render_stats = 0;
    active_disable_sphere = 0;
    active_disable_toon = 0;
    active_disable_edge = 0;
    active_disable_material_light = 0;
    active_gallery_lighting = 0;
    return pixels;
}

void rasterfall_render_model_stats(struct rasterfall_model_render_stats *out)
{
    if (out) memcpy(out, &model_render_stats, sizeof(*out));
}

void rasterfall_render_model_setup_timing(
    struct rasterfall_model_setup_timing *out)
{
    if (out) memcpy(out,
        &rasterfall_render_frontend_current(0)->timing, sizeof(*out));
}

/* The benchmark intentionally exercises the private frontend implementation;
 * keep that test fixture out of the scene-rendering flow. */
#include "dev-tests/rasterfall_actor_benchmark.inc"
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

/* Character frontend ownership, worker binding and command merging form one
 * concurrency boundary.  Keep them together and separate from scene order. */
struct character_frontend_dispatch {
    const struct camera *camera;
    const struct toy_surface *surface;
    const struct rasterfall_model_asset *models[2];
    int *depth;
    unsigned char visible[2];
    int drawn[2];
    long wall_us[2];
};

static void prepare_character_command_renderer(
    struct toy_renderer *commands, struct rasterfall_frontend_state *state,
    int *initialized, const struct toy_surface *surface, int *depth)
{
    if (!*initialized) {
        toy_renderer_init(commands);
        state->toon_shared = -1;
        state->toon_level = 255;
        state->material_alpha = 255;
        state->material_double_sided = 1;
        *initialized = 1;
    }
    state->texture_view = active_wall_texture;
    __memset(&state->timing, 0, sizeof(state->timing));
    commands->surface = *surface;
    commands->depth = depth;
    commands->cmd_count = 0;
    commands->cmd_overflow = 0;
    commands->submitted_triangles = 0;
    commands->submitted_vertices = 0;
    commands->textured_triangles = 0;
    commands->textured_pixels = 0;
    commands->texture_fallback_pixels = 0;
    commands->job_cancelled = 0;
}

static void gallery_face_toward(struct rasterfall_frontend_state *state,
                                int model_x, int model_z,
                                int target_x, int target_z)
{
    long long dx = (long long)target_x - model_x;
    long long dz = (long long)target_z - model_z;
    long long length = isqrt(dx * dx + dz * dz);
    if (length <= 0) {
        state->gallery_facing = 0;
        return;
    }
    /* Maid's local forward axis is +Z.  The gallery transform maps that
     * axis to (sin_yaw, cos_yaw), so derive the yaw from the actual target
     * point instead of applying a model-specific 180-degree correction. */
    state->gallery_facing = 1;
    state->gallery_sy = (int)(dx * 1024 / length);
    state->gallery_cy = (int)(dz * 1024 / length);
}

static void character_frontend_job(int worker_id, int task, void *opaque)
{
    struct character_frontend_dispatch *dispatch = opaque;
    struct toy_renderer *commands;
    struct rasterfall_frontend_state *state;
    const struct rasterfall_model_asset *model;
    int x, y, z, scale;
    long start = render_monotonic_us();
    dispatch->drawn[task] = 0;
    dispatch->wall_us[task] = 0;
    if (!dispatch->visible[task]) return;
    if (task == 0) {
        commands = &private_character_commands;
        state = &private_character_frontend;
        model = dispatch->models[0];
        x = -13000; y = -900; z = -10000;
        scale = character_model_scale(model, eula_actor_profile.target_height_mm);
    } else {
        struct rasterfall_developer_character *entry =
            &developer_characters[task - 1];
        commands = &entry->commands;
        state = &entry->frontend;
        model = dispatch->models[task];
        x = entry->x; y = entry->base_y; z = entry->z;
        scale = character_model_scale(model, entry->target_height_mm);
    }
    if (scale < 1) scale = 1;
    (void)worker_id;
    if (frontend_bind_worker(commands, state) < 0) return;
    if (toy_renderer_job_cancelled(commands)) goto cleanup;
    if (task > 0) {
        long animation_start = render_monotonic_us();
        if (!state->reuse_skinned_vertices || !state->skinned_vertices_valid)
            sample_developer_character(&developer_characters[task - 1]);
        state->timing.animation_sample_us +=
            render_monotonic_us() - animation_start;
    }
    if (toy_renderer_job_cancelled(commands)) goto cleanup;
    if (task > 0) {
        int target_x = active_session ? active_session->level.start_x : -13000;
        int target_z = active_session ? active_session->level.start_z : -12000;
        gallery_face_toward(state, x, z, target_x, target_z);
    } else {
        /* Eula's authored local forward is -Z, so the unrotated display
         * model already faces the player spawn side of the gallery. */
        state->gallery_facing = 0;
    }
    dispatch->drawn[task] = render_gallery_model_range(
        commands, dispatch->camera, model, x, y, z, scale, 0,
        (int)model->primitive_count, 0, -1, 1);
    dispatch->wall_us[task] = render_monotonic_us() - start;
cleanup:
    frontend_unbind_worker(commands);
}

static int render_characters_parallel(struct toy_renderer *renderer,
                                      const struct camera *camera)
{
    struct character_frontend_dispatch dispatch;
    const struct toy_renderer *command_sources[5];
    int i, source_count = 0, drawn = 0;
    long pipeline_start, merge_start;
    dispatch.camera = camera;
    dispatch.surface = &renderer->surface;
    dispatch.depth = renderer->depth;
    dispatch.models[0] = &private_character_model;
    if (!active_pose_preview && private_character_lod2_loaded &&
        character_distance_policy(camera, -13000, -10000, 0, NULL) >=
            RASTERFALL_CHARACTER_FAR)
        dispatch.models[0] = &private_character_lod2_model;
    else if (!active_pose_preview && private_character_lod_loaded &&
             character_distance_policy(camera, -13000, -10000, 0, NULL) >=
                 RASTERFALL_CHARACTER_MID)
        dispatch.models[0] = &private_character_lod_model;
    else if (!active_pose_preview && private_character_lod_loaded &&
             near_original_model_owner != 0 &&
             character_distance_policy(camera, -13000, -10000, 0, NULL) ==
                 RASTERFALL_CHARACTER_NEAR)
        dispatch.models[0] = &private_character_lod_model;
    dispatch.visible[0] = !active_pose_preview && private_character_model.data &&
        character_distance_policy(camera, -13000, -10000, 0,
            &private_character_frontend) != RASTERFALL_CHARACTER_HIDDEN &&
        gallery_model_visible(dispatch.surface, camera,
            &private_character_model, -13000, -900, -10000,
            character_model_scale(&private_character_model,
                                  eula_actor_profile.target_height_mm));
    prepare_character_command_renderer(&private_character_commands,
        &private_character_frontend, &private_character_commands_initialized,
        dispatch.surface, dispatch.depth);
    private_character_commands.job_cancel_flag = &renderer->job_cancelled;
    private_character_frontend.timing.animation_sample_us =
        private_character_animation_us;
    for (i = 0; i < RASTERFALL_DEVELOPER_CHARACTER_COUNT; i++) {
        struct rasterfall_developer_character *entry = &developer_characters[i];
        /* Keep the single Maid display independent from the Eula preview. */
        dispatch.visible[i + 1] = !active_pose_preview && entry->model.data &&
            character_distance_policy(camera, entry->x, entry->z, i + 1,
                &entry->frontend) != RASTERFALL_CHARACTER_HIDDEN &&
            gallery_model_visible(dispatch.surface, camera, &entry->model,
                entry->x, entry->base_y, entry->z,
                character_model_scale(&entry->model, entry->target_height_mm));
        prepare_character_command_renderer(&entry->commands, &entry->frontend,
            &entry->commands_initialized, dispatch.surface, dispatch.depth);
        entry->commands.job_cancel_flag = &renderer->job_cancelled;
        dispatch.models[i + 1] = &entry->model;
        if (entry->lod2_loaded &&
            character_distance_policy(camera, entry->x, entry->z, i + 1,
                                      NULL) >= RASTERFALL_CHARACTER_FAR)
            dispatch.models[i + 1] = &entry->lod2_model;
        else if (entry->lod_loaded &&
            character_distance_policy(camera, entry->x, entry->z, i + 1,
                                      NULL) >= RASTERFALL_CHARACTER_MID)
            dispatch.models[i + 1] = &entry->lod_model;
        else if (entry->lod_loaded && near_original_model_owner != i + 1 &&
                 character_distance_policy(camera, entry->x, entry->z, i + 1,
                                           NULL) == RASTERFALL_CHARACTER_NEAR)
            dispatch.models[i + 1] = &entry->lod_model;
    }
    {
        private_character_frontend.disable_edge |= edge_pass_enabled == 0;
    }
    pipeline_start = render_monotonic_us();
    if (toy_renderer_parallel_for(renderer, 2, 2,
                                  character_frontend_job, &dispatch) < 0)
        return -1;
    scene_stats.character_prepare_wall_us =
        render_monotonic_us() - pipeline_start;
    for (i = 0; i < 2; i++) if (dispatch.visible[i])
        command_sources[source_count++] = i == 0 ?
            &private_character_commands : &developer_characters[i - 1].commands;
    merge_start = render_monotonic_us();
    if (source_count > 0 && toy_renderer_merge_command_batch(renderer,
            command_sources, source_count, 2) < 0)
        return -1;
    scene_stats.character_merge_us = render_monotonic_us() - merge_start;
    for (i = 0; i < 2; i++) {
        struct rasterfall_frontend_state *state = i == 0 ?
            &private_character_frontend : &developer_characters[i - 1].frontend;
        drawn += dispatch.drawn[i];
        scene_stats.character_visible[i] = dispatch.visible[i];
        scene_stats.character_edge_disabled[i] = state->disable_edge;
        scene_stats.character_wall_us[i] = dispatch.wall_us[i];
        scene_stats.character_animation_us[i] = state->timing.animation_sample_us;
        scene_stats.character_skin_us[i] = state->timing.skinning_us;
        scene_stats.character_vertex_us[i] = state->timing.vertex_cache_us;
        scene_stats.character_triangle_us[i] = state->timing.material_us +
            state->timing.body_triangles_us + state->timing.edge_triangles_us;
        scene_stats.character_triangles[i] = i == 0 ?
            private_character_commands.submitted_triangles :
            developer_characters[i - 1].commands.submitted_triangles;
    }
    return drawn;
}
/* Local-only copyright-restricted character sample.  It is deliberately
 * outside the public asset gallery and is never embedded into release builds. */
static int render_private_character(struct toy_renderer *renderer,
                                    const struct camera *camera)
{
    const char *path = private_character_override_path ?
        private_character_override_path : eula_actor_profile.model_path;
    enum rasterfall_character_distance_quality character_quality;
    int character_update;
    if (!private_character_loaded) {
        int frame_budget = renderer->frame_budget_ms;
        toy_renderer_set_frame_budget(renderer, 0);
        if (rasterfall_model_load(&private_character_model, path) == 0) {
            __printf("rasterfall: Eula target_height_mm=%d scale_milli=%d\n",
                     eula_actor_profile.target_height_mm,
                     character_model_scale(&private_character_model,
                                           eula_actor_profile.target_height_mm));
            /* Pair the stock model only with the LOD regenerated from it.
             * Custom model overrides retain their existing full-model path. */
            if (!strcmp(path, eula_actor_profile.model_path) &&
                rasterfall_model_load(&private_character_lod_model,
                    "rasterfall/private-assets/models/eula_lod1.rmesh") == 0 &&
                private_character_lod_model.bone_count ==
                    private_character_model.bone_count) {
                private_character_lod_loaded = 1;
                __printf("rasterfall: Eula LOD vertices=%u triangles=%u\n",
                         private_character_lod_model.vertex_count,
                         private_character_lod_model.index_count / 3);
            }
            if (!strcmp(path, eula_actor_profile.model_path) &&
                rasterfall_model_load(&private_character_lod2_model,
                    "rasterfall/private-assets/models/eula_lod2.rmesh") == 0 &&
                private_character_lod2_model.bone_count ==
                    private_character_model.bone_count) {
                private_character_lod2_loaded = 1;
                __printf("rasterfall: Eula LOD2 vertices=%u triangles=%u\n",
                         private_character_lod2_model.vertex_count,
                         private_character_lod2_model.index_count / 3);
            }
        }
        toy_renderer_set_frame_budget(renderer, frame_budget);
        private_character_loaded = 1;
    }
    character_quality = character_distance_policy(
        camera, -13000, -10000, 0, &private_character_frontend);
    if (character_quality == RASTERFALL_CHARACTER_HIDDEN) return 0;
    character_update = !private_character_frontend.reuse_skinned_vertices;
    {
        const char *requested_vmd = private_character_vmd_path;
        if (active_session &&
            (active_session->skeletal_demo_player.clip_id == 9 ||
             active_session->skeletal_demo_player.clip_id == 11))
            requested_vmd = eula_actor_profile.walk_path;
        if (active_session && active_session->skeletal_demo_player.clip_id == 10)
            requested_vmd = "rasterfall/private-assets/animations/曼珠沙華.vmd";
        if (private_character_vmd_loaded && requested_vmd &&
            strcmp(private_character_vmd_loaded_path, requested_vmd)) {
            rasterfall_vmd_unload(&private_character_vmd);
            private_character_vmd_loaded = 0;
            private_character_vmd_loaded_path[0] = 0;
        }
        if (requested_vmd && !private_character_vmd_loaded &&
        private_character_model.data) {
            int frame_budget = renderer->frame_budget_ms;
            toy_renderer_set_frame_budget(renderer, 0);
            if (rasterfall_vmd_load(&private_character_vmd,
                                requested_vmd) == 0) {
            rasterfall_vmd_map_model(&private_character_vmd,
                                    &private_character_model);
            rasterfall_model_bind_root_motion(
                &private_character_model,
                rasterfall_model_find_bone(&private_character_model,"センター"),
                rasterfall_model_find_bone(&private_character_model,"グルーブ"));
            rasterfall_vmd_build_animation(&private_character_vmd,
                &private_character_vmd_clip, private_character_vmd_tracks,
                RASTERFALL_VMD_MAX_BONES);
            {
                int i;
                for (i = 0; i < private_character_vmd_clip.track_count; i++) {
                    int bone = private_character_vmd_tracks[i].target_bone;
                    const char *name = bone >= 0 ?
                        private_character_model.bones[bone].name : "";
                    if ((private_character_vmd_freeze_head &&
                         (!strcmp(name, "頭") || !strcmp(name, "首"))) ||
                        (private_character_vmd_freeze_torso &&
                         (!strcmp(name, "上半身") || !strcmp(name, "上半身2"))))
                        private_character_vmd_tracks[i].target_bone = -1;
                }
            }
            private_character_vmd_loaded = 1;
            snprintf(private_character_vmd_loaded_path,
                     sizeof(private_character_vmd_loaded_path), "%s",
                     requested_vmd);
            __printf("rasterfall: VMD walk loaded on character model: %s\n",
                     requested_vmd);
        } else __fprintf(2, "rasterfall: cannot load VMD walk %s\n",
                         requested_vmd);
            toy_renderer_set_frame_budget(renderer, frame_budget);
    }
    }
    if (private_character_model.data && active_session && character_update) {
        struct rasterfall_animation_player *player =
            &active_session->skeletal_demo_player;
        int pose_preview_active = active_session->pose_debug_active &&
            active_session->pose_editor.active &&
            active_session->pose_editor.character == 0 &&
            active_session->pose_editor.weapon == TOY_GAME_WEAPON_AK;
        {
            int zero[3] = {0, 0, 0};
            rasterfall_model_set_root_motion(&private_character_model,
                                             zero, zero, 0);
            private_character_model.animation_offset[0] = 0;
            private_character_model.animation_offset[1] = 0;
            private_character_model.animation_offset[2] = 0;
        }
        long sample_start = render_monotonic_us();
        /* The no-argument VMD demo starts with clip_id == -1.  Once the
         * player selects one of the regular/GLB buttons, that selection must
         * own the model pose; the VMD path is only active for its two
         * explicit clips (or the initial forced demo state). */
        if (private_character_vmd_loaded &&
            (player->clip_id == 9 || player->clip_id == 10 ||
             player->clip_id == 11 ||
             (private_character_vmd_forced && player->clip_id < 0 &&
              !pose_preview_active))) {
            rasterfall_model_set_ik_enabled(&private_character_model,
                                            private_character_vmd_ik_enabled);
            rasterfall_model_set_grant_enabled(&private_character_model,
                                               private_character_vmd_grant_enabled);
            rasterfall_model_set_legacy_knee_ccd(
                &private_character_model, private_character_vmd_legacy_knee_ccd);
            player->clip = &private_character_vmd_clip;
            /* The VMD clip is loaded lazily by this render path.  The logic
             * player may have been initialized before that load (or may have
             * been stopped while its clip was still NULL), so binding the
             * actual clip must also re-arm the forced direct-VMD preview. */
            player->playing = pose_preview_active ?
                active_session->pose_editor.animation_playing : 1;
            player->speed_milli = 1000;
            player->loop = 1;
            {
                int center[3], groove[3];
                rasterfall_vmd_sample_bone_translation(&private_character_vmd,
                    "センター", player->time_ms, center);
                rasterfall_vmd_sample_bone_translation(&private_character_vmd,
                    "グルーブ", player->time_ms, groove);
                rasterfall_model_set_root_motion(
                    &private_character_model,center,groove,
                    !private_character_vmd_legacy_root_offset);
                private_character_model.animation_offset[0] = 0;
                private_character_model.animation_offset[1] = 0;
                private_character_model.animation_offset[2] = 0;
                if (private_character_vmd_legacy_root_offset) {
                    private_character_model.animation_offset[0] = center[0] + groove[0];
                    private_character_model.animation_offset[1] = center[1] + groove[1];
                    private_character_model.animation_offset[2] = center[2] + groove[2];
                }
            }
            if (player->clip_id == 11) {
                struct rasterfall_rifle_pose preview_pose;
                struct rasterfall_animation_composition composition;
                const struct rasterfall_pose_calibration *preview_calibration =
                    rasterfall_pose_calibration_resolve(
                        &active_session->pose_editor, 0,
                        TOY_GAME_WEAPON_AK);
                memcpy(preview_pose.rotation, preview_calibration->body_pose,
                       sizeof(preview_pose.rotation));
                composition.locomotion = player->clip;
                composition.locomotion_time_ms = player->time_ms;
                composition.locomotion_weight_milli = 1000;
                composition.rifle_stance = 1;
                composition.upper_body_lock =
                    active_session->pose_debug_active ?
                    active_session->pose_editor.upper_body_lock : 1;
                composition.overlay = active_session->pose_debug_active ?
                    (active_session->pose_editor.animation_overlay == 1 ?
                     RASTERFALL_COMPOSITION_OVERLAY_FIRE :
                     active_session->pose_editor.animation_overlay == 2 ?
                     RASTERFALL_COMPOSITION_OVERLAY_HIT :
                     RASTERFALL_COMPOSITION_OVERLAY_NONE) :
                    ((player->time_ms / 1200) & 1 ?
                     RASTERFALL_COMPOSITION_OVERLAY_HIT :
                     RASTERFALL_COMPOSITION_OVERLAY_FIRE);
                composition.overlay_time_ms = player->time_ms % 1200;
                composition.rifle_pose = &preview_pose;
                composition.hit_pose = NULL;
                composition.hit_pose_preview = 0;
                rasterfall_animation_compose(&private_character_model,&composition);
            } else rasterfall_model_sample_clip(&private_character_model,
                                                player->clip,player->time_ms);
        } else if (player->clip_id >= 0 && player->clip_id < 3) {
            player->clip = &private_character_model.animation.demo_clips[player->clip_id];
            player->loop = player->clip->loop;
            rasterfall_model_sample_clip(&private_character_model, player->clip,
                                         player->time_ms);
        } else if (player->clip_id >= 3 && player->clip_id <= 5 &&
                   private_character_load_glb_clip(0) == 0 &&
                   private_character_load_glb_clip(player->clip_id - 3) == 0) {
            int clip_index = player->clip_id - 3;
            player->clip = &private_character_timing[clip_index];
            player->loop = 1;
            rasterfall_model_sample_glb_rotation_clip(
                &private_character_model, &private_character_glb[clip_index],
                &private_character_glb_reference, player->time_ms);
        } else if (player->clip_id >= 6 && player->clip_id <= 8 &&
                   private_character_load_glb_clip(player->clip_id - 6) == 0) {
            int clip_index=player->clip_id-6;
            player->clip=&private_character_timing[clip_index];player->loop=1;
            rasterfall_model_sample_clip(&private_character_model,NULL,0);
        } else {
            player->clip = NULL;
            rasterfall_model_sample_clip(&private_character_model, NULL, 0);
        }
        if (pose_preview_active &&
            active_session->pose_editor.animation_base == 0) {
            struct rasterfall_rifle_pose preview_pose;
            struct rasterfall_animation_composition composition;
            const struct rasterfall_pose_calibration *preview_calibration =
                rasterfall_pose_calibration_resolve(
                    &active_session->pose_editor, 0, TOY_GAME_WEAPON_AK);
            memcpy(preview_pose.rotation, preview_calibration->body_pose,
                   sizeof(preview_pose.rotation));
            composition.locomotion = NULL;
            composition.locomotion_time_ms = 0;
            composition.locomotion_weight_milli = 1000;
            composition.rifle_stance = 1;
            composition.upper_body_lock =
                active_session->pose_editor.upper_body_lock;
            composition.overlay = active_session->pose_editor.animation_overlay == 1 ?
                RASTERFALL_COMPOSITION_OVERLAY_FIRE :
                active_session->pose_editor.animation_overlay == 2 ?
                RASTERFALL_COMPOSITION_OVERLAY_HIT :
                RASTERFALL_COMPOSITION_OVERLAY_NONE;
            composition.overlay_time_ms = active_session->pose_editor.animation_time_ms;
            composition.rifle_pose = &preview_pose;
            composition.hit_pose = NULL;
            composition.hit_pose_preview = 0;
            rasterfall_animation_compose(&private_character_model, &composition);
        }
        private_character_animation_us = render_monotonic_us() - sample_start;
        scene_stats.character_animation_outside_us =
            private_character_animation_us;
    }
    sync_private_character_lod_pose();
    sync_private_character_lod2_pose();
    if (!developer_characters[0].load_attempted) {
        int frame_budget = renderer->frame_budget_ms;
        toy_renderer_set_frame_budget(renderer, 0);
        load_developer_characters(renderer);
        toy_renderer_set_frame_budget(renderer, frame_budget);
    }
    if (active_session && active_session->pose_debug_active &&
        active_session->pose_editor.active &&
        active_session->pose_editor.character == 1 &&
        active_session->pose_editor.weapon == TOY_GAME_WEAPON_AK)
        return render_maid_pose_preview(renderer, camera);
    if (!private_character_model.data ||
        character_quality == RASTERFALL_CHARACTER_HIDDEN)
        return 0;
    {
        int pixels = 0;
        active_pose_preview = active_session->pose_editor.active &&
            active_session->pose_editor.character == 0 &&
            active_session->pose_editor.weapon == TOY_GAME_WEAPON_AK;
        select_near_original_model(camera, &renderer->surface);
        /* Solve the preview hand before the character frontend skins and
         * rasterizes it.  Solving after render would only move the next
         * frame's attachment target, leaving the visible hand one frame/state
         * behind the weapon. */
        if (active_session->pose_editor.active &&
            active_session->pose_editor.character == 0 &&
            active_session->pose_editor.weapon == TOY_GAME_WEAPON_AK &&
            active_session->pose_editor.pose.left_ik) {
            struct rasterfall_model_attachment_transform rifle_frame;
            if (rifle_frame_transform(&private_character_model,&rifle_frame)==0) {
                const struct rasterfall_pose_calibration *wp =
                    rasterfall_pose_calibration_resolve(&active_session->pose_editor, 0, TOY_GAME_WEAPON_AK);
                const struct rasterfall_weapon_asset_profile *asset =
                    rasterfall_weapon_asset_profile(TOY_GAME_WEAPON_AK);
                int character_scale = character_model_scale(
                    &private_character_model, eula_actor_profile.target_height_mm);
                rifle_solve_hands(&private_character_model,&rifle_frame,wp,
                                  asset,character_scale);
            }
        }
        int character_pixels = render_characters_parallel(renderer, camera);
        if (character_pixels < 0)
            __fprintf(2, "rasterfall: parallel character rendering failed\n");
        else
            pixels += character_pixels;
        if (active_pose_preview && private_character_model.data) {
            /* Render the preview character with the same Eula forward basis
             * used by the actor gallery path.  The parallel path skips this
             * model above so it is drawn exactly once. */
            active_gallery_facing = 1;
            active_gallery_sy = 0;
            active_gallery_cy = eula_actor_profile.forward_cy;
            pixels += render_gallery_model(renderer, camera,
                &private_character_model, -13000, -900, -10000,
                character_model_scale(&private_character_model,
                                      eula_actor_profile.target_height_mm));
            active_gallery_facing = 0;
        }
        if (active_session->pose_editor.active &&
            active_session->pose_editor.character == 0 &&
            active_session->pose_editor.weapon == TOY_GAME_WEAPON_AK) {
            struct rasterfall_model_attachment_transform rifle_frame;
            if (rifle_frame_transform(&private_character_model,&rifle_frame)==0) {
                /* Left-hand IK was already solved before skinning.  Solving
                 * it again here cannot affect the character just drawn and
                 * only mutates the persistent solver history for next frame. */
                const struct rasterfall_pose_calibration *wp = render_pose_calibration(TOY_GAME_WEAPON_AK);
                pixels += render_skeletal_rifle(renderer, camera,
                    &private_character_model, &rifle_frame, &eula_actor_profile,wp,
                    -13000, -900, -10000, 0, 1024,
                    TOY_GAME_WEAPON_AK, 0);
            }
        }
        active_pose_preview = 0;
        {int clip_id=active_session?active_session->skeletal_demo_player.clip_id:-1;
         long preview_start=render_monotonic_us();
         pixels+=render_quaternius_preview(renderer,camera,
                clip_id>=6&&clip_id<=8?clip_id-6:-1,
                active_session?active_session->skeletal_demo_player.time_ms:0);
         scene_stats.character_preview_us=render_monotonic_us()-preview_start;}
        return pixels;
    }
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
        for (i = 0; i < level_map.primitive_count; i++) {
            const struct toy_map_primitive *b = &level_map.primitives[i];
            int dx = wx < b->minx ? b->minx - wx : wx > b->maxx ? wx - b->maxx : 0;
            int dz = wz < b->minz ? b->minz - wz : wz > b->maxz ? wz - b->maxz : 0;
            int dist = dx > dz ? dx : dz;
            if (b->shape == TOY_MAP_PRIMITIVE_BOX &&
                strncmp(b->role, "air_gate", 8) && dist < 900)
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
    out->x = a->x + (int)(((long long)b->x - (long long)a->x) *
                          numerator / denominator);
    out->y = a->y + (int)(((long long)b->y - (long long)a->y) *
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
    out->light = in->light;
}

static void near_intersection_uv(const struct world_uv_vertex *a,
                                 const struct world_uv_vertex *b,
                                 struct world_uv_vertex *out)
{
    long numerator = NEAR_Z - a->p.z;
    long denominator = b->p.z - a->p.z;
    near_intersection(&a->p, &b->p, &out->p);
    out->u = a->u + (int)(((long long)b->u - a->u) * numerator / denominator);
    out->v = a->v + (int)(((long long)b->v - a->v) * numerator / denominator);
    out->su = a->su + (int)(((long long)b->su - a->su) * numerator / denominator);
    out->sv = a->sv + (int)(((long long)b->sv - a->sv) * numerator / denominator);
    out->light = a->light +
        (int)(((long long)b->light - a->light) * numerator / denominator);
}

/* Windows x86_64 follows LLP64, where long remains 32-bit.  A Q16 UV near
 * 65535 multiplied by the inverse-depth scale is roughly 68 billion, so the
 * product must be widened before division even though the final term fits in
 * long on every supported target. */
static long perspective_uv_term(int uv, int z)
{
    return (long)((long long)uv * 1048576LL / z);
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

int rasterfall_render_near_clip_test(void)
{
    struct world_uv_vertex input[3], output[4];
    int count, i, intersections = 0;
    __memset(input, 0, sizeof(input));
    input[0].p.z = 20;
    input[1].p.z = input[2].p.z = 200;
    input[0].u = 0; input[0].v = 1000;
    input[0].su = 2000; input[0].sv = 3000;
    input[1].u = input[2].u = 30000;
    input[1].v = input[2].v = 31000;
    input[1].su = input[2].su = 32000;
    input[1].sv = input[2].sv = 33000;
    count = clip_near_uv(input, 3, output);
    if (count != 4) return 1;
    for (i = 0; i < count; i++) {
        if (output[i].p.z < NEAR_Z) return 2;
        if (output[i].p.z == NEAR_Z) {
            intersections++;
            if (output[i].u <= input[0].u || output[i].u >= input[1].u ||
                output[i].v <= input[0].v || output[i].v >= input[1].v ||
                output[i].su <= input[0].su || output[i].su >= input[1].su ||
                output[i].sv <= input[0].sv || output[i].sv >= input[1].sv)
                return 3;
        }
    }
    if (intersections != 2) return 4;
    input[1].p.z = 20;
    if (clip_near_uv(input, 3, output) != 3) return 5;
    input[2].p.z = 20;
    if (clip_near_uv(input, 3, output) != 0) return 6;
    if (perspective_uv_term(65535, NEAR_Z) != 1073725440L) return 7;
    if (perspective_uv_term(-65535, NEAR_Z) != -1073725440L) return 8;
    return 0;
}

static void project_vertex(const struct toy_surface *surface,
                           const struct vec3 *view,
                           struct toy_screen_vertex *screen)
{
    int focal = surface->width * 3 / 4;
    int z = view->z < NEAR_Z ? NEAR_Z : view->z;
    screen->x = surface->width / 2 +
                (int)((long long)view->x * focal / z);
    screen->y = surface->height / 2 -
                (int)((long long)view->y * focal / z);
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
                              int use_secondary_uv,
                              struct toy_screen_vertex *screen)
{
    int z = view->z < NEAR_Z ? NEAR_Z : view->z;
    project_vertex(surface, view, screen);
    screen->u = u; screen->v = v;
    screen->u_over_z = perspective_uv_term(u, z);
    screen->v_over_z = perspective_uv_term(v, z);
    if (use_secondary_uv) {
        screen->u2 = su; screen->v2 = sv;
        screen->u2_over_z = perspective_uv_term(su, z);
        screen->v2_over_z = perspective_uv_term(sv, z);
    } else {
        screen->u2 = screen->v2 = 0;
        screen->u2_over_z = screen->v2_over_z = 0;
    }
}

static int draw_world_triangle_views(struct toy_renderer *renderer,
                               const struct camera *camera,
                               const struct vec3 *a, const struct vec3 *b,
                               const struct vec3 *c,
                               const struct vec3 *va, const struct vec3 *vb,
                               const struct vec3 *vc, uint32_t color)
{
    struct vec3 input[3], clipped[4];
    int count, drawn = 0;
    if (va && vb && vc) {
        input[0] = *va; input[1] = *vb; input[2] = *vc;
    } else {
        world_to_view(camera, a, &input[0]);
        world_to_view(camera, b, &input[1]);
        world_to_view(camera, c, &input[2]);
    }
    if (input[0].z >= NEAR_Z && input[1].z >= NEAR_Z &&
        input[2].z >= NEAR_Z) {
        clipped[0] = input[0]; clipped[1] = input[1]; clipped[2] = input[2];
        count = 3;
    } else count = clip_near(input, 3, clipped);
    if (active_model_triangle_stats) {
        active_model_triangle_stats->total_triangles++;
        if (count < 3) active_model_triangle_stats->near_rejected_triangles++;
        else if (input[0].z < NEAR_Z || input[1].z < NEAR_Z ||
                 input[2].z < NEAR_Z)
            active_model_triangle_stats->near_clipped_triangles++;
    }
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
            if (!active_material_double_sided) {
                if (active_model_triangle_stats)
                    active_model_triangle_stats->backface_culled_triangles++;
                continue;
            }
            swap.x = sb.x; swap.y = sb.y; swap.z = sb.z;
            swap.inv_z = sb.inv_z;
            sb.x = sc.x; sb.y = sc.y; sb.z = sc.z;
            sb.inv_z = sc.inv_z;
            sc.x = swap.x; sc.y = swap.y; sc.z = swap.z;
            sc.inv_z = swap.inv_z;
        }
        int center_x = (a->x + b->x + c->x) / 3;
        int center_z = (a->z + b->z + c->z) / 3;
        int light = active_gallery_lighting ? 256 :
                    fixed_floor_lighting ? 256 : baked_light_at(center_x, center_z);
        int fog = active_gallery_lighting ? 0 : fixed_floor_lighting ? 0 :
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
        if (active_model_triangle_stats)
            active_model_triangle_stats->emitted_triangles++;
    }
    return drawn;
}

static int draw_world_triangle(struct toy_renderer *renderer,
                               const struct camera *camera,
                               const struct vec3 *a, const struct vec3 *b,
                               const struct vec3 *c, uint32_t color)
{
    return draw_world_triangle_views(renderer, camera, a, b, c, 0, 0, 0,
                                     color);
}

static int draw_world_triangle_alpha(struct toy_renderer *renderer,
                                     const struct camera *camera,
                                     const struct vec3 *a,
                                     const struct vec3 *b,
                                     const struct vec3 *c,
                                     uint32_t color, int alpha)
{
    struct vec3 input[3], clipped[4];
    struct toy_screen_vertex screen[3];
    int count, i, drawn = 0;
    long long area;
    world_to_view(camera, a, &input[0]);
    world_to_view(camera, b, &input[1]);
    world_to_view(camera, c, &input[2]);
    if (input[0].z >= NEAR_Z && input[1].z >= NEAR_Z && input[2].z >= NEAR_Z) {
        clipped[0] = input[0]; clipped[1] = input[1]; clipped[2] = input[2];
        count = 3;
    } else count = clip_near(input, 3, clipped);
    if (count < 3) return 0;
    project_vertex(&renderer->surface, &clipped[0], &screen[0]);
    for (i = 1; i + 1 < count; i++) {
        project_vertex(&renderer->surface, &clipped[i], &screen[1]);
        project_vertex(&renderer->surface, &clipped[i + 1], &screen[2]);
        area = ((long long)screen[2].x - screen[0].x) *
                   ((long long)screen[1].y - screen[0].y) -
               ((long long)screen[2].y - screen[0].y) *
                   ((long long)screen[1].x - screen[0].x);
        if (area >= 0) {
            struct toy_screen_vertex swap = screen[1];
            screen[1] = screen[2]; screen[2] = swap;
        }
        drawn += toy_renderer_triangle_lit_alpha(
            renderer, &screen[0], &screen[1], &screen[2], color,
            fixed_floor_lighting ? 256 : baked_light_at((a->x + b->x + c->x) / 3,
                                                        (a->z + b->z + c->z) / 3),
            fixed_floor_lighting ? 0 : baked_fog_at(world_distance(
                camera, (a->x + b->x + c->x) / 3,
                (a->z + b->z + c->z) / 3)), alpha);
    }
    return drawn;
}

static int draw_quad_alpha(struct toy_renderer *renderer,
                           const struct camera *camera,
                           const struct vec3 *a, const struct vec3 *b,
                           const struct vec3 *c, const struct vec3 *d,
                           uint32_t color, int alpha)
{
    return draw_world_triangle_alpha(renderer, camera, a, b, c, color, alpha) +
           draw_world_triangle_alpha(renderer, camera, a, c, d, color, alpha);
}

static int draw_world_triangle_tex_views(struct toy_renderer *renderer,
                                    const struct camera *camera,
                                    const struct world_uv_vertex *a,
                                    const struct world_uv_vertex *b,
                                    const struct world_uv_vertex *c,
                                    const struct vec3 *va,
                                    const struct vec3 *vb,
                                    const struct vec3 *vc)
{
    struct world_uv_vertex input[3], clipped[4];
    int count, drawn = 0;
    if (va && vb && vc) {
        input[0].p = *va; input[1].p = *vb; input[2].p = *vc;
    } else {
        world_to_view(camera, &a->p, &input[0].p);
        world_to_view(camera, &b->p, &input[1].p);
        world_to_view(camera, &c->p, &input[2].p);
    }
    input[0].u = a->u; input[0].v = a->v;
    input[1].u = b->u; input[1].v = b->v;
    input[2].u = c->u; input[2].v = c->v;
    input[0].su = a->su; input[0].sv = a->sv;
    input[1].su = b->su; input[1].sv = b->sv;
    input[2].su = c->su; input[2].sv = c->sv;
    input[0].light = a->light;
    input[1].light = b->light;
    input[2].light = c->light;
    if (input[0].p.z >= NEAR_Z && input[1].p.z >= NEAR_Z &&
        input[2].p.z >= NEAR_Z) {
        clipped[0] = input[0]; clipped[1] = input[1]; clipped[2] = input[2];
        count = 3;
    } else count = clip_near_uv(input, 3, clipped);
    if (active_model_triangle_stats) {
        active_model_triangle_stats->total_triangles++;
        if (count < 3) active_model_triangle_stats->near_rejected_triangles++;
        else if (input[0].p.z < NEAR_Z || input[1].p.z < NEAR_Z ||
                 input[2].p.z < NEAR_Z)
            active_model_triangle_stats->near_clipped_triangles++;
    }
    for (int i = 1; i + 1 < count; i++) {
        struct toy_screen_vertex sa, sb, sc;
        long long area;
        int reversed = 0;
        project_uv_vertex(&renderer->surface, &clipped[0].p,
                          clipped[0].u, clipped[0].v,
                          clipped[0].su, clipped[0].sv,
                          active_sphere_texture != 0, &sa);
        project_uv_vertex(&renderer->surface, &clipped[i].p,
                          clipped[i].u, clipped[i].v,
                          clipped[i].su, clipped[i].sv,
                          active_sphere_texture != 0, &sb);
        project_uv_vertex(&renderer->surface, &clipped[i + 1].p,
                          clipped[i + 1].u, clipped[i + 1].v,
                          clipped[i + 1].su, clipped[i + 1].sv,
                          active_sphere_texture != 0, &sc);
        area = ((long long)sc.x - sa.x) * ((long long)sb.y - sa.y) -
               ((long long)sc.y - sa.y) * ((long long)sb.x - sa.x);
        if (area >= 0) {
            struct toy_screen_vertex swap;
            if (!active_material_double_sided) {
                if (active_model_triangle_stats)
                    active_model_triangle_stats->backface_culled_triangles++;
                continue;
            }
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
            reversed = 1;
        }
        int center_x = (a->p.x + b->p.x + c->p.x) / 3;
        int center_z = (a->p.z + b->p.z + c->p.z) / 3;
        int light = active_gallery_lighting ? 256 :
                    fixed_floor_lighting ? 256 : baked_light_at(center_x, center_z);
        int fog = active_gallery_lighting ? 0 :
                  fixed_floor_lighting ? 0 : baked_fog_at(world_distance(camera, center_x, center_z));
        if (active_face_material) {
            int face_scene_light = light < 224 ? 224 : light;
            sa.light = clipped[0].light * face_scene_light / 256;
            sb.light = (reversed ? clipped[i + 1].light : clipped[i].light) *
                       face_scene_light / 256;
            sc.light = (reversed ? clipped[i].light : clipped[i + 1].light) *
                       face_scene_light / 256;
            if (sa.light < 224) sa.light = 224;
            if (sb.light < 224) sb.light = 224;
            if (sc.light < 224) sc.light = 224;
            sa.fog = sb.fog = sc.fog = fog / 2;
        } else if (active_skin_material) {
            int skin_scene_light = light < 224 ? 224 : light;
            sa.light = clipped[0].light * skin_scene_light / 256;
            sb.light = (reversed ? clipped[i + 1].light : clipped[i].light) *
                       skin_scene_light / 256;
            sc.light = (reversed ? clipped[i].light : clipped[i + 1].light) *
                       skin_scene_light / 256;
            if (sa.light < 224) sa.light = 224;
            if (sb.light < 224) sb.light = 224;
            if (sc.light < 224) sc.light = 224;
            if (sa.light > 256) sa.light = 256;
            if (sb.light > 256) sb.light = 256;
            if (sc.light > 256) sc.light = 256;
            sa.fog = sb.fog = sc.fog = fog;
        } else {
            sa.light = sb.light = sc.light = light;
            sa.fog = sb.fog = sc.fog = fog;
        }
        if (active_sphere_texture || active_toon_texture ||
            active_toon_shared >= 0 || active_material_ambient ||
            active_material_specular)
            drawn += toy_renderer_triangle_textured_material_lit(
                renderer, &sa, &sb, &sc, active_texture_view,
                active_sphere_texture, active_sphere_mode,
                active_toon_texture, active_toon_shared, active_toon_level,
                active_material_alpha, active_material_ambient,
                active_material_specular, active_material_specular_level,
                active_material_tint,
                1, 0xFF202020U,
                active_face_material || active_skin_material ? -1 : light,
                active_face_material ? -1 : fog);
        else
            drawn += toy_renderer_triangle_textured_lit(renderer, &sa, &sb, &sc,
                                                         active_texture_view, 1,
                                                         0xFF202020U,
                                                         active_face_material ? -1 : light,
                                                         active_face_material ? -1 : fog);
        if (active_model_triangle_stats)
            active_model_triangle_stats->emitted_triangles++;
    }
    return drawn;
}

static int draw_world_triangle_tex(struct toy_renderer *renderer,
                                    const struct camera *camera,
                                    const struct world_uv_vertex *a,
                                    const struct world_uv_vertex *b,
                                    const struct world_uv_vertex *c)
{
    return draw_world_triangle_tex_views(renderer, camera, a, b, c, 0, 0, 0);
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

static int map_has_floor_ground(int x, int z)
{
    int i;
    for (i = 0; i < level_map.draw_count; i++) {
        const struct toy_map_draw *draw = &level_map.draw[i];
        if (draw->type == TOY_MAP_DRAW_FLOOR &&
            x >= draw->a && x < draw->b &&
            z >= draw->c && z < draw->d) return 1;
    }
    return 0;
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
                    /* The world rectangle is only an extent.  Do not paint
                     * its uncovered cells as a default floor. */
                    if (!map_has_floor_ground(center_x, center_z)) continue;
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
                            /* A ground directive supplies coverage only.  It
                             * deliberately leaves the checkerboard visible;
                             * floor remains the authored colour-paint layer. */
                            if (draw->type == TOY_MAP_DRAW_FLOOR &&
                                draw->style == TOY_MAP_FLOOR_GROUND) continue;
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

static int draw_box_alpha(struct toy_renderer *renderer,
                          const struct camera *camera,
                          const struct box *box, int alpha)
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
    pixels += draw_quad_alpha(renderer, camera, &a, &b, &f, &e, box->color, alpha);
    pixels += draw_quad_alpha(renderer, camera, &b, &c, &g, &f, box->color + 0x080808, alpha);
    pixels += draw_quad_alpha(renderer, camera, &c, &d, &h, &g, box->color, alpha);
    pixels += draw_quad_alpha(renderer, camera, &d, &a, &e, &h, box->color + 0x080808, alpha);
    pixels += draw_quad_alpha(renderer, camera, &e, &f, &g, &h, box->color + 0x181818, alpha);
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

static void oriented_world_point(int x, int z, int sy, int cy,
                                 int lx, int ly, int lz, int scale,
                                 struct vec3 *out)
{
    lx = lx * scale / 1000;
    ly = RASTERFALL_WORLD_GROUND_Y +
         (ly - RASTERFALL_WORLD_GROUND_Y) * scale / 1000;
    lz = lz * scale / 1000;
    int ry = -900 + ((ly + 900) * active_actor_roll_cos +
                     lz * active_actor_roll_sin) / 1024;
    int rz = (-(ly + 900) * active_actor_roll_sin +
              lz * active_actor_roll_cos) / 1024;
    out->x = x + (cy * lx + sy * rz) / 1024;
    out->y = ry + active_actor_lift;
    out->z = z + (-sy * lx + cy * rz) / 1024;
}

static void actor_world_point(int x, int z, int sy, int cy,
                              int lx, int ly, int lz, struct vec3 *out)
{
    oriented_world_point(x, z, sy, cy, lx, ly, lz,
        RASTERFALL_HUMAN_HEIGHT_RFU * 1000 /
        RASTERFALL_LEGACY_ACTOR_HEIGHT_RFU, out);
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
    long long dx = (long long)x1 - x0, dz = (long long)z1 - z0;
    long long len = isqrt(dx * dx + dz * dz);
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
        int on = 0;
        for (int effect_index = 0;
             effect_index < RASTERFALL_EFFECT_INSTANCE_SLOTS; effect_index++) {
            const struct rasterfall_effect_instance *effect =
                &effects.instances[effect_index];
            if (effect->active &&
                effect->kind == RASTERFALL_EFFECT_INSTANCE_KIND_INTERACTION_HIGHLIGHT &&
                effect->target_id == i) {
                on = 1;
                break;
            }
        }
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
                 it->kind == TOY_MAP_PICKUP_WAVE_SKIP_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_WEST_CORRIDOR_BUTTON)
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
        else if (it->kind == TOY_MAP_PICKUP_POSE_RESET_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_POSE_RIGHT_ARM_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_POSE_ARMS_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_POSE_BODY_BUTTON)
            pixels += render_special_button(renderer, camera, it->x, it->y,
                                             it->z, on,
                                             it->kind == TOY_MAP_PICKUP_POSE_BODY_BUTTON ? 2 :
                                             it->kind == TOY_MAP_PICKUP_POSE_ARMS_BUTTON);
        else if (it->kind == TOY_MAP_PICKUP_ANIM_IDLE_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_ANIM_WALK_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_ANIM_JOG_BUTTON)
            pixels += render_special_button(renderer, camera, it->x, it->y,
                                             it->z, on,
                                             it->kind == TOY_MAP_PICKUP_ANIM_JOG_BUTTON ? 2 :
                                             it->kind == TOY_MAP_PICKUP_ANIM_WALK_BUTTON);
        else if (it->kind == TOY_MAP_PICKUP_GLB_IDLE_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_GLB_WALK_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_GLB_JOG_BUTTON)
            pixels += render_special_button(renderer,camera,it->x,it->y,it->z,on,
                                             it->kind==TOY_MAP_PICKUP_GLB_JOG_BUTTON?2:
                                             it->kind==TOY_MAP_PICKUP_GLB_WALK_BUTTON);
        else if (it->kind == TOY_MAP_PICKUP_VMD_WALK_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_VMD_MANJUSAKA_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_ANIMATION_COMPOSITION_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_HUMANOID_POSE_DEBUG_BUTTON)
            pixels += render_special_button(renderer, camera, it->x, it->y,
                                             it->z, on, 1);
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
                        pixels += draw_world_triangle(renderer, camera,
                                                      &v[0], &v[1], &v[2],
                                                      color);
                    }
                }
            }
        }
    }
    active_texture_view = previous_texture;
    return pixels;
}

/* ── 第一人称武器模型：视图空间盒体固定在镜头右下方，开火后坐后移 ── */

static void fill_rect(struct toy_surface *surface, int x, int y,
                      int width, int height, uint32_t color);
static uint32_t mix_color(uint32_t from, uint32_t to, int num, int den);

static int render_ramp(struct toy_renderer *renderer,
                       const struct camera *camera,
                       const struct toy_map_draw *ramp)
{
    struct vec3 a, b, c, d, base_a, base_b, base_c, base_d;
    int low = -900 + ramp->e, high = -900 + ramp->f;
    a.x = ramp->a; a.z = ramp->c;
    b.x = ramp->b; b.z = ramp->c;
    c.x = ramp->b; c.z = ramp->d;
    d.x = ramp->a; d.z = ramp->d;
    if (ramp->style == TOY_MAP_PRIMITIVE_RAMP_X) {
        a.y = low; b.y = high; c.y = high; d.y = low;
    } else {
        a.y = low; b.y = low; c.y = high; d.y = high;
    }
    base_a = a; base_a.y = -900;
    base_b = b; base_b.y = -900;
    base_c = c; base_c.y = -900;
    base_d = d; base_d.y = -900;
    return draw_quad(renderer, camera, &a, &b, &c, &d, ramp->color) +
           draw_quad(renderer, camera, &base_a, &base_b, &b, &a,
                     mix_color(ramp->color, 0x10151D, 1, 3)) +
           draw_quad(renderer, camera, &base_b, &base_c, &c, &b,
                     mix_color(ramp->color, 0x10151D, 1, 3)) +
           draw_quad(renderer, camera, &base_c, &base_d, &d, &c,
                     mix_color(ramp->color, 0x10151D, 1, 3)) +
           draw_quad(renderer, camera, &base_d, &base_a, &a, &d,
                     mix_color(ramp->color, 0x10151D, 1, 3));
}

static int render_platform(struct toy_renderer *renderer,
                           const struct camera *camera,
                           const struct toy_map_draw *platform)
{
    struct vec3 a, b, c, d;
    int y = -900 + platform->e;
    a.x = platform->a; a.y = y; a.z = platform->c;
    b.x = platform->b; b.y = y; b.z = platform->c;
    c.x = platform->b; c.y = y; c.z = platform->d;
    d.x = platform->a; d.y = y; d.z = platform->d;
    /* A platform is a walkable surface, not a volumetric wall.  Render only
     * its top so a large roof/platform used above an air gate cannot turn
     * into an opaque wall from the side.  Map syntax selects opaque or
     * translucent rendering; the default is hidden for utility platforms. */
    if (platform->style == 0) return 0;
    return platform->style == 2 ?
        draw_quad(renderer, camera, &a, &b, &c, &d, platform->color) :
        draw_quad_alpha(renderer, camera, &a, &b, &c, &d,
                        platform->color, 96);
}

static int render_scene(struct toy_renderer *renderer, const struct camera *camera)
{
    int pixels = 0;
    long phase_start;
    struct vec3 a, b, c, d;
    rasterfall_render_frame++;
    __memset(&scene_stats, 0, sizeof(scene_stats));
    phase_start = render_monotonic_us();
    /* 自由俯仰下先铺天空/地面：地平线由俯仰角决定，墙面与地板随后覆盖 */
    rasterfall_sky_draw(&renderer->surface, camera);
    fixed_floor_lighting = 1;
    pixels += draw_partitioned_floor(renderer, camera);
    fixed_floor_lighting = 0;
    if (active_coordinate_axes)
        pixels += render_coordinate_ruler(renderer, camera);
    scene_stats.sky_floor_us = render_monotonic_us() - phase_start;
    phase_start = render_monotonic_us();
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
        } else if (x->type==TOY_MAP_DRAW_RAMP) {
            pixels += render_ramp(renderer, camera, x);
        } else if (x->type==TOY_MAP_DRAW_PLATFORM) {
            pixels += render_platform(renderer, camera, x);
        } else if (x->type==TOY_MAP_DRAW_BOX) {
            struct box obstacle={x->a,x->b,x->c,x->d,x->e-900,x->color};
            if (!strncmp(x->text, "air_gate_", 9)) {
                if (active_session->air_walls_enabled)
                    pixels += draw_box_alpha(renderer, camera, &obstacle, 48);
            } else {
                pixels += draw_box(renderer,camera,&obstacle);
            }
        } else if (x->type==TOY_MAP_DRAW_LABEL) {
            struct toy_game_box zone={x->a,x->b,x->c,x->d,0,0}; draw_world_label(renderer,camera,&zone,x->text,x->color);
        } else if (x->type==TOY_MAP_DRAW_SIGN) {
            pixels += render_world_sign(renderer, camera, x);
        }
    }
    scene_stats.map_us = render_monotonic_us() - phase_start;
    phase_start = render_monotonic_us();
    pixels += render_model_gallery(renderer, camera);
    scene_stats.gallery_us = render_monotonic_us() - phase_start;
    phase_start = render_monotonic_us();
    pixels += render_private_character(renderer, camera);
    scene_stats.private_model_us = render_monotonic_us() - phase_start;
    phase_start = render_monotonic_us();
    pixels += render_projectiles(renderer, camera);
    scene_stats.projectiles_us = render_monotonic_us() - phase_start;
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
                             int sy, int cy, int scale, int side_x,
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
        oriented_world_point(x, z, sy, cy, local_x[i], local_y[i],
                             local_z[i], scale, &vertices[i]);
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
                                e->dir_x, e->dir_z, 1000, -390,
                                450, 0, fist_y, fist_z,
                                color + 0x101008);
    pixels += draw_tank_arm_box(renderer, camera, x, z,
                                e->dir_x, e->dir_z, 1000, 390,
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

static uint32_t enemy_feedback_color(int target_id)
{
    int i;
    for (i = 0; i < RASTERFALL_EFFECT_INSTANCE_SLOTS; i++) {
        const struct rasterfall_effect_instance *feedback = &effects.instances[i];
        if (feedback->active &&
            feedback->type == RASTERFALL_EFFECT_INSTANCE_MATERIAL &&
            feedback->kind == RASTERFALL_EFFECT_INSTANCE_KIND_ENEMY_HURT_TINT &&
            feedback->target_id == target_id)
            return feedback->color;
    }
    return 0;
}

/* 两种低多边形敌人；受击闪红/命中闪白，倒地时整体纵向压扁。 */
static int render_blob_shadow(struct toy_renderer *renderer,
                              const struct camera *camera,
                              const struct toy_game_enemy *e, int scale)
{
    struct vec3 a, b, c, d;
    int rx = 230 * scale / 1000;
    int rz = 150 * scale / 1000;
    int y = -886 + active_enemy_lift; /* just above the queried ground */
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
            {
                uint32_t feedback = enemy_feedback_color(i);
                if (feedback) color = feedback;
                else if (e->type == TOY_GAME_ENEMY_PURSUIT_HEAVY)
                    color = RF_COLOR_ENEMY_PURSUIT_HEAVY;
                else if (e->type == TOY_GAME_ENEMY_PURSUIT_FAST)
                    color = RF_COLOR_ENEMY_PURSUIT_FAST;
                else if (e->ai_state == TOY_GAME_ENEMY_TRACKING)
                    color = RF_COLOR_ENEMY_COMMON; /* PURSUIT_COMMON：沿用普通敌人颜色 */
            }
        }
        active_enemy_lift = e->ground_y;
        pixels += render_blob_shadow(renderer, camera, e, scale);
        active_enemy_lift += e->airborne_y;
        if (toy_game_enemy_info(e->type)->ability ==
                TOY_GAME_ENEMY_ABILITY_SMOKER_TONGUE &&
            e->special_target_active)
            pixels += render_smoker_tongue(renderer, camera, e);
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
        render_enemy_alert(renderer, camera, e, scale);
        active_enemy_lift = 0;
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
                                int character_id, uint32_t body_color, int downed,
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
                                     uint32_t body_color, int draw_arms)
{
    const char *path = rasterfall_weapon_model_path(weapon);
    struct rasterfall_model_asset *model;
    struct rasterfall_actor_pose pose;
    int width, height, depth, length, scale, anchor_x, anchor_z;
    int i, pixels = 0;
    if (!path) return 0;
    model = gallery_model_named(path, NULL);
    if (!model) return 0;
    rasterfall_actor_animation_sample(
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
    if (draw_arms) {
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
                                     animation_time_ms, body_color, 1);
}

static const struct rasterfall_pose_calibration *render_pose_calibration(int weapon)
{
    return rasterfall_pose_calibration_resolve(
        active_session ? &active_session->pose_editor : NULL, 0, weapon);
}

static int rifle_frame_transform(const struct rasterfall_model_asset *model,
                                 struct rasterfall_model_attachment_transform *frame)
{
    if (rasterfall_model_attachment_transform(model,"上半身2",frame)==0) return 0;
    return rasterfall_model_attachment_transform(model,"上半身",frame);
}

static void weapon_profile_local(const struct rasterfall_pose_calibration *p,
                                 const int in[3], int out[3])
{
    double x=in[0], y=in[1], z=in[2], a=p->yaw_offset*3.141592653589793/180.0;
    double b=p->pitch_offset*3.141592653589793/180.0, c=p->roll_offset*3.141592653589793/180.0;
    double x1=x*cos(a)-z*sin(a), z1=x*sin(a)+z*cos(a);
    double y1=y*cos(b)-z1*sin(b), z2=y*sin(b)+z1*cos(b);
    double x2=x1*cos(c)-y1*sin(c), y2=x1*sin(c)+y1*cos(c);
    out[0]=(int)x2+p->offset.x; out[1]=(int)y2+p->offset.y; out[2]=(int)z2+p->offset.z;
}

static void skeletal_attachment_point(
    const struct rasterfall_model_asset *character,
    const struct rasterfall_model_attachment_transform *hand,
    const struct rasterfall_skeletal_actor_profile *profile,
    int actor_x,int base_y,int actor_z,int actor_sy,int actor_cy,int scale,
    const struct rasterfall_pose_calibration *weapon_profile,
    const int local[3],struct vec3 *out)
{
    int calibrated[3];
    double rx,ry,rz;
    weapon_profile_local(weapon_profile,local,calibrated);
    rx=hand->rotation[0]*calibrated[0]+hand->rotation[1]*calibrated[1]+hand->rotation[2]*calibrated[2];
    ry=hand->rotation[3]*calibrated[0]+hand->rotation[4]*calibrated[1]+hand->rotation[5]*calibrated[2];
    rz=hand->rotation[6]*calibrated[0]+hand->rotation[7]*calibrated[1]+hand->rotation[8]*calibrated[2];
    int model_x=(int)(hand->position[0]*scale/1000.0+rx);
    int model_y=(int)((hand->position[1]-character->min_y)*scale/1000.0+ry);
    int model_z=(int)(hand->position[2]*scale/1000.0+rz);
    /* Use the same character basis as render_gallery_model.  The PMX Eula
     * asset's -Z forward convention is represented by the profile basis; the
     * hand and the weapon must be transformed by that same basis. */
    int sy=(actor_sy*profile->forward_cy+
            actor_cy*profile->forward_sy)/1024;
    int cy=(actor_cy*profile->forward_cy-
            actor_sy*profile->forward_sy)/1024;
    out->x=actor_x+(model_x*cy+model_z*sy)/1024;
    out->y=base_y+model_y;
    out->z=actor_z+(model_z*cy-model_x*sy)/1024;
}

static void rifle_anchor_model_target(
    const struct rasterfall_model_attachment_transform *frame,
    const struct rasterfall_pose_calibration *profile,
    const struct rasterfall_weapon_asset_profile *asset,
    struct rasterfall_cal_vec3 anchor,double target[3],int character_scale)
{
    int delta[3],calibrated[3],i;
    for(i=0;i<3;i++) delta[i]=(((int *)&anchor)[i]-((int *)&asset->attachment_grip)[i])*1000/character_scale;
    weapon_profile_local(profile,delta,calibrated);
    for(i=0;i<3;i++) target[i]=frame->position[i]+
        (frame->rotation[i*3+0]*calibrated[0]+frame->rotation[i*3+1]*calibrated[1]+frame->rotation[i*3+2]*calibrated[2])/1000.0;
}

static void rifle_solve_hands(struct rasterfall_model_asset *model,
                              const struct rasterfall_model_attachment_transform *frame,
                              const struct rasterfall_pose_calibration *profile,
                              const struct rasterfall_weapon_asset_profile *asset,
                              int character_scale)
{
    double target[3];
    if (!model || !frame || !profile || !asset || !profile->left_ik) return;
    rifle_anchor_model_target(frame,profile,asset,profile->foregrip,target,character_scale);
    rasterfall_model_solve_two_bone_attachment(model,"左腕","左ひじ","左手首",target,(double[3]){-1.0,0.0,0.0});
}

static int render_skeletal_rifle(
    struct toy_renderer *renderer,const struct camera *camera,
    const struct rasterfall_model_asset *character,
    const struct rasterfall_model_attachment_transform *rifle_frame,
    const struct rasterfall_skeletal_actor_profile *profile,
    const struct rasterfall_pose_calibration *calibration,
    int actor_x,int base_y,int actor_z,int actor_sy,int actor_cy,
    int weapon,int muzzle_flash)
{
    const struct rasterfall_weapon_asset_profile *asset=rasterfall_weapon_asset_profile(weapon);
    const struct rasterfall_pose_calibration *weapon_profile=calibration;
    struct rasterfall_model_asset *model;
    struct vec3 grip_world,foregrip_world,muzzle_world;
    int grip_offset[3],foregrip_offset[3],muzzle_offset[3];
    const char *path;
    int length,scale,character_scale,i,pixels=0;
    if(!asset->skeletal || (path=asset->model_path)==NULL)return 0;
    model=gallery_model_named(path,NULL);if(!model)return 0;
    length=model->max_x-model->min_x;
    if(model->max_y-model->min_y>length)length=model->max_y-model->min_y;
    if(model->max_z-model->min_z>length)length=model->max_z-model->min_z;
    if(length<=0)return 0;
    scale=asset->base_scale_milli*weapon_profile->scale_milli/1000/length;
    character_scale=character_model_scale(character,profile->target_height_mm);
    for(i=0;i<3;i++){
        grip_offset[i]=((int *)&weapon_profile->grip)[i]-((int *)&asset->attachment_grip)[i];
        foregrip_offset[i]=((int *)&weapon_profile->foregrip)[i]-((int *)&asset->attachment_grip)[i];
        muzzle_offset[i]=((int *)&weapon_profile->muzzle)[i]-((int *)&asset->attachment_grip)[i];
    }
    skeletal_attachment_point(character,rifle_frame,profile,actor_x,base_y,actor_z,
        actor_sy,actor_cy,character_scale,weapon_profile,grip_offset,&grip_world);
    skeletal_attachment_point(character,rifle_frame,profile,actor_x,base_y,actor_z,
        actor_sy,actor_cy,character_scale,weapon_profile,foregrip_offset,&foregrip_world);
    skeletal_attachment_point(character,rifle_frame,profile,actor_x,base_y,actor_z,
        actor_sy,actor_cy,character_scale,weapon_profile,muzzle_offset,&muzzle_world);
    (void)grip_world;(void)foregrip_world;
    if (active_pose_preview && active_session && active_session->pose_editor.active) {
        if (active_session->pose_editor.anchors) {
            pixels += draw_cuboid(renderer,camera,grip_world.x-24,grip_world.x+24,grip_world.y-24,grip_world.y+24,grip_world.z-24,grip_world.z+24,0xFFCC40);
            pixels += draw_cuboid(renderer,camera,foregrip_world.x-24,foregrip_world.x+24,foregrip_world.y-24,foregrip_world.y+24,foregrip_world.z-24,foregrip_world.z+24,0x40E080);
            pixels += draw_cuboid(renderer,camera,muzzle_world.x-24,muzzle_world.x+24,muzzle_world.y-24,muzzle_world.y+24,muzzle_world.z-24,muzzle_world.z+24,0xFF6040);
        }
        if (active_session->pose_editor.axes) {
            int axis_local[3][3]={{700,0,0},{0,700,0},{0,0,900}}; struct vec3 ends[3]; int a;
            for(a=0;a<3;a++) {
                skeletal_attachment_point(character,rifle_frame,profile,actor_x,base_y,actor_z,actor_sy,actor_cy,character_scale,weapon_profile,axis_local[a],&ends[a]);
                pixels += draw_cuboid(renderer,camera,(grip_world.x+ends[a].x)/2-10,(grip_world.x+ends[a].x)/2+10,(grip_world.y+ends[a].y)/2-10,(grip_world.y+ends[a].y)/2+10,(grip_world.z+ends[a].z)/2-10,(grip_world.z+ends[a].z)/2+10,a==0?0xE05050:a==1?0x50E070:0x5080F0);
            }
        }
    }
    for(i=0;i<(int)model->primitive_count;i++){
        const unsigned char *primitive=model->primitives+
            i*RASTERFALL_MODEL_PRIMITIVE_BYTES;
        const unsigned char *indices=model->indices+model_u32(primitive)*4;
        unsigned int count=model_u32(primitive+4),material=model_u32(primitive+8),j;
        uint32_t color=material<model->material_count?
            model_u32(model->materials+material*model->material_bytes):
            RF_COLOR_UI_TEXT_MUTED;
        for(j=0;j+2<count;j+=3){
            struct vec3 v[3];int k;
            for(k=0;k<3;k++){
                unsigned int index=model_u32(indices+(j+k)*4);
                const unsigned char *p;int local[3];
                if(index>=model->vertex_count)break;
                p=model->vertices+index*model->vertex_bytes;
                {
                    int raw[3], canonical[3];
                    raw[0]=(*(const int*)p-(model->min_x+model->max_x)/2);
                    raw[1]=(*(const int*)(p+4)-(model->min_y+model->max_y)/2);
                    raw[2]=(*(const int*)(p+8)-(model->min_z+model->max_z)/2);
                    if (asset->asset_basis == 1) { canonical[0]=raw[2]; canonical[1]=raw[1]; canonical[2]=raw[0]; }
                    else if (asset->asset_basis == 2) { canonical[0]=-raw[0]; canonical[1]=raw[1]; canonical[2]=-raw[2]; }
                    else { canonical[0]=raw[0]; canonical[1]=raw[1]; canonical[2]=raw[2]; }
                    local[0]=canonical[0]*scale/1000-asset->attachment_grip.x;
                    local[1]=canonical[1]*scale/1000-asset->attachment_grip.y;
                    local[2]=canonical[2]*scale/1000-asset->attachment_grip.z;
                }
                skeletal_attachment_point(character,rifle_frame,profile,actor_x,base_y,
                    actor_z,actor_sy,actor_cy,character_scale,weapon_profile,local,&v[k]);
            }
            if(k==3)pixels+=draw_world_triangle(renderer,camera,&v[0],&v[1],&v[2],color);
        }
    }
    if(muzzle_flash>0)
        pixels+=draw_cuboid(renderer,camera,muzzle_world.x-32,muzzle_world.x+32,
            muzzle_world.y-32,muzzle_world.y+32,muzzle_world.z-32,
            muzzle_world.z+32,RF_COLOR_UI_ACCENT);
    return pixels;
}

static int network_actor_lift(int x, int z, int airborne_y)
{
    struct toy_game_ground_query ground =
        toy_game_query_ground(&game, x, z, 0, 0);
    return ground.support_y + airborne_y;
}

static void render_ai_teammate_name(struct toy_renderer *renderer,
                                    const struct camera *camera)
{
    int i;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
        const struct toy_game_actor *actor = &game.actors[i];
        char label[64];
        const char *display_name;
        long long dx, dz, d2, dist, dot;
        uint32_t color = actor->class_id == TOY_GAME_AI_LEVEL_3 ? RF_COLOR_UI_ACCENT :
                         actor->class_id == TOY_GAME_AI_LEVEL_2 ? RF_COLOR_UI_AI :
                         RF_COLOR_UI_PLAYER;
        if (!actor->active || actor->kind != TOY_GAME_ACTOR_AI) continue;
        dx = (long)actor->x - camera->x;
        dz = (long)actor->z - camera->z;
        d2 = dx * dx + dz * dz;
        if (d2 > 10800LL * 10800LL) continue;
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
                            actor->ground_y + actor->airborne_y +
                            (actor->state == TOY_GAME_ACTOR_DOWNED ? -350 : 700),
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
        if (view.z < NEAR_Z ||
            (view.z > ENEMY_RENDER_DISTANCE && !actor->anime_character_id))
            continue;
        color = actor->class_id == TOY_GAME_AI_LEVEL_3 ? RF_COLOR_AI_HEAVY :
                actor->class_id == TOY_GAME_AI_LEVEL_2 ? RF_COLOR_AI_RIFLE :
                RF_COLOR_AI_BASIC;
        active_actor_lift = actor->ground_y + actor->airborne_y;
        if (actor->anime_character_id && private_character_model.data) {
            struct rasterfall_frontend_state *actor_frontend =
                &ai_character_frontends[i];
            int character_update;
            enum rasterfall_character_distance_quality character_quality;
            if (!ai_character_frontends_initialized[i]) {
                __memset(actor_frontend, 0, sizeof(*actor_frontend));
                actor_frontend->toon_shared = -1;
                actor_frontend->toon_level = 255;
                actor_frontend->material_alpha = 255;
                actor_frontend->material_double_sided = 1;
                ai_character_frontends_initialized[i] = 1;
            }
            character_quality = character_distance_policy(
                camera, actor->x, actor->z, i, actor_frontend);
            if (character_quality == RASTERFALL_CHARACTER_HIDDEN) {
                frontend_set_override(renderer, 0);
                continue;
            }
            /* Skeletal weapons read the character attachment every frame.
             * Do not reuse a stale skinned body while rifle_solve_hands()
             * moves the same pose; otherwise the AK can visibly oscillate
             * against the hands at mid/far LOD update intervals. */
            if (actor->current_slot >= 0 &&
                actor->current_slot < TOY_GAME_WEAPON_SLOTS &&
                rasterfall_weapon_asset_profile(
                    actor->slots[actor->current_slot].weapon)->skeletal)
                actor_frontend->reuse_skinned_vertices = 0;
            character_update = !actor_frontend->reuse_skinned_vertices;
            frontend_set_override(renderer, actor_frontend);
            struct rasterfall_developer_character *maid_entry =
                actor->anime_character_id >= 2 && actor->anime_character_id <= 5 ?
                &developer_characters[0] : NULL;
            struct rasterfall_model_asset *actor_model =
                maid_entry && maid_entry->vmd_loaded ?
                &maid_entry->model : &private_character_model;
            if (maid_entry && maid_entry->lod2_loaded &&
                character_quality >= RASTERFALL_CHARACTER_FAR)
                actor_model = &maid_entry->lod2_model;
            else if (maid_entry && maid_entry->lod_loaded &&
                     character_quality >= RASTERFALL_CHARACTER_MID)
                actor_model = &maid_entry->lod_model;
            else if (!maid_entry && private_character_lod2_loaded &&
                character_quality >= RASTERFALL_CHARACTER_FAR)
                actor_model = &private_character_lod2_model;
            else if (!maid_entry && private_character_lod_loaded &&
                character_quality >= RASTERFALL_CHARACTER_MID)
                actor_model = &private_character_lod_model;
            else if (maid_entry && maid_entry->lod_loaded &&
                     near_original_model_owner != 100 + i &&
                     character_quality == RASTERFALL_CHARACTER_NEAR)
                actor_model = &maid_entry->lod_model;
            else if (!maid_entry && private_character_lod_loaded &&
                     near_original_model_owner != 100 + i &&
                     character_quality == RASTERFALL_CHARACTER_NEAR)
                actor_model = &private_character_lod_model;
            const struct rasterfall_animation_clip *locomotion_clip =
                maid_entry && maid_entry->vmd_loaded ?
                &maid_entry->walk :
                (private_character_vmd_loaded ? &private_character_vmd_clip : NULL);
            struct rasterfall_rifle_pose hit={0};
            struct rasterfall_rifle_pose actor_body_pose;
            struct rasterfall_animation_composition composition;
            struct rasterfall_model_attachment_transform rifle_frame;
            int pose_weapon = actor->current_slot >= 0 &&
                actor->current_slot < TOY_GAME_WEAPON_SLOTS ?
                actor->slots[actor->current_slot].weapon : TOY_GAME_WEAPON_PISTOL;
            int maid_character = actor->anime_character_id >= 2 &&
                                 actor->anime_character_id <= 5;
            const struct rasterfall_pose_calibration *actor_pose =
                rasterfall_pose_calibration_resolve(
                    active_session ? &active_session->pose_editor : NULL,
                    maid_character ? 1 : 0, pose_weapon);
            int have_rifle_frame;
            int blend=actor->locomotion_blend_ms*5;
            if(blend>1000)blend=1000;
            hit.rotation[0][0]=8; hit.rotation[0][2]=-8;
            composition.locomotion=(actor->moving||blend<1000)?locomotion_clip:NULL;
            composition.locomotion_time_ms=actor->animation.time_ms;
            composition.locomotion_weight_milli=actor->moving?blend:1000-blend;
            /* Eula's rifle pose is calibrated in a different local bone
             * basis.  Leave maid in the authored VMD pose until it has its
             * own hand/forearm calibration; otherwise the wrists twist
             * behind the body even though the model yaw is correct. */
            composition.rifle_stance=maid_entry && pose_weapon != TOY_GAME_WEAPON_AK ? 0 : 1;
            /* Maid's Mixamo bind pose differs from Eula's PMX rest pose.
             * Preserve the authored walk above the hips and add only a weak
             * relative rifle silhouette; clearing the upper body here turns
             * Maid's moving pose back toward its bind T-pose. */
            /* Like Eula's editor path, the authored rifle pose owns the
             * complete upper body.  Maid's walk VMD must not reopen or spread
             * the hands after the calibrated arm rotations are applied. */
            composition.upper_body_lock=1;
            composition.overlay=actor->animation.id==TOY_GAME_ANIM_FIRE?RASTERFALL_COMPOSITION_OVERLAY_FIRE:RASTERFALL_COMPOSITION_OVERLAY_NONE;
            composition.overlay_time_ms=actor->animation.time_ms;
            memcpy(actor_body_pose.rotation, actor_pose->body_pose,
                   sizeof(actor_body_pose.rotation));
            if (maid_entry && pose_weapon != TOY_GAME_WEAPON_AK) {
                int pose_bone, axis;
                for (pose_bone = 0;
                     pose_bone < RASTERFALL_RIFLE_POSE_BONE_COUNT;
                     pose_bone++)
                    for (axis = 0; axis < 3; axis++)
                        actor_body_pose.rotation[pose_bone][axis] =
                            actor_body_pose.rotation[pose_bone][axis] * 2 / 3;
            }
            composition.rifle_pose=&actor_body_pose;composition.hit_pose=&hit;
            composition.hit_pose_preview=actor->animation.id==TOY_GAME_ANIM_HIT;
            if (character_update)
                rasterfall_animation_compose(actor_model,&composition);
            have_rifle_frame=rifle_frame_transform(actor_model,&rifle_frame)==0;
            if(have_rifle_frame&&actor->current_slot>=0&&
               actor->current_slot<TOY_GAME_WEAPON_SLOTS){
                int weapon=actor->slots[actor->current_slot].weapon;
                const struct rasterfall_weapon_asset_profile *asset=
                    rasterfall_weapon_asset_profile(weapon);
                const struct rasterfall_pose_calibration *weapon_profile=
                    render_pose_calibration(weapon);
                if(!maid_entry && asset->skeletal&&
                   (actor->animation.id==TOY_GAME_ANIM_IDLE||
                   actor->animation.id==TOY_GAME_ANIM_MOVE||
                   actor->animation.id==TOY_GAME_ANIM_FIRE)){
                    int character_scale=character_model_scale(
                        actor_model,
                        eula_actor_profile.target_height_mm);
                    rifle_solve_hands(actor_model,&rifle_frame,weapon_profile,
                                      asset,character_scale);
                }
            }
            active_gallery_facing=1;
            /* Maid's FBX is authored facing +Z.  Eula's PMX faces -Z and
             * needs the historical 180-degree correction; sharing that
             * correction made maid hold the weapon behind her. */
            if (maid_entry) {
                active_gallery_sy=actor->sy;
                active_gallery_cy=actor->cy;
            } else {
                active_gallery_sy=(actor->sy*eula_actor_profile.forward_cy+
                    actor->cy*eula_actor_profile.forward_sy)/1024;
                active_gallery_cy=(actor->cy*eula_actor_profile.forward_cy-
                    actor->sy*eula_actor_profile.forward_sy)/1024;
            }
            pixels+=render_gallery_model(renderer,camera,actor_model,
                actor->x,-900+active_actor_lift,actor->z,
                character_model_scale(actor_model,
                                      eula_actor_profile.target_height_mm));
            active_gallery_facing=0;
            if(actor->current_slot>=0&&actor->current_slot<TOY_GAME_WEAPON_SLOTS){
                int weapon=actor->slots[actor->current_slot].weapon;
                const struct rasterfall_pose_calibration *weapon_profile=
                    rasterfall_pose_calibration_resolve(
                        active_session ? &active_session->pose_editor : NULL,
                        maid_entry ? 1 : 0, weapon);
                const struct rasterfall_skeletal_actor_profile *actor_profile =
                    maid_entry ? &maid_actor_profile : &eula_actor_profile;
                if(have_rifle_frame && rasterfall_weapon_asset_profile(weapon)->skeletal)
                    pixels+=render_skeletal_rifle(renderer,camera,
                        actor_model,&rifle_frame,actor_profile,
                        weapon_profile,
                        actor->x,-900+active_actor_lift,actor->z,actor->sy,
                        actor->cy,weapon,0);
                else pixels+=render_actor_model_weapon(renderer,camera,actor->x,
                    actor->z,actor->sy,actor->cy,weapon,0,
                    actor->animation.id,actor->animation.time_ms,0,0);
            }
            frontend_set_override(renderer, 0);
            active_actor_lift=0;continue;
        }
        pixels += render_player_avatar(renderer, camera, actor->x, actor->z,
                                       actor->sy, actor->cy,
                                       actor->current_slot >= 0 &&
                                       actor->current_slot < TOY_GAME_WEAPON_SLOTS ?
                                       actor->slots[actor->current_slot].weapon : -1,
                                       0,
                                       actor->character_id, color,
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
                                int character_id, uint32_t body_color, int downed,
                                int animation_id, int animation_time_ms)
{
    const struct rasterfall_character_profile *character =
        rasterfall_character_profile(character_id);
    struct rasterfall_actor_pose pose;
    int pixels = 0, face_y0, face_y1, animation_lift;
    int pose_x, pose_z;
    int death_progress = 0;
    int show_fall_gear = 0;
    if (!renderer || !camera) return 0;
    if (character_id >= 0) body_color = character->body_color;
    rasterfall_actor_animation_sample(
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
                                     0, character->leg_color);
        pixels += draw_actor_leg_box(renderer, camera, pose_x, pose_z,
                                     sy, cy, 10, 95, -900, -610,
                                     0, character->leg_color);
        pixels += draw_actor_box(renderer, camera, pose_x, pose_z, sy, cy,
                                 -155, 155, -620, -100, -100, 100,
                                 body_color);
        pixels += draw_actor_ellipsoid_head(renderer, camera, pose_x, pose_z,
                                            sy, cy, 50, 145, 150,
                                            character->skin_color);
        face_y0 = -35; face_y1 = 185;
    } else if (downed) {
        pixels += draw_cuboid(renderer, camera, pose_x - 170, pose_x + 170,
                              -850 + active_actor_lift, -650 + active_actor_lift,
                              pose_z - 100, pose_z + 100, body_color);
        pixels += draw_ellipsoid_head(renderer, camera, pose_x, pose_z,
                                      -550 + active_actor_lift, 145, 100,
                                      character->skin_color);
        face_y0 = -650; face_y1 = -470;
    } else {
        int left_leg_shift = pose.leg_swing;
        int right_leg_shift = -left_leg_shift;
        /* Rotate two solid cuboids around their hips; the roots remain
         * attached to the torso while the whole volume swings. */
        pixels += draw_actor_leg_box(renderer, camera, pose_x, pose_z,
                                     sy, cy, -95, -10, -900, -610,
                                     left_leg_shift, character->leg_color);
        pixels += draw_actor_leg_box(renderer, camera, pose_x, pose_z,
                                     sy, cy, 10, 95, -900, -610,
                                     right_leg_shift, character->leg_color);
        pixels += draw_actor_tilted_box(renderer, camera, pose_x, pose_z,
                                        sy, cy, -155, 155, -620, -100,
                                        -100, 100, pose.body_pitch,
                                        body_color);
        pixels += draw_actor_ellipsoid_head(renderer, camera, pose_x, pose_z,
                                            sy, cy, 50 + pose.body_pitch * 2 / 3,
                                            145, 150, character->skin_color);
        face_y0 = -35; face_y1 = 185;
    }
    if (!downed || animation_id == TOY_GAME_ANIM_DEATH ||
        animation_id == TOY_GAME_ANIM_REVIVE) {
        pixels += render_actor_weapon(renderer, camera, pose_x, pose_z, sy, cy,
                                      weapon, muzzle_flash, animation_id,
                                      animation_time_ms, body_color);
    }
    pixels += draw_actor_face_rect(renderer, camera, pose_x, pose_z, sy, cy, 145,
                             -72, 72, face_y0, face_y1, character->hair_color);
    pixels += draw_actor_face_rect(renderer, camera, pose_x, pose_z, sy, cy, 145,
                             -16, 16, face_y0 + 40,
                             face_y1 - 40, character->skin_color);
    pixels += draw_actor_face_rect(renderer, camera, pose_x, pose_z, sy, cy, 145,
                             -72, 72, face_y0 + 90,
                             face_y0 + 115, character->skin_color);
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
                             render_camera->cy, player->weapon, 0,
                i % RASTERFALL_CHARACTER_COUNT, colors[i], player->downed,
                player->animation.id,
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
            0,
            client->client_id % RASTERFALL_CHARACTER_COUNT,
            colors[client->client_id], client->down,
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
static void render_effect_ray(struct toy_renderer *renderer,
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

static int render_effect_rays(struct toy_renderer *renderer, const struct camera *camera)
{
    int i, pixels = 0;
    for (i = 0; i < RASTERFALL_EFFECT_INSTANCE_SLOTS; i++) {
        const struct rasterfall_effect_instance *t = &effects.instances[i];
        uint32_t color;
        int remaining;
        if (!t->active || t->type != RASTERFALL_EFFECT_INSTANCE_RAY ||
            (t->kind != RASTERFALL_EFFECT_INSTANCE_KIND_TRACER &&
             t->kind != RASTERFALL_EFFECT_INSTANCE_KIND_ENTITY_HIT_RAY &&
             t->kind != RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION_RAY)) continue;
        remaining = t->lifetime_ms - t->age_ms;
        if (remaining < 0) remaining = 0;
        color = mix_color(t->kind == RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION_RAY ?
                              0xFFF0A0 : 0xFFE060,
                          t->kind == RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION_RAY ?
                              0x8A2408 : 0x3A2C14,
                          remaining * 256 /
                              (t->lifetime_ms > 0 ? t->lifetime_ms : 1),
                          256);
        render_effect_ray(renderer, camera, t->x, t->y, t->z,
                          t->ex, t->ey, t->ez, color,
                          (t->flags & RASTERFALL_EFFECT_EVENT_DEPTH_TEST) != 0);
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

/* A billboard primitive currently uses the same small camera-facing screen
 * rectangle as the original procedural fire point.  Keeping this boundary
 * separate lets textured or larger billboard quads replace it later. */
static int render_effect_billboard(struct toy_renderer *renderer,
                                   const struct camera *camera,
                                   int x, int y, int z, int size,
                                   uint32_t color)
{
    return render_fire_point(renderer, camera, x, y, z, size, color);
}

static int render_effect_billboards(struct toy_renderer *renderer,
                                    const struct camera *camera)
{
    int i, pixels = 0;
    for (i = 0; i < RASTERFALL_EFFECT_INSTANCE_SLOTS; i++) {
        const struct rasterfall_effect_instance *f = &effects.instances[i];
        int size, intensity, forward, remaining;
        if (!f->active || f->type != RASTERFALL_EFFECT_INSTANCE_BILLBOARD)
            continue;
        intensity = f->alpha;
        if (intensity > 256) intensity = 256;
        if (f->kind == RASTERFALL_EFFECT_INSTANCE_KIND_MUZZLE_FLASH) {
            size = f->weapon == TOY_GAME_WEAPON_SHOTGUN ? 9 : 7;
            remaining = f->lifetime_ms - f->age_ms;
            if (remaining < 0) remaining = 0;
            forward = (f->lifetime_ms - remaining) / 3;
            pixels += render_effect_billboard(renderer, camera, f->x, f->y,
                                              f->z, size,
                                              mix_color(0xFFF4A0, 0xB63A08,
                                                        intensity, 256));
            /* Directional lobe preserves the original muzzle burst shape. */
            pixels += render_effect_billboard(renderer, camera,
                                              f->x + f->dir_x * forward / 1024,
                                              f->y + 18,
                                              f->z + f->dir_z * forward / 1024,
                                              size / 2 + 1,
                                              mix_color(0xFFD050, 0x7A1D08,
                                                        intensity, 256));
        } else if (f->kind == RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION_FLASH) {
            size = 11 + (f->age_ms < 24 ? f->age_ms / 4 : 0);
            pixels += render_effect_billboard(renderer, camera, f->x, f->y,
                                              f->z, size,
                                              mix_color(0xFFF4A0, 0xB63A08,
                                                        intensity, 256));
        } else if (f->kind == RASTERFALL_EFFECT_INSTANCE_KIND_PROJECTILE_FLASH) {
            pixels += render_effect_billboard(renderer, camera, f->x, f->y,
                                              f->z, 7,
                                              mix_color(0xFF6060, 0x641010,
                                                        intensity, 256));
        }
    }
    return pixels;
}

static int render_effect_overlay(struct toy_renderer *renderer,
                                 const struct rasterfall_effect_instance *overlay)
{
    int x, y, width, height, alpha;
    if (!overlay->active || overlay->type != RASTERFALL_EFFECT_INSTANCE_OVERLAY)
        return 0;
    x = overlay->x;
    y = overlay->y;
    width = overlay->width > 0 ? overlay->width : renderer->surface.width;
    height = overlay->height > 0 ? overlay->height : renderer->surface.height;
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > renderer->surface.width)
        width = renderer->surface.width - x;
    if (y + height > renderer->surface.height)
        height = renderer->surface.height - y;
    if (width <= 0 || height <= 0) return 0;
    alpha = overlay->alpha;
    if (alpha < 0) alpha = 0;
    if (alpha > 256) alpha = 256;
    fill_rect(&renderer->surface, x, y, width, height,
              mix_color(0x000000, overlay->color, alpha, 256));
    return width * height;
}

static int render_effect_particle(struct toy_renderer *renderer,
                                  const struct camera *camera,
                                  const struct rasterfall_effect_instance *p)
{
    struct vec3 world, view;
    struct toy_screen_vertex screen;
    int width, height, k;
    if (!p->active) return 0;
    world.x = p->x; world.y = p->y; world.z = p->z;
    world_to_view(camera, &world, &view);
    if (view.z < NEAR_Z) return 0;
    project_vertex(&renderer->surface, &view, &screen);
    width = 2 * p->size / 1000;
    if (width < 1) width = 1;
    height = width * p->stretch_y / 1000;
    if (height < 1) height = 1;
    if (screen.x < 0 || screen.x + width >= renderer->surface.width ||
        screen.y < 0 || screen.y + height >= renderer->surface.height) return 0;
    k = p->alpha;
    if (k < 0) k = 0;
    if (k > 256) k = 256;
    fill_rect(&renderer->surface, screen.x, screen.y, width, height,
              p->color ? p->color : mix_color(0xFFC860, 0x4A2008, k, 256));
    return 1;
}

static int render_effect_particles(struct toy_renderer *renderer, const struct camera *camera)
{
    int i, pixels = 0;
    for (i = 0; i < RASTERFALL_EFFECT_INSTANCE_SLOTS; i++) {
        const struct rasterfall_effect_instance *p = &effects.instances[i];
        if (p->type != RASTERFALL_EFFECT_INSTANCE_PARTICLE ||
            (p->kind != RASTERFALL_EFFECT_INSTANCE_KIND_HIT_PARTICLE &&
             p->kind != RASTERFALL_EFFECT_INSTANCE_KIND_FIRE &&
             p->kind != RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION_PARTICLE))
            continue;
        pixels += render_effect_particle(renderer, camera, p);
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
    rasterfall_render_frontend_set_default_texture(active_wall_texture);
    active_model_texture = ctx->model_texture;
    active_lightmap = ctx->lightmap;
    active_textures = ctx->textures_enabled;
    active_fixed_floor_lighting = ctx->fixed_floor_lighting;
}

void rasterfall_render_set_edge_pass(int enabled)
{
    edge_pass_enabled = enabled != 0;
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

void rasterfall_render_scene_stats(struct rasterfall_scene_stats *out)
{
    if (out) memcpy(out, &scene_stats, sizeof(*out));
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
    int pixels;
    if (!renderer || !viewer || !body_camera || game.player_down) return 0;
    active_actor_lift = game.player_ground_y + game.player_airborne_y;
    pixels = render_player_avatar(renderer, viewer, body_camera->x,
                                  body_camera->z, body_camera->sy,
                                  body_camera->cy, -1, 0, -1,
                                  RF_COLOR_UI_PLAYER, 0, game.animation.id,
                                  game.animation.time_ms);
    active_actor_lift = 0;
    return pixels;
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

int rasterfall_render_effects(struct toy_renderer *renderer,
                              const struct camera *camera)
{
    /* Preserve the established world-effect draw order while callers move to
     * one runtime entry point.  Overlay instances stay in the frame-tail
     * screen-space pass. */
    return render_effect_rays(renderer, camera) +
           render_effect_billboards(renderer, camera) +
           render_effect_particles(renderer, camera);
}

int rasterfall_render_overlays(struct toy_renderer *renderer)
{
    int i, pixels = 0;
    for (i = 0; i < RASTERFALL_EFFECT_INSTANCE_SLOTS; i++)
        pixels += render_effect_overlay(renderer, &active_effects->instances[i]);
    return pixels;
}
