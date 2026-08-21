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
            if(len<0.000001)continue;bind[0]/=len;bind[1]/=len;bind[2]/=len;
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

static double inspect_rotation_delta(const int a[3], const int b[3])
{
    struct rasterfall_animation_quaternion qa = rasterfall_animation_quat_from_euler(a[0],a[1],a[2]);
    struct rasterfall_animation_quaternion qb = rasterfall_animation_quat_from_euler(b[0],b[1],b[2]);
    double dot = qa.x*qb.x + qa.y*qb.y + qa.z*qb.z + qa.w*qb.w;
    if (dot < 0.0) dot = -dot;
    if (dot > 1.0) dot = 1.0;
    return 2.0 * atan2(sqrt(1.0-dot*dot), dot) * 180.0 / M_PI;
}

static double inspect_position_delta(const double a[3], const double b[3])
{
    double x=a[0]-b[0], y=a[1]-b[1], z=a[2]-b[2];
    return sqrt(x*x+y*y+z*z);
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
                 "[--vmd-knee-diagnostic] [--vmd-legacy-leg-ccd]\n");
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
        for (i = 3; i < argc; i++) {
            if (!strcmp(argv[i], "--vmd-disable-ik")) disable = 1;
            if (!strcmp(argv[i], "--vmd-disable-grant")) disable_grant = 1;
            if (!strcmp(argv[i], "--vmd-legacy-root-offset")) legacy = 1;
            if (!strcmp(argv[i], "--leg-static-rotation-test")) static_leg_test = 1;
            if (!strcmp(argv[i], "--vmd-leg-trace")) leg_trace = 1;
            if (!strcmp(argv[i], "--vmd-knee-diagnostic")) knee_diagnostic = 1;
            if (!strcmp(argv[i], "--vmd-legacy-knee-ccd") ||
                !strcmp(argv[i], "--vmd-legacy-leg-ccd")) legacy_knee = 1;
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
