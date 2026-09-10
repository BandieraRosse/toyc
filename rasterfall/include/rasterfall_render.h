#ifndef RASTERFALL_RENDER_H
#define RASTERFALL_RENDER_H

#include "toy_renderer.h"
#include "toy_assets.h"
#include "rasterfall_session.h"
#include "rasterfall_effects.h"
#include "rasterfall_net.h"
#include "rasterfall_prop.h"
#include "rasterfall_model.h"

#define RASTERFALL_BAKED_LM_W 32
#define RASTERFALL_BAKED_LM_H 24
#define RASTERFALL_NEAR_Z 64

struct rasterfall_render_context {
    struct rasterfall_session *session;
    struct rasterfall_effects *effects;
    const struct rasterfall_net *net;
    struct toy_texture_view *wall_texture;
    const struct toy_texture_view *model_texture;
    unsigned short lightmap[RASTERFALL_BAKED_LM_W *
                            RASTERFALL_BAKED_LM_H];
    int textures_enabled;
    int fixed_floor_lighting;
};

struct rasterfall_character_profile;

/* Presentation snapshot, borrowed for one draw; no actor ownership or culling.
 * x/z and lift use RFU; lift = ground_y + airborne_y (feet at -900 + lift).
 * sy/cy are the existing 1024-scaled facing basis. */
struct rasterfall_procedural_humanoid_state {
    int x, z, lift;
    int sy, cy;
    int weapon, muzzle_flash;
    int downed;
    int animation_id, animation_time_ms;
    int profession_id; /* rasterfall_profession_id; presentation identity, default NONE. */
};

/* Uses the bound render context and existing serial primitive helpers.
 * character supplies base appearance; state and character must be non-NULL.
 * Saves/restores primitive transform state; does not select actors or cameras. */
int rasterfall_render_procedural_humanoid(
    struct toy_renderer *renderer, const struct camera *camera,
    const struct rasterfall_procedural_humanoid_state *state,
    const struct rasterfall_character_profile *character);

struct rasterfall_model_triangle_stats {
    unsigned long total_triangles;
    unsigned long near_rejected_triangles;
    unsigned long near_clipped_triangles;
    unsigned long backface_culled_triangles;
    unsigned long emitted_triangles;
};

struct rasterfall_model_render_stats {
    struct rasterfall_model_triangle_stats body;
    struct rasterfall_model_triangle_stats edge;
    int command_overflow;
};

struct rasterfall_model_setup_timing {
    long total_us;
    long animation_sample_us;
    long bone_hierarchy_us;
    long skinning_us;
    long vertex_cache_us;
    long material_us;
    long body_triangles_us;
    long edge_triangles_us;
};

struct rasterfall_scene_stats {
    long sky_floor_us;
    long map_us;
    long gallery_us;
    long private_model_us;
    long projectiles_us;
    unsigned long models_tested;
    unsigned long models_culled;
    unsigned long model_triangles_culled;
    long character_wall_us[5];
    long character_animation_us[5];
    long character_skin_us[5];
    long character_vertex_us[5];
    long character_triangle_us[5];
    unsigned long character_triangles[5];
    unsigned char character_visible[5];
    unsigned char character_edge_disabled[5];
    long character_prepare_wall_us;
    long character_merge_us;
    long character_preview_us;
    long character_animation_outside_us;
};

/* Row-major rotation and RFU translation. scale_milli is uniform and 1000
 * means authored size. Rigid attachment RMESH vertices are metric at the
 * asset boundary and are converted using their stored position_scale. */
struct rasterfall_rigid_transform {
    double translation[3];
    double rotation[9];
    int scale_milli;
};

struct rasterfall_rigid_attachment_desc {
    enum rasterfall_character_attachment host_socket;
    const struct rasterfall_model_resource *resource;
    struct rasterfall_rigid_transform mount_correction;
    unsigned int flags;
};

void rasterfall_render_bind(struct rasterfall_render_context *ctx);
void rasterfall_render_set_edge_pass(int enabled);
/* Presentation/diagnostic switch. Runtime default is Lighting V1 enabled. */
void rasterfall_render_set_model_lighting(int enabled);
int rasterfall_render_set_vmd_walk(const char *model_path, const char *vmd_path);
void rasterfall_render_set_vmd_freeze(int freeze_head, int freeze_torso);
void rasterfall_render_set_vmd_ik_enabled(int enabled);
void rasterfall_render_set_vmd_grant_enabled(int enabled);
void rasterfall_render_set_vmd_legacy_root_offset(int enabled);
void rasterfall_render_set_vmd_legacy_knee_ccd(int enabled);
void rasterfall_render_set_vmd_skin_trace(int enabled);
void rasterfall_render_bake_lightmap(void);
void rasterfall_render_set_coordinate_axes(int enabled);
void rasterfall_render_set_action_runtime_debug(int enabled);
int rasterfall_render_model_preview(struct toy_renderer *renderer,
                                    const struct camera *camera,
                                    const struct rasterfall_model_asset *model,
                                    int use_sphere, int use_toon, int use_edge,
                                    int use_material_light);
int rasterfall_render_static_prop(
    struct toy_renderer *renderer, const struct camera *camera,
    const struct rasterfall_prop_instance *instance);
int rasterfall_render_rigid_resource(
    struct toy_renderer *renderer, const struct camera *camera,
    const struct rasterfall_model_resource *resource,
    const struct rasterfall_rigid_transform *model_to_world);
int rasterfall_render_rigid_attachment(
    struct toy_renderer *renderer, const struct camera *camera,
    const struct rasterfall_model_instance *host,
    const struct rasterfall_rigid_attachment_desc *attachment,
    const struct rasterfall_rigid_transform *actor_to_world);
int rasterfall_render_character_instance(
    struct toy_renderer *renderer, const struct camera *camera,
    const struct rasterfall_model_instance *instance,
    const struct rasterfall_rigid_transform *actor_to_world,
    uint32_t shirt_color, uint32_t pants_color);
int rasterfall_rigid_attachment_transform_logic_test(void);
void rasterfall_render_model_stats(struct rasterfall_model_render_stats *out);
void rasterfall_render_model_setup_timing(
    struct rasterfall_model_setup_timing *out);
int rasterfall_render_character_acceptance_capture(const char *model_path,
                                                    const char *output_dir);
int rasterfall_render_character_world_capture(const char *output_dir,
                                              const char *model_path);
int rasterfall_render_actor_benchmark(int iterations, int frontend_workers,
                                      int raster_workers);
/* Process-only diagnostic: call before runtime initialization, then exit. */
int rasterfall_render_visual_capture(const char *scenario, const char *output);
int rasterfall_render_profession_lineup(const char *model_dir, const char *output_dir);
int rasterfall_render_squad_acceptance(const char *model_dir,
                                       const char *output_dir);
int rasterfall_render_rigid_attachment_acceptance(const char *model_dir,
                                                  const char *output_dir);
int rasterfall_render_action_preview(const char *model_path,
                                     const char *action_path, int time_ms,
                                     const char *output_path);
int rasterfall_render_action_composition_capture(const char *model_path,
    const char *lower_path, int lower_time_ms, const char *upper_path,
    int upper_time_ms, const char *additive_path, int additive_time_ms,
    const char *output_path);
void rasterfall_render_scene_stats(struct rasterfall_scene_stats *out);
int rasterfall_render_near_clip_test(void);
int rasterfall_render_static_prop_culling_logic_test(void);

int rasterfall_render_scene(struct toy_renderer *renderer,
                            const struct camera *camera);
void rasterfall_render_coordinate_labels(struct toy_surface *surface,
                                         const struct camera *camera);
int rasterfall_render_sign_text(struct toy_renderer *renderer,
                                 const struct camera *camera);
void rasterfall_render_gallery_selection(struct toy_surface *surface,
                                         const struct camera *camera);
int rasterfall_render_interactables(struct toy_renderer *renderer,
                                    const struct camera *camera);
int rasterfall_render_flags(struct toy_renderer *renderer,
                            const struct camera *camera);
int rasterfall_render_flag_text(struct toy_renderer *renderer,
                                 const struct camera *camera);
int rasterfall_render_enemies(struct toy_renderer *renderer,
                              const struct camera *camera);
int rasterfall_render_ai_teammate(struct toy_renderer *renderer,
                                  const struct camera *camera);
int rasterfall_render_managed_player(struct toy_renderer *renderer,
                                     const struct camera *viewer,
                                     const struct camera *body_camera);
int rasterfall_render_network_teammate(struct toy_renderer *renderer,
                                       const struct camera *camera,
                                       const struct rasterfall_net *net,
                                       const struct toy_game *game);
void rasterfall_render_ai_teammate_name(struct toy_renderer *renderer,
                                        const struct camera *camera);
void rasterfall_render_network_teammate_status(
    struct toy_renderer *renderer, const struct camera *camera,
    const struct rasterfall_net *net, const struct toy_game *game);
/* Unified runtime facade for world-space ray, billboard and particle
 * instances.  Screen-space overlay instances are submitted separately at
 * the frame tail. */
int rasterfall_render_effects(struct toy_renderer *renderer,
                              const struct camera *camera);
int rasterfall_render_overlays(struct toy_renderer *renderer);

#endif
