#include "core.h"
#include "tlibc_everything.h"
#include "rasterfall_model.h"
#include "rasterfall_vmd.h"

static void reset_ik_stats(struct rasterfall_model_asset *m)
{
    m->ik_sample_count = 0;
    m->ik_controller_sample_count = 0;
    m->ik_analytic_solved_count = 0;
    m->ik_analytic_clamped_count = 0;
    m->ik_analytic_rejected_count = 0;
    m->ik_analytic_accept_count[0] = m->ik_analytic_accept_count[1] = 0;
    m->ik_analytic_reject_count[0] = m->ik_analytic_reject_count[1] = 0;
    memset(m->ik_analytic_reject_reason, 0, sizeof(m->ik_analytic_reject_reason));
    memset(m->ik_analytic_error_hist, 0, sizeof(m->ik_analytic_error_hist));
    m->ik_last_leg_solver[0] = m->ik_last_leg_solver[1] = 0;
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
    __printf("analytic leg IK: solved=%lu clamped_targets=%lu rejected=%lu\n",
             m->ik_analytic_solved_count, m->ik_analytic_clamped_count,
             m->ik_analytic_rejected_count);
    __printf("analytic normalized ankle error bins (<.005,.01,.02,.05,.10,.20,.30,.50,1.0,>=1): left=%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu right=%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
             m->ik_analytic_error_hist[0][0],m->ik_analytic_error_hist[0][1],m->ik_analytic_error_hist[0][2],m->ik_analytic_error_hist[0][3],m->ik_analytic_error_hist[0][4],m->ik_analytic_error_hist[0][5],m->ik_analytic_error_hist[0][6],m->ik_analytic_error_hist[0][7],m->ik_analytic_error_hist[0][8],m->ik_analytic_error_hist[0][9],
             m->ik_analytic_error_hist[1][0],m->ik_analytic_error_hist[1][1],m->ik_analytic_error_hist[1][2],m->ik_analytic_error_hist[1][3],m->ik_analytic_error_hist[1][4],m->ik_analytic_error_hist[1][5],m->ik_analytic_error_hist[1][6],m->ik_analytic_error_hist[1][7],m->ik_analytic_error_hist[1][8],m->ik_analytic_error_hist[1][9]);
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

static int inspect_bone_index(const struct rasterfall_model_asset *m,
                              const char *name)
{
    int i;
    for (i = 0; i < (int)m->bone_count; i++)
        if (!strcmp(m->bones[i].name, name)) return i;
    return -1;
}

static void inspect_leg_pair(const struct rasterfall_model_asset *m,
                             const char *name, const char *side)
{
    int i = inspect_bone_index(m, name);
    if (i < 0) {
        __printf("  %s bone=%s MISSING\n", side, name);
        return;
    }
    __printf("  %s bone=%s index=%d local_euler=(%d,%d,%d) global=(%.3f,%.3f,%.3f)\n",
             side, name, i, m->bones[i].rotate_x, m->bones[i].rotate_y,
             m->bones[i].rotate_z, m->bone_transforms[i].position[0],
             m->bone_transforms[i].position[1], m->bone_transforms[i].position[2]);
}

static void inspect_leg_transforms(struct rasterfall_model_asset *m,
                                   const char *label)
{
    static const char *names[] = {
        "左足", "左足D", "左ひざ", "左ひざD", "左足首", "左足首D",
        "右足", "右足D", "右ひざ", "右ひざD", "右足首", "右足首D"
    };
    int i;
    __printf("leg transform diagnostic: %s\n", label);
    for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++)
        inspect_leg_pair(m, names[i], i < 6 ? "L" : "R");
}

static void inspect_static_leg_rotation(struct rasterfall_model_asset *m)
{
    int knee = inspect_bone_index(m, "左ひざ");
    int knee_d = inspect_bone_index(m, "左ひざD");
    int foot = inspect_bone_index(m, "左足");
    int foot_d = inspect_bone_index(m, "左足D");
    int grant_was_enabled = m->grant_enabled;
    __printf("leg static rotation diagnostic: pose=bind angle_x=35deg\n");
    m->grant_enabled = 0;
    rasterfall_model_sample_clip(m, NULL, 0);
    inspect_leg_transforms(m, "bind");
    if (knee >= 0) {
        m->bones[knee].rotate_x = 35;
        rasterfall_model_update_bones(m);
        inspect_leg_transforms(m, "only left knee forced grant=off");
    }
    if (knee >= 0) {
        m->grant_enabled = 1;
        rasterfall_model_sample_clip(m, NULL, 0);
        m->bones[knee].rotate_x = 35;
        rasterfall_model_update_bones(m);
        m->grant_pose_applied = 0;
        rasterfall_model_apply_rotation_grants(m);
        inspect_leg_transforms(m, "only left knee forced grant=on");
    }
    if (knee_d >= 0) {
        m->grant_enabled = 0;
        rasterfall_model_sample_clip(m, NULL, 0);
        m->bones[knee_d].rotate_x = 35;
        rasterfall_model_update_bones(m);
        inspect_leg_transforms(m, "only left kneeD forced grant=off");
        m->grant_enabled = 1;
        rasterfall_model_sample_clip(m, NULL, 0);
        m->bones[knee_d].rotate_x = 35;
        rasterfall_model_update_bones(m);
        m->grant_pose_applied = 0;
        rasterfall_model_apply_rotation_grants(m);
        inspect_leg_transforms(m, "only left kneeD forced grant=on");
    }
    if (foot >= 0) {
        m->grant_enabled = 0;
        rasterfall_model_sample_clip(m, NULL, 0);
        m->bones[foot].rotate_x = 35;
        rasterfall_model_update_bones(m);
        inspect_leg_transforms(m, "only left foot forced grant=off");
        m->grant_enabled = 1;
        rasterfall_model_sample_clip(m, NULL, 0);
        m->bones[foot].rotate_x = 35;
        rasterfall_model_update_bones(m);
        m->grant_pose_applied = 0;
        rasterfall_model_apply_rotation_grants(m);
        inspect_leg_transforms(m, "only left foot forced grant=on");
    }
    if (foot_d >= 0) {
        m->grant_enabled = 0;
        rasterfall_model_sample_clip(m, NULL, 0);
        m->bones[foot_d].rotate_x = 35;
        rasterfall_model_update_bones(m);
        inspect_leg_transforms(m, "only left footD forced grant=off");
        m->grant_enabled = 1;
        rasterfall_model_sample_clip(m, NULL, 0);
        m->bones[foot_d].rotate_x = 35;
        rasterfall_model_update_bones(m);
        m->grant_pose_applied = 0;
        rasterfall_model_apply_rotation_grants(m);
        inspect_leg_transforms(m, "only left footD forced grant=on");
    }
    m->grant_enabled = grant_was_enabled;
    rasterfall_model_sample_clip(m, NULL, 0);
    rasterfall_model_update_bones(m);
}

static void inspect_analytic_synthetic(struct rasterfall_model_asset *m,
                                       const struct rasterfall_vmd_clip *v,
                                       const struct rasterfall_animation_clip *clip)
{
    static const char *side_name[2] = {"left", "right"};
    static const char *target_name[5] = {
        "forward(+Z)", "backward(-Z)", "up(+Y)",
        "forward+up(+Z,+Y)", "lateral(+X)"
    };
    static const double offsets[5][3] = {
        {0.0, 0.0, 180.0}, {0.0, 0.0, -180.0},
        {0.0, 180.0, 0.0}, {0.0, 140.0, 140.0}, {180.0, 0.0, 0.0}
    };
    int side, target;
    int old_ik = m->ik_enabled;
    int old_synthetic = m->ik_synthetic_target;
    __printf("analytic synthetic closure: offsets are model-global units; FK pose reset per case\n");
    m->ik_enabled = 1;
    m->ik_limits_enabled = 1;
    m->ik_synthetic_target = 1;
    m->ik_analytic_geometry_dump = 1;
    for (side = 0; side < 2; side++) {
        int controller, ankle = -1, ik_index;
        double base_offset[3] = {0.0, 0.0, 0.0};
        m->ik_synthetic_side = side;
        m->ik_synthetic_target = 0;
        prepare_vmd_skeleton_translation(m, v, 0, 0);
        rasterfall_model_sample_clip(m, clip, 0);
        controller = rasterfall_model_find_bone(m, side ? "右足ＩＫ" : "左足ＩＫ");
        if (controller >= 0) {
            for (ik_index = 0; ik_index < (int)m->ik_count; ik_index++)
                if (m->iks[ik_index].controller == controller) {
                    ankle = m->iks[ik_index].target;
                    break;
                }
        }
        if (controller >= 0 && ankle >= 0) {
            base_offset[0] = m->bone_transforms[ankle].position[0] -
                             m->bone_transforms[controller].position[0];
            base_offset[1] = m->bone_transforms[ankle].position[1] -
                             m->bone_transforms[controller].position[1];
            base_offset[2] = m->bone_transforms[ankle].position[2] -
                             m->bone_transforms[controller].position[2];
        }
        m->ik_synthetic_target = 1;
        for (target = 0; target < 5; target++) {
            m->ik_synthetic_offset[0] = base_offset[0] + offsets[target][0];
            m->ik_synthetic_offset[1] = base_offset[1] + offsets[target][1];
            m->ik_synthetic_offset[2] = base_offset[2] + offsets[target][2];
            __printf("synthetic case side=%s target=%s offset=(%.1f,%.1f,%.1f)\n",
                     side_name[side], target_name[target], offsets[target][0],
                     offsets[target][1], offsets[target][2]);
            prepare_vmd_skeleton_translation(m, v, 0, 0);
            rasterfall_model_sample_clip(m, clip, 0);
        }
    }
    m->ik_analytic_geometry_dump = 0;
    m->ik_synthetic_target = old_synthetic;
    m->ik_synthetic_side = -1;
    m->ik_enabled = old_ik;
}

static double inspect_vec_length3(const double a[3])
{
    return sqrt(a[0]*a[0] + a[1]*a[1] + a[2]*a[2]);
}

#define model_vec_length inspect_vec_length3
static void inspect_analytic_pole_sweep(struct rasterfall_model_asset *m,
                                        const struct rasterfall_vmd_clip *v,
                                        const struct rasterfall_animation_clip *clip)
{
    static const char *side_name[2] = {"left", "right"};
    static const char *target_name[5] = {"forward", "backward", "up", "forward+up", "lateral"};
    static const double offsets[5][3] = {
        {0.0,0.0,180.0}, {0.0,0.0,-180.0}, {0.0,180.0,0.0},
        {0.0,140.0,140.0}, {180.0,0.0,0.0}
    };
    int side, target, sample;
    int old_ik=m->ik_enabled, old_synthetic=m->ik_synthetic_target;
    __printf("analytic bend-plane sweep: samples=32 low_error_threshold=30.0 units\n");
    m->ik_enabled=0;m->ik_synthetic_target=0;
    for(side=0;side<2;side++) {
        int controller, ankle=-1, thigh, knee, ik_index;
        double base_offset[3], h[3], a0[3], axis[3], u[3], w[3], bind[3], dot, len;
        m->ik_synthetic_side=side;prepare_vmd_skeleton_translation(m,v,0,0);rasterfall_model_sample_clip(m,clip,0);
        controller=rasterfall_model_find_bone(m,side?"右足ＩＫ":"左足ＩＫ");
        thigh=rasterfall_model_find_bone(m,side?"右足":"左足");
        knee=rasterfall_model_find_bone(m,side?"右ひざ":"左ひざ");
        if(controller<0||thigh<0||knee<0)continue;
        for(ik_index=0;ik_index<(int)m->ik_count;ik_index++)if(m->iks[ik_index].controller==controller){ankle=m->iks[ik_index].target;break;}
        if(ankle<0)continue;
        base_offset[0]=m->bone_transforms[ankle].position[0]-m->bone_transforms[controller].position[0];
        base_offset[1]=m->bone_transforms[ankle].position[1]-m->bone_transforms[controller].position[1];
        base_offset[2]=m->bone_transforms[ankle].position[2]-m->bone_transforms[controller].position[2];
        h[0]=m->bone_transforms[thigh].position[0];h[1]=m->bone_transforms[thigh].position[1];h[2]=m->bone_transforms[thigh].position[2];
        a0[0]=m->bone_transforms[ankle].position[0];a0[1]=m->bone_transforms[ankle].position[1];a0[2]=m->bone_transforms[ankle].position[2];
        for(target=0;target<5;target++) {
            double t[3], best_error=1.0e30, best_phi=0.0, best_knee=0.0, bind_phi=0.0;
            int valid_count=0, low_count=0;
            t[0]=a0[0]+offsets[target][0];t[1]=a0[1]+offsets[target][1];t[2]=a0[2]+offsets[target][2];
            axis[0]=t[0]-h[0];axis[1]=t[1]-h[1];axis[2]=t[2]-h[2];len=model_vec_length(axis);if(len<0.000001)continue;axis[0]/=len;axis[1]/=len;axis[2]/=len;
            bind[0]=m->bone_transforms[knee].position[0]-h[0];bind[1]=m->bone_transforms[knee].position[1]-h[1];bind[2]=m->bone_transforms[knee].position[2]-h[2];
            dot=bind[0]*axis[0]+bind[1]*axis[1]+bind[2]*axis[2];bind[0]-=dot*axis[0];bind[1]-=dot*axis[1];bind[2]-=dot*axis[2];len=model_vec_length(bind);
            if(len<0.000001)continue;
            bind[0]/=len;bind[1]/=len;bind[2]/=len;
            u[0]=bind[0];u[1]=bind[1];u[2]=bind[2];
            w[0]=axis[1]*u[2]-axis[2]*u[1];w[1]=axis[2]*u[0]-axis[0]*u[2];w[2]=axis[0]*u[1]-axis[1]*u[0];len=sqrt(w[0]*w[0]+w[1]*w[1]+w[2]*w[2]);if(len<0.000001)continue;w[0]/=len;w[1]/=len;w[2]/=len;
            bind_phi=atan2(bind[0]*w[0]+bind[1]*w[1]+bind[2]*w[2],bind[0]*u[0]+bind[1]*u[1]+bind[2]*u[2])*180.0/M_PI;
            m->ik_enabled=1;
            for(sample=0;sample<32;sample++) {
                double phi=sample*2.0*M_PI/32.0;
                m->ik_synthetic_target=1;m->ik_analytic_pole_override=1;
                m->ik_synthetic_offset[0]=base_offset[0]+offsets[target][0];m->ik_synthetic_offset[1]=base_offset[1]+offsets[target][1];m->ik_synthetic_offset[2]=base_offset[2]+offsets[target][2];
                m->ik_analytic_pole[0]=u[0]*cos(phi)+w[0]*sin(phi);m->ik_analytic_pole[1]=u[1]*cos(phi)+w[1]*sin(phi);m->ik_analytic_pole[2]=u[2]*cos(phi)+w[2]*sin(phi);
                prepare_vmd_skeleton_translation(m,v,0,0);rasterfall_model_sample_clip(m,clip,0);
                if(m->ik_analytic_probe_ran && m->ik_analytic_probe_knee_valid) {valid_count++;if(m->ik_analytic_probe_ankle_error<30.0){low_count++;if(m->ik_analytic_probe_ankle_error<best_error){best_error=m->ik_analytic_probe_ankle_error;best_phi=sample*360.0/32.0;best_knee=m->ik_analytic_probe_raw_knee_x;}}}
                if(sample==0||sample==8||sample==16||sample==24)__printf("pole sample side=%s target=%s phi=%.1f knee=%d valid=%s ankle_error=%.3f\n",side_name[side],target_name[target],sample*360.0/32.0,m->ik_analytic_probe_raw_knee_x,m->ik_analytic_probe_knee_valid?"yes":"no",m->ik_analytic_probe_ankle_error);
            }
            __printf("pole sweep side=%s target=%s valid=%d/32 low_error=%d/32 best_error=%.3f best_phi=%.1f best_knee=%d bind_phi=%.1f\n",side_name[side],target_name[target],valid_count,low_count,low_count?best_error:-1.0,low_count?best_phi:-1.0,low_count?(int)best_knee:0,bind_phi);
        }
    }
    m->ik_analytic_pole_override=0;m->ik_synthetic_target=old_synthetic;m->ik_synthetic_side=-1;m->ik_enabled=old_ik;
}
#undef model_vec_length

struct inspect_leg_sequence_stats {
    unsigned long frames, analytical, fallback, switches, a_to_c, c_to_a;
    unsigned long switch_samples, a_runs, c_runs, a_run_total, c_run_total;
    unsigned long current_run;
    int previous_state;
    double max_switch_knee, sum_switch_knee, max_switch_ankle, sum_switch_ankle;
    unsigned long switch_delta_count;
    double max_a_knee, sum_a_knee, max_a_thigh, sum_a_thigh;
    double max_a_ankle, sum_a_ankle, sum_a_error, max_a_error;
    unsigned long a_delta_count;
    double max_c_knee, sum_c_knee, max_c_ankle, sum_c_ankle;
    unsigned long c_delta_count;
    double max_all_knee, sum_all_knee, max_all_ankle, sum_all_ankle;
    double sum_all_error, max_all_error;
    unsigned long all_delta_count;
};

static double inspect_position_delta(const double a[3], const double b[3]);
static double inspect_rotation_delta(const int a[3], const int b[3]);
static double inspect_vec_length(const double v[3])
{
    return sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
}

struct inspect_knee_branch_value {
    int valid;
    int sign;
    double ratio;
    double dot;
    double h[3], k[3], t[3];
};

static struct inspect_knee_branch_value inspect_knee_branch_value(
    struct rasterfall_model_asset *m, int side, const double target[3])
{
    struct inspect_knee_branch_value out;
    int thigh = rasterfall_model_find_bone(m, side ? "右足" : "左足");
    int knee = rasterfall_model_find_bone(m, side ? "右ひざ" : "左ひざ");
    int ankle = rasterfall_model_find_bone(m, side ? "右足首" : "左足首");
    double axis[3], hk[3], perp[3], ref[3], refp[3], ref_len, perp_len, denom;
    double dot_axis, dot_ref;
    int i;
    memset(&out, 0, sizeof(out));
    if (thigh < 0 || knee < 0 || ankle < 0) return out;
    for (i=0;i<3;i++) {
        out.h[i] = m->bone_transforms[thigh].position[i];
        out.k[i] = m->bone_transforms[knee].position[i];
        out.t[i] = target[i];
        axis[i] = out.t[i] - out.h[i];
        hk[i] = out.k[i] - out.h[i];
        ref[i] = (m->bones[knee].rest_x - m->bones[thigh].rest_x) * 232.0;
    }
    ref[1] = (m->bones[knee].rest_y - m->bones[thigh].rest_y) * 232.0;
    ref[2] = (m->bones[knee].rest_z - m->bones[thigh].rest_z) * 232.0;
    denom = inspect_vec_length(axis);
    if (denom < 0.000001) return out;
    axis[0]/=denom;axis[1]/=denom;axis[2]/=denom;
    dot_axis = hk[0]*axis[0]+hk[1]*axis[1]+hk[2]*axis[2];
    perp[0]=hk[0]-axis[0]*dot_axis;perp[1]=hk[1]-axis[1]*dot_axis;perp[2]=hk[2]-axis[2]*dot_axis;
    perp_len = inspect_vec_length(perp);
    ref_len = inspect_vec_length(ref);
    dot_ref = ref[0]*axis[0]+ref[1]*axis[1]+ref[2]*axis[2];
    refp[0]=ref[0]-axis[0]*dot_ref;refp[1]=ref[1]-axis[1]*dot_ref;refp[2]=ref[2]-axis[2]*dot_ref;
    ref_len = inspect_vec_length(refp);
    if (perp_len < 0.000001 || ref_len < 0.000001) return out;
    out.ratio = perp_len / (inspect_vec_length(hk) +
        inspect_vec_length((double[3]){
            m->bone_transforms[ankle].position[0]-out.k[0],
            m->bone_transforms[ankle].position[1]-out.k[1],
            m->bone_transforms[ankle].position[2]-out.k[2]}) + 0.000001);
    perp[0]/=perp_len;perp[1]/=perp_len;perp[2]/=perp_len;
    refp[0]/=ref_len;refp[1]/=ref_len;refp[2]/=ref_len;
    out.dot = perp[0]*refp[0]+perp[1]*refp[1]+perp[2]*refp[2];
    if (out.dot > 0.15) out.sign=1;
    else if (out.dot < -0.15) out.sign=-1;
    else out.sign=0;
    out.valid=1;
    return out;
}

static double inspect_branch_angle(const struct inspect_knee_branch_value *a,
                                   const struct inspect_knee_branch_value *b)
{
    double dot, cross_len;
    double axes[2][3], hks[2][3], lengths[2];
    const struct inspect_knee_branch_value *values[2]={a,b};
    int n;
    if (!a->valid || !b->valid || a->ratio < 0.05 || b->ratio < 0.05) return 0.0;
    for(n=0;n<2;n++) {
        double axis_len, projection;
        axes[n][0]=values[n]->t[0]-values[n]->h[0];
        axes[n][1]=values[n]->t[1]-values[n]->h[1];
        axes[n][2]=values[n]->t[2]-values[n]->h[2];
        axis_len=inspect_vec_length(axes[n]);
        hks[n][0]=values[n]->k[0]-values[n]->h[0];
        hks[n][1]=values[n]->k[1]-values[n]->h[1];
        hks[n][2]=values[n]->k[2]-values[n]->h[2];
        if(axis_len<0.000001) return 0.0;
        axes[n][0]/=axis_len; axes[n][1]/=axis_len; axes[n][2]/=axis_len;
        projection=hks[n][0]*axes[n][0]+hks[n][1]*axes[n][1]+hks[n][2]*axes[n][2];
        hks[n][0]-=projection*axes[n][0];
        hks[n][1]-=projection*axes[n][1];
        hks[n][2]-=projection*axes[n][2];
        lengths[n]=inspect_vec_length(hks[n]);
        if(lengths[n]<0.000001) return 0.0;
        hks[n][0]/=lengths[n]; hks[n][1]/=lengths[n]; hks[n][2]/=lengths[n];
    }
    dot=hks[0][0]*hks[1][0]+hks[0][1]*hks[1][1]+hks[0][2]*hks[1][2];
    if (dot>1.0)dot=1.0;
    if (dot<-1.0)dot=-1.0;
    cross_len=sqrt((hks[0][1]*hks[1][2]-hks[0][2]*hks[1][1])*(hks[0][1]*hks[1][2]-hks[0][2]*hks[1][1])+
                   (hks[0][2]*hks[1][0]-hks[0][0]*hks[1][2])*(hks[0][2]*hks[1][0]-hks[0][0]*hks[1][2])+
                   (hks[0][0]*hks[1][1]-hks[0][1]*hks[1][0])*(hks[0][0]*hks[1][1]-hks[0][1]*hks[1][0]));
    return atan2(cross_len,dot)*180.0/M_PI;
}

static int inspect_knee_pmx_valid(struct rasterfall_model_asset *m, int side,
                                  int knee, double x)
{
    int controller = rasterfall_model_find_bone(m, side ? "右足ＩＫ" : "左足ＩＫ");
    unsigned int i, j;
    if (controller < 0) return 0;
    for (i=0; i<m->ik_count; i++) {
        if (m->iks[i].controller != controller) continue;
        for (j=0; j<m->iks[i].link_count; j++) {
            struct rasterfall_model_ik_link *link=&m->iks[i].links[j];
            if (link->bone == knee && link->limited)
                return x * M_PI / 180.0 >= link->lower[0] &&
                       x * M_PI / 180.0 <= link->upper[0];
        }
    }
    return 1;
}

static double inspect_leg_error(struct rasterfall_model_asset *m,
                                const struct rasterfall_vmd_clip *v,
                                int side, int time)
{
    const char *name = side ? "右足ＩＫ" : "左足ＩＫ";
    int controller = rasterfall_model_find_bone(m, name), target[3];
    int ankle = -1, i;
    double desired[3], actual[3];
    if (controller < 0) return 0.0;
    for (i=0;i<(int)m->ik_count;i++) if (m->iks[i].controller == controller) { ankle=m->iks[i].target; break; }
    if (ankle < 0) return 0.0;
    rasterfall_vmd_sample_bone_translation(v,name,time,target);
    desired[0]=m->bone_transforms[controller].position[0]+target[0];
    desired[1]=m->bone_transforms[controller].position[1]+target[1];
    desired[2]=m->bone_transforms[controller].position[2]+target[2];
    actual[0]=m->bone_transforms[ankle].position[0];
    actual[1]=m->bone_transforms[ankle].position[1];
    actual[2]=m->bone_transforms[ankle].position[2];
    return inspect_position_delta(desired,actual);
}

static void inspect_leg_sequence(struct rasterfall_model_asset *m,
                                 const struct rasterfall_vmd_clip *v,
                                 const struct rasterfall_animation_clip *clip,
                                 int legacy)
{
    struct inspect_leg_sequence_stats s[2];
    int prev_knee[2][3], prev_thigh[2][3];
    double prev_ankle[2][3];
    int time, side, step=16;
    memset(s,0,sizeof(s));memset(prev_knee,0,sizeof(prev_knee));memset(prev_thigh,0,sizeof(prev_thigh));memset(prev_ankle,0,sizeof(prev_ankle));
    reset_ik_stats(m);m->ik_legacy_knee_ccd=legacy;m->ik_enabled=1;
    for(time=0;time<v->duration_ms;time+=step) {
        prepare_vmd_skeleton_translation(m,v,time,0);rasterfall_model_sample_clip(m,clip,time);
        for(side=0;side<2;side++) {
            int knee=rasterfall_model_find_bone(m,side?"右ひざ":"左ひざ");
            int thigh=rasterfall_model_find_bone(m,side?"右足":"左足");
            int ankle=-1, controller=rasterfall_model_find_bone(m,side?"右足ＩＫ":"左足ＩＫ"), i;
            int state=m->ik_last_leg_solver[side], knee_now[3], thigh_now[3];
            double ankle_now[3], knee_delta=0.0, thigh_delta=0.0, ankle_delta=0.0, error;
            if(knee<0||thigh<0||controller<0||state==0)continue;
            for(i=0;i<(int)m->ik_count;i++)if(m->iks[i].controller==controller){ankle=m->iks[i].target;break;}
            if(ankle<0)continue;
            knee_now[0]=m->bones[knee].rotate_x;knee_now[1]=m->bones[knee].rotate_y;knee_now[2]=m->bones[knee].rotate_z;
            thigh_now[0]=m->bones[thigh].rotate_x;thigh_now[1]=m->bones[thigh].rotate_y;thigh_now[2]=m->bones[thigh].rotate_z;
            ankle_now[0]=m->bone_transforms[ankle].position[0];ankle_now[1]=m->bone_transforms[ankle].position[1];ankle_now[2]=m->bone_transforms[ankle].position[2];
            error=inspect_leg_error(m,v,side,time);s[side].frames++;if(state==1)s[side].analytical++;else s[side].fallback++;
            if(s[side].previous_state) {
                knee_delta=inspect_rotation_delta(prev_knee[side],knee_now);thigh_delta=inspect_rotation_delta(prev_thigh[side],thigh_now);ankle_delta=inspect_position_delta(prev_ankle[side],ankle_now);
                s[side].sum_all_knee+=knee_delta;s[side].sum_all_ankle+=ankle_delta;s[side].all_delta_count++;if(knee_delta>s[side].max_all_knee)s[side].max_all_knee=knee_delta;if(ankle_delta>s[side].max_all_ankle)s[side].max_all_ankle=ankle_delta;
                if(state==1 && s[side].previous_state==1){s[side].sum_a_knee+=knee_delta;s[side].sum_a_thigh+=thigh_delta;s[side].sum_a_ankle+=ankle_delta;s[side].a_delta_count++;if(knee_delta>s[side].max_a_knee)s[side].max_a_knee=knee_delta;if(thigh_delta>s[side].max_a_thigh)s[side].max_a_thigh=thigh_delta;if(ankle_delta>s[side].max_a_ankle)s[side].max_a_ankle=ankle_delta;}
                if(state==2 && s[side].previous_state==2){s[side].sum_c_knee+=knee_delta;s[side].sum_c_ankle+=ankle_delta;s[side].c_delta_count++;if(knee_delta>s[side].max_c_knee)s[side].max_c_knee=knee_delta;if(ankle_delta>s[side].max_c_ankle)s[side].max_c_ankle=ankle_delta;}
                if(state!=s[side].previous_state){s[side].switches++;if(s[side].previous_state==1)s[side].a_to_c++;else s[side].c_to_a++;s[side].sum_switch_knee+=knee_delta;s[side].sum_switch_ankle+=ankle_delta;s[side].switch_delta_count++;if(knee_delta>s[side].max_switch_knee)s[side].max_switch_knee=knee_delta;if(ankle_delta>s[side].max_switch_ankle)s[side].max_switch_ankle=ankle_delta;}
            }
            if(state!=s[side].previous_state){if(s[side].previous_state==1){s[side].a_runs++;s[side].a_run_total+=s[side].current_run;}if(s[side].previous_state==2){s[side].c_runs++;s[side].c_run_total+=s[side].current_run;}s[side].current_run=1;}else s[side].current_run++;
            s[side].previous_state=state;s[side].sum_all_error+=error;if(error>s[side].max_all_error)s[side].max_all_error=error;if(state==1){s[side].sum_a_error+=error;if(error>s[side].max_a_error)s[side].max_a_error=error;}
            memcpy(prev_knee[side],knee_now,sizeof(knee_now));memcpy(prev_thigh[side],thigh_now,sizeof(thigh_now));memcpy(prev_ankle[side],ankle_now,sizeof(ankle_now));
        }
    }
    for(side=0;side<2;side++){if(s[side].previous_state==1){s[side].a_runs++;s[side].a_run_total+=s[side].current_run;}if(s[side].previous_state==2){s[side].c_runs++;s[side].c_run_total+=s[side].current_run;}}
    for(side=0;side<2;side++) {
        double minutes=v->duration_ms/60000.0;
        __printf("leg coverage mode=%s side=%s frames=%lu analytical=%lu %.2f%% fallback=%lu %.2f%% AtoC=%lu CtoA=%lu switches=%lu switches_per_min=%.2f\n",legacy?"legacy":"hybrid",side?"right":"left",s[side].frames,s[side].analytical,s[side].frames?100.0*s[side].analytical/s[side].frames:0.0,s[side].fallback,s[side].frames?100.0*s[side].fallback/s[side].frames:0.0,s[side].a_to_c,s[side].c_to_a,s[side].switches,minutes>0?s[side].switches/minutes:0.0);
        if(!legacy) __printf("leg rejects side=%s geometry_unreachable=%lu no_valid_knee=%lu reconstruction=%lu ambiguous_or_degenerate=%lu\n",side?"right":"left",m->ik_analytic_reject_reason[side][0],m->ik_analytic_reject_reason[side][1],m->ik_analytic_reject_reason[side][2],m->ik_analytic_reject_reason[side][3]);
        __printf("leg runs side=%s longest_analytical=%lu avg_analytical=%.2f longest_fallback=%lu avg_fallback=%.2f switch_knee_max=%.3f avg=%.3f switch_ankle_max=%.3f avg=%.3f\n",side?"right":"left",s[side].a_runs?s[side].a_run_total/s[side].a_runs:0,s[side].a_runs?(double)s[side].a_run_total/s[side].a_runs:0.0,s[side].c_runs?s[side].c_run_total/s[side].c_runs:0,s[side].c_runs?(double)s[side].c_run_total/s[side].c_runs:0.0,s[side].switch_delta_count?s[side].max_switch_knee:0.0,s[side].switch_delta_count?s[side].sum_switch_knee/s[side].switch_delta_count:0.0,s[side].switch_delta_count?s[side].max_switch_ankle:0.0,s[side].switch_delta_count?s[side].sum_switch_ankle/s[side].switch_delta_count:0.0);
        __printf("leg continuity mode=%s side=%s all_knee_max=%.3f all_ankle_max=%.3f analytical_knee_max=%.3f avg=%.3f analytical_thigh_max=%.3f analytical_ankle_max=%.3f avg=%.3f analytical_error_avg=%.3f max=%.3f ccd_knee_max=%.3f avg=%.3f ccd_ankle_max=%.3f avg=%.3f avg_error=%.3f max_error=%.3f\n",legacy?"legacy":"hybrid",side?"right":"left",s[side].max_all_knee,s[side].max_all_ankle,s[side].max_a_knee,s[side].a_delta_count?s[side].sum_a_knee/s[side].a_delta_count:0.0,s[side].max_a_thigh,s[side].max_a_ankle,s[side].a_delta_count?s[side].sum_a_ankle/s[side].a_delta_count:0.0,s[side].analytical?s[side].sum_a_error/s[side].analytical:0.0,s[side].max_a_error,s[side].max_c_knee,s[side].c_delta_count?s[side].sum_c_knee/s[side].c_delta_count:0.0,s[side].max_c_ankle,s[side].c_delta_count?s[side].sum_c_ankle/s[side].c_delta_count:0.0,s[side].frames?s[side].sum_all_error/s[side].frames:0.0,s[side].max_all_error);
    }
}

static void inspect_analytic_thigh_jumps(struct rasterfall_model_asset *m,
                                         const struct rasterfall_vmd_clip *v,
                                         const struct rasterfall_animation_clip *clip)
{
    int time, side, step=16, max_time[2]={-1,-1};
    double max_jump[2]={0.0,0.0};
    unsigned long pole_hist[2][101]={{0}};
    unsigned long pole_count[2]={0,0};
    double pole_min[2]={2.0,2.0}, pole_max[2]={0.0,0.0};
    int previous_state[2]={0,0}, previous_thigh[2][3]={{0,0,0},{0,0,0}};
    reset_ik_stats(m);m->ik_legacy_knee_ccd=0;m->ik_analytic_trace_time_ms=-1;
    for(time=0;time<v->duration_ms;time+=step) {
        prepare_vmd_skeleton_translation(m,v,time,0);rasterfall_model_sample_clip(m,clip,time);
        for(side=0;side<2;side++) {
            int thigh=rasterfall_model_find_bone(m,side?"右足":"左足"), state=m->ik_last_leg_solver[side];
            int now[3];double delta;
            double ratio=m->ik_analytic_last_dynamic_pole_ratio[side];
            if(thigh<0)continue;
            if(ratio<0.0)ratio=0.0;
            if(ratio>1.0)ratio=1.0;
            pole_count[side]++;if(ratio<pole_min[side])pole_min[side]=ratio;if(ratio>pole_max[side])pole_max[side]=ratio;
            pole_hist[side][(int)(ratio*100.0+0.5)]++;
            now[0]=m->bones[thigh].rotate_x;now[1]=m->bones[thigh].rotate_y;now[2]=m->bones[thigh].rotate_z;
            if(previous_state[side]==1 && state==1){delta=inspect_rotation_delta(previous_thigh[side],now);if(delta>max_jump[side]){max_jump[side]=delta;max_time[side]=time;}}
            previous_state[side]=state;memcpy(previous_thigh[side],now,sizeof(now));
        }
    }
    for(side=0;side<2;side++) {
        unsigned long target01=(unsigned long)(pole_count[side]*1/100), target50=(unsigned long)(pole_count[side]*50/100), target99=(unsigned long)(pole_count[side]*99/100), cumulative=0;
        int p01=0,p50=0,p99=0,i;
        for(i=0;i<=100;i++){cumulative+=pole_hist[side][i];if(!p01&&cumulative>target01)p01=i;if(!p50&&cumulative>target50)p50=i;if(!p99&&cumulative>target99)p99=i;}
        __printf("pole conditioning side=%s samples=%lu min=%.6f p01=%.6f p50=%.6f p99=%.6f max=%.6f blend_ratio_lt=0.150000\n",side?"right":"left",pole_count[side],pole_count[side]?pole_min[side]:0.0,p01/100.0,p50/100.0,p99/100.0,pole_count[side]?pole_max[side]:0.0);
        __printf("analytical-only thigh maximum side=%s time=%dms previous=%dms current=%dms delta=%.6fdeg window=±10frames\n",side?"right":"left",max_time[side],max_time[side]-step,max_time[side],max_jump[side]);
        if(max_time[side]<0)continue;
        m->ik_analytic_trace_side=side;
        for(time=max_time[side]-10*step;time<=max_time[side]+10*step;time+=step){if(time<0||time>=v->duration_ms)continue;m->ik_analytic_trace_time_ms=time;prepare_vmd_skeleton_translation(m,v,time,0);rasterfall_model_sample_clip(m,clip,time);}
    }
    {
        static const int history[] = {70080,102480,227680,159008};
        int hi;
        for (hi=0;hi<4;hi++) {
            if (history[hi] >= v->duration_ms) continue;
            prepare_vmd_skeleton_translation(m,v,history[hi],0);
            rasterfall_model_sample_clip(m,clip,history[hi]);
            __printf("analytical history time=%d left=%s right=%s\n",history[hi],m->ik_last_leg_solver[0]==1?"accepted":"rejected/fallback",m->ik_last_leg_solver[1]==1?"accepted":"rejected/fallback");
        }
    }
    m->ik_analytic_trace_time_ms=-1;m->ik_analytic_trace_side=-1;
}

static double inspect_rotation_delta(const int a[3], const int b[3])
{
    struct rasterfall_animation_quaternion qa = rasterfall_animation_quat_from_euler(a[0],a[1],a[2]);
    struct rasterfall_animation_quaternion qb = rasterfall_animation_quat_from_euler(b[0],b[1],b[2]);
    double dot = qa.x*qb.x + qa.y*qb.y + qa.z*qb.z + qa.w*qb.w;
    if (dot < 0.0) dot = -dot;
    if (dot > 1.0) dot = 1.0;
    return 2.0 * atan2(sqrt(1.0-dot*dot), dot) * 180.0 / M_PI;
}

static double inspect_handoff_percentile(double *values, int count, double p)
{
    int i, j, index;
    double value;
    for (i=1;i<count;i++) {
        value=values[i];j=i-1;
        while(j>=0 && values[j]>value){values[j+1]=values[j];j--;}
        values[j+1]=value;
    }
    if(!count)return 0.0;
    index=(int)((count-1)*p);if(index<0)index=0;if(index>=count)index=count-1;
    return values[index];
}

static void inspect_solver_handoff(struct rasterfall_model_asset *m,
                                   const struct rasterfall_vmd_clip *v,
                                   const struct rasterfall_animation_clip *clip,
                                   int inherit)
{
    enum { MAX_HANDOFF=2048, MAX_RUN=2048 };
    static double thigh[2][2][MAX_HANDOFF], knee[2][2][MAX_HANDOFF], ankle[2][2][MAX_HANDOFF];
    static double handoff_pose[2][3][3][MAX_HANDOFF];
    static int run_lengths[2][2][MAX_RUN];
    unsigned long counts[2][2]={{0,0},{0,0}}, transitions[2][2]={{0,0},{0,0}};
    unsigned long same[2][2]={{0,0},{0,0}};
    double sums[2][2][3]={{0}}, maxima[2][2][3]={{0}};
    double pose_sums[2][3][3]={{0}}, pose_maxima[2][3][3]={{0}};
    double top_pose_ankle[2][3][5]={{0}}, top_pose_thigh[2][3][5]={{0}};
    int top_pose_time[2][3][5]={{0}};
    unsigned long runs[2][2]={{0,0},{0,0}}, single[2][2]={{0,0},{0,0}};
    int previous_state[2]={0,0}, previous_thigh[2][3]={{0,0,0},{0,0,0}};
    int previous_knee[2]={0,0}, previous_ankle[2][3]={{0,0,0},{0,0,0}};
    int run_state[2]={0,0}, run_length[2]={0,0};
    int side, time, step=16;
    m->ik_iteration_trace_time_ms=-1;
    m->ik_analytic_trace_time_ms=-1;
    m->ik_analytic_trace_side=-1;
    m->ik_diagnostic_dump=0;
    m->ik_analytical_inherit_diagnostic = inherit;
    memset(m->ik_analytical_cache_valid, 0, sizeof(m->ik_analytical_cache_valid));
    m->ik_last_leg_solver[0] = m->ik_last_leg_solver[1] = 0;
    __printf("handoff diagnostic begin mode=%s\n", inherit ? "analytical-inherit" : "baseline");
    double top_ankle[2][2][5]={{0}}, top_thigh[2][2][5]={{0}};
    int top_time[2][2][5]={{0}};
    memset(counts,0,sizeof(counts));memset(runs,0,sizeof(runs));memset(single,0,sizeof(single));
    for(time=0;time<v->duration_ms;time+=step){
        int current_thigh[2][3], current_knee[2], current_ankle[2][3];
        m->ik_handoff_trace_time_ms = time;
        m->ik_handoff_trace_side = -1;
        m->ik_handoff_snapshot_valid = 0;
        prepare_vmd_skeleton_translation(m,v,time,0);rasterfall_model_sample_clip(m,clip,time);
        for(side=0;side<2;side++){
            int thigh_b=rasterfall_model_find_bone(m,side?"右足":"左足");
            int knee_b=rasterfall_model_find_bone(m,side?"右ひざ":"左ひざ");
            int ankle_b=rasterfall_model_find_bone(m,side?"右足首":"左足首");
            int state=m->ik_last_leg_solver[side];
            if(thigh_b<0 || knee_b<0 || ankle_b<0)continue;
            if(state!=1 && state!=2)continue;
            current_thigh[side][0]=m->bones[thigh_b].rotate_x;current_thigh[side][1]=m->bones[thigh_b].rotate_y;current_thigh[side][2]=m->bones[thigh_b].rotate_z;
            current_knee[side]=m->bones[knee_b].rotate_x;
            current_ankle[side][0]=(int)m->bone_transforms[ankle_b].position[0];current_ankle[side][1]=(int)m->bone_transforms[ankle_b].position[1];current_ankle[side][2]=(int)m->bone_transforms[ankle_b].position[2];
            counts[side][state==1?0:1]++;
            if(run_state[side]==state)run_length[side]++;
            else{
                if(run_state[side] && runs[side][run_state[side]==1?0:1]<MAX_RUN){int ri=run_state[side]==1?0:1;run_lengths[side][ri][runs[side][ri]++]=run_length[side];if(run_length[side]==1)single[side][ri]++;}
                run_state[side]=state;run_length[side]=1;
            }
            if(previous_state[side] && previous_state[side]!=state){
                int type=previous_state[side]==1?0:1, n;
                double td=inspect_rotation_delta(previous_thigh[side],current_thigh[side]);
                double kd=fabs((double)(current_knee[side]-previous_knee[side]));
                double ad=sqrt((double)(current_ankle[side][0]-previous_ankle[side][0])*(current_ankle[side][0]-previous_ankle[side][0])+(double)(current_ankle[side][1]-previous_ankle[side][1])*(current_ankle[side][1]-previous_ankle[side][1])+(double)(current_ankle[side][2]-previous_ankle[side][2])*(current_ankle[side][2]-previous_ankle[side][2]));
                int index=(int)(transitions[side][type]%MAX_HANDOFF);
                thigh[side][type][index]=td;knee[side][type][index]=kd;ankle[side][type][index]=ad;
                sums[side][type][0]+=td;sums[side][type][1]+=kd;sums[side][type][2]+=ad;
                if(td>maxima[side][type][0])maxima[side][type][0]=td;if(kd>maxima[side][type][1])maxima[side][type][1]=kd;if(ad>maxima[side][type][2])maxima[side][type][2]=ad;
                {int j;for(j=0;j<5;j++)if(ad>top_ankle[side][type][j]){int k;for(k=4;k>j;k--){top_ankle[side][type][k]=top_ankle[side][type][k-1];top_thigh[side][type][k]=top_thigh[side][type][k-1];top_time[side][type][k]=top_time[side][type][k-1];}top_ankle[side][type][j]=ad;top_thigh[side][type][j]=td;top_time[side][type][j]=time;break;}}
                transitions[side][type]++;
                if (type == 0 && (m->ik_handoff_snapshot_valid & (1 << side))) {
                    int c0_thigh[3], c1_thigh[3];
                    int c0_knee[3], c1_knee[3];
                    double c0_ankle[3], c1_ankle[3];
                    double previous_ankle_double[3];
                    int i, seg;
                    for (i=0;i<3;i++) {
                        c0_thigh[i]=m->ik_handoff_c0_thigh[side][i];
                        c1_thigh[i]=m->ik_handoff_c1_thigh[side][i];
                        c0_ankle[i]=m->ik_handoff_c0_ankle[side][i];
                        c1_ankle[i]=m->ik_handoff_c1_ankle[side][i];
                        previous_ankle_double[i]=previous_ankle[side][i];
                    }
                    c0_knee[0]=m->ik_handoff_c0_knee[side];c0_knee[1]=c0_knee[2]=0;
                    c1_knee[0]=m->ik_handoff_c1_knee[side];c1_knee[1]=c1_knee[2]=0;
                    {
                        double td[3], kd[3], ad[3];
                        td[0]=inspect_rotation_delta(previous_thigh[side],c0_thigh);
                        td[1]=inspect_rotation_delta(c0_thigh,c1_thigh);
                        td[2]=inspect_rotation_delta(previous_thigh[side],c1_thigh);
                        kd[0]=fabs((double)previous_knee[side]-c0_knee[0]);
                        kd[1]=fabs(c0_knee[0]-c1_knee[0]);
                        kd[2]=fabs((double)previous_knee[side]-c1_knee[0]);
                        ad[0]=inspect_position_delta(previous_ankle_double,c0_ankle);
                        ad[1]=inspect_position_delta(c0_ankle,c1_ankle);
                        ad[2]=inspect_position_delta(previous_ankle_double,c1_ankle);
                        if (inherit && !side && td[0] > 30.0)
                            __printf("left inheritance outlier time=%d "
                                     "state_prev=%d state_now=%d "
                                     "cache_valid_before=%d cache_valid=%d write=%d write_time=%d read=%d read_time=%d "
                                     "P_A_thigh=(%d,%d,%d) cache_thigh=(%d,%d,%d) C0_thigh=(%d,%d,%d) "
                                     "P_A_knee=%d cache_knee=%d C0_knee=%d "
                                     "deltas_thigh=(P-A/cache=%.3f cache/C0=%.3f P-A/C0=%.3f) "
                                     "deltas_knee=(P-A/cache=%d cache/C0=%d P-A/C0=%d) ankle=%.3f\n",
                                     time,previous_state[side],state,
                                     m->ik_analytical_cache_valid_before[side],m->ik_analytical_cache_valid[side],
                                     m->ik_analytical_cache_write[side],m->ik_analytical_cache_write_time[side],
                                     m->ik_analytical_cache_read[side],m->ik_analytical_cache_read_time[side],
                                     previous_thigh[side][0],previous_thigh[side][1],previous_thigh[side][2],
                                     m->ik_analytical_cache_thigh[side][0],m->ik_analytical_cache_thigh[side][1],m->ik_analytical_cache_thigh[side][2],
                                     m->ik_handoff_c0_thigh[side][0],m->ik_handoff_c0_thigh[side][1],m->ik_handoff_c0_thigh[side][2],
                                     previous_knee[side],m->ik_analytical_cache_knee[side],m->ik_handoff_c0_knee[side],
                                     inspect_rotation_delta(previous_thigh[side],m->ik_analytical_cache_thigh[side]),
                                     inspect_rotation_delta(m->ik_analytical_cache_thigh[side],m->ik_handoff_c0_thigh[side]),td[0],
                                     abs(previous_knee[side]-m->ik_analytical_cache_knee[side]),abs(m->ik_analytical_cache_knee[side]-m->ik_handoff_c0_knee[side]),abs(previous_knee[side]-m->ik_handoff_c0_knee[side]),ad[0]);
                        if (time==210400 || time==206512 || time==211376 || time==211328)
                            __printf("handoff paired mode=%s side=%s time=%d "
                                     "AtoC0=(%.3f,%.3f,%.3f) C0toC1=(%.3f,%.3f,%.3f) AtoC1=(%.3f,%.3f,%.3f)\n",
                                     inherit?"analytical-inherit":"baseline",side?"right":"left",time,
                                     td[0],kd[0],ad[0],td[1],kd[1],ad[1],td[2],kd[2],ad[2]);
                        n=(int)(transitions[side][0]-1); if(n>=MAX_HANDOFF)n=MAX_HANDOFF-1;
                        for(seg=0;seg<3;seg++) {
                            handoff_pose[side][seg][0][n]=td[seg];
                            handoff_pose[side][seg][1][n]=kd[seg];
                            handoff_pose[side][seg][2][n]=ad[seg];
                            for(i=0;i<3;i++) {
                                pose_sums[side][seg][i]+=handoff_pose[side][seg][i][n];
                                if(handoff_pose[side][seg][i][n]>pose_maxima[side][seg][i])
                                    pose_maxima[side][seg][i]=handoff_pose[side][seg][i][n];
                            }
                            {
                                int rank, k;
                                for (rank=0;rank<5;rank++) {
                                    if (handoff_pose[side][seg][2][n] > top_pose_ankle[side][seg][rank]) {
                                        for (k=4;k>rank;k--) {
                                            top_pose_ankle[side][seg][k]=top_pose_ankle[side][seg][k-1];
                                            top_pose_thigh[side][seg][k]=top_pose_thigh[side][seg][k-1];
                                            top_pose_time[side][seg][k]=top_pose_time[side][seg][k-1];
                                        }
                                        top_pose_ankle[side][seg][rank]=handoff_pose[side][seg][2][n];
                                        top_pose_thigh[side][seg][rank]=handoff_pose[side][seg][0][n];
                                        top_pose_time[side][seg][rank]=time;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            else if(previous_state[side])same[side][state==1?0:1]++;
            previous_state[side]=state;memcpy(previous_thigh[side],current_thigh[side],sizeof(previous_thigh[side]));previous_knee[side]=current_knee[side];memcpy(previous_ankle[side],current_ankle[side],sizeof(previous_ankle[side]));
        }
    }
    for(side=0;side<2;side++){
        if(run_state[side] && runs[side][run_state[side]==1?0:1]<MAX_RUN){int ri=run_state[side]==1?0:1;run_lengths[side][ri][runs[side][ri]++]=run_length[side];if(run_length[side]==1)single[side][ri]++;}
        __printf("handoff counts side=%s AtoA=%lu AtoC=%lu CtoC=%lu CtoA=%lu switches_per_min=%.2f\n",side?"right":"left",same[side][0],transitions[side][0],same[side][1],transitions[side][1],v->duration_ms>0?(double)(transitions[side][0]+transitions[side][1])*60000.0/v->duration_ms:0.0);
        {
            int type,n=0,i;double minutes=v->duration_ms/60000.0;
            for(type=0;type<2;type++){
                n=(int)transitions[side][type];if(n>MAX_HANDOFF)n=MAX_HANDOFF;
                __printf("handoff side=%s transition=%s count=%d per_min=%.2f thigh(avg/p50/p95/p99/max)=%.3f/%.3f/%.3f/%.3f/%.3f knee(avg/p50/p95/p99/max)=%.3f/%.3f/%.3f/%.3f/%.3f ankle(avg/p50/p95/p99/max)=%.3f/%.3f/%.3f/%.3f/%.3f\n",side?"right":"left",type?"CtoA":"AtoC",n,minutes>0?n/minutes:0.0,n?sums[side][type][0]/n:0.0,n?inspect_handoff_percentile(thigh[side][type],n,0.5):0.0,n?inspect_handoff_percentile(thigh[side][type],n,0.95):0.0,n?inspect_handoff_percentile(thigh[side][type],n,0.99):0.0,maxima[side][type][0],n?sums[side][type][1]/n:0.0,n?inspect_handoff_percentile(knee[side][type],n,0.5):0.0,n?inspect_handoff_percentile(knee[side][type],n,0.95):0.0,n?inspect_handoff_percentile(knee[side][type],n,0.99):0.0,maxima[side][type][1],n?sums[side][type][2]/n:0.0,n?inspect_handoff_percentile(ankle[side][type],n,0.5):0.0,n?inspect_handoff_percentile(ankle[side][type],n,0.95):0.0,n?inspect_handoff_percentile(ankle[side][type],n,0.99):0.0,maxima[side][type][2]);
                for(i=0;i<5;i++)if(top_time[side][type][i])__printf("handoff top side=%s transition=%s rank=%d time=%d thigh=%.3f ankle=%.3f\n",side?"right":"left",type?"CtoA":"AtoC",i+1,top_time[side][type][i],top_thigh[side][type][i],top_ankle[side][type][i]);
            }
        }
        {
            int ri,i,max_run;for(ri=0;ri<2;ri++){max_run=0;for(i=0;i<(int)runs[side][ri];i++)if(run_lengths[side][ri][i]>max_run)max_run=run_lengths[side][ri][i];__printf("solver runs side=%s solver=%s total=%lu single_frame_runs=%lu max=%d\n",side?"right":"left",ri?"CCD":"analytical",runs[side][ri],single[side][ri],max_run);}
        }
        {
            int seg, i, n=(int)transitions[side][0];
            if(n>MAX_HANDOFF)n=MAX_HANDOFF;
            for(seg=0;seg<3;seg++) {
                const char *label=seg==0?"AtoC0":seg==1?"C0toC1":"AtoC1";
                __printf("handoff pose side=%s segment=%s count=%d "
                         "thigh(avg/p50/p90/p95/p99/max)=%.3f/%.3f/%.3f/%.3f/%.3f/%.3f "
                         "knee(avg/p50/p90/p95/p99/max)=%.3f/%.3f/%.3f/%.3f/%.3f/%.3f "
                         "ankle(avg/p50/p90/p95/p99/max)=%.3f/%.3f/%.3f/%.3f/%.3f/%.3f\n",
                         side?"right":"left",label,n,
                         n?pose_sums[side][seg][0]/n:0.0,n?inspect_handoff_percentile(handoff_pose[side][seg][0],n,.50):0.0,
                         n?inspect_handoff_percentile(handoff_pose[side][seg][0],n,.90):0.0,n?inspect_handoff_percentile(handoff_pose[side][seg][0],n,.95):0.0,
                         n?inspect_handoff_percentile(handoff_pose[side][seg][0],n,.99):0.0,pose_maxima[side][seg][0],
                         n?pose_sums[side][seg][1]/n:0.0,n?inspect_handoff_percentile(handoff_pose[side][seg][1],n,.50):0.0,
                         n?inspect_handoff_percentile(handoff_pose[side][seg][1],n,.90):0.0,n?inspect_handoff_percentile(handoff_pose[side][seg][1],n,.95):0.0,
                         n?inspect_handoff_percentile(handoff_pose[side][seg][1],n,.99):0.0,pose_maxima[side][seg][1],
                         n?pose_sums[side][seg][2]/n:0.0,n?inspect_handoff_percentile(handoff_pose[side][seg][2],n,.50):0.0,
                         n?inspect_handoff_percentile(handoff_pose[side][seg][2],n,.90):0.0,n?inspect_handoff_percentile(handoff_pose[side][seg][2],n,.95):0.0,
                         n?inspect_handoff_percentile(handoff_pose[side][seg][2],n,.99):0.0,pose_maxima[side][seg][2]);
                for (i=0;i<5;i++) if (top_pose_time[side][seg][i])
                    __printf("handoff pose top side=%s segment=%s rank=%d time=%d thigh=%.3f ankle=%.3f\n",
                             side?"right":"left",label,i+1,top_pose_time[side][seg][i],
                             top_pose_thigh[side][seg][i],top_pose_ankle[side][seg][i]);
            }
        }
    }
    m->ik_handoff_trace_time_ms=-1;
    m->ik_handoff_trace_side=-1;
    m->ik_handoff_snapshot_valid=0;
    m->ik_analytical_inherit_diagnostic=0;
}

static double inspect_position_delta(const double a[3], const double b[3])
{
    double x=a[0]-b[0], y=a[1]-b[1], z=a[2]-b[2];
    return sqrt(x*x+y*y+z*z);
}

static double inspect_rotation_delta(const int a[3], const int b[3]);

static void inspect_ccd_metric(const char *name, double *v, int n)
{
    static double sorted[20000];
    double sum=0.0;
    int i;
    for(i=0;i<n;i++)sum+=v[i];
    if (n > 20000) n = 20000;
    memcpy(sorted, v, (unsigned long)n * sizeof(double));
    __printf("ccd motion metric=%s count=%d avg=%.3f p50=%.3f p90=%.3f p95=%.3f p99=%.3f max=%.3f\n",
             name,n,n?sum/n:0.0,
             n?inspect_handoff_percentile(sorted,n,.50):0.0,n?inspect_handoff_percentile(sorted,n,.90):0.0,
             n?inspect_handoff_percentile(sorted,n,.95):0.0,n?inspect_handoff_percentile(sorted,n,.99):0.0,
             n?inspect_handoff_percentile(sorted,n,1.0):0.0);
}

static void inspect_knee_branch_scan(struct rasterfall_model_asset *m,
                                     const struct rasterfall_vmd_clip *v,
                                     const struct rasterfall_animation_clip *clip)
{
    enum { MAX_SAMPLES=20000, TOP=5, TRANSITIONS=4 };
    static double angles[2][TRANSITIONS][MAX_SAMPLES];
    struct inspect_knee_branch_value previous[2];
    struct inspect_knee_branch_value top_prev[2][TOP], top_now[2][TOP];
    double top_angle[2][TOP], top_thigh[2][TOP], top_knee[2][TOP];
    double top_ankle[2][TOP], top_target_delta[2][TOP];
    int top_time[2][TOP], top_transition[2][TOP];
    double top_knee_x[2][TOP];
    int top_knee_valid[2][TOP];
    double top_same_error[2][TOP], top_mirror_error[2][TOP];
    unsigned int top_same_iteration[2][TOP], top_mirror_iteration[2][TOP];
    int top_c0_sign[2][TOP], top_best_sign[2][TOP];
    unsigned long cc_compared[2]={0,0}, cc_prev_c0_flip[2]={0,0};
    unsigned long cc_c0_best_compared[2]={0,0}, cc_c0_best_flip[2]={0,0};
    unsigned long cc_same_exists[2]={0,0}, cc_mirror_exists[2]={0,0};
    unsigned long cc_runtime_mirror[2]={0,0}, cc_same_le_c0_mirror_selected[2]={0,0};
    double cc_mirror_advantage[2][MAX_SAMPLES];
    int cc_mirror_advantage_count[2]={0,0};
    unsigned long transitions[2][TRANSITIONS]={{0}}, flips[2][TRANSITIONS]={{0}};
    unsigned long over90[2][TRANSITIONS]={{0}}, over150[2][TRANSITIONS]={{0}};
    unsigned long c0_best_compared[2]={0,0}, c0_best_valid[2]={0,0}, c0_best_flip[2]={0,0};
    unsigned long c0_best_pmx_both_valid[2]={0,0}, c0_best_pmx_one_invalid[2]={0,0};
    unsigned long c0_best_bent[2]={0,0};
    double c0_best_sum_improvement[2]={0,0}, c0_best_max_improvement[2]={0,0};
    double previous_target[2][3];
    double previous_ankle[2][3];
    int previous_thigh[2][3], previous_knee[2];
    int previous_state[2]={0,0}, have_target[2]={0,0};
    int have_pose[2]={0,0};
    int side,time,step=16;
    memset(previous,0,sizeof(previous));memset(top_angle,0,sizeof(top_angle));
    memset(top_time,0,sizeof(top_time));memset(top_transition,0,sizeof(top_transition));
    m->ik_analytical_inherit_diagnostic=1;
    m->ik_best_iteration_enabled=1;
    memset(m->ik_analytical_cache_valid,0,sizeof(m->ik_analytical_cache_valid));
    m->ik_last_leg_solver[0]=m->ik_last_leg_solver[1]=0;
    __printf("knee branch diagnostic begin bent_ratio_min=0.050 sign_band=±0.150\n");
    for(time=0;time<v->duration_ms;time+=step){
        m->ik_handoff_trace_time_ms=time;
        m->ik_handoff_trace_side=-1;
        m->ik_handoff_snapshot_valid=0;
        prepare_vmd_skeleton_translation(m,v,time,0);
        rasterfall_model_sample_clip(m,clip,time);
        for(side=0;side<2;side++){
            const char *name=side?"右足ＩＫ":"左足ＩＫ";
            int controller=rasterfall_model_find_bone(m,name), t[3];
            double target[3], thigh_delta=0.0,knee_delta=0.0,ankle_delta=0.0,target_delta=0.0;
            struct inspect_knee_branch_value now;
            if(controller<0)continue;
            rasterfall_vmd_sample_bone_translation(v,name,time,t);
            target[0]=m->bone_transforms[controller].position[0]+t[0];
            target[1]=m->bone_transforms[controller].position[1]+t[1];
            target[2]=m->bone_transforms[controller].position[2]+t[2];
            now=inspect_knee_branch_value(m,side,target);
            if(!now.valid)continue;
            if(previous_state[side] && previous[side].valid){
                int type=previous_state[side]==1
                    ? (m->ik_last_leg_solver[side]==1 ? 0:1)
                    : (m->ik_last_leg_solver[side]==1 ? 3:2);
                double angle=inspect_branch_angle(&previous[side],&now);
                unsigned long n=transitions[side][type];
                int flip=previous[side].ratio>=0.05 && now.ratio>=0.05 &&
                         previous[side].sign!=0 && now.sign!=0 &&
                         previous[side].sign!=now.sign;
                if(n<MAX_SAMPLES)angles[side][type][n]=angle;
                transitions[side][type]++;
                if(flip)flips[side][type]++;
                if(angle>90.0)over90[side][type]++;
                if(angle>150.0)over150[side][type]++;
                {
                    int thigh=rasterfall_model_find_bone(m,side?"右足":"左足");
                    int knee=rasterfall_model_find_bone(m,side?"右ひざ":"左ひざ");
                    int ankle=now.valid ? rasterfall_model_find_bone(m,side?"右足首":"左足首") : -1;
                    int thigh_now[3];
                    double ankle_now[3];
                    thigh_now[0]=m->bones[thigh].rotate_x;
                    thigh_now[1]=m->bones[thigh].rotate_y;
                    thigh_now[2]=m->bones[thigh].rotate_z;
                    if (ankle >= 0) {
                        ankle_now[0]=m->bone_transforms[ankle].position[0];
                        ankle_now[1]=m->bone_transforms[ankle].position[1];
                        ankle_now[2]=m->bone_transforms[ankle].position[2];
                    } else {
                        ankle_now[0]=ankle_now[1]=ankle_now[2]=0.0;
                    }
                    if(have_pose[side]) {
                        thigh_delta=inspect_rotation_delta(previous_thigh[side],thigh_now);
                        knee_delta=fabs((double)m->bones[knee].rotate_x-previous_knee[side]);
                        ankle_delta=inspect_position_delta(previous_ankle[side],ankle_now);
                    }
                    if(have_target[side])target_delta=inspect_position_delta(previous_target[side],target);
                    if(type == 2 && flip && angle>top_angle[side][TOP-1] && now.ratio>=0.05 && previous[side].ratio>=0.05){
                        int j;
                        for(j=TOP-1;j>0;j--){
                            top_angle[side][j]=top_angle[side][j-1];
                            top_prev[side][j]=top_prev[side][j-1];top_now[side][j]=top_now[side][j-1];
                            top_time[side][j]=top_time[side][j-1];top_transition[side][j]=top_transition[side][j-1];
                            top_thigh[side][j]=top_thigh[side][j-1];top_knee[side][j]=top_knee[side][j-1];top_ankle[side][j]=top_ankle[side][j-1];top_target_delta[side][j]=top_target_delta[side][j-1];
                            top_knee_x[side][j]=top_knee_x[side][j-1];top_knee_valid[side][j]=top_knee_valid[side][j-1];
                            top_same_error[side][j]=top_same_error[side][j-1];top_mirror_error[side][j]=top_mirror_error[side][j-1];
                            top_same_iteration[side][j]=top_same_iteration[side][j-1];top_mirror_iteration[side][j]=top_mirror_iteration[side][j-1];
                            top_c0_sign[side][j]=top_c0_sign[side][j-1];top_best_sign[side][j]=top_best_sign[side][j-1];
                        }
                        top_angle[side][0]=angle;top_prev[side][0]=previous[side];top_now[side][0]=now;
                        top_time[side][0]=time;top_transition[side][0]=type;
                        top_thigh[side][0]=thigh_delta;top_knee[side][0]=knee_delta;top_ankle[side][0]=ankle_delta;top_target_delta[side][0]=target_delta;
                        top_knee_x[side][0]=m->bones[knee].rotate_x;
                        top_knee_valid[side][0]=inspect_knee_pmx_valid(m,side,knee,m->bones[knee].rotate_x);
                        top_same_error[side][0]=m->ik_ccd_diag_best_same_error[side];
                        top_mirror_error[side][0]=m->ik_ccd_diag_best_mirror_error[side];
                        top_same_iteration[side][0]=m->ik_ccd_diag_best_same_iteration[side];
                        top_mirror_iteration[side][0]=m->ik_ccd_diag_best_mirror_iteration[side];
                        top_c0_sign[side][0]=m->ik_ccd_diag_c0_branch_sign[side];
                        top_best_sign[side][0]=m->ik_ccd_diag_best_branch_sign[side];
                    }
                    memcpy(previous_thigh[side],thigh_now,sizeof(thigh_now));
                    previous_knee[side]=m->bones[knee].rotate_x;
                    memcpy(previous_ankle[side],ankle_now,sizeof(ankle_now));
                    have_pose[side]=1;
                }
            }
            if(previous_state[side]==1 && m->ik_last_leg_solver[side]==2 && (m->ik_handoff_snapshot_valid&(1<<side))){
                int thigh=rasterfall_model_find_bone(m,side?"右足":"左足");
                int knee=rasterfall_model_find_bone(m,side?"右ひざ":"左ひざ");
                int saved_thigh[3], saved_knee;
                struct inspect_knee_branch_value c0, best;
                double c0_knee_x, best_knee_x;
                saved_thigh[0]=m->bones[thigh].rotate_x;saved_thigh[1]=m->bones[thigh].rotate_y;saved_thigh[2]=m->bones[thigh].rotate_z;saved_knee=m->bones[knee].rotate_x;
                m->bones[thigh].rotate_x=m->ik_handoff_c0_thigh[side][0];m->bones[thigh].rotate_y=m->ik_handoff_c0_thigh[side][1];m->bones[thigh].rotate_z=m->ik_handoff_c0_thigh[side][2];m->bones[knee].rotate_x=m->ik_handoff_c0_knee[side];m->bones[knee].rotate_y=m->bones[knee].rotate_z=0;rasterfall_model_update_bones(m);c0=inspect_knee_branch_value(m,side,target);
                c0_knee_x=m->bones[knee].rotate_x;
                m->bones[thigh].rotate_x=m->ik_ccd_diag_best_thigh[side][0];m->bones[thigh].rotate_y=m->ik_ccd_diag_best_thigh[side][1];m->bones[thigh].rotate_z=m->ik_ccd_diag_best_thigh[side][2];m->bones[knee].rotate_x=m->ik_ccd_diag_best_knee[side];rasterfall_model_update_bones(m);best=inspect_knee_branch_value(m,side,target);
                best_knee_x=m->bones[knee].rotate_x;
                m->bones[thigh].rotate_x=saved_thigh[0];m->bones[thigh].rotate_y=saved_thigh[1];m->bones[thigh].rotate_z=saved_thigh[2];m->bones[knee].rotate_x=saved_knee;rasterfall_model_update_bones(m);
                c0_best_compared[side]++;
                if (inspect_knee_pmx_valid(m,side,knee,c0_knee_x) &&
                    inspect_knee_pmx_valid(m,side,knee,best_knee_x))
                    c0_best_pmx_both_valid[side]++;
                else
                    c0_best_pmx_one_invalid[side]++;
                if(c0.valid && best.valid && c0.ratio>=0.05 && best.ratio>=0.05 && c0.sign!=0 && best.sign!=0){
                    c0_best_valid[side]++;c0_best_bent[side]++;
                    if(c0.sign!=best.sign){double imp=m->ik_ccd_diag_initial_error[side]-m->ik_ccd_diag_best_error[side];c0_best_flip[side]++;c0_best_sum_improvement[side]+=imp;if(imp>c0_best_max_improvement[side])c0_best_max_improvement[side]=imp;}
                }
            }
            if (previous_state[side] == 2 && m->ik_last_leg_solver[side] == 2 &&
                m->ik_ccd_diag_valid[side]) {
                int c0_sign=m->ik_ccd_diag_c0_branch_sign[side];
                int best_sign=m->ik_ccd_diag_best_branch_sign[side];
                double same=m->ik_ccd_diag_best_same_error[side];
                double mirror=m->ik_ccd_diag_best_mirror_error[side];
                int bent=previous[side].ratio>=0.05 && now.ratio>=0.05;
                if (bent && previous[side].sign && c0_sign) {
                    cc_compared[side]++;
                    if (previous[side].sign != c0_sign) cc_prev_c0_flip[side]++;
                    if (best_sign && best_sign != c0_sign) cc_c0_best_flip[side]++;
                }
                if (c0_sign && bent && same < 1.0e29) {
                    cc_c0_best_compared[side]++;cc_same_exists[side]++;
                    if (mirror < 1.0e29) {
                        cc_mirror_exists[side]++;
                        if (cc_mirror_advantage_count[side] < MAX_SAMPLES)
                            cc_mirror_advantage[side][cc_mirror_advantage_count[side]++]=same-mirror;
                        if (best_sign != c0_sign) {
                            cc_runtime_mirror[side]++;
                            if (same <= m->ik_ccd_diag_initial_error[side])
                                cc_same_le_c0_mirror_selected[side]++;
                        }
                    }
                }
            }
            previous[side]=now;previous_state[side]=m->ik_last_leg_solver[side];memcpy(previous_target[side],target,sizeof(target));have_target[side]=1;
        }
    }
    for(side=0;side<2;side++){
        static const char *labels[4]={"AtoA","AtoC","CtoC","CtoA"};
        int type;
        __printf("knee branch side=%s C0_to_best_compared=%lu branch_valid_pairs=%lu branch_flip=%lu rate=%.3f pmx_both_valid=%lu pmx_one_invalid=%lu avg_error_improvement=%.3f max_error_improvement=%.3f\n",side?"right":"left",c0_best_compared[side],c0_best_valid[side],c0_best_flip[side],c0_best_valid[side]?100.0*c0_best_flip[side]/c0_best_valid[side]:0.0,c0_best_pmx_both_valid[side],c0_best_pmx_one_invalid[side],c0_best_flip[side]?c0_best_sum_improvement[side]/c0_best_flip[side]:0.0,c0_best_max_improvement[side]);
        __printf("knee branch CtoC side=%s bent_compared=%lu prev_to_C0_flips=%lu rate=%.3f C0_to_best_flips=%lu rate=%.3f same_exists=%lu mirror_exists=%lu runtime_mirror=%lu same_le_C0_mirror_selected=%lu\n",side?"right":"left",cc_compared[side],cc_prev_c0_flip[side],cc_compared[side]?100.0*cc_prev_c0_flip[side]/cc_compared[side]:0.0,cc_c0_best_flip[side],cc_c0_best_compared[side]?100.0*cc_c0_best_flip[side]/cc_c0_best_compared[side]:0.0,cc_same_exists[side],cc_mirror_exists[side],cc_runtime_mirror[side],cc_same_le_c0_mirror_selected[side]);
        inspect_ccd_metric("CtoC_mirror_advantage_same_minus_mirror",cc_mirror_advantage[side],cc_mirror_advantage_count[side]);
        for(type=0;type<4;type++){
            double *a=angles[side][type];int n=(int)(transitions[side][type]);
            __printf("knee branch transition=%s total=%lu flips=%lu rate=%.3f >90=%lu >150=%lu\n",labels[type],transitions[side][type],flips[side][type],transitions[side][type]?100.0*flips[side][type]/transitions[side][type]:0.0,over90[side][type],over150[side][type]);
            if(n>MAX_SAMPLES)n=MAX_SAMPLES;
            inspect_ccd_metric("branch_angle",a,n);
        }
        for(type=0;type<TOP;type++)if(top_angle[side][type]>0.0){
            __printf("knee branch top side=%s rank=%d time=%d transition=%s ratio=%.3f->%.3f dot=%.3f->%.3f sign=%d->%d angle=%.3f thigh_delta=%.3f knee_delta=%.3f ankle_delta=%.3f target_delta=%.3f knee_x=%.3f pmx_valid=%s C0sign=%d bestsign=%d same=%.3f@%u mirror=%.3f@%u Kprev=(%.1f,%.1f,%.1f) Know=(%.1f,%.1f,%.1f)\n",side?"right":"left",type+1,top_time[side][type],labels[top_transition[side][type]],top_prev[side][type].ratio,top_now[side][type].ratio,top_prev[side][type].dot,top_now[side][type].dot,top_prev[side][type].sign,top_now[side][type].sign,top_angle[side][type],top_thigh[side][type],top_knee[side][type],top_ankle[side][type],top_target_delta[side][type],top_knee_x[side][type],top_knee_valid[side][type]?"yes":"no",top_c0_sign[side][type],top_best_sign[side][type],top_same_error[side][type],top_same_iteration[side][type],top_mirror_error[side][type],top_mirror_iteration[side][type],top_prev[side][type].k[0],top_prev[side][type].k[1],top_prev[side][type].k[2],top_now[side][type].k[0],top_now[side][type].k[1],top_now[side][type].k[2]);
        }
    }
    m->ik_analytical_inherit_diagnostic=0;
    m->ik_handoff_trace_time_ms=-1;
    m->ik_handoff_trace_side=-1;
    m->ik_handoff_snapshot_valid=0;
}

static void inspect_ccd_motion(struct rasterfall_model_asset *m,
                               const struct rasterfall_vmd_clip *v,
                               const struct rasterfall_animation_clip *clip,
                               int best_mode)
{
    enum { MAX_SAMPLES=20000 };
    static double values[2][7][MAX_SAMPLES];
    static double best_errors[2][MAX_SAMPLES], final_errors[2][MAX_SAMPLES];
    static unsigned int iteration_counts[2][MAX_SAMPLES];
    static int times[2][MAX_SAMPLES];
    static double alignment[2][5][MAX_SAMPLES];
    static double thigh_moves[2][MAX_SAMPLES], knee_moves[2][MAX_SAMPLES];
    static unsigned int best_iterations[2][MAX_SAMPLES];
    double previous_target[2][3];
    int have_target[2]={0,0}, count[2]={0,0};
    int previous_state[2]={0,0};
    int type_count[2][4]={{0}}, best_worse[2][3]={{0}};
    int side,time,step=16;
    int best_c0_count[2]={0,0}, ratio_105[2]={0,0}, ratio_125[2]={0,0};
    int ratio_150[2]={0,0}, ratio_200[2]={0,0};
    int best_hist[2][7]={{0}};
    double restore_diffs[2][MAX_SAMPLES];
    const char *mode_name = best_mode ? "best-retain" : "baseline";
    m->ik_analytical_inherit_diagnostic=1;
    m->ik_best_iteration_enabled=best_mode ? 1 : 0;
    memset(m->ik_analytical_cache_valid,0,sizeof(m->ik_analytical_cache_valid));
    m->ik_last_leg_solver[0]=m->ik_last_leg_solver[1]=0;
    for(time=0;time<v->duration_ms;time+=step){
        m->ik_handoff_trace_time_ms=time;
        m->ik_handoff_trace_side=-1;
        m->ik_handoff_snapshot_valid=0;
        m->ik_iteration_trace_time_ms = -1;
        prepare_vmd_skeleton_translation(m,v,time,0);
        rasterfall_model_sample_clip(m,clip,time);
        for(side=0;side<2;side++){
            const char *name=side?"右足ＩＫ":"左足ＩＫ";
            int controller=rasterfall_model_find_bone(m,name), t[3];
            double target[3], sampled_target[3];
            if(controller<0)continue;
            rasterfall_vmd_sample_bone_translation(v,name,time,t);
            sampled_target[0]=m->bone_transforms[controller].position[0]+t[0];
            sampled_target[1]=m->bone_transforms[controller].position[1]+t[1];
            sampled_target[2]=m->bone_transforms[controller].position[2]+t[2];
            target[0]=m->ik_ccd_diag_target[side][0];
            target[1]=m->ik_ccd_diag_target[side][1];
            target[2]=m->ik_ccd_diag_target[side][2];
            if (!m->ik_ccd_diag_valid[side]) {
                target[0]=sampled_target[0];target[1]=sampled_target[1];target[2]=sampled_target[2];
            }
            if(previous_state[side]==1 && m->ik_last_leg_solver[side]==2 &&
               (m->ik_handoff_snapshot_valid&(1<<side)) && m->ik_ccd_diag_valid[side] && count[side]<MAX_SAMPLES){
                double e0=m->ik_ccd_diag_initial_error[side];
                double e1=m->ik_c1_error[side];
                double move=inspect_position_delta(m->ik_ccd_diag_initial_ankle[side],m->ik_ccd_diag_final_ankle[side]);
                double td=have_target[side]?inspect_position_delta(previous_target[side],target):0.0;
                double improve=e0-e1, rel=e0>0.000001?improve/e0:0.0;
                double efficiency=move>0.000001?improve/move:0.0;
                double regret=m->ik_c1_error[side]-m->ik_ccd_diag_best_error[side];
                int n=count[side];
                values[side][0][n]=e0;values[side][1][n]=move;values[side][2][n]=e1;
                values[side][3][n]=td;values[side][4][n]=rel;values[side][5][n]=efficiency;values[side][6][n]=regret;times[side][n]=time;count[side]++;
                thigh_moves[side][n] = inspect_rotation_delta(
                    m->ik_handoff_c0_thigh[side], m->ik_handoff_c1_thigh[side]);
                knee_moves[side][n] = fabs((double)m->ik_handoff_c0_knee[side] -
                                           (double)m->ik_handoff_c1_knee[side]);
                best_iterations[side][n] = m->ik_ccd_diag_best_is_c0[side] ?
                    (unsigned int)-1 : m->ik_ccd_diag_best_iteration[side];
                restore_diffs[side][n] = fabs(m->ik_c1_error[side] -
                                              m->ik_ccd_diag_best_error[side]);
                if (m->ik_ccd_diag_best_is_c0[side]) best_c0_count[side]++;
                if (best_iterations[side][n] == (unsigned int)-1) best_hist[side][0]++;
                else if (best_iterations[side][n] <= 1) best_hist[side][1]++;
                else if (best_iterations[side][n] <= 3) best_hist[side][2]++;
                else if (best_iterations[side][n] <= 7) best_hist[side][3]++;
                else if (best_iterations[side][n] <= 15) best_hist[side][4]++;
                else if (best_iterations[side][n] <= 31) best_hist[side][5]++;
                else best_hist[side][6]++;
                if (m->ik_ccd_diag_best_error[side] > 0.000001) {
                    double ratio = m->ik_c1_error[side] / m->ik_ccd_diag_best_error[side];
                    if (ratio > 1.05) ratio_105[side]++;
                    if (ratio > 1.25) ratio_125[side]++;
                    if (ratio > 1.50) ratio_150[side]++;
                    if (ratio > 2.00) ratio_200[side]++;
                }
                alignment[side][0][n] = inspect_rotation_delta(
                    m->ik_solver_return_thigh[side], m->ik_handoff_c1_thigh[side]);
                alignment[side][1][n] = fabs((double)m->ik_solver_return_knee[side] -
                                             (double)m->ik_handoff_c1_knee[side]);
                alignment[side][2][n] = inspect_position_delta(
                    m->ik_solver_return_ankle[side], m->ik_handoff_c1_ankle[side]);
                alignment[side][3][n] = inspect_position_delta(
                    m->ik_solver_return_target[side], m->ik_c1_target[side]);
                alignment[side][4][n] = fabs(m->ik_solver_return_error[side] -
                                             m->ik_c1_error[side]);
                best_errors[side][n]=m->ik_ccd_diag_best_error[side];
                final_errors[side][n]=m->ik_c1_error[side];
                iteration_counts[side][n]=m->ik_ccd_diag_iterations[side];
                if(e1>e0){type_count[side][2]++;}
                else if(rel>=0.5)type_count[side][0]++;
                else type_count[side][1]++;
                if(td>e0*0.5 && td>move*0.5)type_count[side][3]++;
                if(regret>0.05)best_worse[side][0]++;
                if(regret>m->ik_ccd_diag_best_error[side]*0.25)best_worse[side][1]++;
                if(regret>m->ik_ccd_diag_best_error[side]*0.50)best_worse[side][2]++;
            }
            memcpy(previous_target[side],sampled_target,sizeof(sampled_target));have_target[side]=1;
            previous_state[side]=m->ik_last_leg_solver[side];
        }
    }
    for(side=0;side<2;side++){
        int i,j;
        __printf("ccd motion mode=%s side=%s transition=AtoC count=%d typeA_high_improvement=%d typeB_low_efficiency=%d typeC_error_worsened=%d typeD_target_dominant=%d\n",mode_name,side?"right":"left",count[side],type_count[side][0],type_count[side][1],type_count[side][2],type_count[side][3]);
        inspect_ccd_metric("E0",values[side][0],count[side]);
        inspect_ccd_metric("M",values[side][1],count[side]);
        inspect_ccd_metric("E1",values[side][2],count[side]);
        inspect_ccd_metric("target_delta",values[side][3],count[side]);
        inspect_ccd_metric("relative_improvement",values[side][4],count[side]);
        inspect_ccd_metric("movement_efficiency",values[side][5],count[side]);
        inspect_ccd_metric("final_minus_best_error",values[side][6],count[side]);
        inspect_ccd_metric("solver_to_c1_thigh_deg",alignment[side][0],count[side]);
        inspect_ccd_metric("solver_to_c1_knee_deg",alignment[side][1],count[side]);
        inspect_ccd_metric("solver_to_c1_ankle",alignment[side][2],count[side]);
        inspect_ccd_metric("solver_to_c1_target",alignment[side][3],count[side]);
        inspect_ccd_metric("solver_to_c1_error",alignment[side][4],count[side]);
        inspect_ccd_metric("restore_best_error_diff",restore_diffs[side],count[side]);
        inspect_ccd_metric("C0_to_C1_thigh_deg",thigh_moves[side],count[side]);
        inspect_ccd_metric("C0_to_C1_knee_deg",knee_moves[side],count[side]);
        __printf("ccd best audit mode=%s side=%s final_worse_0.05=%d final_worse_25pct=%d final_worse_50pct=%d ratio_gt_1.05=%d ratio_gt_1.25=%d ratio_gt_1.50=%d ratio_gt_2.00=%d best_eq_C0=%d %.2f%% hist=C0:%d iter0-1:%d iter2-3:%d iter4-7:%d iter8-15:%d iter16-31:%d iter32-39:%d\n",mode_name,side?"right":"left",best_worse[side][0],best_worse[side][1],best_worse[side][2],ratio_105[side],ratio_125[side],ratio_150[side],ratio_200[side],best_c0_count[side],count[side]?100.0*best_c0_count[side]/count[side]:0.0,best_hist[side][0],best_hist[side][1],best_hist[side][2],best_hist[side][3],best_hist[side][4],best_hist[side][5],best_hist[side][6]);
        for(j=0;j<5;j++){
            int best=-1;
            for(i=0;i<count[side];i++) if(values[side][1][i]>=0 && (best<0||values[side][1][i]>values[side][1][best])) best=i;
            if(best<0)break;
            __printf("ccd top movement side=%s rank=%d time=%d E0=%.3f M=%.3f E1=%.3f target_delta=%.3f best=%.3f final=%.3f iterations=%u\n",side?"right":"left",j+1,times[side][best],values[side][0][best],values[side][1][best],values[side][2][best],values[side][3][best],best_errors[side][best],final_errors[side][best],iteration_counts[side][best]);
            values[side][1][best]=-1.0;
        }
    }
    m->ik_analytical_inherit_diagnostic=0;
    m->ik_best_iteration_enabled=1;
}

static void inspect_vmd_leg_trace(struct rasterfall_model_asset *m,
                                  const struct rasterfall_vmd_clip *v,
                                  const struct rasterfall_animation_clip *clip)
{
    static const char *names[2][4] = {
        {"左足ＩＫ","左足","左ひざ","左足D"},
        {"右足ＩＫ","右足","右ひざ","右足D"}
    };
    double previous_target[2][3], previous_ankle[2][3];
    int previous_thigh[2][3], previous_knee[2][3], previous_d[2][3];
    double peak_target_before[2][3], peak_target_after[2][3], peak_ankle_before[2][3], peak_ankle_after[2][3];
    int peak_thigh_before[2][3], peak_thigh_after[2][3], peak_knee_before[2][3], peak_knee_after[2][3], peak_d_before[2][3], peak_d_after[2][3];
    double max_target[2]={0,0}, max_thigh[2]={0,0}, max_knee[2]={0,0}, max_d[2]={0,0}, max_ankle[2]={0,0};
    int max_target_time[2]={0,0}, max_thigh_time[2]={0,0}, max_knee_time[2]={0,0}, max_d_time[2]={0,0}, max_ankle_time[2]={0,0};
    int time, have=0, side;
    __printf("vmd leg trace: translation_sampling=linear, solver_init=FK_base_each_frame, frame_step=33ms\n");
    prepare_vmd_skeleton_translation(m,v,0,0);
    rasterfall_model_sample_clip(m,clip,0);
    for (time=0; time<=v->duration_ms; time+=1000/30) {
        for (side=0; side<2; side++) {
            int controller=rasterfall_model_find_bone(m,names[side][0]);
            int thigh=rasterfall_model_find_bone(m,names[side][1]);
            int knee=rasterfall_model_find_bone(m,names[side][2]);
            int dbone=rasterfall_model_find_bone(m,names[side][3]);
            int ankle=-1, ik, t[3], euler_thigh[3], euler_knee[3], euler_d[3];
            double target[3], ankle_pos[3];
            if (controller<0 || thigh<0 || knee<0 || dbone<0) continue;
            for (ik=0;ik<(int)m->ik_count;ik++) if(m->iks[ik].controller==controller){ankle=m->iks[ik].target;break;}
            rasterfall_vmd_sample_bone_translation(v,names[side][0],time,t);
            target[0]=m->bone_transforms[controller].position[0]+t[0];
            target[1]=m->bone_transforms[controller].position[1]+t[1];
            target[2]=m->bone_transforms[controller].position[2]+t[2];
            ankle_pos[0]=ankle>=0?m->bone_transforms[ankle].position[0]:0;
            ankle_pos[1]=ankle>=0?m->bone_transforms[ankle].position[1]:0;
            ankle_pos[2]=ankle>=0?m->bone_transforms[ankle].position[2]:0;
            euler_thigh[0]=m->bones[thigh].rotate_x;euler_thigh[1]=m->bones[thigh].rotate_y;euler_thigh[2]=m->bones[thigh].rotate_z;
            euler_knee[0]=m->bones[knee].rotate_x;euler_knee[1]=m->bones[knee].rotate_y;euler_knee[2]=m->bones[knee].rotate_z;
            euler_d[0]=m->bones[dbone].rotate_x;euler_d[1]=m->bones[dbone].rotate_y;euler_d[2]=m->bones[dbone].rotate_z;
            if(have){double x=inspect_position_delta(previous_target[side],target),y=inspect_rotation_delta(previous_thigh[side],euler_thigh),z=inspect_rotation_delta(previous_knee[side],euler_knee),q=inspect_rotation_delta(previous_d[side],euler_d),r=inspect_position_delta(previous_ankle[side],ankle_pos);if(x>max_target[side]){max_target[side]=x;max_target_time[side]=time;memcpy(peak_target_before[side],previous_target[side],sizeof(previous_target[side]));memcpy(peak_target_after[side],target,sizeof(target));}if(y>max_thigh[side]){max_thigh[side]=y;max_thigh_time[side]=time;memcpy(peak_thigh_before[side],previous_thigh[side],sizeof(previous_thigh[side]));memcpy(peak_thigh_after[side],euler_thigh,sizeof(euler_thigh));}if(z>max_knee[side]){max_knee[side]=z;max_knee_time[side]=time;memcpy(peak_knee_before[side],previous_knee[side],sizeof(previous_knee[side]));memcpy(peak_knee_after[side],euler_knee,sizeof(euler_knee));}if(q>max_d[side]){max_d[side]=q;max_d_time[side]=time;memcpy(peak_d_before[side],previous_d[side],sizeof(previous_d[side]));memcpy(peak_d_after[side],euler_d,sizeof(euler_d));}if(r>max_ankle[side]){max_ankle[side]=r;max_ankle_time[side]=time;memcpy(peak_ankle_before[side],previous_ankle[side],sizeof(previous_ankle[side]));memcpy(peak_ankle_after[side],ankle_pos,sizeof(ankle_pos));}}
            if(time<=1000)__printf("leg trace time=%dms side=%s target=(%.3f,%.3f,%.3f) thigh=(%d,%d,%d) knee=(%d,%d,%d) D=(%d,%d,%d) ankle=(%.3f,%.3f,%.3f) error=%.3f iterations=%d\n",time,side==0?"left":"right",target[0],target[1],target[2],euler_thigh[0],euler_thigh[1],euler_thigh[2],euler_knee[0],euler_knee[1],euler_knee[2],euler_d[0],euler_d[1],euler_d[2],ankle_pos[0],ankle_pos[1],ankle_pos[2],inspect_position_delta(target,ankle_pos),ik>=0?m->iks[ik].iterations:0);
            memcpy(previous_target[side],target,sizeof(target));memcpy(previous_ankle[side],ankle_pos,sizeof(ankle_pos));memcpy(previous_thigh[side],euler_thigh,sizeof(euler_thigh));memcpy(previous_knee[side],euler_knee,sizeof(euler_knee));memcpy(previous_d[side],euler_d,sizeof(euler_d));
        }
        have=1;
        if (time < v->duration_ms) {
            prepare_vmd_skeleton_translation(m,v,time+1000/30,0);
            rasterfall_model_sample_clip(m,clip,time+1000/30);
        }
    }
    for(side=0;side<2;side++)__printf("leg trace peaks side=%s target_delta=%.3f@%dms before=(%.3f,%.3f,%.3f) after=(%.3f,%.3f,%.3f) thigh_delta=%.3fdeg@%dms before=(%d,%d,%d) after=(%d,%d,%d) knee_delta=%.3fdeg@%dms before=(%d,%d,%d) after=(%d,%d,%d) D_delta=%.3fdeg@%dms before=(%d,%d,%d) after=(%d,%d,%d) ankle_delta=%.3f@%dms before=(%.3f,%.3f,%.3f) after=(%.3f,%.3f,%.3f)\n",side==0?"left":"right",max_target[side],max_target_time[side],peak_target_before[side][0],peak_target_before[side][1],peak_target_before[side][2],peak_target_after[side][0],peak_target_after[side][1],peak_target_after[side][2],max_thigh[side],max_thigh_time[side],peak_thigh_before[side][0],peak_thigh_before[side][1],peak_thigh_before[side][2],peak_thigh_after[side][0],peak_thigh_after[side][1],peak_thigh_after[side][2],max_knee[side],max_knee_time[side],peak_knee_before[side][0],peak_knee_before[side][1],peak_knee_before[side][2],peak_knee_after[side][0],peak_knee_after[side][1],peak_knee_after[side][2],max_d[side],max_d_time[side],peak_d_before[side][0],peak_d_before[side][1],peak_d_before[side][2],peak_d_after[side][0],peak_d_after[side][1],peak_d_after[side][2],max_ankle[side],max_ankle_time[side],peak_ankle_before[side][0],peak_ankle_before[side][1],peak_ankle_before[side][2],peak_ankle_after[side][0],peak_ankle_after[side][1],peak_ankle_after[side][2]);
    __printf("vmd leg trace conclusion: CCD starts from FK base each frame; early exit threshold=8 units; configured iterations=40\n");
}

static void inspect_knee_window(struct rasterfall_model_asset *m,
                                const struct rasterfall_vmd_clip *v,
                                const struct rasterfall_animation_clip *clip,
                                int center)
{
    static const char *controller_name[2] = {"左足ＩＫ","右足ＩＫ"};
    static const char *knee_name[2] = {"左ひざ","右ひざ"};
    static const char *ankle_name[2] = {"左足首","右足首"};
    int n, side;
    __printf("knee jump window center=%dms (five frames before/after)\n", center);
    for (n=-5; n<=5; n++) {
        int time = center + n * (1000/30);
        if (time < 0) time = 0;
        m->ik_iteration_trace_time_ms = n == 0 ? time : -1;
        prepare_vmd_skeleton_translation(m, v, time, 0);
        rasterfall_model_sample_clip(m, clip, time);
        for (side=0; side<2; side++) {
            int controller = rasterfall_model_find_bone(m, controller_name[side]);
            int knee = rasterfall_model_find_bone(m, knee_name[side]);
            int ankle = rasterfall_model_find_bone(m, ankle_name[side]);
            int t[3];
            double target[3], error;
            if (controller < 0 || knee < 0 || ankle < 0) continue;
            rasterfall_vmd_sample_bone_translation(v, controller_name[side], time, t);
            target[0] = m->bone_transforms[controller].position[0] + t[0];
            target[1] = m->bone_transforms[controller].position[1] + t[1];
            target[2] = m->bone_transforms[controller].position[2] + t[2];
            error = inspect_position_delta(target,
                                           m->bone_transforms[ankle].position);
            __printf("knee window time=%dms side=%s target=(%.3f,%.3f,%.3f) "
                     "ankle=(%.3f,%.3f,%.3f) error=%.3f "
                     "knee_euler=(%d,%d,%d) knee_q=",
                     time, side ? "right" : "left", target[0],target[1],target[2],
                     m->bone_transforms[ankle].position[0],
                     m->bone_transforms[ankle].position[1],
                     m->bone_transforms[ankle].position[2], error,
                     m->bones[knee].rotate_x,m->bones[knee].rotate_y,
                     m->bones[knee].rotate_z);
            {
                struct rasterfall_animation_quaternion q =
                    rasterfall_animation_quat_from_euler(m->bones[knee].rotate_x,
                                                         m->bones[knee].rotate_y,
                                                         m->bones[knee].rotate_z);
                __printf("(%.6f,%.6f,%.6f,%.6f)\n",q.x,q.y,q.z,q.w);
            }
        }
    }
    m->ik_iteration_trace_time_ms = -1;
}

static void inspect_continuity_ab(struct rasterfall_model_asset *m,
                                  const struct rasterfall_vmd_clip *v,
                                  const struct rasterfall_animation_clip *clip,
                                  int limits, int warm)
{
    struct rasterfall_animation_quaternion previous[2];
    double previous_ankle[2][3], sum_knee=0.0, sum_ankle=0.0;
    double max_knee=0.0, max_ankle=0.0;
    unsigned long samples=0;
    int have=0, side, time;
    /* The exact 33ms windows are traced separately.  Use the inspector's
     * existing coarse full-clip cadence for the aggregate A/B so this
     * diagnostic remains usable on the freestanding build. */
    int step = v->duration_ms > 100000 ? 250 : 1000/30;
    memset(m->ik_warm_start_valid, 0, sizeof(m->ik_warm_start_valid));
    memset(previous, 0, sizeof(previous));
    m->ik_limits_enabled = limits;
    m->ik_warm_start_diagnostic = warm;
    reset_ik_stats(m);
    for (time=0; time<=v->duration_ms; time+=step) {
        prepare_vmd_skeleton_translation(m, v, time, 0);
        rasterfall_model_sample_clip(m, clip, time);
        for (side=0; side<2; side++) {
            int knee = rasterfall_model_find_bone(m, side ? "右ひざ" : "左ひざ");
            int ankle = rasterfall_model_find_bone(m, side ? "右足首" : "左足首");
            struct rasterfall_animation_quaternion q;
            double d, p;
            if (knee < 0 || ankle < 0) continue;
            q = rasterfall_animation_quat_from_euler(m->bones[knee].rotate_x,
                                                     m->bones[knee].rotate_y,
                                                     m->bones[knee].rotate_z);
            if (have) {
                double dot = previous[side].x*q.x + previous[side].y*q.y +
                             previous[side].z*q.z + previous[side].w*q.w;
                if (dot < 0.0) dot = -dot;
                if (dot > 1.0) dot = 1.0;
                d = 2.0*atan2(sqrt(1.0-dot*dot),dot)*180.0/M_PI;
                p = inspect_position_delta(previous_ankle[side],
                                           m->bone_transforms[ankle].position);
                sum_knee += d; sum_ankle += p; samples++;
                if (d > max_knee) max_knee=d;
                if (p > max_ankle) max_ankle=p;
            }
            previous[side]=q;
            memcpy(previous_ankle[side],m->bone_transforms[ankle].position,
                   sizeof(previous_ankle[side]));
        }
        have=1;
    }
    __printf("knee continuity AB step=%dms limits=%s warm_start=%s max_knee_delta=%.3fdeg "
             "avg_knee_delta=%.3fdeg max_ankle_delta=%.3f avg_ankle_delta=%.3f "
             "avg_iterations=%.3f avg_error=%.3f\n",
             step, limits ? "on" : "off", warm ? "previous_frame" : "FK_base",
             max_knee, samples ? sum_knee/samples : 0.0, max_ankle,
             samples ? sum_ankle/samples : 0.0,
             m->ik_controller_sample_count ?
                 (double)m->ik_iteration_total/m->ik_controller_sample_count : 0.0,
             m->ik_controller_sample_count ?
                 m->ik_error_after_total/m->ik_controller_sample_count : 0.0);
    m->ik_warm_start_diagnostic = 0;
    m->ik_limits_enabled = 1;
}

static void inspect_solver_variant(struct rasterfall_model_asset *m,
                                   const struct rasterfall_vmd_clip *v,
                                   const struct rasterfall_animation_clip *clip,
                                   const char *label, int reverse,
                                   int knee_scale, int thigh_scale)
{
    m->ik_diagnostic_reverse_order = reverse;
    m->ik_diagnostic_knee_scale_milli = knee_scale;
    m->ik_diagnostic_thigh_scale_milli = thigh_scale;
    __printf("knee solver variant label=%s order=%s knee_scale=%.2f thigh_scale=%.2f\n",
             label, reverse ? "thigh->knee" : "knee->thigh",
             knee_scale/1000.0, thigh_scale/1000.0);
    inspect_continuity_ab(m, v, clip, 1, 0);
    m->ik_iteration_trace_time_ms = 216777;
    prepare_vmd_skeleton_translation(m, v, 216777, 0);
    rasterfall_model_sample_clip(m, clip, 216777);
    m->ik_iteration_trace_time_ms = 51216;
    prepare_vmd_skeleton_translation(m, v, 51216, 0);
    rasterfall_model_sample_clip(m, clip, 51216);
    m->ik_iteration_trace_time_ms = -1;
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
                 "[--vmd-disable-ik] [--vmd-disable-grant] "
                 "[--vmd-legacy-root-offset] "
                 "[--leg-static-rotation-test] [--vmd-leg-trace] "
                 "[--vmd-knee-diagnostic] [--vmd-legacy-leg-ccd] "
                 "[--vmd-handoff-only] [--vmd-ccd-motion-only] "
                 "[--vmd-knee-branch]\n");
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
        int disable = 0, disable_grant = 0, legacy = 0, static_leg_test = 0;
        int leg_trace = 0, knee_diagnostic = 0, legacy_knee = 0;
        int handoff_only = 0, ccd_motion_only = 0, knee_branch = 0;
        for (i = 3; i < argc; i++) {
            if (!strcmp(argv[i], "--vmd-disable-ik")) disable = 1;
            if (!strcmp(argv[i], "--vmd-disable-grant")) disable_grant = 1;
            if (!strcmp(argv[i], "--vmd-legacy-root-offset")) legacy = 1;
            if (!strcmp(argv[i], "--leg-static-rotation-test")) static_leg_test = 1;
            if (!strcmp(argv[i], "--vmd-leg-trace")) leg_trace = 1;
            if (!strcmp(argv[i], "--vmd-knee-diagnostic")) knee_diagnostic = 1;
            if (!strcmp(argv[i], "--vmd-legacy-knee-ccd") ||
                !strcmp(argv[i], "--vmd-legacy-leg-ccd")) legacy_knee = 1;
            if (!strcmp(argv[i], "--vmd-handoff-only")) handoff_only = 1;
            if (!strcmp(argv[i], "--vmd-ccd-motion-only")) ccd_motion_only = 1;
            if (!strcmp(argv[i], "--vmd-knee-branch")) knee_branch = 1;
        }
        if (disable) rasterfall_model_set_ik_enabled(&m, 0);
        if (disable_grant) rasterfall_model_set_grant_enabled(&m, 0);
        rasterfall_model_set_legacy_knee_ccd(&m, legacy_knee);
        if (static_leg_test) inspect_static_leg_rotation(&m);
        rasterfall_model_dump_ik(&m);
        rasterfall_model_dump_bones(&m, "D");
        rasterfall_model_dump_ik_hierarchy(&m);
        if (rasterfall_vmd_build_animation(&v, &clip, tracks,
                                           RASTERFALL_VMD_MAX_BONES) >= 0) {
            if (ccd_motion_only) {
                inspect_ccd_motion(&m, &v, &clip, 0);
                inspect_ccd_motion(&m, &v, &clip, 1);
                rasterfall_vmd_unload(&v);
                return 0;
            }
            if (knee_branch) {
                inspect_knee_branch_scan(&m, &v, &clip);
                rasterfall_vmd_unload(&v);
                return 0;
            }
            if (handoff_only) {
                inspect_solver_handoff(&m, &v, &clip, 0);
                inspect_solver_handoff(&m, &v, &clip, 1);
                inspect_ccd_motion(&m, &v, &clip, 0);
                inspect_ccd_motion(&m, &v, &clip, 1);
                inspect_continuity_ab(&m, &v, &clip, 1, 0);
                inspect_continuity_ab(&m, &v, &clip, 1, 1);
                rasterfall_vmd_unload(&v);
                return 0;
            }
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
                m.ik_limits_enabled = 1;
                rasterfall_model_set_ik_enabled(&m, 0);
                prepare_vmd_skeleton_translation(&m, &v, phase, legacy);
                rasterfall_model_sample_clip(&m, &clip, phase);
                inspect_leg_transforms(&m, "phase=25% IK OFF");
                rasterfall_model_set_ik_enabled(&m, 1);
                prepare_vmd_skeleton_translation(&m, &v, phase, legacy);
                rasterfall_model_sample_clip(&m, &clip, phase);
                inspect_leg_transforms(&m, "phase=25% IK ON");

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
            inspect_analytic_synthetic(&m, &v, &clip);
            inspect_analytic_pole_sweep(&m, &v, &clip);
            /* Pole sweep is diagnostic-only; never let its override leak into
             * the subsequent real-animation continuity scan. */
            m.ik_analytic_pole_override = 0;
            m.ik_synthetic_target = 0;
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
            inspect_analytic_thigh_jumps(&m, &v, &clip);
            inspect_leg_sequence(&m, &v, &clip, 0);
            inspect_leg_sequence(&m, &v, &clip, 1);
            inspect_solver_handoff(&m, &v, &clip, 0);
            m.ik_legacy_knee_ccd = legacy_knee;
            if (leg_trace) {
                m.ik_iteration_trace_time_ms = -1;
                inspect_vmd_leg_trace(&m, &v, &clip);
            }
            if (leg_trace || knee_diagnostic) {
                m.ik_iteration_trace_time_ms = -1;
                m.ik_diagnostic_reverse_order = 0;
                m.ik_diagnostic_knee_scale_milli = 1000;
                m.ik_diagnostic_thigh_scale_milli = 1000;
                inspect_knee_window(&m, &v, &clip,
                                    v.duration_ms > 216777 ? 216777 : v.duration_ms/2);
                inspect_knee_window(&m, &v, &clip,
                                    v.duration_ms > 51216 ? 51216 : v.duration_ms/4);
                inspect_solver_variant(&m, &v, &clip, "baseline", 0, 1000, 1000);
                inspect_solver_variant(&m, &v, &clip, "reverse-order", 1, 1000, 1000);
                inspect_solver_variant(&m, &v, &clip, "both-half", 0, 500, 500);
                inspect_solver_variant(&m, &v, &clip, "both-quarter", 0, 250, 250);
                inspect_solver_variant(&m, &v, &clip, "knee-half", 0, 500, 1000);
                inspect_solver_variant(&m, &v, &clip, "thigh-half", 0, 1000, 500);
                m.ik_diagnostic_reverse_order = 0;
                m.ik_diagnostic_knee_scale_milli = 1000;
                m.ik_diagnostic_thigh_scale_milli = 1000;
            }
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
