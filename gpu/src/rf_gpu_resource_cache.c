#include "rf_gpu_resource_cache.h"
#include <stdlib.h>
#include <string.h>

struct cache_entry {
    struct cache_entry *next;
    struct rasterfall_resource_handle handle;
    uint32_t primitive, texture;
    struct rf_gpu_cached_submesh info;
    struct rf_gpu_graphics_resource *resource;
};
struct rf_gpu_resource_cache {
    struct rf_gpu_graphics *graphics;
    const struct rasterfall_resource_registry *registry;
    struct cache_entry *slots[RASTERFALL_RESOURCE_CAPACITY];
    struct rf_gpu_resource_cache_stats stats;
};

static uint32_t cache_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 |
        (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static int32_t cache_i32(const unsigned char *p)
{
    uint32_t v = cache_u32(p);
    return v <= INT32_MAX ? (int32_t)v : -1 - (int32_t)(UINT32_MAX-v);
}
static int32_t cache_i16(const unsigned char *p)
{
    uint32_t v = (uint32_t)p[0] | (uint32_t)p[1]<<8;
    return v <= INT16_MAX ? (int32_t)v : (int32_t)v-65536;
}
static int cache_range(const struct rasterfall_model_asset *m,
    const unsigned char *p, uint64_t bytes)
{
    uintptr_t base = (uintptr_t)m->data, address = (uintptr_t)p;
    return m->data && p && m->data_size > 0 && address >= base &&
        address-base <= (uintptr_t)m->data_size &&
        bytes <= (uint64_t)m->data_size-(address-base);
}
static const struct rasterfall_model_asset *cache_model(
    const struct rf_gpu_resource_cache *c, unsigned long long epoch,
    struct rasterfall_resource_handle h)
{
    if (!c || !epoch || !c->registry->frame_active ||
        epoch != c->registry->frame_epoch || h.slot >= RASTERFALL_RESOURCE_CAPACITY ||
        !c->registry->slots[h.slot].pinned) return NULL;
    return rasterfall_resources_resolve(c->registry, h);
}
static struct cache_entry *cache_find(struct rf_gpu_resource_cache *c,
    struct rasterfall_resource_handle h, uint32_t primitive, uint32_t texture)
{
    struct cache_entry *e;
    for (e = c->slots[h.slot]; e; e = e->next)
        if (e->handle.generation == h.generation && e->primitive == primitive &&
            e->texture == texture) return e;
    return NULL;
}
struct rf_gpu_resource_cache *rf_gpu_resource_cache_create(
    struct rf_gpu_graphics *graphics, const struct rasterfall_resource_registry *registry)
{
    struct rf_gpu_resource_cache *c;
    if (!graphics || !registry) return NULL;
    c = calloc(1, sizeof(*c));
    if (c) { c->graphics = graphics; c->registry = registry; }
    return c;
}
static void cache_release(struct rf_gpu_resource_cache *c, struct cache_entry *e)
{
    rf_gpu_graphics_resource_destroy(c->graphics, e->resource);
    c->stats.releases++;
    c->stats.entries--;
    free(e);
}
void rf_gpu_resource_cache_collect(struct rf_gpu_resource_cache *c)
{
    if (!c) return;
    for (unsigned i = 0; i < RASTERFALL_RESOURCE_CAPACITY; ++i) {
        struct cache_entry **link = &c->slots[i];
        while (*link) {
            struct cache_entry *e = *link;
            if (rasterfall_resources_resolve(c->registry, e->handle)) {
                link = &e->next;
            } else {
                *link = e->next;
                cache_release(c, e);
            }
        }
    }
}
int rf_gpu_resource_cache_prepare(struct rf_gpu_resource_cache *c,
    unsigned long long epoch, struct rasterfall_resource_handle h,
    uint32_t primitive, uint32_t texture, struct rf_gpu_cached_submesh *info)
{
    const struct rasterfall_model_asset *m = cache_model(c, epoch, h);
    const struct toy_texture_view *t = NULL;
    struct rf_gpu_graphics_vertex *vertices = NULL;
    uint32_t *indices = NULL, *texels = NULL, white = 0xffffff;
    uint32_t first, count, width = 1, height = 1;
    struct cache_entry *e;
    if (info) memset(info, 0, sizeof(*info));
    if (!m || !info) return -1;
    e = cache_find(c, h, primitive, texture);
    if (e) { *info = e->info; c->stats.hits++; return 0; }
    if (c->stats.entries >= RF_GPU_CACHE_CAPACITY) return -1;
    if (m->bone_count || m->skinning_enabled || m->has_character_contract ||
        primitive >= m->primitive_count || m->vertex_bytes < 24 ||
        !cache_range(m, m->primitives, (uint64_t)m->primitive_count*16) ||
        !cache_range(m, m->indices, (uint64_t)m->index_count*4) ||
        !cache_range(m, m->vertices, (uint64_t)m->vertex_count*m->vertex_bytes)) return -1;
    first = cache_u32(m->primitives+(size_t)primitive*16);
    count = cache_u32(m->primitives+(size_t)primitive*16+4);
    /* Corner expansion is bounded by the existing graphics upload contract. */
    if (first % 3 || !count || count % 3 || count > 65536 ||
        first > m->index_count || count > m->index_count-first) return -1;
    if (texture != RF_GPU_CACHE_FLAT_TEXTURE) {
        if (texture >= m->textures.count || !m->textures.views) return -1;
        t = &m->textures.views[texture];
        width = t->width; height = t->height;
        if (!t->data || !width || !height || width > 1024 || height > 1024 ||
            (t->channels != 3 && t->channels != 4) || t->has_transparency ||
            (uint64_t)width*height*t->channels > t->data_size) return -1;
    }
    vertices = calloc(count, sizeof(*vertices));
    indices = malloc((size_t)count*sizeof(*indices));
    texels = malloc((size_t)width*height*sizeof(*texels));
    e = calloc(1, sizeof(*e));
    if (!vertices || !indices || !texels || !e) goto failed;
    for (uint32_t i = 0; i < count; i += 3) {
        const unsigned char *source[3];
        for (unsigned j = 0; j < 3; ++j) {
            uint32_t id = cache_u32(m->indices+(size_t)(first+i+j)*4);
            if (id >= m->vertex_count) goto failed;
            source[j] = m->vertices+(size_t)id*m->vertex_bytes;
        }
        for (unsigned j = 0; j < 3; ++j) {
            struct rf_gpu_graphics_vertex *v = &vertices[i+j];
            for (unsigned axis = 0; axis < 3; ++axis) {
                v->position[axis] = cache_i32(source[j]+axis*4);
                for (unsigned n = 0; n < 3; ++n)
                    v->normals[n*3+axis] = cache_i16(source[n]+12+axis*2);
            }
            v->uv[0] = source[j][18] | (uint32_t)source[j][19]<<8;
            v->uv[1] = source[j][20] | (uint32_t)source[j][21]<<8;
            indices[i+j] = i+j;
        }
    }
    for (uint32_t i = 0; i < width*height; ++i) {
        const unsigned char *pixel = t ? t->data+(size_t)i*t->channels : NULL;
        if (t && t->channels == 4 && pixel[3] != 255) goto failed;
        texels[i] = pixel ? (uint32_t)pixel[0]<<16 | (uint32_t)pixel[1]<<8 | pixel[2] : white;
    }
    e->resource = rf_gpu_graphics_resource_create(c->graphics, vertices, count,
        indices, count, texels, width, height);
    if (!e->resource) goto failed;
    e->handle = h; e->primitive = primitive; e->texture = texture;
    e->info.index_count = count; e->info.texture_width = width; e->info.texture_height = height;
    e->next = c->slots[h.slot]; c->slots[h.slot] = e;
    c->stats.uploads++; c->stats.entries++;
    *info = e->info;
    free(vertices); free(indices); free(texels);
    return 0;
failed:
    free(vertices); free(indices); free(texels); free(e);
    return -1;
}
int rf_gpu_resource_cache_bind(struct rf_gpu_resource_cache *c,
    unsigned long long epoch, struct rasterfall_resource_handle h,
    uint32_t primitive, uint32_t texture)
{
    struct cache_entry *e;
    if (!cache_model(c, epoch, h)) return -1;
    e = cache_find(c, h, primitive, texture);
    return e ? rf_gpu_graphics_resource_bind(c->graphics, e->resource) : -1;
}
struct rf_gpu_graphics_resource *rf_gpu_resource_cache_resource(
    struct rf_gpu_resource_cache *c, unsigned long long epoch,
    struct rasterfall_resource_handle h, uint32_t primitive, uint32_t texture)
{
    struct cache_entry *e;
    if (!cache_model(c, epoch, h)) return NULL;
    e = cache_find(c, h, primitive, texture);
    return e ? e->resource : NULL;
}
void rf_gpu_resource_cache_get_stats(const struct rf_gpu_resource_cache *c,
    struct rf_gpu_resource_cache_stats *stats)
{
    if (stats) { memset(stats, 0, sizeof(*stats)); if (c) *stats = c->stats; }
}
void rf_gpu_resource_cache_destroy(struct rf_gpu_resource_cache *c)
{
    if (!c) return;
    for (unsigned i = 0; i < RASTERFALL_RESOURCE_CAPACITY; ++i)
        while (c->slots[i]) {
            struct cache_entry *e = c->slots[i];
            c->slots[i] = e->next;
            cache_release(c, e);
        }
    free(c);
}
