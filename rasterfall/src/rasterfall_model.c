#include "core.h"
#include "tlibc_everything.h"
#include "toy_assets.h"
#include "rasterfall_model.h"
#include "math.h"

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
    if (!asset->bones || !asset->bone_transforms) return -1;
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
