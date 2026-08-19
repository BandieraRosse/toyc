/* Convert a static glTF 2.0 GLB mesh into Rasterfall's RFM2 format. */
#include "core.h"
#include "tlibc_print.h"
#include "tlibc_everything.h"
#include "rasterfall_model.h"

#define GLB_MAGIC 0x46546c67U
#define GLB_JSON  0x4e4f534aU
#define GLB_BIN   0x004e4942U
#define MAX_PARTS 32
#define MAX_MATERIALS 32

struct slice { const char *p, *end; };
struct accessor { int view, offset, count, component, type; };
struct view { int offset, length, stride; };
struct material { unsigned int color; unsigned short metallic, roughness; };
struct part {
    struct accessor pos, normal, uv, index;
    struct view pos_view, normal_view, uv_view, index_view;
    int vertex_base, index_base;
    int material;
};

static int ws(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }
static const char *skip_ws(const char *p, const char *e)
{ while (p < e && ws(*p)) p++; return p; }

static const char *skip_json(const char *p, const char *e)
{
    int depth = 0, quoted = 0, escaped = 0;
    for (; p < e; p++) {
        char c = *p;
        if (quoted) { if (escaped) escaped = 0; else if (c == '\\') escaped = 1; else if (c == '"') quoted = 0; continue; }
        if (c == '"') { quoted = 1; continue; }
        if (c == '{' || c == '[') depth++;
        else if (c == '}' || c == ']') { depth--; if (depth == 0) return p + 1; }
        else if (depth == 0 && (c == ',' || c == '}' || c == ']')) return p;
    }
    return e;
}

/* The glTF JSON chunk is padded with spaces. A shallow key scanner is enough
 * for the small root/primitive/accessor objects and tolerates that padding. */
static struct slice raw_value(struct slice text, const char *key)
{
    const char *p = text.p; int n;
    struct slice none = {0, 0};
    while (p < text.end) {
        if (*p == '"') {
            const char *q = p + 1; n = 0;
            while (q < text.end && key[n] && *q == key[n]) { q++; n++; }
            if (!key[n] && q < text.end && *q == '"') {
                q = skip_ws(q + 1, text.end);
                if (q < text.end && *q++ == ':') {
                    q = skip_ws(q, text.end);
                    { struct slice out = {q, skip_json(q, text.end)}; return out; }
                }
            }
        }
        p++;
    }
    return none;
}

static struct slice array_item(struct slice array, int want)
{
    const char *p; int i = 0;
    struct slice none = {0, 0};
    if (!array.p || array.end <= array.p + 1 || *array.p != '[') return none;
    p = skip_ws(array.p + 1, array.end - 1);
    while (p < array.end - 1) {
        const char *e = skip_json(p, array.end);
        if (i++ == want) { struct slice out = {p, e}; return out; }
        p = skip_ws(e, array.end);
        if (p < array.end && *p == ',') p = skip_ws(p + 1, array.end); else break;
    }
    return none;
}

static int json_int(struct slice s, int fallback)
{
    int sign = 1, v = 0; const char *p;
    if (!s.p) return fallback;
    p = skip_ws(s.p, s.end);
    if (p >= s.end) return fallback;
    if (*p == '-') { sign = -1; p++; }
    if (p >= s.end || *p < '0' || *p > '9') return fallback;
    while (p < s.end && *p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    return v * sign;
}

/* Parse a non-negative JSON decimal into thousandths. This is sufficient for
 * glTF material factors and avoids pulling floating-point parsing into runtime. */
static int json_fixed(struct slice s, int fallback)
{
    const char *p; int sign = 1, whole = 0, frac = 0, digits = 0;
    if (!s.p) return fallback;
    p = skip_ws(s.p, s.end); if (p >= s.end) return fallback;
    if (*p == '-') { sign = -1; p++; }
    if (p >= s.end || (((*p < '0') || (*p > '9')) && *p != '.')) return fallback;
    while (p < s.end && *p >= '0' && *p <= '9') whole = whole * 10 + (*p++ - '0');
    if (p < s.end && *p == '.') p++;
    while (p < s.end && *p >= '0' && *p <= '9') { if (digits < 3) frac = frac * 10 + (*p - '0'); digits++; p++; }
    while (digits < 3) { frac *= 10; digits++; }
    return sign * (whole * 1000 + frac);
}

static int json_type(struct slice s)
{
    const char *p = s.p ? skip_ws(s.p, s.end) : 0;
    if (!p || p >= s.end || *p != '"') return 0;
    if (p[1] == 'V' && p[2] == 'E' && p[3] == 'C' && p[4] == '2') return 2;
    if (p[1] == 'V' && p[2] == 'E' && p[3] == 'C' && p[4] == '3') return 3;
    if (p[1] == 'S' && p[2] == 'C' && p[3] == 'A' && p[4] == 'L') return 1;
    return 0;
}

static unsigned int u32(const unsigned char *p)
{ return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24; }
static unsigned short u16(const unsigned char *p) { return p[0] | p[1] << 8; }
static float f32(const unsigned char *p)
{ unsigned int u = u32(p); union { unsigned int u; float f; } bits; bits.u = u; return bits.f; }
static int clamp_i(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int f_to_i(float f, int scale) { return (int)(f * scale + (f < 0 ? -0.5f : 0.5f)); }

/* GLTF baseColorFactor is linear-light while the framebuffer is sRGB-like. */
static int linear_to_srgb8(int linear_milli)
{
    int target, x = 0;
    linear_milli = clamp_i(linear_milli, 0, 1000);
    target = linear_milli * 65025 / 1000;
    while ((x + 1) * (x + 1) <= target) x++;
    return x;
}

static int write_all(int fd, const void *buf, int len)
{
    const char *p = (const char *)buf; int n;
    while (len) { n = __write(fd, p, len); if (n <= 0) return -1; p += n; len -= n; }
    return 0;
}

static void put_u16(unsigned char *p, unsigned int v)
{ p[0] = v; p[1] = v >> 8; }
static void put_u32(unsigned char *p, unsigned int v)
{ p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static struct material read_material(struct slice materials, int index)
{
    struct material out = {0xFFFFFFFFU, 0, 65535};
    struct slice m, pbr, color;
    int r, g, b, a, fixed;
    m = array_item(materials, index); if (!m.p) return out;
    pbr = raw_value(m, "pbrMetallicRoughness");
    color = raw_value(pbr, "baseColorFactor");
    r = json_fixed(array_item(color, 0), 1000);
    g = json_fixed(array_item(color, 1), 1000);
    b = json_fixed(array_item(color, 2), 1000);
    a = json_fixed(array_item(color, 3), 1000);
    out.color = (unsigned int)linear_to_srgb8(r) << 16 |
                (unsigned int)linear_to_srgb8(g) << 8 |
                (unsigned int)linear_to_srgb8(b);
    fixed = json_fixed(raw_value(pbr, "metallicFactor"), 0);
    out.metallic = clamp_i(fixed * 65535 / 1000, 0, 65535);
    fixed = json_fixed(raw_value(pbr, "roughnessFactor"), 1000);
    out.roughness = clamp_i(fixed * 65535 / 1000, 0, 65535);
    (void)a;
    return out;
}

static int read_accessor(struct slice accessors, int index, struct accessor *a)
{
    struct slice s = array_item(accessors, index);
    if (!s.p) return -1;
    a->view = json_int(raw_value(s, "bufferView"), -1);
    a->offset = json_int(raw_value(s, "byteOffset"), 0);
    a->count = json_int(raw_value(s, "count"), 0);
    a->component = json_int(raw_value(s, "componentType"), 0);
    a->type = json_type(raw_value(s, "type"));
    return a->view >= 0 ? 0 : -1;
}

static int read_view(struct slice views, int index, struct view *v)
{
    struct slice s = array_item(views, index);
    if (!s.p) return -1;
    v->offset = json_int(raw_value(s, "byteOffset"), 0);
    v->length = json_int(raw_value(s, "byteLength"), 0);
    v->stride = json_int(raw_value(s, "byteStride"), 0);
    return 0;
}

static int read_part(struct slice primitive, struct slice accessors,
                     struct slice views, struct part *part)
{
    struct slice attrs = raw_value(primitive, "attributes"), s;
    int pos_index, index_index;
    if (!attrs.p) return -1;
    pos_index = json_int(raw_value(attrs, "POSITION"), -1);
    index_index = json_int(raw_value(primitive, "indices"), -1);
    if (pos_index < 0 || index_index < 0 || read_accessor(accessors, pos_index, &part->pos) < 0 || read_accessor(accessors, index_index, &part->index) < 0) return -1;
    part->material = json_int(raw_value(primitive, "material"), 0);
    part->normal.view = -1; part->uv.view = -1;
    s = raw_value(attrs, "NORMAL"); if (s.p) { int n = json_int(s, -1); if (read_accessor(accessors, n, &part->normal) < 0) return -1; }
    s = raw_value(attrs, "TEXCOORD_0"); if (s.p) { int n = json_int(s, -1); if (read_accessor(accessors, n, &part->uv) < 0) return -1; }
    if (part->pos.component != 5126 || part->pos.type != 3 || part->index.type != 1 || part->pos.count <= 0 || part->index.count <= 0 || part->index.count % 3) return -1;
    if (part->index.component != 5121 && part->index.component != 5123 && part->index.component != 5125) return -1;
    if (read_view(views, part->pos.view, &part->pos_view) < 0 || read_view(views, part->index.view, &part->index_view) < 0) return -1;
    part->normal_view.offset = part->normal_view.length = part->normal_view.stride = 0;
    part->uv_view.offset = part->uv_view.length = part->uv_view.stride = 0;
    if (part->normal.view >= 0 && (part->normal.component != 5126 || part->normal.type != 3 || read_view(views, part->normal.view, &part->normal_view) < 0)) return -1;
    if (part->uv.view >= 0 && (part->uv.component != 5126 || part->uv.type != 2 || read_view(views, part->uv.view, &part->uv_view) < 0)) return -1;
    return 0;
}

static int part_in_bounds(struct part *p, int bin_len)
{
    int ps = p->pos_view.stride ? p->pos_view.stride : 12;
    int is = p->index.component == 5121 ? 1 : p->index.component == 5123 ? 2 : 4;
    int po = p->pos_view.offset + p->pos.offset;
    int io = p->index_view.offset + p->index.offset;
    if (po < 0 || io < 0 || po + (p->pos.count - 1) * ps + 12 > bin_len || io + p->index.count * is > bin_len) return 0;
    if (p->normal.view >= 0) { int s = p->normal_view.stride ? p->normal_view.stride : 12; int o = p->normal_view.offset + p->normal.offset; if (o < 0 || o + (p->normal.count - 1) * s + 12 > bin_len) return 0; }
    if (p->uv.view >= 0) { int s = p->uv_view.stride ? p->uv_view.stride : 8; int o = p->uv_view.offset + p->uv.offset; if (o < 0 || o + (p->uv.count - 1) * s + 8 > bin_len) return 0; }
    return 1;
}

int main(int argc, char **argv)
{
    int fd, out, size, json_len, bin_len, bin_off, i, part_count = 0;
    int vertex_count = 0, index_count = 0;
    int position_scale = 232;
    int minx = 2147483647, miny = 2147483647, minz = 2147483647;
    int maxx = -2147483647, maxy = -2147483647, maxz = -2147483647;
    float raw_minx = 2147483647.0f, raw_miny = 2147483647.0f, raw_minz = 2147483647.0f;
    float raw_maxx = -2147483647.0f, raw_maxy = -2147483647.0f, raw_maxz = -2147483647.0f;
    unsigned char *file, *json, *bin, header[RASTERFALL_MODEL_HEADER_BYTES];
    struct stat st; struct slice root, meshes, accessors, views, materials_json, mesh, primitives, primitive;
    struct part parts[MAX_PARTS];
    struct material materials[MAX_MATERIALS];
    int material_count;
    if (argc != 3) { __printf("usage: glb2rmesh input.glb output.rmesh\n"); return 2; }
    fd = __openat(AT_FDCWD, argv[1], O_RDONLY, 0);
    if (fd < 0 || __fstat(fd, &st) < 0 || st.st_size < 20 || st.st_size > 64 * 1024 * 1024) { __printf("glb2rmesh: cannot open input\n"); return 1; }
    size = (int)st.st_size; file = (unsigned char *)__mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0); __close(fd);
    if (file == MAP_FAILED || u32(file) != GLB_MAGIC || u32(file + 4) != 2 || u32(file + 8) != (unsigned)size) { __printf("glb2rmesh: invalid GLB header\n"); return 1; }
    json_len = u32(file + 12); if (u32(file + 16) != GLB_JSON) { __printf("glb2rmesh: first chunk is not JSON\n"); return 1; }
    bin_off = 20 + json_len; if (bin_off + 8 > size || u32(file + bin_off + 4) != GLB_BIN) { __printf("glb2rmesh: missing BIN chunk\n"); return 1; }
    bin_len = u32(file + bin_off); bin = file + bin_off + 8; json = file + 20;
    root.p = (const char *)json; root.end = (const char *)json + json_len;
    meshes = raw_value(root, "meshes"); accessors = raw_value(root, "accessors"); views = raw_value(root, "bufferViews"); materials_json = raw_value(root, "materials"); mesh = array_item(meshes, 0); primitives = raw_value(mesh, "primitives");
    if (!accessors.p || !views.p || !primitives.p) { __printf("glb2rmesh: malformed mesh tables\n"); return 1; }
    for (i = 0; i < MAX_PARTS; i++) {
        primitive = array_item(primitives, i); if (!primitive.p) break;
        if (read_part(primitive, accessors, views, &parts[part_count]) < 0 || !part_in_bounds(&parts[part_count], bin_len)) { __printf("glb2rmesh: unsupported or truncated primitive %d\n", i); return 1; }
        parts[part_count].vertex_base = vertex_count; parts[part_count].index_base = index_count;
        vertex_count += parts[part_count].pos.count; index_count += parts[part_count].index.count; part_count++;
    }
    if (!part_count || i == MAX_PARTS) { __printf("glb2rmesh: no mesh primitive or too many primitives\n"); return 1; }
    material_count = 0;
    for (i = 0; i < MAX_MATERIALS; i++) {
        if (!array_item(materials_json, i).p) break;
        materials[i] = read_material(materials_json, i); material_count++;
    }
    if (!material_count) { material_count = 1; materials[0].color = 0xB0B0B0; materials[0].metallic = 0; materials[0].roughness = 65535; }
    for (i = 0; i < part_count; i++) if (parts[i].material < 0 || parts[i].material >= material_count) parts[i].material = 0;
    for (i = 0; i < part_count; i++) { struct part *p = &parts[i]; int stride = p->pos_view.stride ? p->pos_view.stride : 12; int off = p->pos_view.offset + p->pos.offset; int j; for (j = 0; j < p->pos.count; j++) { const unsigned char *v = bin + off + j * stride; float x=f32(v), y=f32(v+4), z=f32(v+8); int sx=f_to_i(x,position_scale), sy=f_to_i(y,position_scale), sz=f_to_i(z,position_scale); if(x<raw_minx)raw_minx=x;if(y<raw_miny)raw_miny=y;if(z<raw_minz)raw_minz=z;if(x>raw_maxx)raw_maxx=x;if(y>raw_maxy)raw_maxy=y;if(z>raw_maxz)raw_maxz=z; if(sx<minx)minx=sx;if(sy<miny)miny=sy;if(sz<minz)minz=sz;if(sx>maxx)maxx=sx;if(sy>maxy)maxy=sy;if(sz>maxz)maxz=sz; } }
    {
        float dx = raw_maxx - raw_minx, dy = raw_maxy - raw_miny, dz = raw_maxz - raw_minz;
        float extent = dx > dy ? dx : dy;
        if (dz > extent) extent = dz;
        if (extent > 0.0f && extent * position_scale < 64.0f) {
            position_scale = (int)(900.0f / extent + 0.5f);
            minx = miny = minz = 2147483647;
            maxx = maxy = maxz = -2147483647;
            for (i = 0; i < part_count; i++) { struct part *p = &parts[i]; int stride = p->pos_view.stride ? p->pos_view.stride : 12; int off = p->pos_view.offset + p->pos.offset; int j; for (j = 0; j < p->pos.count; j++) { const unsigned char *v = bin + off + j * stride; int x=f_to_i(f32(v),position_scale), y=f_to_i(f32(v+4),position_scale), z=f_to_i(f32(v+8),position_scale); if(x<minx)minx=x;if(y<miny)miny=y;if(z<minz)minz=z;if(x>maxx)maxx=x;if(y>maxy)maxy=y;if(z>maxz)maxz=z; } }
        }
    }
    out = __openat(AT_FDCWD, argv[2], O_WRONLY|O_CREAT|O_TRUNC, 0644); if (out < 0) { __printf("glb2rmesh: cannot create output\n"); return 1; }
    __memset(header, 0, sizeof(header)); header[0]='R'; header[1]='F'; header[2]='M'; header[3]='2'; header[4]=2; put_u32(header+8, vertex_count); put_u32(header+12, index_count); put_u32(header+16, position_scale); ((int *)(header+20))[0]=minx; ((int *)(header+20))[1]=miny; ((int *)(header+20))[2]=minz; ((int *)(header+20))[3]=maxx; ((int *)(header+20))[4]=maxy; ((int *)(header+20))[5]=maxz; put_u32(header+44, part_count); put_u32(header+48, material_count); put_u32(header+52, RASTERFALL_MODEL_HEADER_BYTES); put_u32(header+56, RASTERFALL_MODEL_HEADER_BYTES + part_count * RASTERFALL_MODEL_PRIMITIVE_BYTES);
    if (write_all(out, header, RASTERFALL_MODEL_HEADER_BYTES) < 0) { __close(out); return 1; }
    for (i = 0; i < part_count; i++) { unsigned char record[RASTERFALL_MODEL_PRIMITIVE_BYTES]; __memset(record, 0, sizeof(record)); put_u32(record, parts[i].index_base); put_u32(record+4, parts[i].index.count); put_u32(record+8, parts[i].material); if (write_all(out, record, sizeof(record)) < 0) { __close(out); return 1; } }
    for (i = 0; i < material_count; i++) { unsigned char record[RASTERFALL_MODEL_MATERIAL_BYTES_LEGACY]; __memset(record, 0, sizeof(record)); put_u32(record, materials[i].color); put_u16(record+4, materials[i].metallic); put_u16(record+6, materials[i].roughness); put_u32(record+8, 0xffffffffU); if (write_all(out, record, sizeof(record)) < 0) { __close(out); return 1; } }
    for (i = 0; i < part_count; i++) { struct part *p=&parts[i]; int ps=p->pos_view.stride?p->pos_view.stride:12, ns=p->normal_view.stride?p->normal_view.stride:12, us=p->uv_view.stride?p->uv_view.stride:8, po=p->pos_view.offset+p->pos.offset, no=p->normal_view.offset+p->normal.offset, uo=p->uv_view.offset+p->uv.offset, j; for(j=0;j<p->pos.count;j++){ const unsigned char *q=bin+po+j*ps; unsigned char v[RASTERFALL_MODEL_VERTEX_BYTES]; __memset(v,0,sizeof(v)); *(int *)(v)=f_to_i(f32(q),position_scale);*(int *)(v+4)=f_to_i(f32(q+4),position_scale);*(int *)(v+8)=f_to_i(f32(q+8),position_scale); if(p->normal.view>=0&&j<p->normal.count){q=bin+no+j*ns;*(short*)(v+12)=clamp_i(f_to_i(f32(q),32767),-32767,32767);*(short*)(v+14)=clamp_i(f_to_i(f32(q+4),32767),-32767,32767);*(short*)(v+16)=clamp_i(f_to_i(f32(q+8),32767),-32767,32767);} if(p->uv.view>=0&&j<p->uv.count){q=bin+uo+j*us;*(unsigned short*)(v+18)=clamp_i(f_to_i(f32(q),65535),0,65535);*(unsigned short*)(v+20)=clamp_i(f_to_i(f32(q+4),65535),0,65535);} if(write_all(out,v,RASTERFALL_MODEL_VERTEX_BYTES)<0){__close(out);return 1;} } }
    for (i = 0; i < part_count; i++) { struct part *p=&parts[i]; int is=p->index.component==5121?1:p->index.component==5123?2:4, io=p->index_view.offset+p->index.offset, j; for(j=0;j<p->index.count;j++){ const unsigned char *q=bin+io+j*is; unsigned int n=p->index.component==5121?q[0]:p->index.component==5123?u16(q):u32(q); unsigned char b[4]; if(n>=(unsigned)p->pos.count){__printf("glb2rmesh: index out of range in primitive %d\n",i);__close(out);return 1;} n += p->vertex_base; b[0]=n;b[1]=n>>8;b[2]=n>>16;b[3]=n>>24; if(write_all(out,b,4)<0){__close(out);return 1;} } }
    __close(out); __munmap(file,size); __printf("glb2rmesh: %s -> %s (%d vertices, %d triangles, %d primitives, %d materials)\n",argv[1],argv[2],vertex_count,index_count/3,part_count,material_count); return 0;
}
