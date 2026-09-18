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
    RF_GPU_RASTER_CMD_VERTEX_LIT_TRIANGLE_V1 = 4,
    RF_GPU_RASTER_CMD_TEXTURED_TRIANGLE_V1 = 5,
    RF_GPU_RASTER_CMD_SKY_V1 = 6,
    /* Retained VIEWMODEL span barrier.  This is a generic depth-domain
     * control command; triangle records remain business-agnostic. */
    RF_GPU_RASTER_CMD_BEGIN_VIEWMODEL_V1 = 7,
    /* Generic retained transparent span barrier.  It carries no raster
     * state; it is an ordering/audit marker only. */
    RF_GPU_RASTER_CMD_BEGIN_TRANSPARENT_V1 = 8
};

enum rf_gpu_raster_cmd_flags_v1 {
    RF_GPU_RASTER_FLAG_DEPTH_TEST_V1 = 1U << 0,
    RF_GPU_RASTER_FLAG_DEPTH_WRITE_V1 = 1U << 1,
    RF_GPU_RASTER_FLAG_OPAQUE_V1 = 1U << 2,
    RF_GPU_RASTER_FLAG_FOG_V1 = 1U << 3,
    RF_GPU_RASTER_FLAG_TEXTURE_REPEAT_V1 = 1U << 4,
    /* Straight-alpha source-over.  This is a per-command raster state;
     * transparent layer placement is owned by RenderFrame, not the ABI. */
    RF_GPU_RASTER_FLAG_SOURCE_OVER_V1 = 1U << 5
};

enum rf_gpu_texture_format_v1 {
    RF_GPU_TEXTURE_FORMAT_RGB8_V1 = 1,
    RF_GPU_TEXTURE_FORMAT_RGBA8_V1 = 2
};

enum rf_gpu_texture_sampling_v1 {
    RF_GPU_TEXTURE_SAMPLING_NEAREST_V1 = 1
};

struct rf_gpu_texture_desc_v1 {
    uint32_t texel_offset;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t sampling;
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
    /* reserved[0] is material alpha for SOURCE_OVER, reserved[1] is zero. */
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

/* Vertex-lit triangles use command.resource_handle as material alpha only
 * when SOURCE_OVER is set.  Opaque vertex-lit commands keep it zero. */

/* Texture V1 keeps the frozen 96-byte command record.  Textured commands do
 * not store a bbox: CPU binning derives it from the three screen vertices.
 * The released words carry the six signed Q16 perspective terms plus the
 * constant light/fog payload used by the current normal-world workload. */
struct rf_gpu_raster_textured_triangle_v1 {
    int64_t area;
    struct rf_gpu_raster_vertex_v1 a;
    struct rf_gpu_raster_vertex_v1 b;
    struct rf_gpu_raster_vertex_v1 c;
    int32_t a_u_over_z;
    int32_t a_v_over_z;
    int32_t b_u_over_z;
    int32_t b_v_over_z;
    int32_t c_u_over_z;
    int32_t c_v_over_z;
    int32_t light_q8;
    int32_t fog_q8;
    /* material alpha for SOURCE_OVER, zero for opaque commands. */
    uint32_t reserved;
};

struct rf_gpu_raster_clear_v1 {
    uint32_t value;
    uint32_t reserved[19];
};

/* Parameter-only background layer.  It is evaluated before depth-tested
 * world commands and does not write depth.  Direction values use the same
 * Q10 convention as struct camera. */
struct rf_gpu_raster_sky_v1 {
    int32_t direction_sy;
    int32_t direction_cy;
    int32_t pitch_sy;
    int32_t pitch_cy;
    uint32_t zenith_color;
    uint32_t horizon_color;
    uint32_t ground_color;
    uint32_t flags;
    uint32_t reserved[12];
};

union rf_gpu_raster_payload_v1 {
    struct rf_gpu_raster_clear_v1 clear;
    struct rf_gpu_raster_sky_v1 sky;
    struct rf_gpu_raster_flat_triangle_v1 flat_triangle;
    struct rf_gpu_raster_vertex_lit_triangle_v1 vertex_lit_triangle;
    struct rf_gpu_raster_textured_triangle_v1 textured_triangle;
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
_Static_assert(sizeof(struct rf_gpu_raster_textured_triangle_v1) == 80,
               "GPU textured triangle V1 layout");
_Static_assert(sizeof(struct rf_gpu_raster_sky_v1) == 80,
               "GPU sky V1 layout");
_Static_assert(sizeof(struct rf_gpu_texture_desc_v1) == 24,
               "GPU texture descriptor V1 layout");
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
