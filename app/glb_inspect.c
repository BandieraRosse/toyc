/* Minimal offline glTF 2.0 skeleton and animation inspector.  This tool does
 * not import meshes and deliberately keeps glTF concepts out of Rasterfall's
 * renderer and runtime animation path. */
#include "core.h"
#include "tlibc_print.h"
#include "tlibc_everything.h"
#include "rasterfall_humanoid.h"
#include "rasterfall_humanoid_basis.h"
#include "rasterfall_glb_animation.h"
#include "rasterfall_glb_preview.h"
#include "math.h"

#define GLB_MAGIC 0x46546c67U
#define GLB_JSON  0x4e4f534aU
#define GLB_BIN   0x004e4942U
#define GLB_MAX_FILE (128 * 1024 * 1024)

struct slice { const char *p, *end; };
struct table { struct slice *items; int count; };
struct glb_doc {
    unsigned char *file;
    int file_size;
    const unsigned char *bin;
    int bin_size;
    struct slice root;
    struct table nodes, skins, animations, accessors, views, buffers, meshes;
    int *parents;
};
struct accessor_info {
    int view, offset, count, component, components;
};

static unsigned int read_u32(const unsigned char *p)
{ return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24; }
static int is_ws(char c)
{ return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }
static const char *skip_ws(const char *p, const char *end)
{ while (p < end && is_ws(*p)) p++; return p; }

static const char *skip_string(const char *p, const char *end)
{
    int escaped = 0;
    if (p >= end || *p != '"') return p;
    for (p++; p < end; p++) {
        if (escaped) escaped = 0;
        else if (*p == '\\') escaped = 1;
        else if (*p == '"') return p + 1;
    }
    return end;
}

static const char *skip_value(const char *p, const char *end)
{
    int depth = 0;
    p = skip_ws(p, end);
    if (p >= end) return end;
    if (*p == '"') return skip_string(p, end);
    if (*p != '{' && *p != '[') {
        while (p < end && *p != ',' && *p != '}' && *p != ']') p++;
        return p;
    }
    for (; p < end; p++) {
        if (*p == '"') { p = skip_string(p, end) - 1; continue; }
        if (*p == '{' || *p == '[') depth++;
        else if (*p == '}' || *p == ']') {
            if (--depth == 0) return p + 1;
        }
    }
    return end;
}

/* Object-local lookup: unlike a substring scanner this never sees keys in a
 * nested child object, which matters for animation sampler/target records. */
static struct slice object_value(struct slice object, const char *wanted)
{
    const char *p = skip_ws(object.p, object.end), *key_end, *value_end;
    struct slice none = {0, 0};
    int wanted_len = (int)strlen(wanted);
    if (p >= object.end || *p != '{') return none;
    p++;
    while ((p = skip_ws(p, object.end)) < object.end && *p != '}') {
        const char *key;
        int length;
        if (*p != '"') return none;
        key = p + 1; key_end = skip_string(p, object.end);
        if (key_end > object.end || key_end <= p + 1) return none;
        length = (int)(key_end - key - 1);
        p = skip_ws(key_end, object.end);
        if (p >= object.end || *p++ != ':') return none;
        p = skip_ws(p, object.end); value_end = skip_value(p, object.end);
        if (length == wanted_len && !memcmp(key, wanted, length)) {
            struct slice out = {p, value_end}; return out;
        }
        p = skip_ws(value_end, object.end);
        if (p < object.end && *p == ',') p++;
    }
    return none;
}

static int table_build(struct slice array, struct table *table)
{
    const char *p;
    int capacity = 0;
    table->items = 0; table->count = 0;
    p = array.p ? skip_ws(array.p, array.end) : 0;
    if (!p || p >= array.end || *p != '[') return 0;
    p++;
    while ((p = skip_ws(p, array.end)) < array.end && *p != ']') {
        const char *end = skip_value(p, array.end);
        if (end <= p || end > array.end) return -1;
        if (table->count == capacity) {
            int next = capacity ? capacity * 2 : 16;
            struct slice *items = tlibc_malloc(next * sizeof(*items));
            if (!items) return -1;
            if (table->items) {
                memcpy(items, table->items, table->count * sizeof(*items));
                tlibc_free(table->items);
            }
            table->items = items; capacity = next;
        }
        table->items[table->count].p = p;
        table->items[table->count++].end = end;
        p = skip_ws(end, array.end);
        if (p < array.end && *p == ',') p++;
        else if (p >= array.end || *p != ']') return -1;
    }
    return 0;
}

static int json_int(struct slice value, int fallback)
{
    const char *p = value.p ? skip_ws(value.p, value.end) : 0;
    int sign = 1, result = 0;
    if (!p || p >= value.end) return fallback;
    if (*p == '-') { sign = -1; p++; }
    if (p >= value.end || *p < '0' || *p > '9') return fallback;
    while (p < value.end && *p >= '0' && *p <= '9')
        result = result * 10 + (*p++ - '0');
    return result * sign;
}

static double json_number(struct slice value, double fallback)
{
    const char *p = value.p ? skip_ws(value.p, value.end) : 0;
    double result = 0.0, fraction = 0.1, power = 1.0;
    int sign = 1, exponent = 0, exponent_sign = 1;
    if (!p || p >= value.end) return fallback;
    if (*p == '-') { sign = -1; p++; }
    if (p >= value.end || ((*p < '0' || *p > '9') && *p != '.')) return fallback;
    while (p < value.end && *p >= '0' && *p <= '9') result = result * 10.0 + (*p++ - '0');
    if (p < value.end && *p == '.') for (p++; p < value.end && *p >= '0' && *p <= '9'; p++) {
        result += (*p - '0') * fraction; fraction *= 0.1;
    }
    if (p < value.end && (*p == 'e' || *p == 'E')) {
        p++; if (p < value.end && (*p == '-' || *p == '+')) { if (*p == '-') exponent_sign = -1; p++; }
        while (p < value.end && *p >= '0' && *p <= '9') exponent = exponent * 10 + (*p++ - '0');
        while (exponent-- > 0) power *= exponent_sign > 0 ? 10.0 : 0.1;
    }
    return sign * result * power;
}

static struct slice array_value(struct slice array, int index)
{
    const char *p;
    int at = 0;
    struct slice none = {0, 0};
    p = array.p ? skip_ws(array.p, array.end) : 0;
    if (!p || p >= array.end || *p != '[') return none;
    for (p++; (p = skip_ws(p, array.end)) < array.end && *p != ']'; at++) {
        const char *end = skip_value(p, array.end);
        if (at == index) { struct slice out = {p, end}; return out; }
        p = skip_ws(end, array.end);
        if (p < array.end && *p == ',') p++; else break;
    }
    return none;
}

static int string_copy(struct slice value, char *out, int capacity)
{
    const char *p = value.p ? skip_ws(value.p, value.end) : 0;
    int length = 0;
    if (!p || p >= value.end || *p++ != '"' || capacity <= 0) { if (capacity) out[0] = 0; return -1; }
    while (p < value.end && *p != '"' && length + 1 < capacity) {
        if (*p == '\\' && p + 1 < value.end) p++;
        out[length++] = *p++;
    }
    out[length] = 0;
    return length;
}

static void table_free(struct table *table)
{ if (table->items) tlibc_free(table->items); table->items = 0; table->count = 0; }
static void doc_free(struct glb_doc *doc)
{
    table_free(&doc->nodes); table_free(&doc->skins); table_free(&doc->animations);
    table_free(&doc->accessors); table_free(&doc->views); table_free(&doc->buffers);
    table_free(&doc->meshes);
    if (doc->parents) tlibc_free(doc->parents);
    if (doc->file) tlibc_free(doc->file);
    __memset(doc, 0, sizeof(*doc));
}

static int doc_build_tables(struct glb_doc *doc)
{
    int i, child;
    if (table_build(object_value(doc->root, "nodes"), &doc->nodes) < 0 ||
        table_build(object_value(doc->root, "skins"), &doc->skins) < 0 ||
        table_build(object_value(doc->root, "animations"), &doc->animations) < 0 ||
        table_build(object_value(doc->root, "accessors"), &doc->accessors) < 0 ||
        table_build(object_value(doc->root, "bufferViews"), &doc->views) < 0 ||
        table_build(object_value(doc->root, "buffers"), &doc->buffers) < 0 ||
        table_build(object_value(doc->root, "meshes"), &doc->meshes) < 0) return -1;
    doc->parents = tlibc_malloc((doc->nodes.count ? doc->nodes.count : 1) * sizeof(int));
    if (!doc->parents) return -1;
    for (i = 0; i < doc->nodes.count; i++) doc->parents[i] = -1;
    for (i = 0; i < doc->nodes.count; i++) {
        struct slice children = object_value(doc->nodes.items[i], "children"), item;
        for (child = 0; (item = array_value(children, child)).p; child++) {
            int index = json_int(item, -1);
            if (index < 0 || index >= doc->nodes.count || doc->parents[index] >= 0)
                return -1;
            doc->parents[index] = i;
        }
    }
    for (i = 0; i < doc->nodes.count; i++) {
        int at = i, steps = 0;
        while (at >= 0) { if (++steps > doc->nodes.count) return -1; at = doc->parents[at]; }
    }
    return 0;
}

static int doc_parse_memory(struct glb_doc *doc, unsigned char *file, int size,
                            int owns_file)
{
    unsigned int json_size, bin_header, declared;
    __memset(doc, 0, sizeof(*doc));
    if (size < 20 || read_u32(file) != GLB_MAGIC || read_u32(file + 4) != 2) return -1;
    declared = read_u32(file + 8); json_size = read_u32(file + 12);
    if (declared != (unsigned int)size || read_u32(file + 16) != GLB_JSON ||
        json_size > (unsigned int)size - 20) return -1;
    doc->file = owns_file ? file : 0; doc->file_size = size;
    doc->root.p = (const char *)file + 20; doc->root.end = doc->root.p + json_size;
    bin_header = 20 + json_size;
    if (bin_header < (unsigned int)size) {
        unsigned int bin_size;
        if (bin_header > (unsigned int)size - 8 || read_u32(file + bin_header + 4) != GLB_BIN) return -1;
        bin_size = read_u32(file + bin_header);
        if (bin_size > (unsigned int)size - bin_header - 8 || bin_header + 8 + bin_size != (unsigned int)size) return -1;
        doc->bin = file + bin_header + 8; doc->bin_size = bin_size;
    }
    if (doc_build_tables(doc) < 0) return -1;
    return 0;
}

static int doc_load(struct glb_doc *doc, const char *path)
{
    int fd, size, got = 0, n;
    unsigned char *file;
    struct stat st;
    fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0 || __fstat(fd, &st) < 0 || st.st_size < 20 || st.st_size > GLB_MAX_FILE) { if (fd >= 0) __close(fd); return -1; }
    size = (int)st.st_size; file = tlibc_malloc(size);
    if (!file) { __close(fd); return -1; }
    while (got < size && (n = __read(fd, file + got, size - got)) > 0) got += n;
    __close(fd);
    if (got != size || doc_parse_memory(doc, file, size, 1) < 0) { tlibc_free(file); return -1; }
    return 0;
}

static int component_bytes(int component)
{
    if (component == 5120 || component == 5121) return 1;
    if (component == 5122 || component == 5123) return 2;
    if (component == 5125 || component == 5126) return 4;
    return 0;
}
static int type_components(struct slice type)
{
    char name[12]; string_copy(type, name, sizeof(name));
    if (!strcmp(name, "SCALAR")) return 1;
    if (!strcmp(name, "VEC2")) return 2;
    if (!strcmp(name, "VEC3")) return 3;
    if (!strcmp(name, "VEC4")) return 4;
    if (!strcmp(name, "MAT4")) return 16;
    return 0;
}
static int read_accessor_info(const struct glb_doc *doc, int index,
                              struct accessor_info *out)
{
    struct slice object, view;
    int bytes, stride, length, buffer;
    unsigned long required;
    if (index < 0 || index >= doc->accessors.count) return -1;
    object = doc->accessors.items[index];
    out->view = json_int(object_value(object, "bufferView"), -1);
    out->offset = json_int(object_value(object, "byteOffset"), 0);
    out->count = json_int(object_value(object, "count"), -1);
    out->component = json_int(object_value(object, "componentType"), 0);
    out->components = type_components(object_value(object, "type"));
    bytes = component_bytes(out->component);
    if (out->view < 0 || out->view >= doc->views.count || out->offset < 0 ||
        out->count < 0 || !bytes || !out->components) return -1;
    view = doc->views.items[out->view];
    buffer = json_int(object_value(view, "buffer"), 0);
    length = json_int(object_value(view, "byteLength"), -1);
    stride = json_int(object_value(view, "byteStride"), bytes * out->components);
    if (buffer != 0 || length < 0 || stride < bytes * out->components) return -1;
    required = out->count ? (unsigned long)(out->count - 1) * stride +
                            bytes * out->components : 0;
    if ((unsigned long)out->offset + required > (unsigned long)length) return -1;
    if ((unsigned long)json_int(object_value(view, "byteOffset"), 0) + length >
        (unsigned long)doc->bin_size) return -1;
    return 0;
}

static const unsigned char *accessor_data(const struct glb_doc *doc,
                                          const struct accessor_info *accessor,
                                          int *stride)
{
    struct slice view = doc->views.items[accessor->view];
    *stride = json_int(object_value(view, "byteStride"),
                       component_bytes(accessor->component) * accessor->components);
    return doc->bin + json_int(object_value(view, "byteOffset"), 0) + accessor->offset;
}
static float read_f32(const unsigned char *p)
{ union { unsigned int u; float f; } value; value.u = read_u32(p); return value.f; }
static int valid_float(float value)
{ return value == value && value > -1.0e30f && value < 1.0e30f; }

static void node_name(const struct glb_doc *doc, int node, char *name, int size)
{
    if (node < 0 || node >= doc->nodes.count ||
        string_copy(object_value(doc->nodes.items[node], "name"), name, size) < 0)
        snprintf(name, size, "node_%d", node);
}
static void print_vector(struct slice value, int count, const double *defaults)
{
    int i;
    __printf("(");
    for (i = 0; i < count; i++) {
        int scaled = (int)(json_number(array_value(value, i), defaults[i]) * 1000000.0);
        __printf("%s%s%d.%06d", i ? "," : "", scaled < 0 ? "-" : "",
                 abs(scaled) / 1000000, abs(scaled) % 1000000);
    }
    __printf(")");
}

static const char *semantic_names[RASTERFALL_HUMANOID_BONE_COUNT] = {
    "ROOT", "HIPS", "SPINE", "CHEST", "UPPER_CHEST", "NECK", "HEAD",
    "LEFT_SHOULDER", "LEFT_UPPER_ARM", "LEFT_FOREARM", "LEFT_HAND",
    "RIGHT_SHOULDER", "RIGHT_UPPER_ARM", "RIGHT_FOREARM", "RIGHT_HAND",
    "LEFT_UPPER_LEG", "LEFT_LOWER_LEG", "LEFT_FOOT",
    "RIGHT_UPPER_LEG", "RIGHT_LOWER_LEG", "RIGHT_FOOT"
};
static const char *quaternius_names[RASTERFALL_HUMANOID_BONE_COUNT][2] = {
    {"root", "Root"}, {"pelvis", "Pelvis"}, {"spine_01", "Spine1"},
    {"spine_02", "Spine2"}, {"spine_03", "Spine3"}, {"neck_01", "Neck"},
    {"Head", "head"}, {"clavicle_l", "shoulder_l"}, {"upperarm_l", "UpperArm_L"},
    {"lowerarm_l", "LowerArm_L"}, {"hand_l", "Hand_L"},
    {"clavicle_r", "shoulder_r"}, {"upperarm_r", "UpperArm_R"},
    {"lowerarm_r", "LowerArm_R"}, {"hand_r", "Hand_R"},
    {"thigh_l", "Thigh_L"}, {"calf_l", "Calf_L"}, {"foot_l", "Foot_L"},
    {"thigh_r", "Thigh_R"}, {"calf_r", "Calf_R"}, {"foot_r", "Foot_R"}
};
static const unsigned char semantic_chains[][2] = {
    {0,1},{1,2},{2,3},{3,4},{4,5},{5,6},{4,7},{7,8},{8,9},{9,10},
    {4,11},{11,12},{12,13},{13,14},{1,15},{15,16},{16,17},
    {1,18},{18,19},{19,20}
};

static int find_node_exact(const struct glb_doc *doc, const char *primary,
                           const char *alias)
{
    int pass, i; char name[128];
    for (pass = 0; pass < 2; pass++) for (i = 0; i < doc->nodes.count; i++) {
        node_name(doc, i, name, sizeof(name));
        if (!strcmp(name, pass ? alias : primary)) return i;
    }
    return -1;
}
static void map_quaternius(const struct glb_doc *doc, int *mapping)
{
    int i;
    for (i = 0; i < RASTERFALL_HUMANOID_BONE_COUNT; i++)
        mapping[i] = find_node_exact(doc, quaternius_names[i][0], quaternius_names[i][1]);
}
static int descendant(const struct glb_doc *doc, int child, int parent)
{
    int steps = 0;
    if (child < 0 || parent < 0) return 1;
    while (child >= 0 && steps++ <= doc->nodes.count) {
        if (child == parent) return 1;
        child = doc->parents[child];
    }
    return 0;
}
static void print_mapping(const struct glb_doc *doc, int *mapping)
{
    int i, j, mapped = 0, missing = 0, duplicates = 0, chain_errors = 0;
    char name[128];
    for (i = 0; i < RASTERFALL_HUMANOID_BONE_COUNT; i++) {
        if (mapping[i] >= 0) { node_name(doc, mapping[i], name, sizeof(name)); mapped++; __printf("%s -> %s / node %d\n", semantic_names[i], name, mapping[i]); }
        else { missing++; __printf("%s -> MISSING\n", semantic_names[i]); }
        for (j = 0; j < i; j++) if (mapping[i] >= 0 && mapping[i] == mapping[j]) { duplicates++; __printf("humanoid: duplicate %s and %s -> node %d\n", semantic_names[j], semantic_names[i], mapping[i]); break; }
    }
    for (i = 0; i < (int)(sizeof(semantic_chains) / sizeof(semantic_chains[0])); i++)
        if (!descendant(doc, mapping[semantic_chains[i][1]], mapping[semantic_chains[i][0]])) {
            chain_errors++; __printf("humanoid: abnormal parent chain %s is not below %s\n", semantic_names[semantic_chains[i][1]], semantic_names[semantic_chains[i][0]]);
        }
    __printf("humanoid: mapped=%d/%d missing_core_bones=%d duplicate_mappings=%d parent_chain_errors=%d\n",
             mapped, RASTERFALL_HUMANOID_BONE_COUNT, missing, duplicates, chain_errors);
}

static int accessor_changes(const struct glb_doc *doc, int index)
{
    struct accessor_info accessor; const unsigned char *data; int stride, i;
    if (read_accessor_info(doc, index, &accessor) < 0 || accessor.component != 5126 || accessor.count < 2) return 0;
    data = accessor_data(doc, &accessor, &stride);
    for (i = 0; i < accessor.components; i++) {
        float difference = read_f32(data + i * 4) -
            read_f32(data + (accessor.count - 1) * stride + i * 4);
        if (difference < -0.00001f || difference > 0.00001f) return 1;
    }
    return 0;
}

static int inspect_animations(const struct glb_doc *doc, const int *mapping,
                              int humanoid_only)
{
    int animation_index, total_t = 0, total_r = 0, total_s = 0, root_motion_count = 0;
    for (animation_index = 0; animation_index < doc->animations.count; animation_index++) {
        struct slice animation = doc->animations.items[animation_index];
        struct table samplers, channels; char animation_name[128];
        int channel_index, translations = 0, rotations = 0, scales = 0, root_motion = 0;
        int linear = 0, step = 0, cubic = 0;
        double duration = 0.0;
        if (table_build(object_value(animation, "samplers"), &samplers) < 0 ||
            table_build(object_value(animation, "channels"), &channels) < 0) return -1;
        if (string_copy(object_value(animation, "name"), animation_name, sizeof(animation_name)) < 0)
            snprintf(animation_name, sizeof(animation_name), "animation_%d", animation_index);
        for (channel_index = 0; channel_index < channels.count; channel_index++) {
            struct slice channel = channels.items[channel_index], target = object_value(channel, "target");
            int sampler_index = json_int(object_value(channel, "sampler"), -1);
            int target_node = json_int(object_value(target, "node"), -1), input, output;
            char path[20], interpolation[20], target_name[128];
            struct accessor_info timing;
            double channel_duration = 0.0;
            if (sampler_index < 0 || sampler_index >= samplers.count || target_node < 0 || target_node >= doc->nodes.count) { table_free(&samplers); table_free(&channels); return -1; }
            input = json_int(object_value(samplers.items[sampler_index], "input"), -1);
            output = json_int(object_value(samplers.items[sampler_index], "output"), -1);
            if (read_accessor_info(doc, input, &timing) < 0 || read_accessor_info(doc, output, &timing) < 0) { table_free(&samplers); table_free(&channels); return -1; }
            channel_duration = json_number(array_value(object_value(doc->accessors.items[input], "max"), 0), 0.0);
            if (channel_duration > duration) duration = channel_duration;
            string_copy(object_value(target, "path"), path, sizeof(path));
            if (string_copy(object_value(samplers.items[sampler_index], "interpolation"), interpolation, sizeof(interpolation)) < 0) strcpy(interpolation, "LINEAR");
            if (!strcmp(path, "translation")) translations++;
            else if (!strcmp(path, "rotation")) rotations++;
            else if (!strcmp(path, "scale")) scales++;
            if (!strcmp(interpolation, "STEP")) step++;
            else if (!strcmp(interpolation, "CUBICSPLINE")) cubic++;
            else linear++;
            if (!strcmp(path, "translation") &&
                target_node == mapping[RASTERFALL_HUMANOID_ROOT] &&
                accessor_changes(doc, output)) root_motion = 1;
            if (!humanoid_only) { node_name(doc, target_node, target_name, sizeof(target_name)); __printf("  channel[%d] target=%s/node%d path=%s interpolation=%s\n", channel_index, target_name, target_node, path, interpolation); }
        }
        total_t += translations; total_r += rotations; total_s += scales;
        if (root_motion) { root_motion_count++; }
        __printf("animation[%d] name=\"%s\" duration=%d.%03ds channels=%d translation=%d rotation=%d scale=%d interpolation={LINEAR=%d,STEP=%d,CUBICSPLINE=%d} root_motion=%s\n",
                 animation_index, animation_name, (int)duration, abs((int)(duration * 1000.0)) % 1000,
                 channels.count, translations, rotations, scales, linear, step,
                 cubic, root_motion ? "yes" : "no");
        table_free(&samplers); table_free(&channels);
    }
    __printf("animations: count=%d channels={translation=%d,rotation=%d,scale=%d} root_motion_clips=%d\n",
             doc->animations.count, total_t, total_r, total_s, root_motion_count);
    return 0;
}

static int inspect_doc(const struct glb_doc *doc, int humanoid_only)
{
    int i, j, total_joints = 0, mapping[RASTERFALL_HUMANOID_BONE_COUNT];
    int inverse_count = 0, inverse_valid = 1; char name[128], parent_name[128];
    const double t_default[3] = {0,0,0}, r_default[4] = {0,0,0,1}, s_default[3] = {1,1,1};
    map_quaternius(doc, mapping);
    __printf("glb: nodes=%d skins=%d animations=%d accessors=%d bufferViews=%d buffers=%d bin_bytes=%d\n",
             doc->nodes.count, doc->skins.count, doc->animations.count,
             doc->accessors.count, doc->views.count, doc->buffers.count, doc->bin_size);
    for (i = 0; i < doc->skins.count; i++) {
        struct slice skin = doc->skins.items[i], joints = object_value(skin, "joints"), item;
        int skeleton = json_int(object_value(skin, "skeleton"), -1);
        int declared_skeleton = skeleton;
        int inverse = json_int(object_value(skin, "inverseBindMatrices"), -1), joint_count = 0;
        struct accessor_info matrices;
        while ((item = array_value(joints, joint_count)).p) joint_count++;
        if (skeleton < 0 && joint_count) skeleton = json_int(array_value(joints, 0), -1);
        total_joints += joint_count; node_name(doc, skeleton, name, sizeof(name));
        if (read_accessor_info(doc, inverse, &matrices) < 0 || matrices.component != 5126 || matrices.components != 16 || matrices.count != joint_count) inverse_valid = 0;
        else {
            const unsigned char *data; int stride, matrix, component;
            inverse_count += matrices.count; data = accessor_data(doc, &matrices, &stride);
            for (matrix = 0; matrix < matrices.count; matrix++) for (component = 0; component < 16; component++)
                if (!valid_float(read_f32(data + matrix * stride + component * 4))) inverse_valid = 0;
        }
        __printf("skin[%d] skeleton=%s/node%d (%s) joints=%d inverseBindMatrices=%d valid=%s\n",
                 i, name, skeleton, declared_skeleton >= 0 ? "declared" : "inferred joint root",
                 joint_count, inverse, inverse_valid ? "yes" : "no");
        for (j = 0; j < joint_count; j++) {
            int node = json_int(array_value(joints, j), -1), semantic = -1;
            if (node < 0 || node >= doc->nodes.count) return -1;
            for (semantic = 0; semantic < RASTERFALL_HUMANOID_BONE_COUNT; semantic++) if (mapping[semantic] == node) break;
            if (humanoid_only && semantic == RASTERFALL_HUMANOID_BONE_COUNT) continue;
            node_name(doc, node, name, sizeof(name)); node_name(doc, doc->parents[node], parent_name, sizeof(parent_name));
            __printf("  joint[%d] node=%d name=\"%s\" parent=%d/\"%s\" T=", j, node, name, doc->parents[node], parent_name);
            print_vector(object_value(doc->nodes.items[node], "translation"), 3, t_default);
            __printf(" R="); print_vector(object_value(doc->nodes.items[node], "rotation"), 4, r_default);
            __printf(" S="); print_vector(object_value(doc->nodes.items[node], "scale"), 3, s_default); __printf("\n");
        }
    }
    __printf("skins: total_joints=%d inverse_bind_matrices=%d valid=%s\n", total_joints, inverse_count, inverse_valid ? "yes" : "no");
    print_mapping(doc, mapping);
    return inspect_animations(doc, mapping, humanoid_only);
}

static float absolute_float(float value) { return value < 0.0f ? -value : value; }

static float accessor_max_delta(const struct glb_doc *doc, int index,
                                const float *reference, int use_reference)
{
    struct accessor_info accessor; const unsigned char *data;
    int stride, sample, component; float maximum = 0.0f;
    if (read_accessor_info(doc, index, &accessor) < 0 || accessor.component != 5126)
        return -1.0f;
    data = accessor_data(doc, &accessor, &stride);
    for (sample = 0; sample < accessor.count; sample++)
        for (component = 0; component < accessor.components; component++) {
            float base = use_reference ? reference[component] : read_f32(data + component * 4);
            float delta = absolute_float(read_f32(data + sample * stride + component * 4) - base);
            if (delta > maximum) maximum = delta;
        }
    return maximum;
}

static void matrix_identity(double *matrix)
{
    int i; for (i = 0; i < 16; i++) matrix[i] = 0.0;
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0;
}
static void matrix_multiply4(const double *a, const double *b, double *out)
{
    double result[16]; int row, column, k;
    for (column = 0; column < 4; column++) for (row = 0; row < 4; row++) {
        result[column * 4 + row] = 0.0;
        for (k = 0; k < 4; k++)
            result[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
    }
    memcpy(out, result, sizeof(result));
}
static void node_local_matrix(struct slice node, double *matrix)
{
    struct slice translation = object_value(node, "translation");
    struct slice rotation = object_value(node, "rotation");
    struct slice scale = object_value(node, "scale");
    double x = json_number(array_value(rotation, 0), 0.0);
    double y = json_number(array_value(rotation, 1), 0.0);
    double z = json_number(array_value(rotation, 2), 0.0);
    double w = json_number(array_value(rotation, 3), 1.0);
    double sx = json_number(array_value(scale, 0), 1.0);
    double sy = json_number(array_value(scale, 1), 1.0);
    double sz = json_number(array_value(scale, 2), 1.0);
    matrix_identity(matrix);
    matrix[0] = (1.0 - 2.0 * (y*y + z*z)) * sx;
    matrix[1] = (2.0 * (x*y + z*w)) * sx;
    matrix[2] = (2.0 * (x*z - y*w)) * sx;
    matrix[4] = (2.0 * (x*y - z*w)) * sy;
    matrix[5] = (1.0 - 2.0 * (x*x + z*z)) * sy;
    matrix[6] = (2.0 * (y*z + x*w)) * sy;
    matrix[8] = (2.0 * (x*z + y*w)) * sz;
    matrix[9] = (2.0 * (y*z - x*w)) * sz;
    matrix[10] = (1.0 - 2.0 * (x*x + y*y)) * sz;
    matrix[12] = json_number(array_value(translation, 0), 0.0);
    matrix[13] = json_number(array_value(translation, 1), 0.0);
    matrix[14] = json_number(array_value(translation, 2), 0.0);
}
static int build_global_matrix(const struct glb_doc *doc, int node,
                               double *globals, unsigned char *ready)
{
    double local[16]; int parent;
    if (node < 0 || node >= doc->nodes.count) return -1;
    if (ready[node]) return 0;
    parent = doc->parents[node]; node_local_matrix(doc->nodes.items[node], local);
    if (parent >= 0) {
        if (build_global_matrix(doc, parent, globals, ready) < 0) return -1;
        matrix_multiply4(globals + parent * 16, local, globals + node * 16);
    } else memcpy(globals + node * 16, local, sizeof(local));
    ready[node] = 1; return 0;
}

static int bind_consistency(const struct glb_doc *doc)
{
    double *globals; unsigned char *ready; int skin_index, checked = 0;
    double maximum = 0.0;
    globals = tlibc_malloc(doc->nodes.count * 16 * sizeof(double));
    ready = tlibc_malloc(doc->nodes.count);
    if (!globals || !ready) return -1;
    __memset(ready, 0, doc->nodes.count);
    for (skin_index = 0; skin_index < doc->skins.count; skin_index++) {
        struct slice skin = doc->skins.items[skin_index];
        struct slice joints = object_value(skin, "joints"), item;
        int inverse_index = json_int(object_value(skin, "inverseBindMatrices"), -1);
        struct accessor_info inverse; const unsigned char *data; int stride, joint = 0;
        if (read_accessor_info(doc, inverse_index, &inverse) < 0 ||
            inverse.component != 5126 || inverse.components != 16) continue;
        data = accessor_data(doc, &inverse, &stride);
        while ((item = array_value(joints, joint)).p && joint < inverse.count) {
            double inverse_matrix[16], product[16]; int node = json_int(item, -1), i;
            if (build_global_matrix(doc, node, globals, ready) < 0) { tlibc_free(globals); tlibc_free(ready); return -1; }
            for (i = 0; i < 16; i++) inverse_matrix[i] = read_f32(data + joint * stride + i * 4);
            matrix_multiply4(globals + node * 16, inverse_matrix, product);
            for (i = 0; i < 16; i++) {
                double wanted = i == 0 || i == 5 || i == 10 || i == 15 ? 1.0 : 0.0;
                double error = product[i] - wanted; if (error < 0.0) error = -error;
                if (error > maximum) maximum = error;
            }
            checked++; joint++;
        }
    }
    __printf("bind_check: joints=%d max_abs_identity_error=%d.%09d status=%s\n",
             checked, (int)maximum, abs((int)(maximum * 1000000000.0)) % 1000000000,
             maximum < 0.0001 ? "consistent" : "MISMATCH");
    tlibc_free(globals); tlibc_free(ready); return maximum < 0.0001 ? 0 : 1;
}

static void glb_basis_point(const struct glb_doc *doc, int node,
                            double *globals, unsigned char *ready,
                            struct rasterfall_humanoid_point *point)
{
    if (node < 0 || node >= doc->nodes.count ||
        build_global_matrix(doc, node, globals, ready) < 0) return;
    point->value[0] = globals[node * 16 + 12];
    point->value[1] = globals[node * 16 + 13];
    point->value[2] = globals[node * 16 + 14];
    point->valid = 1;
}

static void print_basis_number(double value)
{
    int scaled = (int)(value * 1000000.0);
    __printf("%s%d.%06d", scaled < 0 ? "-" : "", abs(scaled)/1000000,
             abs(scaled)%1000000);
}

static int inspect_humanoid_bases(const struct glb_doc *doc)
{
    struct rasterfall_humanoid_basis_input input;
    struct rasterfall_humanoid_rest_basis bases[RASTERFALL_HUMANOID_BONE_COUNT];
    double *globals, error = 0.0; unsigned char *ready;
    int mapping[RASTERFALL_HUMANOID_BONE_COUNT], i, j; char name[128];
    __memset(&input, 0, sizeof(input)); map_quaternius(doc, mapping);
    globals=tlibc_malloc(doc->nodes.count*16*sizeof(double));ready=tlibc_malloc(doc->nodes.count);
    if(!globals||!ready)return -1;
    __memset(ready,0,doc->nodes.count);
    for(i=0;i<RASTERFALL_HUMANOID_BONE_COUNT;i++)glb_basis_point(doc,mapping[i],globals,ready,&input.bones[i]);
    glb_basis_point(doc,find_node_exact(doc,"ball_l","Ball_L"),globals,ready,&input.left_toe);
    glb_basis_point(doc,find_node_exact(doc,"ball_r","Ball_R"),globals,ready,&input.right_toe);
    glb_basis_point(doc,find_node_exact(doc,"middle_01_l","Middle1_L"),globals,ready,&input.left_middle);
    glb_basis_point(doc,find_node_exact(doc,"middle_01_r","Middle1_R"),globals,ready,&input.right_middle);
    glb_basis_point(doc,find_node_exact(doc,"thumb_01_l","Thumb1_L"),globals,ready,&input.left_thumb);
    glb_basis_point(doc,find_node_exact(doc,"thumb_01_r","Thumb1_R"),globals,ready,&input.right_thumb);
    input.model_up[1]=1.0;input.model_forward[2]=1.0;
    if(rasterfall_humanoid_build_rest_bases(&input,bases)<0)return -1;
    for(i=0;i<RASTERFALL_HUMANOID_BONE_COUNT;i++){
        node_name(doc,mapping[i],name,sizeof(name));__printf("%s -> %s / node %d primary=(",semantic_names[i],name,mapping[i]);
        for(j=0;j<3;j++){if(j)__printf(",");print_basis_number(bases[i].primary[j]);}
        __printf(") secondary=(");for(j=0;j<3;j++){if(j)__printf(",");print_basis_number(bases[i].secondary[j]);}
        __printf(") third=(");for(j=0;j<3;j++){if(j)__printf(",");print_basis_number(bases[i].third[j]);}
        __printf(") quaternion=(");for(j=0;j<4;j++){if(j)__printf(",");print_basis_number(bases[i].rotation[j]);}
        __printf(") source=\"%s\" confidence=%s%s\n",bases[i].source,
            rasterfall_humanoid_basis_confidence_name(bases[i].confidence),
            bases[i].confidence==RASTERFALL_HUMANOID_BASIS_LOW?" warning=fallback":"");
    }
    __printf("humanoid basis: valid=%s anatomy=%s max_error=",rasterfall_humanoid_validate_rest_bases(bases,&error)==0?"yes":"no",
             rasterfall_humanoid_validate_anatomy(bases)==0?"yes":"no");print_basis_number(error);__printf("\n");
    tlibc_free(globals);tlibc_free(ready);return 0;
}

static int wanted_fact_clip(const char *name)
{
    return !strcmp(name, "Idle_Loop") || !strcmp(name, "Walk_Loop") ||
           !strcmp(name, "Jog_Fwd_Loop");
}
static int inspect_facts(const struct glb_doc *doc)
{
    int animation_index, scale_channels = 0, scale_non_identity = 0;
    int scale_varying = 0; float maximum_identity = 0.0f, maximum_temporal = 0.0f;
    const float identity_scale[4] = {1,1,1,1};
    for (animation_index = 0; animation_index < doc->animations.count; animation_index++) {
        struct slice animation = doc->animations.items[animation_index];
        struct table samplers, channels; char animation_name[128]; int channel_index;
        if (table_build(object_value(animation, "samplers"), &samplers) < 0 ||
            table_build(object_value(animation, "channels"), &channels) < 0) return -1;
        string_copy(object_value(animation, "name"), animation_name, sizeof(animation_name));
        if (wanted_fact_clip(animation_name)) __printf("clip_changes: %s\n", animation_name);
        for (channel_index = 0; channel_index < channels.count; channel_index++) {
            struct slice channel = channels.items[channel_index];
            struct slice target = object_value(channel, "target");
            int sampler_index = json_int(object_value(channel, "sampler"), -1);
            int node = json_int(object_value(target, "node"), -1), output;
            char path[20], name[128]; float temporal, identity;
            if (sampler_index < 0 || sampler_index >= samplers.count) return -1;
            output = json_int(object_value(samplers.items[sampler_index], "output"), -1);
            string_copy(object_value(target, "path"), path, sizeof(path));
            temporal = accessor_max_delta(doc, output, 0, 0);
            if (!strcmp(path, "scale")) {
                identity = accessor_max_delta(doc, output, identity_scale, 1);
                scale_channels++; if (identity > 0.00001f) scale_non_identity++;
                if (temporal > 0.00001f) scale_varying++;
                if (identity > maximum_identity) maximum_identity = identity;
                if (temporal > maximum_temporal) maximum_temporal = temporal;
            }
            if (wanted_fact_clip(animation_name) && temporal > 0.00001f &&
                (!strcmp(path, "translation") || !strcmp(path, "rotation"))) {
                node_name(doc, node, name, sizeof(name));
                __printf("  %s %s max_component_delta=%d.%07d%s\n", path, name,
                         (int)temporal, abs((int)(temporal * 10000000.0f)) % 10000000,
                         !strcmp(name, "root") ? " [ROOT]" : !strcmp(name, "pelvis") ? " [PELVIS]" : "");
            }
        }
        table_free(&samplers); table_free(&channels);
    }
    __printf("scale_facts: channels=%d non_identity=%d temporally_varying=%d max_identity_delta=%d.%09d max_temporal_delta=%d.%09d threshold=0.00001\n",
             scale_channels, scale_non_identity, scale_varying,
             (int)maximum_identity, abs((int)(maximum_identity * 1000000000.0f)) % 1000000000,
             (int)maximum_temporal, abs((int)(maximum_temporal * 1000000000.0f)) % 1000000000);
    return bind_consistency(doc);
}

static void put_u32(unsigned char *p, unsigned int value)
{ p[0] = value; p[1] = value >> 8; p[2] = value >> 16; p[3] = value >> 24; }

static int self_test(void)
{
    static const char json[] = "{\"nodes\":[{\"name\":\"root\",\"children\":[1]},{\"name\":\"pelvis\"}],\"skins\":[{\"joints\":[0,1],\"skeleton\":0,\"inverseBindMatrices\":0}],\"animations\":[{\"name\":\"test\",\"samplers\":[{\"input\":1,\"output\":2}],\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}}]}],\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":2,\"type\":\"MAT4\"},{\"bufferView\":1,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\",\"max\":[1]},{\"bufferView\":2,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}],\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":128},{\"buffer\":0,\"byteOffset\":128,\"byteLength\":8},{\"buffer\":0,\"byteOffset\":136,\"byteLength\":24}],\"buffers\":[{\"byteLength\":160}]}";
    unsigned char fixture[20 + ((sizeof(json) + 3) & ~3) + 8 + 160];
    int json_bytes = ((int)sizeof(json) - 1 + 3) & ~3, size = sizeof(fixture), mapping[RASTERFALL_HUMANOID_BONE_COUNT];
    struct glb_doc doc; struct accessor_info accessor;
    __memset(fixture, 0, sizeof(fixture)); __memset(fixture + 20, ' ', json_bytes);
    put_u32(fixture, GLB_MAGIC); put_u32(fixture + 4, 2); put_u32(fixture + 8, size);
    put_u32(fixture + 12, json_bytes); put_u32(fixture + 16, GLB_JSON); memcpy(fixture + 20, json, sizeof(json) - 1);
    put_u32(fixture + 20 + json_bytes, 160); put_u32(fixture + 24 + json_bytes, GLB_BIN);
    if (doc_parse_memory(&doc, fixture, size, 0) < 0 || doc.nodes.count != 2 || doc.parents[1] != 0) return 1;
    map_quaternius(&doc, mapping);
    if (mapping[RASTERFALL_HUMANOID_ROOT] != 0 || mapping[RASTERFALL_HUMANOID_HIPS] != 1 || mapping[RASTERFALL_HUMANOID_HEAD] != -1) return 2;
    {
        struct slice channels = object_value(doc.animations.items[0], "channels");
        struct slice target = object_value(array_value(channels, 0), "target");
        char path[20]; int duplicates = 0, i, j;
        string_copy(object_value(target, "path"), path, sizeof(path));
        if (json_int(object_value(target, "node"), -1) != 1 || strcmp(path, "translation")) return 10;
        mapping[RASTERFALL_HUMANOID_HEAD] = mapping[RASTERFALL_HUMANOID_HIPS];
        for (i = 0; i < RASTERFALL_HUMANOID_BONE_COUNT; i++)
            for (j = 0; j < i; j++) if (mapping[i] >= 0 && mapping[i] == mapping[j]) { duplicates++; break; }
        if (duplicates != 1) return 11;
    }
    if (read_accessor_info(&doc, 2, &accessor) < 0 || accessor.count != 2 || accessor.components != 3) return 3;
    doc.views.items[2] = doc.views.items[2];
    doc_free(&doc);
    {
        char *children = strstr((char *)fixture + 20, "\"children\":[1]");
        if (!children) return 13;
        children[12] = '0';
        if (doc_parse_memory(&doc, fixture, size, 0) == 0) return 14;
        children[12] = '1';
    }
    fixture[0] = 0; if (doc_parse_memory(&doc, fixture, size, 0) == 0) return 4; fixture[0] = 'g';
    fixture[4] = 1; if (doc_parse_memory(&doc, fixture, size, 0) == 0) return 5; fixture[4] = 2;
    put_u32(fixture + 12, (unsigned int)size); if (doc_parse_memory(&doc, fixture, size, 0) == 0) return 6; put_u32(fixture + 12, json_bytes);
    /* Make the final bufferView exceed BIN while remaining valid JSON. */
    {
        const char *needle = "\"byteLength\":24"; char *at = strstr((char *)fixture + 20, needle);
        if (!at) return 7;
        at[13] = '9'; at[14] = '9';
        if (doc_parse_memory(&doc, fixture, size, 0) < 0) return 8;
        if (read_accessor_info(&doc, 2, &accessor) == 0) return 9;
    }
    __printf("glb-inspect: self-test passed\n");
    return 0;
}

struct glb_rotation_implementation {
    struct glb_doc doc;int animation,reference_valid,building_reference;
    double (*reference_local_nodes)[4];
    struct rasterfall_humanoid_rotation_skeleton reference_skeleton;
    struct rasterfall_humanoid_rotation_pose reference_pose;
    struct rasterfall_humanoid_rest_basis reference_basis[RASTERFALL_HUMANOID_BONE_COUNT];
};
static void slice_quaternion(struct slice node,double *q)
{
    struct slice r=object_value(node,"rotation");
    q[0]=json_number(array_value(r,0),0);q[1]=json_number(array_value(r,1),0);
    q[2]=json_number(array_value(r,2),0);q[3]=json_number(array_value(r,3),1);
}
static void glb_quat_multiply(const double *a,const double *b,double *o)
{
    double q[4];q[0]=a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1];q[1]=a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0];q[2]=a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3];q[3]=a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2];memcpy(o,q,sizeof(q));
}
static void glb_quat_normalize(double *q)
{
    double n=sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
    if(n<0.0000001){q[0]=q[1]=q[2]=0;q[3]=1;}else{q[0]/=n;q[1]/=n;q[2]/=n;q[3]/=n;}
}
static int glb_global_rotation(const struct glb_doc *doc,int node,double (*local)[4],double (*global)[4],unsigned char *ready)
{
    int parent;if(ready[node])return 0;parent=doc->parents[node];
    if(parent>=0){if(glb_global_rotation(doc,parent,local,global,ready)<0)return -1;glb_quat_multiply(global[parent],local[node],global[node]);}
    else memcpy(global[node],local[node],4*sizeof(double));
    glb_quat_normalize(global[node]);ready[node]=1;return 0;
}
static int sample_rotation_accessor(const struct glb_doc *doc,int input_index,int output_index,int time_ms,double *q)
{
    struct accessor_info ti,ro;const unsigned char *times,*rotations;int ts,rs,k=0,next;double seconds=time_ms/1000.0,factor,dot;
    if(read_accessor_info(doc,input_index,&ti)<0||read_accessor_info(doc,output_index,&ro)<0||ti.component!=5126||ti.components!=1||ro.component!=5126||ro.components!=4||ti.count!=ro.count||ti.count<1)return -1;
    times=accessor_data(doc,&ti,&ts);rotations=accessor_data(doc,&ro,&rs);
    while(k+1<ti.count&&read_f32(times+(k+1)*ts)<=seconds)k++;
    next=k+1<ti.count?k+1:k;
    factor=next==k?0:(seconds-read_f32(times+k*ts))/(read_f32(times+next*ts)-read_f32(times+k*ts));if(factor<0)factor=0;if(factor>1)factor=1;
    for(int i=0;i<4;i++){double a=read_f32(rotations+k*rs+i*4),b=read_f32(rotations+next*rs+i*4);q[i]=a+(b-a)*factor;}
    dot=0;for(int i=0;i<4;i++)dot+=read_f32(rotations+k*rs+i*4)*read_f32(rotations+next*rs+i*4);
    if(dot<0)for(int i=0;i<4;i++){double a=read_f32(rotations+k*rs+i*4),b=-read_f32(rotations+next*rs+i*4);q[i]=a+(b-a)*factor;}
    glb_quat_normalize(q);return 0;
}

int rasterfall_glb_rotation_clip_load(struct rasterfall_glb_rotation_clip *clip,const char *path,const char *name)
{
    struct glb_rotation_implementation *impl;struct table samplers,channels;int a;
    if(!clip||!path||!name)return -1;
    __memset(clip,0,sizeof(*clip));impl=tlibc_malloc(sizeof(*impl));if(!impl)return -1;__memset(impl,0,sizeof(*impl));
    if(doc_load(&impl->doc,path)<0){tlibc_free(impl);return -1;}
    impl->animation=-1;
    for(a=0;a<impl->doc.animations.count;a++){char n[128];string_copy(object_value(impl->doc.animations.items[a],"name"),n,sizeof(n));if(!strcmp(n,name)){impl->animation=a;break;}}
    if(impl->animation<0){doc_free(&impl->doc);tlibc_free(impl);return -1;}
    {
        struct slice animation=impl->doc.animations.items[a];int c;
        table_build(object_value(animation,"samplers"),&samplers);table_build(object_value(animation,"channels"),&channels);
        for(c=0;c<channels.count;c++){struct slice channel=channels.items[c],target=object_value(channel,"target");char path_name[20],interpolation[20];int si,input,output,keys;string_copy(object_value(target,"path"),path_name,sizeof(path_name));if(strcmp(path_name,"rotation"))continue;clip->rotation_channels++;si=json_int(object_value(channel,"sampler"),-1);if(si<0||si>=samplers.count)goto load_fail;string_copy(object_value(samplers.items[si],"interpolation"),interpolation,sizeof(interpolation));if(interpolation[0]&&strcmp(interpolation,"LINEAR"))goto load_fail;input=json_int(object_value(samplers.items[si],"input"),-1);output=json_int(object_value(samplers.items[si],"output"),-1);if(input<0||input>=impl->doc.accessors.count||output<0||output>=impl->doc.accessors.count)goto load_fail;keys=json_int(object_value(impl->doc.accessors.items[input],"count"),0);if(!clip->min_rotation_keys||keys<clip->min_rotation_keys)clip->min_rotation_keys=keys;if(keys>clip->max_rotation_keys)clip->max_rotation_keys=keys;if(accessor_max_delta(&impl->doc,output,0,0)>0.00001f)clip->active_rotation_bones++;{double end=json_number(array_value(object_value(impl->doc.accessors.items[input],"max"),0),0);if((int)(end*1000+.5)>clip->duration_ms)clip->duration_ms=(int)(end*1000+.5);}}
        table_free(&samplers);table_free(&channels);
    }
    impl->reference_local_nodes=tlibc_malloc(impl->doc.nodes.count*4*sizeof(double));
    if(!impl->reference_local_nodes)goto reference_fail;
    clip->implementation=impl;impl->building_reference=1;
    if(rasterfall_glb_rotation_clip_source(clip,0,&impl->reference_skeleton,
       &impl->reference_pose,impl->reference_basis,0)<0)goto reference_fail;
    impl->building_reference=0;impl->reference_valid=1;return 0;
reference_fail:
    clip->implementation=0;tlibc_free(impl->reference_local_nodes);doc_free(&impl->doc);tlibc_free(impl);return -1;
load_fail:
    table_free(&samplers);table_free(&channels);doc_free(&impl->doc);tlibc_free(impl);return -1;
}
void rasterfall_glb_rotation_clip_unload(struct rasterfall_glb_rotation_clip *clip)
{
    struct glb_rotation_implementation *impl=clip?(struct glb_rotation_implementation*)clip->implementation:0;if(!impl)return;tlibc_free(impl->reference_local_nodes);doc_free(&impl->doc);tlibc_free(impl);__memset(clip,0,sizeof(*clip));
}
int rasterfall_glb_rotation_clip_trace(const struct rasterfall_glb_rotation_clip *clip,int time_ms,struct rasterfall_humanoid_rotation_skeleton *skeleton,struct rasterfall_humanoid_rotation_pose *pose,struct rasterfall_humanoid_rest_basis *basis,struct rasterfall_glb_rotation_trace *trace,int *sampled_time_ms)
{
    struct glb_rotation_implementation *impl=clip?(struct glb_rotation_implementation*)clip->implementation:0;struct glb_doc *doc;double (*rest_local)[4],(*pose_local)[4],(*rest_global)[4],(*pose_global)[4],*matrices;unsigned char *rr,*pr,*mr;int mapping[RASTERFALL_HUMANOID_BONE_COUNT],i,c,t;
    struct rasterfall_humanoid_basis_input input;struct table samplers,channels;
    if(!impl||!skeleton||!pose||!basis)return -1;
    doc=&impl->doc;t=clip->duration_ms>0?time_ms%clip->duration_ms:0;if(t<0)t+=clip->duration_ms;if(sampled_time_ms)*sampled_time_ms=t;
    rest_local=tlibc_malloc(doc->nodes.count*4*sizeof(double));pose_local=tlibc_malloc(doc->nodes.count*4*sizeof(double));rest_global=tlibc_malloc(doc->nodes.count*4*sizeof(double));pose_global=tlibc_malloc(doc->nodes.count*4*sizeof(double));rr=tlibc_malloc(doc->nodes.count);pr=tlibc_malloc(doc->nodes.count);matrices=tlibc_malloc(doc->nodes.count*16*sizeof(double));mr=tlibc_malloc(doc->nodes.count);
    if(!rest_local||!pose_local||!rest_global||!pose_global||!rr||!pr||!matrices||!mr)return -1;
    for(i=0;i<doc->nodes.count;i++){slice_quaternion(doc->nodes.items[i],rest_local[i]);memcpy(pose_local[i],rest_local[i],4*sizeof(double));}__memset(rr,0,doc->nodes.count);__memset(pr,0,doc->nodes.count);
    table_build(object_value(doc->animations.items[impl->animation],"samplers"),&samplers);table_build(object_value(doc->animations.items[impl->animation],"channels"),&channels);
    for(c=0;c<channels.count;c++){struct slice channel=channels.items[c],target=object_value(channel,"target");char path[20];int si,node,input_index,output_index;string_copy(object_value(target,"path"),path,sizeof(path));if(strcmp(path,"rotation"))continue;si=json_int(object_value(channel,"sampler"),-1);node=json_int(object_value(target,"node"),-1);input_index=json_int(object_value(samplers.items[si],"input"),-1);output_index=json_int(object_value(samplers.items[si],"output"),-1);if(sample_rotation_accessor(doc,input_index,output_index,t,pose_local[node])<0)return -1;}
    if(impl->building_reference)memcpy(impl->reference_local_nodes,pose_local,doc->nodes.count*4*sizeof(double));
    for(i=0;i<doc->nodes.count;i++){glb_global_rotation(doc,i,rest_local,rest_global,rr);glb_global_rotation(doc,i,pose_local,pose_global,pr);}map_quaternius(doc,mapping);rasterfall_humanoid_rotation_skeleton_identity(skeleton);
    for(i=0;i<RASTERFALL_HUMANOID_BONE_COUNT;i++){memcpy(skeleton->rest_global[i],rest_global[mapping[i]],4*sizeof(double));memcpy(pose->global[i],pose_global[mapping[i]],4*sizeof(double));if(trace){memcpy(trace->rest_local[i],rest_local[mapping[i]],4*sizeof(double));memcpy(trace->animated_local[i],pose_local[mapping[i]],4*sizeof(double));memcpy(trace->rest_global[i],rest_global[mapping[i]],4*sizeof(double));memcpy(trace->animated_global[i],pose_global[mapping[i]],4*sizeof(double));}}
    __memset(&input,0,sizeof(input));__memset(mr,0,doc->nodes.count);for(i=0;i<RASTERFALL_HUMANOID_BONE_COUNT;i++)glb_basis_point(doc,mapping[i],matrices,mr,&input.bones[i]);glb_basis_point(doc,find_node_exact(doc,"ball_l","Ball_L"),matrices,mr,&input.left_toe);glb_basis_point(doc,find_node_exact(doc,"ball_r","Ball_R"),matrices,mr,&input.right_toe);glb_basis_point(doc,find_node_exact(doc,"middle_01_l","Middle1_L"),matrices,mr,&input.left_middle);glb_basis_point(doc,find_node_exact(doc,"middle_01_r","Middle1_R"),matrices,mr,&input.right_middle);glb_basis_point(doc,find_node_exact(doc,"thumb_01_l","Thumb1_L"),matrices,mr,&input.left_thumb);glb_basis_point(doc,find_node_exact(doc,"thumb_01_r","Thumb1_R"),matrices,mr,&input.right_thumb);input.model_up[1]=1;input.model_forward[2]=1;rasterfall_humanoid_build_rest_bases(&input,basis);
    table_free(&samplers);table_free(&channels);tlibc_free(rest_local);tlibc_free(pose_local);tlibc_free(rest_global);tlibc_free(pose_global);tlibc_free(rr);tlibc_free(pr);tlibc_free(matrices);tlibc_free(mr);return 0;
}

int rasterfall_glb_rotation_clip_source(const struct rasterfall_glb_rotation_clip *clip,int time_ms,struct rasterfall_humanoid_rotation_skeleton *skeleton,struct rasterfall_humanoid_rotation_pose *pose,struct rasterfall_humanoid_rest_basis *basis,int *sampled_time_ms)
{
    return rasterfall_glb_rotation_clip_trace(clip,time_ms,skeleton,pose,basis,0,sampled_time_ms);
}
int rasterfall_glb_rotation_clip_reference(const struct rasterfall_glb_rotation_clip *clip,struct rasterfall_humanoid_rotation_skeleton *skeleton,struct rasterfall_humanoid_rotation_pose *pose,struct rasterfall_humanoid_rest_basis *basis)
{
    struct glb_rotation_implementation *impl=clip?(struct glb_rotation_implementation*)clip->implementation:0;
    if(!impl||!impl->reference_valid||!skeleton||!pose||!basis)return -1;
    memcpy(skeleton,&impl->reference_skeleton,sizeof(*skeleton));memcpy(pose,&impl->reference_pose,sizeof(*pose));memcpy(basis,impl->reference_basis,sizeof(impl->reference_basis));return 0;
}

struct glb_preview_implementation {
    struct glb_doc doc;
    int animation, joint_count, mesh_node;
    int *joint_nodes;
    double *inverse_bind;
    double *base_t, *base_r, *base_s;
    unsigned short *vertex_joints;
    double *vertex_weights, *bind_positions, *bind_normals;
};

static double preview_component(const unsigned char *p,int component,int normalized)
{
    if(component==5126)return read_f32(p);
    if(component==5121)return normalized?p[0]/255.0:p[0];
    if(component==5123){unsigned int v=p[0]|p[1]<<8;return normalized?v/65535.0:v;}
    if(component==5120){signed char v=*(const signed char*)p;return normalized?(v<-127?-1.0:v/127.0):v;}
    if(component==5122){short v=(short)(p[0]|p[1]<<8);return normalized?(v<-32767?-1.0:v/32767.0):v;}
    if(component==5125)return read_u32(p);
    return 0;
}
static int preview_accessor_values(const struct glb_doc *doc,int index,int wanted,
                                   double *out)
{
    struct accessor_info a;const unsigned char *data;int stride,i,c,bytes,normalized=0;
    struct slice flag;
    if(read_accessor_info(doc,index,&a)<0||a.components!=wanted)return -1;
    flag=object_value(doc->accessors.items[index],"normalized");
    if(flag.p&&flag.end-flag.p>=4&&!memcmp(skip_ws(flag.p,flag.end),"true",4))normalized=1;
    data=accessor_data(doc,&a,&stride);bytes=component_bytes(a.component);
    for(i=0;i<a.count;i++)for(c=0;c<wanted;c++)out[i*wanted+c]=preview_component(data+i*stride+c*bytes,a.component,normalized);
    return a.count;
}
static void preview_node_defaults(struct glb_preview_implementation *impl)
{
    int i;for(i=0;i<impl->doc.nodes.count;i++){
        struct slice n=impl->doc.nodes.items[i],t=object_value(n,"translation"),r=object_value(n,"rotation"),s=object_value(n,"scale");
        impl->base_t[i*3]=json_number(array_value(t,0),0);impl->base_t[i*3+1]=json_number(array_value(t,1),0);impl->base_t[i*3+2]=json_number(array_value(t,2),0);
        impl->base_r[i*4]=json_number(array_value(r,0),0);impl->base_r[i*4+1]=json_number(array_value(r,1),0);impl->base_r[i*4+2]=json_number(array_value(r,2),0);impl->base_r[i*4+3]=json_number(array_value(r,3),1);
        impl->base_s[i*3]=json_number(array_value(s,0),1);impl->base_s[i*3+1]=json_number(array_value(s,1),1);impl->base_s[i*3+2]=json_number(array_value(s,2),1);
    }
}
static void preview_trs_matrix(const double *t,const double *r,const double *s,double *m)
{
    double x=r[0],y=r[1],z=r[2],w=r[3];matrix_identity(m);
    m[0]=(1-2*(y*y+z*z))*s[0];m[1]=(2*(x*y+z*w))*s[0];m[2]=(2*(x*z-y*w))*s[0];
    m[4]=(2*(x*y-z*w))*s[1];m[5]=(1-2*(x*x+z*z))*s[1];m[6]=(2*(y*z+x*w))*s[1];
    m[8]=(2*(x*z+y*w))*s[2];m[9]=(2*(y*z-x*w))*s[2];m[10]=(1-2*(x*x+y*y))*s[2];
    m[12]=t[0];m[13]=t[1];m[14]=t[2];
}
static int preview_global(int node,const struct glb_doc *doc,const double *t,
                          const double *r,const double *s,double *global,
                          unsigned char *ready)
{
    double local[16];int parent;if(ready[node])return 0;parent=doc->parents[node];
    preview_trs_matrix(t+node*3,r+node*4,s+node*3,local);
    if(parent>=0){if(preview_global(parent,doc,t,r,s,global,ready)<0)return -1;matrix_multiply4(global+parent*16,local,global+node*16);}else memcpy(global+node*16,local,sizeof(local));
    ready[node]=1;return 0;
}
static int preview_sample_vector(const struct glb_doc *doc,int input_index,
                                 int output_index,int time_ms,int components,
                                 double *out)
{
    struct accessor_info ti,vo;const unsigned char *times,*values;int ts,vs,k=0,next,c;double seconds=time_ms/1000.0,factor;
    if(read_accessor_info(doc,input_index,&ti)<0||read_accessor_info(doc,output_index,&vo)<0||ti.component!=5126||ti.components!=1||vo.component!=5126||vo.components!=components||ti.count!=vo.count||ti.count<1)return -1;
    times=accessor_data(doc,&ti,&ts);values=accessor_data(doc,&vo,&vs);while(k+1<ti.count&&read_f32(times+(k+1)*ts)<=seconds)k++;next=k+1<ti.count?k+1:k;
    factor=next==k?0:(seconds-read_f32(times+k*ts))/(read_f32(times+next*ts)-read_f32(times+k*ts));if(factor<0)factor=0;if(factor>1)factor=1;
    for(c=0;c<components;c++){double a=read_f32(values+k*vs+c*4),b=read_f32(values+next*vs+c*4);out[c]=a+(b-a)*factor;}
    if(components==4){double dot=0;for(c=0;c<4;c++)dot+=read_f32(values+k*vs+c*4)*read_f32(values+next*vs+c*4);if(dot<0)for(c=0;c<4;c++){double a=read_f32(values+k*vs+c*4),b=-read_f32(values+next*vs+c*4);out[c]=a+(b-a)*factor;}glb_quat_normalize(out);}
    return 0;
}

int rasterfall_glb_preview_load(struct rasterfall_glb_preview *preview,const char *path)
{
    struct glb_preview_implementation *impl;struct slice primitive,attributes,joints,item;struct accessor_info a;double *temp;int mesh=0,position,normal,ji,weight,index,inverse,i,c,stride;const unsigned char *data;
    if(!preview||!path)return -1;
    __memset(preview,0,sizeof(*preview));impl=tlibc_malloc(sizeof(*impl));if(!impl)return -1;__memset(impl,0,sizeof(*impl));
    if(doc_load(&impl->doc,path)<0||impl->doc.meshes.count<1||impl->doc.skins.count<1)goto fail;
    primitive=array_value(object_value(impl->doc.meshes.items[mesh],"primitives"),0);attributes=object_value(primitive,"attributes");
    position=json_int(object_value(attributes,"POSITION"),-1);normal=json_int(object_value(attributes,"NORMAL"),-1);ji=json_int(object_value(attributes,"JOINTS_0"),-1);weight=json_int(object_value(attributes,"WEIGHTS_0"),-1);index=json_int(object_value(primitive,"indices"),-1);
    if(read_accessor_info(&impl->doc,position,&a)<0||a.components!=3||a.count<1)goto fail;
    preview->vertex_count=a.count;
    impl->bind_positions=tlibc_malloc(a.count*3*sizeof(double));impl->bind_normals=tlibc_malloc(a.count*3*sizeof(double));impl->vertex_joints=tlibc_malloc(a.count*4*sizeof(unsigned short));impl->vertex_weights=tlibc_malloc(a.count*4*sizeof(double));preview->positions=tlibc_malloc(a.count*3*sizeof(double));preview->normals=tlibc_malloc(a.count*3*sizeof(double));
    if(!impl->bind_positions||!impl->bind_normals||!impl->vertex_joints||!impl->vertex_weights||!preview->positions||!preview->normals)goto fail;
    if(preview_accessor_values(&impl->doc,position,3,impl->bind_positions)!=a.count||preview_accessor_values(&impl->doc,normal,3,impl->bind_normals)!=a.count)goto fail;
    temp=tlibc_malloc(a.count*4*sizeof(double));if(!temp)goto fail;if(preview_accessor_values(&impl->doc,ji,4,temp)!=a.count)goto fail;for(i=0;i<a.count*4;i++)impl->vertex_joints[i]=(unsigned short)temp[i];if(preview_accessor_values(&impl->doc,weight,4,impl->vertex_weights)!=a.count)goto fail;tlibc_free(temp);
    if(read_accessor_info(&impl->doc,index,&a)<0||a.components!=1)goto fail;
    preview->index_count=a.count;preview->indices=tlibc_malloc(a.count*sizeof(unsigned int));if(!preview->indices)goto fail;data=accessor_data(&impl->doc,&a,&stride);for(i=0;i<a.count;i++)preview->indices[i]=(unsigned int)preview_component(data+i*stride,a.component,0);
    joints=object_value(impl->doc.skins.items[0],"joints");while((item=array_value(joints,impl->joint_count)).p)impl->joint_count++;impl->joint_nodes=tlibc_malloc(impl->joint_count*sizeof(int));impl->inverse_bind=tlibc_malloc(impl->joint_count*16*sizeof(double));if(!impl->joint_nodes||!impl->inverse_bind)goto fail;
    for(i=0;i<impl->joint_count;i++)impl->joint_nodes[i]=json_int(array_value(joints,i),-1);
    inverse=json_int(object_value(impl->doc.skins.items[0],"inverseBindMatrices"),-1);if(preview_accessor_values(&impl->doc,inverse,16,impl->inverse_bind)!=impl->joint_count)goto fail;
    for(i=0;i<impl->doc.nodes.count;i++)if(json_int(object_value(impl->doc.nodes.items[i],"mesh"),-1)==mesh){impl->mesh_node=i;break;}
    c=impl->doc.nodes.count;impl->animation=-1;impl->base_t=tlibc_malloc(c*3*sizeof(double));impl->base_r=tlibc_malloc(c*4*sizeof(double));impl->base_s=tlibc_malloc(c*3*sizeof(double));if(!impl->base_t||!impl->base_r||!impl->base_s)goto fail;preview_node_defaults(impl);preview->implementation=impl;return rasterfall_glb_preview_sample(preview,0);
fail:
    if(impl){doc_free(&impl->doc);tlibc_free(impl->joint_nodes);tlibc_free(impl->inverse_bind);tlibc_free(impl->base_t);tlibc_free(impl->base_r);tlibc_free(impl->base_s);tlibc_free(impl->vertex_joints);tlibc_free(impl->vertex_weights);tlibc_free(impl->bind_positions);tlibc_free(impl->bind_normals);tlibc_free(impl);}tlibc_free(preview->positions);tlibc_free(preview->normals);tlibc_free(preview->indices);__memset(preview,0,sizeof(*preview));return -1;
}
void rasterfall_glb_preview_unload(struct rasterfall_glb_preview *preview)
{
    struct glb_preview_implementation *impl=preview?(struct glb_preview_implementation*)preview->implementation:0;if(!impl)return;doc_free(&impl->doc);tlibc_free(impl->joint_nodes);tlibc_free(impl->inverse_bind);tlibc_free(impl->base_t);tlibc_free(impl->base_r);tlibc_free(impl->base_s);tlibc_free(impl->vertex_joints);tlibc_free(impl->vertex_weights);tlibc_free(impl->bind_positions);tlibc_free(impl->bind_normals);tlibc_free(impl);tlibc_free(preview->positions);tlibc_free(preview->normals);tlibc_free(preview->indices);__memset(preview,0,sizeof(*preview));
}
int rasterfall_glb_preview_select_animation(struct rasterfall_glb_preview *preview,const char *name)
{
    struct glb_preview_implementation *impl=preview?
        (struct glb_preview_implementation*)preview->implementation:0;
    int a;if(!impl||!name)return -1;impl->animation=-1;preview->duration_ms=0;
    for(a=0;a<impl->doc.animations.count;a++){
        char n[128];string_copy(object_value(impl->doc.animations.items[a],"name"),n,sizeof(n));
        if(!strcmp(n,name)){
            struct table samplers;int s;impl->animation=a;
            table_build(object_value(impl->doc.animations.items[a],"samplers"),&samplers);
            for(s=0;s<samplers.count;s++){
                int input=json_int(object_value(samplers.items[s],"input"),-1);
                double end=json_number(array_value(object_value(impl->doc.accessors.items[input],"max"),0),0);
                if((int)(end*1000+.5)>preview->duration_ms)preview->duration_ms=(int)(end*1000+.5);
            }
            table_free(&samplers);break;
        }
    }
    return impl->animation>=0?0:-1;
}
int rasterfall_glb_preview_sample(struct rasterfall_glb_preview *preview,int time_ms)
{
    struct glb_preview_implementation *impl=preview?(struct glb_preview_implementation*)preview->implementation:0;double *t,*r,*s,*global,*skin;unsigned char *ready;int nodes,i,c,time=time_ms;if(!impl)return -1;nodes=impl->doc.nodes.count;t=tlibc_malloc(nodes*3*sizeof(double));r=tlibc_malloc(nodes*4*sizeof(double));s=tlibc_malloc(nodes*3*sizeof(double));global=tlibc_malloc(nodes*16*sizeof(double));skin=tlibc_malloc(impl->joint_count*16*sizeof(double));ready=tlibc_malloc(nodes);if(!t||!r||!s||!global||!skin||!ready)return -1;memcpy(t,impl->base_t,nodes*3*sizeof(double));memcpy(r,impl->base_r,nodes*4*sizeof(double));memcpy(s,impl->base_s,nodes*3*sizeof(double));if(preview->duration_ms>0){time%=preview->duration_ms;if(time<0)time+=preview->duration_ms;}
    if(impl->animation>=0){struct table samplers,channels;table_build(object_value(impl->doc.animations.items[impl->animation],"samplers"),&samplers);table_build(object_value(impl->doc.animations.items[impl->animation],"channels"),&channels);for(c=0;c<channels.count;c++){struct slice channel=channels.items[c],target=object_value(channel,"target");char path[20];int si=json_int(object_value(channel,"sampler"),-1),node=json_int(object_value(target,"node"),-1),input,output;if(si<0||si>=samplers.count||node<0||node>=nodes)continue;input=json_int(object_value(samplers.items[si],"input"),-1);output=json_int(object_value(samplers.items[si],"output"),-1);string_copy(object_value(target,"path"),path,sizeof(path));if(!strcmp(path,"translation"))preview_sample_vector(&impl->doc,input,output,time,3,t+node*3);else if(!strcmp(path,"rotation"))preview_sample_vector(&impl->doc,input,output,time,4,r+node*4);else if(!strcmp(path,"scale"))preview_sample_vector(&impl->doc,input,output,time,3,s+node*3);}table_free(&samplers);table_free(&channels);}
    __memset(ready,0,nodes);for(i=0;i<impl->joint_count;i++){int node=impl->joint_nodes[i];if(preview_global(node,&impl->doc,t,r,s,global,ready)<0)goto fail;matrix_multiply4(global+node*16,impl->inverse_bind+i*16,skin+i*16);}
    for(i=0;i<preview->vertex_count;i++){double p[3]={0,0,0},n[3]={0,0,0},sum=0;for(c=0;c<4;c++){int joint=impl->vertex_joints[i*4+c];double w=impl->vertex_weights[i*4+c],*m;if(joint<0||joint>=impl->joint_count||w==0)continue;m=skin+joint*16;p[0]+=w*(m[0]*impl->bind_positions[i*3]+m[4]*impl->bind_positions[i*3+1]+m[8]*impl->bind_positions[i*3+2]+m[12]);p[1]+=w*(m[1]*impl->bind_positions[i*3]+m[5]*impl->bind_positions[i*3+1]+m[9]*impl->bind_positions[i*3+2]+m[13]);p[2]+=w*(m[2]*impl->bind_positions[i*3]+m[6]*impl->bind_positions[i*3+1]+m[10]*impl->bind_positions[i*3+2]+m[14]);n[0]+=w*(m[0]*impl->bind_normals[i*3]+m[4]*impl->bind_normals[i*3+1]+m[8]*impl->bind_normals[i*3+2]);n[1]+=w*(m[1]*impl->bind_normals[i*3]+m[5]*impl->bind_normals[i*3+1]+m[9]*impl->bind_normals[i*3+2]);n[2]+=w*(m[2]*impl->bind_normals[i*3]+m[6]*impl->bind_normals[i*3+1]+m[10]*impl->bind_normals[i*3+2]);sum+=w;}if(sum<=0){memcpy(p,impl->bind_positions+i*3,3*sizeof(double));memcpy(n,impl->bind_normals+i*3,3*sizeof(double));}memcpy(preview->positions+i*3,p,3*sizeof(double));{double length=sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);if(length>0){n[0]/=length;n[1]/=length;n[2]/=length;}}memcpy(preview->normals+i*3,n,3*sizeof(double));if(i==0)for(c=0;c<3;c++)preview->min[c]=preview->max[c]=p[c];else for(c=0;c<3;c++){if(p[c]<preview->min[c])preview->min[c]=p[c];if(p[c]>preview->max[c])preview->max[c]=p[c];}}
    tlibc_free(t);tlibc_free(r);tlibc_free(s);tlibc_free(global);tlibc_free(skin);tlibc_free(ready);return 0;
fail:tlibc_free(t);tlibc_free(r);tlibc_free(s);tlibc_free(global);tlibc_free(skin);tlibc_free(ready);return -1;
}

#ifndef RASTERFALL_GLB_LIBRARY
int main(int argc, char **argv)
{
    struct glb_doc doc; int humanoid_only = 0, facts = 0, basis = 0, result;
    if (argc == 2 && !strcmp(argv[1], "--self-test")) return self_test();
    if (argc < 2 || argc > 3 || (argc == 3 && strcmp(argv[2], "humanoid") && strcmp(argv[2], "facts") && strcmp(argv[2], "basis") && strcmp(argv[2], "preview"))) {
        __printf("usage: glb-inspect file.glb [humanoid|basis|facts|preview]\n       glb-inspect --self-test\n"); return 2;
    }
    if(argc==3&&!strcmp(argv[2],"preview")){
        static const char *clips[3]={"Idle_Loop","Walk_Loop","Jog_Fwd_Loop"};struct rasterfall_glb_preview preview;int clip;
        if(rasterfall_glb_preview_load(&preview,argv[1])<0){__fprintf(2,"glb preview: load failed\n");return 1;}
        __printf("glb preview: vertices=%d indices=%d\n",preview.vertex_count,preview.index_count);
        for(clip=0;clip<3;clip++)if(rasterfall_glb_preview_select_animation(&preview,clips[clip])<0||rasterfall_glb_preview_sample(&preview,preview.duration_ms/2)<0){rasterfall_glb_preview_unload(&preview);return 1;}else __printf("  %s duration_ms=%d bounds=(%d,%d,%d)-(%d,%d,%d)\n",clips[clip],preview.duration_ms,(int)(preview.min[0]*1000),(int)(preview.min[1]*1000),(int)(preview.min[2]*1000),(int)(preview.max[0]*1000),(int)(preview.max[1]*1000),(int)(preview.max[2]*1000));
        rasterfall_glb_preview_unload(&preview);return 0;
    }
    humanoid_only = argc == 3 && !strcmp(argv[2], "humanoid");
    facts = argc == 3 && !strcmp(argv[2], "facts");
    basis = argc == 3 && !strcmp(argv[2], "basis");
    if (doc_load(&doc, argv[1]) < 0) { __fprintf(2, "glb-inspect: invalid or unsupported GLB: %s\n", argv[1]); return 1; }
    result = facts ? inspect_facts(&doc) : basis ? inspect_humanoid_bases(&doc) : inspect_doc(&doc, humanoid_only);
    doc_free(&doc);
    return result < 0 ? 1 : 0;
}
#endif
