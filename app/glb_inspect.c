/* Minimal offline glTF 2.0 skeleton and animation inspector.  This tool does
 * not import meshes and deliberately keeps glTF concepts out of Rasterfall's
 * renderer and runtime animation path. */
#include "core.h"
#include "tlibc_print.h"
#include "tlibc_everything.h"
#include "rasterfall_humanoid.h"

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
    struct table nodes, skins, animations, accessors, views, buffers;
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
        table_build(object_value(doc->root, "buffers"), &doc->buffers) < 0) return -1;
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

int main(int argc, char **argv)
{
    struct glb_doc doc; int humanoid_only = 0, result;
    if (argc == 2 && !strcmp(argv[1], "--self-test")) return self_test();
    if (argc < 2 || argc > 3 || (argc == 3 && strcmp(argv[2], "humanoid"))) {
        __printf("usage: glb-inspect file.glb [humanoid]\n       glb-inspect --self-test\n"); return 2;
    }
    humanoid_only = argc == 3;
    if (doc_load(&doc, argv[1]) < 0) { __fprintf(2, "glb-inspect: invalid or unsupported GLB: %s\n", argv[1]); return 1; }
    result = inspect_doc(&doc, humanoid_only);
    doc_free(&doc);
    return result < 0 ? 1 : 0;
}
