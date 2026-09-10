#include "core.h"
#include "string.h"
#include "tlibc_everything.h"
#include "toy_assets.h"
#include "rasterfall_action.h"
#include "rasterfall_model.h"

static const char *const action_names[] = {"NONE", "RIFLE_IDLE"};
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
        !clip->track_count || !clip->key_count) return -1;
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

int rasterfall_action_apply(struct rasterfall_model_instance *instance,
                            const struct rasterfall_action_clip *clip,
                            int time_ms)
{
    struct rasterfall_model_asset *pose;
    unsigned int i;
    if (!instance || rasterfall_action_validate(clip) < 0 ||
        rasterfall_model_instance_reset_pose(instance) < 0) return -1;
    pose = rasterfall_model_instance_pose(instance);
    if (!pose->has_character_contract) return -1;
    for (i = 0; i < clip->track_count; i++) {
        int bone = rasterfall_model_humanoid_bone(pose, clip->tracks[i].target);
        struct rasterfall_animation_rotation rotation;
        if (bone < 0 || bone >= (int)pose->bone_count) return -1;
        rasterfall_animation_quat_to_euler(sample_track(clip, &clip->tracks[i], time_ms), &rotation);
        pose->bones[bone].rotate_x = rotation.x;
        pose->bones[bone].rotate_y = rotation.y;
        pose->bones[bone].rotate_z = rotation.z;
    }
    return rasterfall_model_instance_update_bones(instance);
}

void rasterfall_action_dump(const struct rasterfall_action_clip *clip)
{
    unsigned int i, k;
    if (!clip) return;
    __printf("action:\n  %s\nduration:\n  %dms\nskeleton:\n  %s\nloop:\n  %s\ntracks:\n",
        clip->name, clip->duration_ms, clip->skeleton, clip->loop ? "yes" : "no");
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

int rasterfall_action_logic_test(void)
{
    struct rasterfall_action_clip clip;
    struct rasterfall_animation_rotation a, b;
    if (rasterfall_action_load(&clip, "rasterfall/assets/actions/rifle_idle.rfanim") < 0) return 1;
    if (clip.id != RASTERFALL_ACTION_RIFLE_IDLE || clip.duration_ms != 1200 ||
        clip.track_count < 5 || clip.key_count < 10) return 2;
    rasterfall_animation_quat_to_euler(sample_track(&clip,&clip.tracks[0],600),&a);
    rasterfall_animation_quat_to_euler(sample_track(&clip,&clip.tracks[0],1800),&b);
    if (a.x < 1 || a.x > 3 || b.x != a.x || a.y || a.z) return 3;
    return 0;
}
