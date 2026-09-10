#include "core.h"
#include "string.h"
#include "tlibc_everything.h"
#include "toy_assets.h"
#include "rasterfall_action.h"
#include "rasterfall_model.h"
#include "rasterfall_calibration.h"
#include "math.h"

static const char *const action_names[] = {
    "NONE", "LOCOMOTION_IDLE", "LOCOMOTION_WALK", "RIFLE_IDLE",
    "RIFLE_AIM", "RIFLE_FIRE", "RIFLE_RECOIL"
};
static const char *const layer_names[] = {"LOWER_BODY", "UPPER_BODY", "ADDITIVE"};
static const char *const role_names[] = {
    "root", "hips", "spine", "chest", "upper_chest", "neck", "head",
    "left_shoulder", "left_upper_arm", "left_forearm", "left_hand",
    "right_shoulder", "right_upper_arm", "right_forearm", "right_hand",
    "left_upper_leg", "left_lower_leg", "left_foot",
    "right_upper_leg", "right_lower_leg", "right_foot"
};

const char *rasterfall_action_id_name(enum rasterfall_action_id id)
{
    return id >= 0 && id < RASTERFALL_ACTION_COUNT ? action_names[id] : "INVALID";
}

const char *rasterfall_action_layer_name(enum rasterfall_action_layer_id layer)
{
    return layer >= 0 && layer < RASTERFALL_ACTION_LAYER_COUNT ?
        layer_names[layer] : "INVALID";
}

static int parse_uint(const char **text, int *out)
{
    int value = 0;
    const char *p = *text;
    while (*p == ' ' || *p == '\t') p++;
    if (*p < '0' || *p > '9') return -1;
    while (*p >= '0' && *p <= '9') {
        if (value > 1000000) return -1;
        value = value * 10 + (*p++ - '0');
    }
    *text = p; *out = value;
    return 0;
}

static int parse_int(const char **text, int *out)
{
    int sign = 1, value;
    while (**text == ' ' || **text == '\t') (*text)++;
    if (**text == '-') { sign = -1; (*text)++; }
    if (parse_uint(text, &value) < 0) return -1;
    *out = value * sign;
    return 0;
}

static int token(const char **text, char *out, int capacity)
{
    int n = 0;
    while (**text == ' ' || **text == '\t') (*text)++;
    while (**text && **text != ' ' && **text != '\t' && **text != '\r' &&
           **text != '\n') {
        if (n + 1 >= capacity) return -1;
        out[n++] = *(*text)++;
    }
    out[n] = 0;
    return n ? 0 : -1;
}

static int role_from_name(const char *name)
{
    int i;
    for (i = 0; i < RASTERFALL_HUMANOID_BONE_COUNT; i++)
        if (!strcmp(name, role_names[i])) return i;
    return -1;
}

static int parse_line(struct rasterfall_action_clip *clip, const char *line,
                      struct rasterfall_action_track **active)
{
    char command[32], value[RASTERFALL_ACTION_SKELETON_BYTES];
    const char *p = line;
    int a, b, c, d, role;
    if (token(&p, command, sizeof(command)) < 0 || command[0] == '#') return 0;
    if (!strcmp(command, "RFANIM")) {
        return parse_uint(&p, &a) < 0 || a != RASTERFALL_ACTION_FORMAT_VERSION ? -1 : 0;
    } else if (!strcmp(command, "action")) {
        if (token(&p, clip->name, sizeof(clip->name)) < 0) return -1;
        for (a = 1; a < RASTERFALL_ACTION_COUNT; a++)
            if (!strcmp(clip->name, action_names[a])) clip->id = a;
        return clip->id == RASTERFALL_ACTION_NONE ? -1 : 0;
    } else if (!strcmp(command, "layer")) {
        if (token(&p, value, sizeof(value)) < 0) return -1;
        for (a = 0; a < RASTERFALL_ACTION_LAYER_COUNT; a++)
            if (!strcmp(value, layer_names[a])) clip->layer = a;
        return clip->layer < 0 ? -1 : 0;
    } else if (!strcmp(command, "skeleton")) {
        return token(&p, clip->skeleton, sizeof(clip->skeleton));
    } else if (!strcmp(command, "duration")) {
        return parse_uint(&p, &clip->duration_ms);
    } else if (!strcmp(command, "loop")) {
        if (parse_uint(&p, &clip->loop) < 0 || clip->loop > 1) return -1;
        return 0;
    } else if (!strcmp(command, "track")) {
        if (*active || clip->track_count >= RASTERFALL_ACTION_MAX_TRACKS ||
            token(&p, value, sizeof(value)) < 0) return -1;
        role = role_from_name(value);
        if (role < 0 || token(&p, value, sizeof(value)) < 0) return -1;
        *active = &clip->tracks[clip->track_count++];
        (*active)->target = role;
        (*active)->interpolation = !strcmp(value, "linear") ?
            RASTERFALL_ACTION_LINEAR : !strcmp(value, "step") ?
            RASTERFALL_ACTION_STEP : -1;
        (*active)->first_key = clip->key_count;
        return (*active)->interpolation < 0 ? -1 : 0;
    } else if (!strcmp(command, "key")) {
        struct rasterfall_animation_keyframe *key;
        if (!*active || clip->key_count >= RASTERFALL_ACTION_MAX_KEYS ||
            parse_uint(&p, &a) < 0 || parse_int(&p, &b) < 0 ||
            parse_int(&p, &c) < 0 || parse_int(&p, &d) < 0) return -1;
        key = &clip->keys[clip->key_count++];
        key->time_ms = a;
        key->rotation = rasterfall_animation_quat_from_euler(b, c, d);
        key->tx = key->ty = key->tz = 0.0f;
        (*active)->key_count++;
        return 0;
    } else if (!strcmp(command, "endtrack")) {
        if (!*active || !(*active)->key_count) return -1;
        *active = NULL; return 0;
    }
    return -1;
}

int rasterfall_action_validate(const struct rasterfall_action_clip *clip)
{
    unsigned int i, k;
    if (!clip || clip->id <= RASTERFALL_ACTION_NONE || !clip->name[0] ||
        strcmp(clip->name, rasterfall_action_id_name(clip->id)) ||
        strcmp(clip->skeleton, "RF_HUMANOID_V1") || clip->duration_ms <= 0 ||
        !clip->track_count || !clip->key_count || clip->layer < 0 ||
        clip->layer >= RASTERFALL_ACTION_LAYER_COUNT) return -1;
    for (i = 0; i < clip->track_count; i++) {
        const struct rasterfall_action_track *t = &clip->tracks[i];
        if (t->target < 0 || t->target >= RASTERFALL_HUMANOID_BONE_COUNT ||
            !t->key_count || t->first_key + t->key_count > clip->key_count) return -1;
        for (k = 0; k < t->key_count; k++) {
            const struct rasterfall_animation_keyframe *key = &clip->keys[t->first_key + k];
            if (key->time_ms < 0 || key->time_ms > clip->duration_ms ||
                (k && key[-1].time_ms >= key->time_ms)) return -1;
        }
    }
    return 0;
}

int rasterfall_action_load(struct rasterfall_action_clip *clip, const char *path)
{
    unsigned char *data;
    uint32_t size;
    char *p, *end, saved;
    struct rasterfall_action_track *active = NULL;
    int result = -1;
    if (!clip || !path) return -1;
    memset(clip, 0, sizeof(*clip));
    clip->layer = -1;
    data = toy_asset_load_file(path, &size);
    if (!data || !size) return -1;
    p = (char *)data; end = p + size;
    while (p < end) {
        char *line = p;
        while (p < end && *p != '\n') p++;
        saved = p < end ? *p : 0;
        if (p < end) *p = 0;
        if (parse_line(clip, line, &active) < 0) goto done;
        if (p < end) { *p = saved; p++; }
    }
    if (!active && rasterfall_action_validate(clip) == 0) result = 0;
done:
    tlibc_free(data);
    return result;
}

static struct rasterfall_animation_quaternion sample_track(
    const struct rasterfall_action_clip *clip,
    const struct rasterfall_action_track *track, int time_ms)
{
    const struct rasterfall_animation_keyframe *a, *b;
    unsigned int k;
    int factor = 0;
    if (clip->loop && clip->duration_ms > 0) time_ms %= clip->duration_ms;
    if (time_ms < 0) time_ms = 0;
    if (time_ms > clip->duration_ms) time_ms = clip->duration_ms;
    a = b = &clip->keys[track->first_key];
    for (k = 1; k < track->key_count; k++) {
        const struct rasterfall_animation_keyframe *next = a + 1;
        if (time_ms < next->time_ms) { b = next; break; }
        a = next;
    }
    if (track->interpolation == RASTERFALL_ACTION_LINEAR && b != a)
        factor = (time_ms - a->time_ms) * 1000 / (b->time_ms - a->time_ms);
    return rasterfall_animation_quat_nlerp(a->rotation, b->rotation, factor);
}

static int action_matches_layer(enum rasterfall_action_id id,
                                enum rasterfall_action_layer_id layer)
{
    if (layer == RASTERFALL_ACTION_LAYER_LOWER_BODY)
        return id == RASTERFALL_ACTION_LOCOMOTION_IDLE ||
               id == RASTERFALL_ACTION_LOCOMOTION_WALK;
    if (layer == RASTERFALL_ACTION_LAYER_UPPER_BODY)
        return id == RASTERFALL_ACTION_RIFLE_IDLE ||
               id == RASTERFALL_ACTION_RIFLE_AIM ||
               id == RASTERFALL_ACTION_RIFLE_FIRE;
    if (layer == RASTERFALL_ACTION_LAYER_ADDITIVE)
        return id == RASTERFALL_ACTION_RIFLE_RECOIL;
    return 0;
}

static int role_in_layer(enum rasterfall_humanoid_bone role,
                         enum rasterfall_action_layer_id layer)
{
    if (layer == RASTERFALL_ACTION_LAYER_LOWER_BODY)
        return role == RASTERFALL_HUMANOID_ROOT ||
               role == RASTERFALL_HUMANOID_HIPS ||
               role >= RASTERFALL_HUMANOID_LEFT_UPPER_LEG;
    if (layer == RASTERFALL_ACTION_LAYER_UPPER_BODY)
        return role >= RASTERFALL_HUMANOID_SPINE &&
               role <= RASTERFALL_HUMANOID_RIGHT_HAND;
    if (layer == RASTERFALL_ACTION_LAYER_ADDITIVE)
        return role == RASTERFALL_HUMANOID_SPINE ||
               role == RASTERFALL_HUMANOID_CHEST ||
               role == RASTERFALL_HUMANOID_LEFT_SHOULDER ||
               role == RASTERFALL_HUMANOID_LEFT_UPPER_ARM ||
               role == RASTERFALL_HUMANOID_LEFT_FOREARM ||
               role == RASTERFALL_HUMANOID_RIGHT_SHOULDER ||
               role == RASTERFALL_HUMANOID_RIGHT_UPPER_ARM ||
               role == RASTERFALL_HUMANOID_RIGHT_FOREARM;
    return 0;
}

static struct rasterfall_animation_quaternion quat_multiply(
    struct rasterfall_animation_quaternion a,
    struct rasterfall_animation_quaternion b)
{
    struct rasterfall_animation_quaternion q;
    q.w = a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z;
    q.x = a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y;
    q.y = a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x;
    q.z = a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w;
    return rasterfall_animation_quat_normalize(q);
}

static int apply_layer(struct rasterfall_model_asset *pose,
    const struct rasterfall_action_layer *layer,
    enum rasterfall_action_layer_id layer_id)
{
    unsigned int i;
    const struct rasterfall_action_clip *clip = layer->clip;
    if (!clip) return 0;
    if (rasterfall_action_validate(clip) < 0 || clip->layer != layer_id ||
        !action_matches_layer(clip->id, layer_id)) return -1;
    for (i = 0; i < clip->track_count; i++) {
        int bone;
        struct rasterfall_animation_rotation rotation;
        if (!role_in_layer(clip->tracks[i].target, layer_id)) return -1;
        bone = rasterfall_model_humanoid_bone(pose, clip->tracks[i].target);
        if (bone < 0 || bone >= (int)pose->bone_count) return -1;
        if (layer_id == RASTERFALL_ACTION_LAYER_ADDITIVE) {
            struct rasterfall_animation_quaternion base =
                rasterfall_animation_quat_from_euler(
                    pose->bones[bone].rotate_x, pose->bones[bone].rotate_y,
                    pose->bones[bone].rotate_z);
            rasterfall_animation_quat_to_euler(quat_multiply(base,
                sample_track(clip, &clip->tracks[i], layer->time_ms)), &rotation);
        } else rasterfall_animation_quat_to_euler(sample_track(clip,
            &clip->tracks[i], layer->time_ms), &rotation);
        pose->bones[bone].rotate_x = rotation.x;
        pose->bones[bone].rotate_y = rotation.y;
        pose->bones[bone].rotate_z = rotation.z;
    }
    return 0;
}

int rasterfall_action_compose(struct rasterfall_model_instance *instance,
    const struct rasterfall_action_composition *composition)
{
    struct rasterfall_model_asset *pose;
    int layer;
    if (!instance || !composition ||
        rasterfall_model_instance_reset_pose(instance) < 0) return -1;
    pose = rasterfall_model_instance_pose(instance);
    if (!pose || !pose->has_character_contract) return -1;
    for (layer = RASTERFALL_ACTION_LAYER_LOWER_BODY;
         layer <= RASTERFALL_ACTION_LAYER_ADDITIVE; layer++)
        if (apply_layer(pose, &composition->layers[layer], layer) < 0) return -1;
    return rasterfall_model_instance_update_bones(instance);
}

int rasterfall_action_apply(struct rasterfall_model_instance *instance,
                            const struct rasterfall_action_clip *clip,
                            int time_ms)
{
    struct rasterfall_action_composition composition;
    enum rasterfall_action_layer_id layer;
    if (!clip) return -1;
    memset(&composition, 0, sizeof(composition));
    layer = clip->layer;
    composition.layers[layer].clip = clip;
    composition.layers[layer].time_ms = time_ms;
    return rasterfall_action_compose(instance, &composition);
}

int rasterfall_action_weapon_target_debug(
    const struct rasterfall_model_instance *instance, int weapon,
    struct rasterfall_action_weapon_targets *targets)
{
    struct rasterfall_model_attachment_transform grip_target;
    struct rasterfall_weapon_socket_transform grip, foregrip;
    const struct rasterfall_model_asset *pose;
    int i;
    if (!instance || !targets) return -1;
    pose = rasterfall_model_instance_final_pose(instance);
    if (!pose || rasterfall_model_instance_attachment_transform(instance,
            RASTERFALL_ATTACHMENT_WEAPON_R, &grip_target) < 0 ||
        rasterfall_weapon_socket_transform(weapon,
            RASTERFALL_WEAPON_SOCKET_PRIMARY_GRIP, &grip) < 0 ||
        rasterfall_weapon_socket_transform(weapon,
            RASTERFALL_WEAPON_SOCKET_FOREGRIP, &foregrip) < 0) return -1;
    memset(targets, 0, sizeof(*targets));
    for (i = 0; i < 9; i++)
        targets->weapon_transform[i] = grip_target.rotation[i];
    for (i = 0; i < 3; i++) {
        int row;
        targets->right_hand_target[i] = grip_target.position[i];
        targets->weapon_transform[9+i] = grip_target.position[i];
        for (row = 0; row < 3; row++) {
            targets->weapon_transform[9+i] -= grip_target.rotation[i*3+row] *
                ((int *)&grip.position)[row];
        }
        targets->left_hand_target[i] = targets->weapon_transform[9+i];
        for (row = 0; row < 3; row++)
            targets->left_hand_target[i] += grip_target.rotation[i*3+row] *
                ((int *)&foregrip.position)[row];
    }
    return 0;
}

void rasterfall_action_dump(const struct rasterfall_action_clip *clip)
{
    unsigned int i, k;
    if (!clip) return;
    __printf("action:\n  %s\nlayer:\n  %s\nduration:\n  %dms\nskeleton:\n  %s\nloop:\n  %s\ntracks:\n",
        clip->name, rasterfall_action_layer_name(clip->layer), clip->duration_ms,
        clip->skeleton, clip->loop ? "yes" : "no");
    for (i = 0; i < clip->track_count; i++) {
        const struct rasterfall_action_track *t = &clip->tracks[i];
        __printf("  %s interpolation=%s keys=%u\n", role_names[t->target],
            t->interpolation == RASTERFALL_ACTION_LINEAR ? "linear" : "step", t->key_count);
        for (k = 0; k < t->key_count; k++) {
            struct rasterfall_animation_rotation r;
            const struct rasterfall_animation_keyframe *key = &clip->keys[t->first_key + k];
            rasterfall_animation_quat_to_euler(key->rotation, &r);
            __printf("    %dms rotation=(%d,%d,%d)\n", key->time_ms, r.x, r.y, r.z);
        }
    }
}

int rasterfall_action_pose_debug(const struct rasterfall_model_instance *instance,
                                 const char *role_name)
{
    const struct rasterfall_model_asset *pose;
    const struct rasterfall_model_bone_transform *transform;
    int role = role_from_name(role_name), bone, i;
    if (!instance || role < 0) return -1;
    pose = rasterfall_model_instance_final_pose(instance);
    bone = rasterfall_model_humanoid_bone(pose, role);
    transform = rasterfall_model_instance_bone_transform(instance, bone);
    if (!transform) return -1;
    __printf("bone:\n  %s\nstable_bone_id:\n  %d\nposition:\n  %.6f %.6f %.6f\nrotation:\n",
        role_name, bone, transform->position[0], transform->position[1], transform->position[2]);
    for (i = 0; i < 3; i++) __printf("  %.6f %.6f %.6f\n", transform->rotation[i*3], transform->rotation[i*3+1], transform->rotation[i*3+2]);
    __printf("sockets:\n");
    for (i = 0; i < RASTERFALL_ATTACHMENT_COUNT; i++) {
        static const char *names[] = {"WEAPON_RIGHT_GRIP", "WEAPON_LEFT_GRIP", "FOREGRIP", "BACK", "CHEST", "HEAD", "HIP_LEFT", "HIP_RIGHT"};
        struct rasterfall_model_attachment_transform socket;
        if (pose->attachments[i].present && pose->attachments[i].parent_bone == bone &&
            rasterfall_model_instance_attachment_transform(instance, i, &socket) == 0)
            __printf("  %s position=(%.6f,%.6f,%.6f)\n", names[i], socket.position[0], socket.position[1], socket.position[2]);
    }
    return 0;
}

static const enum rasterfall_humanoid_bone pipeline_debug_roles[] = {
    RASTERFALL_HUMANOID_SPINE,
    RASTERFALL_HUMANOID_LEFT_SHOULDER,
    RASTERFALL_HUMANOID_RIGHT_SHOULDER,
    RASTERFALL_HUMANOID_RIGHT_HAND,
    RASTERFALL_HUMANOID_LEFT_HAND
};

static void pipeline_print_matrix(const char *name, const double position[3],
                                  const double rotation[9])
{
    int row;
    __printf("  %s transform position=(%.6f,%.6f,%.6f) rotation=\n",
        name, position[0], position[1], position[2]);
    for (row = 0; row < 3; row++)
        __printf("    %.6f %.6f %.6f\n", rotation[row * 3],
            rotation[row * 3 + 1], rotation[row * 3 + 2]);
}

static void pipeline_print_bone(
    const struct rasterfall_model_instance *instance,
    enum rasterfall_humanoid_bone role)
{
    const struct rasterfall_model_asset *pose =
        rasterfall_model_instance_final_pose(instance);
    const struct rasterfall_model_bone_transform *transform;
    int bone;
    if (!pose) return;
    bone = rasterfall_model_humanoid_bone(pose, role);
    transform = bone >= 0 ? rasterfall_model_instance_bone_transform(
        instance, (unsigned int)bone) : NULL;
    if (!transform) {
        __printf("  %s role=%d bone=UNMAPPED\n", role_names[role], role);
        return;
    }
    __printf("  %s role=%d bone=%d local_rotation=(%d,%d,%d)\n",
        role_names[role], role, bone, pose->bones[bone].rotate_x,
        pose->bones[bone].rotate_y, pose->bones[bone].rotate_z);
    pipeline_print_matrix(role_names[role], transform->position,
                          transform->rotation);
}

static int pipeline_print_attachment(
    const struct rasterfall_model_instance *instance,
    enum rasterfall_character_attachment attachment, const char *name,
    struct rasterfall_model_attachment_transform *out)
{
    if (rasterfall_model_instance_attachment_transform(instance, attachment,
                                                       out) < 0) {
        __printf("  %s transform=UNAVAILABLE\n", name);
        memset(out, 0, sizeof(*out));
        return -1;
    }
    pipeline_print_matrix(name, out->position, out->rotation);
    return 0;
}

static void pipeline_matrix_multiply(const double a[9], const double b[9],
                                     double out[9])
{
    int row, column;
    for (row = 0; row < 3; row++)
        for (column = 0; column < 3; column++)
            out[row * 3 + column] = a[row * 3] * b[column] +
                a[row * 3 + 1] * b[3 + column] +
                a[row * 3 + 2] * b[6 + column];
}

static void pipeline_matrix_vector(const double rotation[9],
                                   const double vector[3], double out[3])
{
    out[0] = rotation[0] * vector[0] + rotation[1] * vector[1] +
             rotation[2] * vector[2];
    out[1] = rotation[3] * vector[0] + rotation[4] * vector[1] +
             rotation[5] * vector[2];
    out[2] = rotation[6] * vector[0] + rotation[7] * vector[1] +
             rotation[8] * vector[2];
}

static void pipeline_quaternion_matrix(const float q[4], double out[9])
{
    double x = q[0], y = q[1], z = q[2], w = q[3];
    out[0] = 1.0 - 2.0 * (y * y + z * z);
    out[1] = 2.0 * (x * y - z * w);
    out[2] = 2.0 * (x * z + y * w);
    out[3] = 2.0 * (x * y + z * w);
    out[4] = 1.0 - 2.0 * (x * x + z * z);
    out[5] = 2.0 * (y * z - x * w);
    out[6] = 2.0 * (x * z - y * w);
    out[7] = 2.0 * (y * z + x * w);
    out[8] = 1.0 - 2.0 * (x * x + y * y);
}

static void pipeline_matrix_transpose(const double in[9], double out[9])
{
    int row, column;
    for (row = 0; row < 3; row++)
        for (column = 0; column < 3; column++)
            out[row * 3 + column] = in[column * 3 + row];
}

static double pipeline_vector_length(const double vector[3])
{
    return sqrt(vector[0] * vector[0] + vector[1] * vector[1] +
                vector[2] * vector[2]);
}

static void pipeline_print_delta(const char *name, const double a[3],
                                 const double b[3])
{
    double delta[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    __printf("  %s delta=(%.6f,%.6f,%.6f) length=%.6f\n", name,
        delta[0], delta[1], delta[2], pipeline_vector_length(delta));
}

static int pipeline_print_weapon(
    const struct rasterfall_model_instance *instance, int weapon,
    const struct rasterfall_model_attachment_transform *weapon_right,
    const struct rasterfall_model_attachment_transform *foregrip_attachment)
{
    struct rasterfall_weapon_socket_transform authored[RASTERFALL_WEAPON_SOCKET_COUNT];
    double authored_rotation[RASTERFALL_WEAPON_SOCKET_COUNT][9];
    double weapon_rotation[9], inverse_primary[9], origin[3];
    double derived_position[RASTERFALL_WEAPON_SOCKET_COUNT][3];
    double derived_rotation[RASTERFALL_WEAPON_SOCKET_COUNT][9];
    const struct rasterfall_model_asset *pose =
        rasterfall_model_instance_final_pose(instance);
    const struct rasterfall_model_bone_transform *right_hand;
    const struct rasterfall_weapon_asset_profile *asset_profile;
    const struct rasterfall_pose_calibration *pose_profile;
    int right_hand_bone, i;
    if (!weapon_right || !foregrip_attachment ||
        rasterfall_weapon_socket_transform(weapon,
            RASTERFALL_WEAPON_SOCKET_PRIMARY_GRIP, &authored[0]) < 0) {
        __printf("weapon: UNAVAILABLE weapon=%d\n", weapon);
        return 0;
    }
    for (i = 1; i < RASTERFALL_WEAPON_SOCKET_COUNT; i++)
        if (rasterfall_weapon_socket_transform(weapon, i, &authored[i]) < 0) {
            __printf("weapon: UNAVAILABLE socket=%s\n",
                rasterfall_weapon_socket_name(i));
            return 0;
        }
    __printf("weapon_authored_local:\n");
    for (i = 0; i < RASTERFALL_WEAPON_SOCKET_COUNT; i++)
        __printf("  %s position=(%d,%d,%d) rotation=(%.6f,%.6f,%.6f,%.6f)\n",
            rasterfall_weapon_socket_name(i), authored[i].position.x,
            authored[i].position.y, authored[i].position.z,
            authored[i].rotation[0], authored[i].rotation[1],
            authored[i].rotation[2], authored[i].rotation[3]);
    asset_profile = rasterfall_weapon_asset_profile(weapon);
    pose_profile = rasterfall_pose_calibration_resolve(NULL, 0, weapon);
    if (asset_profile && pose_profile)
        __printf("renderer_calibration_input:\n"
                 "  asset_attachment_grip=(%d,%d,%d) asset_basis=%d\n"
                 "  offset=(%d,%d,%d) yaw_pitch_roll=(%d,%d,%d)\n"
                 "  grip=(%d,%d,%d) foregrip=(%d,%d,%d) muzzle=(%d,%d,%d)\n",
            asset_profile->attachment_grip.x, asset_profile->attachment_grip.y,
            asset_profile->attachment_grip.z, asset_profile->asset_basis,
            pose_profile->offset.x, pose_profile->offset.y,
            pose_profile->offset.z, pose_profile->yaw_offset,
            pose_profile->pitch_offset, pose_profile->roll_offset,
            pose_profile->grip.x, pose_profile->grip.y, pose_profile->grip.z,
            pose_profile->foregrip.x, pose_profile->foregrip.y,
            pose_profile->foregrip.z, pose_profile->muzzle.x,
            pose_profile->muzzle.y, pose_profile->muzzle.z);
    for (i = 0; i < RASTERFALL_WEAPON_SOCKET_COUNT; i++)
        pipeline_quaternion_matrix(authored[i].rotation, authored_rotation[i]);
    pipeline_matrix_transpose(authored_rotation[RASTERFALL_WEAPON_SOCKET_PRIMARY_GRIP],
                              inverse_primary);
    pipeline_matrix_multiply(weapon_right->rotation, inverse_primary,
                             weapon_rotation);
    pipeline_matrix_vector(weapon_rotation,
        (double[3]){authored[RASTERFALL_WEAPON_SOCKET_PRIMARY_GRIP].position.x,
                    authored[RASTERFALL_WEAPON_SOCKET_PRIMARY_GRIP].position.y,
                    authored[RASTERFALL_WEAPON_SOCKET_PRIMARY_GRIP].position.z},
        origin);
    for (i = 0; i < 3; i++) origin[i] = weapon_right->position[i] - origin[i];
    for (i = 0; i < RASTERFALL_WEAPON_SOCKET_COUNT; i++) {
        double local[3];
        local[0] = authored[i].position.x;
        local[1] = authored[i].position.y;
        local[2] = authored[i].position.z;
        pipeline_matrix_vector(weapon_rotation, local, derived_position[i]);
        for (int axis = 0; axis < 3; axis++)
            derived_position[i][axis] += origin[axis];
        pipeline_matrix_multiply(weapon_rotation, authored_rotation[i],
                                 derived_rotation[i]);
    }
    pipeline_print_matrix("weapon origin", origin, weapon_rotation);
    for (i = 0; i < RASTERFALL_WEAPON_SOCKET_COUNT; i++)
        pipeline_print_matrix(rasterfall_weapon_socket_name(i),
                              derived_position[i], derived_rotation[i]);
    {
        double muzzle_delta[3], muzzle_length, muzzle_direction[3];
        muzzle_delta[0] = derived_position[RASTERFALL_WEAPON_SOCKET_MUZZLE][0] -
            derived_position[RASTERFALL_WEAPON_SOCKET_PRIMARY_GRIP][0];
        muzzle_delta[1] = derived_position[RASTERFALL_WEAPON_SOCKET_MUZZLE][1] -
            derived_position[RASTERFALL_WEAPON_SOCKET_PRIMARY_GRIP][1];
        muzzle_delta[2] = derived_position[RASTERFALL_WEAPON_SOCKET_MUZZLE][2] -
            derived_position[RASTERFALL_WEAPON_SOCKET_PRIMARY_GRIP][2];
        muzzle_length = pipeline_vector_length(muzzle_delta);
        for (i = 0; i < 3; i++)
            muzzle_direction[i] = muzzle_length > 0.000001 ?
                muzzle_delta[i] / muzzle_length : 0.0;
        __printf("  MUZZLE direction=(%.6f,%.6f,%.6f) length=%.6f\n",
            muzzle_direction[0], muzzle_direction[1], muzzle_direction[2],
            muzzle_length);
        __printf("  MUZZLE axis=(%.6f,%.6f,%.6f) canonical=+Z\n",
            derived_rotation[RASTERFALL_WEAPON_SOCKET_MUZZLE][2],
            derived_rotation[RASTERFALL_WEAPON_SOCKET_MUZZLE][5],
            derived_rotation[RASTERFALL_WEAPON_SOCKET_MUZZLE][8]);
    }
    if (pose) {
        int expected_right_hand = rasterfall_model_humanoid_bone(pose,
            RASTERFALL_HUMANOID_RIGHT_HAND);
        int expected_left_hand = rasterfall_model_humanoid_bone(pose,
            RASTERFALL_HUMANOID_LEFT_HAND);
        __printf("socket_parent_mapping WEAPON_R parent=%d expected_right_hand=%d %s; FOREGRIP parent=%d expected_left_hand=%d %s\n",
            pose->attachments[RASTERFALL_ATTACHMENT_WEAPON_R].parent_bone,
            expected_right_hand,
            pose->attachments[RASTERFALL_ATTACHMENT_WEAPON_R].parent_bone ==
                expected_right_hand ? "MATCH" : "MISMATCH",
            pose->attachments[RASTERFALL_ATTACHMENT_FOREGRIP].parent_bone,
            expected_left_hand,
            pose->attachments[RASTERFALL_ATTACHMENT_FOREGRIP].parent_bone ==
                expected_left_hand ? "MATCH" : "MISMATCH");
        right_hand_bone = rasterfall_model_humanoid_bone(pose,
            RASTERFALL_HUMANOID_RIGHT_HAND);
        right_hand = right_hand_bone >= 0 ?
            rasterfall_model_instance_bone_transform(instance,
                (unsigned int)right_hand_bone) : NULL;
        if (right_hand)
            pipeline_print_delta("right_hand_vs_WEAPON_R", right_hand->position,
                weapon_right->position);
        if (right_hand)
            pipeline_print_delta("right_hand_vs_PRIMARY_GRIP",
                right_hand->position,
                derived_position[RASTERFALL_WEAPON_SOCKET_PRIMARY_GRIP]);
        pipeline_print_delta("WEAPON_R_vs_PRIMARY_GRIP",
            weapon_right->position,
            derived_position[RASTERFALL_WEAPON_SOCKET_PRIMARY_GRIP]);
        pipeline_print_delta("FOREGRIP_attachment_vs_FOREGRIP",
            foregrip_attachment->position,
            derived_position[RASTERFALL_WEAPON_SOCKET_FOREGRIP]);
        {
            int left_hand_bone = rasterfall_model_humanoid_bone(pose,
                RASTERFALL_HUMANOID_LEFT_HAND);
            const struct rasterfall_model_bone_transform *left_hand =
                left_hand_bone >= 0 ? rasterfall_model_instance_bone_transform(
                    instance, (unsigned int)left_hand_bone) : NULL;
            if (left_hand)
                pipeline_print_delta("left_hand_vs_FOREGRIP_attachment",
                    left_hand->position, foregrip_attachment->position);
            if (left_hand)
                pipeline_print_delta("left_hand_vs_FOREGRIP",
                    left_hand->position,
                    derived_position[RASTERFALL_WEAPON_SOCKET_FOREGRIP]);
        }
    }
    __printf("placement_provenance:\n"
             "  canonical_diagnostic=finalized_instance_attachment:WEAPON_R + authored_weapon_local:PRIMARY_GRIP\n"
             "  modular_renderer_weapon=finalized_instance_attachment:WEAPON_R + authored_weapon_local:PRIMARY_GRIP\n"
             "  WEAPON_R_role=finalized_pose_weapon_source; FOREGRIP_role=socket_contract_only\n"
             "  legacy_actor_weapon_offset=not_used_by_modular_skeletal_path\n");
    return 0;
}

int rasterfall_action_pipeline_debug(
    const struct rasterfall_model_instance *instance, const char *label,
    int weapon)
{
    struct rasterfall_model_attachment_transform weapon_right;
    struct rasterfall_model_attachment_transform foregrip;
    int i, have_weapon_right, have_foregrip;
    if (!instance || !rasterfall_model_instance_final_pose(instance)) return -1;
    __printf("ACTION_PIPELINE label=%s\n", label ? label : "POSE");
    __printf("finalized_bones model_space:\n");
    for (i = 0; i < (int)(sizeof(pipeline_debug_roles) /
                          sizeof(pipeline_debug_roles[0])); i++)
        pipeline_print_bone(instance, pipeline_debug_roles[i]);
    __printf("finalized_character_sockets:\n");
    have_weapon_right = pipeline_print_attachment(instance,
        RASTERFALL_ATTACHMENT_WEAPON_R, "WEAPON_R", &weapon_right);
    have_foregrip = pipeline_print_attachment(instance,
        RASTERFALL_ATTACHMENT_FOREGRIP, "FOREGRIP", &foregrip);
    if (have_weapon_right == 0 && have_foregrip == 0)
        pipeline_print_weapon(instance, weapon, &weapon_right, &foregrip);
    else __printf("weapon: UNAVAILABLE because finalized character sockets are incomplete\n");
    return 0;
}

static void pipeline_compare_bone(
    const struct rasterfall_model_instance *a,
    const struct rasterfall_model_instance *b, const char *a_label,
    const char *b_label, enum rasterfall_humanoid_bone role)
{
    const struct rasterfall_model_asset *pose_a =
        rasterfall_model_instance_final_pose(a);
    const struct rasterfall_model_asset *pose_b =
        rasterfall_model_instance_final_pose(b);
    int bone_a = rasterfall_model_humanoid_bone(pose_a, role);
    int bone_b = rasterfall_model_humanoid_bone(pose_b, role);
    const struct rasterfall_model_bone_transform *ta = bone_a >= 0 ?
        rasterfall_model_instance_bone_transform(a, (unsigned int)bone_a) : NULL;
    const struct rasterfall_model_bone_transform *tb = bone_b >= 0 ?
        rasterfall_model_instance_bone_transform(b, (unsigned int)bone_b) : NULL;
    if (!ta || !tb) {
        __printf("  %s mapping=%d/%d transform=UNAVAILABLE\n",
            role_names[role], bone_a, bone_b);
        return;
    }
    __printf("  %s %s_local=(%d,%d,%d) %s_local=(%d,%d,%d) local_delta=(%d,%d,%d) position_delta=(%.6f,%.6f,%.6f)\n",
        role_names[role], a_label, pose_a->bones[bone_a].rotate_x,
        pose_a->bones[bone_a].rotate_y, pose_a->bones[bone_a].rotate_z,
        b_label, pose_b->bones[bone_b].rotate_x, pose_b->bones[bone_b].rotate_y,
        pose_b->bones[bone_b].rotate_z,
        pose_b->bones[bone_b].rotate_x - pose_a->bones[bone_a].rotate_x,
        pose_b->bones[bone_b].rotate_y - pose_a->bones[bone_a].rotate_y,
        pose_b->bones[bone_b].rotate_z - pose_a->bones[bone_a].rotate_z,
        tb->position[0] - ta->position[0], tb->position[1] - ta->position[1],
        tb->position[2] - ta->position[2]);
}

int rasterfall_action_pipeline_compare_debug(
    const struct rasterfall_model_instance *a, const char *a_label,
    const struct rasterfall_model_instance *b, const char *b_label,
    int weapon)
{
    int i;
    if (!a || !b) return -1;
    __printf("ACTION_COMPARE %s_vs_%s\n", a_label ? a_label : "A",
             b_label ? b_label : "B");
    __printf("bone_mapping_and_action_delta:\n");
    for (i = 0; i < (int)(sizeof(pipeline_debug_roles) /
                          sizeof(pipeline_debug_roles[0])); i++)
        pipeline_compare_bone(a, b, a_label ? a_label : "A",
                              b_label ? b_label : "B", pipeline_debug_roles[i]);
    __printf("%s_snapshot:\n", a_label ? a_label : "A");
    rasterfall_action_pipeline_debug(a, a_label, weapon);
    __printf("%s_snapshot:\n", b_label ? b_label : "B");
    rasterfall_action_pipeline_debug(b, b_label, weapon);
    return 0;
}

int rasterfall_action_logic_test(void)
{
    struct rasterfall_action_clip clip, lower, aim, fire, recoil;
    struct rasterfall_animation_rotation a, b;
    unsigned int i, upper_roles = 0;
    if (rasterfall_action_load(&clip, "rasterfall/assets/actions/rifle_idle.rfanim") < 0) return 1;
    if (clip.id != RASTERFALL_ACTION_RIFLE_IDLE || clip.duration_ms != 2400 ||
        clip.track_count < 5 || clip.key_count < 10) return 2;
    rasterfall_animation_quat_to_euler(sample_track(&clip,&clip.tracks[0],1200),&a);
    rasterfall_animation_quat_to_euler(sample_track(&clip,&clip.tracks[0],3600),&b);
    if (a.x != 1 || b.x != a.x || a.y || a.z) return 3;
    if (rasterfall_action_load(&lower, "rasterfall/assets/actions/locomotion_walk.rfanim") < 0 ||
        rasterfall_action_load(&aim, "rasterfall/assets/actions/rifle_aim.rfanim") < 0 ||
        rasterfall_action_load(&fire, "rasterfall/assets/actions/rifle_fire.rfanim") < 0 ||
        rasterfall_action_load(&recoil, "rasterfall/assets/actions/rifle_recoil.rfanim") < 0)
        return 4;
    if (!action_matches_layer(lower.id, RASTERFALL_ACTION_LAYER_LOWER_BODY) ||
        !action_matches_layer(aim.id, RASTERFALL_ACTION_LAYER_UPPER_BODY) ||
        !action_matches_layer(fire.id, RASTERFALL_ACTION_LAYER_UPPER_BODY) ||
        !action_matches_layer(recoil.id, RASTERFALL_ACTION_LAYER_ADDITIVE) ||
        recoil.layer != RASTERFALL_ACTION_LAYER_ADDITIVE ||
        role_in_layer(RASTERFALL_HUMANOID_ROOT, RASTERFALL_ACTION_LAYER_ADDITIVE) ||
        role_in_layer(RASTERFALL_HUMANOID_LEFT_UPPER_LEG,
            RASTERFALL_ACTION_LAYER_ADDITIVE) ||
        role_in_layer(RASTERFALL_HUMANOID_LEFT_UPPER_LEG,
            RASTERFALL_ACTION_LAYER_UPPER_BODY)) return 5;
    for (i = 0; i < aim.track_count; i++) {
        switch (aim.tracks[i].target) {
        case RASTERFALL_HUMANOID_CHEST: upper_roles |= 1u << 0; break;
        case RASTERFALL_HUMANOID_RIGHT_UPPER_ARM: upper_roles |= 1u << 1; break;
        case RASTERFALL_HUMANOID_RIGHT_FOREARM: upper_roles |= 1u << 2; break;
        case RASTERFALL_HUMANOID_LEFT_UPPER_ARM: upper_roles |= 1u << 3; break;
        case RASTERFALL_HUMANOID_LEFT_FOREARM: upper_roles |= 1u << 4; break;
        case RASTERFALL_HUMANOID_LEFT_HAND: upper_roles |= 1u << 5; break;
        case RASTERFALL_HUMANOID_RIGHT_HAND: upper_roles |= 1u << 6; break;
        default: break;
        }
    }
    if (upper_roles != 0x7f) return 6;
    rasterfall_animation_quat_to_euler(sample_track(&lower,&lower.tracks[1],0),&a);
    rasterfall_animation_quat_to_euler(sample_track(&lower,&lower.tracks[1],400),&b);
    if (lower.duration_ms != 800 || a.x != 24 || b.x != -24 ||
        sample_track(&lower,&lower.tracks[1],800).x !=
        sample_track(&lower,&lower.tracks[1],0).x) return 7;
    return 0;
}
