/* This trace deliberately sits after the real vertex-cache preparation.  The
 * RFM2 renderer has no separate GPU palette: model_transform_vertex consumes
 * bone_transforms directly, so this is the last state before triangles are
 * submitted. */
#define RASTERFALL_SKIN_TRACE_BONES 6
#define RASTERFALL_SKIN_TRACE_VERTICES 6
#define RASTERFALL_SKIN_TRACE_DETAIL_BONES 4
#define RASTERFALL_SKIN_TRACE_FRAMES 64
#define RASTERFALL_SKIN_TRACE_PENDING 16
struct rasterfall_skin_trace_frame {
    unsigned long serial;
    int time_ms;
    int bone_index[RASTERFALL_SKIN_TRACE_BONES];
    double bone_matrix[RASTERFALL_SKIN_TRACE_BONES][9];
    int vertex_index[RASTERFALL_SKIN_TRACE_VERTICES];
    int vertex_position[RASTERFALL_SKIN_TRACE_VERTICES][3];
    int detail_index[RASTERFALL_SKIN_TRACE_DETAIL_BONES];
    double detail_local_q[RASTERFALL_SKIN_TRACE_DETAIL_BONES][4];
    double detail_global_q[RASTERFALL_SKIN_TRACE_DETAIL_BONES][4];
    double detail_parent_q[RASTERFALL_SKIN_TRACE_DETAIL_BONES][4];
    int detail_grant_euler[RASTERFALL_SKIN_TRACE_DETAIL_BONES][3];
    int sampled_local[4][3];
    int pre_ik_local[4][3];
    int solver_local[4][3];
    int grant_local[4][3];
    int final_local[4][3];
    double solver_global[4][9];
    double grant_global[4][9];
    double final_global[4][9];
    int write_bone;
    int write_valid;
    double write_result_global_q[4];
    double write_local_before_q[4];
    double write_local_before_matrix[9];
    int write_euler[3];
    double write_final_q[4];
    int write_path;
    int write_valid_by_bone[4];
    int write_path_by_bone[4];
    double write_result_global_q_by_bone[4][4];
    double write_local_before_q_by_bone[4][4];
    double write_local_before_matrix_by_bone[4][9];
    int write_euler_by_bone[4][3];
    double write_final_q_by_bone[4][4];
    int previous_solver[2];
    int transition_active[2];
    int transition_remaining[2];
    int transition_duration[2];
    int transition_reason[2];
    double transition_source_q[2][4];
    double transition_target_q[2][4];
    int reconciliation_active[2];
    int reconciliation_used[2];
    int reconciliation_unavailable[2];
    double reconciliation_source_q[2][4];
    double reconciliation_target_q[2][4];
    double previous_final_thigh_q[2][4];
    int ik_solver[2];
    int ik_iterations[2];
    double ik_target[2][3];
    int ik_knee_x[2];
};
static struct rasterfall_skin_trace_frame skin_trace_frames[
    RASTERFALL_SKIN_TRACE_FRAMES];
static unsigned long skin_trace_serial;
static int skin_trace_have_previous;
static const struct rasterfall_model_asset *skin_trace_model;
static int skin_trace_bones[RASTERFALL_SKIN_TRACE_BONES];
static int skin_trace_vertices[RASTERFALL_SKIN_TRACE_VERTICES];
static unsigned long skin_trace_pending[RASTERFALL_SKIN_TRACE_PENDING];
static int skin_trace_pending_count;

static unsigned int skin_trace_u16(const unsigned char *p)
{
    return (unsigned int)p[0] | (unsigned int)p[1] << 8;
}

static double skin_trace_matrix_angle(const double *a, const double *b)
{
    double r[9], trace, c;
    int i, j, k;
    /* r = transpose(a) * b; matrices are the same row-major rotations used
     * by model_transform_vertex. */
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) {
        r[i * 3 + j] = 0.0;
        for (k = 0; k < 3; k++)
            r[i * 3 + j] += a[k * 3 + i] * b[k * 3 + j];
    }
    trace = r[0] + r[4] + r[8];
    c = (trace - 1.0) * 0.5;
    if (c < -1.0) c = -1.0;
    if (c > 1.0) c = 1.0;
    return atan2(sqrt(1.0 - c * c), c) * 180.0 /
           3.14159265358979323846;
}

static double skin_trace_vec_distance(const int *a, const int *b)
{
    double x = (double)a[0] - b[0];
    double y = (double)a[1] - b[1];
    double z = (double)a[2] - b[2];
    return sqrt(x * x + y * y + z * z);
}

static void skin_trace_matrix_quaternion(const double *m, double q[4])
{
    double t;
    if (m[0] + m[4] + m[8] > 0.0) {
        t = sqrt(m[0] + m[4] + m[8] + 1.0) * 2.0;
        q[3] = 0.25 * t;
        q[0] = (m[7] - m[5]) / t;
        q[1] = (m[2] - m[6]) / t;
        q[2] = (m[3] - m[1]) / t;
    } else if (m[0] > m[4] && m[0] > m[8]) {
        t = sqrt(1.0 + m[0] - m[4] - m[8]) * 2.0;
        q[3] = (m[7] - m[5]) / t;
        q[0] = 0.25 * t;
        q[1] = (m[1] + m[3]) / t;
        q[2] = (m[2] + m[6]) / t;
    } else if (m[4] > m[8]) {
        t = sqrt(1.0 + m[4] - m[0] - m[8]) * 2.0;
        q[3] = (m[2] - m[6]) / t;
        q[0] = (m[1] + m[3]) / t;
        q[1] = 0.25 * t;
        q[2] = (m[5] + m[7]) / t;
    } else {
        t = sqrt(1.0 + m[8] - m[0] - m[4]) * 2.0;
        q[3] = (m[3] - m[1]) / t;
        q[0] = (m[2] + m[6]) / t;
        q[1] = (m[5] + m[7]) / t;
        q[2] = 0.25 * t;
    }
}

static void skin_trace_detail_pose(const struct rasterfall_model_asset *model,
                                   int bone_index, double local_q[4],
                                   double global_q[4], double parent_q[4],
                                   int euler[3])
{
    double local[9], parent_inverse[9];
    int parent = bone_index >= 0 ? model->bones[bone_index].parent : -1;
    if (bone_index < 0 || (unsigned)bone_index >= model->bone_count) {
        memset(local_q, 0, 4 * sizeof(double));
        memset(global_q, 0, 4 * sizeof(double));
        memset(parent_q, 0, 4 * sizeof(double));
        memset(euler, 0, 3 * sizeof(int));
        return;
    }
    skin_trace_matrix_quaternion(model->bone_transforms[bone_index].rotation,
                                 global_q);
    if (parent >= 0) {
        skin_trace_matrix_quaternion(model->bone_transforms[parent].rotation,
                                     parent_q);
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
            parent_inverse[i * 3 + j] =
                model->bone_transforms[parent].rotation[j * 3 + i];
        }
        /* local = transpose(parent_global) * global */
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) {
            local[i * 3 + j] = 0.0;
            for (int k = 0; k < 3; k++)
                local[i * 3 + j] += parent_inverse[i * 3 + k] *
                    model->bone_transforms[bone_index].rotation[k * 3 + j];
        }
    } else {
        memcpy(local, model->bone_transforms[bone_index].rotation,
               sizeof(local));
        memcpy(parent_q, (double[4]){0.0, 0.0, 0.0, 1.0},
               4 * sizeof(double));
    }
    skin_trace_matrix_quaternion(local, local_q);
    euler[0] = model->bones[bone_index].rotate_x;
    euler[1] = model->bones[bone_index].rotate_y;
    euler[2] = model->bones[bone_index].rotate_z;
}

static void skin_trace_find_layout(const struct rasterfall_model_asset *model)
{
    static const char *names[RASTERFALL_SKIN_TRACE_BONES] = {
        "左足", "左ひざ", "左足D", "右足", "右ひざ", "右足D"
    };
    int i, v;
    skin_trace_model = model;
    for (i = 0; i < RASTERFALL_SKIN_TRACE_BONES; i++)
        skin_trace_bones[i] = rasterfall_model_find_bone(model, names[i]);
    for (i = 0; i < RASTERFALL_SKIN_TRACE_VERTICES; i++)
        skin_trace_vertices[i] = -1;
    if (!model->skin_vertices) return;
    for (v = 0; v < (int)model->vertex_count; v++) {
        const unsigned char *skin = model->skin_vertices +
            v * RASTERFALL_MODEL_SKIN_VERTEX_BYTES;
        unsigned int b0 = skin_trace_u16(skin), b1 = skin_trace_u16(skin + 2);
        for (i = 0; i < RASTERFALL_SKIN_TRACE_BONES; i++)
            if (skin_trace_vertices[i] < 0 &&
                (b0 == (unsigned int)skin_trace_bones[i] ||
                 b1 == (unsigned int)skin_trace_bones[i]))
                skin_trace_vertices[i] = v;
    }
}

static void skin_trace_print_frame(const struct rasterfall_model_asset *model,
                                   const struct rasterfall_skin_trace_frame *f,
                                   const struct rasterfall_skin_trace_frame *p)
{
    int i, side;
    double matrix_jump = 0.0, vertex_jump = 0.0;
    int matrix_slot = -1, vertex_slot = -1;
    if (p) {
        for (i = 0; i < RASTERFALL_SKIN_TRACE_BONES; i++) {
            double d = skin_trace_matrix_angle(
                p->bone_matrix[i], f->bone_matrix[i]);
            if (d > matrix_jump) { matrix_jump = d; matrix_slot = i; }
        }
        for (i = 0; i < RASTERFALL_SKIN_TRACE_VERTICES; i++) {
            double d;
            if (f->vertex_index[i] < 0 || p->vertex_index[i] < 0) continue;
            d = skin_trace_vec_distance(p->vertex_position[i],
                                        f->vertex_position[i]);
            if (d > vertex_jump) { vertex_jump = d; vertex_slot = i; }
        }
    }
    __printf("rasterfall skin-trace frame=%lu time=%d matrix_jump=%.3f bone_slot=%d vertex_jump=%.3f vertex_slot=%d\n",
             f->serial, f->time_ms, matrix_jump, matrix_slot, vertex_jump,
             vertex_slot);
    for (i = 0; i < RASTERFALL_SKIN_TRACE_DETAIL_BONES; i++) {
        int b = f->detail_index[i];
        side = i < 2 ? 0 : 1;
        __printf("  palette-detail bone=%d name=\"%s\" parent=%d local_q=(%.6f %.6f %.6f %.6f) global_q=(%.6f %.6f %.6f %.6f) parent_q=(%.6f %.6f %.6f %.6f) grant_local_euler=(%d,%d,%d) IK_result=%s\n",
                 b, b >= 0 ? model->bones[b].name : "<missing>",
                 b >= 0 ? model->bones[b].parent : -1,
                 f->detail_local_q[i][0], f->detail_local_q[i][1],
                 f->detail_local_q[i][2], f->detail_local_q[i][3],
                 f->detail_global_q[i][0], f->detail_global_q[i][1],
                 f->detail_global_q[i][2], f->detail_global_q[i][3],
                 f->detail_parent_q[i][0], f->detail_parent_q[i][1],
                 f->detail_parent_q[i][2], f->detail_parent_q[i][3],
                 f->detail_grant_euler[i][0], f->detail_grant_euler[i][1],
                 f->detail_grant_euler[i][2],
                 (i == 0 || i == 2) ? "thigh-global/local" : "knee-global/local");
        if (b >= 0 && (i == 0 || i == 2)) {
            __printf("    solver-return thigh local=(%d,%d,%d) global_q=(%.6f %.6f %.6f %.6f)\n",
                     model->ik_solver_return_thigh[side][0],
                     model->ik_solver_return_thigh[side][1],
                     model->ik_solver_return_thigh[side][2],
                     model->ik_solver_return_thigh_global_q[side][0],
                     model->ik_solver_return_thigh_global_q[side][1],
                     model->ik_solver_return_thigh_global_q[side][2],
                     model->ik_solver_return_thigh_global_q[side][3]);
        } else if (b >= 0) {
            __printf("    solver-return knee local_x=%d global_q=(%.6f %.6f %.6f %.6f)\n",
                     model->ik_solver_return_knee[side],
                     model->ik_solver_return_knee_global_q[side][0],
                     model->ik_solver_return_knee_global_q[side][1],
                     model->ik_solver_return_knee_global_q[side][2],
                     model->ik_solver_return_knee_global_q[side][3]);
        }
    }
    for (i = 0; i < 4; i++) {
        __printf("  stage bone=%d name=\"%s\" sampled=(%d,%d,%d) pre_ik=(%d,%d,%d) solver=(%d,%d,%d) grant=(%d,%d,%d) final=(%d,%d,%d)\n",
                 f->detail_index[i],
                 f->detail_index[i] >= 0 ? model->bones[f->detail_index[i]].name : "<missing>",
                 f->sampled_local[i][0], f->sampled_local[i][1], f->sampled_local[i][2],
                 f->pre_ik_local[i][0], f->pre_ik_local[i][1], f->pre_ik_local[i][2],
                 f->solver_local[i][0], f->solver_local[i][1], f->solver_local[i][2],
                 f->grant_local[i][0], f->grant_local[i][1], f->grant_local[i][2],
                 f->final_local[i][0], f->final_local[i][1], f->final_local[i][2]);
        __printf("    global solver=(%.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f) grant=(%.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f) final=(%.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f)\n",
                 f->solver_global[i][0], f->solver_global[i][1], f->solver_global[i][2],
                 f->solver_global[i][3], f->solver_global[i][4], f->solver_global[i][5],
                 f->solver_global[i][6], f->solver_global[i][7], f->solver_global[i][8],
                 f->grant_global[i][0], f->grant_global[i][1], f->grant_global[i][2],
                 f->grant_global[i][3], f->grant_global[i][4], f->grant_global[i][5],
                 f->grant_global[i][6], f->grant_global[i][7], f->grant_global[i][8],
                 f->final_global[i][0], f->final_global[i][1], f->final_global[i][2],
                 f->final_global[i][3], f->final_global[i][4], f->final_global[i][5],
                 f->final_global[i][6], f->final_global[i][7], f->final_global[i][8]);
    }
    for (i = 0; i < 2; i++) {
        __printf("  ik side=%s solver=%d iterations=%d target=(%.3f,%.3f,%.3f) knee_local_x=%d constraint=[-180,-0.5]\n",
                 i ? "right" : "left", f->ik_solver[i], f->ik_iterations[i],
                 f->ik_target[i][0], f->ik_target[i][1], f->ik_target[i][2],
                 f->ik_knee_x[i]);
        __printf("  policy side=%s previous_solver=%d transition=%d remaining=%d/%d reason=%d prev_final_thigh_q=(%.6f %.6f %.6f %.6f) transition_source_q=(%.6f %.6f %.6f %.6f) transition_target_q=(%.6f %.6f %.6f %.6f) reconciliation=%d used=%d unavailable=%d source_q=(%.6f %.6f %.6f %.6f) target_q=(%.6f %.6f %.6f %.6f)\n",
                 i ? "right" : "left", f->previous_solver[i],
                 f->transition_active[i], f->transition_remaining[i],
                 f->transition_duration[i], f->transition_reason[i],
                 f->previous_final_thigh_q[i][0], f->previous_final_thigh_q[i][1],
                 f->previous_final_thigh_q[i][2], f->previous_final_thigh_q[i][3],
                 f->transition_source_q[i][0], f->transition_source_q[i][1],
                 f->transition_source_q[i][2], f->transition_source_q[i][3],
                 f->transition_target_q[i][0], f->transition_target_q[i][1],
                 f->transition_target_q[i][2], f->transition_target_q[i][3],
                 f->reconciliation_active[i], f->reconciliation_used[i],
                 f->reconciliation_unavailable[i],
                 f->reconciliation_source_q[i][0], f->reconciliation_source_q[i][1],
                 f->reconciliation_source_q[i][2], f->reconciliation_source_q[i][3],
                 f->reconciliation_target_q[i][0], f->reconciliation_target_q[i][1],
                 f->reconciliation_target_q[i][2], f->reconciliation_target_q[i][3]);
    }
    if (f->write_valid) {
        __printf("  write-trace bone=%d path=%d time=%d IK_RESULT_GLOBAL q=(%.6f %.6f %.6f %.6f) LOCAL_BEFORE_WRITE q=(%.6f %.6f %.6f %.6f) matrix=(%.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f) EULER_CONVERT xyz=(%d,%d,%d) FINAL_BONE q=(%.6f %.6f %.6f %.6f)\n",
                 f->write_bone, f->write_path, f->time_ms,
                 f->write_result_global_q[0], f->write_result_global_q[1],
                 f->write_result_global_q[2], f->write_result_global_q[3],
                 f->write_local_before_q[0], f->write_local_before_q[1],
                 f->write_local_before_q[2], f->write_local_before_q[3],
                 f->write_local_before_matrix[0], f->write_local_before_matrix[1],
                 f->write_local_before_matrix[2], f->write_local_before_matrix[3],
                 f->write_local_before_matrix[4], f->write_local_before_matrix[5],
                 f->write_local_before_matrix[6], f->write_local_before_matrix[7],
                 f->write_local_before_matrix[8],
                 f->write_euler[0], f->write_euler[1], f->write_euler[2],
                 f->write_final_q[0], f->write_final_q[1],
                 f->write_final_q[2], f->write_final_q[3]);
    }
    for (i = 0; i < 4; i++) if (f->write_valid_by_bone[i]) {
        __printf("  write-by-bone bone=%d path=%d time=%d IK_RESULT_GLOBAL q=(%.6f %.6f %.6f %.6f) LOCAL_BEFORE_WRITE q=(%.6f %.6f %.6f %.6f) matrix=(%.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f) EULER_CONVERT xyz=(%d,%d,%d) FINAL_BONE q=(%.6f %.6f %.6f %.6f)\n",
                 f->detail_index[i], f->write_path_by_bone[i], f->time_ms,
                 f->write_result_global_q_by_bone[i][0], f->write_result_global_q_by_bone[i][1],
                 f->write_result_global_q_by_bone[i][2], f->write_result_global_q_by_bone[i][3],
                 f->write_local_before_q_by_bone[i][0], f->write_local_before_q_by_bone[i][1],
                 f->write_local_before_q_by_bone[i][2], f->write_local_before_q_by_bone[i][3],
                 f->write_local_before_matrix_by_bone[i][0], f->write_local_before_matrix_by_bone[i][1],
                 f->write_local_before_matrix_by_bone[i][2], f->write_local_before_matrix_by_bone[i][3],
                 f->write_local_before_matrix_by_bone[i][4], f->write_local_before_matrix_by_bone[i][5],
                 f->write_local_before_matrix_by_bone[i][6], f->write_local_before_matrix_by_bone[i][7],
                 f->write_local_before_matrix_by_bone[i][8], f->write_euler_by_bone[i][0],
                 f->write_euler_by_bone[i][1], f->write_euler_by_bone[i][2],
                 f->write_final_q_by_bone[i][0], f->write_final_q_by_bone[i][1],
                 f->write_final_q_by_bone[i][2], f->write_final_q_by_bone[i][3]);
    }
    for (i = 0; i < RASTERFALL_SKIN_TRACE_VERTICES; i++) {
        const unsigned char *s;
        if (f->vertex_index[i] < 0) continue;
        s = model->skin_vertices + f->vertex_index[i] *
            RASTERFALL_MODEL_SKIN_VERTEX_BYTES;
        __printf("  vertex slot=%d index=%d weights=%u,%u/%u type=%u pos=(%d,%d,%d)\n",
                 i, f->vertex_index[i], skin_trace_u16(s), skin_trace_u16(s + 2),
                 skin_trace_u16(s + 4), s[6], f->vertex_position[i][0],
                 f->vertex_position[i][1], f->vertex_position[i][2]);
    }
    if (p && matrix_slot >= 0) {
        __printf("  palette bone slot=%d index=%d prev=(%.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f) current=(%.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f)\n",
                 matrix_slot, f->bone_index[matrix_slot],
                 p->bone_matrix[matrix_slot][0], p->bone_matrix[matrix_slot][1],
                 p->bone_matrix[matrix_slot][2], p->bone_matrix[matrix_slot][3],
                 p->bone_matrix[matrix_slot][4], p->bone_matrix[matrix_slot][5],
                 p->bone_matrix[matrix_slot][6], p->bone_matrix[matrix_slot][7],
                 p->bone_matrix[matrix_slot][8], f->bone_matrix[matrix_slot][0],
                 f->bone_matrix[matrix_slot][1], f->bone_matrix[matrix_slot][2],
                 f->bone_matrix[matrix_slot][3], f->bone_matrix[matrix_slot][4],
                 f->bone_matrix[matrix_slot][5], f->bone_matrix[matrix_slot][6],
                 f->bone_matrix[matrix_slot][7], f->bone_matrix[matrix_slot][8]);
    }
}

static void skin_trace_print_window(const struct rasterfall_model_asset *model,
                                    unsigned long event_serial)
{
    long serial;
    __printf("rasterfall skin-trace window event=%lu before_after=10\n",
             event_serial);
    for (serial = (long)event_serial - 10;
         serial <= (long)event_serial + 10; serial++) {
        struct rasterfall_skin_trace_frame *f;
        struct rasterfall_skin_trace_frame *p = 0;
        if (serial < 0 || (unsigned long)serial >= skin_trace_serial) continue;
        f = &skin_trace_frames[(unsigned long)serial %
                               RASTERFALL_SKIN_TRACE_FRAMES];
        if (serial > 0)
            p = &skin_trace_frames[(unsigned long)(serial - 1) %
                                   RASTERFALL_SKIN_TRACE_FRAMES];
        skin_trace_print_frame(model, f, p);
    }
}

static void skin_trace_capture(struct toy_renderer *renderer,
                               const struct rasterfall_model_asset *model)
{
    struct rasterfall_skin_trace_frame *f, *p = 0;
    int i, time_ms = 0;
    double max_matrix_jump = 0.0, max_vertex_jump = 0.0;
    if (!private_character_vmd_skin_trace || model != &private_character_model ||
        !model->bone_transforms || !model->skin_vertices) return;
    if (skin_trace_model != model)
        skin_trace_find_layout(model);
    if (active_session)
        time_ms = active_session->skeletal_demo_player.time_ms;
    f = &skin_trace_frames[skin_trace_serial % RASTERFALL_SKIN_TRACE_FRAMES];
    memset(f, 0, sizeof(*f));
    f->serial = skin_trace_serial;
    f->time_ms = time_ms;
    for (i = 0; i < RASTERFALL_SKIN_TRACE_BONES; i++) {
        int b = skin_trace_bones[i];
        f->bone_index[i] = b;
        if (b >= 0 && (unsigned)b < model->bone_count)
            memcpy(f->bone_matrix[i], model->bone_transforms[b].rotation,
                   sizeof(f->bone_matrix[i]));
    }
    {
        int detail_names[RASTERFALL_SKIN_TRACE_DETAIL_BONES];
        detail_names[0] = rasterfall_model_find_bone(model, "左足");
        detail_names[1] = rasterfall_model_find_bone(model, "左ひざ");
        detail_names[2] = rasterfall_model_find_bone(model, "右足");
        detail_names[3] = rasterfall_model_find_bone(model, "右ひざ");
        for (i = 0; i < RASTERFALL_SKIN_TRACE_DETAIL_BONES; i++) {
            f->detail_index[i] = detail_names[i];
            skin_trace_detail_pose(model, detail_names[i],
                                   f->detail_local_q[i],
                                   f->detail_global_q[i],
                                   f->detail_parent_q[i],
                                   f->detail_grant_euler[i]);
        }
    }
    if (model->render_trace_valid) {
        memcpy(f->sampled_local, model->render_trace_sampled_local,
               sizeof(f->sampled_local));
        memcpy(f->pre_ik_local, model->render_trace_pre_ik_local,
               sizeof(f->pre_ik_local));
        memcpy(f->solver_local, model->render_trace_solver_local,
               sizeof(f->solver_local));
        memcpy(f->grant_local, model->render_trace_grant_local,
               sizeof(f->grant_local));
        memcpy(f->final_local, model->render_trace_final_local,
               sizeof(f->final_local));
        memcpy(f->solver_global, model->render_trace_solver_global,
               sizeof(f->solver_global));
        memcpy(f->grant_global, model->render_trace_grant_global,
               sizeof(f->grant_global));
        memcpy(f->final_global, model->render_trace_final_global,
               sizeof(f->final_global));
    }
    for (i = 0; i < 2; i++) {
        f->ik_solver[i] = model->ik_last_leg_solver[i];
        f->ik_target[i][0] = model->ik_ccd_diag_target[i][0];
        f->ik_target[i][1] = model->ik_ccd_diag_target[i][1];
        f->ik_target[i][2] = model->ik_ccd_diag_target[i][2];
        f->ik_iterations[i] = 0;
        f->ik_knee_x[i] = model->ik_solver_return_knee[i];
    }
    f->write_bone = model->render_trace_write_bone;
    f->write_valid = model->render_trace_write_valid;
    memcpy(f->write_result_global_q, model->render_trace_ik_result_global_q,
           sizeof(f->write_result_global_q));
    memcpy(f->write_local_before_q, model->render_trace_local_before_write_q,
           sizeof(f->write_local_before_q));
    memcpy(f->write_local_before_matrix,
           model->render_trace_local_before_write_matrix,
           sizeof(f->write_local_before_matrix));
    memcpy(f->write_euler, model->render_trace_euler_convert,
           sizeof(f->write_euler));
    memcpy(f->write_final_q, model->render_trace_final_bone_q,
           sizeof(f->write_final_q));
    f->write_path = model->render_trace_write_path;
    memcpy(f->write_valid_by_bone, model->render_trace_write_valid_by_bone,
           sizeof(f->write_valid_by_bone));
    memcpy(f->write_path_by_bone, model->render_trace_write_path_by_bone,
           sizeof(f->write_path_by_bone));
    memcpy(f->write_result_global_q_by_bone,
           model->render_trace_ik_result_global_q_by_bone,
           sizeof(f->write_result_global_q_by_bone));
    memcpy(f->write_local_before_q_by_bone,
           model->render_trace_local_before_write_q_by_bone,
           sizeof(f->write_local_before_q_by_bone));
    memcpy(f->write_local_before_matrix_by_bone,
           model->render_trace_local_before_write_matrix_by_bone,
           sizeof(f->write_local_before_matrix_by_bone));
    memcpy(f->write_euler_by_bone, model->render_trace_euler_convert_by_bone,
           sizeof(f->write_euler_by_bone));
    memcpy(f->write_final_q_by_bone,
           model->render_trace_final_bone_q_by_bone,
           sizeof(f->write_final_q_by_bone));
    memcpy(f->previous_solver, model->render_trace_previous_solver,
           sizeof(f->previous_solver));
    memcpy(f->transition_active, model->render_trace_transition_active,
           sizeof(f->transition_active));
    memcpy(f->transition_remaining, model->render_trace_transition_remaining,
           sizeof(f->transition_remaining));
    memcpy(f->transition_duration, model->render_trace_transition_duration,
           sizeof(f->transition_duration));
    memcpy(f->transition_reason, model->render_trace_transition_reason,
           sizeof(f->transition_reason));
    memcpy(f->transition_source_q, model->render_trace_transition_source_q,
           sizeof(f->transition_source_q));
    memcpy(f->transition_target_q, model->render_trace_transition_target_q,
           sizeof(f->transition_target_q));
    memcpy(f->reconciliation_active, model->render_trace_reconciliation_active,
           sizeof(f->reconciliation_active));
    memcpy(f->reconciliation_used, model->render_trace_reconciliation_used,
           sizeof(f->reconciliation_used));
    memcpy(f->reconciliation_unavailable, model->render_trace_reconciliation_unavailable,
           sizeof(f->reconciliation_unavailable));
    memcpy(f->reconciliation_source_q, model->render_trace_reconciliation_source_q,
           sizeof(f->reconciliation_source_q));
    memcpy(f->reconciliation_target_q, model->render_trace_reconciliation_target_q,
           sizeof(f->reconciliation_target_q));
    memcpy(f->previous_final_thigh_q, model->render_trace_previous_final_thigh_q,
           sizeof(f->previous_final_thigh_q));
    for (i = 0; i < (int)model->ik_count; i++) {
        int side = -1;
        if (model->iks[i].controller < 0 ||
            model->iks[i].controller >= (int)model->bone_count) continue;
        if (!strcmp(model->bones[model->iks[i].controller].name, "左足ＩＫ")) side = 0;
        if (!strcmp(model->bones[model->iks[i].controller].name, "右足ＩＫ")) side = 1;
        if (side >= 0) f->ik_iterations[side] = model->iks[i].iterations;
    }
    for (i = 0; i < RASTERFALL_SKIN_TRACE_VERTICES; i++) {
        f->vertex_index[i] = skin_trace_vertices[i];
        if (f->vertex_index[i] >= 0)
            memcpy(f->vertex_position[i],
                   gallery_vertex_cache[f->vertex_index[i]].model_position,
                   sizeof(f->vertex_position[i]));
    }
    if (skin_trace_have_previous) {
        p = &skin_trace_frames[(skin_trace_serial - 1) %
                               RASTERFALL_SKIN_TRACE_FRAMES];
        for (i = 0; i < RASTERFALL_SKIN_TRACE_BONES; i++) {
            double d = skin_trace_matrix_angle(p->bone_matrix[i],
                                                f->bone_matrix[i]);
            if (d > max_matrix_jump) max_matrix_jump = d;
        }
        for (i = 0; i < RASTERFALL_SKIN_TRACE_VERTICES; i++) {
            double d;
            if (f->vertex_index[i] < 0 || p->vertex_index[i] < 0) continue;
            d = skin_trace_vec_distance(p->vertex_position[i],
                                        f->vertex_position[i]);
            if (d > max_vertex_jump) max_vertex_jump = d;
        }
    }
    /* A palette rotation over 90 degrees or a 300-unit skinned jump is an
     * event.  Defer printing until ten later render frames exist. */
    if (skin_trace_have_previous &&
        (max_matrix_jump > 90.0 || max_vertex_jump > 300.0) &&
        skin_trace_pending_count < RASTERFALL_SKIN_TRACE_PENDING)
        skin_trace_pending[skin_trace_pending_count++] = skin_trace_serial;
    skin_trace_serial++;
    skin_trace_have_previous = 1;
    for (i = 0; i < skin_trace_pending_count; ) {
        if (skin_trace_serial - skin_trace_pending[i] >= 10) {
            skin_trace_print_window(model, skin_trace_pending[i]);
            skin_trace_pending[i] = skin_trace_pending[--skin_trace_pending_count];
        } else i++;
    }
}
