#ifndef RASTERFALL_OPTIONS_H
#define RASTERFALL_OPTIONS_H

struct rasterfall_options {
    int input_debug, logic_test, action_runtime_debug;
    int requested_net_mode, net_port, net_loss_percent;
    const char *net_address;
    int auto_mode, textures_enabled, edge_pass_enabled;
    int stats_enabled, texture_stats, frame_limit;
    const char *dump_path;
    const char *visual_scenario, *visual_output;
    const char *character_acceptance_model, *character_acceptance_dir;
    const char *profession_lineup_models, *profession_lineup_dir;
    const char *squad_acceptance_models, *squad_acceptance_dir;
    const char *rigid_attachment_models, *rigid_attachment_dir;
    const char *character_world_capture_dir, *character_world_capture_model;
    const char *view_model_path, *view_output_dir;
    int model_views_supersample, model_skinning, model_pose;
    int material_regression;
    const char *performance_model_path;
    const char *bone_model_path, *bone_search;
    const char *humanoid_model_path, *humanoid_basis_model_path;
    const char *retarget_model_path, *retarget_action;
    const char *glb_animation_model, *glb_animation_path;
    const char *glb_animation_name;
    const char *glb_motion_model, *glb_motion_path;
    const char *action_info_path;
    const char *action_preview_model, *action_preview_path, *action_preview_output;
    const char *composition_capture_model, *composition_capture_lower;
    const char *composition_capture_upper, *composition_capture_additive;
    const char *composition_capture_output;
    int composition_capture_lower_time, composition_capture_upper_time;
    int composition_capture_additive_time;
    const char *pose_debug_model, *pose_debug_action, *pose_debug_role;
    const char *pose_debug_upper_action;
    const char *pose_debug_additive_action;
    int action_time_ms, pose_debug_upper_time_ms, pose_debug_additive_time_ms;
    const char *vmd_walk_model, *vmd_walk_path;
    int vmd_freeze_head, vmd_freeze_torso;
    int vmd_disable_ik, vmd_disable_grant;
    int vmd_legacy_root_offset, vmd_legacy_knee_ccd, vmd_skin_trace;
    int performance_iterations, performance_workers;
    int actor_performance, actor_raster_workers;
};

void rasterfall_options_init(struct rasterfall_options *options,
                             int textures_enabled);
int rasterfall_options_parse(struct rasterfall_options *options,
                             int argc, char **argv);
void rasterfall_options_usage(int fd);

#endif
