#ifndef RASTERFALL_RF_GPU_RASTER_ABI_H
#define RASTERFALL_RF_GPU_RASTER_ABI_H

#include "tlibc_types.h"

#define RF_GPU_RASTER_OFFSETOF(type, member) __builtin_offsetof(type, member)

#define RF_GPU_RASTER_ABI_MAGIC 0x31415246U /* "FRA1" as little-endian bytes */
#define RF_GPU_RASTER_ABI_VERSION 1U
#define RF_GPU_RASTER_ENDIAN_LITTLE 0x01020304U

enum rf_gpu_raster_cmd_kind_v1 {
    RF_GPU_RASTER_CMD_CLEAR_COLOR_V1 = 1,
    RF_GPU_RASTER_CMD_CLEAR_DEPTH_V1 = 2,
    RF_GPU_RASTER_CMD_FLAT_TRIANGLE_V1 = 3,
    /* V1-compatible extension: same frozen 96-byte record and triangle
     * offsets, with the final three Q8 words interpreted as vertex lights. */
    RF_GPU_RASTER_CMD_VERTEX_LIT_TRIANGLE_V1 = 4
};

enum rf_gpu_raster_cmd_flags_v1 {
    RF_GPU_RASTER_FLAG_DEPTH_TEST_V1 = 1U << 0,
    RF_GPU_RASTER_FLAG_DEPTH_WRITE_V1 = 1U << 1,
    RF_GPU_RASTER_FLAG_OPAQUE_V1 = 1U << 2,
    RF_GPU_RASTER_FLAG_FOG_V1 = 1U << 3
};

/* Serialized V1 is a little-endian array of one header followed by command
 * records. All reserved fields must be zero. Resource handles are integer
 * IDs; V1 flat commands use handle zero because they have no external input. */
struct rf_gpu_raster_stream_header_v1 {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t command_size;
    uint32_t command_count;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t endian_tag;
};

struct rf_gpu_raster_vertex_v1 {
    int32_t x;
    int32_t y;
    int32_t inv_z;
};

struct rf_gpu_raster_flat_triangle_v1 {
    int64_t area;
    struct rf_gpu_raster_vertex_v1 a;
    struct rf_gpu_raster_vertex_v1 b;
    struct rf_gpu_raster_vertex_v1 c;
    int32_t bbox_minx;
    int32_t bbox_maxx;
    int32_t bbox_miny;
    int32_t bbox_maxy;
    uint32_t color;
    int32_t light_q8;
    int32_t fog_q8;
    uint32_t reserved[2];
};

struct rf_gpu_raster_vertex_lit_triangle_v1 {
    int64_t area;
    struct rf_gpu_raster_vertex_v1 a;
    struct rf_gpu_raster_vertex_v1 b;
    struct rf_gpu_raster_vertex_v1 c;
    int32_t bbox_minx;
    int32_t bbox_maxx;
    int32_t bbox_miny;
    int32_t bbox_maxy;
    uint32_t color;
    int32_t light_a_q8;
    int32_t fog_q8;
    int32_t light_b_q8;
    int32_t light_c_q8;
};

struct rf_gpu_raster_clear_v1 {
    uint32_t value;
    uint32_t reserved[19];
};

union rf_gpu_raster_payload_v1 {
    struct rf_gpu_raster_clear_v1 clear;
    struct rf_gpu_raster_flat_triangle_v1 flat_triangle;
    struct rf_gpu_raster_vertex_lit_triangle_v1 vertex_lit_triangle;
};

struct rf_gpu_raster_cmd_v1 {
    uint32_t kind;
    uint32_t byte_size;
    uint32_t flags;
    uint32_t resource_handle;
    union rf_gpu_raster_payload_v1 payload;
};

#define RF_GPU_RASTER_STREAM_HEADER_V1_SIZE 32U
#define RF_GPU_RASTER_CMD_V1_SIZE 96U

_Static_assert(sizeof(int32_t) == 4 && sizeof(uint32_t) == 4 &&
               sizeof(int64_t) == 8, "GPU raster ABI fixed-width types");
_Static_assert(sizeof(struct rf_gpu_raster_stream_header_v1) ==
               RF_GPU_RASTER_STREAM_HEADER_V1_SIZE, "GPU raster header V1 layout");
_Static_assert(sizeof(struct rf_gpu_raster_vertex_v1) == 12, "GPU raster vertex V1 layout");
_Static_assert(sizeof(struct rf_gpu_raster_flat_triangle_v1) == 80,
               "GPU flat triangle V1 layout");
_Static_assert(sizeof(struct rf_gpu_raster_vertex_lit_triangle_v1) == 80,
               "GPU vertex-lit triangle V1 layout");
_Static_assert(sizeof(union rf_gpu_raster_payload_v1) == 80,
               "GPU raster payload V1 layout");
_Static_assert(sizeof(struct rf_gpu_raster_cmd_v1) == RF_GPU_RASTER_CMD_V1_SIZE,
               "GPU raster command V1 layout");
_Static_assert(RF_GPU_RASTER_OFFSETOF(struct rf_gpu_raster_cmd_v1, payload) == 16,
               "GPU raster command V1 payload offset");
_Static_assert(RF_GPU_RASTER_OFFSETOF(struct rf_gpu_raster_flat_triangle_v1, a) == 8,
               "GPU flat triangle V1 vertex offset");
_Static_assert(RF_GPU_RASTER_OFFSETOF(struct rf_gpu_raster_flat_triangle_v1, color) == 60,
               "GPU flat triangle V1 color offset");
_Static_assert(RF_GPU_RASTER_OFFSETOF(struct rf_gpu_raster_vertex_lit_triangle_v1,
                                     light_b_q8) == 72,
               "GPU vertex-lit triangle V1 light offset");

#endif
