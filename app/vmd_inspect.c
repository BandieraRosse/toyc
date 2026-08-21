#include "core.h"
#include "tlibc_everything.h"
#include "rasterfall_model.h"
#include "rasterfall_vmd.h"

static void reset_ik_stats(struct rasterfall_model_asset *m)
{
    m->ik_sample_count = 0;
    m->ik_controller_sample_count = 0;
    m->ik_iteration_total = 0;
    m->ik_iteration_max = 0;
    m->ik_error_before_total = m->ik_error_after_total = 0.0;
    m->ik_error_before_max = m->ik_error_after_max = 0.0;
    m->ik_reach_sample_count = m->ik_unreachable_count = 0;
    m->ik_reach_distance_total = m->ik_reach_ratio_total = 0.0;
    m->ik_reach_distance_max = m->ik_reach_ratio_max = 0.0;
}

static void print_ik_runtime(const struct rasterfall_model_asset *m)
{
    __printf("ik runtime diagnostic: samples=%lu avg_iterations_per_leg=%.2f "
             "max_iterations_per_leg=%u avg_error_before=%.3f "
             "avg_error_after=%.3f max_error_before=%.3f "
             "max_error_after=%.3f enabled=%s\n",
             m->ik_sample_count,
             m->ik_controller_sample_count ?
                 (double)m->ik_iteration_total / m->ik_controller_sample_count : 0.0,
             m->ik_iteration_max,
             m->ik_controller_sample_count ?
                 m->ik_error_before_total / m->ik_controller_sample_count : 0.0,
             m->ik_controller_sample_count ?
                 m->ik_error_after_total / m->ik_controller_sample_count : 0.0,
             m->ik_error_before_max, m->ik_error_after_max,
             m->ik_enabled ? "yes" : "no");
    __printf("ik reachability: samples=%lu avg_distance=%.3f max_distance=%.3f "
             "avg_distance_over_reach=%.4f max_distance_over_reach=%.4f "
             "unreachable=%.2f%%\n",
             m->ik_reach_sample_count,
             m->ik_reach_sample_count ?
                 m->ik_reach_distance_total / m->ik_reach_sample_count : 0.0,
             m->ik_reach_distance_max,
             m->ik_reach_sample_count ?
                 m->ik_reach_ratio_total / m->ik_reach_sample_count : 0.0,
             m->ik_reach_ratio_max,
             m->ik_reach_sample_count ?
                 100.0 * m->ik_unreachable_count / m->ik_reach_sample_count : 0.0);
}

static void prepare_vmd_skeleton_translation(struct rasterfall_model_asset *m,
                                             const struct rasterfall_vmd_clip *v,
                                             int time_ms, int legacy)
{
    int center[3], groove[3];
    rasterfall_vmd_sample_bone_translation(v, "センター", time_ms, center);
    rasterfall_vmd_sample_bone_translation(v, "グルーブ", time_ms, groove);
    rasterfall_model_set_vmd_skeleton_translation(m, center, groove, !legacy);
    m->animation_offset_x = legacy ? center[0] + groove[0] : 0;
    m->animation_offset_y = legacy ? center[1] + groove[1] : 0;
    m->animation_offset_z = legacy ? center[2] + groove[2] : 0;
}

int main(int argc, char **argv)
{
    struct rasterfall_vmd_clip v;
    struct rasterfall_model_asset m;
    struct rasterfall_animation_clip clip;
    struct rasterfall_animation_track tracks[RASTERFALL_VMD_MAX_BONES];
    int have = 0, result, ordered = 1, normalized = 1, i, j;
    double norm;

    if (argc < 2) {
        __printf("usage: vmd-inspect <motion.vmd> [eula.rmesh] "
                 "[--vmd-disable-ik] [--vmd-legacy-root-offset]\n");
        return 2;
    }
    if (rasterfall_vmd_load(&v, argv[1]) < 0) {
        __fprintf(2, "vmd-inspect: malformed or truncated VMD\n");
        return 1;
    }
    memset(&m, 0, sizeof(m));
    if (argc > 2) {
        if (rasterfall_model_load(&m, argv[2]) < 0) {
            rasterfall_vmd_unload(&v);
            return 1;
        }
        have = 1;
        rasterfall_vmd_map_eula(&v, &m);
    }
    if (have) {
        int phase = v.duration_ms / 4;
        int disable = 0, legacy = 0;
        for (i = 3; i < argc; i++) {
            if (!strcmp(argv[i], "--vmd-disable-ik")) disable = 1;
            if (!strcmp(argv[i], "--vmd-legacy-root-offset")) legacy = 1;
        }
        if (disable) rasterfall_model_set_ik_enabled(&m, 0);
        rasterfall_model_dump_ik(&m);
        rasterfall_model_dump_ik_hierarchy(&m);
        if (rasterfall_vmd_build_animation(&v, &clip, tracks,
                                           RASTERFALL_VMD_MAX_BONES) >= 0) {
            {
                int was_ik_enabled = m.ik_enabled;
                int target_phase, sample_step = v.duration_ms > 30000 ? 100 : 16;
                m.ik_enabled = 0;
                rasterfall_model_reset_center_ab_diagnostic(&m);
                __printf("center semantics A/B: offline only, step_ms=%d\n", sample_step);
                for (target_phase = 0; target_phase < 4; target_phase++) {
                    int phase_ms = v.duration_ms * target_phase / 4;
                    rasterfall_model_sample_clip(&m, &clip, phase_ms);
                    rasterfall_model_center_ab_diagnostic(&m, &v, phase_ms, 1);
                }
                for (i = 0; i < v.duration_ms; i += sample_step) {
                    rasterfall_model_sample_clip(&m, &clip, i);
                    rasterfall_model_center_ab_diagnostic(&m, &v, i, 0);
                }
                rasterfall_model_print_center_ab_diagnostic(&m);
                m.ik_enabled = was_ik_enabled;
            }
            if (!disable) {
                int target_phase;
                m.ik_target_space_diagnostic = 1;
                for (target_phase = 0; target_phase < 4; target_phase++) {
                    __printf("ik target-space diagnostic: phase=%d%%\n", target_phase * 25);
                    rasterfall_model_sample_clip(&m, &clip,
                                                 v.duration_ms * target_phase / 4);
                }
                m.ik_target_space_diagnostic = 0;
                m.ik_diagnostic_dump = 1;
                __printf("ik diagnostic: normal target, phase=25%%\n");
                prepare_vmd_skeleton_translation(&m, &v, phase, legacy);
                rasterfall_model_sample_clip(&m, &clip, phase);
                m.ik_diagnostic_dump = 0;

                m.ik_synthetic_target = 1;
                m.ik_synthetic_offset[0] = 0.0;
                m.ik_synthetic_offset[1] = 0.0;
                m.ik_synthetic_offset[2] = 1000.0;
                m.ik_diagnostic_dump = 1;
                __printf("ik diagnostic: synthetic left target offset=(0,0,1000)\n");
                rasterfall_model_sample_clip(&m, &clip, phase);
                m.ik_diagnostic_dump = 0;
                m.ik_synthetic_target = 0;

                m.ik_limits_enabled = 0;
                m.ik_diagnostic_dump = 1;
                __printf("ik diagnostic: normal target, knee limits=off, phase=25%%\n");
                prepare_vmd_skeleton_translation(&m, &v, phase, legacy);
                rasterfall_model_sample_clip(&m, &clip, phase);
                m.ik_diagnostic_dump = 0;
                m.ik_limits_enabled = 1;
            }
            {
                int sample_step = v.duration_ms > 30000 ? 100 : 16;
                reset_ik_stats(&m);
                __printf("ik runtime sampling: step_ms=%d (solver unchanged)\n",
                         sample_step);
                for (i = 0; i < v.duration_ms; i += sample_step)
                { prepare_vmd_skeleton_translation(&m, &v, i, legacy);
                    rasterfall_model_sample_clip(&m, &clip, i);
                }
            }
            print_ik_runtime(&m);
        }
    }
    rasterfall_vmd_dump(&v, have ? &m : 0);
    rasterfall_vmd_dump_motion_diagnostic(&v, have ? &m : 0);
    rasterfall_vmd_dump_translation_diagnostic(&v);
    for (i = 0; i < v.track_count; i++)
        for (j = 1; j < v.tracks[i].key_count; j++) {
            if (v.tracks[i].keys[j].frame < v.tracks[i].keys[j - 1].frame)
                ordered = 0;
            norm = v.tracks[i].keys[j].rotation.x * v.tracks[i].keys[j].rotation.x +
                   v.tracks[i].keys[j].rotation.y * v.tracks[i].keys[j].rotation.y +
                   v.tracks[i].keys[j].rotation.z * v.tracks[i].keys[j].rotation.z +
                   v.tracks[i].keys[j].rotation.w * v.tracks[i].keys[j].rotation.w;
            if (norm < .999 || norm > 1.001) normalized = 0;
        }
    result = rasterfall_vmd_logic_test() || !ordered || !normalized ||
             v.duration_ms != v.max_frame * 1000 / 30;
    __printf("vmd validation: frame_order=%s quaternion_normalization=%s "
             "duration=%s loop_sampling=existing AnimationClip nlerp\n",
             ordered ? "pass" : "FAIL", normalized ? "pass" : "FAIL",
             v.duration_ms == v.max_frame * 1000 / 30 ? "pass" : "FAIL");
    if (have) rasterfall_model_unload(&m);
    rasterfall_vmd_unload(&v);
    return result;
}
