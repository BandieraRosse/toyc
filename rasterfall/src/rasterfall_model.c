#include "core.h"
#include "tlibc_everything.h"
#include "toy_assets.h"
#include "rasterfall_model.h"

static unsigned int model_u32(const unsigned char *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
}

int rasterfall_model_load(struct rasterfall_model_asset *asset,
                          const char *path)
{
    uint32_t size;
    unsigned char *data;
    if (!asset || !path) return -1;
    __memset(asset, 0, sizeof(*asset));
    data = toy_asset_load_file(path, &size);
    if (!data || size < RASTERFALL_MODEL_HEADER_BYTES ||
        model_u32(data) != RASTERFALL_MODEL_MAGIC ||
        model_u32(data + 4) != RASTERFALL_MODEL_VERSION ||
        model_u32(data + 8) > 1000000 || model_u32(data + 12) > 3000000 ||
        model_u32(data + 44) > 32 || model_u32(data + 48) > 32 ||
        model_u32(data + 52) != RASTERFALL_MODEL_HEADER_BYTES ||
        model_u32(data + 56) != RASTERFALL_MODEL_HEADER_BYTES +
            model_u32(data + 44) * RASTERFALL_MODEL_PRIMITIVE_BYTES ||
        (long)model_u32(data + 56) +
            (long)model_u32(data + 48) * RASTERFALL_MODEL_MATERIAL_BYTES +
            (long)model_u32(data + 8) * RASTERFALL_MODEL_VERTEX_BYTES +
            (unsigned long)model_u32(data + 12) * 4 != (unsigned long)size) {
        if (data) tlibc_free(data);
        return -1;
    }
    asset->data = data;
    asset->data_size = size;
    asset->vertex_count = model_u32(data + 8);
    asset->index_count = model_u32(data + 12);
    asset->primitive_count = model_u32(data + 44);
    asset->material_count = model_u32(data + 48);
    asset->primitives = data + model_u32(data + 52);
    asset->materials = data + model_u32(data + 56);
    asset->vertices = asset->materials + asset->material_count * RASTERFALL_MODEL_MATERIAL_BYTES;
    asset->indices = asset->vertices + asset->vertex_count * RASTERFALL_MODEL_VERTEX_BYTES;
    asset->min_x = *(const int *)(data + 20);
    asset->min_y = *(const int *)(data + 24);
    asset->min_z = *(const int *)(data + 28);
    asset->max_x = *(const int *)(data + 32);
    asset->max_y = *(const int *)(data + 36);
    asset->max_z = *(const int *)(data + 40);
    return 0;
}

void rasterfall_model_unload(struct rasterfall_model_asset *asset)
{
    if (!asset || !asset->data) return;
    tlibc_free((void *)asset->data);
    __memset(asset, 0, sizeof(*asset));
}
