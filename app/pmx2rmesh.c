/* Extract the static mesh data needed by Rasterfall from a PMX 2.0/2.1 file.
 *
 * This is intentionally an offline converter.  Bones, morphs, rigid bodies,
 * joints and toon textures are parsed only far enough to skip them. Base and
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

struct cursor { const unsigned char *p, *end; };
struct pmx_header { int vertex_size, texture_size, material_size; int bone_size, morph_size, rigid_size; int encoding, append_uv; };
struct pmx_texture { char path[TEXTURE_PATH_MAX]; };
struct pmx_material { unsigned int color; unsigned int index_count; int texture_index; int sphere_index; int sphere_mode; int alpha; int toon_index; int toon_shared; };
struct pmx_vertex { float x, y, z, nx, ny, nz, u, v; };

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

static int skip_weight(struct cursor *c, const struct pmx_header *h, unsigned int type)
{
    int i;
    if (type == 0) return index_value(c, h->bone_size, 1, &i);
    if (type == 1) return index_value(c, h->bone_size, 1, &i) < 0 ||
        index_value(c, h->bone_size, 1, &i) < 0 || f32(c, 0) < 0 ? -1 : 0;
    if (type == 2 || type == 4) {
        for (i = 0; i < 4; i++) if (index_value(c, h->bone_size, 1, &i) < 0) return -1;
        for (i = 0; i < 4; i++) if (f32(c, 0) < 0) return -1;
        return 0;
    }
    /* SDEF: two bone indices, weight, C, R0 and R1. */
    if (type == 3)
        return index_value(c, h->bone_size, 1, &i) < 0 ||
            index_value(c, h->bone_size, 1, &i) < 0 || f32(c, 0) < 0 ||
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
    if (!have(c, h->append_uv * 16U) || (c->p += h->append_uv * 16U,
        u8(c, &weight_type) < 0) || skip_weight(c, h, weight_type) < 0 ||
        f32(c, 0) < 0) return -1;
    (void)i;
    return 0;
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
    float r, g, b, a;
    if (i32(c, &n) < 0 || n <= 0 || n > MAX_MATERIALS) return -1;
    for (i = 0; i < n; i++) {
        if (text(c) < 0 || text(c) < 0 || f32(c, &r) < 0 || f32(c, &g) < 0 ||
            f32(c, &b) < 0 || f32(c, &a) < 0 || !have(c, 12 + 4 + 12 + 1 + 16 + 4)) return -1;
        c->p += 12 + 4 + 12 + 1 + 16 + 4;
        if (index_value(c, h->texture_size, 1, &texture) < 0 ||
            index_value(c, h->texture_size, 1, &sphere) < 0 ||
            u8(c, (unsigned int *)&sphere_mode) < 0 ||
            u8(c, (unsigned int *)&toon_flag) < 0) return -1;
        if (toon_flag == 0) { if (index_value(c, h->texture_size, 1, &toon) < 0) return -1; }
        else if (u8(c, (unsigned int *)&toon) < 0) return -1;
        if (text(c) < 0 || i32(c, &face_count) < 0 || face_count < 0 || face_count % 3) return -1;
        materials[i].index_count = (unsigned int)face_count;
        /* Sphere maps are handled separately; retain the ordinary base map. */
        materials[i].texture_index = texture;
        materials[i].sphere_index = sphere;
        materials[i].sphere_mode = sphere_mode;
        materials[i].alpha = color_byte(a);
        materials[i].toon_index = toon;
        materials[i].toon_shared = toon_flag != 0;
        materials[i].color = (unsigned int)color_byte(r) << 16 |
            (unsigned int)color_byte(g) << 8 | (unsigned int)color_byte(b);
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
    unsigned char out[RASTERFALL_MODEL_VERTEX_BYTES];
    __memset(out, 0, sizeof(out));
    *(int *)(out + 0) = fixed(v->x, scale); *(int *)(out + 4) = fixed(v->y, scale); *(int *)(out + 8) = fixed(v->z, scale);
    *(short *)(out + 12) = (short)fixed(v->nx, 32767); *(short *)(out + 14) = (short)fixed(v->ny, 32767); *(short *)(out + 16) = (short)fixed(v->nz, 32767);
    put_u16(out + 18, (unsigned int)(v->u < 0 ? 0 : v->u > 1 ? 65535 : v->u * 65535.0f));
    put_u16(out + 20, (unsigned int)(v->v < 0 ? 0 : v->v > 1 ? 65535 : v->v * 65535.0f));
    return write_all(fd, out, sizeof(out));
}

int main(int argc, char **argv)
{
    int fd, out, size, vertex_count, index_count, material_count, texture_count, i, j, scale = 232;
    int min_x = 2147483647, min_y = 2147483647, min_z = 2147483647;
    int max_x = -2147483647, max_y = -2147483647, max_z = -2147483647;
    unsigned char *file, header_out[RASTERFALL_MODEL_HEADER_BYTES], record[16];
    struct stat st; struct cursor c; struct pmx_header h; struct pmx_texture textures[MAX_TEXTURES]; struct pmx_material materials[MAX_MATERIALS]; struct pmx_vertex v;
    int primitive_count = 0, index_base = 0;
    if (argc != 4) { __printf("usage: pmx2rmesh input.pmx output.rmesh texture_dir\n"); return 2; }
    fd = __openat(AT_FDCWD, argv[1], O_RDONLY, 0);
    if (fd < 0 || __fstat(fd, &st) < 0 || st.st_size < 64 || st.st_size > 64 * 1024 * 1024) { __printf("pmx2rmesh: cannot open input\n"); return 1; }
    size = (int)st.st_size; file = (unsigned char *)__mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0); __close(fd);
    if (file == MAP_FAILED) { __printf("pmx2rmesh: cannot map input\n"); return 1; }
    c.p = file; c.end = file + size;
    if (header(&c, &h) < 0 || i32(&c, &vertex_count) < 0 || vertex_count <= 0 || vertex_count > MAX_VERTICES) goto invalid;
    for (i = 0; i < vertex_count; i++) if (read_vertex(&c, &h, &v) < 0) goto invalid;
    if (i32(&c, &index_count) < 0 || index_count <= 0 || index_count > MAX_INDICES || index_count % 3) goto invalid;
    for (i = 0; i < index_count; i++) { int index; if (index_value(&c, h.vertex_size, 0, &index) < 0 || index < 0 || index >= vertex_count) goto invalid; }
    if (i32(&c, &texture_count) < 0 || texture_count < 0 || texture_count > MAX_TEXTURES) goto invalid;
    for (i = 0; i < texture_count; i++) if (read_text(&c, h.encoding, textures[i].path, sizeof(textures[i].path)) < 0) goto invalid;
    if (read_materials(&c, &h, materials, &material_count) < 0) goto invalid;
    for (i = 0; i < material_count; i++) { if (materials[i].index_count) primitive_count++; index_base += materials[i].index_count; }
    if (index_base != index_count || primitive_count > 32) { __printf("pmx2rmesh: too many or inconsistent material groups (%d/%d, %d)\n", index_base, index_count, primitive_count); goto invalid; }
    if (copy_textures(argv[1], argv[3], textures, texture_count) < 0) goto invalid;
    out = __openat(AT_FDCWD, argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644); if (out < 0) { __printf("pmx2rmesh: cannot create output\n"); goto invalid; }
    __memset(header_out, 0, sizeof(header_out)); header_out[0] = 'R'; header_out[1] = 'F'; header_out[2] = 'M'; header_out[3] = '2'; put_u32(header_out + 4, 5); put_u32(header_out + 8, vertex_count); put_u32(header_out + 12, index_count); put_u32(header_out + 16, scale); put_u32(header_out + 44, primitive_count); put_u32(header_out + 48, material_count); put_u32(header_out + 52, 64); put_u32(header_out + 56, 64 + primitive_count * 16);
    if (write_all(out, header_out, sizeof(header_out)) < 0) { __close(out); goto invalid; }
    index_base = 0; for (i = 0; i < material_count; i++) if (materials[i].index_count) { __memset(record, 0, sizeof(record)); put_u32(record, index_base); put_u32(record + 4, materials[i].index_count); put_u32(record + 8, i); if (write_all(out, record, sizeof(record)) < 0) { __close(out); goto invalid; } index_base += materials[i].index_count; }
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
        record[7] = 0;
        put_u32(record + 8, materials[i].texture_index < 0 ? 0xffffffffU : (unsigned int)materials[i].texture_index);
        put_u32(record + 12, sphere);
        if (write_all(out, record, sizeof(record)) < 0) { __close(out); goto invalid; }
    }
    c.p = file; c.end = file + size; if (header(&c, &h) < 0 || i32(&c, &j) < 0) { __close(out); goto invalid; }
    for (i = 0; i < vertex_count; i++) { if (read_vertex(&c, &h, &v) < 0 || emit_vertex(out, &v, scale) < 0) { __close(out); goto invalid; } j = fixed(v.x, scale); if (j < min_x) min_x = j; if (j > max_x) max_x = j; j = fixed(v.y, scale); if (j < min_y) min_y = j; if (j > max_y) max_y = j; j = fixed(v.z, scale); if (j < min_z) min_z = j; if (j > max_z) max_z = j; }
    if (i32(&c, &j) < 0) { __close(out); goto invalid; }
    { unsigned char index_buffer[8192]; int index_bytes = 0;
      for (i = 0; i < index_count; i++) { int index; if (index_value(&c, h.vertex_size, 0, &index) < 0) { __close(out); goto invalid; } if (index_bytes + 4 > (int)sizeof(index_buffer)) { if (write_all(out, index_buffer, index_bytes) < 0) { __close(out); goto invalid; } index_bytes = 0; } put_u32(index_buffer + index_bytes, index); index_bytes += 4; }
      if (index_bytes && write_all(out, index_buffer, index_bytes) < 0) { __close(out); goto invalid; }
    }
    *(int *)(header_out + 20) = min_x; *(int *)(header_out + 24) = min_y; *(int *)(header_out + 28) = min_z; *(int *)(header_out + 32) = max_x; *(int *)(header_out + 36) = max_y; *(int *)(header_out + 40) = max_z;
    if (__lseek(out, 0, SEEK_SET) < 0 || write_all(out, header_out, sizeof(header_out)) < 0) { __close(out); goto invalid; }
    __close(out);
    __munmap(file, size); __printf("pmx2rmesh: %s -> %s (%d vertices, %d triangles, %d materials)\n", argv[1], argv[2], vertex_count, index_count / 3, material_count); return 0;
invalid:
    __munmap(file, size); __printf("pmx2rmesh: unsupported or truncated PMX\n"); return 1;
}
