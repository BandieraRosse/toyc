#include "core.h"
#include "tlibc_everything.h"
#include "toy_assets.h"
#include "rasterfall_model.h"

static unsigned int model_u32(const unsigned char *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
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
    if (!data || size < RASTERFALL_MODEL_HEADER_BYTES ||
        model_u32(data) != RASTERFALL_MODEL_MAGIC ||
        (version < 2 || version > RASTERFALL_MODEL_VERSION) ||
        model_u32(data + 8) > 1000000 || model_u32(data + 12) > 3000000 ||
        model_u32(data + 44) > 32 || model_u32(data + 48) > 32 ||
        model_u32(data + 52) != RASTERFALL_MODEL_HEADER_BYTES ||
        model_u32(data + 56) != RASTERFALL_MODEL_HEADER_BYTES +
            model_u32(data + 44) * RASTERFALL_MODEL_PRIMITIVE_BYTES ||
        (long)model_u32(data + 56) +
            (long)model_u32(data + 48) * material_bytes +
            (long)model_u32(data + 8) * vertex_bytes +
            (unsigned long)model_u32(data + 12) * 4 != (unsigned long)size) {
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
    tlibc_free((void *)asset->data);
    __memset(asset, 0, sizeof(*asset));
}
