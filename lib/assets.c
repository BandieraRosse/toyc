#include "tlibc_everything.h"
#include "toy_assets.h"

/* 资产文件的公共约束。所有整数都使用小端序，文件头声明实际数据位置。 */
#define TOY_ASSET_MAX_SIZE (64 * 1024 * 1024)

static uint16_t read_u16(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const unsigned char *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* 检查 [offset, offset + length) 是否完全位于文件范围内，避免整数溢出。 */
static int range_valid(uint32_t offset, uint32_t length, uint32_t total)
{
    return offset <= total && length <= total - offset;
}

/* Rasterfall 的生成资源对象会提供强定义；其他程序继续使用磁盘资源。 */
__attribute__((weak)) const unsigned char *toy_embedded_asset_find(const char *path,
                                                                    uint32_t *size)
{
    (void)path;
    if (size) *size = 0;
    return NULL;
}
__attribute__((weak)) int toy_embedded_asset_count(void) { return 0; }
__attribute__((weak)) const char *toy_embedded_asset_path(int index)
{
    (void)index;
    return NULL;
}

/* 读取整个资产文件。返回的缓冲区由对应的 unload 函数释放。 */
static unsigned char *read_file(const char *path, uint32_t *size)
{
    int fd;
    int n;
    int received = 0;
    struct stat st;
    unsigned char *data;

    if (!path || !size) return NULL;

    fd = openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0 || fstat(fd, &st) < 0) {
        if (fd >= 0) close(fd);
        return NULL;
    }
    if (st.st_size <= 0 || st.st_size > TOY_ASSET_MAX_SIZE) {
        close(fd);
        return NULL;
    }

    data = (unsigned char *)tlibc_malloc((unsigned long)st.st_size);
    if (!data) {
        close(fd);
        return NULL;
    }

    while (received < st.st_size) {
        n = read(fd, data + received, (int)(st.st_size - received));
        if (n <= 0) {
            tlibc_free(data);
            close(fd);
            return NULL;
        }
        received += n;
    }

    close(fd);
    *size = (uint32_t)received;
    return data;
}

unsigned char *toy_asset_load_file(const char *path, uint32_t *size)
{
    const unsigned char *embedded;
    unsigned char *copy;
    uint32_t embedded_size = 0;

    if (!path || !size) return NULL;
    embedded = toy_embedded_asset_find(path, &embedded_size);
    if (embedded && embedded_size > 0 && embedded_size <= TOY_ASSET_MAX_SIZE) {
        copy = (unsigned char *)tlibc_malloc(embedded_size);
        if (!copy) return NULL;
        memcpy(copy, embedded, embedded_size);
        *size = embedded_size;
        return copy;
    }
    return read_file(path, size);
}

/* 校验 magic、版本和头部大小，并返回头部长度。 */
static int read_header(const unsigned char *data, uint32_t size,
                       const char *magic, uint16_t *header_size)
{
    uint16_t header;

    if (!data || !magic || !header_size || size < 8 ||
        memcmp(data, magic, 4) != 0 ||
        read_u16(data + 4) != TOY_ASSET_VERSION)
        return -1;

    header = read_u16(data + 6);
    if (header < 8 || header > size) return -1;
    *header_size = header;
    return 0;
}

int toy_texture_load(const char *path, struct toy_texture_asset *asset)
{
    unsigned char *data;
    uint32_t size;
    uint32_t offset;
    uint32_t length;
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    uint16_t header_size;

    if (!asset) return -1;
    memset(asset, 0, sizeof(*asset));

    data = read_file(path, &size);
    if (!data || read_header(data, size, "TTEX", &header_size) < 0 ||
        header_size < 32 || size < 32) {
        if (data) tlibc_free(data);
        return -1;
    }

    width = read_u32(data + 8);
    height = read_u32(data + 12);
    channels = read_u16(data + 16);
    offset = read_u32(data + 20);
    length = read_u32(data + 24);

    /* TTEX v1 supports tightly packed RGB888 and RGBA8888. */
    if (!width || !height || (channels != 3 && channels != 4) ||
        read_u16(data + 18) != 1 || width > 8192 || height > 8192 ||
        width > 0xffffffffu / (height * channels) ||
        length != width * height * channels || offset < header_size ||
        !range_valid(offset, length, size)) {
        tlibc_free(data);
        return -1;
    }

    asset->blob = data;
    asset->data = data + offset;
    asset->width = width;
    asset->height = height;
    asset->data_size = length;
    asset->channels = channels;
    if (channels == 4) {
        uint32_t i, pixels = width * height;
        for (i = 0; i < pixels; i++)
            if (asset->data[i * 4 + 3] != 255) {
                asset->has_transparency = 1;
                break;
            }
    }
    return 0;
}

int toy_sound_load(const char *path, struct toy_sound_asset *asset)
{
    unsigned char *data;
    uint32_t size;
    uint32_t offset;
    uint32_t length;
    uint32_t rate;
    uint32_t frames;
    uint16_t channels;
    uint16_t bits;
    uint16_t header_size;

    if (!asset) return -1;
    memset(asset, 0, sizeof(*asset));

    data = read_file(path, &size);
    if (!data || read_header(data, size, "TSND", &header_size) < 0 ||
        header_size < 32 || size < 32) {
        if (data) tlibc_free(data);
        return -1;
    }

    rate = read_u32(data + 8);
    channels = read_u16(data + 12);
    bits = read_u16(data + 14);
    frames = read_u32(data + 16);
    offset = read_u32(data + 20);
    length = read_u32(data + 24);

    if (!rate || channels < 1 || channels > 2 || bits != 16 || !frames ||
        frames > 0xffffffffu / (channels * 2u) ||
        length != frames * channels * 2u || offset < header_size ||
        !range_valid(offset, length, size)) {
        tlibc_free(data);
        return -1;
    }

    asset->blob = data;
    asset->data = data + offset;
    asset->rate = rate;
    asset->channels = channels;
    asset->frames = frames;
    asset->data_size = length;
    return 0;
}

int toy_mesh_load(const char *path, struct toy_mesh_asset *asset)
{
    unsigned char *data;
    uint32_t size;
    uint32_t vertex_offset;
    uint32_t index_offset;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t vertex_size;
    uint32_t index_size;
    uint32_t vertex_bytes;
    uint32_t index_bytes;
    uint16_t header_size;
    uint32_t i;

    if (!asset) return -1;
    memset(asset, 0, sizeof(*asset));

    data = read_file(path, &size);
    if (!data || read_header(data, size, "TMES", &header_size) < 0 ||
        header_size < 40 || size < 40) {
        if (data) tlibc_free(data);
        return -1;
    }

    vertex_count = read_u32(data + 8);
    index_count = read_u32(data + 12);
    vertex_size = read_u32(data + 16);
    index_size = read_u32(data + 20);
    vertex_offset = read_u32(data + 24);
    index_offset = read_u32(data + 28);

    if (!vertex_count || !index_count || vertex_count > 1048576 ||
        index_count > 3145728 || vertex_size != 16 || index_size != 4 ||
        vertex_count > 0xffffffffu / vertex_size ||
        index_count > 0xffffffffu / index_size) {
        tlibc_free(data);
        return -1;
    }

    vertex_bytes = vertex_count * vertex_size;
    index_bytes = index_count * index_size;
    if (vertex_offset < header_size || index_offset < vertex_offset ||
        index_offset - vertex_offset < vertex_bytes ||
        !range_valid(vertex_offset, vertex_bytes, size) ||
        !range_valid(index_offset, index_bytes, size)) {
        tlibc_free(data);
        return -1;
    }

    /* 每个索引必须指向已存在的顶点。 */
    for (i = 0; i < index_count; i++) {
        if (read_u32(data + index_offset + i * 4) >= vertex_count) {
            tlibc_free(data);
            return -1;
        }
    }

    asset->blob = data;
    asset->vertices = data + vertex_offset;
    asset->indices = data + index_offset;
    asset->vertex_count = vertex_count;
    asset->index_count = index_count;
    return 0;
}

void toy_texture_unload(struct toy_texture_asset *asset)
{
    if (!asset) return;
    if (asset->blob) tlibc_free((void *)asset->blob);
    memset(asset, 0, sizeof(*asset));
}

void toy_sound_unload(struct toy_sound_asset *asset)
{
    if (!asset) return;
    if (asset->blob) tlibc_free((void *)asset->blob);
    memset(asset, 0, sizeof(*asset));
}

void toy_mesh_unload(struct toy_mesh_asset *asset)
{
    if (!asset) return;
    if (asset->blob) tlibc_free((void *)asset->blob);
    memset(asset, 0, sizeof(*asset));
}
