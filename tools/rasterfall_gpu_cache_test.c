/* Hosted integration: real registry lifecycle, device uploads and pixel output.
 * Synthetic RFM2 backing supplies independent geometry/texture oracles; real
 * loader reuse checks generation and an authored multi-submesh asset. */
#include "tlibc_everything.h"
#include "rf_gpu_resource_cache.h"
#include "rasterfall_prop.h"

static void put32(unsigned char *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (unsigned char)(v>>(i*8));
}
static int fixture(struct rasterfall_resource_registry *r, unsigned slot,
    struct rasterfall_resource_handle *h, unsigned char red)
{
    struct rasterfall_model_asset *m = tlibc_malloc(sizeof(*m));
    unsigned char *data;
    const int positions[3][3] = {{-128,-128,0},{128,-128,0},{0,128,0}};
    if (!m) return -1;
    memset(m, 0, sizeof(*m));
    r->slots[slot].model = m; r->slots[slot].active = 1;
    r->slots[slot].generation = 1;
    h->slot = slot; h->generation = 1;
    data = tlibc_malloc(100);
    if (!data) return -1;
    memset(data, 0, 100);
    m->data = data; m->data_size = 100;
    m->primitives = data; m->primitive_count = 1;
    m->indices = data+16; m->index_count = 3;
    m->vertices = data+28; m->vertex_count = 3; m->vertex_bytes = 24;
    m->format_version = 2; m->position_scale = 1000;
    put32(data+4, 3);
    for (unsigned i = 0; i < 3; ++i) {
        put32(data+16+i*4, i);
        for (unsigned j = 0; j < 3; ++j) put32(data+28+i*24+j*4, (uint32_t)positions[i][j]);
        data[28+i*24+14] = 255; data[28+i*24+15] = 127;
    }
    m->textures.assets = tlibc_malloc(sizeof(*m->textures.assets));
    m->textures.views = tlibc_malloc(sizeof(*m->textures.views));
    if (m->textures.assets) memset(m->textures.assets, 0, sizeof(*m->textures.assets));
    if (!m->textures.assets || !m->textures.views) return -1;
    m->textures.count = 1;
    memset(m->textures.views, 0, sizeof(*m->textures.views));
    data = tlibc_malloc(4);
    if (!data) return -1;
    data[0] = red; data[1] = 37; data[2] = 91; data[3] = 255;
    m->textures.assets[0].data = data;
    m->textures.views[0].data = data; m->textures.views[0].data_size = 4;
    m->textures.views[0].channels = 4;
    m->textures.views[0].width = m->textures.views[0].height = 1;
    return 0;
}
static struct rf_gpu_graphics_draw draw(unsigned width, unsigned height)
{
    struct rf_gpu_graphics_draw d;
    memset(&d, 0, sizeof(d));
    d.translation_scale[2] = 256; d.translation_scale[3] = 1000;
    d.rotation[1] = d.view[1] = d.view[3] = 1024;
    d.projection[0] = width; d.projection[1] = height;
    d.projection[2] = 64; d.projection[3] = width*3/4;
    d.material[0] = 0xffffff; d.material[1] = 256; d.material[2] = 1;
    d.texture[0] = d.texture[1] = 1;
    d.index_count = 3; d.double_sided = 1; d.integer_depth = 1;
    return d;
}
int main(void)
{
    struct rf_gpu gpu;
    struct rf_gpu_vulkan_context context;
    struct rf_gpu_graphics *g = NULL;
    struct rf_gpu_resource_cache *cache = NULL;
    struct rasterfall_resource_registry *r = tlibc_malloc(sizeof(*r));
    struct rasterfall_resource_handle a, b, loaded;
    struct rf_gpu_cached_submesh info;
    struct rf_gpu_resource_cache_stats stats;
    struct rf_gpu_graphics_stats before, after;
    struct rf_gpu_graphics_draw d = draw(64, 48);
    uint32_t pixels[80*60]; float depth[80*60];
    unsigned long long epoch;
    int failure = 0, initialized = 0;
    const struct rasterfall_prop_asset_profile *profile =
        rasterfall_prop_asset_profile(RASTERFALL_PROP_ASSET_ARCH_BEAM);
#define CHECK(x) do { if (!(x)) { __printf("FAIL line %d: %s\n", __LINE__, #x); failure = __LINE__; goto done; } } while (0)
    if (r) memset(r, 0, sizeof(*r));
    memset(&context, 0, sizeof(context)); context.require_graphics = 1;
    CHECK(r && profile && fixture(r, 0, &a, 211) == 0 && fixture(r, 1, &b, 53) == 0);
    CHECK(rf_gpu_init(&gpu, RF_GPU_POLICY_REQUIRED, &rf_gpu_vulkan_backend, &context) == 0);
    initialized = 1;
    __printf("HG-2B adapter=%s vendor=%x device=%x type=%u queue=%u\n", gpu.info.adapter_name,
        gpu.info.vendor_id, gpu.info.device_id, gpu.info.adapter_type, gpu.info.queue_family);
    CHECK((g = rf_gpu_graphics_create(&context)) != NULL);
    CHECK((cache = rf_gpu_resource_cache_create(g, r)) != NULL);
    CHECK(rf_gpu_graphics_resize(g, 64, 48) == 0);
    CHECK(rf_gpu_resource_cache_prepare(cache, 0, a, 0, 0, &info) < 0);
    CHECK(rasterfall_resources_frame_begin(r) == 0); epoch = r->frame_epoch;
    CHECK(rf_gpu_resource_cache_prepare(cache, epoch, a, 0, 0, &info) < 0);
    CHECK(rasterfall_resources_pin(r, a) == 0 && rasterfall_resources_pin(r, b) == 0);
    CHECK(rf_gpu_resource_cache_bind(cache, epoch, a, 0, 0) < 0);
    CHECK(rf_gpu_resource_cache_prepare(cache, epoch, a, 0, 0, &info) == 0);
    CHECK(info.index_count == 3 && info.texture_width == 1 && info.texture_height == 1);
    CHECK(rf_gpu_resource_cache_bind(cache, epoch, a, 0, 0) == 0);
    CHECK(rf_gpu_graphics_render(g, &d, 1, pixels, depth, 80*60) == 0);
    CHECK(pixels[24*64+32] == 0xff5b25d3 && depth[24*64+32] == 0.25f);
    /* Reject malformed backing before uploads, preserving a valid binding. */
    rf_gpu_graphics_get_stats(g, &before);
    {
        struct rasterfall_model_asset *bad = r->slots[b.slot].model;
        unsigned char *index = (unsigned char *)bad->indices;
        unsigned char *texel = (unsigned char *)bad->textures.views[0].data;
        put32(index, 99);
        CHECK(rf_gpu_resource_cache_prepare(cache, epoch, b, 0, 0, &info) < 0);
        put32(index, 0); texel[3] = 127;
        CHECK(rf_gpu_resource_cache_prepare(cache, epoch, b, 0, 0, &info) < 0);
        texel[3] = 255; bad->textures.views[0].data_size = 3;
        CHECK(rf_gpu_resource_cache_prepare(cache, epoch, b, 0, 0, &info) < 0);
        bad->textures.views[0].data_size = 4;
        put32((unsigned char *)bad->vertices, 32768);
        CHECK(rf_gpu_resource_cache_prepare(cache, epoch, b, 0, 0, &info) < 0);
        put32((unsigned char *)bad->vertices, (uint32_t)-128);
        bad->data_size--;
        CHECK(rf_gpu_resource_cache_prepare(cache, epoch, b, 0, 0, &info) < 0);
        bad->data_size++;
        bad->bone_count = 1;
        CHECK(rf_gpu_resource_cache_prepare(cache, epoch, b, 0, 0, &info) < 0);
        bad->bone_count = 0;
    }
    rf_gpu_graphics_get_stats(g, &after);
    CHECK(before.mesh_upload_bytes == after.mesh_upload_bytes &&
        before.texture_upload_bytes == after.texture_upload_bytes);
    CHECK(rf_gpu_resource_cache_prepare(cache, epoch, b, 0, 0, &info) == 0);
    /* Preparing another resource must leave current binding untouched. */
    CHECK(rf_gpu_graphics_render(g, &d, 1, pixels, depth, 80*60) == 0);
    CHECK(pixels[24*64+32] == 0xff5b25d3);
    CHECK(rf_gpu_resource_cache_bind(cache, epoch, b, 0, 0) == 0);
    CHECK(rf_gpu_graphics_continue(g, &d, 1, pixels, depth, 80*60) == 0);
    CHECK(pixels[24*64+32] == 0xff5b2535 && depth[24*64+32] == 0.25f);
    CHECK(rf_gpu_resource_cache_prepare(cache, epoch, b, 0, RF_GPU_CACHE_FLAT_TEXTURE, &info) == 0);
    CHECK(rf_gpu_resource_cache_bind(cache, epoch, b, 0, RF_GPU_CACHE_FLAT_TEXTURE) == 0);
    d.material[2] = 0; d.material[0] = 0x13579b;
    CHECK(rf_gpu_graphics_render(g, &d, 1, pixels, depth, 80*60) == 0);
    CHECK(pixels[24*64+32] == 0xff9b5713);
    d = draw(64, 48);
    rf_gpu_graphics_get_stats(g, &before);
    for (unsigned i = 0; i < 8; ++i) {
        struct rasterfall_resource_handle h = (i & 1) ? a : b;
        CHECK(rf_gpu_resource_cache_prepare(cache, epoch, h, 0, 0, &info) == 0);
        CHECK(rf_gpu_resource_cache_bind(cache, epoch, h, 0, 0) == 0);
        CHECK(rf_gpu_graphics_render(g, &d, 1, pixels, depth, 80*60) == 0);
        CHECK(pixels[24*64+32] == ((i & 1) ? 0xff5b25d3U : 0xff5b2535U));
    }
    CHECK(rf_gpu_graphics_resize(g, 80, 60) == 0); d = draw(80, 60);
    CHECK(rf_gpu_resource_cache_bind(cache, epoch, a, 0, 0) == 0);
    CHECK(rf_gpu_graphics_render(g, &d, 1, pixels, depth, 80*60) == 0);
    CHECK(pixels[30*80+40] == 0xff5b25d3);
    rf_gpu_graphics_get_stats(g, &after);
    CHECK(after.mesh_upload_bytes == before.mesh_upload_bytes &&
        after.texture_upload_bytes == before.texture_upload_bytes &&
        after.target_builds == before.target_builds+1);
    CHECK(rf_gpu_resource_cache_prepare(cache, epoch, a, 1, 0, &info) < 0);
    CHECK(rf_gpu_resource_cache_prepare(cache, epoch, a, 0, 1, &info) < 0);
    rasterfall_resources_frame_complete(r);
    CHECK(rf_gpu_resource_cache_bind(cache, epoch, a, 0, 0) < 0);
    CHECK(rasterfall_resources_frame_begin(r) == 0);
    CHECK(rasterfall_resources_pin(r, a) == 0 && rasterfall_resources_pin(r, b) == 0);
    CHECK(rf_gpu_resource_cache_bind(cache, epoch, a, 0, 0) < 0);
    epoch = r->frame_epoch;
    CHECK(rf_gpu_resource_cache_bind(cache, epoch, a, 0, 0) == 0);
    rasterfall_resources_invalidate(r);
    rf_gpu_resource_cache_collect(cache);
    rf_gpu_resource_cache_get_stats(cache, &stats); CHECK(stats.entries == 3 && !stats.releases);
    CHECK(rf_gpu_resource_cache_bind(cache, epoch, a, 0, 0) == 0);
    CHECK(rf_gpu_graphics_render(g, &d, 1, pixels, depth, 80*60) == 0);
    CHECK(pixels[30*80+40] == 0xff5b25d3);
    rasterfall_resources_frame_complete(r);
    rf_gpu_resource_cache_collect(cache);
    rf_gpu_resource_cache_get_stats(cache, &stats); CHECK(!stats.entries && stats.releases == 3);
    CHECK(rf_gpu_graphics_render(g, &d, 1, pixels, depth, 80*60) < 0);
    CHECK(rasterfall_resources_load(r, profile->model_path, &loaded) == 0);
    CHECK(loaded.slot == a.slot && loaded.generation != a.generation);
    CHECK(rasterfall_resources_frame_begin(r) == 0); epoch = r->frame_epoch;
    CHECK(rasterfall_resources_pin(r, loaded) == 0);
    CHECK(rf_gpu_resource_cache_prepare(cache, epoch, a, 0, 0, &info) < 0);
    CHECK(rf_gpu_resource_cache_bind(cache, epoch, a, 0, 0) < 0);
    for (unsigned i = 0; i < r->slots[loaded.slot].model->primitive_count; ++i) {
        CHECK(rf_gpu_resource_cache_prepare(cache, epoch, loaded, i,
            RF_GPU_CACHE_FLAT_TEXTURE, &info) == 0);
        CHECK(rf_gpu_resource_cache_bind(cache, epoch, loaded, i, RF_GPU_CACHE_FLAT_TEXTURE) == 0);
        d.index_count = info.index_count; d.material[2] = 0;
        d.translation_scale[2] = 2048;
        CHECK(rf_gpu_graphics_render(g, &d, 1, pixels, depth, 80*60) == 0);
    }
    /* Device cache recreation leaves CPU definitions and pins unchanged. */
    rf_gpu_resource_cache_destroy(cache); cache = NULL;
    rf_gpu_graphics_destroy(g); g = NULL;
    CHECK((g = rf_gpu_graphics_create(&context)) != NULL);
    CHECK((cache = rf_gpu_resource_cache_create(g, r)) != NULL);
    CHECK(rf_gpu_resource_cache_prepare(cache, epoch, loaded, 0, RF_GPU_CACHE_FLAT_TEXTURE, &info) == 0);
    rf_gpu_resource_cache_get_stats(cache, &stats); CHECK(stats.uploads == 1 && stats.entries == 1);
done:
    rf_gpu_resource_cache_destroy(cache);
    rf_gpu_graphics_destroy(g);
    if (initialized) rf_gpu_shutdown(&gpu);
    rasterfall_resources_invalidate(r); rasterfall_resources_frame_complete(r);
    tlibc_free(r);
    __printf("registry-gpu-cache: %s line=%d\n", failure ? "FAIL" : "PASS", failure);
    return failure ? 1 : 0;
}
