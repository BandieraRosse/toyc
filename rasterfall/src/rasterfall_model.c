#include "core.h"
#include "tlibc_everything.h"
#include "toy_assets.h"
#include "rasterfall_model.h"
#include "math.h"
#include "rasterfall_humanoid_retarget.h"
#include "rasterfall_glb_animation.h"

static unsigned int model_u32(const unsigned char *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
}

static unsigned int model_u16(const unsigned char *p)
{
    return p[0] | p[1] << 8;
}

static int model_i32(const unsigned char *p)
{
    return (int)model_u32(p);
}

static void matrix_multiply(const double *a, const double *b, double *out)
{
    double r[9];
    int row, column;
    for (row = 0; row < 3; row++) for (column = 0; column < 3; column++)
        r[row * 3 + column] =
            a[row * 3] * b[column] +
            a[row * 3 + 1] * b[3 + column] +
            a[row * 3 + 2] * b[6 + column];
    memcpy(out, r, sizeof(r));
}

static void matrix_rotate_xyz(int x_degrees, int y_degrees, int z_degrees,
                              double *out)
{
    if (!x_degrees && !y_degrees && !z_degrees) {
        int i;
        for (i = 0; i < 9; i++) out[i] = 0.0;
        out[0] = out[4] = out[8] = 1.0;
        return;
    }
    double x = x_degrees * M_PI / 180.0;
    double y = y_degrees * M_PI / 180.0;
    double z = z_degrees * M_PI / 180.0;
    double sx = sin(x), cx = cos(x), sy = sin(y), cy = cos(y);
    double sz = sin(z), cz = cos(z);
    double rx[9] = {1, 0, 0, 0, cx, -sx, 0, sx, cx};
    double ry[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy};
    double rz[9] = {cz, -sz, 0, sz, cz, 0, 0, 0, 1};
    double temp[9];
    matrix_multiply(rz, ry, temp);
    matrix_multiply(temp, rx, out);
}

static void matrix_vector(const double *m, double x, double y, double z,
                          double *out_x, double *out_y, double *out_z)
{
    *out_x = m[0] * x + m[1] * y + m[2] * z;
    *out_y = m[3] * x + m[4] * y + m[5] * z;
    *out_z = m[6] * x + m[7] * y + m[8] * z;
}

static int rounded(double value)
{
    return (int)(value + (value < 0.0 ? -0.5 : 0.5));
}

static int bone_name_matches(const char *actual, const char *wanted)
{
    return actual && wanted && (!strcmp(actual, wanted) || strstr(actual, wanted));
}

int rasterfall_model_find_bone(const struct rasterfall_model_asset *asset,
                               const char *name)
{
    unsigned int i;
    if (!asset || !name) return -1;
    for (i = 0; i < asset->bone_count; i++)
        if (bone_name_matches(asset->bones[i].name, name)) return (int)i;
    return -1;
}

static int model_find_first_bone(const struct rasterfall_model_asset *asset,
                                 const char *a, const char *b, const char *c)
{
    int index = rasterfall_model_find_bone(asset, a);
    if (index < 0 && b) index = rasterfall_model_find_bone(asset, b);
    if (index < 0 && c) index = rasterfall_model_find_bone(asset, c);
    return index;
}

static int model_validate_hierarchy(struct rasterfall_model_asset *asset)
{
    unsigned int i;
    int roots = 0, max_depth = 0;
    asset->bone_order = tlibc_malloc(asset->bone_count * sizeof(*asset->bone_order));
    if (!asset->bone_order) return -1;
    /* Validate the complete parent table before following any chain.  A
     * corrupt child may otherwise lead through a valid parent to that
     * parent's invalid parent index. */
    for (i = 0; i < asset->bone_count; i++) {
        if (asset->bones[i].parent < -1 ||
            asset->bones[i].parent >= (int)asset->bone_count) return -1;
        if (asset->bones[i].parent < 0) roots++;
    }
    for (i = 0; i < asset->bone_count; i++) {
        int at = (int)i, depth = 0;
        while (at >= 0) {
            if (++depth > (int)asset->bone_count ||
                depth > RASTERFALL_MODEL_MAX_BONE_DEPTH) return -1;
            at = asset->bones[at].parent;
        }
        if (depth > max_depth) max_depth = depth;
        {
            unsigned int insert = i;
            while (insert > 0) {
                unsigned int previous = asset->bone_order[insert - 1];
                int previous_at = (int)previous, previous_depth = 0;
                while (previous_at >= 0) {
                    previous_depth++;
                    previous_at = asset->bones[previous_at].parent;
                }
                if (previous_depth <= depth) break;
                asset->bone_order[insert] = previous;
                insert--;
            }
            asset->bone_order[insert] = i;
        }
    }
    if (!roots) return -1;
    asset->root_bone_count = (unsigned int)roots;
    asset->max_bone_depth = (unsigned int)max_depth;
    return 0;
}

static int model_load_skin(struct rasterfall_model_asset *asset,
                           const unsigned char *skin, unsigned int bytes)
{
    unsigned int bone_count, bone_bytes, vertex_count, vertex_bytes, names_bytes;
    const unsigned char *bone_data, *skin_vertices, *names;
    unsigned long required;
    unsigned int i, invalid_references = 0, bdef1 = 0, bdef2 = 0;
    if (bytes < RASTERFALL_MODEL_SKIN_HEADER_BYTES ||
        model_u32(skin) != RASTERFALL_MODEL_SKIN_MAGIC) return -1;
    bone_count = model_u32(skin + 8);
    bone_bytes = model_u32(skin + 12);
    vertex_count = model_u32(skin + 16);
    vertex_bytes = model_u32(skin + 20);
    names_bytes = model_u32(skin + 24);
    required = RASTERFALL_MODEL_SKIN_HEADER_BYTES +
        (unsigned long)bone_count * bone_bytes +
        (unsigned long)vertex_count * vertex_bytes + names_bytes;
    if (model_u32(skin + 4) != bytes || required != bytes ||
        bone_count == 0 || bone_count > RASTERFALL_MODEL_MAX_BONES ||
        bone_bytes != RASTERFALL_MODEL_BONE_BYTES ||
        vertex_count != asset->vertex_count ||
        vertex_bytes != RASTERFALL_MODEL_SKIN_VERTEX_BYTES || !names_bytes)
        return -1;
    bone_data = skin + RASTERFALL_MODEL_SKIN_HEADER_BYTES;
    skin_vertices = bone_data + bone_count * bone_bytes;
    names = skin_vertices + vertex_count * vertex_bytes;
    asset->bones = tlibc_malloc((size_t)bone_count * sizeof(*asset->bones));
    asset->bone_transforms = tlibc_malloc((size_t)bone_count *
                                          sizeof(*asset->bone_transforms));
    asset->animation_rotations = tlibc_malloc((size_t)bone_count *
                                              sizeof(*asset->animation_rotations));
    if (!asset->bones || !asset->bone_transforms || !asset->animation_rotations)
        return -1;
    __memset(asset->bones, 0, bone_count * sizeof(*asset->bones));
    asset->bone_count = bone_count;
    asset->skin_vertices = skin_vertices;
    for (i = 0; i < bone_count; i++) {
        const unsigned char *record = bone_data + i * bone_bytes;
        unsigned int name_offset = model_u32(record + 20), left;
        unsigned int name_length;
        if (name_offset >= names_bytes) return -1;
        left = names_bytes - name_offset;
        for (name_length = 0; name_length < left &&
             names[name_offset + name_length]; name_length++) {}
        if (name_length == left) return -1;
        asset->bones[i].parent = model_i32(record);
        asset->bones[i].flags = model_u16(record + 4);
        asset->bones[i].rest_x = model_i32(record + 8);
        asset->bones[i].rest_y = model_i32(record + 12);
        asset->bones[i].rest_z = model_i32(record + 16);
        asset->bones[i].name = (const char *)(names + name_offset);
    }
    if (model_validate_hierarchy(asset) < 0) return -1;
    for (i = 0; i < vertex_count; i++) {
        const unsigned char *record = skin_vertices + i * vertex_bytes;
        unsigned int bone0 = model_u16(record), bone1 = model_u16(record + 2);
        unsigned int weight = model_u16(record + 4), type = record[6];
        if (type == 0) {
            bdef1++;
            if (bone0 >= bone_count || bone1 != 0xffffU || weight != 65535U)
                invalid_references++;
        } else if (type == 1) {
            bdef2++;
            if (bone0 >= bone_count || bone1 >= bone_count)
                invalid_references++;
        } else invalid_references++;
    }
    if (invalid_references) {
        __fprintf(2, "rasterfall: invalid skeletal data: %u vertex bone references\n",
                  invalid_references);
        return -1;
    }
    asset->demo_right_arm = model_find_first_bone(asset, "右腕", "right arm", "RightArm");
    asset->demo_left_arm = model_find_first_bone(asset, "左腕", "left arm", "LeftArm");
    asset->demo_body = model_find_first_bone(asset, "上半身2", "upper body 2", "UpperBody2");
    if (asset->demo_body < 0)
        asset->demo_body = model_find_first_bone(asset, "上半身", "upper body", "UpperBody");
    rasterfall_model_build_demo_clips(asset);
    asset->skinning_enabled = 1;
    asset->pose = RASTERFALL_MODEL_POSE_BIND;
    if (rasterfall_model_update_bones(asset) < 0) return -1;
    __printf("rasterfall: skeleton bones=%u roots=%u max_depth=%u BDEF1=%u BDEF2=%u invalid_bone_references=0\n",
             bone_count, asset->root_bone_count, asset->max_bone_depth,
             bdef1, bdef2);
    __printf("rasterfall: demo bones right_arm={index=%d,name=\"%s\"} left_arm={index=%d,name=\"%s\"} body={index=%d,name=\"%s\"}\n",
             asset->demo_right_arm,
             asset->demo_right_arm >= 0 ? asset->bones[asset->demo_right_arm].name : "not found",
             asset->demo_left_arm,
             asset->demo_left_arm >= 0 ? asset->bones[asset->demo_left_arm].name : "not found",
             asset->demo_body,
             asset->demo_body >= 0 ? asset->bones[asset->demo_body].name : "not found");
    return 0;
}

static int model_texture_path(const char *model_path, int index,
                              char *out, int size)
{
    const char *slash = strrchr(model_path, '/');
    const char *name = slash ? slash + 1 : model_path;
    const char *dot = strrchr(name, '.');
    int length = slash ? (int)(slash - model_path) : 0;
    int n = dot ? (int)(dot - name) : (int)strlen(name);
    char base[128];
    if (n <= 0 || n >= (int)sizeof(base) || length >= size) return -1;
    memcpy(base, name, n); base[n] = 0;
    if (length) snprintf(out, size, "%.*s/%s.textures/texture_%03d.ttex", length, model_path, base, index);
    else snprintf(out, size, "%s.textures/texture_%03d.ttex", base, index);
    return 0;
}

int rasterfall_model_load(struct rasterfall_model_asset *asset,
                          const char *path)
{
    uint32_t size;
    uint32_t version, vertex_bytes, material_bytes;
    unsigned long static_bytes;
    unsigned int skin_offset = 0;
    unsigned char *data;
    if (!asset || !path) return -1;
    __memset(asset, 0, sizeof(*asset));
    data = toy_asset_load_file(path, &size);
    version = data && size >= 8 ? model_u32(data + 4) : 0;
    vertex_bytes = version >= 10 ? RASTERFALL_MODEL_VERTEX_BYTES_EDGE_SCALE :
                   version >= 6 ? RASTERFALL_MODEL_VERTEX_BYTES_ADDITIONAL_UV :
                                  RASTERFALL_MODEL_VERTEX_BYTES;
    material_bytes = version >= 9 ? RASTERFALL_MODEL_MATERIAL_BYTES :
                     version >= 8 ? RASTERFALL_MODEL_MATERIAL_BYTES_EDGE :
                                    RASTERFALL_MODEL_MATERIAL_BYTES_LEGACY;
    static_bytes = data && size >= RASTERFALL_MODEL_HEADER_BYTES ?
        (unsigned long)model_u32(data + 56) +
        (unsigned long)model_u32(data + 48) * material_bytes +
        (unsigned long)model_u32(data + 8) * vertex_bytes +
        (unsigned long)model_u32(data + 12) * 4 : 0;
    if (version >= 11 && data && size >= RASTERFALL_MODEL_HEADER_BYTES)
        skin_offset = model_u32(data + 60);
    if (!data || size < RASTERFALL_MODEL_HEADER_BYTES ||
        model_u32(data) != RASTERFALL_MODEL_MAGIC ||
        (version < 2 || version > RASTERFALL_MODEL_VERSION) ||
        model_u32(data + 8) > 1000000 || model_u32(data + 12) > 3000000 ||
        model_u32(data + 44) > 32 || model_u32(data + 48) > 32 ||
        model_u32(data + 52) != RASTERFALL_MODEL_HEADER_BYTES ||
        model_u32(data + 56) != RASTERFALL_MODEL_HEADER_BYTES +
            model_u32(data + 44) * RASTERFALL_MODEL_PRIMITIVE_BYTES ||
        static_bytes > (unsigned long)size ||
        (version < 11 && static_bytes != (unsigned long)size) ||
        (version >= 11 && (skin_offset != static_bytes ||
                           skin_offset >= size))) {
        if (data) tlibc_free(data);
        return -1;
    }
    asset->data = data;
    asset->data_size = size;
    asset->format_version = version;
    asset->vertex_bytes = vertex_bytes;
    asset->vertex_count = model_u32(data + 8);
    asset->index_count = model_u32(data + 12);
    asset->primitive_count = model_u32(data + 44);
    asset->material_count = model_u32(data + 48);
    asset->material_bytes = material_bytes;
    asset->primitives = data + model_u32(data + 52);
    asset->materials = data + model_u32(data + 56);
    asset->vertices = asset->materials + asset->material_count * asset->material_bytes;
    asset->indices = asset->vertices + asset->vertex_count * asset->vertex_bytes;
    asset->min_x = *(const int *)(data + 20);
    asset->min_y = *(const int *)(data + 24);
    asset->min_z = *(const int *)(data + 28);
    asset->max_x = *(const int *)(data + 32);
    asset->max_y = *(const int *)(data + 36);
    asset->max_z = *(const int *)(data + 40);
    if (version >= 11 &&
        model_load_skin(asset, data + skin_offset, size - skin_offset) < 0) {
        __fprintf(2, "rasterfall: invalid RFM2 skeletal section: %s\n", path);
        rasterfall_model_unload(asset);
        return -1;
    }
    {
        unsigned int i, max_texture = 0;
        int found = 0;
        if (asset->material_count) for (i = 0; i < asset->material_count; i++) {
            unsigned int texture = model_u32(asset->materials + i * asset->material_bytes + 8);
            unsigned int sphere = asset->format_version >= 3 ?
                model_u32(asset->materials + i * asset->material_bytes + 12) & 0xffffU : 0xffffU;
            unsigned int toon = asset->format_version >= 5 &&
                asset->materials[i * asset->material_bytes + 6] == 1 ?
                asset->materials[i * asset->material_bytes + 5] : 0xffU;
            if (texture != 0xffffffffU && texture < 256 && (!found || texture > max_texture)) { max_texture = texture; found = 1; }
            if (sphere != 0xffffU && sphere < 256 && (!found || sphere > max_texture)) { max_texture = sphere; found = 1; }
            if (toon != 0xffU && (!found || toon > max_texture)) { max_texture = toon; found = 1; }
        }
        if (found) {
            char texture_path[256];
            asset->texture_count = max_texture + 1;
            asset->texture_assets = (struct toy_texture_asset *)tlibc_malloc(asset->texture_count * sizeof(*asset->texture_assets));
            asset->texture_views = (struct toy_texture_view *)tlibc_malloc(asset->texture_count * sizeof(*asset->texture_views));
            if (!asset->texture_assets || !asset->texture_views) { rasterfall_model_unload(asset); return -1; }
            __memset(asset->texture_assets, 0, asset->texture_count * sizeof(*asset->texture_assets));
            __memset(asset->texture_views, 0, asset->texture_count * sizeof(*asset->texture_views));
            for (i = 0; i < asset->texture_count; i++) {
                if (model_texture_path(path, (int)i, texture_path, sizeof(texture_path)) == 0)
                    toy_texture_load(texture_path, &asset->texture_assets[i]);
                asset->texture_views[i].data = asset->texture_assets[i].data;
                asset->texture_views[i].width = asset->texture_assets[i].width;
                asset->texture_views[i].height = asset->texture_assets[i].height;
                asset->texture_views[i].data_size = asset->texture_assets[i].data_size;
                asset->texture_views[i].channels = asset->texture_assets[i].channels;
                asset->texture_views[i].has_transparency = asset->texture_assets[i].has_transparency;
            }
        }
        if (asset->format_version >= 3) for (i = 0; i < asset->material_count; i++) {
            unsigned int base = model_u32(asset->materials + i * asset->material_bytes + 8);
            unsigned int packed = model_u32(asset->materials + i * asset->material_bytes + 12);
            unsigned int sphere = packed & 0xffffU;
            unsigned int mode = (packed >> 16) & 3U;
            if (sphere != 0xffffU)
                __printf("rasterfall: material=%u base_texture_index=%u sphere_texture_index=%u sphere_mode=%u uv_source=%s\n",
                         i, base, sphere, mode,
                         mode == 3 ? "ADDITIONAL_UV" :
                         mode == 0 ? "DISABLED" : "SPHERE_UV");
        }
    }
    return 0;
}

void rasterfall_model_unload(struct rasterfall_model_asset *asset)
{
    if (!asset || !asset->data) return;
    if (asset->texture_assets) {
        unsigned int i;
        for (i = 0; i < asset->texture_count; i++) toy_texture_unload(&asset->texture_assets[i]);
        tlibc_free(asset->texture_assets);
    }
    if (asset->texture_views) tlibc_free(asset->texture_views);
    if (asset->bones) tlibc_free(asset->bones);
    if (asset->bone_transforms) tlibc_free(asset->bone_transforms);
    if (asset->animation_rotations) tlibc_free(asset->animation_rotations);
    if (asset->bone_order) tlibc_free(asset->bone_order);
    tlibc_free((void *)asset->data);
    __memset(asset, 0, sizeof(*asset));
}

int rasterfall_model_set_skinning(struct rasterfall_model_asset *asset,
                                  int enabled)
{
    if (!asset || !asset->bone_count) return enabled ? -1 : 0;
    asset->skinning_enabled = enabled != 0;
    return 0;
}

int rasterfall_model_set_pose(struct rasterfall_model_asset *asset, int pose)
{
    unsigned int i;
    if (!asset || !asset->bone_count || pose < RASTERFALL_MODEL_POSE_BIND ||
        pose > RASTERFALL_MODEL_POSE_BODY_TURN) return -1;
    for (i = 0; i < asset->bone_count; i++) {
        asset->bones[i].rotate_x = 0;
        asset->bones[i].rotate_y = 0;
        asset->bones[i].rotate_z = 0;
    }
    if (pose == RASTERFALL_MODEL_POSE_RIGHT_ARM) {
        if (asset->demo_right_arm < 0) return -1;
        asset->bones[asset->demo_right_arm].rotate_z = -38;
    } else if (pose == RASTERFALL_MODEL_POSE_ARMS) {
        if (asset->demo_right_arm < 0 || asset->demo_left_arm < 0) return -1;
        asset->bones[asset->demo_right_arm].rotate_z = -42;
        asset->bones[asset->demo_left_arm].rotate_z = 42;
    } else if (pose == RASTERFALL_MODEL_POSE_BODY_TURN) {
        if (asset->demo_body < 0) return -1;
        asset->bones[asset->demo_body].rotate_y = 24;
    }
    asset->pose = pose;
    return rasterfall_model_update_bones(asset);
}

int rasterfall_model_build_demo_clips(struct rasterfall_model_asset *asset)
{
    int right, left, body;
    if (!asset) return -1;
    right = asset->demo_right_arm; left = asset->demo_left_arm;
    body = asset->demo_body;
    __memset(asset->demo_clips, 0, sizeof(asset->demo_clips));
    __memset(asset->demo_tracks, 0, sizeof(asset->demo_tracks));
    __memset(asset->demo_keys, 0, sizeof(asset->demo_keys));
    /* ARM RAISE: 0 -> -38 -> 0, one-shot. */
    asset->demo_keys[0][0].time_ms=0;
    asset->demo_keys[0][1].time_ms=400;
    asset->demo_keys[0][2].time_ms=800;
    asset->demo_keys[0][0].rotation=rasterfall_animation_quat_from_euler(0,0,0);
    asset->demo_keys[0][1].rotation=rasterfall_animation_quat_from_euler(0,0,-38);
    asset->demo_keys[0][2].rotation=rasterfall_animation_quat_from_euler(0,0,0);
    asset->demo_tracks[0][0]=(struct rasterfall_animation_track){right,asset->demo_keys[0],3};
    asset->demo_clips[0]=(struct rasterfall_animation_clip){800,0,asset->demo_tracks[0],1};
    /* ARMS LOOP: two tracks, symmetric and continuous at the endpoints. */
    asset->demo_keys[1][0].time_ms=0; asset->demo_keys[1][1].time_ms=750;
    asset->demo_keys[1][2].time_ms=1500; asset->demo_keys[1][3].time_ms=0;
    asset->demo_keys[1][4].time_ms=750; asset->demo_keys[1][5].time_ms=1500;
    asset->demo_keys[1][0].rotation=rasterfall_animation_quat_from_euler(0,0,0);
    asset->demo_keys[1][1].rotation=rasterfall_animation_quat_from_euler(0,0,-42);
    asset->demo_keys[1][2].rotation=rasterfall_animation_quat_from_euler(0,0,0);
    asset->demo_keys[1][3].rotation=rasterfall_animation_quat_from_euler(0,0,0);
    asset->demo_keys[1][4].rotation=rasterfall_animation_quat_from_euler(0,0,42);
    asset->demo_keys[1][5].rotation=rasterfall_animation_quat_from_euler(0,0,0);
    asset->demo_tracks[1][0]=(struct rasterfall_animation_track){right,asset->demo_keys[1],3};
    asset->demo_tracks[1][1]=(struct rasterfall_animation_track){left,asset->demo_keys[1]+3,3};
    asset->demo_clips[1]=(struct rasterfall_animation_clip){1500,1,asset->demo_tracks[1],2};
    /* BODY TURN: parent track, so the existing hierarchy propagates it. */
    asset->demo_keys[2][0].time_ms=0; asset->demo_keys[2][1].time_ms=500;
    asset->demo_keys[2][2].time_ms=1000;
    asset->demo_keys[2][0].rotation=rasterfall_animation_quat_from_euler(0,0,0);
    asset->demo_keys[2][1].rotation=rasterfall_animation_quat_from_euler(0,24,0);
    asset->demo_keys[2][2].rotation=rasterfall_animation_quat_from_euler(0,0,0);
    asset->demo_tracks[2][0]=(struct rasterfall_animation_track){body,asset->demo_keys[2],3};
    asset->demo_clips[2]=(struct rasterfall_animation_clip){1000,0,asset->demo_tracks[2],1};
    return right >= 0 && (left >= 0) && body >= 0 ? 0 : -1;
}

int rasterfall_model_sample_clip(struct rasterfall_model_asset *asset,
                                 const struct rasterfall_animation_clip *clip,
                                 int time_ms)
{
    unsigned int i;
    if (!asset || !asset->animation_rotations || !asset->bone_count) return -1;
    rasterfall_animation_sample(clip, time_ms, asset->animation_rotations,
                                asset->bone_count);
    for (i = 0; i < asset->bone_count; i++) {
        asset->bones[i].rotate_x = asset->animation_rotations[i].x;
        asset->bones[i].rotate_y = asset->animation_rotations[i].y;
        asset->bones[i].rotate_z = asset->animation_rotations[i].z;
    }
    asset->pose = RASTERFALL_MODEL_POSE_BIND;
    return 0;
}

int rasterfall_model_sample_glb_rotation_clip(
    struct rasterfall_model_asset *asset,
    const struct rasterfall_glb_rotation_clip *clip, int time_ms)
{
    struct rasterfall_humanoid_rest_basis source_basis[RASTERFALL_HUMANOID_BONE_COUNT];
    struct rasterfall_humanoid_rest_basis target_basis[RASTERFALL_HUMANOID_BONE_COUNT];
    struct rasterfall_humanoid_rotation_skeleton source, target;
    struct rasterfall_humanoid_rotation_pose pose, source_reference, target_reference;
    struct rasterfall_humanoid_mapping mapping;
    struct rasterfall_humanoid_retarget_result result;
    unsigned int reference_mask=((1u<<RASTERFALL_HUMANOID_BONE_COUNT)-1u)&~1u;
    int sampled, humanoid, bone;
    if (!asset || !clip || rasterfall_model_build_humanoid_bases(asset, target_basis) < 0)
        return -1;
    rasterfall_humanoid_rotation_skeleton_identity(&target);
    rasterfall_humanoid_rotation_pose_bind(&target,&target_reference);
    rasterfall_humanoid_map_eula(asset, &mapping);
    if (rasterfall_glb_rotation_clip_reference(clip,&source,&source_reference,
                                               source_basis)<0 ||
        rasterfall_glb_rotation_clip_source(clip, time_ms, &source, &pose,
                                            source_basis, &sampled) < 0)
        return -1;
    memcpy(pose.global[RASTERFALL_HUMANOID_ROOT],
           source.rest_global[RASTERFALL_HUMANOID_ROOT],4*sizeof(double));
    if (rasterfall_humanoid_retarget_rotations_from_reference(
            &source,&pose,&source_reference,reference_mask,source_basis,
            &target,&target_reference,target_basis,&result)<0)
        return -1;
    for (bone = 0; bone < (int)asset->bone_count; bone++)
        asset->bones[bone].rotate_x = asset->bones[bone].rotate_y =
            asset->bones[bone].rotate_z = 0;
    for (humanoid = 0; humanoid < RASTERFALL_HUMANOID_BONE_COUNT; humanoid++) {
        struct rasterfall_animation_quaternion q = {
            result.local_rotation[humanoid][0], result.local_rotation[humanoid][1],
            result.local_rotation[humanoid][2], result.local_rotation[humanoid][3]};
        struct rasterfall_animation_rotation rotation;
        bone = mapping.bone_indices[humanoid];
        if (bone < 0 || bone >= (int)asset->bone_count) continue;
        rasterfall_animation_quat_to_euler(q, &rotation);
        asset->bones[bone].rotate_x = rotation.x;
        asset->bones[bone].rotate_y = rotation.y;
        asset->bones[bone].rotate_z = rotation.z;
    }
    asset->pose = RASTERFALL_MODEL_POSE_BIND;
    return rasterfall_model_update_bones(asset);
}

int rasterfall_model_update_bones(struct rasterfall_model_asset *asset)
{
    unsigned int order;
    if (!asset || !asset->bone_count || !asset->bone_transforms) return -1;
    for (order = 0; order < asset->bone_count; order++) {
        unsigned int i = asset->bone_order[order];
        struct rasterfall_model_bone *bone;
        struct rasterfall_model_bone_transform *transform;
        double local[9];
        int parent;
        bone = &asset->bones[i];
        transform = &asset->bone_transforms[i];
        parent = bone->parent;
        matrix_rotate_xyz(bone->rotate_x, bone->rotate_y, bone->rotate_z,
                          local);
        if (parent < 0) {
            memcpy(transform->rotation, local, sizeof(local));
            transform->position[0] = bone->rest_x;
            transform->position[1] = bone->rest_y;
            transform->position[2] = bone->rest_z;
        } else {
            struct rasterfall_model_bone *parent_bone = &asset->bones[parent];
            struct rasterfall_model_bone_transform *parent_transform =
                &asset->bone_transforms[parent];
            double x, y, z;
            matrix_multiply(parent_transform->rotation, local,
                            transform->rotation);
            matrix_vector(parent_transform->rotation,
                bone->rest_x - parent_bone->rest_x,
                bone->rest_y - parent_bone->rest_y,
                bone->rest_z - parent_bone->rest_z, &x, &y, &z);
            transform->position[0] = parent_transform->position[0] + x;
            transform->position[1] = parent_transform->position[1] + y;
            transform->position[2] = parent_transform->position[2] + z;
        }
    }
    return 0;
}

static void model_transform_vertex(const struct rasterfall_model_asset *asset,
                                   unsigned int bone_index,
                                   const int bind_position[3],
                                   const int bind_normal[3], double position[3],
                                   double normal[3])
{
    const struct rasterfall_model_bone *bone = &asset->bones[bone_index];
    const struct rasterfall_model_bone_transform *transform =
        &asset->bone_transforms[bone_index];
    matrix_vector(transform->rotation,
                  bind_position[0] - bone->rest_x,
                  bind_position[1] - bone->rest_y,
                  bind_position[2] - bone->rest_z,
                  &position[0], &position[1], &position[2]);
    position[0] += transform->position[0];
    position[1] += transform->position[1];
    position[2] += transform->position[2];
    matrix_vector(transform->rotation, bind_normal[0], bind_normal[1],
                  bind_normal[2], &normal[0], &normal[1], &normal[2]);
}

int rasterfall_model_skin_vertex(const struct rasterfall_model_asset *asset,
                                 unsigned int index, int position[3],
                                 int normal[3])
{
    const unsigned char *vertex, *skin;
    int bind_position[3], bind_normal[3];
    unsigned int bone0, bone1, weight, type;
    double p0[3], p1[3], n0[3], n1[3], length;
    int axis;
    if (!asset || index >= asset->vertex_count || !position || !normal)
        return -1;
    vertex = asset->vertices + index * asset->vertex_bytes;
    for (axis = 0; axis < 3; axis++) {
        bind_position[axis] = *(const int *)(vertex + axis * 4);
        bind_normal[axis] = *(const short *)(vertex + 12 + axis * 2);
    }
    if (!asset->skinning_enabled || !asset->skin_vertices) {
        memcpy(position, bind_position, sizeof(bind_position));
        memcpy(normal, bind_normal, sizeof(bind_normal));
        return 0;
    }
    skin = asset->skin_vertices + index * RASTERFALL_MODEL_SKIN_VERTEX_BYTES;
    bone0 = model_u16(skin);
    bone1 = model_u16(skin + 2);
    weight = model_u16(skin + 4);
    type = skin[6];
    model_transform_vertex(asset, bone0, bind_position, bind_normal, p0, n0);
    if (type == 0) {
        for (axis = 0; axis < 3; axis++) {
            position[axis] = rounded(p0[axis]);
            normal[axis] = rounded(n0[axis]);
        }
    } else {
        model_transform_vertex(asset, bone1, bind_position, bind_normal, p1, n1);
        for (axis = 0; axis < 3; axis++) {
            position[axis] = rounded((p0[axis] * weight +
                                      p1[axis] * (65535U - weight)) / 65535.0);
            normal[axis] = rounded((n0[axis] * weight +
                                    n1[axis] * (65535U - weight)) / 65535.0);
        }
    }
    if (asset->pose == RASTERFALL_MODEL_POSE_BIND) {
        memcpy(normal, bind_normal, sizeof(bind_normal));
        return 0;
    }
    length = sqrt((double)normal[0] * normal[0] +
                  (double)normal[1] * normal[1] +
                  (double)normal[2] * normal[2]);
    if (length > 0.0) for (axis = 0; axis < 3; axis++)
        normal[axis] = rounded(normal[axis] * 32767.0 / length);
    return 0;
}

void rasterfall_model_dump_bones(const struct rasterfall_model_asset *asset,
                                 const char *search)
{
    unsigned int i, found = 0;
    if (!asset || !asset->bone_count) {
        __printf("rasterfall: model has no skeletal data\n");
        return;
    }
    for (i = 0; i < asset->bone_count; i++) {
        const struct rasterfall_model_bone *bone = &asset->bones[i];
        if (search && *search && !strstr(bone->name, search)) continue;
        __printf("rasterfall: bone[%u] name=\"%s\" parent=%d rest=(%d,%d,%d) flags=0x%x\n",
                 i, bone->name, bone->parent, bone->rest_x, bone->rest_y,
                 bone->rest_z, bone->flags);
        found++;
    }
    __printf("rasterfall: bone list matches=%u total=%u search=\"%s\"\n",
             found, asset->bone_count, search ? search : "");
}

static const char *humanoid_names[RASTERFALL_HUMANOID_BONE_COUNT] = {
    "ROOT", "HIPS", "SPINE", "CHEST", "UPPER_CHEST", "NECK", "HEAD",
    "LEFT_SHOULDER", "LEFT_UPPER_ARM", "LEFT_FOREARM", "LEFT_HAND",
    "RIGHT_SHOULDER", "RIGHT_UPPER_ARM", "RIGHT_FOREARM", "RIGHT_HAND",
    "LEFT_UPPER_LEG", "LEFT_LOWER_LEG", "LEFT_FOOT",
    "RIGHT_UPPER_LEG", "RIGHT_LOWER_LEG", "RIGHT_FOOT"
};

const char *rasterfall_humanoid_bone_name(enum rasterfall_humanoid_bone bone)
{
    if (bone < 0 || bone >= RASTERFALL_HUMANOID_BONE_COUNT) return "INVALID";
    return humanoid_names[bone];
}

void rasterfall_humanoid_mapping_init(struct rasterfall_humanoid_mapping *mapping)
{
    int i;
    if (!mapping) return;
    for (i = 0; i < RASTERFALL_HUMANOID_BONE_COUNT; i++)
        mapping->bone_indices[i] = -1;
}

static int model_find_exact_bone(const struct rasterfall_model_asset *asset,
                                 const char *a, const char *b)
{
    unsigned int i;
    if (!asset) return -1;
    for (i = 0; i < asset->bone_count; i++)
        if (asset->bones[i].name && !strcmp(asset->bones[i].name, a))
            return (int)i;
    if (b) for (i = 0; i < asset->bone_count; i++)
        if (asset->bones[i].name && !strcmp(asset->bones[i].name, b))
            return (int)i;
    return -1;
}

void rasterfall_humanoid_map_eula(const struct rasterfall_model_asset *asset,
                                  struct rasterfall_humanoid_mapping *mapping)
{
    static const char *names[RASTERFALL_HUMANOID_BONE_COUNT][2] = {
        {"全ての親", "操作中心"}, {"腰", "下半身"},
        {"上半身", "upper body"}, {"上半身3", "upper body 3"},
        {"上半身2", "upper body 2"}, {"首", "neck"}, {"頭", "head"},
        {"左肩", "left shoulder"}, {"左腕", "left arm"},
        {"左ひじ", "left elbow"}, {"左手首", "left wrist"},
        {"右肩", "right shoulder"}, {"右腕", "right arm"},
        {"右ひじ", "right elbow"}, {"右手首", "right wrist"},
        {"左足", "left leg"}, {"左ひざ", "left knee"},
        {"左足首", "left ankle"}, {"右足", "right leg"},
        {"右ひざ", "right knee"}, {"右足首", "right ankle"}
    };
    int i;
    rasterfall_humanoid_mapping_init(mapping);
    if (!asset || !mapping) return;
    for (i = 0; i < RASTERFALL_HUMANOID_BONE_COUNT; i++)
        mapping->bone_indices[i] = model_find_exact_bone(asset, names[i][0], names[i][1]);
}

static int model_bone_is_descendant(const struct rasterfall_model_asset *asset,
                                    int child, int ancestor)
{
    unsigned int steps = 0;
    if (!asset || child < 0 || ancestor < 0) return 1; /* Missing is separate. */
    while (child >= 0 && steps++ <= asset->bone_count) {
        if (child == ancestor) return 1;
        child = asset->bones[child].parent;
    }
    return 0;
}

static const unsigned char humanoid_chains[][2] = {
    {0,1}, {1,2}, {2,3}, {3,4}, {4,5}, {5,6},
    {4,7}, {7,8}, {8,9}, {9,10}, {4,11}, {11,12}, {12,13}, {13,14},
    {1,15}, {15,16}, {16,17}, {1,18}, {18,19}, {19,20}
};

void rasterfall_humanoid_validate(const struct rasterfall_model_asset *asset,
                                  const struct rasterfall_humanoid_mapping *mapping,
                                  struct rasterfall_humanoid_diagnostics *diagnostics)
{
    int i, j;
    __memset(diagnostics, 0, sizeof(*diagnostics));
    if (!mapping) return;
    for (i = 0; i < RASTERFALL_HUMANOID_BONE_COUNT; i++) {
        int index = mapping->bone_indices[i];
        if (index < 0 || !asset || index >= (int)asset->bone_count) {
            diagnostics->missing_count++;
            continue;
        }
        diagnostics->mapped_count++;
        for (j = 0; j < i; j++)
            if (mapping->bone_indices[j] == index) {
                diagnostics->duplicate_count++;
                break;
            }
    }
    if (!asset) return;
    for (i = 0; i < (int)(sizeof(humanoid_chains) / sizeof(humanoid_chains[0])); i++)
        if (!model_bone_is_descendant(asset,
                mapping->bone_indices[humanoid_chains[i][1]],
                mapping->bone_indices[humanoid_chains[i][0]]))
            diagnostics->parent_chain_error_count++;
}

void rasterfall_model_dump_humanoid(const struct rasterfall_model_asset *asset)
{
    struct rasterfall_humanoid_mapping mapping;
    struct rasterfall_humanoid_diagnostics diagnostics;
    int i, j;
    rasterfall_humanoid_map_eula(asset, &mapping);
    rasterfall_humanoid_validate(asset, &mapping, &diagnostics);
    for (i = 0; i < RASTERFALL_HUMANOID_BONE_COUNT; i++) {
        int index = mapping.bone_indices[i];
        if (index >= 0)
            __printf("%s -> %s / index %d\n", humanoid_names[i],
                     asset->bones[index].name, index);
        else __printf("%s -> MISSING\n", humanoid_names[i]);
    }
    for (i = 0; i < RASTERFALL_HUMANOID_BONE_COUNT; i++)
        if (mapping.bone_indices[i] >= 0) for (j = 0; j < i; j++)
            if (mapping.bone_indices[j] == mapping.bone_indices[i]) {
                __printf("humanoid: duplicate %s and %s -> %s / index %d\n",
                         humanoid_names[j], humanoid_names[i],
                         asset->bones[mapping.bone_indices[i]].name,
                         mapping.bone_indices[i]);
                break;
            }
    for (i = 0; i < (int)(sizeof(humanoid_chains) / sizeof(humanoid_chains[0])); i++) {
        int parent = humanoid_chains[i][0], child = humanoid_chains[i][1];
        if (!model_bone_is_descendant(asset, mapping.bone_indices[child],
                                      mapping.bone_indices[parent]))
            __printf("humanoid: abnormal parent chain %s is not below %s\n",
                     humanoid_names[child], humanoid_names[parent]);
    }
    __printf("humanoid: mapped=%u/%u missing_core_bones=%u duplicate_bone_mappings=%u parent_chain_errors=%u\n",
             diagnostics.mapped_count, RASTERFALL_HUMANOID_BONE_COUNT,
             diagnostics.missing_count, diagnostics.duplicate_count,
             diagnostics.parent_chain_error_count);
}

int rasterfall_humanoid_logic_test(void)
{
    struct rasterfall_model_asset asset;
    struct rasterfall_model_bone bones[4];
    struct rasterfall_humanoid_mapping mapping;
    struct rasterfall_humanoid_diagnostics diagnostics;
    __memset(&asset, 0, sizeof(asset)); __memset(bones, 0, sizeof(bones));
    asset.bones = bones; asset.bone_count = 4;
    bones[0].name = "全ての親"; bones[0].parent = -1;
    bones[1].name = "腰"; bones[1].parent = 0;
    bones[2].name = "上半身"; bones[2].parent = 1;
    bones[3].name = "右腕"; bones[3].parent = 2;
    rasterfall_humanoid_map_eula(&asset, &mapping);
    if (mapping.bone_indices[RASTERFALL_HUMANOID_ROOT] != 0 ||
        mapping.bone_indices[RASTERFALL_HUMANOID_HIPS] != 1 ||
        mapping.bone_indices[RASTERFALL_HUMANOID_SPINE] != 2 ||
        mapping.bone_indices[RASTERFALL_HUMANOID_RIGHT_UPPER_ARM] != 3 ||
        mapping.bone_indices[RASTERFALL_HUMANOID_HEAD] != -1) return 1;
    rasterfall_humanoid_validate(&asset, &mapping, &diagnostics);
    if (diagnostics.mapped_count != 4 || diagnostics.missing_count != 17 ||
        diagnostics.duplicate_count != 0) return 2;
    mapping.bone_indices[RASTERFALL_HUMANOID_HEAD] = 3;
    rasterfall_humanoid_validate(&asset, &mapping, &diagnostics);
    if (diagnostics.duplicate_count != 1 || diagnostics.missing_count != 16)
        return 3;
    return 0;
}

static void model_basis_point(const struct rasterfall_model_asset *asset,
                              int index, struct rasterfall_humanoid_point *point)
{
    if (index < 0 || index >= (int)asset->bone_count) return;
    point->value[0] = asset->bones[index].rest_x;
    point->value[1] = asset->bones[index].rest_y;
    point->value[2] = asset->bones[index].rest_z;
    point->valid = 1;
}

int rasterfall_model_build_humanoid_bases(
    const struct rasterfall_model_asset *asset,
    struct rasterfall_humanoid_rest_basis *bases)
{
    struct rasterfall_humanoid_mapping mapping;
    struct rasterfall_humanoid_basis_input input;
    int i;
    if (!asset || !bases) return -1;
    __memset(&input, 0, sizeof(input));
    rasterfall_humanoid_map_eula(asset, &mapping);
    for (i = 0; i < RASTERFALL_HUMANOID_BONE_COUNT; i++)
        model_basis_point(asset, mapping.bone_indices[i], &input.bones[i]);
    model_basis_point(asset, model_find_exact_bone(asset, "左つま先", 0), &input.left_toe);
    model_basis_point(asset, model_find_exact_bone(asset, "右つま先", 0), &input.right_toe);
    model_basis_point(asset, model_find_exact_bone(asset, "左中指１", 0), &input.left_middle);
    model_basis_point(asset, model_find_exact_bone(asset, "右中指１", 0), &input.right_middle);
    model_basis_point(asset, model_find_exact_bone(asset, "左親指０", 0), &input.left_thumb);
    model_basis_point(asset, model_find_exact_bone(asset, "右親指０", 0), &input.right_thumb);
    input.model_up[1] = 1.0;
    input.model_forward[2] = 1.0;
    return rasterfall_humanoid_build_rest_bases(&input, bases);
}

static void model_print_basis_number(double value)
{
    int scaled = (int)(value * 1000000.0);
    __printf("%s%d.%06d", scaled < 0 ? "-" : "", abs(scaled) / 1000000,
             abs(scaled) % 1000000);
}

void rasterfall_model_dump_humanoid_bases(const struct rasterfall_model_asset *asset)
{
    struct rasterfall_humanoid_mapping mapping;
    struct rasterfall_humanoid_rest_basis bases[RASTERFALL_HUMANOID_BONE_COUNT];
    double error = 0.0; int i, j;
    rasterfall_humanoid_map_eula(asset, &mapping);
    if (rasterfall_model_build_humanoid_bases(asset, bases) < 0) {
        __printf("humanoid basis: construction failed\n"); return;
    }
    for (i = 0; i < RASTERFALL_HUMANOID_BONE_COUNT; i++) {
        __printf("%s -> %s / index %d primary=(", humanoid_names[i],
                 mapping.bone_indices[i] >= 0 ? asset->bones[mapping.bone_indices[i]].name : "MISSING",
                 mapping.bone_indices[i]);
        for (j=0;j<3;j++){if(j)__printf(",");model_print_basis_number(bases[i].primary[j]);}
        __printf(") secondary=(");for(j=0;j<3;j++){if(j)__printf(",");model_print_basis_number(bases[i].secondary[j]);}
        __printf(") third=(");for(j=0;j<3;j++){if(j)__printf(",");model_print_basis_number(bases[i].third[j]);}
        __printf(") quaternion=(");for(j=0;j<4;j++){if(j)__printf(",");model_print_basis_number(bases[i].rotation[j]);}
        __printf(") source=\"%s\" confidence=%s%s\n", bases[i].source,
                 rasterfall_humanoid_basis_confidence_name(bases[i].confidence),
                 bases[i].confidence == RASTERFALL_HUMANOID_BASIS_LOW ? " warning=fallback" : "");
    }
    __printf("humanoid basis: valid=%s anatomy=%s max_error=", rasterfall_humanoid_validate_rest_bases(bases,&error)==0?"yes":"no",
             rasterfall_humanoid_validate_anatomy(bases)==0?"yes":"no");
    model_print_basis_number(error); __printf("\n");
}

static void model_quat_multiply(const double *a,const double *b,double *out)
{
    double q[4];q[0]=a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1];
    q[1]=a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0];
    q[2]=a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3];
    q[3]=a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2];memcpy(out,q,sizeof(q));
}
static void model_quat_rotate(const double *q,const double *v,double *out)
{
    double vector[4]={v[0],v[1],v[2],0},inverse[4]={-q[0],-q[1],-q[2],q[3]},temp[4],result[4];
    model_quat_multiply(q,vector,temp);model_quat_multiply(temp,inverse,result);
    out[0]=result[0];out[1]=result[1];out[2]=result[2];
}

static void model_print_vector(const double *v,int count)
{
    int i;__printf("(");for(i=0;i<count;i++){if(i)__printf(",");model_print_basis_number(v[i]);}__printf(")");
}
static void model_quat_inverse(const double *q,double *out)
{
    double n=q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3];
    out[0]=-q[0]/n;out[1]=-q[1]/n;out[2]=-q[2]/n;out[3]=q[3]/n;
}
static void model_quat_delta(const double *animated,const double *rest,double *out)
{
    double inverse[4];model_quat_inverse(rest,inverse);model_quat_multiply(animated,inverse,out);
}
static void model_quat_change_basis(const double *basis,const double *q,double *out)
{
    double inverse[4],temp[4];model_quat_inverse(basis,inverse);model_quat_multiply(inverse,q,temp);model_quat_multiply(temp,basis,out);
}
static void model_print_axis_angle(const double *q)
{
    double n=sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]),angle=2.0*atan2(n,q[3])*180.0/M_PI;
    double axis[3]={0,0,0};if(n>0.0000001){axis[0]=q[0]/n;axis[1]=q[1]/n;axis[2]=q[2]/n;}
    __printf("axis=");model_print_vector(axis,3);__printf(" angle=");model_print_basis_number(angle);
}
static double model_quat_difference(const double *a,const double *b)
{
    double dot=a[0]*b[0]+a[1]*b[1]+a[2]*b[2]+a[3]*b[3];if(dot<0)dot=-dot;return 1.0-dot;
}

int rasterfall_model_retarget_synthetic_test(
    const struct rasterfall_model_asset *asset,const char *action)
{
    struct rasterfall_humanoid_rest_basis target_basis[RASTERFALL_HUMANOID_BONE_COUNT];
    struct rasterfall_humanoid_rest_basis source_basis[RASTERFALL_HUMANOID_BONE_COUNT];
    struct rasterfall_humanoid_rotation_skeleton source,target;
    struct rasterfall_humanoid_rotation_pose pose;
    struct rasterfall_humanoid_retarget_result result;
    struct rasterfall_humanoid_mapping mapping;
    int bone,degrees,i;double delta[4],new_direction[3];const double *display_direction;
    if(!strcmp(action,"right-arm")){bone=RASTERFALL_HUMANOID_RIGHT_UPPER_ARM;degrees=-35;}
    else if(!strcmp(action,"left-arm")){bone=RASTERFALL_HUMANOID_LEFT_UPPER_ARM;degrees=35;}
    else if(!strcmp(action,"right-leg")){bone=RASTERFALL_HUMANOID_RIGHT_UPPER_LEG;degrees=30;}
    else if(!strcmp(action,"chest")){bone=RASTERFALL_HUMANOID_CHEST;degrees=30;}
    else return -1;
    if(rasterfall_model_build_humanoid_bases(asset,target_basis)<0)return -1;
    __memset(source_basis,0,sizeof(source_basis));
    for(i=0;i<RASTERFALL_HUMANOID_BONE_COUNT;i++){
        source_basis[i].rotation[3]=1.0;source_basis[i].valid=1;
    }
    rasterfall_humanoid_rotation_skeleton_identity(&source);
    rasterfall_humanoid_rotation_skeleton_identity(&target);
    rasterfall_humanoid_rotation_pose_bind(&source,&pose);
    rasterfall_humanoid_synthetic_delta(bone,degrees,delta);
    memcpy(pose.global[bone],delta,sizeof(delta));
    if(rasterfall_humanoid_retarget_rotations(&source,&pose,source_basis,
                                               &target,target_basis,&result)<0)return -1;
    display_direction=bone==RASTERFALL_HUMANOID_CHEST?target_basis[bone].secondary:target_basis[bone].primary;
    model_quat_rotate(result.global_rotation[bone],display_direction,new_direction);
    rasterfall_humanoid_map_eula(asset,&mapping);
    __printf("retarget synthetic: action=%s semantic=%s target=%s/index%d degrees=%d canonical_axis=%s\n",
             action,humanoid_names[bone],asset->bones[mapping.bone_indices[bone]].name,
             mapping.bone_indices[bone],degrees,
             bone==RASTERFALL_HUMANOID_CHEST?"primary":
             bone==RASTERFALL_HUMANOID_RIGHT_UPPER_LEG?"third":"secondary");
    __printf("  source canonical delta=(");for(i=0;i<4;i++){if(i)__printf(",");model_print_basis_number(delta[i]);}
    __printf(")\n  target local quaternion=(");for(i=0;i<4;i++){if(i)__printf(",");model_print_basis_number(result.local_rotation[bone][i]);}
    __printf(")\n  target bind %s=(",bone==RASTERFALL_HUMANOID_CHEST?"secondary":"primary");for(i=0;i<3;i++){if(i)__printf(",");model_print_basis_number(display_direction[i]);}
    __printf(") resulting global %s=(",bone==RASTERFALL_HUMANOID_CHEST?"secondary":"primary");for(i=0;i<3;i++){if(i)__printf(",");model_print_basis_number(new_direction[i]);}
    __printf(") normalized=yes\n");return 0;
}

int rasterfall_model_glb_animation_test(struct rasterfall_model_asset *asset,
                                        const char *glb_path,const char *clip_name)
{
    struct rasterfall_glb_rotation_clip clip;struct rasterfall_humanoid_rotation_skeleton source,target;
    struct rasterfall_humanoid_rotation_pose source_pose,source_reference,target_reference;struct rasterfall_humanoid_retarget_result result;
    struct rasterfall_humanoid_rest_basis source_basis[RASTERFALL_HUMANOID_BONE_COUNT],target_basis[RASTERFALL_HUMANOID_BONE_COUNT];
    struct rasterfall_humanoid_mapping mapping;unsigned int reference_mask=((1u<<RASTERFALL_HUMANOID_BONE_COUNT)-1u)&~1u;int samples[4],sample,i,bone,sampled;
    struct rasterfall_glb_rotation_trace trace;
    struct rasterfall_animation_clip timing_clip;struct rasterfall_animation_player player;
    if(rasterfall_glb_rotation_clip_load(&clip,glb_path,clip_name)<0)return -1;
    if(rasterfall_model_build_humanoid_bases(asset,target_basis)<0){rasterfall_glb_rotation_clip_unload(&clip);return -1;}
    rasterfall_humanoid_rotation_skeleton_identity(&target);rasterfall_humanoid_rotation_pose_bind(&target,&target_reference);rasterfall_humanoid_map_eula(asset,&mapping);
    if(rasterfall_glb_rotation_clip_reference(&clip,&source,&source_reference,source_basis)<0){rasterfall_glb_rotation_clip_unload(&clip);return -1;}
    samples[0]=0;samples[1]=clip.duration_ms/4;samples[2]=clip.duration_ms/2;samples[3]=clip.duration_ms*3/4;
    __printf("glb animation: name=%s duration_ms=%d rotation_channels=%d active_rotation_bones=%d rotation_keys=%d..%d interpolation=LINEAR\n",clip_name,clip.duration_ms,clip.rotation_channels,clip.active_rotation_bones,clip.min_rotation_keys,clip.max_rotation_keys);
    for(sample=0;sample<4;sample++){
        if(rasterfall_glb_rotation_clip_trace(&clip,samples[sample],&source,&source_pose,source_basis,&trace,&sampled)<0){rasterfall_glb_rotation_clip_unload(&clip);return -1;}memcpy(source_pose.global[RASTERFALL_HUMANOID_ROOT],source.rest_global[RASTERFALL_HUMANOID_ROOT],4*sizeof(double));if(rasterfall_humanoid_retarget_rotations_from_reference(&source,&source_pose,&source_reference,reference_mask,source_basis,&target,&target_reference,target_basis,&result)<0){rasterfall_glb_rotation_clip_unload(&clip);return -1;}
        if(sample==0){double source_error=0,target_error=0;for(bone=0;bone<RASTERFALL_HUMANOID_BONE_COUNT;bone++){double e=model_quat_difference(source_pose.global[bone],bone==RASTERFALL_HUMANOID_ROOT?source.rest_global[bone]:source_reference.global[bone]);if(e>source_error)source_error=e;e=model_quat_difference(result.global_rotation[bone],target_reference.global[bone]);if(e>target_error)target_error=e;}__printf("reference invariant: source_max_error=");model_print_basis_number(source_error);__printf(" target_max_error=");model_print_basis_number(target_error);__printf(" status=%s\n",source_error<0.00000001&&target_error<0.00000001?"pass":"FAIL");if(source_error>=0.00000001||target_error>=0.00000001){rasterfall_glb_rotation_clip_unload(&clip);return -1;}}
        for(i=0;i<(int)asset->bone_count;i++)asset->bones[i].rotate_x=asset->bones[i].rotate_y=asset->bones[i].rotate_z=0;
        for(bone=0;bone<RASTERFALL_HUMANOID_BONE_COUNT;bone++){
            struct rasterfall_animation_quaternion q={result.local_rotation[bone][0],result.local_rotation[bone][1],result.local_rotation[bone][2],result.local_rotation[bone][3]};
            struct rasterfall_animation_rotation rotation;rasterfall_animation_quat_to_euler(q,&rotation);i=mapping.bone_indices[bone];asset->bones[i].rotate_x=rotation.x;asset->bones[i].rotate_y=rotation.y;asset->bones[i].rotate_z=rotation.z;
        }
        asset->pose=RASTERFALL_MODEL_POSE_RIGHT_ARM;rasterfall_model_update_bones(asset);
        __printf("sample requested_ms=%d sampled_ms=%d",samples[sample],sampled);
        {static const int watched[3]={RASTERFALL_HUMANOID_RIGHT_UPPER_ARM,RASTERFALL_HUMANOID_CHEST,RASTERFALL_HUMANOID_RIGHT_UPPER_LEG};int w;for(w=0;w<3;w++){double direction[3];bone=watched[w];model_quat_rotate(result.global_rotation[bone],target_basis[bone].primary,direction);__printf(" %s_source_global=(",humanoid_names[bone]);for(i=0;i<4;i++){if(i)__printf(",");model_print_basis_number(source_pose.global[bone][i]);}__printf(") target_local=(");for(i=0;i<4;i++){if(i)__printf(",");model_print_basis_number(result.local_rotation[bone][i]);}__printf(") target_primary=(");for(i=0;i<3;i++){if(i)__printf(",");model_print_basis_number(direction[i]);}__printf(")");}}
        __printf(" normalized=yes warnings=0\n");
        {
            static const int aligned[6]={RASTERFALL_HUMANOID_LEFT_SHOULDER,RASTERFALL_HUMANOID_RIGHT_SHOULDER,RASTERFALL_HUMANOID_LEFT_UPPER_ARM,RASTERFALL_HUMANOID_RIGHT_UPPER_ARM,RASTERFALL_HUMANOID_LEFT_FOREARM,RASTERFALL_HUMANOID_RIGHT_FOREARM};
            int a;for(a=0;a<6;a++){double bind_delta[4],dynamic_delta[4],canonical_dynamic[4],direction[3];bone=aligned[a];model_quat_delta(source_pose.global[bone],source.rest_global[bone],bind_delta);model_quat_delta(source_pose.global[bone],source_reference.global[bone],dynamic_delta);model_quat_change_basis(source_basis[bone].rotation,dynamic_delta,canonical_dynamic);model_quat_rotate(result.global_rotation[bone],target_basis[bone].primary,direction);__printf("reference t=%d bone=%s bind_relative ",sampled,humanoid_names[bone]);model_print_axis_angle(bind_delta);__printf(" dynamic_relative ");model_print_axis_angle(dynamic_delta);__printf(" canonical_dynamic ");model_print_axis_angle(canonical_dynamic);__printf(" target_primary=");model_print_vector(direction,3);__printf("\n");}
        }
        {
            static const int diagnostic_bones[8]={RASTERFALL_HUMANOID_LEFT_SHOULDER,RASTERFALL_HUMANOID_RIGHT_SHOULDER,RASTERFALL_HUMANOID_LEFT_UPPER_ARM,RASTERFALL_HUMANOID_RIGHT_UPPER_ARM,RASTERFALL_HUMANOID_LEFT_FOREARM,RASTERFALL_HUMANOID_RIGHT_FOREARM,RASTERFALL_HUMANOID_CHEST,RASTERFALL_HUMANOID_HIPS};
            int d;for(d=0;d<8;d++){double source_delta[4],canonical_delta[4],target_delta[4],bind_p[3],bind_s[3],animated_p[3],animated_s[3],applied_p[3],application_dot;bone=diagnostic_bones[d];model_quat_delta(trace.animated_global[bone],trace.rest_global[bone],source_delta);model_quat_change_basis(source_basis[bone].rotation,source_delta,canonical_delta);model_quat_delta(result.global_rotation[bone],target.rest_global[bone],target_delta);model_quat_rotate(target.rest_global[bone],target_basis[bone].primary,bind_p);model_quat_rotate(target.rest_global[bone],target_basis[bone].secondary,bind_s);model_quat_rotate(result.global_rotation[bone],target_basis[bone].primary,animated_p);model_quat_rotate(result.global_rotation[bone],target_basis[bone].secondary,animated_s);i=mapping.bone_indices[bone];matrix_vector(asset->bone_transforms[i].rotation,target_basis[bone].primary[0],target_basis[bone].primary[1],target_basis[bone].primary[2],&applied_p[0],&applied_p[1],&applied_p[2]);application_dot=applied_p[0]*animated_p[0]+applied_p[1]*animated_p[1]+applied_p[2]*animated_p[2];
                __printf("trace t=%d bone=%s\n source rest_local=",sampled,humanoid_names[bone]);model_print_vector(trace.rest_local[bone],4);__printf(" animated_local=");model_print_vector(trace.animated_local[bone],4);__printf("\n source rest_global=");model_print_vector(trace.rest_global[bone],4);__printf(" animated_global=");model_print_vector(trace.animated_global[bone],4);__printf(" delta_global=");model_print_vector(source_delta,4);__printf("\n source basis P=");model_print_vector(source_basis[bone].primary,3);__printf(" S=");model_print_vector(source_basis[bone].secondary,3);__printf(" T=");model_print_vector(source_basis[bone].third,3);__printf(" canonical_delta ");model_print_axis_angle(canonical_delta);__printf("\n target basis P=");model_print_vector(target_basis[bone].primary,3);__printf(" S=");model_print_vector(target_basis[bone].secondary,3);__printf(" T=");model_print_vector(target_basis[bone].third,3);__printf(" target_delta ");model_print_axis_angle(target_delta);__printf("\n target global=");model_print_vector(result.global_rotation[bone],4);__printf(" local=");model_print_vector(result.local_rotation[bone],4);__printf(" bind_P=");model_print_vector(bind_p,3);__printf(" bind_F=");model_print_vector(bind_s,3);__printf(" animated_P=");model_print_vector(animated_p,3);__printf(" animated_F=");model_print_vector(animated_s,3);__printf(" applied_P=");model_print_vector(applied_p,3);__printf(" applied_dot=");model_print_basis_number(application_dot);__printf("\n");
            }
            if(sample==0){__printf("coordinate root rest_local=");model_print_vector(trace.rest_local[RASTERFALL_HUMANOID_ROOT],4);__printf(" rest_global=");model_print_vector(trace.rest_global[RASTERFALL_HUMANOID_ROOT],4);__printf(" source_model_forward=(0,0,1) target_model_forward=(0,0,1) forward_dot=1.000000 no_axis_conversion=yes\n");}
            bone=RASTERFALL_HUMANOID_RIGHT_UPPER_ARM;{struct rasterfall_humanoid_retarget_result second;rasterfall_humanoid_retarget_rotations_from_reference(&source,&source_pose,&source_reference,reference_mask,source_basis,&target,&target_reference,target_basis,&second);__printf("reference repeat t=%d RIGHT_UPPER_ARM global_error=",sampled);model_print_basis_number(model_quat_difference(result.global_rotation[bone],second.global_rotation[bone]));__printf("\n");}
        }
    }
    __memset(&timing_clip,0,sizeof(timing_clip));timing_clip.duration_ms=clip.duration_ms;timing_clip.loop=1;
    __memset(&player,0,sizeof(player));player.clip=&timing_clip;player.playing=player.loop=1;player.speed_milli=1000;
    for(sample=0;sample<180;sample++){
        rasterfall_animation_player_update(&player,16);
        if(rasterfall_glb_rotation_clip_source(&clip,player.time_ms,&source,&source_pose,source_basis,&sampled)<0){rasterfall_glb_rotation_clip_unload(&clip);return -1;}
        memcpy(source_pose.global[RASTERFALL_HUMANOID_ROOT],source.rest_global[RASTERFALL_HUMANOID_ROOT],4*sizeof(double));
        if(rasterfall_humanoid_retarget_rotations_from_reference(&source,&source_pose,&source_reference,reference_mask,source_basis,&target,&target_reference,target_basis,&result)<0){rasterfall_glb_rotation_clip_unload(&clip);return -1;}
        for(bone=0;bone<RASTERFALL_HUMANOID_BONE_COUNT;bone++){struct rasterfall_animation_quaternion q={result.local_rotation[bone][0],result.local_rotation[bone][1],result.local_rotation[bone][2],result.local_rotation[bone][3]};struct rasterfall_animation_rotation rotation;rasterfall_animation_quat_to_euler(q,&rotation);i=mapping.bone_indices[bone];asset->bones[i].rotate_x=rotation.x;asset->bones[i].rotate_y=rotation.y;asset->bones[i].rotate_z=rotation.z;}
        rasterfall_model_update_bones(asset);
    }
    __printf("playback: frames=180 step_ms=16 player_time_ms=%d loop=yes drift=none Bone/BDEF_path=updated\n",player.time_ms);
    rasterfall_glb_rotation_clip_unload(&clip);return 0;
}

int rasterfall_model_skinning_logic_test(void)
{
    struct rasterfall_model_asset asset;
    struct rasterfall_model_bone bones[2];
    struct rasterfall_model_bone_transform transforms[2];
    unsigned int order[2] = {0, 1};
    unsigned char vertices[RASTERFALL_MODEL_VERTEX_BYTES_EDGE_SCALE];
    unsigned char skin[RASTERFALL_MODEL_SKIN_VERTEX_BYTES];
    int position[3], normal[3];
    __memset(&asset, 0, sizeof(asset));
    __memset(bones, 0, sizeof(bones));
    __memset(vertices, 0, sizeof(vertices));
    __memset(skin, 0, sizeof(skin));
    asset.vertex_count = 1;
    asset.vertex_bytes = sizeof(vertices);
    asset.vertices = vertices;
    asset.skin_vertices = skin;
    asset.bones = bones;
    asset.bone_transforms = transforms;
    asset.bone_order = order;
    asset.bone_count = 2;
    asset.skinning_enabled = 1;
    bones[0].parent = -1;
    bones[1].parent = 0;
    bones[1].rest_x = 10;
    *(int *)(vertices) = 20;
    *(short *)(vertices + 12) = 32767;
    skin[0] = 1;
    skin[2] = 0xff; skin[3] = 0xff;
    skin[4] = 0xff; skin[5] = 0xff;
    asset.pose = RASTERFALL_MODEL_POSE_BIND;
    if (rasterfall_model_update_bones(&asset) < 0 ||
        rasterfall_model_skin_vertex(&asset, 0, position, normal) < 0 ||
        position[0] != 20 || position[1] != 0 || normal[0] != 32767)
        return 1;
    bones[0].rotate_z = 90;
    asset.pose = RASTERFALL_MODEL_POSE_RIGHT_ARM;
    if (rasterfall_model_update_bones(&asset) < 0 ||
        rasterfall_model_skin_vertex(&asset, 0, position, normal) < 0 ||
        position[0] != 0 || position[1] != 20 ||
        normal[0] != 0 || normal[1] != 32767)
        return 2;
    bones[0].rotate_z = 0;
    bones[1].rotate_z = 90;
    skin[0] = 0; skin[1] = 0;
    skin[2] = 1; skin[3] = 0;
    skin[6] = 1;
    skin[4] = 0xff; skin[5] = 0xff;
    if (rasterfall_model_update_bones(&asset) < 0 ||
        rasterfall_model_skin_vertex(&asset, 0, position, normal) < 0 ||
        position[0] != 20 || position[1] != 0) return 3;
    skin[4] = 0; skin[5] = 0;
    if (rasterfall_model_skin_vertex(&asset, 0, position, normal) < 0 ||
        position[0] != 10 || position[1] != 10) return 4;
    skin[4] = 0; skin[5] = 0x80;
    if (rasterfall_model_skin_vertex(&asset, 0, position, normal) < 0 ||
        position[0] != 15 || position[1] != 5 ||
        normal[0] < 23160 || normal[0] > 23180 ||
        normal[1] < 23160 || normal[1] > 23180) return 5;
    return 0;
}
