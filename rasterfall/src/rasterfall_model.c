#include "core.h"
#include "tlibc_everything.h"
#include "rasterfall_model.h"

static unsigned int model_u32(const unsigned char *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
}

int rasterfall_model_load(struct rasterfall_model_asset *asset,
                          const char *path)
{
    int fd, size;
    struct stat st;
    const unsigned char *data;
    if (!asset || !path) return -1;
    __memset(asset, 0, sizeof(*asset));
    fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0 || __fstat(fd, &st) < 0 || st.st_size < RASTERFALL_MODEL_HEADER_BYTES) {
        if (fd >= 0) __close(fd);
        return -1;
    }
    size = (int)st.st_size;
    data = (const unsigned char *)__mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0);
    __close(fd);
    if (data == MAP_FAILED || model_u32(data) != RASTERFALL_MODEL_MAGIC ||
        model_u32(data + 4) != RASTERFALL_MODEL_VERSION ||
        model_u32(data + 8) > 1000000 || model_u32(data + 12) > 3000000 ||
        model_u32(data + 44) > 32 || model_u32(data + 48) > 32 ||
        model_u32(data + 52) != RASTERFALL_MODEL_HEADER_BYTES ||
        model_u32(data + 56) != RASTERFALL_MODEL_HEADER_BYTES +
            model_u32(data + 44) * RASTERFALL_MODEL_PRIMITIVE_BYTES ||
        (long)model_u32(data + 56) +
            (long)model_u32(data + 48) * RASTERFALL_MODEL_MATERIAL_BYTES +
            (long)model_u32(data + 8) * RASTERFALL_MODEL_VERTEX_BYTES +
            (long)model_u32(data + 12) * 4 != size) {
        if (data != MAP_FAILED) __munmap((void *)data, size);
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
    __munmap((void *)asset->data, asset->data_size);
    __memset(asset, 0, sizeof(*asset));
}
