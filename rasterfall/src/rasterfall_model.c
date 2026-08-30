#include "core.h"
#include "tlibc_everything.h"
#include "toy_assets.h"
#include "rasterfall_model.h"
#include "math.h"
#include "rasterfall_humanoid_retarget.h"
#include "rasterfall_glb_animation.h"
#include "rasterfall_vmd.h"

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

static float model_f32(const unsigned char *p)
{
    union { unsigned int u; float f; } value;
    value.u = model_u32(p);
    return value.f;
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
    unsigned int j;
    unsigned int expected_bone_bytes;
    if (bytes < RASTERFALL_MODEL_SKIN_HEADER_BYTES ||
        model_u32(skin) != RASTERFALL_MODEL_SKIN_MAGIC) return -1;
    bone_count = model_u32(skin + 8);
    bone_bytes = model_u32(skin + 12);
    vertex_count = model_u32(skin + 16);
    vertex_bytes = model_u32(skin + 20);
    names_bytes = model_u32(skin + 24);
    expected_bone_bytes = asset->format_version >= 13 ?
        RASTERFALL_MODEL_BONE_BYTES : RASTERFALL_MODEL_BONE_BYTES_LEGACY;
    required = RASTERFALL_MODEL_SKIN_HEADER_BYTES +
        (unsigned long)bone_count * bone_bytes +
        (unsigned long)vertex_count * vertex_bytes + names_bytes;
    if (model_u32(skin + 4) != bytes || required > bytes ||
        bone_count == 0 || bone_count > RASTERFALL_MODEL_MAX_BONES ||
        bone_bytes != expected_bone_bytes ||
        vertex_count != asset->vertex_count ||
        vertex_bytes != RASTERFALL_MODEL_SKIN_VERTEX_BYTES || !names_bytes)
        return -1;
    bone_data = skin + RASTERFALL_MODEL_SKIN_HEADER_BYTES;
    skin_vertices = bone_data + bone_count * bone_bytes;
    names = skin_vertices + vertex_count * vertex_bytes;
    asset->bones = tlibc_malloc((size_t)bone_count * sizeof(*asset->bones));
    asset->bone_transforms = tlibc_malloc((size_t)bone_count *
                                          sizeof(*asset->bone_transforms));
    asset->animation.rotations = tlibc_malloc((size_t)bone_count *
                                              sizeof(*asset->animation.rotations));
    if (!asset->bones || !asset->bone_transforms || !asset->animation.rotations)
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
        asset->bones[i].grant_parent = -1;
        asset->bones[i].grant_ratio = 0.0f;
        asset->bones[i].grant_rotation_enabled =
            (asset->bones[i].flags & 0x0100) != 0;
        asset->bones[i].grant_translation_enabled =
            (asset->bones[i].flags & 0x0200) != 0;
        if (bone_bytes >= RASTERFALL_MODEL_BONE_BYTES) {
            unsigned int grant_parent = model_u32(record + 24);
            float ratio = model_f32(record + 28);
            if (grant_parent != 0xffffffffU && grant_parent >= bone_count)
                return -1;
            if (ratio != ratio || ratio < -1000000.0f || ratio > 1000000.0f)
                return -1;
            asset->bones[i].grant_parent = grant_parent == 0xffffffffU ?
                -1 : (int)grant_parent;
            asset->bones[i].grant_ratio = ratio;
        }
    }
    {
        unsigned long used = (unsigned long)(names - skin) + names_bytes;
        unsigned long remaining = bytes >= used ? bytes - used : 0;
        if (remaining) {
            const unsigned char *section = skin + used;
            unsigned int ik_count, record_bytes, link_bytes, total_links;
            unsigned long records_end;
            if (remaining < RASTERFALL_MODEL_IK_HEADER_BYTES ||
                model_u32(section) != RASTERFALL_MODEL_IK_MAGIC ||
                model_u32(section + 4) != remaining ||
                (ik_count = model_u32(section + 8)) > bone_count ||
                (record_bytes = model_u32(section + 12)) != RASTERFALL_MODEL_IK_RECORD_BYTES ||
                (link_bytes = model_u32(section + 16)) != RASTERFALL_MODEL_IK_LINK_BYTES)
                return -1;
            records_end = RASTERFALL_MODEL_IK_HEADER_BYTES +
                (unsigned long)ik_count * record_bytes;
            if (records_end > remaining ||
                (remaining - records_end) % link_bytes) return -1;
            total_links = (unsigned int)((remaining - records_end) / link_bytes);
            asset->iks = ik_count ? tlibc_malloc(ik_count * sizeof(*asset->iks)) : 0;
            if (ik_count && !asset->iks) return -1;
            if (ik_count) __memset(asset->iks, 0, ik_count * sizeof(*asset->iks));
            asset->ik_count = ik_count;
            for (i = 0; i < ik_count; i++) {
                const unsigned char *record = section + RASTERFALL_MODEL_IK_HEADER_BYTES + i * record_bytes;
                unsigned int controller = model_u32(record), target = model_u32(record + 4);
                unsigned int count = model_u32(record + 16), start = model_u32(record + 20);
                if (controller >= bone_count || target >= bone_count ||
                    count > bone_count || start > total_links || count > total_links - start)
                    return -1;
                asset->iks[i].controller = (int)controller;
                asset->iks[i].target = (int)target;
                asset->iks[i].iterations = (int)model_u32(record + 8);
                asset->iks[i].angle = model_f32(record + 12);
                asset->iks[i].link_count = count;
                if (count) {
                    asset->iks[i].links = tlibc_malloc(count * sizeof(*asset->iks[i].links));
                    if (!asset->iks[i].links) return -1;
                    __memset(asset->iks[i].links, 0, count * sizeof(*asset->iks[i].links));
                }
                for (j = 0; j < count; j++) {
                    const unsigned char *link = section + records_end +
                        (start + j) * link_bytes;
                    unsigned int bone = model_u32(link), limited = model_u32(link + 4);
                    if (bone >= bone_count || limited > 1) return -1;
                    asset->iks[i].links[j].bone = (int)bone;
                    asset->iks[i].links[j].limited = (int)limited;
                    if (limited) {
                        int axis;
                        for (axis = 0; axis < 3; axis++) {
                            asset->iks[i].links[j].lower[axis] = model_f32(link + 8 + axis * 4);
                            asset->iks[i].links[j].upper[axis] = model_f32(link + 20 + axis * 4);
                        }
                    }
                }
            }
        }
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
    asset->animation.demo_right_arm = model_find_first_bone(asset, "右腕", "right arm", "RightArm");
    asset->animation.demo_left_arm = model_find_first_bone(asset, "左腕", "left arm", "LeftArm");
    asset->animation.demo_body = model_find_first_bone(asset, "上半身2", "upper body 2", "UpperBody2");
    if (asset->animation.demo_body < 0)
        asset->animation.demo_body = model_find_first_bone(asset, "上半身", "upper body", "UpperBody");
    rasterfall_model_build_demo_clips(asset);
    asset->skinning_enabled = 1;
    asset->animation.pose = RASTERFALL_MODEL_POSE_BIND;
    if (rasterfall_model_update_bones(asset) < 0) return -1;
    __printf("rasterfall: skeleton bones=%u roots=%u max_depth=%u BDEF1=%u BDEF2=%u invalid_bone_references=0\n",
             bone_count, asset->root_bone_count, asset->max_bone_depth,
             bdef1, bdef2);
    __printf("rasterfall: demo bones right_arm={index=%d,name=\"%s\"} left_arm={index=%d,name=\"%s\"} body={index=%d,name=\"%s\"}\n",
             asset->animation.demo_right_arm,
             asset->animation.demo_right_arm >= 0 ? asset->bones[asset->animation.demo_right_arm].name : "not found",
             asset->animation.demo_left_arm,
             asset->animation.demo_left_arm >= 0 ? asset->bones[asset->animation.demo_left_arm].name : "not found",
             asset->animation.demo_body,
             asset->animation.demo_body >= 0 ? asset->bones[asset->animation.demo_body].name : "not found");
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
    char *lod;
    if (n <= 0 || n >= (int)sizeof(base) || length >= size) return -1;
    memcpy(base, name, n); base[n] = 0;
    /* Generated mesh LODs and explicitly authored Lite meshes retain the
     * source material/texture table.  They therefore share the canonical
     * `character.textures/` directory instead of duplicating large assets. */
    lod = strstr(base, "_lod");
    if (lod && lod[4] >= '0' && lod[4] <= '9') *lod = 0;
    lod = strstr(base, "_lite");
    if (lod && lod[5] == 0) *lod = 0;
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
    asset->ik_enabled = 1;
    asset->ik_limits_enabled = 1;
    asset->root_motion.primary_bone = -1;
    asset->root_motion.secondary_bone = -1;
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
    asset->grant_enabled = version >= 13 ? 1 : 0;
    asset->ik_best_iteration_enabled = 1;
    asset->ik_previous_final_enabled = 1;
    /* Inspector trace points are opt-in.  Runtime must not treat time zero
     * as an active trace target after the asset's zero initialization. */
    asset->ik_analytic_trace_time_ms = -1;
    asset->ik_analytic_trace_side = -1;
    asset->ik_iteration_trace_time_ms = -1;
    asset->ik_handoff_trace_time_ms = -1;
    asset->ik_handoff_trace_side = -1;
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
            asset->textures.count = max_texture + 1;
            asset->textures.assets = (struct toy_texture_asset *)tlibc_malloc(asset->textures.count * sizeof(*asset->textures.assets));
            asset->textures.views = (struct toy_texture_view *)tlibc_malloc(asset->textures.count * sizeof(*asset->textures.views));
            if (!asset->textures.assets || !asset->textures.views) { rasterfall_model_unload(asset); return -1; }
            __memset(asset->textures.assets, 0, asset->textures.count * sizeof(*asset->textures.assets));
            __memset(asset->textures.views, 0, asset->textures.count * sizeof(*asset->textures.views));
            for (i = 0; i < asset->textures.count; i++) {
                if (model_texture_path(path, (int)i, texture_path, sizeof(texture_path)) == 0)
                    toy_texture_load(texture_path, &asset->textures.assets[i]);
                asset->textures.views[i].data = asset->textures.assets[i].data;
                asset->textures.views[i].width = asset->textures.assets[i].width;
                asset->textures.views[i].height = asset->textures.assets[i].height;
                asset->textures.views[i].data_size = asset->textures.assets[i].data_size;
                asset->textures.views[i].channels = asset->textures.assets[i].channels;
                asset->textures.views[i].has_transparency = asset->textures.assets[i].has_transparency;
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
    if (asset->textures.assets) {
        unsigned int i;
        for (i = 0; i < asset->textures.count; i++) toy_texture_unload(&asset->textures.assets[i]);
        tlibc_free(asset->textures.assets);
    }
    if (asset->textures.views) tlibc_free(asset->textures.views);
    if (asset->bones) tlibc_free(asset->bones);
    if (asset->bone_transforms) tlibc_free(asset->bone_transforms);
    if (asset->animation.rotations) tlibc_free(asset->animation.rotations);
    if (asset->bone_order) tlibc_free(asset->bone_order);
    if (asset->iks) {
        unsigned int i;
        for (i = 0; i < asset->ik_count; i++) if (asset->iks[i].links)
            tlibc_free(asset->iks[i].links);
        tlibc_free(asset->iks);
    }
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
        if (asset->animation.demo_right_arm < 0) return -1;
        asset->bones[asset->animation.demo_right_arm].rotate_z = -38;
    } else if (pose == RASTERFALL_MODEL_POSE_ARMS) {
        if (asset->animation.demo_right_arm < 0 || asset->animation.demo_left_arm < 0) return -1;
        asset->bones[asset->animation.demo_right_arm].rotate_z = -42;
        asset->bones[asset->animation.demo_left_arm].rotate_z = 42;
    } else if (pose == RASTERFALL_MODEL_POSE_BODY_TURN) {
        if (asset->animation.demo_body < 0) return -1;
        asset->bones[asset->animation.demo_body].rotate_y = 24;
    }
    asset->animation.pose = pose;
    return rasterfall_model_update_bones(asset);
}

int rasterfall_model_build_demo_clips(struct rasterfall_model_asset *asset)
{
    int right, left, body;
    if (!asset) return -1;
    right = asset->animation.demo_right_arm; left = asset->animation.demo_left_arm;
    body = asset->animation.demo_body;
    __memset(asset->animation.demo_clips, 0, sizeof(asset->animation.demo_clips));
    __memset(asset->animation.demo_tracks, 0, sizeof(asset->animation.demo_tracks));
    __memset(asset->animation.demo_keys, 0, sizeof(asset->animation.demo_keys));
    /* ARM RAISE: 0 -> -38 -> 0, one-shot. */
    asset->animation.demo_keys[0][0].time_ms=0;
    asset->animation.demo_keys[0][1].time_ms=400;
    asset->animation.demo_keys[0][2].time_ms=800;
    asset->animation.demo_keys[0][0].rotation=rasterfall_animation_quat_from_euler(0,0,0);
    asset->animation.demo_keys[0][1].rotation=rasterfall_animation_quat_from_euler(0,0,-38);
    asset->animation.demo_keys[0][2].rotation=rasterfall_animation_quat_from_euler(0,0,0);
    asset->animation.demo_tracks[0][0]=(struct rasterfall_animation_track){right,asset->animation.demo_keys[0],3};
    asset->animation.demo_clips[0]=(struct rasterfall_animation_clip){800,0,asset->animation.demo_tracks[0],1,1.0f};
    /* ARMS LOOP: two tracks, symmetric and continuous at the endpoints. */
    asset->animation.demo_keys[1][0].time_ms=0; asset->animation.demo_keys[1][1].time_ms=750;
    asset->animation.demo_keys[1][2].time_ms=1500; asset->animation.demo_keys[1][3].time_ms=0;
    asset->animation.demo_keys[1][4].time_ms=750; asset->animation.demo_keys[1][5].time_ms=1500;
    asset->animation.demo_keys[1][0].rotation=rasterfall_animation_quat_from_euler(0,0,0);
    asset->animation.demo_keys[1][1].rotation=rasterfall_animation_quat_from_euler(0,0,-42);
    asset->animation.demo_keys[1][2].rotation=rasterfall_animation_quat_from_euler(0,0,0);
    asset->animation.demo_keys[1][3].rotation=rasterfall_animation_quat_from_euler(0,0,0);
    asset->animation.demo_keys[1][4].rotation=rasterfall_animation_quat_from_euler(0,0,42);
    asset->animation.demo_keys[1][5].rotation=rasterfall_animation_quat_from_euler(0,0,0);
    asset->animation.demo_tracks[1][0]=(struct rasterfall_animation_track){right,asset->animation.demo_keys[1],3};
    asset->animation.demo_tracks[1][1]=(struct rasterfall_animation_track){left,asset->animation.demo_keys[1]+3,3};
    asset->animation.demo_clips[1]=(struct rasterfall_animation_clip){1500,1,asset->animation.demo_tracks[1],2,1.0f};
    /* BODY TURN: parent track, so the existing hierarchy propagates it. */
    asset->animation.demo_keys[2][0].time_ms=0; asset->animation.demo_keys[2][1].time_ms=500;
    asset->animation.demo_keys[2][2].time_ms=1000;
    asset->animation.demo_keys[2][0].rotation=rasterfall_animation_quat_from_euler(0,0,0);
    asset->animation.demo_keys[2][1].rotation=rasterfall_animation_quat_from_euler(0,24,0);
    asset->animation.demo_keys[2][2].rotation=rasterfall_animation_quat_from_euler(0,0,0);
    asset->animation.demo_tracks[2][0]=(struct rasterfall_animation_track){body,asset->animation.demo_keys[2],3};
    asset->animation.demo_clips[2]=(struct rasterfall_animation_clip){1000,0,asset->animation.demo_tracks[2],1,1.0f};
    return right >= 0 && (left >= 0) && body >= 0 ? 0 : -1;
}

static double model_vec_length(const double v[3])
{
    return sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}

/* Inspector-only bend branch classification.  This mirrors the diagnostic
 * definition used by vmd_inspect: it is never used to choose a solver pose. */
static int model_ccd_branch_sign(const struct rasterfall_model_asset *asset,
                                 const struct rasterfall_model_ik *ik,
                                 const double target[3])
{
    int knee=ik->links[0].bone;
    int thigh=ik->links[ik->link_count-1].bone;
    double axis[3], perp[3], ref[3], refp[3];
    double axis_len, perp_len, ref_len, projection, dot;
    int i;
    for(i=0;i<3;i++) {
        axis[i]=target[i]-asset->bone_transforms[thigh].position[i];
        perp[i]=asset->bone_transforms[knee].position[i]-asset->bone_transforms[thigh].position[i];
        ref[i]=(i==0 ? asset->bones[knee].rest_x-asset->bones[thigh].rest_x :
                i==1 ? asset->bones[knee].rest_y-asset->bones[thigh].rest_y :
                       asset->bones[knee].rest_z-asset->bones[thigh].rest_z) *
                      RASTERFALL_VMD_TRANSLATION_SCALE;
    }
    axis_len=model_vec_length(axis);
    if(axis_len<0.000001)return 0;
    for(i=0;i<3;i++)axis[i]/=axis_len;
    projection=perp[0]*axis[0]+perp[1]*axis[1]+perp[2]*axis[2];
    ref_len=model_vec_length(ref);
    for(i=0;i<3;i++) { perp[i]-=projection*axis[i]; refp[i]=ref[i]; }
    projection=refp[0]*axis[0]+refp[1]*axis[1]+refp[2]*axis[2];
    for(i=0;i<3;i++)refp[i]-=projection*axis[i];
    perp_len=model_vec_length(perp); ref_len=model_vec_length(refp);
    if(perp_len<0.000001 || ref_len<0.000001)return 0;
    for(i=0;i<3;i++){perp[i]/=perp_len;refp[i]/=ref_len;}
    dot=perp[0]*refp[0]+perp[1]*refp[1]+perp[2]*refp[2];
    if(dot>0.15)return 1;
    if(dot<-0.15)return -1;
    return 0;
}

static void model_vec_cross(const double a[3], const double b[3], double out[3])
{
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
}

static void model_matrix_transpose(const double *m, double *out)
{
    int r, c;
    for (r = 0; r < 3; r++) for (c = 0; c < 3; c++)
        out[r*3+c] = m[c*3+r];
}

static void model_matrix_axis_angle(const double axis[3], double angle,
                                    double *out)
{
    double x = axis[0], y = axis[1], z = axis[2];
    double s = sin(angle), c = cos(angle), t = 1.0 - c;
    out[0] = t*x*x+c;   out[1] = t*x*y-s*z; out[2] = t*x*z+s*y;
    out[3] = t*x*y+s*z; out[4] = t*y*y+c;   out[5] = t*y*z-s*x;
    out[6] = t*x*z-s*y; out[7] = t*y*z+s*x; out[8] = t*z*z+c;
}

static struct rasterfall_animation_quaternion model_matrix_to_quaternion(
    const double *m);

static void model_matrix_to_euler(const double *m, int *x, int *y, int *z)
{
    double sy = -m[6], pitch, roll, yaw;
    if (sy > 1.0) sy = 1.0;
    if (sy < -1.0) sy = -1.0;
    pitch = atan2(sy, sqrt(1.0 - sy*sy));
    if (fabs(cos(pitch)) > 0.000001) {
        roll = atan2(m[7], m[8]);
        yaw = atan2(m[3], m[0]);
    } else {
        roll = atan2(-m[5], m[4]);
        yaw = 0.0;
    }
    *x = rounded(roll * 180.0 / M_PI);
    *y = rounded(pitch * 180.0 / M_PI);
    *z = rounded(yaw * 180.0 / M_PI);
}

static int model_angle_wrap(int value)
{
    while (value > 180) value -= 360;
    while (value < -180) value += 360;
    return value;
}

/* XYZ has two equivalent Euler representations away from gimbal lock.  The
 * model pose currently stores integer Euler values, so choose the branch
 * nearest the FK local pose instead of allowing the matrix extractor to
 * switch branches at an arbitrary frame. */
static void model_matrix_to_euler_near(const double *m, const int reference[3],
                                       int *x, int *y, int *z)
{
    int candidates[2][3], i, best=0;
    double ref_matrix[9], candidate_matrix[9], best_distance=2.0;
    struct rasterfall_animation_quaternion ref_q, candidate_q;
    model_matrix_to_euler(m,&candidates[0][0],&candidates[0][1],&candidates[0][2]);
    candidates[1][0]=model_angle_wrap(candidates[0][0]+180);
    candidates[1][1]=model_angle_wrap(180-candidates[0][1]);
    candidates[1][2]=model_angle_wrap(candidates[0][2]+180);
    matrix_rotate_xyz(reference[0],reference[1],reference[2],ref_matrix);
    ref_q=model_matrix_to_quaternion(ref_matrix);
    for(i=0;i<2;i++){
        double dot;
        matrix_rotate_xyz(candidates[i][0],candidates[i][1],candidates[i][2],candidate_matrix);
        candidate_q=model_matrix_to_quaternion(candidate_matrix);
        dot=ref_q.x*candidate_q.x+ref_q.y*candidate_q.y+ref_q.z*candidate_q.z+ref_q.w*candidate_q.w;
        if(dot<0.0)dot=-dot;
        if(dot>1.0)dot=1.0;
        if(1.0-dot<best_distance){best_distance=1.0-dot;best=i;}
    }
    *x=candidates[best][0];*y=candidates[best][1];*z=candidates[best][2];
}

static int model_clamp_angle(int value, float lower, float upper)
{
    int lo = rounded(lower * 180.0 / M_PI);
    int hi = rounded(upper * 180.0 / M_PI);
    if (lower < 0.0f && lo == 0) lo = -1;
    if (upper < 0.0f && hi == 0) hi = -1;
    if (lo > hi) { int swap = lo; lo = hi; hi = swap; }
    return value < lo ? lo : value > hi ? hi : value;
}

static int model_is_x_hinge(const struct rasterfall_model_ik_link *link)
{
    if (!link || !link->limited) return 0;
    return fabs(link->lower[1]) < 0.0001f && fabs(link->upper[1]) < 0.0001f &&
           fabs(link->lower[2]) < 0.0001f && fabs(link->upper[2]) < 0.0001f;
}

static void model_sample_track_translation(
    const struct rasterfall_animation_track *track, int time_ms, int duration,
    double out[3])
{
    const struct rasterfall_animation_keyframe *a, *b;
    int i, factor = 0;
    out[0] = out[1] = out[2] = 0.0;
    if (!track || !track->keys || track->key_count <= 0) return;
    if (duration > 0) time_ms %= duration;
    a = b = &track->keys[0];
    for (i = 1; i < track->key_count; i++) {
        if (time_ms < track->keys[i].time_ms) { b = &track->keys[i]; break; }
        a = &track->keys[i];
    }
    if (b != a && b->time_ms > a->time_ms)
        factor = (time_ms - a->time_ms) * 1000 / (b->time_ms - a->time_ms);
    if (duration > 0 && track->key_count > 1 &&
        a == &track->keys[track->key_count - 1] && time_ms > a->time_ms &&
        track->keys[0].time_ms < duration) {
        factor = (time_ms - a->time_ms) * 1000 / (duration - a->time_ms);
        b = &track->keys[0];
    }
    if (factor < 0) factor = 0;
    if (factor > 1000) factor = 1000;
    out[0] = a->tx + (b->tx - a->tx) * factor / 1000.0;
    out[1] = a->ty + (b->ty - a->ty) * factor / 1000.0;
    out[2] = a->tz + (b->tz - a->tz) * factor / 1000.0;
}

static double model_clip_translation_scale(
    const struct rasterfall_animation_clip *clip)
{
    return clip && clip->translation_scale > 0.0f ?
        clip->translation_scale : 1.0;
}

static const struct rasterfall_animation_track *model_find_clip_track(
    const struct rasterfall_animation_clip *clip, int bone)
{
    int i;
    if (!clip) return 0;
    for (i = 0; i < clip->track_count; i++)
        if (clip->tracks[i].target_bone == bone) return &clip->tracks[i];
    return 0;
}

/* IK metadata also contains toe chains.  Keep the controller-to-leg mapping
 * in one place so temporal leg state and diagnostics cannot be attributed to
 * an unrelated chain. */
static int model_leg_ik_side(const struct rasterfall_model_asset *asset,
                             const struct rasterfall_model_ik *ik)
{
    const char *name;
    if (!asset || !ik || ik->controller < 0 ||
        ik->controller >= (int)asset->bone_count) return -1;
    name=asset->bones[ik->controller].name;
    if (!strcmp(name,"左足ＩＫ")) return 0;
    if (!strcmp(name,"右足ＩＫ")) return 1;
    return -1;
}

static int model_analytic_target(const struct rasterfall_model_asset *asset,
                                 const struct rasterfall_model_ik *ik,
                                 const struct rasterfall_animation_clip *clip,
                                 int time_ms, double target[3])
{
    const struct rasterfall_animation_track *track;
    double raw[3]={0.0,0.0,0.0};
    if (!asset || !ik || ik->controller < 0 || ik->target < 0) return 0;
    track=model_find_clip_track(clip,ik->controller);
    if (!track && !asset->ik_synthetic_target) return 0;
    target[0]=asset->bone_transforms[ik->controller].position[0];
    target[1]=asset->bone_transforms[ik->controller].position[1];
    target[2]=asset->bone_transforms[ik->controller].position[2];
    if (asset->ik_synthetic_target) {
        target[0]+=asset->ik_synthetic_offset[0];target[1]+=asset->ik_synthetic_offset[1];target[2]+=asset->ik_synthetic_offset[2];
    } else {
        model_sample_track_translation(track,time_ms,clip->duration_ms,raw);
        double scale=model_clip_translation_scale(clip);
        target[0]+=raw[0]*scale;target[1]+=raw[1]*scale;target[2]+=raw[2]*scale;
    }
    return 1;
}

static int model_leg_branch(const struct rasterfall_model_asset *asset, int side,
                            const double target[3], double *ratio)
{
    int thigh=rasterfall_model_find_bone(asset,side ? "右足" : "左足");
    int knee=rasterfall_model_find_bone(asset,side ? "右ひざ" : "左ひざ");
    int ankle=rasterfall_model_find_bone(asset,side ? "右足首" : "左足首");
    double axis[3],hk[3],ref[3],perp[3],refp[3],axis_len,perp_len,ref_len,dot;
    if (thigh<0||knee<0||ankle<0) return 0;
    axis[0]=target[0]-asset->bone_transforms[thigh].position[0];axis[1]=target[1]-asset->bone_transforms[thigh].position[1];axis[2]=target[2]-asset->bone_transforms[thigh].position[2];
    hk[0]=asset->bone_transforms[knee].position[0]-asset->bone_transforms[thigh].position[0];hk[1]=asset->bone_transforms[knee].position[1]-asset->bone_transforms[thigh].position[1];hk[2]=asset->bone_transforms[knee].position[2]-asset->bone_transforms[thigh].position[2];
    ref[0]=asset->bones[knee].rest_x-asset->bones[thigh].rest_x;ref[1]=asset->bones[knee].rest_y-asset->bones[thigh].rest_y;ref[2]=asset->bones[knee].rest_z-asset->bones[thigh].rest_z;
    axis_len=model_vec_length(axis);if(axis_len<0.000001)return 0;axis[0]/=axis_len;axis[1]/=axis_len;axis[2]/=axis_len;
    dot=hk[0]*axis[0]+hk[1]*axis[1]+hk[2]*axis[2];perp[0]=hk[0]-dot*axis[0];perp[1]=hk[1]-dot*axis[1];perp[2]=hk[2]-dot*axis[2];perp_len=model_vec_length(perp);
    dot=ref[0]*axis[0]+ref[1]*axis[1]+ref[2]*axis[2];refp[0]=ref[0]-dot*axis[0];refp[1]=ref[1]-dot*axis[1];refp[2]=ref[2]-dot*axis[2];ref_len=model_vec_length(refp);
    if(ratio)*ratio=perp_len/(model_vec_length(hk)+model_vec_length((double[3]){asset->bone_transforms[ankle].position[0]-asset->bone_transforms[knee].position[0],asset->bone_transforms[ankle].position[1]-asset->bone_transforms[knee].position[1],asset->bone_transforms[ankle].position[2]-asset->bone_transforms[knee].position[2]})+0.000001);
    if(perp_len<0.000001||ref_len<0.000001)return 0;
    perp[0]/=perp_len;perp[1]/=perp_len;perp[2]/=perp_len;refp[0]/=ref_len;refp[1]/=ref_len;refp[2]/=ref_len;
    dot=perp[0]*refp[0]+perp[1]*refp[1]+perp[2]*refp[2];
    return dot>0.15?1:(dot<-0.15?-1:0);
}

static double model_leg_branch_angle_from_kperp(const double a[3], const double b[3])
{
    double la=model_vec_length(a),lb=model_vec_length(b),dot,cross[3],cross_len;
    if(la<0.000001 || lb<0.000001)return 0.0;
    dot=(a[0]*b[0]+a[1]*b[1]+a[2]*b[2])/(la*lb);
    if(dot>1.0)dot=1.0;
    if(dot<-1.0)dot=-1.0;
    cross[0]=a[1]*b[2]-a[2]*b[1];
    cross[1]=a[2]*b[0]-a[0]*b[2];
    cross[2]=a[0]*b[1]-a[1]*b[0];
    cross_len=model_vec_length(cross)/(la*lb);
    return atan2(cross_len,dot)*180.0/M_PI;
}

static int model_leg_kperp(const struct rasterfall_model_asset *asset, int side,
                           const double target[3], double out[3])
{
    int thigh=rasterfall_model_find_bone(asset,side ? "右足" : "左足");
    int knee=rasterfall_model_find_bone(asset,side ? "右ひざ" : "左ひざ");
    double axis[3],hk[3],len,d;
    if(thigh<0||knee<0)return 0;
    axis[0]=target[0]-asset->bone_transforms[thigh].position[0];axis[1]=target[1]-asset->bone_transforms[thigh].position[1];axis[2]=target[2]-asset->bone_transforms[thigh].position[2];
    len=model_vec_length(axis);if(len<0.000001)return 0;
    axis[0]/=len;axis[1]/=len;axis[2]/=len;
    hk[0]=asset->bone_transforms[knee].position[0]-asset->bone_transforms[thigh].position[0];hk[1]=asset->bone_transforms[knee].position[1]-asset->bone_transforms[thigh].position[1];hk[2]=asset->bone_transforms[knee].position[2]-asset->bone_transforms[thigh].position[2];
    d=hk[0]*axis[0]+hk[1]*axis[1]+hk[2]*axis[2];out[0]=hk[0]-d*axis[0];out[1]=hk[1]-d*axis[1];out[2]=hk[2]-d*axis[2];
    return model_vec_length(out)>=0.000001;
}

static void model_store_previous_final_bend(
    struct rasterfall_model_asset *asset, int side, const double target[3], int source)
{
    int thigh, knee;
    double axis[3], hk[3], bend[3], axis_len, bend_len, dot, hk_len;
    if (!asset || side < 0 || side >= 2 || !target) return;
    if (asset->ik_previous_final_bend_valid[side]) {
        asset->ik_previous_final_bend_prev[side][0]=asset->ik_previous_final_bend[side][0];
        asset->ik_previous_final_bend_prev[side][1]=asset->ik_previous_final_bend[side][1];
        asset->ik_previous_final_bend_prev[side][2]=asset->ik_previous_final_bend[side][2];
        asset->ik_previous_final_bend_prev_valid[side]=1;
    } else {
        asset->ik_previous_final_bend_prev_valid[side]=0;
    }
    asset->ik_previous_final_bend_valid[side] = 0;
    asset->ik_previous_final_bend_ratio[side] = 0.0;
    asset->ik_previous_final_bend_source[side] = source;
    thigh = rasterfall_model_find_bone(asset, side ? "右足" : "左足");
    knee = rasterfall_model_find_bone(asset, side ? "右ひざ" : "左ひざ");
    if (thigh < 0 || knee < 0) return;
    axis[0]=target[0]-asset->bone_transforms[thigh].position[0];
    axis[1]=target[1]-asset->bone_transforms[thigh].position[1];
    axis[2]=target[2]-asset->bone_transforms[thigh].position[2];
    hk[0]=asset->bone_transforms[knee].position[0]-asset->bone_transforms[thigh].position[0];
    hk[1]=asset->bone_transforms[knee].position[1]-asset->bone_transforms[thigh].position[1];
    hk[2]=asset->bone_transforms[knee].position[2]-asset->bone_transforms[thigh].position[2];
    axis_len=model_vec_length(axis);hk_len=model_vec_length(hk);
    if (axis_len < 0.000001 || hk_len < 0.000001) return;
    axis[0]/=axis_len;axis[1]/=axis_len;axis[2]/=axis_len;
    dot=hk[0]*axis[0]+hk[1]*axis[1]+hk[2]*axis[2];
    bend[0]=hk[0]-dot*axis[0];bend[1]=hk[1]-dot*axis[1];bend[2]=hk[2]-dot*axis[2];
    bend_len=model_vec_length(bend);
    if (bend_len/(hk_len+0.000001) < 0.05 || bend_len < 0.000001) return;
    asset->ik_previous_final_bend[side][0]=bend[0]/bend_len;
    asset->ik_previous_final_bend[side][1]=bend[1]/bend_len;
    asset->ik_previous_final_bend[side][2]=bend[2]/bend_len;
    asset->ik_previous_final_bend_valid[side]=1;
    asset->ik_previous_final_bend_ratio[side]=bend_len/(hk_len+0.000001);
    if (source == 1) {
        asset->ik_last_analytical_bend[side][0]=asset->ik_previous_final_bend[side][0];
        asset->ik_last_analytical_bend[side][1]=asset->ik_previous_final_bend[side][1];
        asset->ik_last_analytical_bend[side][2]=asset->ik_previous_final_bend[side][2];
        asset->ik_last_analytical_bend_valid[side]=1;
    }
}

static struct rasterfall_animation_quaternion model_matrix_to_quaternion(
    const double *m);

static int model_rotation_between(const double from_in[3],
                                  const double to_in[3],
                                  const double pole[3], double out[9])
{
    double from[3], to[3], axis[3], len_from, len_to, cross_len, dot, angle;
    int i;
    len_from = model_vec_length(from_in); len_to = model_vec_length(to_in);
    if (len_from < 0.000001 || len_to < 0.000001) return -1;
    for (i=0;i<3;i++) { from[i]=from_in[i]/len_from; to[i]=to_in[i]/len_to; }
    model_vec_cross(from, to, axis);
    cross_len = model_vec_length(axis);
    dot = from[0]*to[0] + from[1]*to[1] + from[2]*to[2];
    if (dot > 1.0) dot = 1.0;
    if (dot < -1.0) dot = -1.0;
    if (cross_len < 0.000001) {
        if (dot > 0.0) {
            out[0]=out[4]=out[8]=1.0; out[1]=out[2]=out[3]=out[5]=out[6]=out[7]=0.0;
            return 0;
        }
        memcpy(axis, pole, sizeof(axis));
        cross_len = model_vec_length(axis);
        if (cross_len < 0.000001) return -1;
        axis[0]/=cross_len; axis[1]/=cross_len; axis[2]/=cross_len;
        model_matrix_axis_angle(axis, M_PI, out);
        return 0;
    }
    axis[0]/=cross_len; axis[1]/=cross_len; axis[2]/=cross_len;
    angle = atan2(cross_len, dot);
    model_matrix_axis_angle(axis, angle, out);
    return 0;
}

static void model_probe_analytic_candidate(
    struct rasterfall_model_asset *asset, const struct rasterfall_model_ik *ik,
    const double h[3], const double from[3], const double kd[3],
    const double target[3], const double pole[3], const int base_thigh[3],
    const int base_knee[3], const char *label)
{
    double delta[9], desired_global[9], parent_inverse[9], local[9];
    double e0[3], e1[3], h0[3], h1[3], z0[3], z1[3], lower[3];
    double base_frame[9], desired_frame[9], base_transpose[9], frame_tmp[9];
    double hinge_axis[3], a[3], b[3], cross[3], dot, alen, blen, signed_angle;
    double ankle[3], error, thigh_error;
    int thigh = ik->links[1].bone, knee = ik->links[0].bone;
    int parent, x, y, z, raw_x, clamped_x, valid;
    e0[0]=from[0]/model_vec_length(from);e0[1]=from[1]/model_vec_length(from);e0[2]=from[2]/model_vec_length(from);
    lower[0]=target[0]-kd[0];lower[1]=target[1]-kd[1];lower[2]=target[2]-kd[2];
    alen=model_vec_length(lower);if(alen<0.000001)return;lower[0]/=alen;lower[1]/=alen;lower[2]/=alen;
    e1[0]=(kd[0]-h[0])/model_vec_length((double[3]){kd[0]-h[0],kd[1]-h[1],kd[2]-h[2]});
    e1[1]=(kd[1]-h[1])/model_vec_length((double[3]){kd[0]-h[0],kd[1]-h[1],kd[2]-h[2]});
    e1[2]=(kd[2]-h[2])/model_vec_length((double[3]){kd[0]-h[0],kd[1]-h[1],kd[2]-h[2]});
    parent=asset->bones[knee].parent;
    if(parent>=0)matrix_vector(asset->bone_transforms[parent].rotation,1.0,0.0,0.0,&h0[0],&h0[1],&h0[2]);else{h0[0]=1.0;h0[1]=0.0;h0[2]=0.0;}
    dot=h0[0]*e0[0]+h0[1]*e0[1]+h0[2]*e0[2];h0[0]-=dot*e0[0];h0[1]-=dot*e0[1];h0[2]-=dot*e0[2];alen=model_vec_length(h0);if(alen<0.000001)return;h0[0]/=alen;h0[1]/=alen;h0[2]/=alen;
    model_vec_cross(e0,h0,z0);model_vec_cross(e1,lower,h1);alen=model_vec_length(h1);if(alen<0.000001)return;h1[0]/=alen;h1[1]/=alen;h1[2]/=alen;
    if(model_rotation_between(e0,e1,pole,delta)<0)return;
    matrix_vector(delta,h0[0],h0[1],h0[2],&hinge_axis[0],&hinge_axis[1],&hinge_axis[2]);
    if(hinge_axis[0]*h1[0]+hinge_axis[1]*h1[1]+hinge_axis[2]*h1[2]<0.0){h1[0]=-h1[0];h1[1]=-h1[1];h1[2]=-h1[2];}
    model_vec_cross(e1,h1,z1);
    base_frame[0]=e0[0];base_frame[1]=h0[0];base_frame[2]=z0[0];base_frame[3]=e0[1];base_frame[4]=h0[1];base_frame[5]=z0[1];base_frame[6]=e0[2];base_frame[7]=h0[2];base_frame[8]=z0[2];
    desired_frame[0]=e1[0];desired_frame[1]=h1[0];desired_frame[2]=z1[0];desired_frame[3]=e1[1];desired_frame[4]=h1[1];desired_frame[5]=z1[1];desired_frame[6]=e1[2];desired_frame[7]=h1[2];desired_frame[8]=z1[2];
    model_matrix_transpose(base_frame,base_transpose);matrix_multiply(desired_frame,base_transpose,frame_tmp);memcpy(delta,frame_tmp,sizeof(delta));
    matrix_multiply(delta,asset->bone_transforms[thigh].rotation,desired_global);
    parent=asset->bones[thigh].parent;
    if(parent>=0){model_matrix_transpose(asset->bone_transforms[parent].rotation,parent_inverse);matrix_multiply(parent_inverse,desired_global,local);}else memcpy(local,desired_global,sizeof(local));
    model_matrix_to_euler(local,&x,&y,&z);
    asset->bones[thigh].rotate_x=x;asset->bones[thigh].rotate_y=y;asset->bones[thigh].rotate_z=z;
    rasterfall_model_update_bones(asset);
    thigh_error=model_vec_length((double[3]){
        asset->bone_transforms[knee].position[0]-kd[0],
        asset->bone_transforms[knee].position[1]-kd[1],
        asset->bone_transforms[knee].position[2]-kd[2]});
    parent=asset->bones[knee].parent;
    if(parent>=0)matrix_vector(asset->bone_transforms[parent].rotation,1.0,0.0,0.0,&hinge_axis[0],&hinge_axis[1],&hinge_axis[2]);
    else {hinge_axis[0]=1.0;hinge_axis[1]=0.0;hinge_axis[2]=0.0;}
    a[0]=asset->bone_transforms[ik->target].position[0]-asset->bone_transforms[knee].position[0];
    a[1]=asset->bone_transforms[ik->target].position[1]-asset->bone_transforms[knee].position[1];
    a[2]=asset->bone_transforms[ik->target].position[2]-asset->bone_transforms[knee].position[2];
    b[0]=target[0]-asset->bone_transforms[knee].position[0];
    b[1]=target[1]-asset->bone_transforms[knee].position[1];
    b[2]=target[2]-asset->bone_transforms[knee].position[2];
    dot=a[0]*hinge_axis[0]+a[1]*hinge_axis[1]+a[2]*hinge_axis[2];a[0]-=dot*hinge_axis[0];a[1]-=dot*hinge_axis[1];a[2]-=dot*hinge_axis[2];
    dot=b[0]*hinge_axis[0]+b[1]*hinge_axis[1]+b[2]*hinge_axis[2];b[0]-=dot*hinge_axis[0];b[1]-=dot*hinge_axis[1];b[2]-=dot*hinge_axis[2];
    alen=model_vec_length(a);blen=model_vec_length(b);
    if(alen<0.000001||blen<0.000001)goto restore;
    a[0]/=alen;a[1]/=alen;a[2]/=alen;b[0]/=blen;b[1]/=blen;b[2]/=blen;model_vec_cross(a,b,cross);
    signed_angle=atan2(hinge_axis[0]*cross[0]+hinge_axis[1]*cross[1]+hinge_axis[2]*cross[2],a[0]*b[0]+a[1]*b[1]+a[2]*b[2]);
    raw_x=base_knee[0]+rounded(signed_angle*180.0/M_PI);
    clamped_x=asset->ik_limits_enabled?model_clamp_angle(raw_x,ik->links[0].lower[0],ik->links[0].upper[0]):raw_x;
    valid=raw_x==clamped_x;
    asset->bones[knee].rotate_x=clamped_x;asset->bones[knee].rotate_y=0;asset->bones[knee].rotate_z=0;rasterfall_model_update_bones(asset);
    ankle[0]=asset->bone_transforms[ik->target].position[0];ankle[1]=asset->bone_transforms[ik->target].position[1];ankle[2]=asset->bone_transforms[ik->target].position[2];
    error=model_vec_length((double[3]){ankle[0]-target[0],ankle[1]-target[1],ankle[2]-target[2]});
    __printf("analytic candidate=%s K=(%.3f,%.3f,%.3f) thigh_error=%.6f signed_knee=%.6fdeg raw_x=%d clamped_x=%d valid=%s ankle_error=%.6f A=(%.3f,%.3f,%.3f)\n",label,kd[0],kd[1],kd[2],thigh_error,signed_angle*180.0/M_PI,raw_x,clamped_x,valid?"yes":"no",error,ankle[0],ankle[1],ankle[2]);
restore:
    asset->bones[thigh].rotate_x=base_thigh[0];asset->bones[thigh].rotate_y=base_thigh[1];asset->bones[thigh].rotate_z=base_thigh[2];
    asset->bones[knee].rotate_x=base_knee[0];asset->bones[knee].rotate_y=base_knee[1];asset->bones[knee].rotate_z=base_knee[2];
    rasterfall_model_update_bones(asset);
}

/* Build a deterministic bind frame from the PMX rest H/K/A geometry. */
static int model_build_thigh_bind_frame(
    const struct rasterfall_model_asset *asset, int thigh, int knee, int ankle,
    double out[9])
{
    double h[3], k[3], a[3], axis[3], secondary[3], third[3];
    double axis_len, secondary_len, dot;
    h[0]=asset->bones[thigh].rest_x; h[1]=asset->bones[thigh].rest_y; h[2]=asset->bones[thigh].rest_z;
    k[0]=asset->bones[knee].rest_x; k[1]=asset->bones[knee].rest_y; k[2]=asset->bones[knee].rest_z;
    a[0]=asset->bones[ankle].rest_x; a[1]=asset->bones[ankle].rest_y; a[2]=asset->bones[ankle].rest_z;
    axis[0]=a[0]-h[0]; axis[1]=a[1]-h[1]; axis[2]=a[2]-h[2];
    axis_len=model_vec_length(axis); if(axis_len<0.000001)return -1;
    axis[0]/=axis_len; axis[1]/=axis_len; axis[2]/=axis_len;
    secondary[0]=k[0]-h[0]; secondary[1]=k[1]-h[1]; secondary[2]=k[2]-h[2];
    dot=secondary[0]*axis[0]+secondary[1]*axis[1]+secondary[2]*axis[2];
    secondary[0]-=dot*axis[0]; secondary[1]-=dot*axis[1]; secondary[2]-=dot*axis[2];
    secondary_len=model_vec_length(secondary); if(secondary_len<0.000001)return -1;
    secondary[0]/=secondary_len; secondary[1]/=secondary_len; secondary[2]/=secondary_len;
    model_vec_cross(axis,secondary,third);
    out[0]=axis[0]; out[1]=secondary[0]; out[2]=third[0];
    out[3]=axis[1]; out[4]=secondary[1]; out[5]=third[1];
    out[6]=axis[2]; out[7]=secondary[2]; out[8]=third[2];
    return 0;
}

static int model_render_trace_leg_bone(int bone);
static int model_render_trace_leg_slot(int bone);
static void model_capture_rotation_write_trace(
    struct rasterfall_model_asset *asset, int bone,
    const double *result_global, const double *local_before,
    int x, int y, int z);
static void model_trace_quaternion_matrix(
    struct rasterfall_animation_quaternion q, double m[9]);
static void model_limit_leg_lateral_step(
    struct rasterfall_model_asset *asset,
    const struct rasterfall_model_ik *ik,
    const int source_thigh[3], int source_knee,
    const double source_ankle[3]);

static int model_solve_one_leg_analytic(
    struct rasterfall_model_asset *asset, const struct rasterfall_model_ik *ik,
    const struct rasterfall_animation_clip *clip, int time_ms,
    unsigned int *attempts, double *before, double *after)
{
    const struct rasterfall_animation_track *track;
    double target[3], raw[3], h[3], k0[3], a0[3], u[3], pole[3], v[3], kd[3];
    double l1, l2, d, dc, min_d, max_d, cos_h, bend, current_lower[3];
    double from[3], desired_global[9], parent_inverse[9], local[9];
    double axis_dot, v_len, current[3], offset[3];
    double pole_source[3], pole_projected[3], v_projected[3];
    double stable_bind_frame[9], stable_bind_secondary[3], bind_projected[3];
    double dynamic_ratio, bind_ratio, first_projection_dot, post_projection_dot;
    int pole_source_kind, pole_side;
    double hinge_axis[3], aa[3], bb[3], cross2[3], aa_len, bb_len, dot2, signed2;
    double bind_pole[3];
    double e0[3], e1[3], h1[3], z1[3], ldesired[3], candidate_b[3];
    double thigh_before_global[9], thigh_local_trace[9];
    int knee, thigh, parent, x, y, z, clamped = 0;
    int base_knee_x, base_thigh[3], base_knee[3];
    if (asset) asset->ik_analytic_last_reason=4;
    if (!asset || !ik || !clip || ik->link_count != 2 || !model_is_x_hinge(&ik->links[0]) ||
        ik->controller < 0 || ik->target < 0) return 0;
    pole_side=model_leg_ik_side(asset,ik);
    if (pole_side < 0) return 0;
    track = model_find_clip_track(clip, ik->controller);
    if (!track && !asset->ik_synthetic_target) return 0;
    asset->ik_analytic_last_reason=0;
    if (asset) {
        asset->ik_analytic_last_dynamic_pole_ratio[pole_side]=0.0;
        asset->ik_analytic_last_bind_pole_ratio[pole_side]=0.0;
        asset->ik_analytic_last_selected_pole[pole_side][0]=0.0;
        asset->ik_analytic_last_selected_pole[pole_side][1]=0.0;
        asset->ik_analytic_last_selected_pole[pole_side][2]=0.0;
        asset->ik_analytic_last_fullframe_secondary[pole_side][0]=0.0;
        asset->ik_analytic_last_fullframe_secondary[pole_side][1]=0.0;
        asset->ik_analytic_last_fullframe_secondary[pole_side][2]=0.0;
        asset->ik_analytic_last_reconciled_secondary[pole_side][0]=0.0;
        asset->ik_analytic_last_reconciled_secondary[pole_side][1]=0.0;
        asset->ik_analytic_last_reconciled_secondary[pole_side][2]=0.0;
        asset->ik_analytic_last_anchored_pole[pole_side][0]=0.0;
        asset->ik_analytic_last_anchored_pole[pole_side][1]=0.0;
        asset->ik_analytic_last_anchored_pole[pole_side][2]=0.0;
        asset->ik_analytic_last_pole_anchor[pole_side][0]=0.0;
        asset->ik_analytic_last_pole_anchor[pole_side][1]=0.0;
        asset->ik_analytic_last_pole_anchor[pole_side][2]=0.0;
        asset->ik_analytic_last_pole_anchor_length[pole_side]=0.0;
        asset->ik_analytic_last_pole_anchor_ratio[pole_side]=0.0;
        asset->ik_analytic_last_pole_anchor_valid[pole_side]=0;
        asset->ik_analytic_last_pole_flipped_by_anchor[pole_side]=0;
        if (!asset->ik_analytic_pole_override) {
            asset->ik_analytic_last_anchor_confidence[pole_side]=0.0;
            asset->ik_analytic_last_anchor_rejected[pole_side]=0;
            asset->ik_analytic_last_anchor_reject_reason[pole_side]=0;
            asset->ik_analytic_last_anchor_conflict_dot[pole_side]=0.0;
            asset->ik_analytic_last_previous_bend_ratio[pole_side]=asset->ik_previous_final_bend_ratio[pole_side];
            asset->ik_analytic_last_previous_bend_source[pole_side]=asset->ik_previous_final_bend_source[pole_side];
        }
        asset->ik_analytic_last_pole_source[pole_side]=0;
        asset->ik_analytic_last_pole_override[pole_side]=0;
    }
    knee = ik->links[0].bone; thigh = ik->links[1].bone;
    if (knee < 0 || thigh < 0 || ik->target >= (int)asset->bone_count) return 0;
    rasterfall_model_update_bones(asset);
    if (asset->ik_synthetic_target && asset->ik_synthetic_side >= 0 &&
        asset->ik_synthetic_side != pole_side) return 0;
    target[0]=asset->bone_transforms[ik->controller].position[0];
    target[1]=asset->bone_transforms[ik->controller].position[1];
    target[2]=asset->bone_transforms[ik->controller].position[2];
    raw[0]=raw[1]=raw[2]=0.0;
    if (asset->ik_synthetic_target) {
        target[0]+=asset->ik_synthetic_offset[0]; target[1]+=asset->ik_synthetic_offset[1]; target[2]+=asset->ik_synthetic_offset[2];
    } else {
        model_sample_track_translation(track,time_ms,clip->duration_ms,raw);
        double scale=model_clip_translation_scale(clip);
        target[0]+=raw[0]*scale; target[1]+=raw[1]*scale; target[2]+=raw[2]*scale;
    }
    h[0]=asset->bone_transforms[thigh].position[0]; h[1]=asset->bone_transforms[thigh].position[1]; h[2]=asset->bone_transforms[thigh].position[2];
    k0[0]=asset->bone_transforms[knee].position[0]; k0[1]=asset->bone_transforms[knee].position[1]; k0[2]=asset->bone_transforms[knee].position[2];
    a0[0]=asset->bone_transforms[ik->target].position[0]; a0[1]=asset->bone_transforms[ik->target].position[1]; a0[2]=asset->bone_transforms[ik->target].position[2];
    current[0]=a0[0]; current[1]=a0[1]; current[2]=a0[2];
    offset[0]=current[0]-target[0]; offset[1]=current[1]-target[1]; offset[2]=current[2]-target[2];
    *before=model_vec_length(offset); *after=*before; if (attempts) *attempts=0;
    from[0]=k0[0]-h[0]; from[1]=k0[1]-h[1]; from[2]=k0[2]-h[2];
    current_lower[0]=a0[0]-k0[0]; current_lower[1]=a0[1]-k0[1]; current_lower[2]=a0[2]-k0[2];
    l1=model_vec_length(from); l2=model_vec_length(current_lower);
    u[0]=target[0]-h[0]; u[1]=target[1]-h[1]; u[2]=target[2]-h[2]; d=model_vec_length(u);
    if (l1 < 0.000001 || l2 < 0.000001) return 0;
    if (model_build_thigh_bind_frame(asset,thigh,knee,ik->target,stable_bind_frame)<0) return 0;
    stable_bind_secondary[0]=stable_bind_frame[1];
    stable_bind_secondary[1]=stable_bind_frame[4];
    stable_bind_secondary[2]=stable_bind_frame[7];
    min_d=fabs(l1-l2)+0.001; max_d=l1+l2-0.001;
    if (max_d < min_d) max_d=min_d;
    dc=d; if (dc < min_d) {dc=min_d;clamped=1;} if (dc > max_d) {dc=max_d;clamped=1;}
    if (clamped) asset->ik_analytic_last_reason=1;
    {
        double ratio=d/(l1+l2);
        asset->solver_metrics.ik_reach_sample_count++;
        asset->solver_metrics.ik_reach_distance_total+=d;
        asset->solver_metrics.ik_reach_ratio_total+=ratio;
        if(d>asset->solver_metrics.ik_reach_distance_max)asset->solver_metrics.ik_reach_distance_max=d;
        if(ratio>asset->solver_metrics.ik_reach_ratio_max)asset->solver_metrics.ik_reach_ratio_max=ratio;
        if(d>l1+l2)asset->solver_metrics.ik_unreachable_count++;
    }
    if (d < 0.000001) { u[0]=from[0];u[1]=from[1];u[2]=from[2];d=model_vec_length(u); }
    u[0]/=d;u[1]/=d;u[2]/=d;
    /* The bend reference is taken from this leg's FK/bind H-K-A plane, not
     * from a shared left/right sign or from the animated target direction. */
    /* The dynamic bend reference is the current hip-to-knee vector.  Using
     * hip-to-ankle here lets the lower leg trajectory redefine the bend plane
     * and can reverse the oriented pole while H->T remains continuous. */
    pole_source[0]=k0[0]-h[0];pole_source[1]=k0[1]-h[1];pole_source[2]=k0[2]-h[2];
    pole[0]=pole_source[0];pole[1]=pole_source[1];pole[2]=pole_source[2];
    /* Project the reference itself onto the plane perpendicular to H->T.
     * The previous code projected the axis using the reference dot product;
     * that is not an orthogonal projection and can reverse the subsequent
     * normalized pole even when the inputs move continuously. */
    axis_dot=pole[0]*u[0]+pole[1]*u[1]+pole[2]*u[2];
    first_projection_dot=axis_dot;
    /* u is already unit length here, so the orthogonal projection is
     * reference - u * dot(reference,u). */
    pole[0]-=u[0]*axis_dot;
    pole[1]-=u[1]*axis_dot;
    pole[2]-=u[2]*axis_dot;
    post_projection_dot=pole[0]*u[0]+pole[1]*u[1]+pole[2]*u[2];
    pole_projected[0]=pole[0];pole_projected[1]=pole[1];pole_projected[2]=pole[2];
    aa_len=model_vec_length(pole);
    if (aa_len < 0.000001) return 0;
    bind_pole[0]=pole[0]/aa_len;bind_pole[1]=pole[1]/aa_len;bind_pole[2]=pole[2]/aa_len;
    axis_dot=pole[0]*u[0]+pole[1]*u[1]+pole[2]*u[2];
    v[0]=pole[0]-u[0]*axis_dot;v[1]=pole[1]-u[1]*axis_dot;v[2]=pole[2]-u[2]*axis_dot;
    v_projected[0]=v[0];v_projected[1]=v[1];v_projected[2]=v[2];
    dynamic_ratio=model_vec_length(pole_projected)/model_vec_length(pole_source);
    bind_projected[0]=stable_bind_secondary[0]-u[0]*(stable_bind_secondary[0]*u[0]+stable_bind_secondary[1]*u[1]+stable_bind_secondary[2]*u[2]);
    bind_projected[1]=stable_bind_secondary[1]-u[1]*(stable_bind_secondary[0]*u[0]+stable_bind_secondary[1]*u[1]+stable_bind_secondary[2]*u[2]);
    bind_projected[2]=stable_bind_secondary[2]-u[2]*(stable_bind_secondary[0]*u[0]+stable_bind_secondary[1]*u[1]+stable_bind_secondary[2]*u[2]);
    bind_ratio=model_vec_length(bind_projected);
    /* A well-conditioned dynamic pole has its own oriented FK meaning.  Do
     * not force it into the bind-pole hemisphere: that would turn a valid
     * dynamic direction into a 180-degree branch jump. */
    v[0]=v_projected[0];v[1]=v_projected[1];v[2]=v_projected[2];
    pole_source_kind=0;
    /* Below roughly sin(8.6 degrees), the dynamic reference has poor
     * conditioning.  Blend toward the non-degenerate rest-frame reference
     * over this dimensionless interval instead of hard-switching sources. */
    if (dynamic_ratio < 0.15) {
        if (bind_ratio < 0.05) { asset->ik_analytic_last_reason=4; return 0; }
        {
            double w=dynamic_ratio/0.15;
            double bx=bind_projected[0]/bind_ratio;
            double by=bind_projected[1]/bind_ratio;
            double bz=bind_projected[2]/bind_ratio;
            v[0]=bx*(1.0-w)+v_projected[0]*w;
            v[1]=by*(1.0-w)+v_projected[1]*w;
            v[2]=bz*(1.0-w)+v_projected[2]*w;
            v_len=model_vec_length(v);
            if (v_len < 0.000001) { asset->ik_analytic_last_reason=4; return 0; }
            v[0]/=v_len;v[1]/=v_len;v[2]/=v_len;
        }
        pole_source_kind=dynamic_ratio < 0.000001 ? 1 : 2;
    }
    /* A blended pole is not a proven PMX branch: its two inputs may represent
     * opposite knee sides.  Keep analytical acceptance conservative until a
     * candidate-level branch comparison establishes a unique valid solution. */
    if (pole_source_kind == 2) {
        asset->ik_analytic_last_reason=4;
        return 0;
    }
    if (asset) {
        asset->ik_analytic_last_dynamic_pole_ratio[pole_side]=dynamic_ratio;
        asset->ik_analytic_last_bind_pole_ratio[pole_side]=bind_ratio;
    }
    v_len=model_vec_length(v);
    if (v_len < 0.000001) {
        v[0]=current_lower[0];v[1]=current_lower[1];v[2]=current_lower[2];
        axis_dot=v[0]*u[0]+v[1]*u[1]+v[2]*u[2];
        v[0]-=u[0]*axis_dot;v[1]-=u[1]*axis_dot;v[2]-=u[2]*axis_dot;v_len=model_vec_length(v);
    }
    if (v_len < 0.000001) return 0;
    v[0]/=v_len;v[1]/=v_len;v[2]/=v_len;
    if (asset->ik_analytic_pole_override) {
        v[0]=asset->ik_analytic_pole[0];
        v[1]=asset->ik_analytic_pole[1];
        v[2]=asset->ik_analytic_pole[2];
        v_len=model_vec_length(v);
        if (v_len < 0.000001) return 0;
        v[0]/=v_len;v[1]/=v_len;v[2]/=v_len;
        pole_source_kind=3;
    }
    asset->ik_analytic_last_dynamic_pole[pole_side][0]=v[0];
    asset->ik_analytic_last_dynamic_pole[pole_side][1]=v[1];
    asset->ik_analytic_last_dynamic_pole[pole_side][2]=v[2];
    if (!asset->ik_analytic_pole_override &&
        asset->ik_previous_final_bend_valid[pole_side]) {
        double anchor[3], anchor_dot, anchor_len;
        int reject_reason=0;
        anchor_dot=asset->ik_previous_final_bend[pole_side][0]*u[0]+
                   asset->ik_previous_final_bend[pole_side][1]*u[1]+
                   asset->ik_previous_final_bend[pole_side][2]*u[2];
        anchor[0]=asset->ik_previous_final_bend[pole_side][0]-u[0]*anchor_dot;
        anchor[1]=asset->ik_previous_final_bend[pole_side][1]-u[1]*anchor_dot;
        anchor[2]=asset->ik_previous_final_bend[pole_side][2]-u[2]*anchor_dot;
        anchor_len=model_vec_length(anchor);
        if (asset->ik_previous_final_bend_ratio[pole_side] < 0.05)
            reject_reason=2;
        if (!reject_reason && anchor_len < 0.05)
            reject_reason=2;
        if (!reject_reason && asset->ik_previous_final_bend_prev_valid[pole_side]) {
            double prior_dot=asset->ik_previous_final_bend[pole_side][0]*asset->ik_previous_final_bend_prev[pole_side][0]+
                asset->ik_previous_final_bend[pole_side][1]*asset->ik_previous_final_bend_prev[pole_side][1]+
                asset->ik_previous_final_bend[pole_side][2]*asset->ik_previous_final_bend_prev[pole_side][2];
            if (prior_dot < 0.25) reject_reason=3;
        }
        if (!reject_reason && asset->ik_previous_final_bend_source[pole_side] == 2 &&
            asset->ik_last_analytical_bend_valid[pole_side]) {
            double stable_dot=asset->ik_previous_final_bend[pole_side][0]*asset->ik_last_analytical_bend[pole_side][0]+
                asset->ik_previous_final_bend[pole_side][1]*asset->ik_last_analytical_bend[pole_side][1]+
                asset->ik_previous_final_bend[pole_side][2]*asset->ik_last_analytical_bend[pole_side][2];
            if (stable_dot < 0.25) reject_reason=4;
        }
        if (!reject_reason && asset->ik_previous_final_bend_source[pole_side] == 2 &&
            asset->ik_last_analytical_branch_valid[pole_side] &&
            asset->ik_previous_final_branch[pole_side] != 0 &&
            asset->ik_last_analytical_branch[pole_side] != 0 &&
            asset->ik_previous_final_branch[pole_side] != asset->ik_last_analytical_branch[pole_side])
            reject_reason=4;
        /* A CCD pose that has moved to the opposite rest/bind hemisphere is
         * stale evidence.  Rest is only a conflict detector here; it never
         * supplies the pole on its own. */
        if (!reject_reason && bind_ratio >= 0.05) {
            double bind_dot=(asset->ik_previous_final_bend[pole_side][0]*bind_projected[0]+asset->ik_previous_final_bend[pole_side][1]*bind_projected[1]+asset->ik_previous_final_bend[pole_side][2]*bind_projected[2]);
            asset->ik_analytic_last_anchor_conflict_dot[pole_side]=bind_dot/(bind_ratio+0.000001);
            if (bind_dot < -0.75*bind_ratio)
                reject_reason=asset->ik_previous_final_bend_source[pole_side] == 2 ? 4 : 5;
        }
        if (reject_reason) {
            asset->ik_analytic_last_anchor_rejected[pole_side]=1;
            asset->ik_analytic_last_anchor_reject_reason[pole_side]=reject_reason;
            asset->ik_analytic_anchor_rejected_count[pole_side]++;
        } else if (anchor_len >= 0.000001) {
            anchor[0]/=anchor_len;anchor[1]/=anchor_len;anchor[2]/=anchor_len;
            asset->ik_analytic_last_anchor_confidence[pole_side]=1.0;
            asset->ik_analytic_last_pole_anchor_valid[pole_side]=1;
            asset->ik_analytic_last_pole_anchor[pole_side][0]=anchor[0];
            asset->ik_analytic_last_pole_anchor[pole_side][1]=anchor[1];
            asset->ik_analytic_last_pole_anchor[pole_side][2]=anchor[2];
            asset->ik_analytic_last_pole_anchor_length[pole_side]=anchor_len;
            asset->ik_analytic_last_pole_anchor_ratio[pole_side]=anchor_len;
            asset->ik_analytic_pole_anchor_valid_count[pole_side]++;
            if (v[0]*anchor[0]+v[1]*anchor[1]+v[2]*anchor[2] < 0.0) {
                v[0]=-v[0];v[1]=-v[1];v[2]=-v[2];
                asset->ik_analytic_last_pole_flipped_by_anchor[pole_side]=1;
                asset->ik_analytic_pole_flipped_by_anchor_count[pole_side]++;
            }
        }
    }
    if (asset) {
        asset->ik_analytic_last_pole_source[pole_side]=pole_source_kind;
        asset->ik_analytic_last_pole_override[pole_side]=pole_source_kind==3;
        asset->ik_analytic_last_selected_pole[pole_side][0]=v[0];
        asset->ik_analytic_last_selected_pole[pole_side][1]=v[1];
        asset->ik_analytic_last_selected_pole[pole_side][2]=v[2];
        asset->ik_analytic_last_anchored_pole[pole_side][0]=v[0];
        asset->ik_analytic_last_anchored_pole[pole_side][1]=v[1];
        asset->ik_analytic_last_anchored_pole[pole_side][2]=v[2];
    }
    cos_h=(l1*l1+dc*dc-l2*l2)/(2.0*l1*dc);
    if (cos_h > 1.0) cos_h = 1.0;
    if (cos_h < -1.0) cos_h = -1.0;
    bend=1.0-cos_h*cos_h;if(bend<0.0)bend=0.0;bend=sqrt(bend);
    kd[0]=h[0]+u[0]*(l1*cos_h)+v[0]*(l1*bend);
    kd[1]=h[1]+u[1]*(l1*cos_h)+v[1]*(l1*bend);
    kd[2]=h[2]+u[2]*(l1*cos_h)+v[2]*(l1*bend);
    base_thigh[0]=asset->bones[thigh].rotate_x;base_thigh[1]=asset->bones[thigh].rotate_y;base_thigh[2]=asset->bones[thigh].rotate_z;
    base_knee[0]=asset->bones[knee].rotate_x;base_knee[1]=asset->bones[knee].rotate_y;base_knee[2]=asset->bones[knee].rotate_z;
    candidate_b[0]=h[0]+u[0]*(l1*cos_h)-v[0]*(l1*bend);
    candidate_b[1]=h[1]+u[1]*(l1*cos_h)-v[1]*(l1*bend);
    candidate_b[2]=h[2]+u[2]*(l1*cos_h)-v[2]*(l1*bend);
    if (asset->ik_analytic_geometry_dump) {
        model_probe_analytic_candidate(asset,ik,h,from,kd,target,v,base_thigh,base_knee,"A");
        model_probe_analytic_candidate(asset,ik,h,from,candidate_b,target,v,base_thigh,base_knee,"B");
    }
    e0[0]=from[0]/l1;e0[1]=from[1]/l1;e0[2]=from[2]/l1;
    e1[0]=(kd[0]-h[0])/l1;e1[1]=(kd[1]-h[1])/l1;e1[2]=(kd[2]-h[2])/l1;
    ldesired[0]=(target[0]-kd[0])/l2;ldesired[1]=(target[1]-kd[1])/l2;ldesired[2]=(target[2]-kd[2])/l2;
    {
        double thigh_delta[9];
        /* Rotate the actual upper-leg direction onto the analytical upper-leg
         * direction by the shortest arc.  The former full-frame construction
         * used the bind hip-to-ankle axis as its source primary axis but the
         * hip-to-knee axis as its destination primary axis.  Near extension
         * that mismatched basis admits a valid-looking orientation differing
         * by roughly 180 degrees.  Applying the delta to the sampled global
         * thigh frame also preserves the animation's existing axial twist. */
        if (model_rotation_between(from,e1,v,thigh_delta)<0) return 0;
        matrix_multiply(thigh_delta,asset->bone_transforms[thigh].rotation,
                        desired_global);
        asset->ik_analytic_last_fullframe_secondary[pole_side][0]=v[0];
        asset->ik_analytic_last_fullframe_secondary[pole_side][1]=v[1];
        asset->ik_analytic_last_fullframe_secondary[pole_side][2]=v[2];
        asset->ik_near_degenerate_ca_active[pole_side]=0;
        asset->ik_near_degenerate_ca_reconciled[pole_side]=0;
        asset->ik_near_degenerate_ca_unavailable[pole_side]=0;
    }
    memcpy(thigh_before_global,asset->bone_transforms[thigh].rotation,sizeof(thigh_before_global));
    model_vec_cross(e1,ldesired,h1); aa_len=model_vec_length(h1);
    if (aa_len < 0.000001) return 0;
    h1[0]/=aa_len;h1[1]/=aa_len;h1[2]/=aa_len;
    model_vec_cross(e1,h1,z1);
    parent=asset->bones[thigh].parent;
    if(parent>=0){model_matrix_transpose(asset->bone_transforms[parent].rotation,parent_inverse);matrix_multiply(parent_inverse,desired_global,local);}else memcpy(local,desired_global,sizeof(local));
    memcpy(thigh_local_trace,local,sizeof(thigh_local_trace));
    model_matrix_to_euler_near(local,base_thigh,&x,&y,&z);
    model_capture_rotation_write_trace(asset, thigh, desired_global, local,
                                       x, y, z);
    asset->bones[thigh].rotate_x=x;asset->bones[thigh].rotate_y=y;asset->bones[thigh].rotate_z=z;
    rasterfall_model_update_bones(asset);
    if (model_render_trace_leg_bone(thigh)) {
        struct rasterfall_animation_quaternion q =
            model_matrix_to_quaternion(asset->bone_transforms[thigh].rotation);
        asset->render_trace_final_bone_q[0]=q.x;
        asset->render_trace_final_bone_q[1]=q.y;
        asset->render_trace_final_bone_q[2]=q.z;
        asset->render_trace_final_bone_q[3]=q.w;
        memcpy(asset->render_trace_final_bone_q_by_bone[model_render_trace_leg_slot(thigh)],
               asset->render_trace_final_bone_q, 4 * sizeof(double));
    }
    {
        int trace_side = pole_side;
        if (asset->ik_analytic_trace_time_ms == time_ms && asset->ik_analytic_trace_side == trace_side) {
            double tc[3], tl, td, ta, bn[3], bl;
            double basis[3][3], primary[3], secondary[3], tertiary[3];
            double bind_unit[3], dynamic_unit[3], blend_pre[3], blend_len, bind_dynamic_dot;
            double bind_kd_plus[3], bind_kd_minus[3];
            double diag_base[9], diag_desired[9], diag_transpose[9];
            double diag_delta[9], diag_global[9];
            struct rasterfall_animation_quaternion qdiag;
            int primary_axis, secondary_axis, third_axis, i;
            double best, value, projection;
            struct rasterfall_animation_quaternion qb=model_matrix_to_quaternion(thigh_before_global);
            struct rasterfall_animation_quaternion qa=model_matrix_to_quaternion(asset->bone_transforms[thigh].rotation);
            struct rasterfall_animation_quaternion ql=model_matrix_to_quaternion(thigh_local_trace);
            struct rasterfall_animation_quaternion qd=model_matrix_to_quaternion(desired_global);
            bind_unit[0]=bind_projected[0]/bind_ratio;bind_unit[1]=bind_projected[1]/bind_ratio;bind_unit[2]=bind_projected[2]/bind_ratio;
            dynamic_unit[0]=v_projected[0]/model_vec_length(v_projected);dynamic_unit[1]=v_projected[1]/model_vec_length(v_projected);dynamic_unit[2]=v_projected[2]/model_vec_length(v_projected);
            bind_dynamic_dot=bind_unit[0]*dynamic_unit[0]+bind_unit[1]*dynamic_unit[1]+bind_unit[2]*dynamic_unit[2];
            blend_pre[0]=bind_unit[0];blend_pre[1]=bind_unit[1];blend_pre[2]=bind_unit[2];
            if (dynamic_ratio < 0.15) {
                double bw=dynamic_ratio/0.15;
                blend_pre[0]=bind_unit[0]*(1.0-bw)+dynamic_unit[0]*bw;
                blend_pre[1]=bind_unit[1]*(1.0-bw)+dynamic_unit[1]*bw;
                blend_pre[2]=bind_unit[2]*(1.0-bw)+dynamic_unit[2]*bw;
            }
            blend_len=model_vec_length(blend_pre);
            bind_kd_plus[0]=h[0]+u[0]*(l1*cos_h)+bind_unit[0]*(l1*bend);
            bind_kd_plus[1]=h[1]+u[1]*(l1*cos_h)+bind_unit[1]*(l1*bend);
            bind_kd_plus[2]=h[2]+u[2]*(l1*cos_h)+bind_unit[2]*(l1*bend);
            bind_kd_minus[0]=h[0]+u[0]*(l1*cos_h)-bind_unit[0]*(l1*bend);
            bind_kd_minus[1]=h[1]+u[1]*(l1*cos_h)-bind_unit[1]*(l1*bend);
            bind_kd_minus[2]=h[2]+u[2]*(l1*cos_h)-bind_unit[2]*(l1*bend);
            model_vec_cross(e0,e1,tc);tl=model_vec_length(tc);td=e0[0]*e1[0]+e0[1]*e1[1]+e0[2]*e1[2];if(td>1.0)td=1.0;if(td<-1.0)td=-1.0;ta=atan2(tl,td)*180.0/M_PI;
            bn[0]=e1[1]*v[2]-e1[2]*v[1];bn[1]=e1[2]*v[0]-e1[0]*v[2];bn[2]=e1[0]*v[1]-e1[1]*v[0];bl=model_vec_length(bn);if(bl>0.000001){bn[0]/=bl;bn[1]/=bl;bn[2]/=bl;}
            __printf("analytic thigh trace side=%s time=%d H=(%.3f,%.3f,%.3f) T=(%.3f,%.3f,%.3f) Kd=(%.3f,%.3f,%.3f) pole=(%.6f,%.6f,%.6f) bend_normal=(%.6f,%.6f,%.6f)\n",trace_side?"right":"left",time_ms,h[0],h[1],h[2],target[0],target[1],target[2],kd[0],kd[1],kd[2],v[0],v[1],v[2],bn[0],bn[1],bn[2]);
            __printf("analytic pole chain side=%s time=%d source=(%.6f,%.6f,%.6f) source_len=%.6f source_dot_HK=%.6f first_dot=%.6f post_dot=%.6f projected=(%.6f,%.6f,%.6f) projected_len=%.6f axis_projected=(%.6f,%.6f,%.6f) axis_projected_len=%.6f normalized=(%.6f,%.6f,%.6f)\n",trace_side?"right":"left",time_ms,pole_source[0],pole_source[1],pole_source[2],model_vec_length(pole_source),pole_source[0]*from[0]+pole_source[1]*from[1]+pole_source[2]*from[2],first_projection_dot,post_projection_dot,pole_projected[0],pole_projected[1],pole_projected[2],model_vec_length(pole_projected),v_projected[0],v_projected[1],v_projected[2],model_vec_length(v_projected),v[0],v[1],v[2]);
            __printf("analytic pole conditioning side=%s time=%d dynamic_ratio=%.9f bind_ratio=%.9f bind_projected=(%.6f,%.6f,%.6f) selected_source=%s selected=(%.6f,%.6f,%.6f)\n",trace_side?"right":"left",time_ms,dynamic_ratio,bind_ratio,bind_projected[0],bind_projected[1],bind_projected[2],pole_source_kind==1?"bind":(pole_source_kind==2?"blend":(pole_source_kind==3?"override":"dynamic")),v[0],v[1],v[2]);
            __printf("analytic pole canonical side=%s time=%d source=%s override=%d selected=(%.6f,%.6f,%.6f) kd=(%.6f,%.6f,%.6f) fullframe_secondary=(%.6f,%.6f,%.6f)\n",trace_side?"right":"left",time_ms,pole_source_kind==1?"bind":(pole_source_kind==2?"blend":(pole_source_kind==3?"override":"dynamic")),asset->ik_analytic_last_pole_override[trace_side],asset->ik_analytic_last_selected_pole[trace_side][0],asset->ik_analytic_last_selected_pole[trace_side][1],asset->ik_analytic_last_selected_pole[trace_side][2],kd[0],kd[1],kd[2],v[0],v[1],v[2]);
            __printf("analytic pole transition side=%s time=%d Pd=(%.6f,%.6f,%.6f) Pb=(%.6f,%.6f,%.6f) dot=%.6f ratio=%.6f blend_pre=(%.6f,%.6f,%.6f) blend_len=%.6f\n",trace_side?"right":"left",time_ms,dynamic_unit[0],dynamic_unit[1],dynamic_unit[2],bind_unit[0],bind_unit[1],bind_unit[2],bind_dynamic_dot,dynamic_ratio,blend_pre[0],blend_pre[1],blend_pre[2],blend_len);
            if (asset->ik_analytic_trace_time_ms == time_ms) {
                model_probe_analytic_candidate(asset,ik,h,from,bind_kd_plus,target,bind_unit,base_thigh,base_knee,"bind+");
                bind_unit[0]=-bind_unit[0];bind_unit[1]=-bind_unit[1];bind_unit[2]=-bind_unit[2];
                model_probe_analytic_candidate(asset,ik,h,from,bind_kd_minus,target,bind_unit,base_thigh,base_knee,"bind-");
            }
            __printf("analytic pole refs side=%s time=%d dynamic_pole=(%.6f,%.6f,%.6f) target_axis=(%.6f,%.6f,%.6f) candidate_A=(%.6f,%.6f,%.6f) candidate_B=(%.6f,%.6f,%.6f)\n",trace_side?"right":"left",time_ms,bind_pole[0],bind_pole[1],bind_pole[2],u[0],u[1],u[2],kd[0],kd[1],kd[2],candidate_b[0],candidate_b[1],candidate_b[2]);
            __printf("analytic thigh trace side=%s time=%d current_dir=(%.6f,%.6f,%.6f) desired_dir=(%.6f,%.6f,%.6f) dir_dot=%.6f from_to_axis=(%.6f,%.6f,%.6f) from_to_angle=%.6f\n",trace_side?"right":"left",time_ms,e0[0],e0[1],e0[2],e1[0],e1[1],e1[2],td,tl>0.000001?tc[0]/tl:0.0,tl>0.000001?tc[1]/tl:0.0,tl>0.000001?tc[2]/tl:0.0,ta);
            __printf("analytic thigh trace side=%s time=%d desired_global_q=(%.6f,%.6f,%.6f,%.6f) global_before_q=(%.6f,%.6f,%.6f,%.6f) global_after_q=(%.6f,%.6f,%.6f,%.6f) local_q=(%.6f,%.6f,%.6f,%.6f) local_euler=(%d,%d,%d) base_euler=(%d,%d,%d)\n",trace_side?"right":"left",time_ms,qd.x,qd.y,qd.z,qd.w,qb.x,qb.y,qb.z,qb.w,qa.x,qa.y,qa.z,qa.w,ql.x,ql.y,ql.z,ql.w,x,y,z,base_thigh[0],base_thigh[1],base_thigh[2]);
            __printf("analytic thigh trace side=%s time=%d before_basis=(%.6f,%.6f,%.6f|%.6f,%.6f,%.6f|%.6f,%.6f,%.6f) after_basis=(%.6f,%.6f,%.6f|%.6f,%.6f,%.6f|%.6f,%.6f,%.6f)\n",trace_side?"right":"left",time_ms,thigh_before_global[0],thigh_before_global[3],thigh_before_global[6],thigh_before_global[1],thigh_before_global[4],thigh_before_global[7],thigh_before_global[2],thigh_before_global[5],thigh_before_global[8],asset->bone_transforms[thigh].rotation[0],asset->bone_transforms[thigh].rotation[3],asset->bone_transforms[thigh].rotation[6],asset->bone_transforms[thigh].rotation[1],asset->bone_transforms[thigh].rotation[4],asset->bone_transforms[thigh].rotation[7],asset->bone_transforms[thigh].rotation[2],asset->bone_transforms[thigh].rotation[5],asset->bone_transforms[thigh].rotation[8]);

            /* Diagnostic only: retain the current thigh's complete frame while
             * changing its primary axis to e1.  The runtime path above uses a
             * parent-X frame, so this candidate exposes whether its secondary
             * basis is the source of the apparent half-turn. */
            basis[0][0]=thigh_before_global[0];basis[0][1]=thigh_before_global[3];basis[0][2]=thigh_before_global[6];
            basis[1][0]=thigh_before_global[1];basis[1][1]=thigh_before_global[4];basis[1][2]=thigh_before_global[7];
            basis[2][0]=thigh_before_global[2];basis[2][1]=thigh_before_global[5];basis[2][2]=thigh_before_global[8];
            primary_axis=0;best=-1.0;
            for(i=0;i<3;i++){value=fabs(basis[i][0]*e0[0]+basis[i][1]*e0[1]+basis[i][2]*e0[2]);if(value>best){best=value;primary_axis=i;}}
            primary[0]=e0[0];primary[1]=e0[1];primary[2]=e0[2];
            secondary_axis=(primary_axis+1)%3;if(secondary_axis==primary_axis)secondary_axis=(primary_axis+2)%3;
            third_axis=3-primary_axis-secondary_axis;
            projection=basis[secondary_axis][0]*primary[0]+basis[secondary_axis][1]*primary[1]+basis[secondary_axis][2]*primary[2];
            secondary[0]=basis[secondary_axis][0]-projection*primary[0];secondary[1]=basis[secondary_axis][1]-projection*primary[1];secondary[2]=basis[secondary_axis][2]-projection*primary[2];
            projection=model_vec_length(secondary);
            if(projection>0.000001){secondary[0]/=projection;secondary[1]/=projection;secondary[2]/=projection;}
            model_vec_cross(primary,secondary,tertiary);
            if(tertiary[0]*basis[third_axis][0]+tertiary[1]*basis[third_axis][1]+tertiary[2]*basis[third_axis][2]<0.0){secondary[0]=-secondary[0];secondary[1]=-secondary[1];secondary[2]=-secondary[2];model_vec_cross(primary,secondary,tertiary);}
            if(secondary[0]*h1[0]+secondary[1]*h1[1]+secondary[2]*h1[2]<0.0){h1[0]=-h1[0];h1[1]=-h1[1];h1[2]=-h1[2];}
            diag_base[0]=primary[0];diag_base[1]=secondary[0];diag_base[2]=tertiary[0];diag_base[3]=primary[1];diag_base[4]=secondary[1];diag_base[5]=tertiary[1];diag_base[6]=primary[2];diag_base[7]=secondary[2];diag_base[8]=tertiary[2];
            diag_desired[0]=e1[0];diag_desired[1]=h1[0];diag_desired[2]=z1[0];diag_desired[3]=e1[1];diag_desired[4]=h1[1];diag_desired[5]=z1[1];diag_desired[6]=e1[2];diag_desired[7]=h1[2];diag_desired[8]=z1[2];
            model_matrix_transpose(diag_base,diag_transpose);matrix_multiply(diag_desired,diag_transpose,diag_delta);matrix_multiply(diag_delta,thigh_before_global,diag_global);qdiag=model_matrix_to_quaternion(diag_global);
            __printf("analytic thigh fullframe candidate side=%s time=%d primary_axis=%d secondary_axis=%d q=(%.6f,%.6f,%.6f,%.6f) secondary=(%.6f,%.6f,%.6f) near_active=%d reconciled=%d unavailable=%d\n",trace_side?"right":"left",time_ms,primary_axis,secondary_axis,qdiag.x,qdiag.y,qdiag.z,qdiag.w,secondary[0],secondary[1],secondary[2],asset->ik_near_degenerate_ca_active[pole_side],asset->ik_near_degenerate_ca_reconciled[pole_side],asset->ik_near_degenerate_ca_unavailable[pole_side]);
        }
    }
    if (asset->ik_analytic_geometry_dump)
        __printf("analytic thigh controller=%s Kdesired=(%.3f,%.3f,%.3f) Kactual=(%.3f,%.3f,%.3f) error=%.6f\n",
                 asset->bones[ik->controller].name,kd[0],kd[1],kd[2],
                 asset->bone_transforms[knee].position[0],
                 asset->bone_transforms[knee].position[1],
                 asset->bone_transforms[knee].position[2],
                 model_vec_length((double[3]){
                     asset->bone_transforms[knee].position[0]-kd[0],
                     asset->bone_transforms[knee].position[1]-kd[1],
                     asset->bone_transforms[knee].position[2]-kd[2]}));
    parent=asset->bones[knee].parent;
    if(parent>=0) matrix_vector(asset->bone_transforms[parent].rotation,1.0,0.0,0.0,&hinge_axis[0],&hinge_axis[1],&hinge_axis[2]);
    else {hinge_axis[0]=1.0;hinge_axis[1]=0.0;hinge_axis[2]=0.0;}
    aa[0]=asset->bone_transforms[ik->target].position[0]-asset->bone_transforms[knee].position[0];
    aa[1]=asset->bone_transforms[ik->target].position[1]-asset->bone_transforms[knee].position[1];
    aa[2]=asset->bone_transforms[ik->target].position[2]-asset->bone_transforms[knee].position[2];
    bb[0]=target[0]-asset->bone_transforms[knee].position[0];
    bb[1]=target[1]-asset->bone_transforms[knee].position[1];
    bb[2]=target[2]-asset->bone_transforms[knee].position[2];
    dot2=aa[0]*hinge_axis[0]+aa[1]*hinge_axis[1]+aa[2]*hinge_axis[2];
    aa[0]-=hinge_axis[0]*dot2;aa[1]-=hinge_axis[1]*dot2;aa[2]-=hinge_axis[2]*dot2;
    dot2=bb[0]*hinge_axis[0]+bb[1]*hinge_axis[1]+bb[2]*hinge_axis[2];
    bb[0]-=hinge_axis[0]*dot2;bb[1]-=hinge_axis[1]*dot2;bb[2]-=hinge_axis[2]*dot2;
    aa_len=model_vec_length(aa);bb_len=model_vec_length(bb);
    if(aa_len<0.000001||bb_len<0.000001)return 0;
    aa[0]/=aa_len;aa[1]/=aa_len;aa[2]/=aa_len;bb[0]/=bb_len;bb[1]/=bb_len;bb[2]/=bb_len;
    model_vec_cross(aa,bb,cross2);
    signed2=atan2(hinge_axis[0]*cross2[0]+hinge_axis[1]*cross2[1]+hinge_axis[2]*cross2[2],aa[0]*bb[0]+aa[1]*bb[1]+aa[2]*bb[2]);
    base_knee_x = base_knee[0];
    if (asset->ik_analytic_geometry_dump) {
        double finite_error[3], finite_ankle[3][3];
        int finite_angle[3] = {base_knee_x, base_knee_x + 5, base_knee_x - 5};
        int finite_i;
        for (finite_i = 0; finite_i < 3; finite_i++) {
            asset->bones[knee].rotate_x = finite_angle[finite_i];
            asset->bones[knee].rotate_y = 0;
            asset->bones[knee].rotate_z = 0;
            rasterfall_model_update_bones(asset);
            finite_ankle[finite_i][0] = asset->bone_transforms[ik->target].position[0];
            finite_ankle[finite_i][1] = asset->bone_transforms[ik->target].position[1];
            finite_ankle[finite_i][2] = asset->bone_transforms[ik->target].position[2];
            finite_error[finite_i] = model_vec_length((double[3]){
                finite_ankle[finite_i][0] - target[0],
                finite_ankle[finite_i][1] - target[1],
                finite_ankle[finite_i][2] - target[2]});
        }
        asset->bones[knee].rotate_x = base_knee_x;
        asset->bones[knee].rotate_y = 0;
        asset->bones[knee].rotate_z = 0;
        rasterfall_model_update_bones(asset);
        __printf("analytic hinge finite-diff controller=%s base_x=%d predicted_correction=%.6fdeg hinge_axis=(%.6f,%.6f,%.6f) current_vec=(%.3f,%.3f,%.3f) desired_vec=(%.3f,%.3f,%.3f)\n",
                 asset->bones[ik->controller].name,base_knee_x,signed2*180.0/M_PI,
                 hinge_axis[0],hinge_axis[1],hinge_axis[2],
                 aa[0],aa[1],aa[2],bb[0],bb[1],bb[2]);
        __printf("analytic hinge finite-diff controller=%s angle=%d ankle=(%.3f,%.3f,%.3f) error=%.6f\n",
                 asset->bones[ik->controller].name,finite_angle[0],finite_ankle[0][0],finite_ankle[0][1],finite_ankle[0][2],finite_error[0]);
        __printf("analytic hinge finite-diff controller=%s angle=%d ankle=(%.3f,%.3f,%.3f) error=%.6f\n",
                 asset->bones[ik->controller].name,finite_angle[1],finite_ankle[1][0],finite_ankle[1][1],finite_ankle[1][2],finite_error[1]);
        __printf("analytic hinge finite-diff controller=%s angle=%d ankle=(%.3f,%.3f,%.3f) error=%.6f\n",
                 asset->bones[ik->controller].name,finite_angle[2],finite_ankle[2][0],finite_ankle[2][1],finite_ankle[2][2],finite_error[2]);
    }
    x=base_knee_x+rounded(signed2*180.0/M_PI);
    {
        int raw_knee_x=x;
        asset->ik_analytic_probe_ran=1;
        asset->ik_analytic_probe_raw_knee_x=raw_knee_x;
        asset->ik_analytic_probe_knee_valid=1;
        asset->ik_analytic_probe_ankle_error=0.0;
        if(asset->ik_limits_enabled &&
           model_clamp_angle(raw_knee_x,ik->links[0].lower[0],ik->links[0].upper[0]) != raw_knee_x)
            asset->ik_analytic_probe_knee_valid=0;
        if (!asset->ik_analytic_probe_knee_valid) asset->ik_analytic_last_reason=2;
        if (!asset->ik_analytic_probe_knee_valid) {
            asset->bones[thigh].rotate_x=base_thigh[0];asset->bones[thigh].rotate_y=base_thigh[1];asset->bones[thigh].rotate_z=base_thigh[2];
            asset->bones[knee].rotate_x=base_knee[0];asset->bones[knee].rotate_y=base_knee[1];asset->bones[knee].rotate_z=base_knee[2];
            rasterfall_model_update_bones(asset);
            return 0;
        }
    }
    if(asset->ik_limits_enabled)x=model_clamp_angle(x,ik->links[0].lower[0],ik->links[0].upper[0]);
    asset->bones[knee].rotate_x=x;asset->bones[knee].rotate_y=0;asset->bones[knee].rotate_z=0;
    rasterfall_model_update_bones(asset);
    current[0]=asset->bone_transforms[ik->target].position[0];current[1]=asset->bone_transforms[ik->target].position[1];current[2]=asset->bone_transforms[ik->target].position[2];
    offset[0]=current[0]-target[0];offset[1]=current[1]-target[1];offset[2]=current[2]-target[2];*after=model_vec_length(offset);
    asset->ik_analytic_probe_ankle_error=*after;
    {
        double normalized_error=*after/(l1+l2>0.000001?l1+l2:1.0);
        int bucket=9;
        if(normalized_error<0.005)bucket=0;else if(normalized_error<0.01)bucket=1;else if(normalized_error<0.02)bucket=2;else if(normalized_error<0.05)bucket=3;else if(normalized_error<0.10)bucket=4;else if(normalized_error<0.20)bucket=5;else if(normalized_error<0.30)bucket=6;else if(normalized_error<0.50)bucket=7;else if(normalized_error<1.0)bucket=8;
        if (!asset->ik_analytic_candidate_probe)
            asset->ik_analytic_error_hist[pole_side][bucket]++;
        if (normalized_error > 0.05) {
            asset->ik_analytic_last_reason=3;
            asset->bones[thigh].rotate_x=base_thigh[0];asset->bones[thigh].rotate_y=base_thigh[1];asset->bones[thigh].rotate_z=base_thigh[2];
            asset->bones[knee].rotate_x=base_knee[0];asset->bones[knee].rotate_y=base_knee[1];asset->bones[knee].rotate_z=base_knee[2];
            rasterfall_model_update_bones(asset);
            return 0;
        }
    }
    if (*after > *before && asset->ik_analytic_last_reason == 0)
        asset->ik_analytic_last_reason=3;
    if (!asset->ik_analytic_candidate_probe) {
        struct rasterfall_animation_quaternion tq =
            model_matrix_to_quaternion(asset->bone_transforms[thigh].rotation);
        struct rasterfall_animation_quaternion kq =
            model_matrix_to_quaternion(asset->bone_transforms[knee].rotation);
        struct rasterfall_animation_quaternion pq =
            rasterfall_animation_quat_from_euler(
                asset->ik_previous_final_thigh[pole_side][0],
                asset->ik_previous_final_thigh[pole_side][1],
                asset->ik_previous_final_thigh[pole_side][2]);
        asset->ik_candidate_trace_valid[pole_side] = 1;
        asset->ik_candidate_trace_previous_solver[pole_side] = asset->ik_last_leg_solver[pole_side];
        memcpy(asset->ik_candidate_trace_h[pole_side], h, 3 * sizeof(double));
        memcpy(asset->ik_candidate_trace_k0[pole_side], k0, 3 * sizeof(double));
        memcpy(asset->ik_candidate_trace_target[pole_side], target, 3 * sizeof(double));
        memcpy(asset->ik_candidate_trace_pole[pole_side], v, 3 * sizeof(double));
        asset->ik_candidate_trace_previous_q[pole_side][0]=pq.x;
        asset->ik_candidate_trace_previous_q[pole_side][1]=pq.y;
        asset->ik_candidate_trace_previous_q[pole_side][2]=pq.z;
        asset->ik_candidate_trace_previous_q[pole_side][3]=pq.w;
        asset->ik_candidate_trace_dynamic_q[pole_side][0]=tq.x;
        asset->ik_candidate_trace_dynamic_q[pole_side][1]=tq.y;
        asset->ik_candidate_trace_dynamic_q[pole_side][2]=tq.z;
        asset->ik_candidate_trace_dynamic_q[pole_side][3]=tq.w;
        asset->ik_candidate_trace_dynamic_knee_q[pole_side][0]=kq.x;
        asset->ik_candidate_trace_dynamic_knee_q[pole_side][1]=kq.y;
        asset->ik_candidate_trace_dynamic_knee_q[pole_side][2]=kq.z;
        asset->ik_candidate_trace_dynamic_knee_q[pole_side][3]=kq.w;
        asset->ik_candidate_trace_dynamic_branch[pole_side]=model_leg_branch(asset,pole_side,target,0);
        asset->ik_candidate_trace_dynamic_error[pole_side]=*after;
    }
    if (asset->ik_analytic_geometry_dump)
        __printf("analytic knee controller=%s hinge_axis=(%.6f,%.6f,%.6f) knee_x=%d Adesired=(%.3f,%.3f,%.3f) Aactual=(%.3f,%.3f,%.3f) knee_error=%.6f ankle_error=%.6f\n",
                 asset->bones[ik->controller].name,hinge_axis[0],hinge_axis[1],hinge_axis[2],x,
                 target[0],target[1],target[2],current[0],current[1],current[2],
                 model_vec_length((double[3]){
                     asset->bone_transforms[knee].position[0]-kd[0],
                     asset->bone_transforms[knee].position[1]-kd[1],
                     asset->bone_transforms[knee].position[2]-kd[2]}),*after);
    if (asset->ik_analytic_geometry_dump)
        __printf("analytic geometry controller=%s H=(%.3f,%.3f,%.3f) Kbind=(%.3f,%.3f,%.3f) Abind=(%.3f,%.3f,%.3f) bind_pole=(%.6f,%.6f,%.6f) target_pole=(%.6f,%.6f,%.6f) Kdesired=(%.3f,%.3f,%.3f) Kdistance=%.6f Adistance=%.6f\n",
                 asset->bones[ik->controller].name,h[0],h[1],h[2],k0[0],k0[1],k0[2],a0[0],a0[1],a0[2],bind_pole[0],bind_pole[1],bind_pole[2],v[0],v[1],v[2],kd[0],kd[1],kd[2],fabs(model_vec_length((double[3]){kd[0]-h[0],kd[1]-h[1],kd[2]-h[2]})-l1),fabs(model_vec_length((double[3]){target[0]-kd[0],target[1]-kd[1],target[2]-kd[2]})-l2));
    if (asset->ik_diagnostic_dump)
        __printf("analytic leg controller=%s time=%d H=(%.3f,%.3f,%.3f) Kdesired=(%.3f,%.3f,%.3f) Kactual=(%.3f,%.3f,%.3f) T=(%.3f,%.3f,%.3f) L1=%.3f L2=%.3f d=%.3f dc=%.3f knee_angle=%d thigh_local=(%d,%d,%d) ankle=(%.3f,%.3f,%.3f) error_before=%.3f error_after=%.3f clamped=%s\n",
                 asset->bones[ik->controller].name,time_ms,h[0],h[1],h[2],kd[0],kd[1],kd[2],asset->bone_transforms[knee].position[0],asset->bone_transforms[knee].position[1],asset->bone_transforms[knee].position[2],target[0],target[1],target[2],l1,l2,d,dc,x,asset->bones[thigh].rotate_x,asset->bones[thigh].rotate_y,asset->bones[thigh].rotate_z,current[0],current[1],current[2],*before,*after,clamped?"yes":"no");
    if (!asset->ik_analytic_candidate_probe) {
        asset->solver_metrics.ik_analytic_solved_count++;
        if(clamped)asset->solver_metrics.ik_analytic_clamped_count++;
    }
    return 1;
}

static int model_solve_leg_ccd(struct rasterfall_model_asset *asset,
                               const struct rasterfall_model_ik *ik,
                               const struct rasterfall_animation_clip *clip,
                               int time_ms, unsigned int *attempts,
                               double *before, double *after)
{
    /* The importer declares its source-to-model translation scale on the
     * format-neutral clip.  CCD is done on global positions;
     * the resulting global link rotation is converted back with the inverse
     * parent rotation before Euler/local PMX limits are applied. */
    const struct rasterfall_animation_track *track;
    double target[3], controller_global[3], current[3], offset[3], raw_translation[3];
    double target_parent_local[3], target_hierarchy[3];
    double before_target[3], before_knee[3], before_thigh[3], before_ankle[3];
    double after_target[3], after_knee[3], after_thigh[3];
    double after_ankle[3];
    int before_knee_rot[3], before_thigh_rot[3];
    int leg_side, continuous_branch;
    int continuous_thigh[3], continuous_knee;
    double continuous_score;
    unsigned long trace_improving = 0, trace_worsening = 0, trace_unchanged = 0;
    unsigned int iteration;
    int link_index;
    if (!asset || !ik || !clip || !attempts || !before || !after ||
        ik->controller < 0 || ik->target < 0 || ik->target >= (int)asset->bone_count ||
        ik->controller >= (int)asset->bone_count || !ik->link_count)
        return 0;
    leg_side=model_leg_ik_side(asset,ik);
    if (leg_side < 0) return 0;
    track = model_find_clip_track(clip, ik->controller);
    if (asset->ik_synthetic_target && leg_side != 0) return 0;
    if (!asset->ik_synthetic_target && !track) return 0;
    rasterfall_model_update_bones(asset);
    {
        int handoff_side = leg_side;
        int handoff_knee = ik->links[0].bone;
        int handoff_thigh = ik->links[ik->link_count - 1].bone;
        if (asset->ik_handoff_trace_time_ms == time_ms &&
            (asset->ik_handoff_trace_side < 0 ||
             asset->ik_handoff_trace_side == handoff_side)) {
            struct rasterfall_animation_quaternion tq =
                model_matrix_to_quaternion(asset->bone_transforms[handoff_thigh].rotation);
            struct rasterfall_animation_quaternion kq =
                model_matrix_to_quaternion(asset->bone_transforms[handoff_knee].rotation);
            asset->ik_handoff_snapshot_valid |= 1 << handoff_side;
            asset->ik_handoff_c0_thigh[handoff_side][0] = asset->bones[handoff_thigh].rotate_x;
            asset->ik_handoff_c0_thigh[handoff_side][1] = asset->bones[handoff_thigh].rotate_y;
            asset->ik_handoff_c0_thigh[handoff_side][2] = asset->bones[handoff_thigh].rotate_z;
            asset->ik_handoff_c0_knee[handoff_side] = asset->bones[handoff_knee].rotate_x;
            asset->ik_handoff_c0_thigh_global_q[handoff_side][0] = tq.x;
            asset->ik_handoff_c0_thigh_global_q[handoff_side][1] = tq.y;
            asset->ik_handoff_c0_thigh_global_q[handoff_side][2] = tq.z;
            asset->ik_handoff_c0_thigh_global_q[handoff_side][3] = tq.w;
            asset->ik_handoff_c0_knee_global_q[handoff_side][0] = kq.x;
            asset->ik_handoff_c0_knee_global_q[handoff_side][1] = kq.y;
            asset->ik_handoff_c0_knee_global_q[handoff_side][2] = kq.z;
            asset->ik_handoff_c0_knee_global_q[handoff_side][3] = kq.w;
            asset->ik_handoff_c0_ankle[handoff_side][0] = asset->bone_transforms[ik->target].position[0];
            asset->ik_handoff_c0_ankle[handoff_side][1] = asset->bone_transforms[ik->target].position[1];
            asset->ik_handoff_c0_ankle[handoff_side][2] = asset->bone_transforms[ik->target].position[2];
        }
    }
    {
        int warm_side = leg_side;
        int warm_knee = ik->links[0].bone;
        int warm_thigh = ik->links[ik->link_count - 1].bone;
        if (asset->ik_warm_start_diagnostic && warm_side >= 0 &&
            warm_side < 2 && asset->ik_warm_start_valid[warm_side]) {
            asset->bones[warm_knee].rotate_x = asset->ik_warm_start_knee[warm_side][0];
            asset->bones[warm_knee].rotate_y = asset->ik_warm_start_knee[warm_side][1];
            asset->bones[warm_knee].rotate_z = asset->ik_warm_start_knee[warm_side][2];
            asset->bones[warm_thigh].rotate_x = asset->ik_warm_start_thigh[warm_side][0];
            asset->bones[warm_thigh].rotate_y = asset->ik_warm_start_thigh[warm_side][1];
            asset->bones[warm_thigh].rotate_z = asset->ik_warm_start_thigh[warm_side][2];
            rasterfall_model_update_bones(asset);
        }
    }
    controller_global[0] = asset->bone_transforms[ik->controller].position[0];
    controller_global[1] = asset->bone_transforms[ik->controller].position[1];
    controller_global[2] = asset->bone_transforms[ik->controller].position[2];
    target[0] = controller_global[0];
    target[1] = controller_global[1];
    target[2] = controller_global[2];
    raw_translation[0] = raw_translation[1] = raw_translation[2] = 0.0;
    if (asset->ik_synthetic_target) {
        target[0] += asset->ik_synthetic_offset[0];
        target[1] += asset->ik_synthetic_offset[1];
        target[2] += asset->ik_synthetic_offset[2];
    } else {
        model_sample_track_translation(track, time_ms, clip->duration_ms,
                                       raw_translation);
        double scale=model_clip_translation_scale(clip);
        target[0] += raw_translation[0] * scale;
        target[1] += raw_translation[1] * scale;
        target[2] += raw_translation[2] * scale;
    }
    if (asset->ik_target_space_diagnostic && !asset->ik_synthetic_target) {
        int parent = asset->bones[ik->controller].parent;
        double scaled[3], rotated[3];
        scaled[0] = raw_translation[0] * model_clip_translation_scale(clip);
        scaled[1] = raw_translation[1] * model_clip_translation_scale(clip);
        scaled[2] = raw_translation[2] * model_clip_translation_scale(clip);
        target_parent_local[0] = asset->bones[ik->controller].rest_x -
                                 (parent >= 0 ? asset->bones[parent].rest_x : 0);
        target_parent_local[1] = asset->bones[ik->controller].rest_y -
                                 (parent >= 0 ? asset->bones[parent].rest_y : 0);
        target_parent_local[2] = asset->bones[ik->controller].rest_z -
                                 (parent >= 0 ? asset->bones[parent].rest_z : 0);
        target_parent_local[0] += scaled[0];
        target_parent_local[1] += scaled[1];
        target_parent_local[2] += scaled[2];
        if (parent >= 0) {
            matrix_vector(asset->bone_transforms[parent].rotation,
                          target_parent_local[0], target_parent_local[1],
                          target_parent_local[2], &rotated[0], &rotated[1],
                          &rotated[2]);
            target_hierarchy[0] = asset->bone_transforms[parent].position[0] + rotated[0];
            target_hierarchy[1] = asset->bone_transforms[parent].position[1] + rotated[1];
            target_hierarchy[2] = asset->bone_transforms[parent].position[2] + rotated[2];
        } else {
            target_hierarchy[0] = target_parent_local[0];
            target_hierarchy[1] = target_parent_local[1];
            target_hierarchy[2] = target_parent_local[2];
        }
        {
            int thigh = ik->links[ik->link_count - 1].bone;
            double upper[3], lower[3], max_reach;
            upper[0] = asset->bone_transforms[thigh].position[0] - asset->bone_transforms[ik->links[0].bone].position[0];
            upper[1] = asset->bone_transforms[thigh].position[1] - asset->bone_transforms[ik->links[0].bone].position[1];
            upper[2] = asset->bone_transforms[thigh].position[2] - asset->bone_transforms[ik->links[0].bone].position[2];
            lower[0] = asset->bone_transforms[ik->links[0].bone].position[0] - asset->bone_transforms[ik->target].position[0];
            lower[1] = asset->bone_transforms[ik->links[0].bone].position[1] - asset->bone_transforms[ik->target].position[1];
            lower[2] = asset->bone_transforms[ik->links[0].bone].position[2] - asset->bone_transforms[ik->target].position[2];
            max_reach = model_vec_length(upper) + model_vec_length(lower);
            __printf("ik target-space phase_ms=%d controller=%s parent=%s parent_parent=%s\n",
                     time_ms, asset->bones[ik->controller].name,
                     parent >= 0 ? asset->bones[parent].name : "<root>",
                     parent >= 0 && asset->bones[parent].parent >= 0 ?
                         asset->bones[asset->bones[parent].parent].name : "<root>");
            __printf("  rest_local=(%d,%d,%d) parent_global_pos=(%.3f,%.3f,%.3f)\n",
                     asset->bones[ik->controller].rest_x - (parent >= 0 ? asset->bones[parent].rest_x : 0),
                     asset->bones[ik->controller].rest_y - (parent >= 0 ? asset->bones[parent].rest_y : 0),
                     asset->bones[ik->controller].rest_z - (parent >= 0 ? asset->bones[parent].rest_z : 0),
                     parent >= 0 ? asset->bone_transforms[parent].position[0] : 0.0,
                     parent >= 0 ? asset->bone_transforms[parent].position[1] : 0.0,
                     parent >= 0 ? asset->bone_transforms[parent].position[2] : 0.0);
            if (parent >= 0)
                __printf("  parent_global_rot=[%.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f]\n",
                         asset->bone_transforms[parent].rotation[0], asset->bone_transforms[parent].rotation[1], asset->bone_transforms[parent].rotation[2], asset->bone_transforms[parent].rotation[3], asset->bone_transforms[parent].rotation[4], asset->bone_transforms[parent].rotation[5], asset->bone_transforms[parent].rotation[6], asset->bone_transforms[parent].rotation[7], asset->bone_transforms[parent].rotation[8]);
            __printf("  vmd=(%.3f,%.3f,%.3f) A=(%.3f,%.3f,%.3f) B=(%.3f,%.3f,%.3f)\n",
                     raw_translation[0], raw_translation[1], raw_translation[2],
                     target[0], target[1], target[2], target_hierarchy[0],
                     target_hierarchy[1], target_hierarchy[2]);
            __printf("  A_hip_distance=%.3f B_hip_distance=%.3f max_reach=%.3f A_reachable=%s B_reachable=%s\n",
                     model_vec_length((double[3]){target[0] - asset->bone_transforms[thigh].position[0], target[1] - asset->bone_transforms[thigh].position[1], target[2] - asset->bone_transforms[thigh].position[2]}),
                     model_vec_length((double[3]){target_hierarchy[0] - asset->bone_transforms[thigh].position[0], target_hierarchy[1] - asset->bone_transforms[thigh].position[1], target_hierarchy[2] - asset->bone_transforms[thigh].position[2]}),
                     max_reach,
                     model_vec_length((double[3]){target[0] - asset->bone_transforms[thigh].position[0], target[1] - asset->bone_transforms[thigh].position[1], target[2] - asset->bone_transforms[thigh].position[2]}) <= max_reach ? "yes" : "no",
                     model_vec_length((double[3]){target_hierarchy[0] - asset->bone_transforms[thigh].position[0], target_hierarchy[1] - asset->bone_transforms[thigh].position[1], target_hierarchy[2] - asset->bone_transforms[thigh].position[2]}) <= max_reach ? "yes" : "no");
        }
    }
    current[0] = asset->bone_transforms[ik->target].position[0];
    current[1] = asset->bone_transforms[ik->target].position[1];
    current[2] = asset->bone_transforms[ik->target].position[2];
    offset[0] = current[0] - target[0]; offset[1] = current[1] - target[1];
    offset[2] = current[2] - target[2];
    *before = model_vec_length(offset);
    {
        int ccd_side = leg_side;
        asset->ik_ccd_diag_valid[ccd_side] = 1;
        asset->ik_ccd_diag_initial_error[ccd_side] = *before;
        asset->ik_ccd_diag_best_error[ccd_side] = *before;
        asset->ik_ccd_diag_final_error[ccd_side] = *before;
        asset->ik_ccd_diag_best_iteration[ccd_side] = 0;
        asset->ik_ccd_diag_iterations[ccd_side] = 0;
        asset->ik_ccd_diag_best_is_c0[ccd_side] = 1;
        asset->ik_ccd_diag_c0_branch_sign[ccd_side] =
            model_ccd_branch_sign(asset, ik, target);
        asset->ik_ccd_diag_best_branch_sign[ccd_side] =
            asset->ik_ccd_diag_c0_branch_sign[ccd_side];
        asset->ik_ccd_diag_best_same_error[ccd_side] =
            asset->ik_ccd_diag_c0_branch_sign[ccd_side] ? *before : 1.0e30;
        asset->ik_ccd_diag_best_mirror_error[ccd_side] = 1.0e30;
        asset->ik_ccd_diag_best_same_iteration[ccd_side] = (unsigned int)-1;
        asset->ik_ccd_diag_best_mirror_iteration[ccd_side] = (unsigned int)-1;
        asset->ik_ccd_diag_first_mirror_iteration[ccd_side] = (unsigned int)-1;
        asset->ik_ccd_diag_target[ccd_side][0] = target[0];
        asset->ik_ccd_diag_target[ccd_side][1] = target[1];
        asset->ik_ccd_diag_target[ccd_side][2] = target[2];
        asset->ik_ccd_diag_initial_ankle[ccd_side][0] = current[0];
        asset->ik_ccd_diag_initial_ankle[ccd_side][1] = current[1];
        asset->ik_ccd_diag_initial_ankle[ccd_side][2] = current[2];
        {
            int knee = ik->links[0].bone;
            int thigh = ik->links[ik->link_count - 1].bone;
            asset->ik_ccd_diag_best_thigh[ccd_side][0] = asset->bones[thigh].rotate_x;
            asset->ik_ccd_diag_best_thigh[ccd_side][1] = asset->bones[thigh].rotate_y;
            asset->ik_ccd_diag_best_thigh[ccd_side][2] = asset->bones[thigh].rotate_z;
            asset->ik_ccd_diag_best_knee[ccd_side] = asset->bones[knee].rotate_x;
        }
    }
    continuous_branch=asset->ik_ccd_diag_c0_branch_sign[leg_side];
    if (!continuous_branch && asset->ik_previous_final_valid[leg_side])
        continuous_branch=asset->ik_previous_final_branch[leg_side];
    continuous_score=*before;
    continuous_thigh[0]=asset->bones[ik->links[ik->link_count-1].bone].rotate_x;
    continuous_thigh[1]=asset->bones[ik->links[ik->link_count-1].bone].rotate_y;
    continuous_thigh[2]=asset->bones[ik->links[ik->link_count-1].bone].rotate_z;
    continuous_knee=asset->bones[ik->links[0].bone].rotate_x;
    {
        int knee = ik->links[0].bone;
        int thigh = ik->links[ik->link_count - 1].bone;
        double upper[3], lower[3], reach_distance, max_reach, ratio;
        upper[0] = asset->bone_transforms[thigh].position[0] - asset->bone_transforms[knee].position[0];
        upper[1] = asset->bone_transforms[thigh].position[1] - asset->bone_transforms[knee].position[1];
        upper[2] = asset->bone_transforms[thigh].position[2] - asset->bone_transforms[knee].position[2];
        lower[0] = asset->bone_transforms[knee].position[0] - asset->bone_transforms[ik->target].position[0];
        lower[1] = asset->bone_transforms[knee].position[1] - asset->bone_transforms[ik->target].position[1];
        lower[2] = asset->bone_transforms[knee].position[2] - asset->bone_transforms[ik->target].position[2];
        max_reach = model_vec_length(upper) + model_vec_length(lower);
        reach_distance = model_vec_length((double[3]){
            target[0] - asset->bone_transforms[thigh].position[0],
            target[1] - asset->bone_transforms[thigh].position[1],
            target[2] - asset->bone_transforms[thigh].position[2]});
        ratio = max_reach > 0.0 ? reach_distance / max_reach : 0.0;
        asset->solver_metrics.ik_reach_sample_count++;
        asset->solver_metrics.ik_reach_distance_total += reach_distance;
        asset->solver_metrics.ik_reach_ratio_total += ratio;
        if (reach_distance > asset->solver_metrics.ik_reach_distance_max)
            asset->solver_metrics.ik_reach_distance_max = reach_distance;
        if (ratio > asset->solver_metrics.ik_reach_ratio_max) asset->solver_metrics.ik_reach_ratio_max = ratio;
        if (reach_distance > max_reach) asset->solver_metrics.ik_unreachable_count++;
    }
    *after = *before;
    before_target[0] = target[0]; before_target[1] = target[1]; before_target[2] = target[2];
    before_ankle[0] = current[0]; before_ankle[1] = current[1]; before_ankle[2] = current[2];
    memcpy(before_knee, asset->bone_transforms[ik->links[0].bone].position, sizeof(before_knee));
    memcpy(before_thigh, asset->bone_transforms[ik->links[ik->link_count-1].bone].position, sizeof(before_thigh));
    before_knee_rot[0] = asset->bones[ik->links[0].bone].rotate_x;
    before_knee_rot[1] = asset->bones[ik->links[0].bone].rotate_y;
    before_knee_rot[2] = asset->bones[ik->links[0].bone].rotate_z;
    before_thigh_rot[0] = asset->bones[ik->links[ik->link_count-1].bone].rotate_x;
    before_thigh_rot[1] = asset->bones[ik->links[ik->link_count-1].bone].rotate_y;
    before_thigh_rot[2] = asset->bones[ik->links[ik->link_count-1].bone].rotate_z;
    for (iteration = 0; iteration < (unsigned int)ik->iterations; iteration++) {
        int changed = 0, update_order;
        (*attempts)++;
        for (update_order = 0; update_order < (int)ik->link_count; update_order++) {
            link_index = asset->ik_diagnostic_reverse_order ?
                (int)ik->link_count - 1 - update_order : update_order;
            const struct rasterfall_model_ik_link *link = &ik->links[link_index];
            struct rasterfall_model_bone_transform *lt;
            double from[3], to[3], axis[3], cross_len, dot, angle;
            double delta[9], global[9], next_global[9], parent_inverse[9], local[9];
            double error_before_link, hinge_correction = 0.0;
            int x, y, z, parent, hinge_path;
            if (link->bone < 0 || link->bone >= (int)asset->bone_count) return 0;
            lt = &asset->bone_transforms[link->bone];
            from[0] = asset->bone_transforms[ik->target].position[0] - lt->position[0];
            from[1] = asset->bone_transforms[ik->target].position[1] - lt->position[1];
            from[2] = asset->bone_transforms[ik->target].position[2] - lt->position[2];
            to[0] = target[0] - lt->position[0];
            to[1] = target[1] - lt->position[1];
            to[2] = target[2] - lt->position[2];
            if (model_vec_length(from) < 0.000001 || model_vec_length(to) < 0.000001)
                continue;
            error_before_link = *after;
            hinge_path = !asset->ik_legacy_knee_ccd && link_index == 0 &&
                         model_is_x_hinge(link);
            if (hinge_path) {
                double hinge_axis[3], a[3], b[3], cross[3];
                double a_len, b_len, signed_angle, current_angle, next_angle;
                parent = asset->bones[link->bone].parent;
                if (parent < 0) {
                    hinge_axis[0] = 1.0; hinge_axis[1] = 0.0; hinge_axis[2] = 0.0;
                } else {
                    matrix_vector(asset->bone_transforms[parent].rotation,
                                  1.0, 0.0, 0.0, &hinge_axis[0],
                                  &hinge_axis[1], &hinge_axis[2]);
                }
                a[0] = from[0]; a[1] = from[1]; a[2] = from[2];
                b[0] = to[0]; b[1] = to[1]; b[2] = to[2];
                a[0] -= hinge_axis[0] * (a[0]*hinge_axis[0] + a[1]*hinge_axis[1] + a[2]*hinge_axis[2]);
                a[1] -= hinge_axis[1] * (from[0]*hinge_axis[0] + from[1]*hinge_axis[1] + from[2]*hinge_axis[2]);
                a[2] -= hinge_axis[2] * (from[0]*hinge_axis[0] + from[1]*hinge_axis[1] + from[2]*hinge_axis[2]);
                b[0] -= hinge_axis[0] * (to[0]*hinge_axis[0] + to[1]*hinge_axis[1] + to[2]*hinge_axis[2]);
                b[1] -= hinge_axis[1] * (to[0]*hinge_axis[0] + to[1]*hinge_axis[1] + to[2]*hinge_axis[2]);
                b[2] -= hinge_axis[2] * (to[0]*hinge_axis[0] + to[1]*hinge_axis[1] + to[2]*hinge_axis[2]);
                a_len = model_vec_length(a); b_len = model_vec_length(b);
                if (a_len < 0.000001 || b_len < 0.000001) continue;
                a[0]/=a_len; a[1]/=a_len; a[2]/=a_len;
                b[0]/=b_len; b[1]/=b_len; b[2]/=b_len;
                model_vec_cross(a,b,cross);
                signed_angle = atan2(hinge_axis[0]*cross[0] + hinge_axis[1]*cross[1] + hinge_axis[2]*cross[2],
                                     a[0]*b[0] + a[1]*b[1] + a[2]*b[2]);
                if (ik->angle > 0.0f && fabs(signed_angle) > ik->angle)
                    signed_angle = signed_angle < 0.0 ? -ik->angle : ik->angle;
                if (fabs(signed_angle) > 0.25)
                    signed_angle = signed_angle < 0.0 ? -0.25 : 0.25;
                signed_angle *= (asset->ik_diagnostic_knee_scale_milli > 0 ?
                                 asset->ik_diagnostic_knee_scale_milli : 1000) / 1000.0;
                current_angle = asset->bones[link->bone].rotate_x * M_PI / 180.0;
                next_angle = current_angle + signed_angle;
                x = asset->ik_limits_enabled ?
                    model_clamp_angle(rounded(next_angle * 180.0 / M_PI),
                                      link->lower[0], link->upper[0]) :
                    rounded(next_angle * 180.0 / M_PI);
                y = z = 0;
                hinge_correction = signed_angle * 180.0 / M_PI;
                axis[0] = signed_angle < 0.0 ? -hinge_axis[0] : hinge_axis[0];
                axis[1] = signed_angle < 0.0 ? -hinge_axis[1] : hinge_axis[1];
                axis[2] = signed_angle < 0.0 ? -hinge_axis[2] : hinge_axis[2];
                angle = fabs(signed_angle);
                memcpy(global, lt->rotation, sizeof(global));
                matrix_rotate_xyz(x, y, z, local);
            } else {
            model_vec_cross(from, to, axis); cross_len = model_vec_length(axis);
            dot = (from[0]*to[0] + from[1]*to[1] + from[2]*to[2]) /
                  (model_vec_length(from) * model_vec_length(to));
            if (dot > 1.0) dot = 1.0;
            if (dot < -1.0) dot = -1.0;
            if (cross_len < 0.000001) continue;
            axis[0] /= cross_len; axis[1] /= cross_len; axis[2] /= cross_len;
            angle = atan2(cross_len, dot);
            if (ik->angle > 0.0f && angle > ik->angle) angle = ik->angle;
            if (angle > 0.25) angle = 0.25;
            if (link_index == (int)ik->link_count - 1)
                angle *= (asset->ik_diagnostic_thigh_scale_milli > 0 ?
                          asset->ik_diagnostic_thigh_scale_milli : 1000) / 1000.0;
            if (angle < 0.000001) continue;
            model_matrix_axis_angle(axis, angle, delta);
            memcpy(global, lt->rotation, sizeof(global));
            matrix_multiply(delta, global, next_global);
            parent = asset->bones[link->bone].parent;
            if (parent < 0) memcpy(local, next_global, sizeof(local));
            else {
                model_matrix_transpose(asset->bone_transforms[parent].rotation,
                                       parent_inverse);
                matrix_multiply(parent_inverse, next_global, local);
            }
            model_matrix_to_euler(local, &x, &y, &z);
            }
            if (asset->ik_iteration_trace_time_ms == time_ms) {
                struct rasterfall_animation_quaternion correction = {
                    axis[0] * sin(angle / 2.0), axis[1] * sin(angle / 2.0),
                    axis[2] * sin(angle / 2.0), cos(angle / 2.0)};
                struct rasterfall_animation_quaternion global_q =
                    model_matrix_to_quaternion(global);
                struct rasterfall_animation_quaternion parent_q =
                    parent >= 0 ? model_matrix_to_quaternion(
                        asset->bone_transforms[parent].rotation) :
                        (struct rasterfall_animation_quaternion){0,0,0,1};
                struct rasterfall_animation_quaternion local_q =
                    model_matrix_to_quaternion(local);
                __printf("ik iteration trace controller=%s time=%d iteration=%u "
                         "knee_global_before_q=(%.6f,%.6f,%.6f,%.6f) "
                         "correction_axis_angle=(%.6f,%.6f,%.6f,%.6f) "
                         "correction_q=(%.6f,%.6f,%.6f,%.6f) "
                         "parent_global_q=(%.6f,%.6f,%.6f,%.6f) "
                         "local_before_clamp_q=(%.6f,%.6f,%.6f,%.6f) "
                         "local_euler_before_clamp=(%d,%d,%d)\n",
                         asset->bones[ik->controller].name, time_ms, iteration,
                         global_q.x,global_q.y,global_q.z,global_q.w,
                         axis[0],axis[1],axis[2],
                         hinge_path ? hinge_correction : angle*180.0/M_PI,
                         correction.x,correction.y,correction.z,correction.w,
                         parent_q.x,parent_q.y,parent_q.z,parent_q.w,
                         local_q.x,local_q.y,local_q.z,local_q.w,x,y,z);
            }
            if (link->limited && asset->ik_limits_enabled) {
                x = model_clamp_angle(x, link->lower[0], link->upper[0]);
                y = model_clamp_angle(y, link->lower[1], link->upper[1]);
                z = model_clamp_angle(z, link->lower[2], link->upper[2]);
            }
            if (asset->ik_iteration_trace_time_ms == time_ms) {
                double clamped_local[9];
                struct rasterfall_animation_quaternion final_q;
                matrix_rotate_xyz(x,y,z,clamped_local);
                final_q = model_matrix_to_quaternion(clamped_local);
                __printf("ik iteration trace controller=%s time=%d iteration=%u "
                         "local_euler_after_clamp=(%d,%d,%d) "
                         "final_local_q=(%.6f,%.6f,%.6f,%.6f)\n",
                         asset->bones[ik->controller].name, time_ms, iteration,
                         x,y,z,final_q.x,final_q.y,final_q.z,final_q.w);
            }
            asset->bones[link->bone].rotate_x = x;
            asset->bones[link->bone].rotate_y = y;
            asset->bones[link->bone].rotate_z = z;
            rasterfall_model_update_bones(asset);
            changed = 1;
            current[0] = asset->bone_transforms[ik->target].position[0];
            current[1] = asset->bone_transforms[ik->target].position[1];
            current[2] = asset->bone_transforms[ik->target].position[2];
            offset[0] = current[0] - target[0]; offset[1] = current[1] - target[1];
            offset[2] = current[2] - target[2];
            *after = model_vec_length(offset);
            if (asset->ik_iteration_trace_time_ms == time_ms) {
                if (*after < error_before_link) trace_improving++;
                else if (*after > error_before_link) trace_worsening++;
                else trace_unchanged++;
                __printf("ik iteration error controller=%s time=%d iteration=%u "
                         "joint=%s path=%s error_before_joint=%.3f "
                         "error_after_joint=%.3f %s knee_angle=%d thigh=(%d,%d,%d)\n",
                         asset->bones[ik->controller].name,time_ms,iteration,
                         link_index == 0 ? "knee" : "thigh",
                         hinge_path ? "hinge" : "3d",error_before_link,*after,
                         *after < error_before_link ? "improving" :
                         *after > error_before_link ? "worsening" : "unchanged",
                         asset->bones[ik->links[0].bone].rotate_x,
                         asset->bones[ik->links[ik->link_count-1].bone].rotate_x,
                         asset->bones[ik->links[ik->link_count-1].bone].rotate_y,
                         asset->bones[ik->links[ik->link_count-1].bone].rotate_z);
            }
            if (*after < 8.0) break;
        }
        {
            int ccd_side = leg_side;
            int knee = ik->links[0].bone;
            int thigh = ik->links[ik->link_count - 1].bone;
            int branch_sign = model_ccd_branch_sign(asset, ik, target);
            double score=*after;
            asset->ik_ccd_diag_iterations[ccd_side] = iteration + 1;
            if (branch_sign && branch_sign == asset->ik_ccd_diag_c0_branch_sign[ccd_side]) {
                if (*after < asset->ik_ccd_diag_best_same_error[ccd_side]) {
                    asset->ik_ccd_diag_best_same_error[ccd_side] = *after;
                    asset->ik_ccd_diag_best_same_iteration[ccd_side] = iteration;
                }
            } else if (branch_sign && asset->ik_ccd_diag_c0_branch_sign[ccd_side] &&
                       branch_sign != asset->ik_ccd_diag_c0_branch_sign[ccd_side]) {
                if (asset->ik_ccd_diag_first_mirror_iteration[ccd_side] == (unsigned int)-1)
                    asset->ik_ccd_diag_first_mirror_iteration[ccd_side] = iteration;
                if (*after < asset->ik_ccd_diag_best_mirror_error[ccd_side]) {
                    asset->ik_ccd_diag_best_mirror_error[ccd_side] = *after;
                    asset->ik_ccd_diag_best_mirror_iteration[ccd_side] = iteration;
                }
            }
            if (*after < asset->ik_ccd_diag_best_error[ccd_side]) {
                asset->ik_ccd_diag_best_error[ccd_side] = *after;
                asset->ik_ccd_diag_best_iteration[ccd_side] = iteration;
                asset->ik_ccd_diag_best_is_c0[ccd_side] = 0;
                asset->ik_ccd_diag_best_branch_sign[ccd_side] = branch_sign;
                asset->ik_ccd_diag_best_thigh[ccd_side][0] = asset->bones[thigh].rotate_x;
                asset->ik_ccd_diag_best_thigh[ccd_side][1] = asset->bones[thigh].rotate_y;
                asset->ik_ccd_diag_best_thigh[ccd_side][2] = asset->bones[thigh].rotate_z;
                asset->ik_ccd_diag_best_knee[ccd_side] = asset->bones[knee].rotate_x;
            }
            /* Preserve the entering knee branch among the candidates. */
            if ((!continuous_branch ||
                 (branch_sign && branch_sign==continuous_branch)) &&
                score < continuous_score) {
                if (!continuous_branch) continuous_branch=branch_sign;
                continuous_score=score;
                continuous_thigh[0]=asset->bones[thigh].rotate_x;
                continuous_thigh[1]=asset->bones[thigh].rotate_y;
                continuous_thigh[2]=asset->bones[thigh].rotate_z;
                continuous_knee=asset->bones[knee].rotate_x;
            }
        }
        if (!changed || *after < 8.0) break;
    }
    if (asset->ik_best_iteration_enabled) {
        int knee = ik->links[0].bone;
        int thigh = ik->links[ik->link_count - 1].bone;
        asset->bones[thigh].rotate_x = continuous_thigh[0];
        asset->bones[thigh].rotate_y = continuous_thigh[1];
        asset->bones[thigh].rotate_z = continuous_thigh[2];
        asset->bones[knee].rotate_x = continuous_knee;
        asset->bones[knee].rotate_y = 0;
        asset->bones[knee].rotate_z = 0;
    }
    rasterfall_model_update_bones(asset);
    if (asset->ik_previous_final_valid[leg_side])
        model_limit_leg_lateral_step(asset,ik,before_thigh_rot,
                                     before_knee_rot[0],before_ankle);
    if (asset->ik_previous_final_enabled) {
        int final_side = leg_side;
        int final_knee = ik->links[0].bone;
        int final_thigh = ik->links[ik->link_count - 1].bone;
        asset->ik_previous_final_thigh[final_side][0] = asset->bones[final_thigh].rotate_x;
        asset->ik_previous_final_thigh[final_side][1] = asset->bones[final_thigh].rotate_y;
        asset->ik_previous_final_thigh[final_side][2] = asset->bones[final_thigh].rotate_z;
        asset->ik_previous_final_knee[final_side] = asset->bones[final_knee].rotate_x;
        {
            double final_target[3], final_ratio=0.0;
        asset->ik_previous_final_branch[final_side] = 0;
        if (model_analytic_target(asset,ik,clip,time_ms,final_target))
            {
                asset->ik_previous_final_branch[final_side]=model_leg_branch(asset,final_side,final_target,&final_ratio);
                model_store_previous_final_bend(asset,final_side,final_target,2);
            }
        }
        asset->ik_previous_final_valid[final_side] = 1;
    }
    if (asset->ik_iteration_trace_time_ms == time_ms)
        {
            int trace_side = leg_side;
            __printf("ik iteration summary controller=%s time=%d improving=%lu "
                     "worsening=%lu unchanged=%lu best=%s:%u best_error=%.3f final_error=%.3f\n",
                     asset->bones[ik->controller].name,time_ms,
                     trace_improving,trace_worsening,trace_unchanged,
                     asset->ik_ccd_diag_best_is_c0[trace_side] ? "C0" : "iteration",
                     asset->ik_ccd_diag_best_iteration[trace_side],
                     asset->ik_ccd_diag_best_error[trace_side],
                     asset->ik_ccd_diag_final_error[trace_side]);
        }
    {
        int warm_side = leg_side;
        int warm_knee = ik->links[0].bone;
        int warm_thigh = ik->links[ik->link_count - 1].bone;
        if (asset->ik_warm_start_diagnostic && warm_side >= 0 && warm_side < 2) {
            asset->ik_warm_start_knee[warm_side][0] = asset->bones[warm_knee].rotate_x;
            asset->ik_warm_start_knee[warm_side][1] = asset->bones[warm_knee].rotate_y;
            asset->ik_warm_start_knee[warm_side][2] = asset->bones[warm_knee].rotate_z;
            asset->ik_warm_start_thigh[warm_side][0] = asset->bones[warm_thigh].rotate_x;
            asset->ik_warm_start_thigh[warm_side][1] = asset->bones[warm_thigh].rotate_y;
            asset->ik_warm_start_thigh[warm_side][2] = asset->bones[warm_thigh].rotate_z;
            asset->ik_warm_start_valid[warm_side] = 1;
        }
    }
    current[0] = asset->bone_transforms[ik->target].position[0];
    current[1] = asset->bone_transforms[ik->target].position[1];
    current[2] = asset->bone_transforms[ik->target].position[2];
    offset[0] = current[0] - target[0]; offset[1] = current[1] - target[1];
    offset[2] = current[2] - target[2];
    *after = model_vec_length(offset);
    {
        int ccd_side = leg_side;
        asset->ik_ccd_diag_final_error[ccd_side] = *after;
        asset->ik_ccd_diag_final_ankle[ccd_side][0] = current[0];
        asset->ik_ccd_diag_final_ankle[ccd_side][1] = current[1];
        asset->ik_ccd_diag_final_ankle[ccd_side][2] = current[2];
        {
            int knee = ik->links[0].bone;
            int thigh = ik->links[ik->link_count - 1].bone;
            struct rasterfall_animation_quaternion tq =
                model_matrix_to_quaternion(asset->bone_transforms[thigh].rotation);
            struct rasterfall_animation_quaternion kq =
                model_matrix_to_quaternion(asset->bone_transforms[knee].rotation);
            asset->ik_solver_return_thigh[ccd_side][0] = asset->bones[thigh].rotate_x;
            asset->ik_solver_return_thigh[ccd_side][1] = asset->bones[thigh].rotate_y;
            asset->ik_solver_return_thigh[ccd_side][2] = asset->bones[thigh].rotate_z;
            asset->ik_solver_return_knee[ccd_side] = asset->bones[knee].rotate_x;
            asset->ik_solver_return_thigh_global_q[ccd_side][0] = tq.x;
            asset->ik_solver_return_thigh_global_q[ccd_side][1] = tq.y;
            asset->ik_solver_return_thigh_global_q[ccd_side][2] = tq.z;
            asset->ik_solver_return_thigh_global_q[ccd_side][3] = tq.w;
            memcpy(asset->ik_solver_return_thigh_global_frame[ccd_side],
                   asset->bone_transforms[thigh].rotation,
                   sizeof(asset->ik_solver_return_thigh_global_frame[ccd_side]));
            asset->ik_solver_return_thigh_frame_valid[ccd_side] = 1;
            asset->ik_solver_return_knee_global_q[ccd_side][0] = kq.x;
            asset->ik_solver_return_knee_global_q[ccd_side][1] = kq.y;
            asset->ik_solver_return_knee_global_q[ccd_side][2] = kq.z;
            asset->ik_solver_return_knee_global_q[ccd_side][3] = kq.w;
            memcpy(asset->ik_solver_return_ankle[ccd_side], current, sizeof(current));
            memcpy(asset->ik_solver_return_target[ccd_side], target, sizeof(target));
            asset->ik_solver_return_error[ccd_side] = *after;
        }
    }
    if (asset->ik_handoff_trace_time_ms == time_ms) {
        int handoff_side = leg_side;
        if (asset->ik_handoff_trace_side < 0 ||
            asset->ik_handoff_trace_side == handoff_side) {
            int handoff_knee = ik->links[0].bone;
            int handoff_thigh = ik->links[ik->link_count - 1].bone;
            struct rasterfall_animation_quaternion tq =
                model_matrix_to_quaternion(asset->bone_transforms[handoff_thigh].rotation);
            struct rasterfall_animation_quaternion kq =
                model_matrix_to_quaternion(asset->bone_transforms[handoff_knee].rotation);
            asset->ik_handoff_c1_thigh[handoff_side][0] = asset->bones[handoff_thigh].rotate_x;
            asset->ik_handoff_c1_thigh[handoff_side][1] = asset->bones[handoff_thigh].rotate_y;
            asset->ik_handoff_c1_thigh[handoff_side][2] = asset->bones[handoff_thigh].rotate_z;
            asset->ik_handoff_c1_knee[handoff_side] = asset->bones[handoff_knee].rotate_x;
            asset->ik_handoff_c1_thigh_global_q[handoff_side][0] = tq.x;
            asset->ik_handoff_c1_thigh_global_q[handoff_side][1] = tq.y;
            asset->ik_handoff_c1_thigh_global_q[handoff_side][2] = tq.z;
            asset->ik_handoff_c1_thigh_global_q[handoff_side][3] = tq.w;
            asset->ik_handoff_c1_knee_global_q[handoff_side][0] = kq.x;
            asset->ik_handoff_c1_knee_global_q[handoff_side][1] = kq.y;
            asset->ik_handoff_c1_knee_global_q[handoff_side][2] = kq.z;
            asset->ik_handoff_c1_knee_global_q[handoff_side][3] = kq.w;
            asset->ik_handoff_c1_ankle[handoff_side][0] = asset->bone_transforms[ik->target].position[0];
            asset->ik_handoff_c1_ankle[handoff_side][1] = asset->bone_transforms[ik->target].position[1];
            asset->ik_handoff_c1_ankle[handoff_side][2] = asset->bone_transforms[ik->target].position[2];
        }
    }
    if (asset->ik_diagnostic_dump) {
        int knee = ik->links[0].bone;
        int thigh = ik->links[ik->link_count - 1].bone;
        memcpy(after_target, target, sizeof(after_target));
        memcpy(after_ankle, asset->bone_transforms[ik->target].position, sizeof(after_ankle));
        memcpy(after_knee, asset->bone_transforms[knee].position, sizeof(after_knee));
        memcpy(after_thigh, asset->bone_transforms[thigh].position, sizeof(after_thigh));
        __printf("ik pose diagnostic controller=%s phase_ms=%d limits=%s synthetic=%s\n",
                 asset->bones[ik->controller].name, time_ms,
                 asset->ik_limits_enabled ? "on" : "off",
                 asset->ik_synthetic_target ? "yes" : "no");
        __printf("  controller_global=(%.3f,%.3f,%.3f) vmd_translation=(%.3f,%.3f,%.3f) desired_global=(%.3f,%.3f,%.3f)\n",
                 controller_global[0], controller_global[1], controller_global[2],
                 raw_translation[0], raw_translation[1], raw_translation[2],
                 after_target[0], after_target[1], after_target[2]);
        __printf("  solve_before upper_leg=(%.3f,%.3f,%.3f) knee=(%.3f,%.3f,%.3f) ankle=(%.3f,%.3f,%.3f) thigh_local=(%d,%d,%d) knee_local=(%d,%d,%d)\n",
                 before_thigh[0],before_thigh[1],before_thigh[2],before_knee[0],before_knee[1],before_knee[2],before_ankle[0],before_ankle[1],before_ankle[2],before_thigh_rot[0],before_thigh_rot[1],before_thigh_rot[2],before_knee_rot[0],before_knee_rot[1],before_knee_rot[2]);
        __printf("  solve_after  upper_leg=(%.3f,%.3f,%.3f) knee=(%.3f,%.3f,%.3f) ankle=(%.3f,%.3f,%.3f) thigh_local=(%d,%d,%d) knee_local=(%d,%d,%d)\n",
                 after_thigh[0],after_thigh[1],after_thigh[2],after_knee[0],after_knee[1],after_knee[2],after_ankle[0],after_ankle[1],after_ankle[2],asset->bones[thigh].rotate_x,asset->bones[thigh].rotate_y,asset->bones[thigh].rotate_z,asset->bones[knee].rotate_x,asset->bones[knee].rotate_y,asset->bones[knee].rotate_z);
        __printf("  error_before=%.3f error_after=%.3f hip_to_target_before=%.3f hip_to_target_after=%.3f upper_length=%.3f lower_length=%.3f max_reach=%.3f final_update_bones_pose=(%d,%d,%d)/(%d,%d,%d)\n",
                 *before,*after,model_vec_length((double[3]){before_target[0]-before_thigh[0],before_target[1]-before_thigh[1],before_target[2]-before_thigh[2]}),model_vec_length((double[3]){after_target[0]-after_thigh[0],after_target[1]-after_thigh[1],after_target[2]-after_thigh[2]}),sqrt((before_thigh[0]-before_knee[0])*(before_thigh[0]-before_knee[0])+(before_thigh[1]-before_knee[1])*(before_thigh[1]-before_knee[1])+(before_thigh[2]-before_knee[2])*(before_thigh[2]-before_knee[2])),sqrt((before_knee[0]-before_ankle[0])*(before_knee[0]-before_ankle[0])+(before_knee[1]-before_ankle[1])*(before_knee[1]-before_ankle[1])+(before_knee[2]-before_ankle[2])*(before_knee[2]-before_ankle[2])),sqrt((before_thigh[0]-before_knee[0])*(before_thigh[0]-before_knee[0])+(before_thigh[1]-before_knee[1])*(before_thigh[1]-before_knee[1])+(before_thigh[2]-before_knee[2])*(before_thigh[2]-before_knee[2]))+sqrt((before_knee[0]-before_ankle[0])*(before_knee[0]-before_ankle[0])+(before_knee[1]-before_ankle[1])*(before_knee[1]-before_ankle[1])+(before_knee[2]-before_ankle[2])*(before_knee[2]-before_ankle[2])),asset->bones[thigh].rotate_x,asset->bones[thigh].rotate_y,asset->bones[thigh].rotate_z,asset->bones[knee].rotate_x,asset->bones[knee].rotate_y,asset->bones[knee].rotate_z);
    }
    return 1;
}

static int model_try_compatible_analytic(
    struct rasterfall_model_asset *asset, const struct rasterfall_model_ik *ik,
    const struct rasterfall_animation_clip *clip, int time_ms, int side,
    int previous_solver, int previous_valid, const int previous_thigh[3], int previous_knee,
    const int base_thigh[3], const int base_knee[3], double before, double dynamic_after,
    double *selected_after)
{
    double target[3], current_ratio=0.0, previous_ratio=0.0;
    int current_sign, previous_sign, compat_sign, thigh, knee, old_override, compat_ok;
    int dynamic_thigh[3], dynamic_knee;
    int dynamic_reason, dynamic_source, dynamic_override, dynamic_raw, dynamic_knee_valid, compat_reason;
    double dynamic_probe_error;
    double dynamic_selected[3], compat_before=0.0, compat_after=0.0;
    double dynamic_kperp[3], previous_kperp[3], branch_angle=0.0;
    if (asset->ik_synthetic_target || !previous_valid) return 0;
    if (!model_analytic_target(asset,ik,clip,time_ms,target)) return 0;
    thigh=ik->links[ik->link_count-1].bone;knee=ik->links[0].bone;
    dynamic_thigh[0]=asset->bones[thigh].rotate_x;dynamic_thigh[1]=asset->bones[thigh].rotate_y;dynamic_thigh[2]=asset->bones[thigh].rotate_z;dynamic_knee=asset->bones[knee].rotate_x;
    current_sign=model_leg_branch(asset,side,target,&current_ratio);
    model_leg_kperp(asset,side,target,dynamic_kperp);
    asset->bones[thigh].rotate_x=previous_thigh[0];asset->bones[thigh].rotate_y=previous_thigh[1];asset->bones[thigh].rotate_z=previous_thigh[2];
    asset->bones[knee].rotate_x=previous_knee;asset->bones[knee].rotate_y=0;asset->bones[knee].rotate_z=0;rasterfall_model_update_bones(asset);
    previous_sign=model_leg_branch(asset,side,target,&previous_ratio);
    model_leg_kperp(asset,side,target,previous_kperp);
    branch_angle=model_leg_branch_angle_from_kperp(dynamic_kperp,previous_kperp);
    asset->bones[thigh].rotate_x=base_thigh[0];asset->bones[thigh].rotate_y=base_thigh[1];asset->bones[thigh].rotate_z=base_thigh[2];
    asset->bones[knee].rotate_x=base_knee[0];asset->bones[knee].rotate_y=base_knee[1];asset->bones[knee].rotate_z=base_knee[2];rasterfall_model_update_bones(asset);
    if (current_ratio<0.05 || previous_ratio<0.05 || !current_sign || !previous_sign || current_sign==previous_sign) {
        asset->bones[thigh].rotate_x=dynamic_thigh[0];asset->bones[thigh].rotate_y=dynamic_thigh[1];asset->bones[thigh].rotate_z=dynamic_thigh[2];
        asset->bones[knee].rotate_x=dynamic_knee;asset->bones[knee].rotate_y=0;asset->bones[knee].rotate_z=0;rasterfall_model_update_bones(asset);
        return 0;
    }
    dynamic_selected[0]=asset->ik_analytic_last_selected_pole[side][0];dynamic_selected[1]=asset->ik_analytic_last_selected_pole[side][1];dynamic_selected[2]=asset->ik_analytic_last_selected_pole[side][2];
    asset->ik_analytic_last_dynamic_error[side]=dynamic_after;asset->ik_analytic_last_compatible_valid[side]=0;asset->ik_analytic_last_compatible_branch[side]=0;asset->ik_analytic_last_compatible_error[side]=0.0;
    dynamic_reason=asset->ik_analytic_last_reason;dynamic_source=asset->ik_analytic_last_pole_source[side];dynamic_override=asset->ik_analytic_last_pole_override[side];
    dynamic_raw=asset->ik_analytic_probe_raw_knee_x;dynamic_knee_valid=asset->ik_analytic_probe_knee_valid;dynamic_probe_error=asset->ik_analytic_probe_ankle_error;
    old_override=asset->ik_analytic_pole_override;asset->ik_analytic_pole_override=1;
    asset->ik_analytic_pole[0]=-dynamic_selected[0];asset->ik_analytic_pole[1]=-dynamic_selected[1];asset->ik_analytic_pole[2]=-dynamic_selected[2];
    asset->ik_analytic_candidate_probe=1;
    compat_ok=model_solve_one_leg_analytic(asset,ik,clip,time_ms,0,&compat_before,&compat_after);
    compat_reason=asset->ik_analytic_last_reason;
    compat_sign=compat_ok ? model_leg_branch(asset,side,target,0) : 0;
    if (asset->ik_candidate_trace_valid[side]) {
        struct rasterfall_animation_quaternion tq =
            model_matrix_to_quaternion(asset->bone_transforms[thigh].rotation);
        struct rasterfall_animation_quaternion kq =
            model_matrix_to_quaternion(asset->bone_transforms[knee].rotation);
        asset->ik_candidate_trace_compatible_q[side][0]=tq.x;
        asset->ik_candidate_trace_compatible_q[side][1]=tq.y;
        asset->ik_candidate_trace_compatible_q[side][2]=tq.z;
        asset->ik_candidate_trace_compatible_q[side][3]=tq.w;
        asset->ik_candidate_trace_compatible_knee_q[side][0]=kq.x;
        asset->ik_candidate_trace_compatible_knee_q[side][1]=kq.y;
        asset->ik_candidate_trace_compatible_knee_q[side][2]=kq.z;
        asset->ik_candidate_trace_compatible_knee_q[side][3]=kq.w;
        asset->ik_candidate_trace_compatible_branch[side]=compat_sign;
        asset->ik_candidate_trace_compatible_error[side]=compat_after;
        asset->ik_candidate_trace_compatible_valid[side]=compat_ok;
    }
    asset->ik_analytic_last_compatible_pole[side][0]=-dynamic_selected[0];asset->ik_analytic_last_compatible_pole[side][1]=-dynamic_selected[1];asset->ik_analytic_last_compatible_pole[side][2]=-dynamic_selected[2];
    asset->ik_analytic_last_compatible_error[side]=compat_after;asset->ik_analytic_last_compatible_branch[side]=compat_sign;
    asset->ik_analytic_candidate_probe=0;asset->ik_analytic_pole_override=old_override;
    compat_ok = compat_ok && compat_sign==previous_sign && compat_reason==0 && compat_after<=before;
    asset->ik_analytic_last_compatible_valid[side]=compat_ok;
    if (compat_ok && (compat_after<=dynamic_after*1.10 || (branch_angle>=150.0 && compat_after<=dynamic_after*1.25))) {
        if (selected_after) *selected_after=compat_after;
        asset->ik_analytic_compatible_selected[side]++;
        if (previous_solver == 1)
            asset->ik_analytic_compatible_selected_aa[side]++;
        asset->ik_analytic_last_takeover_reason[side]=1;
        if (branch_angle>=150.0 && compat_after>dynamic_after*1.10)
            asset->ik_analytic_compatible_extreme_selected[side]++;
        return 1;
    }
    asset->bones[thigh].rotate_x=dynamic_thigh[0];asset->bones[thigh].rotate_y=dynamic_thigh[1];asset->bones[thigh].rotate_z=dynamic_thigh[2];
    asset->bones[knee].rotate_x=dynamic_knee;asset->bones[knee].rotate_y=0;asset->bones[knee].rotate_z=0;rasterfall_model_update_bones(asset);
    asset->ik_analytic_last_selected_pole[side][0]=dynamic_selected[0];asset->ik_analytic_last_selected_pole[side][1]=dynamic_selected[1];asset->ik_analytic_last_selected_pole[side][2]=dynamic_selected[2];
    asset->ik_analytic_last_reason=dynamic_reason;asset->ik_analytic_last_pole_source[side]=dynamic_source;asset->ik_analytic_last_pole_override[side]=dynamic_override;
    asset->ik_analytic_probe_raw_knee_x=dynamic_raw;asset->ik_analytic_probe_knee_valid=dynamic_knee_valid;asset->ik_analytic_probe_ankle_error=dynamic_probe_error;
    if (!compat_ok || compat_reason==2 || compat_reason==4) {
        asset->ik_analytic_compatible_unavailable[side]++;asset->ik_analytic_last_takeover_reason[side]=2;
    } else {
        asset->ik_analytic_compatible_costly[side]++;asset->ik_analytic_last_takeover_reason[side]=3;
    }
    return 0;
}

static void model_set_leg_pose_euler(
    struct rasterfall_model_asset *asset, int thigh, int knee,
    struct rasterfall_animation_quaternion thigh_q,
    struct rasterfall_animation_quaternion knee_q)
{
    struct rasterfall_animation_rotation thigh_euler, knee_euler;
    struct rasterfall_animation_quaternion source = thigh_q;
    rasterfall_animation_quat_to_euler(thigh_q, &thigh_euler);
    rasterfall_animation_quat_to_euler(knee_q, &knee_euler);
    asset->bones[thigh].rotate_x=thigh_euler.x;
    asset->bones[thigh].rotate_y=thigh_euler.y;
    asset->bones[thigh].rotate_z=thigh_euler.z;
    asset->bones[knee].rotate_x=knee_euler.x;
    asset->bones[knee].rotate_y=knee_euler.y;
    asset->bones[knee].rotate_z=knee_euler.z;
    if (model_render_trace_leg_bone(thigh)) {
        asset->render_trace_write_bone=thigh;
        asset->render_trace_write_valid=1;
        asset->render_trace_write_path=2;
        asset->render_trace_write_valid_by_bone[model_render_trace_leg_slot(thigh)]=1;
        asset->render_trace_write_path_by_bone[model_render_trace_leg_slot(thigh)]=2;
        asset->render_trace_local_before_write_q[0]=source.x;
        asset->render_trace_local_before_write_q[1]=source.y;
        asset->render_trace_local_before_write_q[2]=source.z;
        asset->render_trace_local_before_write_q[3]=source.w;
        memcpy(asset->render_trace_local_before_write_q_by_bone[model_render_trace_leg_slot(thigh)],
               asset->render_trace_local_before_write_q, 4 * sizeof(double));
        source=rasterfall_animation_quat_normalize(source);
        model_trace_quaternion_matrix(source,
            asset->render_trace_local_before_write_matrix);
        memcpy(asset->render_trace_local_before_write_matrix_by_bone[model_render_trace_leg_slot(thigh)],
               asset->render_trace_local_before_write_matrix, 9 * sizeof(double));
        memcpy(asset->render_trace_euler_convert_by_bone[model_render_trace_leg_slot(thigh)],
               asset->render_trace_euler_convert, 3 * sizeof(int));
        {
            double result_global[9];
            int parent=asset->bones[thigh].parent;
            struct rasterfall_animation_quaternion q;
            if (parent>=0)
                matrix_multiply(asset->bone_transforms[parent].rotation,
                                asset->render_trace_local_before_write_matrix,
                                result_global);
            else
                memcpy(result_global, asset->render_trace_local_before_write_matrix,
                       sizeof(result_global));
            q=model_matrix_to_quaternion(result_global);
            asset->render_trace_ik_result_global_q[0]=q.x;
            asset->render_trace_ik_result_global_q[1]=q.y;
            asset->render_trace_ik_result_global_q[2]=q.z;
            asset->render_trace_ik_result_global_q[3]=q.w;
            memcpy(asset->render_trace_ik_result_global_q_by_bone[model_render_trace_leg_slot(thigh)],
                   asset->render_trace_ik_result_global_q, 4 * sizeof(double));
        }
        asset->render_trace_euler_convert[0]=thigh_euler.x;
        asset->render_trace_euler_convert[1]=thigh_euler.y;
        asset->render_trace_euler_convert[2]=thigh_euler.z;
        memcpy(asset->render_trace_euler_convert_by_bone[model_render_trace_leg_slot(thigh)],
               asset->render_trace_euler_convert, 3 * sizeof(int));
    }
    rasterfall_model_update_bones(asset);
    if (model_render_trace_leg_bone(thigh)) {
        struct rasterfall_animation_quaternion q =
            model_matrix_to_quaternion(asset->bone_transforms[thigh].rotation);
        asset->render_trace_final_bone_q[0]=q.x;
        asset->render_trace_final_bone_q[1]=q.y;
        asset->render_trace_final_bone_q[2]=q.z;
        asset->render_trace_final_bone_q[3]=q.w;
        memcpy(asset->render_trace_final_bone_q_by_bone[model_render_trace_leg_slot(thigh)],
               asset->render_trace_final_bone_q, 4 * sizeof(double));
    }
}

static void model_limit_leg_lateral_step(
    struct rasterfall_model_asset *asset,
    const struct rasterfall_model_ik *ik,
    const int source_thigh[3], int source_knee,
    const double source_ankle[3])
{
    struct rasterfall_animation_quaternion from_thigh, from_knee;
    struct rasterfall_animation_quaternion to_thigh, to_knee;
    double upper[3], lower[3], reach, lateral, limit;
    int thigh, knee, factor, iteration;
    if (!asset || !ik || !source_thigh || !source_ankle ||
        ik->link_count < 2 || ik->target < 0 ||
        ik->target >= (int)asset->bone_count) return;
    knee=ik->links[0].bone;
    thigh=ik->links[ik->link_count-1].bone;
    if (knee < 0 || knee >= (int)asset->bone_count ||
        thigh < 0 || thigh >= (int)asset->bone_count) return;
    lateral=fabs(asset->bone_transforms[ik->target].position[0]-source_ankle[0]);
    upper[0]=asset->bone_transforms[knee].position[0]-
        asset->bone_transforms[thigh].position[0];
    upper[1]=asset->bone_transforms[knee].position[1]-
        asset->bone_transforms[thigh].position[1];
    upper[2]=asset->bone_transforms[knee].position[2]-
        asset->bone_transforms[thigh].position[2];
    lower[0]=asset->bone_transforms[ik->target].position[0]-
        asset->bone_transforms[knee].position[0];
    lower[1]=asset->bone_transforms[ik->target].position[1]-
        asset->bone_transforms[knee].position[1];
    lower[2]=asset->bone_transforms[ik->target].position[2]-
        asset->bone_transforms[knee].position[2];
    reach=model_vec_length(upper)+model_vec_length(lower);
    /* Lateral motion is the ill-conditioned axis near full extension.  One
     * sample may move at most 1.5 percent of the leg length on that axis. */
    limit=reach*0.015;
    if (lateral <= limit || lateral < 0.000001) return;
    factor=(int)(limit*1000.0/lateral);
    if (factor < 1) factor=1;
    if (factor > 1000) factor=1000;
    from_thigh=rasterfall_animation_quat_from_euler(
        source_thigh[0],source_thigh[1],source_thigh[2]);
    from_knee=rasterfall_animation_quat_from_euler(source_knee,0,0);
    to_thigh=rasterfall_animation_quat_from_euler(
        asset->bones[thigh].rotate_x,asset->bones[thigh].rotate_y,
        asset->bones[thigh].rotate_z);
    to_knee=rasterfall_animation_quat_from_euler(
        asset->bones[knee].rotate_x,asset->bones[knee].rotate_y,
        asset->bones[knee].rotate_z);
    for (iteration=0;iteration<4;iteration++) {
        model_set_leg_pose_euler(
            asset,thigh,knee,
            rasterfall_animation_quat_nlerp(from_thigh,to_thigh,factor),
            rasterfall_animation_quat_nlerp(from_knee,to_knee,factor));
        lateral=fabs(asset->bone_transforms[ik->target].position[0]-
                     source_ankle[0]);
        if (lateral <= limit*1.05 || factor <= 1) break;
        factor=(int)(factor*limit/lateral);
        if (factor < 1) factor=1;
    }
}

static int model_reconcile_analytical_leg_transition(
    struct rasterfall_model_asset *asset, const struct rasterfall_model_ik *ik,
    const struct rasterfall_animation_clip *clip, int time_ms, int side,
    int thigh, int knee, int previous_valid, const int previous_thigh[3],
    int previous_knee)
{
    double target[3], previous_ankle[3], ratio=0.0, previous_ratio=0.0;
    int current_sign, previous_sign, start_transition=0;
    struct rasterfall_animation_quaternion current_thigh_q, current_knee_q;
    struct rasterfall_animation_quaternion previous_thigh_q, previous_knee_q;
    struct rasterfall_animation_quaternion result_thigh_q, result_knee_q;
    if (side < 0 || side >= 2 || !previous_valid ||
        !model_analytic_target(asset,ik,clip,time_ms,target)) return 0;

    current_sign=model_leg_branch(asset,side,target,&ratio);
    current_thigh_q=rasterfall_animation_quat_from_euler(
        asset->bones[thigh].rotate_x,asset->bones[thigh].rotate_y,asset->bones[thigh].rotate_z);
    current_knee_q=rasterfall_animation_quat_from_euler(
        asset->bones[knee].rotate_x,asset->bones[knee].rotate_y,asset->bones[knee].rotate_z);
    previous_thigh_q=rasterfall_animation_quat_from_euler(
        previous_thigh[0],previous_thigh[1],previous_thigh[2]);
    previous_knee_q=rasterfall_animation_quat_from_euler(previous_knee,0,0);

    asset->bones[thigh].rotate_x=previous_thigh[0];
    asset->bones[thigh].rotate_y=previous_thigh[1];
    asset->bones[thigh].rotate_z=previous_thigh[2];
    asset->bones[knee].rotate_x=previous_knee;
    asset->bones[knee].rotate_y=0;
    asset->bones[knee].rotate_z=0;
    rasterfall_model_update_bones(asset);
    previous_sign=model_leg_branch(asset,side,target,&previous_ratio);
    memcpy(previous_ankle,asset->bone_transforms[ik->target].position,
           sizeof(previous_ankle));
    model_set_leg_pose_euler(asset,thigh,knee,current_thigh_q,current_knee_q);
    model_limit_leg_lateral_step(asset,ik,previous_thigh,previous_knee,
                                 previous_ankle);
    current_thigh_q=rasterfall_animation_quat_from_euler(
        asset->bones[thigh].rotate_x,asset->bones[thigh].rotate_y,
        asset->bones[thigh].rotate_z);
    current_knee_q=rasterfall_animation_quat_from_euler(
        asset->bones[knee].rotate_x,asset->bones[knee].rotate_y,
        asset->bones[knee].rotate_z);

    {
        double qdot=current_thigh_q.x*previous_thigh_q.x+
            current_thigh_q.y*previous_thigh_q.y+
            current_thigh_q.z*previous_thigh_q.z+
            current_thigh_q.w*previous_thigh_q.w;
        if(qdot<0.0)qdot=-qdot;
        if(qdot>1.0)qdot=1.0;
        if (!asset->ik_leg_transition_active[side] &&
            ratio >= 0.05 && previous_ratio >= 0.05 &&
            current_sign && previous_sign && current_sign != previous_sign &&
            2.0*atan2(sqrt(1.0-qdot*qdot),qdot)*180.0/M_PI > 90.0) {
        start_transition=1;
        asset->ik_leg_hard_branch_switch_count[side]++;
        }
    }

    if (start_transition) {
        asset->ik_leg_transition_active[side]=1;
        asset->ik_leg_transition_start_time[side]=time_ms;
        asset->ik_leg_transition_duration[side]=4;
        asset->ik_leg_transition_remaining[side]=4;
        asset->ik_leg_transition_reason[side]=1;
        asset->ik_leg_transition_old_branch[side]=previous_sign;
        asset->ik_leg_transition_new_branch[side]=current_sign;
        asset->ik_leg_transition_old_thigh_q[side][0]=previous_thigh_q.x;
        asset->ik_leg_transition_old_thigh_q[side][1]=previous_thigh_q.y;
        asset->ik_leg_transition_old_thigh_q[side][2]=previous_thigh_q.z;
        asset->ik_leg_transition_old_thigh_q[side][3]=previous_thigh_q.w;
        asset->ik_leg_transition_new_thigh_q[side][0]=current_thigh_q.x;
        asset->ik_leg_transition_new_thigh_q[side][1]=current_thigh_q.y;
        asset->ik_leg_transition_new_thigh_q[side][2]=current_thigh_q.z;
        asset->ik_leg_transition_new_thigh_q[side][3]=current_thigh_q.w;
        asset->ik_leg_transition_old_knee_q[side][0]=previous_knee_q.x;
        asset->ik_leg_transition_old_knee_q[side][1]=previous_knee_q.y;
        asset->ik_leg_transition_old_knee_q[side][2]=previous_knee_q.z;
        asset->ik_leg_transition_old_knee_q[side][3]=previous_knee_q.w;
        asset->ik_leg_transition_new_knee_q[side][0]=current_knee_q.x;
        asset->ik_leg_transition_new_knee_q[side][1]=current_knee_q.y;
        asset->ik_leg_transition_new_knee_q[side][2]=current_knee_q.z;
        asset->ik_leg_transition_new_knee_q[side][3]=current_knee_q.w;
        asset->ik_leg_transition_started_count[side]++;
    }

    if (!asset->ik_leg_transition_active[side]) return 0;
    {
        int duration=asset->ik_leg_transition_duration[side];
        int step=duration-asset->ik_leg_transition_remaining[side]+1;
        int factor=duration>0 ? (step*1000)/duration : 1000;
        previous_thigh_q.x=asset->ik_leg_transition_old_thigh_q[side][0];
        previous_thigh_q.y=asset->ik_leg_transition_old_thigh_q[side][1];
        previous_thigh_q.z=asset->ik_leg_transition_old_thigh_q[side][2];
        previous_thigh_q.w=asset->ik_leg_transition_old_thigh_q[side][3];
        current_thigh_q.x=asset->ik_leg_transition_new_thigh_q[side][0];
        current_thigh_q.y=asset->ik_leg_transition_new_thigh_q[side][1];
        current_thigh_q.z=asset->ik_leg_transition_new_thigh_q[side][2];
        current_thigh_q.w=asset->ik_leg_transition_new_thigh_q[side][3];
        previous_knee_q.x=asset->ik_leg_transition_old_knee_q[side][0];
        previous_knee_q.y=asset->ik_leg_transition_old_knee_q[side][1];
        previous_knee_q.z=asset->ik_leg_transition_old_knee_q[side][2];
        previous_knee_q.w=asset->ik_leg_transition_old_knee_q[side][3];
        current_knee_q.x=asset->ik_leg_transition_new_knee_q[side][0];
        current_knee_q.y=asset->ik_leg_transition_new_knee_q[side][1];
        current_knee_q.z=asset->ik_leg_transition_new_knee_q[side][2];
        current_knee_q.w=asset->ik_leg_transition_new_knee_q[side][3];
        result_thigh_q=rasterfall_animation_quat_nlerp(previous_thigh_q,current_thigh_q,factor);
        result_knee_q=rasterfall_animation_quat_nlerp(previous_knee_q,current_knee_q,factor);
        model_set_leg_pose_euler(asset,thigh,knee,result_thigh_q,result_knee_q);
        model_limit_leg_lateral_step(asset,ik,previous_thigh,previous_knee,
                                     previous_ankle);
    }
    asset->ik_leg_transition_remaining[side]--;
    if (asset->ik_leg_transition_remaining[side] <= 0) {
        asset->ik_leg_transition_active[side]=0;
        asset->ik_leg_transition_completed_count[side]++;
    }
    return 1;
}

static void model_solve_leg_iks(
    struct rasterfall_model_asset *asset,
    const struct rasterfall_animation_clip *clip, int time_ms)
{
    unsigned int i, attempts = 0, max_attempts = 0;
    double before_total = 0.0, after_total = 0.0;
    double before_max = 0.0, after_max = 0.0, before, after;
    int solved = 0;
    for (i = 0; i < asset->ik_count; i++) {
        unsigned int one_attempts = 0;
        int analytic_thigh[3], analytic_knee[3];
        int analytic_ok;
        int leg_side = model_leg_ik_side(asset,&asset->iks[i]);
        int previous_solver=0, previous_valid=0, previous_thigh[3]={0,0,0}, previous_knee=0;
        if (leg_side >= 0 && asset->ik_previous_final_enabled) {
            previous_solver=asset->ik_last_leg_solver[leg_side];
            previous_valid=asset->ik_previous_final_valid[leg_side];
            previous_thigh[0]=asset->ik_previous_final_thigh[leg_side][0];previous_thigh[1]=asset->ik_previous_final_thigh[leg_side][1];previous_thigh[2]=asset->ik_previous_final_thigh[leg_side][2];
            previous_knee=asset->ik_previous_final_knee[leg_side];
        }
        analytic_thigh[0]=analytic_thigh[1]=analytic_thigh[2]=0;
        analytic_knee[0]=analytic_knee[1]=analytic_knee[2]=0;
        if (asset->iks[i].link_count >= 2 &&
            asset->iks[i].links[0].bone >= 0 &&
            asset->iks[i].links[asset->iks[i].link_count-1].bone >= 0) {
            int thigh0 = asset->iks[i].links[asset->iks[i].link_count-1].bone;
            int knee0 = asset->iks[i].links[0].bone;
            if (thigh0 < (int)asset->bone_count && knee0 < (int)asset->bone_count) {
                analytic_thigh[0]=asset->bones[thigh0].rotate_x;
                analytic_thigh[1]=asset->bones[thigh0].rotate_y;
                analytic_thigh[2]=asset->bones[thigh0].rotate_z;
                analytic_knee[0]=asset->bones[knee0].rotate_x;
                analytic_knee[1]=asset->bones[knee0].rotate_y;
                analytic_knee[2]=asset->bones[knee0].rotate_z;
            }
        }
        if (!asset->ik_legacy_knee_ccd &&
            (analytic_ok=model_solve_one_leg_analytic(asset, &asset->iks[i], clip, time_ms,
                                         &one_attempts, &before, &after))) {
            if (after > before) {
                int thigh=asset->iks[i].links[asset->iks[i].link_count-1].bone;
                int knee=asset->iks[i].links[0].bone;
                asset->bones[thigh].rotate_x=analytic_thigh[0];asset->bones[thigh].rotate_y=analytic_thigh[1];asset->bones[thigh].rotate_z=analytic_thigh[2];
                asset->bones[knee].rotate_x=analytic_knee[0];asset->bones[knee].rotate_y=analytic_knee[1];asset->bones[knee].rotate_z=analytic_knee[2];
                rasterfall_model_update_bones(asset);
                asset->solver_metrics.ik_analytic_rejected_count++;
                asset->solver_metrics.ik_analytic_solved_count--;
                (void)analytic_ok;
            } else {
            if (leg_side >= 0) {
                asset->ik_analytic_last_takeover_reason[leg_side]=0;
                asset->ik_analytic_last_dynamic_error[leg_side]=after;asset->ik_analytic_last_compatible_valid[leg_side]=0;asset->ik_analytic_last_compatible_branch[leg_side]=0;asset->ik_analytic_last_compatible_error[leg_side]=0.0;
                if(previous_solver==2)asset->ik_analytic_normal_dynamic[leg_side]++;
            }
            if (leg_side >= 0 && model_try_compatible_analytic(asset,&asset->iks[i],clip,time_ms,leg_side,previous_solver,previous_valid,previous_thigh,previous_knee,analytic_thigh,analytic_knee,before,after,&after)) {
                /* The compatible candidate is already installed. */
            }
            if (leg_side >= 0)
                model_reconcile_analytical_leg_transition(
                    asset,&asset->iks[i],clip,time_ms,leg_side,
                    asset->iks[i].links[asset->iks[i].link_count-1].bone,
                    asset->iks[i].links[0].bone,previous_valid,
                    previous_thigh,previous_knee);
            if (leg_side >= 0 && asset->ik_candidate_trace_valid[leg_side]) {
                int selected_thigh = asset->iks[i].links[asset->iks[i].link_count-1].bone;
                struct rasterfall_animation_quaternion tq =
                    model_matrix_to_quaternion(asset->bone_transforms[selected_thigh].rotation);
                double selected_target[3], selected_error = 0.0;
                asset->ik_candidate_trace_selected_q[leg_side][0]=tq.x;
                asset->ik_candidate_trace_selected_q[leg_side][1]=tq.y;
                asset->ik_candidate_trace_selected_q[leg_side][2]=tq.z;
                asset->ik_candidate_trace_selected_q[leg_side][3]=tq.w;
                asset->ik_candidate_trace_selected_branch[leg_side]=0;
                if (model_analytic_target(asset,&asset->iks[i],clip,time_ms,selected_target)) {
                    asset->ik_candidate_trace_selected_branch[leg_side]=
                        model_leg_branch(asset,leg_side,selected_target,0);
                    {
                        double dx=asset->bone_transforms[asset->iks[i].target].position[0]-selected_target[0];
                        double dy=asset->bone_transforms[asset->iks[i].target].position[1]-selected_target[1];
                        double dz=asset->bone_transforms[asset->iks[i].target].position[2]-selected_target[2];
                        selected_error=sqrt(dx*dx+dy*dy+dz*dz);
                    }
                }
                asset->ik_candidate_trace_selected_error[leg_side]=selected_error;
                asset->ik_candidate_trace_selection_reason[leg_side]=
                    asset->ik_analytic_last_takeover_reason[leg_side];
                if (asset->ik_analytic_trace_time_ms == time_ms &&
                    asset->ik_analytic_trace_side == leg_side) {
                    double *p=asset->ik_candidate_trace_previous_q[leg_side];
                    double *d=asset->ik_candidate_trace_dynamic_q[leg_side];
                    double *c=asset->ik_candidate_trace_compatible_q[leg_side];
                    double *s=asset->ik_candidate_trace_selected_q[leg_side];
                    double dp=fabs(p[0]*d[0]+p[1]*d[1]+p[2]*d[2]+p[3]*d[3]);
                    double dc=fabs(p[0]*c[0]+p[1]*c[1]+p[2]*c[2]+p[3]*c[3]);
                    double ds=fabs(p[0]*s[0]+p[1]*s[1]+p[2]*s[2]+p[3]*s[3]);
                    if (dp>1.0) dp=1.0;
                    if (dc>1.0) dc=1.0;
                    if (ds>1.0) ds=1.0;
                    __printf("analytic candidate provenance side=%s time=%d prev_solver=%d H=(%.3f,%.3f,%.3f) K0=(%.3f,%.3f,%.3f) T=(%.3f,%.3f,%.3f) pole=(%.6f,%.6f,%.6f)\n",
                             leg_side?"right":"left",time_ms,
                             asset->ik_candidate_trace_previous_solver[leg_side],
                             asset->ik_candidate_trace_h[leg_side][0],asset->ik_candidate_trace_h[leg_side][1],asset->ik_candidate_trace_h[leg_side][2],
                             asset->ik_candidate_trace_k0[leg_side][0],asset->ik_candidate_trace_k0[leg_side][1],asset->ik_candidate_trace_k0[leg_side][2],
                             asset->ik_candidate_trace_target[leg_side][0],asset->ik_candidate_trace_target[leg_side][1],asset->ik_candidate_trace_target[leg_side][2],
                             asset->ik_candidate_trace_pole[leg_side][0],asset->ik_candidate_trace_pole[leg_side][1],asset->ik_candidate_trace_pole[leg_side][2]);
                    __printf("  previous_q=(%.6f %.6f %.6f %.6f) dynamic_q=(%.6f %.6f %.6f %.6f) branch=%d error=%.3f angle_prev=%.3f\n",
                             p[0],p[1],p[2],p[3],d[0],d[1],d[2],d[3],
                             asset->ik_candidate_trace_dynamic_branch[leg_side],asset->ik_candidate_trace_dynamic_error[leg_side],
                             2.0*atan2(sqrt(1.0-dp*dp),dp)*180.0/M_PI);
                    __printf("  compatible_q=(%.6f %.6f %.6f %.6f) branch=%d valid=%d error=%.3f angle_prev=%.3f selected_q=(%.6f %.6f %.6f %.6f) branch=%d reason=%d error=%.3f angle_prev=%.3f\n",
                             c[0],c[1],c[2],c[3],asset->ik_candidate_trace_compatible_branch[leg_side],
                             asset->ik_candidate_trace_compatible_valid[leg_side],asset->ik_candidate_trace_compatible_error[leg_side],
                             2.0*atan2(sqrt(1.0-dc*dc),dc)*180.0/M_PI,s[0],s[1],s[2],s[3],
                             asset->ik_candidate_trace_selected_branch[leg_side],
                             asset->ik_candidate_trace_selection_reason[leg_side],selected_error,
                             2.0*atan2(sqrt(1.0-ds*ds),ds)*180.0/M_PI);
                }
            }
            if (leg_side >= 0 && asset->ik_analytic_last_takeover_reason[leg_side] != 0 && asset->ik_analytic_normal_dynamic[leg_side] > 0)
                asset->ik_analytic_normal_dynamic[leg_side]--;
            if (leg_side >= 0) {
                if (asset->ik_previous_final_enabled) {
                    int final_thigh = asset->iks[i].links[asset->iks[i].link_count-1].bone;
                    int final_knee = asset->iks[i].links[0].bone;
                    asset->ik_previous_final_thigh[leg_side][0] = asset->bones[final_thigh].rotate_x;
                    asset->ik_previous_final_thigh[leg_side][1] = asset->bones[final_thigh].rotate_y;
                    asset->ik_previous_final_thigh[leg_side][2] = asset->bones[final_thigh].rotate_z;
                    asset->ik_previous_final_knee[leg_side] = asset->bones[final_knee].rotate_x;
                    {
                        double final_target[3], final_ratio=0.0;
                        asset->ik_previous_final_branch[leg_side]=0;
                        if (model_analytic_target(asset,&asset->iks[i],clip,time_ms,final_target)) {
                asset->ik_previous_final_branch[leg_side]=model_leg_branch(asset,leg_side,final_target,&final_ratio);
                if (asset->ik_previous_final_branch[leg_side] != 0) {
                    asset->ik_last_analytical_branch[leg_side]=asset->ik_previous_final_branch[leg_side];
                    asset->ik_last_analytical_branch_valid[leg_side]=1;
                }
                model_store_previous_final_bend(asset,leg_side,final_target,1);
                        }
                    }
                    asset->ik_previous_final_valid[leg_side] = 1;
                }
                if (leg_side < 2 &&
                    asset->ik_analytical_inherit_diagnostic) {
                    int thigh = asset->iks[i].links[asset->iks[i].link_count-1].bone;
                    int knee = asset->iks[i].links[0].bone;
                    asset->ik_analytical_cache_thigh[leg_side][0] = asset->bones[thigh].rotate_x;
                    asset->ik_analytical_cache_thigh[leg_side][1] = asset->bones[thigh].rotate_y;
                    asset->ik_analytical_cache_thigh[leg_side][2] = asset->bones[thigh].rotate_z;
                    asset->ik_analytical_cache_knee[leg_side] = asset->bones[knee].rotate_x;
                    asset->ik_analytical_cache_valid[leg_side] = 1;
                    asset->ik_analytical_cache_write[leg_side] = 1;
                    asset->ik_analytical_cache_write_time[leg_side] = time_ms;
                }
                asset->ik_last_leg_solver[leg_side]=1;
                asset->ik_analytic_accept_count[leg_side]++;
            }
            solved++;
            before_total += before; after_total += after;
            if (before > before_max) before_max = before;
            if (after > after_max) after_max = after;
            continue;
            }
        }
        if (leg_side >= 0 && leg_side < 2 && asset->ik_previous_final_enabled &&
            asset->ik_previous_final_valid[leg_side]) {
            if (asset->ik_leg_transition_active[leg_side]) {
                asset->ik_leg_transition_active[leg_side]=0;
                asset->ik_leg_transition_interrupted_count[leg_side]++;
            }
            int restore_thigh = asset->iks[i].links[asset->iks[i].link_count-1].bone;
            int restore_knee = asset->iks[i].links[0].bone;
            asset->bones[restore_thigh].rotate_x = asset->ik_previous_final_thigh[leg_side][0];
            asset->bones[restore_thigh].rotate_y = asset->ik_previous_final_thigh[leg_side][1];
            asset->bones[restore_thigh].rotate_z = asset->ik_previous_final_thigh[leg_side][2];
            asset->bones[restore_knee].rotate_x = asset->ik_previous_final_knee[leg_side];
            asset->bones[restore_knee].rotate_y = 0;
            asset->bones[restore_knee].rotate_z = 0;
            rasterfall_model_update_bones(asset);
        }
        if (leg_side >= 0 && leg_side < 2 &&
            asset->ik_analytical_inherit_diagnostic &&
            asset->ik_last_leg_solver[leg_side] == 1 &&
            asset->ik_analytical_cache_valid[leg_side]) {
            int thigh = asset->iks[i].links[asset->iks[i].link_count-1].bone;
            int knee = asset->iks[i].links[0].bone;
            asset->bones[thigh].rotate_x = asset->ik_analytical_cache_thigh[leg_side][0];
            asset->bones[thigh].rotate_y = asset->ik_analytical_cache_thigh[leg_side][1];
            asset->bones[thigh].rotate_z = asset->ik_analytical_cache_thigh[leg_side][2];
            asset->bones[knee].rotate_x = asset->ik_analytical_cache_knee[leg_side];
            asset->bones[knee].rotate_y = 0;
            asset->bones[knee].rotate_z = 0;
            rasterfall_model_update_bones(asset);
            asset->ik_analytical_cache_read[leg_side] = 1;
            asset->ik_analytical_cache_read_time[leg_side] = time_ms;
        }
        if (asset->ik_legacy_knee_ccd && leg_side >= 0)
            asset->ik_last_leg_solver[leg_side]=2;
        if (!asset->ik_legacy_knee_ccd && leg_side >= 0) {
            int reason=asset->ik_analytic_last_reason;
            asset->ik_last_leg_solver[leg_side]=2;
            asset->ik_analytic_reject_count[leg_side]++;
            if (reason < 1 || reason > 4) reason=3;
            asset->ik_analytic_reject_reason[leg_side][reason-1]++;
        }
        if (!model_solve_leg_ccd(asset, &asset->iks[i], clip, time_ms,
                                 &one_attempts, &before, &after)) continue;
        solved++;
        attempts += one_attempts;
        if (one_attempts > max_attempts) max_attempts = one_attempts;
        before_total += before; after_total += after;
        if (before > before_max) before_max = before;
        if (after > after_max) after_max = after;
    }
    if (!solved) return;
    asset->solver_metrics.ik_sample_count++;
    asset->solver_metrics.ik_controller_sample_count += (unsigned long)solved;
    asset->solver_metrics.ik_iteration_total += attempts;
    if (max_attempts > asset->solver_metrics.ik_iteration_max) asset->solver_metrics.ik_iteration_max = max_attempts;
    asset->solver_metrics.ik_error_before_total += before_total;
    asset->solver_metrics.ik_error_after_total += after_total;
    if (before_max > asset->solver_metrics.ik_error_before_max) asset->solver_metrics.ik_error_before_max = before_max;
    if (after_max > asset->solver_metrics.ik_error_after_max) asset->solver_metrics.ik_error_after_max = after_max;
}

void rasterfall_model_set_ik_enabled(struct rasterfall_model_asset *asset,
                                     int enabled)
{
    if (asset) asset->ik_enabled = enabled ? 1 : 0;
}

void rasterfall_model_set_grant_enabled(struct rasterfall_model_asset *asset,
                                        int enabled)
{
    if (asset) {
        asset->grant_enabled = enabled ? 1 : 0;
        asset->grant_pose_applied = 0;
    }
}

void rasterfall_model_set_legacy_knee_ccd(struct rasterfall_model_asset *asset,
                                           int enabled)
{
    if (asset) asset->ik_legacy_knee_ccd = enabled ? 1 : 0;
}

static struct rasterfall_animation_quaternion model_matrix_to_quaternion(
    const double *m)
{
    struct rasterfall_animation_quaternion q;
    double trace = m[0] + m[4] + m[8], scale;
    if (trace > 0.0) {
        scale = sqrt(trace + 1.0) * 2.0;
        q.w = scale / 4.0;
        q.x = (m[7] - m[5]) / scale;
        q.y = (m[2] - m[6]) / scale;
        q.z = (m[3] - m[1]) / scale;
    } else if (m[0] > m[4] && m[0] > m[8]) {
        scale = sqrt(1.0 + m[0] - m[4] - m[8]) * 2.0;
        q.w = (m[7] - m[5]) / scale;
        q.x = scale / 4.0;
        q.y = (m[1] + m[3]) / scale;
        q.z = (m[2] + m[6]) / scale;
    } else if (m[4] > m[8]) {
        scale = sqrt(1.0 + m[4] - m[0] - m[8]) * 2.0;
        q.w = (m[2] - m[6]) / scale;
        q.x = (m[1] + m[3]) / scale;
        q.y = scale / 4.0;
        q.z = (m[5] + m[7]) / scale;
    } else {
        scale = sqrt(1.0 + m[8] - m[0] - m[4]) * 2.0;
        q.w = (m[3] - m[1]) / scale;
        q.x = (m[2] + m[6]) / scale;
        q.y = (m[5] + m[7]) / scale;
        q.z = scale / 4.0;
    }
    return rasterfall_animation_quat_normalize(q);
}

static struct rasterfall_animation_quaternion model_quaternion_multiply(
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

int rasterfall_model_apply_rotation_grants(struct rasterfall_model_asset *asset)
{
    unsigned int i;
    static const struct rasterfall_animation_quaternion identity = {0,0,0,1};
    if (!asset || !asset->bone_count || !asset->grant_enabled ||
        asset->grant_pose_applied) return 0;
    for (i = 0; i < asset->bone_count; i++) {
        struct rasterfall_model_bone *bone = &asset->bones[i];
        struct rasterfall_animation_quaternion base, source, inherited, result;
        struct rasterfall_animation_rotation euler;
        double base_matrix[9], source_matrix[9];
        int ratio_milli;
        if (!bone->grant_rotation_enabled || bone->grant_parent < 0) continue;
        if (bone->grant_parent >= (int)asset->bone_count ||
            bone->grant_parent == (int)i) return -1;
        if (bone->grant_ratio <= 0.0f) continue;
        if (bone->grant_ratio > 1.0f) {
            __printf("rasterfall: grant ratio clamped bone[%u]=%.6f\n",
                     i, bone->grant_ratio);
            ratio_milli = 1000;
        } else ratio_milli = (int)(bone->grant_ratio * 1000.0f + 0.5f);
        matrix_rotate_xyz(bone->rotate_x, bone->rotate_y, bone->rotate_z,
                          base_matrix);
        matrix_rotate_xyz(asset->bones[bone->grant_parent].rotate_x,
                          asset->bones[bone->grant_parent].rotate_y,
                          asset->bones[bone->grant_parent].rotate_z,
                          source_matrix);
        base = model_matrix_to_quaternion(base_matrix);
        source = model_matrix_to_quaternion(source_matrix);
        inherited = rasterfall_animation_quat_nlerp(identity, source,
                                                    ratio_milli);
        /* PMX append rotation is composed in the bone's local pose.  The
         * D-bone keeps its own parent/rest hierarchy; only its local
         * rotation receives the grant parent's rotation. */
        result = model_quaternion_multiply(base, inherited);
        rasterfall_animation_quat_to_euler(result, &euler);
        bone->rotate_x = euler.x;
        bone->rotate_y = euler.y;
        bone->rotate_z = euler.z;
    }
    asset->grant_pose_applied = 1;
    return rasterfall_model_update_bones(asset);
}

void rasterfall_model_bind_root_motion(struct rasterfall_model_asset *asset,
                                       int primary_bone,
                                       int secondary_bone)
{
    if (!asset) return;
    asset->root_motion.primary_bone = primary_bone;
    asset->root_motion.secondary_bone = secondary_bone;
}

void rasterfall_model_set_root_motion(
    struct rasterfall_model_asset *asset, const int primary[3],
    const int secondary[3], int enabled)
{
    if (!asset) return;
    asset->root_motion.enabled = enabled ? 1 : 0;
    asset->root_motion.primary[0] = primary ? primary[0] : 0;
    asset->root_motion.primary[1] = primary ? primary[1] : 0;
    asset->root_motion.primary[2] = primary ? primary[2] : 0;
    asset->root_motion.secondary[0] = secondary ? secondary[0] : 0;
    asset->root_motion.secondary[1] = secondary ? secondary[1] : 0;
    asset->root_motion.secondary[2] = secondary ? secondary[2] : 0;
}

static void model_capture_render_trace(
    struct rasterfall_model_asset *asset, int stage)
{
    static const char *names[4] = {"左足", "左ひざ", "右足", "右ひざ"};
    int i, bone;
    if (!asset) return;
    for (i = 0; i < 4; i++) {
        bone = rasterfall_model_find_bone(asset, names[i]);
        if (bone < 0 || bone >= (int)asset->bone_count) continue;
        if (stage == 0) {
            asset->render_trace_sampled_local[i][0] = asset->bones[bone].rotate_x;
            asset->render_trace_sampled_local[i][1] = asset->bones[bone].rotate_y;
            asset->render_trace_sampled_local[i][2] = asset->bones[bone].rotate_z;
            asset->render_trace_pre_ik_local[i][0] = asset->bones[bone].rotate_x;
            asset->render_trace_pre_ik_local[i][1] = asset->bones[bone].rotate_y;
            asset->render_trace_pre_ik_local[i][2] = asset->bones[bone].rotate_z;
        } else if (stage == 1) {
            asset->render_trace_solver_local[i][0] = asset->bones[bone].rotate_x;
            asset->render_trace_solver_local[i][1] = asset->bones[bone].rotate_y;
            asset->render_trace_solver_local[i][2] = asset->bones[bone].rotate_z;
            memcpy(asset->render_trace_solver_global[i],
                   asset->bone_transforms[bone].rotation, 9 * sizeof(double));
        } else if (stage == 2) {
            asset->render_trace_grant_local[i][0] = asset->bones[bone].rotate_x;
            asset->render_trace_grant_local[i][1] = asset->bones[bone].rotate_y;
            asset->render_trace_grant_local[i][2] = asset->bones[bone].rotate_z;
            memcpy(asset->render_trace_grant_global[i],
                   asset->bone_transforms[bone].rotation, 9 * sizeof(double));
        } else {
            asset->render_trace_final_local[i][0] = asset->bones[bone].rotate_x;
            asset->render_trace_final_local[i][1] = asset->bones[bone].rotate_y;
            asset->render_trace_final_local[i][2] = asset->bones[bone].rotate_z;
            memcpy(asset->render_trace_final_global[i],
                   asset->bone_transforms[bone].rotation, 9 * sizeof(double));
        }
    }
    asset->render_trace_valid = 1;
}

static int model_render_trace_leg_bone(int bone)
{
    return bone == 99 || bone == 100 || bone == 103 || bone == 104;
}

static int model_render_trace_leg_slot(int bone)
{
    return bone == 99 ? 0 : bone == 100 ? 1 :
           bone == 103 ? 2 : bone == 104 ? 3 : -1;
}

static void model_trace_quaternion_matrix(
    struct rasterfall_animation_quaternion q, double m[9])
{
    double xx=q.x*q.x, yy=q.y*q.y, zz=q.z*q.z;
    double xy=q.x*q.y, xz=q.x*q.z, yz=q.y*q.z;
    double wx=q.w*q.x, wy=q.w*q.y, wz=q.w*q.z;
    m[0]=1.0-2.0*(yy+zz); m[1]=2.0*(xy-wz); m[2]=2.0*(xz+wy);
    m[3]=2.0*(xy+wz); m[4]=1.0-2.0*(xx+zz); m[5]=2.0*(yz-wx);
    m[6]=2.0*(xz-wy); m[7]=2.0*(yz+wx); m[8]=1.0-2.0*(xx+yy);
}

static void model_capture_rotation_write_trace(
    struct rasterfall_model_asset *asset, int bone,
    const double *result_global, const double *local_before,
    int x, int y, int z)
{
    struct rasterfall_animation_quaternion q;
    int slot;
    if (!asset || !model_render_trace_leg_bone(bone)) return;
    slot = model_render_trace_leg_slot(bone);
    asset->render_trace_write_bone = bone;
    asset->render_trace_write_valid = 1;
    asset->render_trace_write_path = 1;
    asset->render_trace_write_valid_by_bone[slot] = 1;
    asset->render_trace_write_path_by_bone[slot] = 1;
    q = model_matrix_to_quaternion(result_global);
    asset->render_trace_ik_result_global_q[0] = q.x;
    asset->render_trace_ik_result_global_q[1] = q.y;
    asset->render_trace_ik_result_global_q[2] = q.z;
    asset->render_trace_ik_result_global_q[3] = q.w;
    memcpy(asset->render_trace_ik_result_global_q_by_bone[slot],
           asset->render_trace_ik_result_global_q, 4 * sizeof(double));
    q = model_matrix_to_quaternion(local_before);
    asset->render_trace_local_before_write_q[0] = q.x;
    asset->render_trace_local_before_write_q[1] = q.y;
    asset->render_trace_local_before_write_q[2] = q.z;
    asset->render_trace_local_before_write_q[3] = q.w;
    memcpy(asset->render_trace_local_before_write_q_by_bone[slot],
           asset->render_trace_local_before_write_q, 4 * sizeof(double));
    memcpy(asset->render_trace_local_before_write_matrix, local_before,
           9 * sizeof(double));
    memcpy(asset->render_trace_local_before_write_matrix_by_bone[slot],
           local_before, 9 * sizeof(double));
    asset->render_trace_euler_convert[0] = x;
    asset->render_trace_euler_convert[1] = y;
    asset->render_trace_euler_convert[2] = z;
    memcpy(asset->render_trace_euler_convert_by_bone[slot],
           asset->render_trace_euler_convert, 3 * sizeof(int));
}

int rasterfall_model_sample_clip(struct rasterfall_model_asset *asset,
                                 const struct rasterfall_animation_clip *clip,
                                 int time_ms)
{
    unsigned int i;
    if (!asset || !asset->animation.rotations || !asset->bone_count) return -1;
    asset->render_trace_time_ms = time_ms;
    asset->render_trace_write_valid = 0;
    memset(asset->render_trace_write_valid_by_bone, 0,
           sizeof(asset->render_trace_write_valid_by_bone));
    memset(asset->ik_candidate_trace_valid, 0,
           sizeof(asset->ik_candidate_trace_valid));
    memset(asset->ik_candidate_trace_compatible_valid, 0,
           sizeof(asset->ik_candidate_trace_compatible_valid));
    for (i = 0; i < 2; i++) {
        struct rasterfall_animation_quaternion q =
            rasterfall_animation_quat_from_euler(
                asset->ik_previous_final_thigh[i][0],
                asset->ik_previous_final_thigh[i][1],
                asset->ik_previous_final_thigh[i][2]);
        asset->render_trace_previous_solver[i] = asset->ik_last_leg_solver[i];
        asset->render_trace_previous_final_thigh_q[i][0] = q.x;
        asset->render_trace_previous_final_thigh_q[i][1] = q.y;
        asset->render_trace_previous_final_thigh_q[i][2] = q.z;
        asset->render_trace_previous_final_thigh_q[i][3] = q.w;
    }
    asset->grant_pose_applied = 0;
    for (i = 0; i < 2; i++) {
        asset->ik_analytical_cache_valid_before[i] = asset->ik_analytical_cache_valid[i];
        asset->ik_analytical_cache_write[i] = 0;
        asset->ik_analytical_cache_read[i] = 0;
        asset->ik_analytical_cache_write_time[i] = -1;
        asset->ik_analytical_cache_read_time[i] = -1;
    }
    for (i = 0; i < asset->bone_count; i++) {
        asset->bones[i].animation_x = 0;
        asset->bones[i].animation_y = 0;
        asset->bones[i].animation_z = 0;
    }
    if (asset->root_motion.enabled) {
        int center = asset->root_motion.primary_bone;
        int groove = asset->root_motion.secondary_bone;
        if (center >= 0 && center < (int)asset->bone_count) {
            asset->bones[center].animation_x = asset->root_motion.primary[0];
            asset->bones[center].animation_y = asset->root_motion.primary[1];
            asset->bones[center].animation_z = asset->root_motion.primary[2];
        }
        if (groove >= 0 && groove < (int)asset->bone_count) {
            asset->bones[groove].animation_x = asset->root_motion.secondary[0];
            asset->bones[groove].animation_y = asset->root_motion.secondary[1];
            asset->bones[groove].animation_z = asset->root_motion.secondary[2];
        }
    }
    rasterfall_animation_sample(clip, time_ms, asset->animation.rotations,
                                asset->bone_count);
    for (i = 0; i < asset->bone_count; i++) {
        asset->bones[i].rotate_x = asset->animation.rotations[i].x;
        asset->bones[i].rotate_y = asset->animation.rotations[i].y;
        asset->bones[i].rotate_z = asset->animation.rotations[i].z;
    }
    model_capture_render_trace(asset, 0);
    asset->animation.pose = RASTERFALL_MODEL_POSE_BIND;
    if (asset->ik_enabled && clip && asset->ik_count)
        model_solve_leg_iks(asset, clip, time_ms);
    model_capture_render_trace(asset, 1);
    for (i = 0; i < asset->ik_count; i++) {
        int side = -1, thigh;
        struct rasterfall_animation_quaternion tq;
        if (asset->iks[i].controller < 0 ||
            asset->iks[i].controller >= (int)asset->bone_count ||
            asset->iks[i].link_count < 2)
            continue;
        side=model_leg_ik_side(asset,&asset->iks[i]);
        if (side < 0 || asset->ik_last_leg_solver[side] == 0) continue;
        thigh = asset->iks[i].links[asset->iks[i].link_count - 1].bone;
        tq = model_matrix_to_quaternion(asset->bone_transforms[thigh].rotation);
        asset->ik_pregrant_thigh_global_q[side][0] = tq.x;
        asset->ik_pregrant_thigh_global_q[side][1] = tq.y;
        asset->ik_pregrant_thigh_global_q[side][2] = tq.z;
        asset->ik_pregrant_thigh_global_q[side][3] = tq.w;
        asset->ik_pregrant_thigh_global_q_valid[side] = 1;
    }
    if (asset->grant_enabled)
        rasterfall_model_apply_rotation_grants(asset);
    model_capture_render_trace(asset, 2);
    for (i = 0; i < 2; i++) {
        asset->render_trace_transition_active[i] = asset->ik_leg_transition_active[i];
        asset->render_trace_transition_remaining[i] = asset->ik_leg_transition_remaining[i];
        asset->render_trace_transition_duration[i] = asset->ik_leg_transition_duration[i];
        asset->render_trace_transition_reason[i] = asset->ik_leg_transition_reason[i];
        memcpy(asset->render_trace_transition_source_q[i],
               asset->ik_leg_transition_old_thigh_q[i], 4 * sizeof(double));
        memcpy(asset->render_trace_transition_target_q[i],
               asset->ik_leg_transition_new_thigh_q[i], 4 * sizeof(double));
        asset->render_trace_reconciliation_active[i] = asset->ik_near_degenerate_ca_active[i];
        asset->render_trace_reconciliation_used[i] = asset->ik_near_degenerate_ca_reconciled[i];
        asset->render_trace_reconciliation_unavailable[i] = asset->ik_near_degenerate_ca_unavailable[i];
        memcpy(asset->render_trace_reconciliation_source_q[i],
               asset->ik_solver_return_thigh_global_q[i], 4 * sizeof(double));
        memcpy(asset->render_trace_reconciliation_target_q[i],
               asset->ik_final_thigh_global_q[i], 4 * sizeof(double));
    }
    for (i = 0; i < asset->ik_count; i++) {
        int side = -1, thigh;
        struct rasterfall_animation_quaternion tq;
        if (asset->iks[i].controller < 0 ||
            asset->iks[i].controller >= (int)asset->bone_count ||
            asset->iks[i].link_count < 2)
            continue;
        side=model_leg_ik_side(asset,&asset->iks[i]);
        if (side < 0 || asset->ik_last_leg_solver[side] == 0) continue;
        thigh = asset->iks[i].links[asset->iks[i].link_count - 1].bone;
        tq = model_matrix_to_quaternion(asset->bone_transforms[thigh].rotation);
        asset->ik_final_thigh_global_q[side][0] = tq.x;
        asset->ik_final_thigh_global_q[side][1] = tq.y;
        asset->ik_final_thigh_global_q[side][2] = tq.z;
        asset->ik_final_thigh_global_q[side][3] = tq.w;
        asset->ik_final_thigh_global_q_valid[side] = 1;
    }
    for (i = 0; i < 2; i++)
        memcpy(asset->render_trace_reconciliation_target_q[i],
               asset->ik_final_thigh_global_q[i], 4 * sizeof(double));
    /* Refresh the diagnostics snapshot after final policy state is populated. */
    model_capture_render_trace(asset, 2);
    for (i = 0; i < 2; i++) {
        int d_index = rasterfall_model_find_bone(asset, i ? "右足D" : "左足D");
        if (d_index >= 0) {
            struct rasterfall_animation_quaternion dq =
                model_matrix_to_quaternion(asset->bone_transforms[d_index].rotation);
            asset->ik_final_dthigh_global_q[i][0] = dq.x;
            asset->ik_final_dthigh_global_q[i][1] = dq.y;
            asset->ik_final_dthigh_global_q[i][2] = dq.z;
            asset->ik_final_dthigh_global_q[i][3] = dq.w;
            asset->ik_final_dthigh_global_q_valid[i] = 1;
        }
    }
    model_capture_render_trace(asset, 3);
    /* C1 is the complete post-sample pose, after grant propagation and the
     * final global rebuild.  Keep it separate from the solver-return state. */
    for (i = 0; i < asset->ik_count; i++) {
        int side = -1, knee, thigh, target;
        struct rasterfall_animation_quaternion tq, kq;
        double error_vec[3];
        if (asset->iks[i].controller < 0 ||
            asset->iks[i].controller >= (int)asset->bone_count ||
            asset->iks[i].link_count < 2)
            continue;
        side=model_leg_ik_side(asset,&asset->iks[i]);
        if (side < 0 || asset->ik_last_leg_solver[side] != 2) continue;
        knee = asset->iks[i].links[0].bone;
        thigh = asset->iks[i].links[asset->iks[i].link_count - 1].bone;
        target = asset->iks[i].target;
        asset->ik_handoff_c1_thigh[side][0] = asset->bones[thigh].rotate_x;
        asset->ik_handoff_c1_thigh[side][1] = asset->bones[thigh].rotate_y;
        asset->ik_handoff_c1_thigh[side][2] = asset->bones[thigh].rotate_z;
        asset->ik_handoff_c1_knee[side] = asset->bones[knee].rotate_x;
        tq = model_matrix_to_quaternion(asset->bone_transforms[thigh].rotation);
        kq = model_matrix_to_quaternion(asset->bone_transforms[knee].rotation);
        asset->ik_handoff_c1_thigh_global_q[side][0] = tq.x;
        asset->ik_handoff_c1_thigh_global_q[side][1] = tq.y;
        asset->ik_handoff_c1_thigh_global_q[side][2] = tq.z;
        asset->ik_handoff_c1_thigh_global_q[side][3] = tq.w;
        asset->ik_handoff_c1_knee_global_q[side][0] = kq.x;
        asset->ik_handoff_c1_knee_global_q[side][1] = kq.y;
        asset->ik_handoff_c1_knee_global_q[side][2] = kq.z;
        asset->ik_handoff_c1_knee_global_q[side][3] = kq.w;
        memcpy(asset->ik_handoff_c1_ankle[side], asset->bone_transforms[target].position,
               sizeof(asset->ik_handoff_c1_ankle[side]));
        memcpy(asset->ik_c1_target[side], asset->ik_ccd_diag_target[side],
               sizeof(asset->ik_c1_target[side]));
        error_vec[0] = asset->bone_transforms[target].position[0] - asset->ik_c1_target[side][0];
        error_vec[1] = asset->bone_transforms[target].position[1] - asset->ik_c1_target[side][1];
        error_vec[2] = asset->bone_transforms[target].position[2] - asset->ik_c1_target[side][2];
        asset->ik_c1_error[side] = model_vec_length(error_vec);
    }
    return 0;
}

int rasterfall_model_sample_glb_rotation_clip(
    struct rasterfall_model_asset *asset,
    const struct rasterfall_glb_rotation_clip *clip,
    const struct rasterfall_glb_rotation_reference *reference,
    int time_ms)
{
    struct rasterfall_humanoid_rest_basis source_basis[RASTERFALL_HUMANOID_BONE_COUNT];
    struct rasterfall_humanoid_rest_basis target_basis[RASTERFALL_HUMANOID_BONE_COUNT];
    struct rasterfall_humanoid_rotation_skeleton source, target;
    struct rasterfall_humanoid_rotation_pose pose, source_reference, target_reference;
    struct rasterfall_humanoid_mapping mapping;
    struct rasterfall_humanoid_retarget_result result;
    unsigned int reference_mask=((1u<<RASTERFALL_HUMANOID_BONE_COUNT)-1u)&~1u;
    int sampled, humanoid, bone;
    if (!asset || !clip || !reference ||
        rasterfall_model_build_humanoid_bases(asset, target_basis) < 0)
        return -1;
    rasterfall_humanoid_rotation_skeleton_identity(&target);
    rasterfall_humanoid_rotation_pose_bind(&target,&target_reference);
    rasterfall_model_map_humanoid(asset, &mapping);
    memcpy(&source,&reference->skeleton,sizeof(source));
    memcpy(&source_reference,&reference->pose,sizeof(source_reference));
    memcpy(source_basis,reference->basis,sizeof(source_basis));
    if (rasterfall_glb_rotation_clip_source(clip,time_ms,&source,&pose,
                                            source_basis,&sampled)<0)
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
    asset->animation.pose = RASTERFALL_MODEL_POSE_BIND;
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
            transform->position[0] += bone->animation_x;
            transform->position[1] += bone->animation_y;
            transform->position[2] += bone->animation_z;
            transform->position[0] += asset->animation_offset[0];
            transform->position[1] += asset->animation_offset[1];
            transform->position[2] += asset->animation_offset[2];
        } else {
            struct rasterfall_model_bone *parent_bone = &asset->bones[parent];
            struct rasterfall_model_bone_transform *parent_transform =
                &asset->bone_transforms[parent];
            double x, y, z;
            matrix_multiply(parent_transform->rotation, local,
                            transform->rotation);
            matrix_vector(parent_transform->rotation,
                bone->rest_x - parent_bone->rest_x + bone->animation_x,
                bone->rest_y - parent_bone->rest_y + bone->animation_y,
                bone->rest_z - parent_bone->rest_z + bone->animation_z,
                &x, &y, &z);
            transform->position[0] = parent_transform->position[0] + x;
            transform->position[1] = parent_transform->position[1] + y;
            transform->position[2] = parent_transform->position[2] + z;
        }
    }
    return 0;
}

int rasterfall_model_attachment_transform(
    const struct rasterfall_model_asset *asset, const char *bone_name,
    struct rasterfall_model_attachment_transform *out)
{
    int bone;
    if (!asset || !bone_name || !out || !asset->bone_transforms) return -1;
    bone = rasterfall_model_find_bone(asset, bone_name);
    if (bone < 0 || bone >= (int)asset->bone_count) return -1;
    memcpy(out->position, asset->bone_transforms[bone].position,
           sizeof(out->position));
    memcpy(out->rotation, asset->bone_transforms[bone].rotation,
           sizeof(out->rotation));
    return 0;
}

int rasterfall_model_solve_two_bone_attachment(
    struct rasterfall_model_asset *asset, const char *upper_bone,
    const char *forearm_bone, const char *hand_bone,
    const double requested[3], const double pole_hint[3])
{
    struct rasterfall_model_two_bone_diagnostics *diag;
    int upper,forearm,hand,parent,i,reference[3],x,y,z,used_previous=0;
    double h[3],k[3],a[3],axis[3],pole[3],current[3],desired[3];
    double l1,l2,d,min_reach,max_reach,along,height,dot,len;
    double elbow[3],delta[9],desired_global[9],parent_inverse[9],local[9];
    if(!asset||!requested||!pole_hint)return -1;
    upper=rasterfall_model_find_bone(asset,upper_bone);
    forearm=rasterfall_model_find_bone(asset,forearm_bone);
    hand=rasterfall_model_find_bone(asset,hand_bone);
    if(upper<0||forearm<0||hand<0||asset->bones[forearm].parent!=upper||
       asset->bones[hand].parent!=forearm)return -1;
    if(rasterfall_model_update_bones(asset)<0)return -1;
    for(i=0;i<3;i++){
        h[i]=asset->bone_transforms[upper].position[i];
        k[i]=asset->bone_transforms[forearm].position[i];
        a[i]=asset->bone_transforms[hand].position[i];
        axis[i]=requested[i]-h[i];
    }
    l1=model_vec_length((double[3]){k[0]-h[0],k[1]-h[1],k[2]-h[2]});
    l2=model_vec_length((double[3]){a[0]-k[0],a[1]-k[1],a[2]-k[2]});
    d=model_vec_length(axis);if(l1<0.000001||l2<0.000001)return -1;
    if(d<0.000001){for(i=0;i<3;i++)axis[i]=a[i]-h[i];d=model_vec_length(axis);}
    if(d<0.000001)return -1;
    for(i=0;i<3;i++)axis[i]/=d;
    min_reach=fabs(l1-l2)+0.001;max_reach=l1+l2-0.001;
    diag=&asset->attachment_ik_diagnostics;memset(diag,0,sizeof(*diag));
    memcpy(diag->requested_target,requested,sizeof(diag->requested_target));
    diag->reach=d;diag->max_reach=max_reach;
    if(d>max_reach){d=max_reach;diag->reach_clamped=1;}
    if(d<min_reach){d=min_reach;diag->reach_clamped=1;}
    for(i=0;i<3;i++)diag->clamped_target[i]=h[i]+axis[i]*d;
    for(i=0;i<3;i++)pole[i]=pole_hint[i];
    dot=pole[0]*axis[0]+pole[1]*axis[1]+pole[2]*axis[2];
    for(i=0;i<3;i++)pole[i]-=axis[i]*dot;
    len=model_vec_length(pole);
    if(len<0.000001){
        for(i=0;i<3;i++)pole[i]=k[i]-h[i];
        dot=pole[0]*axis[0]+pole[1]*axis[1]+pole[2]*axis[2];
        for(i=0;i<3;i++)pole[i]-=axis[i]*dot;
        len=model_vec_length(pole);
    }
    if(len<0.000001){pole[0]=0;pole[1]=0;pole[2]=1;len=1;}
    for(i=0;i<3;i++)pole[i]/=len;
    if(asset->attachment_ik_previous_pole_valid){
        double previous[3],previous_dot;
        for(i=0;i<3;i++)previous[i]=asset->attachment_ik_previous_pole[i];
        dot=previous[0]*axis[0]+previous[1]*axis[1]+previous[2]*axis[2];
        for(i=0;i<3;i++)previous[i]-=axis[i]*dot;
        len=model_vec_length(previous);
        if(len>0.000001){
            for(i=0;i<3;i++)previous[i]/=len;
            previous_dot=previous[0]*pole[0]+previous[1]*pole[1]+previous[2]*pole[2];
            if(previous_dot<0.0)for(i=0;i<3;i++)pole[i]=-pole[i];
            for(i=0;i<3;i++)pole[i]=previous[i]*0.75+pole[i]*0.25;
            len=model_vec_length(pole);if(len>0.000001)
                for(i=0;i<3;i++)pole[i]/=len;
            used_previous=1;
        }
    }
    memcpy(asset->attachment_ik_previous_pole,pole,sizeof(pole));
    asset->attachment_ik_previous_pole_valid=1;
    memcpy(diag->elbow_pole,pole,sizeof(diag->elbow_pole));
    diag->used_previous_pole=used_previous;
    along=(l1*l1-l2*l2+d*d)/(2.0*d);
    height=l1*l1-along*along;height=height>0.0?sqrt(height):0.0;
    for(i=0;i<3;i++)elbow[i]=h[i]+axis[i]*along+pole[i]*height;

    for(i=0;i<3;i++){current[i]=k[i]-h[i];desired[i]=elbow[i]-h[i];}
    if(model_rotation_between(current,desired,pole,delta)<0)return -1;
    matrix_multiply(delta,asset->bone_transforms[upper].rotation,desired_global);
    parent=asset->bones[upper].parent;
    if(parent>=0){model_matrix_transpose(asset->bone_transforms[parent].rotation,
        parent_inverse);matrix_multiply(parent_inverse,desired_global,local);}
    else memcpy(local,desired_global,sizeof(local));
    reference[0]=asset->bones[upper].rotate_x;reference[1]=asset->bones[upper].rotate_y;
    reference[2]=asset->bones[upper].rotate_z;
    model_matrix_to_euler_near(local,reference,&x,&y,&z);
    asset->bones[upper].rotate_x=x;asset->bones[upper].rotate_y=y;asset->bones[upper].rotate_z=z;
    rasterfall_model_update_bones(asset);

    for(i=0;i<3;i++){
        current[i]=asset->bone_transforms[hand].position[i]-
                   asset->bone_transforms[forearm].position[i];
        desired[i]=diag->clamped_target[i]-
                   asset->bone_transforms[forearm].position[i];
    }
    if(model_rotation_between(current,desired,pole,delta)<0)return -1;
    matrix_multiply(delta,asset->bone_transforms[forearm].rotation,desired_global);
    parent=asset->bones[forearm].parent;
    model_matrix_transpose(asset->bone_transforms[parent].rotation,parent_inverse);
    matrix_multiply(parent_inverse,desired_global,local);
    reference[0]=asset->bones[forearm].rotate_x;reference[1]=asset->bones[forearm].rotate_y;
    reference[2]=asset->bones[forearm].rotate_z;
    model_matrix_to_euler_near(local,reference,&x,&y,&z);
    asset->bones[forearm].rotate_x=x;asset->bones[forearm].rotate_y=y;
    asset->bones[forearm].rotate_z=z;
    rasterfall_model_update_bones(asset);
    diag->hand_error=model_vec_length((double[3]){
        asset->bone_transforms[hand].position[0]-diag->clamped_target[0],
        asset->bone_transforms[hand].position[1]-diag->clamped_target[1],
        asset->bone_transforms[hand].position[2]-diag->clamped_target[2]});
    return 0;
}

void rasterfall_model_print_two_bone_diagnostics(
    const struct rasterfall_model_asset *asset,const char *label)
{
    const struct rasterfall_model_two_bone_diagnostics *d;
    if(!asset)return;
    d=&asset->attachment_ik_diagnostics;
    __printf("two_bone_ik label=%s requested=(%.3f,%.3f,%.3f) clamped=(%.3f,%.3f,%.3f) reach=%.3f max=%.3f reach_clamped=%d pole=(%.6f,%.6f,%.6f) previous_pole=%d hand_error=%.6f\n",
        label?label:"?",d->requested_target[0],d->requested_target[1],
        d->requested_target[2],d->clamped_target[0],d->clamped_target[1],
        d->clamped_target[2],d->reach,d->max_reach,d->reach_clamped,
        d->elbow_pole[0],d->elbow_pole[1],d->elbow_pole[2],
        d->used_previous_pole,d->hand_error);
}

static const struct rasterfall_vmd_bone_track *model_vmd_track(
    const struct rasterfall_vmd_clip *vmd, const char *name)
{
    int i;
    if (!vmd || !name) return 0;
    for (i = 0; i < vmd->track_count; i++)
        if (!strcmp(vmd->tracks[i].name, name)) return &vmd->tracks[i];
    return 0;
}

static void model_vmd_translation(const struct rasterfall_vmd_clip *vmd,
                                  const char *name, int time_ms, double out[3])
{
    const struct rasterfall_vmd_bone_track *track = model_vmd_track(vmd, name);
    const struct rasterfall_vmd_keyframe *a, *b;
    int i, at, next, factor = 0;
    if (!out) return;
    out[0] = out[1] = out[2] = 0.0;
    if (!track || !track->key_count) return;
    at = vmd->duration_ms > 0 ? time_ms % vmd->duration_ms : time_ms;
    a = b = &track->keys[0];
    for (i = 1; i < track->key_count; i++) {
        if (at < track->keys[i].frame * 1000 / 30) {
            b = &track->keys[i];
            break;
        }
        a = &track->keys[i];
    }
    if (a != b && b->frame > a->frame) {
        next = b->frame * 1000 / 30;
        factor = (at - a->frame * 1000 / 30) * 1000 /
                 (next - a->frame * 1000 / 30);
    }
    if (factor < 0) factor = 0;
    if (factor > 1000) factor = 1000;
    out[0] = (a->tx + (b->tx - a->tx) * factor / 1000.0) *
             RASTERFALL_VMD_TRANSLATION_SCALE;
    out[1] = (a->ty + (b->ty - a->ty) * factor / 1000.0) *
             RASTERFALL_VMD_TRANSLATION_SCALE;
    out[2] = (a->tz + (b->tz - a->tz) * factor / 1000.0) *
             RASTERFALL_VMD_TRANSLATION_SCALE;
}

static void model_update_center_diagnostic(
    const struct rasterfall_model_asset *asset,
    const double center_offset[3], const double groove_offset[3],
    struct rasterfall_model_bone_transform *transforms)
{
    unsigned int order;
    int center = rasterfall_model_find_bone(asset, "センター");
    int groove = rasterfall_model_find_bone(asset, "グルーブ");
    for (order = 0; order < asset->bone_count; order++) {
        unsigned int i = asset->bone_order[order];
        const struct rasterfall_model_bone *bone = &asset->bones[i];
        double local[9], x, y, z;
        int parent = bone->parent;
        matrix_rotate_xyz(bone->rotate_x, bone->rotate_y, bone->rotate_z,
                          local);
        if (parent < 0) {
            memcpy(transforms[i].rotation, local, sizeof(local));
            transforms[i].position[0] = bone->rest_x;
            transforms[i].position[1] = bone->rest_y;
            transforms[i].position[2] = bone->rest_z;
        } else {
            double local_x = bone->rest_x - asset->bones[parent].rest_x;
            double local_y = bone->rest_y - asset->bones[parent].rest_y;
            double local_z = bone->rest_z - asset->bones[parent].rest_z;
            if ((int)i == center) {
                local_x += center_offset[0]; local_y += center_offset[1];
                local_z += center_offset[2];
            }
            if ((int)i == groove) {
                local_x += groove_offset[0]; local_y += groove_offset[1];
                local_z += groove_offset[2];
            }
            matrix_multiply(transforms[parent].rotation, local, transforms[i].rotation);
            matrix_vector(transforms[parent].rotation, local_x, local_y, local_z,
                          &x, &y, &z);
            transforms[i].position[0] = transforms[parent].position[0] + x;
            transforms[i].position[1] = transforms[parent].position[1] + y;
            transforms[i].position[2] = transforms[parent].position[2] + z;
        }
    }
}

static double model_distance3(const double a[3], const double b[3])
{
    double x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrt(x * x + y * y + z * z);
}

void rasterfall_model_dump_ik_hierarchy(const struct rasterfall_model_asset *asset)
{
    static const char *names[] = {"全ての親", "センター", "センター2", "グルーブ", "腰", "下半身",
                                  "左足", "右足", "左足IK親", "右足IK親",
                                  "左足ＩＫ", "右足ＩＫ"};
    int i;
    if (!asset) return;
    __printf("MMD IK hierarchy (child -> parent):\n");
    for (i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++) {
        int bone = rasterfall_model_find_bone(asset, names[i]);
        int parent;
        __printf("  %s: ", names[i]);
        if (bone < 0) { __printf("missing\n"); continue; }
        parent = asset->bones[bone].parent;
        __printf("index=%d parent=%s(%d) rest=(%d,%d,%d)\n", bone,
                 parent >= 0 ? asset->bones[parent].name : "<root>", parent,
                 asset->bones[bone].rest_x, asset->bones[bone].rest_y,
                 asset->bones[bone].rest_z);
    }
}

void rasterfall_model_reset_center_ab_diagnostic(struct rasterfall_model_asset *asset)
{
    if (!asset) return;
    asset->solver_metrics.center_ab_samples = asset->solver_metrics.center_ab_a_unreachable =
        asset->solver_metrics.center_ab_b_unreachable = 0;
    asset->solver_metrics.center_ab_a_ratio_total = asset->solver_metrics.center_ab_b_ratio_total = 0.0;
    asset->solver_metrics.center_ab_a_ratio_max = asset->solver_metrics.center_ab_b_ratio_max = 0.0;
    asset->solver_metrics.center_ab_a_excess_total = asset->solver_metrics.center_ab_b_excess_total = 0.0;
    asset->solver_metrics.center_ab_a_excess_max = asset->solver_metrics.center_ab_b_excess_max = 0.0;
}

void rasterfall_model_center_ab_diagnostic(struct rasterfall_model_asset *asset,
                                           const struct rasterfall_vmd_clip *vmd,
                                           int time_ms, int print_sample)
{
    struct rasterfall_model_bone_transform *b;
    double center[3], groove[3];
    unsigned int i;
    if (!asset || !vmd || !asset->bone_count) return;
    b = tlibc_malloc(asset->bone_count * sizeof(*b));
    if (!b) return;
    model_vmd_translation(vmd, "センター", time_ms, center);
    model_vmd_translation(vmd, "グルーブ", time_ms, groove);
    memcpy(b, asset->bone_transforms, asset->bone_count * sizeof(*b));
    model_update_center_diagnostic(asset, center, groove, b);
    if (print_sample)
        __printf("center AB phase_ms=%d center_local=(%.3f,%.3f,%.3f) groove_local=(%.3f,%.3f,%.3f)\n",
                 time_ms, center[0], center[1], center[2], groove[0], groove[1], groove[2]);
    for (i = 0; i < asset->ik_count; i++) {
        const struct rasterfall_model_ik *ik = &asset->iks[i];
        int thigh, knee, parent;
        double desired_a[3], desired_b[3], upper[3], lower[3];
        double max_reach, distance_a, distance_b, ratio_a, ratio_b;
        if (strcmp(asset->bones[ik->controller].name, "左足ＩＫ") &&
            strcmp(asset->bones[ik->controller].name, "右足ＩＫ")) continue;
        if (!model_vmd_track(vmd, asset->bones[ik->controller].name)) continue;
        thigh = ik->links[ik->link_count - 1].bone;
        knee = ik->links[0].bone;
        parent = asset->bones[ik->controller].parent;
        model_vmd_translation(vmd, asset->bones[ik->controller].name,
                              time_ms, desired_a);
        desired_a[0] += asset->bone_transforms[ik->controller].position[0];
        desired_a[1] += asset->bone_transforms[ik->controller].position[1];
        desired_a[2] += asset->bone_transforms[ik->controller].position[2];
        desired_b[0] = b[ik->controller].position[0] +
                       (desired_a[0] - asset->bone_transforms[ik->controller].position[0]);
        desired_b[1] = b[ik->controller].position[1] +
                       (desired_a[1] - asset->bone_transforms[ik->controller].position[1]);
        desired_b[2] = b[ik->controller].position[2] +
                       (desired_a[2] - asset->bone_transforms[ik->controller].position[2]);
        upper[0] = asset->bone_transforms[thigh].position[0] - asset->bone_transforms[knee].position[0];
        upper[1] = asset->bone_transforms[thigh].position[1] - asset->bone_transforms[knee].position[1];
        upper[2] = asset->bone_transforms[thigh].position[2] - asset->bone_transforms[knee].position[2];
        lower[0] = asset->bone_transforms[knee].position[0] - asset->bone_transforms[ik->target].position[0];
        lower[1] = asset->bone_transforms[knee].position[1] - asset->bone_transforms[ik->target].position[1];
        lower[2] = asset->bone_transforms[knee].position[2] - asset->bone_transforms[ik->target].position[2];
        max_reach = model_vec_length(upper) + model_vec_length(lower);
        distance_a = model_distance3(asset->bone_transforms[thigh].position, desired_a);
        distance_b = model_distance3(b[thigh].position, desired_b);
        ratio_a = max_reach > 0.0 ? distance_a / max_reach : 0.0;
        ratio_b = max_reach > 0.0 ? distance_b / max_reach : 0.0;
        asset->solver_metrics.center_ab_samples++;
        asset->solver_metrics.center_ab_a_ratio_total += ratio_a;
        asset->solver_metrics.center_ab_b_ratio_total += ratio_b;
        if (ratio_a > asset->solver_metrics.center_ab_a_ratio_max) asset->solver_metrics.center_ab_a_ratio_max = ratio_a;
        if (ratio_b > asset->solver_metrics.center_ab_b_ratio_max) asset->solver_metrics.center_ab_b_ratio_max = ratio_b;
        if (distance_a > max_reach) asset->solver_metrics.center_ab_a_unreachable++;
        if (distance_b > max_reach) asset->solver_metrics.center_ab_b_unreachable++;
        if (distance_a > max_reach) asset->solver_metrics.center_ab_a_excess_total += distance_a - max_reach;
        if (distance_b > max_reach) asset->solver_metrics.center_ab_b_excess_total += distance_b - max_reach;
        if (distance_a > max_reach && distance_a - max_reach > asset->solver_metrics.center_ab_a_excess_max)
            asset->solver_metrics.center_ab_a_excess_max = distance_a - max_reach;
        if (distance_b > max_reach && distance_b - max_reach > asset->solver_metrics.center_ab_b_excess_max)
            asset->solver_metrics.center_ab_b_excess_max = distance_b - max_reach;
        if (print_sample)
            __printf("  %s hip_A=(%.3f,%.3f,%.3f) hip_B=(%.3f,%.3f,%.3f) target_A=(%.3f,%.3f,%.3f) target_B=(%.3f,%.3f,%.3f) distance_A/B=%.3f/%.3f max_reach=%.3f\n",
                     asset->bones[ik->controller].name,
                     asset->bone_transforms[thigh].position[0], asset->bone_transforms[thigh].position[1], asset->bone_transforms[thigh].position[2],
                     b[thigh].position[0], b[thigh].position[1], b[thigh].position[2],
                     desired_a[0], desired_a[1], desired_a[2], desired_b[0], desired_b[1], desired_b[2],
                     distance_a, distance_b, max_reach);
        (void)parent;
    }
    tlibc_free(b);
}

void rasterfall_model_print_center_ab_diagnostic(
    const struct rasterfall_model_asset *asset)
{
    if (!asset) return;
    __printf("center AB reachability: controller_samples=%lu A_unreachable=%.2f%% B_unreachable=%.2f%% A_avg_ratio=%.4f B_avg_ratio=%.4f A_max_ratio=%.4f B_max_ratio=%.4f A_avg_excess=%.3f B_avg_excess=%.3f A_max_excess=%.3f B_max_excess=%.3f\n",
             asset->solver_metrics.center_ab_samples,
             asset->solver_metrics.center_ab_samples ? 100.0 * asset->solver_metrics.center_ab_a_unreachable / asset->solver_metrics.center_ab_samples : 0.0,
             asset->solver_metrics.center_ab_samples ? 100.0 * asset->solver_metrics.center_ab_b_unreachable / asset->solver_metrics.center_ab_samples : 0.0,
             asset->solver_metrics.center_ab_samples ? asset->solver_metrics.center_ab_a_ratio_total / asset->solver_metrics.center_ab_samples : 0.0,
             asset->solver_metrics.center_ab_samples ? asset->solver_metrics.center_ab_b_ratio_total / asset->solver_metrics.center_ab_samples : 0.0,
             asset->solver_metrics.center_ab_a_ratio_max, asset->solver_metrics.center_ab_b_ratio_max,
             asset->solver_metrics.center_ab_samples ? asset->solver_metrics.center_ab_a_excess_total / asset->solver_metrics.center_ab_samples : 0.0,
             asset->solver_metrics.center_ab_samples ? asset->solver_metrics.center_ab_b_excess_total / asset->solver_metrics.center_ab_samples : 0.0,
             asset->solver_metrics.center_ab_a_excess_max, asset->solver_metrics.center_ab_b_excess_max);
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
    if (asset->animation.pose == RASTERFALL_MODEL_POSE_BIND) {
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
        __printf("rasterfall: bone[%u] name=\"%s\" parent=%d rest=(%d,%d,%d) flags=0x%x grant_parent=%d grant_ratio=%.6f grant_rotation=%s grant_translation=%s\n",
                 i, bone->name, bone->parent, bone->rest_x, bone->rest_y,
                 bone->rest_z, bone->flags, bone->grant_parent,
                 bone->grant_ratio, bone->grant_rotation_enabled ? "yes" : "no",
                 bone->grant_translation_enabled ? "yes" : "no");
        found++;
    }
    __printf("rasterfall: bone list matches=%u total=%u search=\"%s\"\n",
             found, asset->bone_count, search ? search : "");
}

void rasterfall_model_dump_ik(const struct rasterfall_model_asset *asset)
{
    unsigned int i, j;
    if (!asset) return;
    __printf("rasterfall: IK metadata version=%u controllers=%u\n",
             asset->format_version, asset->ik_count);
    for (i = 0; i < asset->ik_count; i++) {
        const struct rasterfall_model_ik *ik = &asset->iks[i];
        __printf("  controller[%d] %s target[%d] %s iterations=%d angle_limit_rad=%.6f angle_limit_deg=%.3f links=%u\n",
                 ik->controller, asset->bones[ik->controller].name,
                 ik->target, asset->bones[ik->target].name, ik->iterations,
                 ik->angle, ik->angle * 180.0 / M_PI, ik->link_count);
        for (j = 0; j < ik->link_count; j++) {
            const struct rasterfall_model_ik_link *link = &ik->links[j];
            __printf("    link[%u] bone[%d] %s limited=%s",
                     j, link->bone, asset->bones[link->bone].name,
                     link->limited ? "yes" : "no");
            if (link->limited)
                __printf(" lower=(%.3f,%.3f,%.3f) upper=(%.3f,%.3f,%.3f) deg",
                    link->lower[0] * 180.0 / M_PI,
                    link->lower[1] * 180.0 / M_PI,
                    link->lower[2] * 180.0 / M_PI,
                    link->upper[0] * 180.0 / M_PI,
                    link->upper[1] * 180.0 / M_PI,
                    link->upper[2] * 180.0 / M_PI);
            __printf("\n");
        }
    }
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

void rasterfall_model_map_humanoid(const struct rasterfall_model_asset *asset,
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
    rasterfall_model_map_humanoid(asset, &mapping);
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
    rasterfall_model_map_humanoid(&asset, &mapping);
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
    rasterfall_model_map_humanoid(asset, &mapping);
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
    rasterfall_model_map_humanoid(asset, &mapping);
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
    rasterfall_model_map_humanoid(asset,&mapping);
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
    struct rasterfall_glb_rotation_clip clip,reference_clip;struct rasterfall_humanoid_rotation_skeleton source,target;
    struct rasterfall_glb_rotation_reference shared_reference;
    struct rasterfall_humanoid_rotation_pose source_pose,source_reference,target_reference;struct rasterfall_humanoid_retarget_result result;
    struct rasterfall_humanoid_rest_basis source_basis[RASTERFALL_HUMANOID_BONE_COUNT],target_basis[RASTERFALL_HUMANOID_BONE_COUNT];
    struct rasterfall_humanoid_mapping mapping;unsigned int reference_mask=((1u<<RASTERFALL_HUMANOID_BONE_COUNT)-1u)&~1u;int samples[4],sample,i,bone,sampled;
    struct rasterfall_glb_rotation_trace trace;
    struct rasterfall_animation_clip timing_clip;struct rasterfall_animation_player player;
    if(rasterfall_glb_rotation_clip_load(&reference_clip,glb_path,"Idle_Loop")<0)return -1;
    if(rasterfall_glb_rotation_clip_load(&clip,glb_path,clip_name)<0){rasterfall_glb_rotation_clip_unload(&reference_clip);return -1;}
    if(rasterfall_model_build_humanoid_bases(asset,target_basis)<0){rasterfall_glb_rotation_clip_unload(&clip);rasterfall_glb_rotation_clip_unload(&reference_clip);return -1;}
    rasterfall_humanoid_rotation_skeleton_identity(&target);rasterfall_humanoid_rotation_pose_bind(&target,&target_reference);rasterfall_model_map_humanoid(asset,&mapping);
    if(rasterfall_glb_rotation_reference_build(&reference_clip,&shared_reference)<0){rasterfall_glb_rotation_clip_unload(&clip);rasterfall_glb_rotation_clip_unload(&reference_clip);return -1;}memcpy(&source,&shared_reference.skeleton,sizeof(source));memcpy(&source_reference,&shared_reference.pose,sizeof(source_reference));memcpy(source_basis,shared_reference.basis,sizeof(source_basis));
    samples[0]=0;samples[1]=clip.duration_ms/4;samples[2]=clip.duration_ms/2;samples[3]=clip.duration_ms*3/4;
    __printf("glb animation: name=%s duration_ms=%d rotation_channels=%d active_rotation_bones=%d rotation_keys=%d..%d interpolation=LINEAR\n",clip_name,clip.duration_ms,clip.rotation_channels,clip.active_rotation_bones,clip.min_rotation_keys,clip.max_rotation_keys);
    for(sample=0;sample<4;sample++){
        if(rasterfall_glb_rotation_clip_trace(&clip,samples[sample],&source,&source_pose,source_basis,&trace,&sampled)<0){rasterfall_glb_rotation_clip_unload(&clip);rasterfall_glb_rotation_clip_unload(&reference_clip);return -1;}memcpy(source_pose.global[RASTERFALL_HUMANOID_ROOT],source.rest_global[RASTERFALL_HUMANOID_ROOT],4*sizeof(double));if(rasterfall_humanoid_retarget_rotations_from_reference(&source,&source_pose,&source_reference,reference_mask,source_basis,&target,&target_reference,target_basis,&result)<0){rasterfall_glb_rotation_clip_unload(&clip);rasterfall_glb_rotation_clip_unload(&reference_clip);return -1;}
        if(sample==0&&strcmp(clip_name,"Idle_Loop")==0){double source_error=0,target_error=0;for(bone=0;bone<RASTERFALL_HUMANOID_BONE_COUNT;bone++){double e=model_quat_difference(source_pose.global[bone],bone==RASTERFALL_HUMANOID_ROOT?source.rest_global[bone]:source_reference.global[bone]);if(e>source_error)source_error=e;e=model_quat_difference(result.global_rotation[bone],target_reference.global[bone]);if(e>target_error)target_error=e;}__printf("reference invariant: source_max_error=");model_print_basis_number(source_error);__printf(" target_max_error=");model_print_basis_number(target_error);__printf(" status=%s\n",source_error<0.00000001&&target_error<0.00000001?"pass":"FAIL");if(source_error>=0.00000001||target_error>=0.00000001){rasterfall_glb_rotation_clip_unload(&clip);rasterfall_glb_rotation_clip_unload(&reference_clip);return -1;}}
        for(i=0;i<(int)asset->bone_count;i++)asset->bones[i].rotate_x=asset->bones[i].rotate_y=asset->bones[i].rotate_z=0;
        for(bone=0;bone<RASTERFALL_HUMANOID_BONE_COUNT;bone++){
            struct rasterfall_animation_quaternion q={result.local_rotation[bone][0],result.local_rotation[bone][1],result.local_rotation[bone][2],result.local_rotation[bone][3]};
            struct rasterfall_animation_rotation rotation;rasterfall_animation_quat_to_euler(q,&rotation);i=mapping.bone_indices[bone];asset->bones[i].rotate_x=rotation.x;asset->bones[i].rotate_y=rotation.y;asset->bones[i].rotate_z=rotation.z;
        }
        asset->animation.pose=RASTERFALL_MODEL_POSE_RIGHT_ARM;rasterfall_model_update_bones(asset);
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
        if(rasterfall_glb_rotation_clip_source(&clip,player.time_ms,&source,&source_pose,source_basis,&sampled)<0){rasterfall_glb_rotation_clip_unload(&clip);rasterfall_glb_rotation_clip_unload(&reference_clip);return -1;}
        memcpy(source_pose.global[RASTERFALL_HUMANOID_ROOT],source.rest_global[RASTERFALL_HUMANOID_ROOT],4*sizeof(double));
        if(rasterfall_humanoid_retarget_rotations_from_reference(&source,&source_pose,&source_reference,reference_mask,source_basis,&target,&target_reference,target_basis,&result)<0){rasterfall_glb_rotation_clip_unload(&clip);rasterfall_glb_rotation_clip_unload(&reference_clip);return -1;}
        for(bone=0;bone<RASTERFALL_HUMANOID_BONE_COUNT;bone++){struct rasterfall_animation_quaternion q={result.local_rotation[bone][0],result.local_rotation[bone][1],result.local_rotation[bone][2],result.local_rotation[bone][3]};struct rasterfall_animation_rotation rotation;rasterfall_animation_quat_to_euler(q,&rotation);i=mapping.bone_indices[bone];asset->bones[i].rotate_x=rotation.x;asset->bones[i].rotate_y=rotation.y;asset->bones[i].rotate_z=rotation.z;}
        rasterfall_model_update_bones(asset);
    }
    __printf("playback: frames=180 step_ms=16 player_time_ms=%d loop=yes drift=none Bone/BDEF_path=updated\n",player.time_ms);
    rasterfall_glb_rotation_clip_unload(&clip);rasterfall_glb_rotation_clip_unload(&reference_clip);return 0;
}

static double model_quat_angle_between(const double *a,const double *b)
{
    double dot=a[0]*b[0]+a[1]*b[1]+a[2]*b[2]+a[3]*b[3];
    if(dot<0)dot=-dot;
    if(dot>1)dot=1;
    return 2.0*atan2(sqrt(1.0-dot*dot),dot)*180.0/M_PI;
}
static void model_matrix_quaternion(const double *m,double *q)
{
    double trace=m[0]+m[4]+m[8],scale;
    if(trace>0){scale=sqrt(trace+1)*2;q[3]=scale/4;q[0]=(m[7]-m[5])/scale;q[1]=(m[2]-m[6])/scale;q[2]=(m[3]-m[1])/scale;}
    else if(m[0]>m[4]&&m[0]>m[8]){scale=sqrt(1+m[0]-m[4]-m[8])*2;q[3]=(m[7]-m[5])/scale;q[0]=scale/4;q[1]=(m[1]+m[3])/scale;q[2]=(m[2]+m[6])/scale;}
    else if(m[4]>m[8]){scale=sqrt(1+m[4]-m[0]-m[8])*2;q[3]=(m[2]-m[6])/scale;q[0]=(m[1]+m[3])/scale;q[1]=scale/4;q[2]=(m[5]+m[7])/scale;}
    else{scale=sqrt(1+m[8]-m[0]-m[4])*2;q[3]=(m[3]-m[1])/scale;q[0]=(m[2]+m[6])/scale;q[1]=(m[5]+m[7])/scale;q[2]=scale/4;}
    {double n=sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);int i;for(i=0;i<4;i++)q[i]/=n;}
}
static double model_motion_range(double q[9][RASTERFALL_HUMANOID_BONE_COUNT][4],int bone)
{
    double maximum=0;int a,b;for(a=0;a<9;a++)for(b=a+1;b<9;b++){double angle=model_quat_angle_between(q[a][bone],q[b][bone]);if(angle>maximum)maximum=angle;}return maximum;
}
static double model_motion_phase(const double left[9],const double right[9])
{
    double lm=0,rm=0,n=0,ld=0,rd=0;int i;for(i=0;i<8;i++){lm+=left[i];rm+=right[i];}lm/=8;rm/=8;for(i=0;i<8;i++){double l=left[i]-lm,r=right[i]-rm;n+=l*r;ld+=l*l;rd+=r*r;}return ld>0&&rd>0?n/sqrt(ld*rd):0;
}
static double model_direction_range(double direction[9][RASTERFALL_HUMANOID_BONE_COUNT][3],int bone)
{
    double maximum=0;int a,b;for(a=0;a<9;a++)for(b=a+1;b<9;b++){double dot=direction[a][bone][0]*direction[b][bone][0]+direction[a][bone][1]*direction[b][bone][1]+direction[a][bone][2]*direction[b][bone][2],cross[3],angle;if(dot>1)dot=1;if(dot< -1)dot= -1;cross[0]=direction[a][bone][1]*direction[b][bone][2]-direction[a][bone][2]*direction[b][bone][1];cross[1]=direction[a][bone][2]*direction[b][bone][0]-direction[a][bone][0]*direction[b][bone][2];cross[2]=direction[a][bone][0]*direction[b][bone][1]-direction[a][bone][1]*direction[b][bone][0];angle=atan2(sqrt(cross[0]*cross[0]+cross[1]*cross[1]+cross[2]*cross[2]),dot)*180.0/M_PI;if(angle>maximum)maximum=angle;}return maximum;
}

int rasterfall_model_glb_motion_diagnostic(struct rasterfall_model_asset *asset,
                                           const char *glb_path)
{
    static const char *clips[2]={"Walk_Loop","Jog_Fwd_Loop"};
    static const int bones[16]={RASTERFALL_HUMANOID_LEFT_SHOULDER,RASTERFALL_HUMANOID_RIGHT_SHOULDER,RASTERFALL_HUMANOID_LEFT_UPPER_ARM,RASTERFALL_HUMANOID_RIGHT_UPPER_ARM,RASTERFALL_HUMANOID_LEFT_FOREARM,RASTERFALL_HUMANOID_RIGHT_FOREARM,RASTERFALL_HUMANOID_LEFT_HAND,RASTERFALL_HUMANOID_RIGHT_HAND,RASTERFALL_HUMANOID_LEFT_UPPER_LEG,RASTERFALL_HUMANOID_RIGHT_UPPER_LEG,RASTERFALL_HUMANOID_LEFT_LOWER_LEG,RASTERFALL_HUMANOID_RIGHT_LOWER_LEG,RASTERFALL_HUMANOID_LEFT_FOOT,RASTERFALL_HUMANOID_RIGHT_FOOT,RASTERFALL_HUMANOID_HIPS,RASTERFALL_HUMANOID_CHEST};
    struct rasterfall_glb_rotation_clip idle,motion;struct rasterfall_glb_rotation_reference idle_shared,current_shared;struct rasterfall_humanoid_rotation_skeleton source,target,idle_skeleton,current_reference_skeleton;
    struct rasterfall_humanoid_rotation_pose idle_reference,current_reference,pose,target_reference;
    struct rasterfall_humanoid_rest_basis source_basis[RASTERFALL_HUMANOID_BONE_COUNT],idle_basis[RASTERFALL_HUMANOID_BONE_COUNT],target_basis[RASTERFALL_HUMANOID_BONE_COUNT];
    struct rasterfall_humanoid_mapping mapping;unsigned int mask=((1u<<RASTERFALL_HUMANOID_BONE_COUNT)-1u)&~1u;int clip_index;
    if(!asset||!glb_path||rasterfall_glb_rotation_clip_load(&idle,glb_path,"Idle_Loop")<0)return -1;
    if(rasterfall_glb_rotation_reference_build(&idle,&idle_shared)<0||rasterfall_model_build_humanoid_bases(asset,target_basis)<0){rasterfall_glb_rotation_clip_unload(&idle);return -1;}memcpy(&idle_skeleton,&idle_shared.skeleton,sizeof(idle_skeleton));memcpy(&idle_reference,&idle_shared.pose,sizeof(idle_reference));memcpy(idle_basis,idle_shared.basis,sizeof(idle_basis));
    rasterfall_humanoid_rotation_skeleton_identity(&target);rasterfall_humanoid_rotation_pose_bind(&target,&target_reference);rasterfall_model_map_humanoid(asset,&mapping);
    for(clip_index=0;clip_index<2;clip_index++){
        double A[9][RASTERFALL_HUMANOID_BONE_COUNT][4],B[9][RASTERFALL_HUMANOID_BONE_COUNT][4],C[9][RASTERFALL_HUMANOID_BONE_COUNT][4],CI[9][RASTERFALL_HUMANOID_BONE_COUNT][4],D[9][RASTERFALL_HUMANOID_BONE_COUNT][4],E[9][RASTERFALL_HUMANOID_BONE_COUNT][4],F[9][RASTERFALL_HUMANOID_BONE_COUNT][4],G[9][RASTERFALL_HUMANOID_BONE_COUNT][4];
        double source_forward[RASTERFALL_HUMANOID_BONE_COUNT][9],target_forward[RASTERFALL_HUMANOID_BONE_COUNT][9],source_primary[9][RASTERFALL_HUMANOID_BONE_COUNT][3],target_primary[9][RASTERFALL_HUMANOID_BONE_COUNT][3],actual_primary[9][RASTERFALL_HUMANOID_BONE_COUNT][3];int duration,sample,bi,bone,sampled;
        if(rasterfall_glb_rotation_clip_load(&motion,glb_path,clips[clip_index])<0)continue;
        duration=motion.duration_ms;
        if(rasterfall_glb_rotation_reference_build(&motion,&current_shared)<0){rasterfall_glb_rotation_clip_unload(&motion);continue;}memcpy(&current_reference_skeleton,&current_shared.skeleton,sizeof(current_reference_skeleton));memcpy(&current_reference,&current_shared.pose,sizeof(current_reference));memcpy(source_basis,current_shared.basis,sizeof(source_basis));
        __printf("motion clip=%s glb_duration_ms=%d player_duration_ms=%d key_time=0..%d samples=9 reference_used=Idle_Loop_t0 comparison=%s_t0\n",clips[clip_index],duration,duration,duration,clips[clip_index]);
        for(sample=0;sample<9;sample++){
            struct rasterfall_glb_rotation_trace trace;struct rasterfall_humanoid_retarget_result result,idle_result;int time=(int)((long)duration*sample/8);
            if(rasterfall_glb_rotation_clip_trace(&motion,time,&source,&pose,source_basis,&trace,&sampled)<0){rasterfall_glb_rotation_clip_unload(&motion);rasterfall_glb_rotation_clip_unload(&idle);return -1;}
            memcpy(pose.global[0],source.rest_global[0],4*sizeof(double));
            if(rasterfall_humanoid_retarget_rotations_from_reference(&source,&pose,&current_reference,mask,source_basis,&target,&target_reference,target_basis,&result)<0||rasterfall_humanoid_retarget_rotations_from_reference(&source,&pose,&idle_reference,mask,source_basis,&target,&target_reference,target_basis,&idle_result)<0){rasterfall_glb_rotation_clip_unload(&motion);rasterfall_glb_rotation_clip_unload(&idle);return -1;}
            for(bone=0;bone<RASTERFALL_HUMANOID_BONE_COUNT;bone++){
                double inv[4],delta[4],direction[3];memcpy(A[sample][bone],trace.animated_local[bone],4*sizeof(double));memcpy(B[sample][bone],pose.global[bone],4*sizeof(double));model_quat_delta(pose.global[bone],current_reference.global[bone],C[sample][bone]);model_quat_delta(pose.global[bone],idle_reference.global[bone],CI[sample][bone]);model_quat_change_basis(source_basis[bone].rotation,CI[sample][bone],D[sample][bone]);memcpy(E[sample][bone],idle_result.global_rotation[bone],4*sizeof(double));memcpy(F[sample][bone],idle_result.local_rotation[bone],4*sizeof(double));
                model_quat_rotate(CI[sample][bone],source_basis[bone].primary,direction);memcpy(source_primary[sample][bone],direction,3*sizeof(double));source_forward[bone][sample]=direction[2];model_quat_rotate(idle_result.global_rotation[bone],target_basis[bone].primary,direction);memcpy(target_primary[sample][bone],direction,3*sizeof(double));target_forward[bone][sample]=direction[2];(void)inv;(void)delta;
            }
            for(bone=0;bone<(int)asset->bone_count;bone++)asset->bones[bone].rotate_x=asset->bones[bone].rotate_y=asset->bones[bone].rotate_z=0;
            for(bone=0;bone<RASTERFALL_HUMANOID_BONE_COUNT;bone++){struct rasterfall_animation_quaternion q={F[sample][bone][0],F[sample][bone][1],F[sample][bone][2],F[sample][bone][3]};struct rasterfall_animation_rotation e;int index=mapping.bone_indices[bone];rasterfall_animation_quat_to_euler(q,&e);asset->bones[index].rotate_x=e.x;asset->bones[index].rotate_y=e.y;asset->bones[index].rotate_z=e.z;}
            rasterfall_model_update_bones(asset);for(bone=0;bone<RASTERFALL_HUMANOID_BONE_COUNT;bone++){int index=mapping.bone_indices[bone];model_matrix_quaternion(asset->bone_transforms[index].rotation,G[sample][bone]);matrix_vector(asset->bone_transforms[index].rotation,target_basis[bone].primary[0],target_basis[bone].primary[1],target_basis[bone].primary[2],&actual_primary[sample][bone][0],&actual_primary[sample][bone][1],&actual_primary[sample][bone][2]);}
            __printf("  time[%d]=requested:%d sampled:%d wrap=%s\n",sample,time,sampled,sample==8&&sampled==0?"yes":"no");
        }
        for(bi=0;bi<16;bi++){bone=bones[bi];__printf("range %s A_local=",humanoid_names[bone]);model_print_basis_number(model_motion_range(A,bone));__printf(" B_global=");model_print_basis_number(model_motion_range(B,bone));__printf(" C_clip_t0=");model_print_basis_number(model_motion_range(C,bone));__printf(" C_idle_t0=");model_print_basis_number(model_motion_range(CI,bone));__printf(" D_canonical=");model_print_basis_number(model_motion_range(D,bone));__printf(" E_target_global=");model_print_basis_number(model_motion_range(E,bone));__printf(" F_target_local=");model_print_basis_number(model_motion_range(F,bone));__printf(" G_target_global=");model_print_basis_number(model_motion_range(G,bone));__printf("\n");for(sample=0;sample<9;sample++){__printf("  %d%% A=",sample*125/10);model_print_basis_number(model_quat_angle_between(A[0][bone],A[sample][bone]));__printf(" B=");model_print_basis_number(model_quat_angle_between(B[0][bone],B[sample][bone]));__printf(" C=");model_print_basis_number(model_quat_angle_between(C[0][bone],C[sample][bone]));__printf(" D=");model_print_basis_number(model_quat_angle_between(D[0][bone],D[sample][bone]));__printf(" E=");model_print_basis_number(model_quat_angle_between(E[0][bone],E[sample][bone]));__printf(" F=");model_print_basis_number(model_quat_angle_between(F[0][bone],F[sample][bone]));__printf(" G=");model_print_basis_number(model_quat_angle_between(G[0][bone],G[sample][bone]));__printf(" source_primary_z=");model_print_basis_number(source_forward[bone][sample]);__printf(" target_primary_z=");model_print_basis_number(target_forward[bone][sample]);__printf("\n");}}
        {static const double identity[4]={0,0,0,1};for(bi=0;bi<16;bi++){bone=bones[bi];__printf("reference-offset %s clip_t0=",humanoid_names[bone]);model_print_basis_number(model_quat_angle_between(C[0][bone],identity));__printf(" idle_t0_start=");model_print_basis_number(model_quat_angle_between(CI[0][bone],identity));__printf(" idle_t0_half=");model_print_basis_number(model_quat_angle_between(CI[4][bone],identity));__printf(" primary_range source=");model_print_basis_number(model_direction_range(source_primary,bone));__printf(" target=");model_print_basis_number(model_direction_range(target_primary,bone));__printf(" actual=");model_print_basis_number(model_direction_range(actual_primary,bone));__printf("\n");}}
        {static const int pairs[3][2]={{RASTERFALL_HUMANOID_LEFT_UPPER_ARM,RASTERFALL_HUMANOID_RIGHT_UPPER_ARM},{RASTERFALL_HUMANOID_LEFT_UPPER_LEG,RASTERFALL_HUMANOID_RIGHT_UPPER_LEG},{RASTERFALL_HUMANOID_LEFT_LOWER_LEG,RASTERFALL_HUMANOID_RIGHT_LOWER_LEG}};int p;for(p=0;p<3;p++){__printf("phase %s/%s source_correlation=",humanoid_names[pairs[p][0]],humanoid_names[pairs[p][1]]);model_print_basis_number(model_motion_phase(source_forward[pairs[p][0]],source_forward[pairs[p][1]]));__printf(" target_correlation=");model_print_basis_number(model_motion_phase(target_forward[pairs[p][0]],target_forward[pairs[p][1]]));__printf(" source_0_50=(");model_print_basis_number(source_forward[pairs[p][0]][0]);__printf(",");model_print_basis_number(source_forward[pairs[p][0]][4]);__printf(")/(");model_print_basis_number(source_forward[pairs[p][1]][0]);__printf(",");model_print_basis_number(source_forward[pairs[p][1]][4]);__printf(") target_0_50=(");model_print_basis_number(target_forward[pairs[p][0]][0]);__printf(",");model_print_basis_number(target_forward[pairs[p][0]][4]);__printf(")/(");model_print_basis_number(target_forward[pairs[p][1]][0]);__printf(",");model_print_basis_number(target_forward[pairs[p][1]][4]);__printf(")\n");}}
        {static const int pairs[3][2]={{RASTERFALL_HUMANOID_LEFT_UPPER_ARM,RASTERFALL_HUMANOID_RIGHT_UPPER_ARM},{RASTERFALL_HUMANOID_LEFT_UPPER_LEG,RASTERFALL_HUMANOID_RIGHT_UPPER_LEG},{RASTERFALL_HUMANOID_LEFT_LOWER_LEG,RASTERFALL_HUMANOID_RIGHT_LOWER_LEG}};static const int quarters[4]={0,2,4,6};int p,q;for(p=0;p<3;p++){__printf("quarters %s/%s source_lr=",humanoid_names[pairs[p][0]],humanoid_names[pairs[p][1]]);for(q=0;q<4;q++){if(q)__printf(";");__printf("%d:",q*25);model_print_basis_number(source_forward[pairs[p][0]][quarters[q]]);__printf(",");model_print_basis_number(source_forward[pairs[p][1]][quarters[q]]);}__printf(" target_lr=");for(q=0;q<4;q++){if(q)__printf(";");__printf("%d:",q*25);model_print_basis_number(target_forward[pairs[p][0]][quarters[q]]);__printf(",");model_print_basis_number(target_forward[pairs[p][1]][quarters[q]]);}__printf("\n");}}
        rasterfall_glb_rotation_clip_unload(&motion);
    }
    rasterfall_glb_rotation_clip_unload(&idle);return 0;
}

int rasterfall_model_skinning_logic_test(void)
{
    struct rasterfall_model_asset asset;
    struct rasterfall_model_bone bones[2];
    struct rasterfall_model_bone_transform transforms[2];
    unsigned int order[2] = {0, 1};
    unsigned char vertices[RASTERFALL_MODEL_VERTEX_BYTES_EDGE_SCALE];
    unsigned char skin[RASTERFALL_MODEL_SKIN_VERTEX_BYTES];
    struct rasterfall_model_attachment_transform attachment;
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
    bones[1].name = "RIGHT_HAND";
    bones[1].rest_x = 10;
    *(int *)(vertices) = 20;
    *(short *)(vertices + 12) = 32767;
    skin[0] = 1;
    skin[2] = 0xff; skin[3] = 0xff;
    skin[4] = 0xff; skin[5] = 0xff;
    asset.animation.pose = RASTERFALL_MODEL_POSE_BIND;
    if (rasterfall_model_update_bones(&asset) < 0 ||
        rasterfall_model_skin_vertex(&asset, 0, position, normal) < 0 ||
        position[0] != 20 || position[1] != 0 || normal[0] != 32767)
        return 1;
    bones[0].rotate_z = 90;
    asset.animation.pose = RASTERFALL_MODEL_POSE_RIGHT_ARM;
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
    if (rasterfall_model_attachment_transform(
            &asset, "RIGHT_HAND", &attachment) < 0 ||
        attachment.position[0] != 10.0 || attachment.position[1] != 0.0 ||
        attachment.rotation[0] > 0.001 || attachment.rotation[0] < -0.001 ||
        attachment.rotation[3] < 0.999) return 6;
    {
        struct rasterfall_model_asset arm;
        struct rasterfall_model_bone arm_bones[4];
        struct rasterfall_model_bone_transform arm_transforms[4];
        unsigned int arm_order[4]={0,1,2,3};
        double target[3]={12.0,12.0,0.0},pole[3]={0.0,0.0,1.0};
        double prior_pole[3],dot;
        __memset(&arm,0,sizeof(arm));__memset(arm_bones,0,sizeof(arm_bones));
        arm.bones=arm_bones;arm.bone_transforms=arm_transforms;
        arm.bone_order=arm_order;arm.bone_count=4;
        arm_bones[0].parent=-1;arm_bones[0].name="ROOT";
        arm_bones[1].parent=0;arm_bones[1].name="UPPER";
        arm_bones[2].parent=1;arm_bones[2].name="FOREARM";arm_bones[2].rest_x=10;
        arm_bones[3].parent=2;arm_bones[3].name="HAND";arm_bones[3].rest_x=20;
        if(rasterfall_model_solve_two_bone_attachment(&arm,"UPPER","FOREARM",
            "HAND",target,pole)<0||arm.attachment_ik_diagnostics.hand_error>0.1||
            arm.attachment_ik_diagnostics.reach_clamped)return 7;
        memcpy(prior_pole,arm.attachment_ik_previous_pole,sizeof(prior_pole));
        target[0]=12.2;target[1]=12.1;
        if(rasterfall_model_solve_two_bone_attachment(&arm,"UPPER","FOREARM",
            "HAND",target,pole)<0||!arm.attachment_ik_diagnostics.used_previous_pole)
            return 8;
        dot=prior_pole[0]*arm.attachment_ik_previous_pole[0]+
            prior_pole[1]*arm.attachment_ik_previous_pole[1]+
            prior_pole[2]*arm.attachment_ik_previous_pole[2];
        if(dot<0.9)return 9;
        target[0]=40.0;target[1]=0.0;
        if(rasterfall_model_solve_two_bone_attachment(&arm,"UPPER","FOREARM",
            "HAND",target,pole)<0||!arm.attachment_ik_diagnostics.reach_clamped||
            arm.attachment_ik_diagnostics.hand_error>0.1)return 10;
        rasterfall_model_print_two_bone_diagnostics(&arm,"logic_test_left_arm");
    }
    return 0;
}
