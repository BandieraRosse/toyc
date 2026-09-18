/* Extract the render and first-stage skeletal data needed by Rasterfall from
 * a PMX 2.0/2.1 file.
 *
 * This is intentionally an offline converter.  BDEF1/BDEF2 and the rest bone
 * hierarchy are retained; morphs, rigid bodies and joints are not. Base and
 * sphere texture references are retained in the existing RFM2 material
 * records; source texture files are copied to a caller-selected directory.
 */
#include "core.h"
#include "tlibc_print.h"
#include "tlibc_everything.h"
#include "rasterfall_model.h"

#define MAX_MATERIALS 128
#define MAX_TEXTURES 256
#define MAX_VERTICES 1000000
#define MAX_INDICES 3000000
#define TEXTURE_PATH_MAX 512
#define MATERIAL_NAME_MAX 128
#define BONE_NAME_MAX 128

struct cursor { const unsigned char *p, *end; };
struct pmx_header { int vertex_size, texture_size, material_size; int bone_size, morph_size, rigid_size; int encoding, append_uv; };
struct pmx_texture { char path[TEXTURE_PATH_MAX]; };
struct pmx_material { char name[MATERIAL_NAME_MAX], name_en[MATERIAL_NAME_MAX]; unsigned int color; unsigned int index_count; int texture_index; int sphere_index; int sphere_mode; int alpha; int toon_index; int toon_shared; int draw_flags; unsigned int edge_color; int edge_size; unsigned int ambient_color; unsigned int specular_color; int specular_power; int visual_role; };

static int ascii_contains(const char *text, const char *needle)
{
    int i, j;
    for (i = 0; text[i]; i++) {
        for (j = 0; needle[j]; j++) {
            int a = text[i + j], b = needle[j];
            if (!a) return 0;
            if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
            if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
            if (a != b) break;
        }
        if (!needle[j]) return 1;
    }
    return 0;
}

static int material_visual_role(const char *name)
{
    if (ascii_contains(name, "hair")) return RASTERFALL_MODEL_MATERIAL_ROLE_HAIR;
    if (ascii_contains(name, "eyes") || ascii_contains(name, "eyeiris") ||
        ascii_contains(name, "eyewhite") ||
        ascii_contains(name, "eyehighlight")) return RASTERFALL_MODEL_MATERIAL_ROLE_EYES;
    if (ascii_contains(name, "face") && ascii_contains(name, "skin"))
        return RASTERFALL_MODEL_MATERIAL_ROLE_FACE;
    if (ascii_contains(name, "face") || ascii_contains(name, "mouth") ||
        ascii_contains(name, "brow") || ascii_contains(name, "eyeline"))
        return RASTERFALL_MODEL_MATERIAL_ROLE_FACE;
    if (ascii_contains(name, "skin") || ascii_contains(name, "body"))
        return RASTERFALL_MODEL_MATERIAL_ROLE_SKIN;
    if (ascii_contains(name, "cloth") || ascii_contains(name, "tops") ||
        ascii_contains(name, "shoes")) return RASTERFALL_MODEL_MATERIAL_ROLE_CLOTHING;
    if (ascii_contains(name, "equipment"))
        return RASTERFALL_MODEL_MATERIAL_ROLE_EQUIPMENT;
    return RASTERFALL_MODEL_MATERIAL_ROLE_NONE;
}
struct pmx_vertex { float x, y, z, nx, ny, nz, u, v, au, av, edge_scale, weight; unsigned int weight_type; int bone0, bone1; };
struct pmx_ik_link { int bone, limited; float lower[3], upper[3]; };
struct pmx_bone {
    char name[BONE_NAME_MAX]; int parent; float x, y, z; unsigned int flags; int depth;
    int tail_index, append_parent; float tail[3], append_ratio;
    float fixed_axis[3], local_x[3], local_z[3];
    int ik_target, ik_iterations, ik_link_count;
    float ik_angle;
    struct pmx_ik_link *ik_links;
};
struct pmx_diagnostics { unsigned int weights[5]; int bone_count, root_count, max_depth; unsigned int advanced_flags; };

static int have(struct cursor *c, unsigned int n)
{ return c->p <= c->end && n <= (unsigned int)(c->end - c->p); }

static int bytes(struct cursor *c, void *out, unsigned int n)
{
    if (!have(c, n)) return -1;
    if (out) memcpy(out, c->p, n);
    c->p += n;
    return 0;
}

static int u8(struct cursor *c, unsigned int *out)
{
    unsigned char v;
    if (bytes(c, &v, 1) < 0) return -1;
    if (out) *out = v;
    return 0;
}

static int u32(struct cursor *c, unsigned int *out)
{
    unsigned char b[4];
    if (bytes(c, b, 4) < 0) return -1;
    if (out) *out = (unsigned int)b[0] | (unsigned int)b[1] << 8 |
        (unsigned int)b[2] << 16 | (unsigned int)b[3] << 24;
    return 0;
}

static int u16(struct cursor *c, unsigned int *out)
{
    unsigned char b[2];
    if (bytes(c, b, 2) < 0) return -1;
    if (out) *out = (unsigned int)b[0] | (unsigned int)b[1] << 8;
    return 0;
}

static int i32(struct cursor *c, int *out)
{
    unsigned int v;
    if (u32(c, &v) < 0) return -1;
    if (out) *out = (int)v;
    return 0;
}

static int f32(struct cursor *c, float *out)
{
    unsigned int v;
    union { unsigned int u; float f; } bits;
    if (u32(c, &v) < 0) return -1;
    bits.u = v;
    if (out) *out = bits.f;
    return 0;
}

static int index_value(struct cursor *c, int size, int signed_value, int *out)
{
    unsigned int v = 0;
    unsigned char b;
    unsigned short s;
    if (size == 1) {
        if (u8(c, &v) < 0) return -1;
    } else if (size == 2) {
        if (bytes(c, &s, 2) < 0) return -1;
        v = (unsigned int)s;
    } else if (size == 4) {
        if (u32(c, &v) < 0) return -1;
    } else return -1;
    if (signed_value) {
        if (size == 1) { b = (unsigned char)v; *out = b == 255 ? -1 : (int)b; }
        else if (size == 2) { s = (unsigned short)v; *out = s == 65535 ? -1 : (int)s; }
        else *out = v == 0xffffffffU ? -1 : (int)v;
    } else if (out) *out = (int)v;
    return 0;
}

static int text(struct cursor *c)
{
    int length;
    if (i32(c, &length) < 0 || length < 0 || length > 16 * 1024 * 1024 ||
        !have(c, (unsigned int)length)) return -1;
    c->p += length;
    return 0;
}

static int utf8_put(char *out, int max, int *at, unsigned int code)
{
    if (code < 0x80) {
        if (*at + 1 >= max) return -1;
        out[(*at)++] = (char)code;
    } else if (code < 0x800) {
        if (*at + 2 >= max) return -1;
        out[(*at)++] = (char)(0xC0 | (code >> 6));
        out[(*at)++] = (char)(0x80 | (code & 63));
    } else {
        if (*at + 3 >= max) return -1;
        out[(*at)++] = (char)(0xE0 | (code >> 12));
        out[(*at)++] = (char)(0x80 | ((code >> 6) & 63));
        out[(*at)++] = (char)(0x80 | (code & 63));
    }
    return 0;
}

static int read_text(struct cursor *c, int encoding, char *out, int max)
{
    int length, at = 0, i;
    if (i32(c, &length) < 0 || length < 0 || length > 16 * 1024 * 1024 ||
        !have(c, (unsigned int)length)) return -1;
    if (!out || max < 1) { c->p += length; return 0; }
    if (encoding == 1) {
        for (i = 0; i < length; i++) {
            if (at + 1 >= max) return -1;
            out[at++] = c->p[i] == '\\' ? '/' : (char)c->p[i];
        }
    } else {
        if (length & 1) return -1;
        for (i = 0; i < length; i += 2) {
            unsigned int code = c->p[i] | (unsigned int)c->p[i + 1] << 8;
            if (code == '\\') code = '/';
            if (code >= 0xD800 && code <= 0xDBFF && i + 3 < length) {
                unsigned int low = c->p[i + 2] | (unsigned int)c->p[i + 3] << 8;
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    code = 0x10000 + ((code - 0xD800) << 10) + low - 0xDC00;
                    i += 2;
                }
            }
            if (code > 0xFFFF) {
                if (at + 4 >= max) return -1;
                out[at++] = (char)(0xF0 | (code >> 18));
                out[at++] = (char)(0x80 | ((code >> 12) & 63));
                out[at++] = (char)(0x80 | ((code >> 6) & 63));
                out[at++] = (char)(0x80 | (code & 63));
            } else if (utf8_put(out, max, &at, code) < 0) return -1;
        }
    }
    out[at] = 0;
    c->p += length;
    return 0;
}

static int header(struct cursor *c, struct pmx_header *h)
{
    unsigned char magic[4], length, encoding, append_uv, sizes[6];
    float version;
    if (bytes(c, magic, 4) < 0 || memcmp(magic, "PMX ", 4) != 0 ||
        f32(c, &version) < 0 || (version != 2.0f && version != 2.1f) ||
        bytes(c, &length, 1) < 0 || length < 8 ||
        bytes(c, &encoding, 1) < 0 || encoding > 1 ||
        bytes(c, &append_uv, 1) < 0 || append_uv > 4 ||
        bytes(c, sizes, 6) < 0)
        return -1;
    h->encoding = encoding;
    h->append_uv = append_uv;
    h->vertex_size = sizes[0]; h->texture_size = sizes[1];
    h->material_size = sizes[2]; h->bone_size = sizes[3];
    h->morph_size = sizes[4]; h->rigid_size = sizes[5];
    if ((h->vertex_size != 1 && h->vertex_size != 2 && h->vertex_size != 4) ||
        (h->texture_size != 1 && h->texture_size != 2 && h->texture_size != 4) ||
        (h->material_size != 1 && h->material_size != 2 && h->material_size != 4) ||
        (h->bone_size != 1 && h->bone_size != 2 && h->bone_size != 4) ||
        (h->morph_size != 1 && h->morph_size != 2 && h->morph_size != 4) ||
        (h->rigid_size != 1 && h->rigid_size != 2 && h->rigid_size != 4)) return -1;
    return text(c) < 0 || text(c) < 0 || text(c) < 0 || text(c) < 0 ? -1 : 0;
}

static int read_weight(struct cursor *c, const struct pmx_header *h,
                       unsigned int type, struct pmx_vertex *v)
{
    int i;
    v->bone0 = v->bone1 = -1; v->weight = 1.0f;
    if (type == 0) return index_value(c, h->bone_size, 1, &v->bone0);
    if (type == 1) return index_value(c, h->bone_size, 1, &v->bone0) < 0 ||
        index_value(c, h->bone_size, 1, &v->bone1) < 0 ||
        f32(c, &v->weight) < 0 ? -1 : 0;
    if (type == 2 || type == 4) {
        int bones[4];
        float weights[4];
        int first = 0, second = 1;
        for (i = 0; i < 4; i++)
            if (index_value(c, h->bone_size, 1, &bones[i]) < 0) return -1;
        for (i = 0; i < 4; i++) if (f32(c, &weights[i]) < 0) return -1;
        if (weights[second] > weights[first]) first = 1, second = 0;
        for (i = 2; i < 4; i++) {
            if (weights[i] > weights[first]) second = first, first = i;
            else if (weights[i] > weights[second]) second = i;
        }
        v->bone0 = bones[first]; v->bone1 = bones[second];
        v->weight = weights[first] + weights[second] > 0.000001f ?
            weights[first] / (weights[first] + weights[second]) : 1.0f;
        return 0;
    }
    /* SDEF: two bone indices, weight, C, R0 and R1. */
    if (type == 3)
        return index_value(c, h->bone_size, 1, &v->bone0) < 0 ||
            index_value(c, h->bone_size, 1, &v->bone1) < 0 ||
            f32(c, &v->weight) < 0 ||
            !have(c, 36) ? -1 : (c->p += 36, 0);
    return -1;
}

static int read_vertex(struct cursor *c, const struct pmx_header *h,
                        struct pmx_vertex *v)
{
    unsigned int i, weight_type;
    if (f32(c, &v->x) < 0 || f32(c, &v->y) < 0 || f32(c, &v->z) < 0 ||
        f32(c, &v->nx) < 0 || f32(c, &v->ny) < 0 || f32(c, &v->nz) < 0 ||
        f32(c, &v->u) < 0 || f32(c, &v->v) < 0) return -1;
    v->au = 0.0f; v->av = 0.0f;
    if (h->append_uv > 0 &&
        (f32(c, &v->au) < 0 || f32(c, &v->av) < 0 ||
         !have(c, 8) || (c->p += 8, 0))) return -1;
    if (!have(c, (h->append_uv > 0 ? h->append_uv - 1 : 0) * 16U) ||
        (c->p += (h->append_uv > 0 ? h->append_uv - 1 : 0) * 16U,
        u8(c, &weight_type) < 0) || read_weight(c, h, weight_type, v) < 0 ||
        f32(c, &v->edge_scale) < 0) return -1;
    v->weight_type = weight_type;
    (void)i;
    return 0;
}

static int read_bones(struct cursor *c, const struct pmx_header *h,
                      struct pmx_bone **bones_out,
                      struct pmx_diagnostics *diagnostics)
{
    struct pmx_bone *bones;
    int count, i;
    if (i32(c, &count) < 0 || count <= 0 ||
        count > RASTERFALL_MODEL_MAX_BONES) return -1;
    bones = tlibc_malloc((size_t)count * sizeof(*bones));
    if (!bones) return -1;
    __memset(bones, 0, count * sizeof(*bones));
    for (i = 0; i < count; i++) {
        unsigned int flags, flag;
        int ignored;
        bones[i].tail_index = bones[i].append_parent = -1;
        if (read_text(c, h->encoding, bones[i].name, sizeof(bones[i].name)) < 0 ||
            text(c) < 0 || f32(c, &bones[i].x) < 0 ||
            f32(c, &bones[i].y) < 0 || f32(c, &bones[i].z) < 0 ||
            index_value(c, h->bone_size, 1, &bones[i].parent) < 0 ||
            i32(c, &ignored) < 0 || u16(c, &flags) < 0) goto fail;
        bones[i].flags = flags;
        if (flags & 0x0001) {
            if (index_value(c, h->bone_size, 1, &bones[i].tail_index) < 0) goto fail;
        } else if (f32(c, &bones[i].tail[0]) < 0 ||
                   f32(c, &bones[i].tail[1]) < 0 ||
                   f32(c, &bones[i].tail[2]) < 0) goto fail;
        if (flags & (0x0100 | 0x0200)) {
            if (index_value(c, h->bone_size, 1, &bones[i].append_parent) < 0 ||
                f32(c, &bones[i].append_ratio) < 0) goto fail;
        }
        if (flags & 0x0400) {
            if (f32(c, &bones[i].fixed_axis[0]) < 0 ||
                f32(c, &bones[i].fixed_axis[1]) < 0 ||
                f32(c, &bones[i].fixed_axis[2]) < 0) goto fail;
        }
        if (flags & 0x0800) {
            if (f32(c, &bones[i].local_x[0]) < 0 ||
                f32(c, &bones[i].local_x[1]) < 0 ||
                f32(c, &bones[i].local_x[2]) < 0 ||
                f32(c, &bones[i].local_z[0]) < 0 ||
                f32(c, &bones[i].local_z[1]) < 0 ||
                f32(c, &bones[i].local_z[2]) < 0) goto fail;
        }
        if (flags & 0x2000) {
            if (i32(c, &ignored) < 0) goto fail;
        }
        if (flags & 0x0020) {
            int link;
            if (index_value(c, h->bone_size, 1, &bones[i].ik_target) < 0 ||
                i32(c, &bones[i].ik_iterations) < 0 ||
                f32(c, &bones[i].ik_angle) < 0 ||
                i32(c, &bones[i].ik_link_count) < 0 ||
                bones[i].ik_target < 0 || bones[i].ik_target >= count ||
                bones[i].ik_iterations < 0 || bones[i].ik_link_count < 0 ||
                bones[i].ik_link_count > count) goto fail;
            if (bones[i].ik_link_count) {
                bones[i].ik_links = tlibc_malloc((size_t)bones[i].ik_link_count *
                                                  sizeof(*bones[i].ik_links));
                if (!bones[i].ik_links) goto fail;
                __memset(bones[i].ik_links, 0,
                         bones[i].ik_link_count * sizeof(*bones[i].ik_links));
            }
            for (link = 0; link < bones[i].ik_link_count; link++) {
                if (index_value(c, h->bone_size, 1,
                                &bones[i].ik_links[link].bone) < 0 ||
                    bones[i].ik_links[link].bone < 0 ||
                    bones[i].ik_links[link].bone >= count ||
                    u8(c, &flag) < 0 || flag > 1) goto fail;
                bones[i].ik_links[link].limited = (int)flag;
                if (flag && (f32(c, &bones[i].ik_links[link].lower[0]) < 0 ||
                             f32(c, &bones[i].ik_links[link].lower[1]) < 0 ||
                             f32(c, &bones[i].ik_links[link].lower[2]) < 0 ||
                             f32(c, &bones[i].ik_links[link].upper[0]) < 0 ||
                             f32(c, &bones[i].ik_links[link].upper[1]) < 0 ||
                             f32(c, &bones[i].ik_links[link].upper[2]) < 0)) goto fail;
            }
        }
        diagnostics->advanced_flags |= flags &
            (0x0020 | 0x0100 | 0x0200 | 0x0400 | 0x0800 | 0x1000 | 0x2000);
    }
    diagnostics->bone_count = count;
    diagnostics->root_count = 0;
    diagnostics->max_depth = 0;
    for (i = 0; i < count; i++) {
        if (bones[i].parent < -1 || bones[i].parent >= count) goto fail;
        if (bones[i].parent < 0) diagnostics->root_count++;
    }
    for (i = 0; i < count; i++) {
        int at = i, depth = 0;
        while (at >= 0) {
            if (++depth > count || depth > RASTERFALL_MODEL_MAX_BONE_DEPTH)
                goto fail;
            at = bones[at].parent;
        }
        bones[i].depth = depth;
        if (depth > diagnostics->max_depth) diagnostics->max_depth = depth;
    }
    if (!diagnostics->root_count) goto fail;
    *bones_out = bones;
    return 0;
fail:
    for (i = 0; i < count; i++) if (bones[i].ik_links) tlibc_free(bones[i].ik_links);
    tlibc_free(bones);
    return -1;
}

static int color_byte(float value)
{
    int v = (int)(value * 255.0f + 0.5f);
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

static int read_materials(struct cursor *c, const struct pmx_header *h,
                          struct pmx_material *materials, int *count)
{
    int n, i, j, texture, sphere, sphere_mode, toon_flag, toon, face_count;
    char memo[128];
    float r, g, b, a, sr, sg, sb, power, ar, ag, ab;
    float er, eg, eb, ea, edge_size;
    if (i32(c, &n) < 0 || n <= 0 || n > MAX_MATERIALS) return -1;
    for (i = 0; i < n; i++) {
        if (read_text(c, h->encoding, materials[i].name, sizeof(materials[i].name)) < 0 ||
            read_text(c, h->encoding, materials[i].name_en, sizeof(materials[i].name_en)) < 0 ||
            f32(c, &r) < 0 || f32(c, &g) < 0 ||
            f32(c, &b) < 0 || f32(c, &a) < 0 ||
            f32(c, &sr) < 0 || f32(c, &sg) < 0 || f32(c, &sb) < 0 ||
            f32(c, &power) < 0 || f32(c, &ar) < 0 || f32(c, &ag) < 0 ||
            f32(c, &ab) < 0) return -1;
        if (u8(c, (unsigned int *)&materials[i].draw_flags) < 0 ||
            f32(c, &er) < 0 || f32(c, &eg) < 0 || f32(c, &eb) < 0 ||
            f32(c, &ea) < 0 || f32(c, &edge_size) < 0) return -1;
        if (index_value(c, h->texture_size, 1, &texture) < 0 ||
            index_value(c, h->texture_size, 1, &sphere) < 0 ||
            u8(c, (unsigned int *)&sphere_mode) < 0 ||
            u8(c, (unsigned int *)&toon_flag) < 0) return -1;
        if (toon_flag == 0) { if (index_value(c, h->texture_size, 1, &toon) < 0) return -1; }
        else if (u8(c, (unsigned int *)&toon) < 0) return -1;
        if (read_text(c, h->encoding, memo, sizeof(memo)) < 0 ||
            i32(c, &face_count) < 0 || face_count < 0 || face_count % 3) return -1;
        materials[i].visual_role = ascii_contains(memo, "rasterfall role=") ?
            material_visual_role(memo) : RASTERFALL_MODEL_MATERIAL_ROLE_NONE;
        materials[i].index_count = (unsigned int)face_count;
        /* Sphere maps are handled separately; retain the ordinary base map. */
        materials[i].texture_index = texture;
        materials[i].sphere_index = sphere;
        materials[i].sphere_mode = sphere_mode;
        materials[i].alpha = color_byte(a);
        materials[i].toon_index = toon;
        materials[i].toon_shared = toon_flag != 0;
        materials[i].edge_color = (unsigned int)color_byte(ea) << 24 |
            (unsigned int)color_byte(er) << 16 |
            (unsigned int)color_byte(eg) << 8 | (unsigned int)color_byte(eb);
        /* Rasterfall's integer world/projection path needs a few world units
         * before an outline reaches one screen pixel.  A 1/16 model-unit
         * shell keeps the result narrow after gallery scaling. */
        materials[i].edge_size = edge_size <= 0.0f ? 0 :
            (int)(edge_size * 232.0f / 16.0f + 0.5f);
        if (edge_size > 0.0f && materials[i].edge_size < 1)
            materials[i].edge_size = 1;
        materials[i].color = (unsigned int)color_byte(r) << 16 |
            (unsigned int)color_byte(g) << 8 | (unsigned int)color_byte(b);
        materials[i].ambient_color = (unsigned int)color_byte(ar) << 16 |
            (unsigned int)color_byte(ag) << 8 | (unsigned int)color_byte(ab);
        materials[i].specular_color = (unsigned int)color_byte(sr) << 16 |
            (unsigned int)color_byte(sg) << 8 | (unsigned int)color_byte(sb);
        materials[i].specular_power = power <= 0.0f ? 0 :
            (int)(power * 256.0f + 0.5f);
    }
    *count = n;
    (void)j; (void)a; (void)texture; (void)sphere; (void)sphere_mode; (void)toon;
    return 0;
}

static int write_all(int fd, const void *buf, int length);

static int copy_file(const char *source, const char *destination)
{
    int in, out, n;
    unsigned char buffer[8192];
    struct stat st;
    in = __openat(AT_FDCWD, source, O_RDONLY, 0);
    if (in < 0 || __fstat(in, &st) < 0 || st.st_size <= 0 || st.st_size > 64 * 1024 * 1024) {
        if (in >= 0) __close(in);
        return -1;
    }
    out = __openat(AT_FDCWD, destination, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { __close(in); return -1; }
    while ((n = (int)__read(in, buffer, sizeof(buffer))) > 0) {
        if (write_all(out, buffer, n) < 0) { __close(in); __close(out); return -1; }
    }
    __close(in); __close(out);
    return n < 0 ? -1 : 0;
}

static int copy_textures(const char *pmx_path, const char *output_dir,
                         struct pmx_texture *textures, int texture_count)
{
    char source_dir[TEXTURE_PATH_MAX], source[TEXTURE_PATH_MAX * 2];
    char destination[TEXTURE_PATH_MAX * 2], name[64], extension[32];
    const char *slash, *dot;
    int i, length;
    slash = strrchr(pmx_path, '/');
    length = slash ? (int)(slash - pmx_path) : 0;
    if (length >= (int)sizeof(source_dir)) return -1;
    memcpy(source_dir, pmx_path, length); source_dir[length] = 0;
    if (tlibc_recursive_mkdir(output_dir) < 0) return -1;
    for (i = 0; i < texture_count; i++) {
        length = (int)strlen(textures[i].path);
        if (!length || textures[i].path[length - 1] == '/') {
            __printf("pmx2rmesh: texture %d is an empty/directory placeholder; runtime will use material fallback\n", i);
            continue;
        }
        if (textures[i].path[0] == '/' ||
            (textures[i].path[0] && textures[i].path[1] == ':'))
            snprintf(source, sizeof(source), "%s", textures[i].path);
        else if (source_dir[0]) snprintf(source, sizeof(source), "%s/%s", source_dir, textures[i].path);
        else snprintf(source, sizeof(source), "%s", textures[i].path);
        dot = strrchr(textures[i].path, '.');
        if (!dot || strlen(dot) >= sizeof(extension)) snprintf(extension, sizeof(extension), ".bin");
        else snprintf(extension, sizeof(extension), "%s", dot);
        snprintf(name, sizeof(name), "texture_%03d%s", i, extension);
        snprintf(destination, sizeof(destination), "%s/%s", output_dir, name);
        if (copy_file(source, destination) < 0) {
            __printf("pmx2rmesh: cannot copy texture %d: %s\n", i, source);
            return -1;
        }
    }
    return 0;
}

static int write_all(int fd, const void *buf, int length)
{
    const unsigned char *p = (const unsigned char *)buf;
    int n;
    while (length > 0) { n = __write(fd, p, length); if (n <= 0) return -1; p += n; length -= n; }
    return 0;
}

static void put_u16(unsigned char *p, unsigned int v)
{ p[0] = v; p[1] = v >> 8; }
static void put_u32(unsigned char *p, unsigned int v)
{ p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static int fixed(float value, int scale)
{ return (int)(value * scale + (value < 0.0f ? -0.5f : 0.5f)); }

static int emit_vertex(int fd, const struct pmx_vertex *v, int scale)
{
    unsigned char out[RASTERFALL_MODEL_VERTEX_BYTES_EDGE_SCALE];
    __memset(out, 0, sizeof(out));
    *(int *)(out + 0) = fixed(v->x, scale); *(int *)(out + 4) = fixed(v->y, scale); *(int *)(out + 8) = fixed(v->z, scale);
    *(short *)(out + 12) = (short)fixed(v->nx, 32767); *(short *)(out + 14) = (short)fixed(v->ny, 32767); *(short *)(out + 16) = (short)fixed(v->nz, 32767);
    put_u16(out + 18, (unsigned int)(v->u < 0 ? 0 : v->u > 1 ? 65535 : v->u * 65535.0f));
    put_u16(out + 20, (unsigned int)(v->v < 0 ? 0 : v->v > 1 ? 65535 : v->v * 65535.0f));
    *(int *)(out + 24) = fixed(v->au, 65536);
    *(int *)(out + 28) = fixed(v->av, 65536);
    put_u32(out + 32, v->edge_scale <= 0.0f ? 0U :
        v->edge_scale >= 65535.0f ? 0xffffffffU :
        (unsigned int)(v->edge_scale * 65536.0f + 0.5f));
    return write_all(fd, out, sizeof(out));
}

static int emit_skin_vertex(int fd, const struct pmx_vertex *v)
{
    unsigned char out[RASTERFALL_MODEL_SKIN_VERTEX_BYTES];
    unsigned int weight;
    __memset(out, 0, sizeof(out));
    if (v->weight_type == 0) {
        put_u16(out, (unsigned int)v->bone0);
        put_u16(out + 2, 0xffffU);
        put_u16(out + 4, 65535U);
    } else {
        weight = (unsigned int)(v->weight * 65535.0f + 0.5f);
        if (weight > 65535U) weight = 65535U;
        put_u16(out, (unsigned int)v->bone0);
        put_u16(out + 2, (unsigned int)v->bone1);
        put_u16(out + 4, weight);
        out[6] = 1;
    }
    return write_all(fd, out, sizeof(out));
}

static int emit_skin_section(int fd, const unsigned char *file, int size,
                             const struct pmx_bone *bones, int bone_count,
                             int vertex_count, int scale)
{
    struct cursor c;
    struct pmx_header h;
    struct pmx_vertex v;
    unsigned char skin_header[RASTERFALL_MODEL_SKIN_HEADER_BYTES];
    unsigned char record[RASTERFALL_MODEL_BONE_BYTES];
    unsigned int names_bytes = 0, total_bytes, name_offset = 0;
    unsigned int ik_count = 0, link_count = 0, link_start = 0;
    int i, ignored;
    for (i = 0; i < bone_count; i++)
        names_bytes += (unsigned int)strlen(bones[i].name) + 1;
    for (i = 0; i < bone_count; i++) if (bones[i].flags & 0x0020) {
        ik_count++; link_count += (unsigned int)bones[i].ik_link_count;
    }
    total_bytes = RASTERFALL_MODEL_SKIN_HEADER_BYTES +
        bone_count * RASTERFALL_MODEL_BONE_BYTES +
        vertex_count * RASTERFALL_MODEL_SKIN_VERTEX_BYTES + names_bytes;
    if (ik_count) total_bytes += RASTERFALL_MODEL_IK_HEADER_BYTES +
        ik_count * RASTERFALL_MODEL_IK_RECORD_BYTES +
        link_count * RASTERFALL_MODEL_IK_LINK_BYTES;
    __memset(skin_header, 0, sizeof(skin_header));
    put_u32(skin_header, RASTERFALL_MODEL_SKIN_MAGIC);
    put_u32(skin_header + 4, total_bytes);
    put_u32(skin_header + 8, (unsigned int)bone_count);
    put_u32(skin_header + 12, RASTERFALL_MODEL_BONE_BYTES);
    put_u32(skin_header + 16, (unsigned int)vertex_count);
    put_u32(skin_header + 20, RASTERFALL_MODEL_SKIN_VERTEX_BYTES);
    put_u32(skin_header + 24, names_bytes);
    if (write_all(fd, skin_header, sizeof(skin_header)) < 0) return -1;
    for (i = 0; i < bone_count; i++) {
        __memset(record, 0, sizeof(record));
        put_u32(record, (unsigned int)bones[i].parent);
        put_u16(record + 4, bones[i].flags);
        *(int *)(record + 8) = fixed(bones[i].x, scale);
        *(int *)(record + 12) = fixed(bones[i].y, scale);
        *(int *)(record + 16) = fixed(bones[i].z, scale);
        put_u32(record + 20, name_offset);
        put_u32(record + 24, bones[i].append_parent < 0 ? 0xffffffffU :
                (unsigned int)bones[i].append_parent);
        memcpy(record + 28, &bones[i].append_ratio, sizeof(float));
        if (write_all(fd, record, sizeof(record)) < 0) return -1;
        name_offset += (unsigned int)strlen(bones[i].name) + 1;
    }
    c.p = file; c.end = file + size;
    if (header(&c, &h) < 0 || i32(&c, &ignored) < 0) return -1;
    for (i = 0; i < vertex_count; i++)
        if (read_vertex(&c, &h, &v) < 0 || emit_skin_vertex(fd, &v) < 0)
            return -1;
    for (i = 0; i < bone_count; i++)
        if (write_all(fd, bones[i].name, (int)strlen(bones[i].name) + 1) < 0)
            return -1;
    if (ik_count) {
        unsigned char section[RASTERFALL_MODEL_IK_HEADER_BYTES];
        __memset(section, 0, sizeof(section));
        put_u32(section, RASTERFALL_MODEL_IK_MAGIC);
        put_u32(section + 4, RASTERFALL_MODEL_IK_HEADER_BYTES +
                ik_count * RASTERFALL_MODEL_IK_RECORD_BYTES +
                link_count * RASTERFALL_MODEL_IK_LINK_BYTES);
        put_u32(section + 8, ik_count);
        put_u32(section + 12, RASTERFALL_MODEL_IK_RECORD_BYTES);
        put_u32(section + 16, RASTERFALL_MODEL_IK_LINK_BYTES);
        if (write_all(fd, section, sizeof(section)) < 0) return -1;
        for (i = 0; i < bone_count; i++) if (bones[i].flags & 0x0020) {
            unsigned char r[RASTERFALL_MODEL_IK_RECORD_BYTES];
            __memset(r, 0, sizeof(r));
            put_u32(r, (unsigned int)i); put_u32(r + 4, (unsigned int)bones[i].ik_target);
            put_u32(r + 8, (unsigned int)bones[i].ik_iterations);
            memcpy(r + 12, &bones[i].ik_angle, sizeof(float));
            put_u32(r + 16, (unsigned int)bones[i].ik_link_count);
            put_u32(r + 20, link_start);
            if (write_all(fd, r, sizeof(r)) < 0) return -1;
            link_start += (unsigned int)bones[i].ik_link_count;
        }
        for (i = 0; i < bone_count; i++) if (bones[i].flags & 0x0020) {
            int j;
            for (j = 0; j < bones[i].ik_link_count; j++) {
                unsigned char r[RASTERFALL_MODEL_IK_LINK_BYTES];
                struct pmx_ik_link *l = &bones[i].ik_links[j];
                __memset(r, 0, sizeof(r)); put_u32(r, (unsigned int)l->bone);
                put_u32(r + 4, (unsigned int)l->limited);
                memcpy(r + 8, l->lower, 12); memcpy(r + 20, l->upper, 12);
                if (write_all(fd, r, sizeof(r)) < 0) return -1;
            }
        }
    }
    return 0;
}

static void free_bones(struct pmx_bone *bones, int count)
{
    int i;
    if (!bones) return;
    for (i = 0; i < count; i++) if (bones[i].ik_links) tlibc_free(bones[i].ik_links);
    tlibc_free(bones);
}

static const char *texture_name(const struct pmx_texture *textures,
                                int texture_count, int index)
{
    return index >= 0 && index < texture_count ? textures[index].path : "(none)";
}

static void print_diagnostics(const struct pmx_header *h,
                              const struct pmx_texture *textures, int texture_count,
                              const struct pmx_material *materials, int material_count,
                              int vertex_count, int index_count,
                              const struct pmx_diagnostics *diagnostics)
{
    int i, base_textured = 0, transparent = 0, double_sided = 0, edge = 0;
    int ambient = 0, specular = 0, toon_texture = 0, toon_shared = 0;
    int sphere_multiply = 0, sphere_add = 0, sphere_subtexture = 0;
    __printf("pmx2rmesh: diagnostic: materials=%d textures=%d additional_uv=%d\n",
             material_count, texture_count, h->append_uv);
    for (i = 0; i < material_count; i++) {
        const struct pmx_material *m = &materials[i];
        if (m->texture_index >= 0) base_textured++;
        if (m->alpha < 255) transparent++;
        if (m->draw_flags & 0x01) double_sided++;
        if ((m->draw_flags & 0x10) && m->edge_size > 0 && (m->edge_color >> 24)) edge++;
        if (m->ambient_color) ambient++;
        if (m->specular_color && m->specular_power > 0) specular++;
        if (m->toon_index >= 0) {
            if (m->toon_shared) toon_shared++; else toon_texture++;
        }
        if (m->sphere_index >= 0) {
            if (m->sphere_mode == 1) sphere_multiply++;
            else if (m->sphere_mode == 2) sphere_add++;
            else if (m->sphere_mode == 3) sphere_subtexture++;
        }
        __printf("pmx2rmesh: material[%d] name=\"%s\" name_en=\"%s\" base=\"%s\" sphere=\"%s\" sphere_mode=%d toon_kind=%s toon_index=%d toon=\"%s\" alpha=%d flags=0x%x flags_decoded={double_sided=%s,ground_shadow=%s,cast_self_shadow=%s,receive_self_shadow=%s,edge=%s,vertex_color=%s,point_draw=%s,line_draw=%s} edge_rgba=0x%x edge_size=%d ambient=0x%x specular=0x%x specular_power=%d indices=%u\n",
                 i, m->name, m->name_en,
                 texture_name(textures, texture_count, m->texture_index),
                 texture_name(textures, texture_count, m->sphere_index),
                 m->sphere_mode, m->toon_index < 0 ? "none" :
                 m->toon_shared ? "shared" : "texture", m->toon_index,
                 m->toon_shared ? "(built-in)" :
                 texture_name(textures, texture_count, m->toon_index),
                 m->alpha, m->draw_flags,
                 m->draw_flags & 0x01 ? "yes" : "no",
                 m->draw_flags & 0x02 ? "yes" : "no",
                 m->draw_flags & 0x04 ? "yes" : "no",
                 m->draw_flags & 0x08 ? "yes" : "no",
                 m->draw_flags & 0x10 ? "yes" : "no",
                 m->draw_flags & 0x20 ? "yes" : "no",
                 m->draw_flags & 0x40 ? "yes" : "no",
                 m->draw_flags & 0x80 ? "yes" : "no",
                 m->edge_color,
                 m->edge_size, m->ambient_color, m->specular_color,
                 m->specular_power, m->index_count);
    }
    __printf("pmx2rmesh: feature summary: vertices=%d triangles=%d materials=%d textures=%d base_textured=%d transparent=%d opaque=%d double_sided=%d single_sided=%d edge=%d\n",
             vertex_count, index_count / 3, material_count, texture_count,
             base_textured, transparent, material_count - transparent,
             double_sided, material_count - double_sided, edge);
    __printf("pmx2rmesh: feature summary: sphere={multiply=%d,add=%d,subtexture=%d} toon={texture=%d,shared=%d} lighting={ambient=%d,specular=%d} additional_uv=%d\n",
             sphere_multiply, sphere_add, sphere_subtexture, toon_texture,
             toon_shared, ambient, specular, h->append_uv);
    __printf("pmx2rmesh: feature summary: skinning={BDEF1=%u,BDEF2=%u,BDEF4=%u,SDEF=%u,QDEF=%u} bones=%d roots=%d max_depth=%d invalid_bone_references=0\n",
             diagnostics->weights[0], diagnostics->weights[1],
             diagnostics->weights[2], diagnostics->weights[3],
             diagnostics->weights[4], diagnostics->bone_count,
             diagnostics->root_count, diagnostics->max_depth);
    __printf("pmx2rmesh: skeletal stage1: BDEF1=%u BDEF2=%u imported; advanced_bone_flags=0x%x parsed_not_evaluated; BDEF4=%u SDEF=%u QDEF=%u reduced_to_two_weights; morphs/display_frames/rigid_bodies/joints/soft_bodies=not imported\n",
             diagnostics->weights[0], diagnostics->weights[1], diagnostics->advanced_flags,
             diagnostics->weights[2], diagnostics->weights[3],
             diagnostics->weights[4]);
    if (h->append_uv > 1)
        __printf("pmx2rmesh: unsupported: additional_uv_channels_ignored=%d (channel 1 retained)\n",
                 h->append_uv - 1);
}

int main(int argc, char **argv)
{
    int fd, out, size, vertex_count, index_count, material_count, texture_count, i, j, scale = 232;
    int min_x = 2147483647, min_y = 2147483647, min_z = 2147483647;
    int max_x = -2147483647, max_y = -2147483647, max_z = -2147483647;
    unsigned char *file, header_out[RASTERFALL_MODEL_HEADER_BYTES], record[RASTERFALL_MODEL_MATERIAL_BYTES];
    struct stat st; struct cursor c; struct pmx_header h; struct pmx_texture textures[MAX_TEXTURES]; struct pmx_material materials[MAX_MATERIALS]; struct pmx_vertex v;
    int primitive_count = 0, index_base = 0;
    struct pmx_diagnostics diagnostics;
    struct pmx_bone *bones = NULL;
    unsigned int invalid_bone_references = 0;
    int humanoid_bones = argc == 3 && !strcmp(argv[1], "--humanoid-bones");
    int bone_audit = argc == 3 && !strcmp(argv[1], "--bone-audit");
    const char *input_path = humanoid_bones || bone_audit ? argv[2] : argv[1];
    if (!humanoid_bones && !bone_audit && argc != 4) { __printf("usage: pmx2rmesh input.pmx output.rmesh texture_dir\n       pmx2rmesh --humanoid-bones input.pmx\n       pmx2rmesh --bone-audit input.pmx\n"); return 2; }
    fd = __openat(AT_FDCWD, input_path, O_RDONLY, 0);
    if (fd < 0 || __fstat(fd, &st) < 0 || st.st_size < 64 || st.st_size > 64 * 1024 * 1024) { __printf("pmx2rmesh: cannot open input\n"); return 1; }
    size = (int)st.st_size; file = (unsigned char *)__mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0); __close(fd);
    if (file == MAP_FAILED) { __printf("pmx2rmesh: cannot map input\n"); return 1; }
    c.p = file; c.end = file + size;
    __memset(&diagnostics, 0, sizeof(diagnostics)); diagnostics.bone_count = -1;
    if (header(&c, &h) < 0 || i32(&c, &vertex_count) < 0 || vertex_count <= 0 || vertex_count > MAX_VERTICES) goto invalid;
    for (i = 0; i < vertex_count; i++) {
        if (read_vertex(&c, &h, &v) < 0) goto invalid;
        if (v.weight_type < 5) diagnostics.weights[v.weight_type]++;
    }
    if (i32(&c, &index_count) < 0 || index_count <= 0 || index_count > MAX_INDICES || index_count % 3) goto invalid;
    for (i = 0; i < index_count; i++) { int index; if (index_value(&c, h.vertex_size, 0, &index) < 0 || index < 0 || index >= vertex_count) goto invalid; }
    if (i32(&c, &texture_count) < 0 || texture_count < 0 || texture_count > MAX_TEXTURES) goto invalid;
    for (i = 0; i < texture_count; i++) if (read_text(&c, h.encoding, textures[i].path, sizeof(textures[i].path)) < 0) goto invalid;
    if (read_materials(&c, &h, materials, &material_count) < 0) goto invalid;
    if (read_bones(&c, &h, &bones, &diagnostics) < 0) {
        __printf("pmx2rmesh: invalid bone hierarchy or truncated bone data\n");
        goto invalid;
    }
    if (bone_audit) {
        unsigned int *influenced = tlibc_malloc((size_t)diagnostics.bone_count * sizeof(*influenced));
        double *weight_sum = tlibc_malloc((size_t)diagnostics.bone_count * sizeof(*weight_sum));
        float *max_weight = tlibc_malloc((size_t)diagnostics.bone_count * sizeof(*max_weight));
        if (!influenced || !weight_sum || !max_weight) goto invalid;
        __memset(influenced, 0, diagnostics.bone_count * sizeof(*influenced));
        __memset(weight_sum, 0, diagnostics.bone_count * sizeof(*weight_sum));
        __memset(max_weight, 0, diagnostics.bone_count * sizeof(*max_weight));
        c.p = file; c.end = file + size;
        if (header(&c, &h) < 0 || i32(&c, &j) < 0 || j != vertex_count) goto audit_fail;
        for (i = 0; i < vertex_count; i++) {
            if (read_vertex(&c, &h, &v) < 0) goto audit_fail;
            if (v.bone0 >= 0 && v.bone0 < diagnostics.bone_count) {
                float w = v.weight_type != 0 ? v.weight : 1.0f;
                influenced[v.bone0]++; weight_sum[v.bone0] += w;
                if (w > max_weight[v.bone0]) max_weight[v.bone0] = w;
            }
            if (v.weight_type != 0 && v.bone1 >= 0 && v.bone1 < diagnostics.bone_count) {
                float w = 1.0f - v.weight;
                influenced[v.bone1]++; weight_sum[v.bone1] += w;
                if (w > max_weight[v.bone1]) max_weight[v.bone1] = w;
            }
        }
        __printf("pmx2rmesh: bone audit input=%s bones=%d vertices=%d\n",
                 input_path, diagnostics.bone_count, vertex_count);
        for (i = 0; i < diagnostics.bone_count; i++) {
            if (!strstr(bones[i].name, "足") && !strstr(bones[i].name, "ひざ") &&
                !strstr(bones[i].name, "足首")) continue;
            __printf("  bone[%d] name=\"%s\" parent=%d/\"%s\" flags=0x%x rest=(%.3f,%.3f,%.3f) ",
                     i, bones[i].name, bones[i].parent,
                     bones[i].parent >= 0 ? bones[bones[i].parent].name : "<root>",
                     bones[i].flags, bones[i].x, bones[i].y, bones[i].z);
            __printf("grant_rot=%s grant_trans=%s grant_parent=%d/\"%s\" ratio=%.6f ",
                     bones[i].flags & 0x0100 ? "yes" : "no",
                     bones[i].flags & 0x0200 ? "yes" : "no",
                     bones[i].append_parent,
                     bones[i].append_parent >= 0 ? bones[bones[i].append_parent].name : "<none>",
                     bones[i].append_ratio);
            __printf("influenced_vertex_count=%u sum_weights=%.6f max_weight=%.6f\n",
                     influenced[i], weight_sum[i], max_weight[i]);
        }
        __printf("pmx2rmesh: RFM2 grant metadata status=saved (SKN1 v13 bone bytes=%d)\n",
                 RASTERFALL_MODEL_BONE_BYTES);
        tlibc_free(influenced); tlibc_free(weight_sum); tlibc_free(max_weight);
        free_bones(bones, diagnostics.bone_count); __munmap(file, size); return 0;
audit_fail:
        tlibc_free(influenced); tlibc_free(weight_sum); tlibc_free(max_weight);
        goto invalid;
    }
    if (humanoid_bones) {
        static const char *semantic[] = {
            "ROOT","HIPS","SPINE","CHEST","UPPER_CHEST","NECK","HEAD",
            "LEFT_SHOULDER","LEFT_UPPER_ARM","LEFT_FOREARM","LEFT_HAND",
            "RIGHT_SHOULDER","RIGHT_UPPER_ARM","RIGHT_FOREARM","RIGHT_HAND",
            "LEFT_UPPER_LEG","LEFT_LOWER_LEG","LEFT_FOOT",
            "RIGHT_UPPER_LEG","RIGHT_LOWER_LEG","RIGHT_FOOT"
        };
        static const char *wanted[] = {
            "全ての親","腰","上半身","上半身3","上半身2","首","頭",
            "左肩","左腕","左ひじ","左手首","右肩","右腕","右ひじ","右手首",
            "左足","左ひざ","左足首","右足","右ひざ","右足首"
        };
        int semantic_index;
        for (semantic_index = 0; semantic_index < 21; semantic_index++) {
            int bone_index;
            for (bone_index = 0; bone_index < diagnostics.bone_count; bone_index++)
                if (!strcmp(bones[bone_index].name, wanted[semantic_index])) break;
            if (bone_index == diagnostics.bone_count) continue;
            {
                struct pmx_bone *bone = &bones[bone_index];
                __printf("%s name=\"%s\" index=%d parent=%d/\"%s\" position=(%d.%06d,%d.%06d,%d.%06d) flags=0x%x tail=",
                         semantic[semantic_index], bone->name, bone_index, bone->parent,
                         bone->parent >= 0 ? bones[bone->parent].name : "NONE",
                         (int)bone->x, abs((int)(bone->x*1000000))%1000000,
                         (int)bone->y, abs((int)(bone->y*1000000))%1000000,
                         (int)bone->z, abs((int)(bone->z*1000000))%1000000,
                         bone->flags);
                if (bone->flags & 1) __printf("bone:%d/\"%s\"", bone->tail_index,
                    bone->tail_index >= 0 ? bones[bone->tail_index].name : "NONE");
                else __printf("offset=(%d.%06d,%d.%06d,%d.%06d)",
                    (int)bone->tail[0],abs((int)(bone->tail[0]*1000000))%1000000,
                    (int)bone->tail[1],abs((int)(bone->tail[1]*1000000))%1000000,
                    (int)bone->tail[2],abs((int)(bone->tail[2]*1000000))%1000000);
                __printf(" local_axis=%s", bone->flags & 0x0800 ? "yes" : "no");
                if (bone->flags & 0x0800) __printf(" X=(%d.%06d,%d.%06d,%d.%06d) Z=(%d.%06d,%d.%06d,%d.%06d)",
                    (int)bone->local_x[0],abs((int)(bone->local_x[0]*1000000))%1000000,(int)bone->local_x[1],abs((int)(bone->local_x[1]*1000000))%1000000,(int)bone->local_x[2],abs((int)(bone->local_x[2]*1000000))%1000000,
                    (int)bone->local_z[0],abs((int)(bone->local_z[0]*1000000))%1000000,(int)bone->local_z[1],abs((int)(bone->local_z[1]*1000000))%1000000,(int)bone->local_z[2],abs((int)(bone->local_z[2]*1000000))%1000000);
                __printf(" fixed_axis=%s", bone->flags & 0x0400 ? "yes" : "no");
                if (bone->flags & 0x0400) __printf(" axis=(%d.%06d,%d.%06d,%d.%06d)",
                    (int)bone->fixed_axis[0],abs((int)(bone->fixed_axis[0]*1000000))%1000000,(int)bone->fixed_axis[1],abs((int)(bone->fixed_axis[1]*1000000))%1000000,(int)bone->fixed_axis[2],abs((int)(bone->fixed_axis[2]*1000000))%1000000);
                __printf(" append_rotation=%s append_translation=%s append_parent=%d ratio=%d.%06d IK=%s after_physics=%s external_parent=%s rotatable=%s translatable=%s\n",
                    bone->flags&0x0100?"yes":"no",bone->flags&0x0200?"yes":"no",bone->append_parent,
                    (int)bone->append_ratio,abs((int)(bone->append_ratio*1000000))%1000000,
                    bone->flags&0x0020?"yes":"no",bone->flags&0x1000?"yes":"no",
                    bone->flags&0x2000?"yes":"no",bone->flags&0x0002?"yes":"no",
                    bone->flags&0x0004?"yes":"no");
            }
        }
        free_bones(bones, diagnostics.bone_count); __munmap(file, size); return 0;
    }
    if (diagnostics.weights[2] || diagnostics.weights[3] || diagnostics.weights[4])
        __printf("pmx2rmesh: skin compatibility: BDEF4=%u and QDEF=%u use normalized top-two weights; SDEF=%u uses its BDEF2 base weights\n",
                 diagnostics.weights[2], diagnostics.weights[4],
                 diagnostics.weights[3]);
    c.p = file; c.end = file + size;
    if (header(&c, &h) < 0 || i32(&c, &j) < 0 || j != vertex_count)
        goto invalid;
    for (i = 0; i < vertex_count; i++) {
        if (read_vertex(&c, &h, &v) < 0) goto invalid;
        if (v.bone0 < 0 || v.bone0 >= diagnostics.bone_count ||
            (v.weight_type != 0 &&
             (v.bone1 < 0 || v.bone1 >= diagnostics.bone_count ||
              !(v.weight >= 0.0f && v.weight <= 1.0f))))
            invalid_bone_references++;
    }
    if (invalid_bone_references) {
        __printf("pmx2rmesh: invalid BDEF bone references/weights=%u\n",
                 invalid_bone_references);
        goto invalid;
    }
    for (i = 0; i < material_count; i++) { if (materials[i].index_count) primitive_count++; index_base += materials[i].index_count; }
    if (index_base != index_count || primitive_count > 32) { __printf("pmx2rmesh: too many or inconsistent material groups (%d/%d, %d)\n", index_base, index_count, primitive_count); goto invalid; }
    print_diagnostics(&h, textures, texture_count, materials, material_count,
                      vertex_count, index_count, &diagnostics);
    for (i = 0; i < diagnostics.bone_count; i++)
        __printf("pmx2rmesh: bone[%d] name=\"%s\" parent=%d rest=(%d,%d,%d) flags=0x%x depth=%d\n",
                 i, bones[i].name, bones[i].parent,
                 fixed(bones[i].x, scale), fixed(bones[i].y, scale),
                 fixed(bones[i].z, scale), bones[i].flags, bones[i].depth);
    if (copy_textures(argv[1], argv[3], textures, texture_count) < 0) goto invalid;
    out = __openat(AT_FDCWD, argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644); if (out < 0) { __printf("pmx2rmesh: cannot create output\n"); goto invalid; }
    __memset(header_out, 0, sizeof(header_out)); header_out[0] = 'R'; header_out[1] = 'F'; header_out[2] = 'M'; header_out[3] = '2'; put_u32(header_out + 4, RASTERFALL_MODEL_VERSION); put_u32(header_out + 8, vertex_count); put_u32(header_out + 12, index_count); put_u32(header_out + 16, scale); put_u32(header_out + 44, primitive_count); put_u32(header_out + 48, material_count); put_u32(header_out + 52, 64); put_u32(header_out + 56, 64 + primitive_count * 16); put_u32(header_out + 60, 64 + primitive_count * 16 + material_count * RASTERFALL_MODEL_MATERIAL_BYTES + vertex_count * RASTERFALL_MODEL_VERTEX_BYTES_EDGE_SCALE + index_count * 4);
    if (write_all(out, header_out, sizeof(header_out)) < 0) { __close(out); goto invalid; }
    index_base = 0; for (i = 0; i < material_count; i++) if (materials[i].index_count) { __memset(record, 0, sizeof(record)); put_u32(record, index_base); put_u32(record + 4, materials[i].index_count); put_u32(record + 8, i); if (write_all(out, record, RASTERFALL_MODEL_PRIMITIVE_BYTES) < 0) { __close(out); goto invalid; } index_base += materials[i].index_count; }
    for (i = 0; i < material_count; i++) {
        unsigned int sphere = materials[i].sphere_index < 0 ? 0xffffffffU :
            ((unsigned int)materials[i].sphere_index & 0xffffU) |
            (((unsigned int)materials[i].sphere_mode & 3U) << 16);
        __memset(record, 0, sizeof(record)); put_u32(record, materials[i].color);
        put_u16(record + 4, (unsigned int)materials[i].alpha);
        record[5] = materials[i].toon_index < 0 ? 255 :
                    (unsigned char)materials[i].toon_index;
        record[6] = materials[i].toon_index < 0 ? 0 :
                    materials[i].toon_shared ? 2 : 1;
        record[7] = (unsigned char)materials[i].draw_flags;
        put_u32(record + 8, materials[i].texture_index < 0 ? 0xffffffffU : (unsigned int)materials[i].texture_index);
        put_u32(record + 12, sphere);
        put_u32(record + 16, materials[i].edge_color);
        put_u32(record + 20, (unsigned int)materials[i].edge_size);
        put_u32(record + 24, materials[i].ambient_color);
        put_u32(record + 28, materials[i].specular_color);
        put_u32(record + 32, (unsigned int)materials[i].specular_power);
        record[RASTERFALL_MODEL_MATERIAL_ROLE_OFFSET] =
            (unsigned char)materials[i].visual_role;
        if (write_all(out, record, sizeof(record)) < 0) { __close(out); goto invalid; }
    }
    c.p = file; c.end = file + size; if (header(&c, &h) < 0 || i32(&c, &j) < 0) { __close(out); goto invalid; }
    for (i = 0; i < vertex_count; i++) { if (read_vertex(&c, &h, &v) < 0 || emit_vertex(out, &v, scale) < 0) { __close(out); goto invalid; } j = fixed(v.x, scale); if (j < min_x) min_x = j; if (j > max_x) max_x = j; j = fixed(v.y, scale); if (j < min_y) min_y = j; if (j > max_y) max_y = j; j = fixed(v.z, scale); if (j < min_z) min_z = j; if (j > max_z) max_z = j; }
    if (i32(&c, &j) < 0) { __close(out); goto invalid; }
    { unsigned char index_buffer[8192]; int index_bytes = 0;
      for (i = 0; i < index_count; i++) { int index; if (index_value(&c, h.vertex_size, 0, &index) < 0) { __close(out); goto invalid; } if (index_bytes + 4 > (int)sizeof(index_buffer)) { if (write_all(out, index_buffer, index_bytes) < 0) { __close(out); goto invalid; } index_bytes = 0; } put_u32(index_buffer + index_bytes, index); index_bytes += 4; }
      if (index_bytes && write_all(out, index_buffer, index_bytes) < 0) { __close(out); goto invalid; }
    }
    if (emit_skin_section(out, file, size, bones,
                          diagnostics.bone_count, vertex_count, scale) < 0) {
        __close(out);
        goto invalid;
    }
    *(int *)(header_out + 20) = min_x; *(int *)(header_out + 24) = min_y; *(int *)(header_out + 28) = min_z; *(int *)(header_out + 32) = max_x; *(int *)(header_out + 36) = max_y; *(int *)(header_out + 40) = max_z;
    if (__lseek(out, 0, SEEK_SET) < 0 || write_all(out, header_out, sizeof(header_out)) < 0) { __close(out); goto invalid; }
    __close(out);
    free_bones(bones, diagnostics.bone_count);
    __munmap(file, size); __printf("pmx2rmesh: %s -> %s (%d vertices, %d triangles, %d materials, %d bones, RFM2 v%d)\n", argv[1], argv[2], vertex_count, index_count / 3, material_count, diagnostics.bone_count, RASTERFALL_MODEL_VERSION); return 0;
invalid:
    if (bones) free_bones(bones, diagnostics.bone_count);
    __munmap(file, size); __printf("pmx2rmesh: unsupported or truncated PMX\n"); return 1;
}
