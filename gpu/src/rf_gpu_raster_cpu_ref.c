#include "rf_gpu_raster_cpu_ref.h"
#include "rf_gpu_raster_abi.h"
#include "rf_gpu_raster_pack.h"
#include "toy_renderer.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#endif

static double cpu_now_ms(void)
{
#if defined(_WIN32)
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter); QueryPerformanceFrequency(&frequency);
    return (double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart;
#else
    struct timespec value;
    timespec_get(&value, TIME_UTC);
    return (double)value.tv_sec * 1000.0 + (double)value.tv_nsec / 1000000.0;
#endif
}

int rf_gpu_raster_cpu_reference_textured_v1(
                                   const void *stream, size_t stream_size,
                                   const struct rf_gpu_texture_desc_v1 *descs,
                                   uint32_t desc_count,
                                   const unsigned char *texels,
                                   size_t texel_size,
                                   uint32_t *color, int32_t *depth,
                                   uint32_t color_stride,
                                   uint32_t depth_stride,
                                   struct rf_gpu_cpu_reference_timing *timing)
{
    const struct rf_gpu_raster_stream_header_v1 *header = stream;
    const struct rf_gpu_raster_cmd_v1 *commands;
    struct toy_renderer renderer;
    struct toy_surface surface;
    double start;
    uint32_t i, y;
    struct toy_texture_view *texture_views = NULL;
    int result = -1;
    if (timing) memset(timing, 0, sizeof(*timing));
    if (rf_gpu_raster_validate_v1(stream, stream_size) || !color || !depth ||
        color_stride < header->framebuffer_width ||
        depth_stride < header->framebuffer_width) return -1;
    surface.pixels = color;
    surface.width = (int)header->framebuffer_width;
    surface.height = (int)header->framebuffer_height;
    surface.stride = (int)(color_stride * sizeof(*color));
    toy_renderer_init(&renderer);
    if (desc_count) {
        texture_views = calloc(desc_count, sizeof(*texture_views));
        if (!texture_views) goto done;
    }
    /* Differential runs deliberately use the deterministic inline production
     * path; raster math is the same path used when worker creation is absent. */
    toy_renderer_set_worker_count(&renderer, -1);
    start = cpu_now_ms();
    commands = (const void *)(header + 1);
    if (toy_renderer_begin(&renderer, &surface,
                           commands[0].payload.clear.value) < 0) goto done;
    for (i = 2; i < header->command_count; ++i) {
        const struct rf_gpu_raster_flat_triangle_v1 *t =
            &commands[i].payload.flat_triangle;
        struct toy_screen_vertex a, b, c;
        memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
        memset(&c, 0, sizeof(c));
        a.x = t->a.x; a.y = t->a.y; a.inv_z = t->a.inv_z;
        b.x = t->b.x; b.y = t->b.y; b.inv_z = t->b.inv_z;
        c.x = t->c.x; c.y = t->c.y; c.inv_z = t->c.inv_z;
        if (commands[i].kind == RF_GPU_RASTER_CMD_TEXTURED_TRIANGLE_V1) {
            const struct rf_gpu_raster_textured_triangle_v1 *v =
                &commands[i].payload.textured_triangle;
            struct toy_texture_view *texture;
            uint32_t handle = commands[i].resource_handle;
            const struct rf_gpu_texture_desc_v1 *d;
            uint32_t channels;
            uint64_t bytes;
            if (!handle || handle > desc_count || !descs || !texels) goto done;
            d = &descs[handle - 1];
            channels = d->format == RF_GPU_TEXTURE_FORMAT_RGBA8_V1 ? 4 : 3;
            bytes = (uint64_t)d->width * d->height * channels;
            if (!d->width || !d->height || d->stride != d->width * channels ||
                d->sampling != RF_GPU_TEXTURE_SAMPLING_NEAREST_V1 ||
                (d->format != RF_GPU_TEXTURE_FORMAT_RGB8_V1 &&
                 d->format != RF_GPU_TEXTURE_FORMAT_RGBA8_V1) ||
                (uint64_t)d->texel_offset + bytes > texel_size ||
                bytes > 0xffffffffU) goto done;
            texture = &texture_views[handle - 1];
            memset(texture, 0, sizeof(*texture));
            texture->data = texels + d->texel_offset;
            texture->width = d->width; texture->height = d->height;
            texture->channels = channels; texture->data_size = (uint32_t)bytes;
            a.u_over_z = v->a_u_over_z; a.v_over_z = v->a_v_over_z;
            b.u_over_z = v->b_u_over_z; b.v_over_z = v->b_v_over_z;
            c.u_over_z = v->c_u_over_z; c.v_over_z = v->c_v_over_z;
            toy_renderer_triangle_textured_lit(&renderer, &a, &b, &c,
                texture,
                (commands[i].flags & RF_GPU_RASTER_FLAG_TEXTURE_REPEAT_V1) != 0,
                0, v->light_q8, v->fog_q8);
        } else if (commands[i].kind == RF_GPU_RASTER_CMD_VERTEX_LIT_TRIANGLE_V1) {
            const struct rf_gpu_raster_vertex_lit_triangle_v1 *v =
                &commands[i].payload.vertex_lit_triangle;
            a.light = v->light_a_q8;
            b.light = v->light_b_q8;
            c.light = v->light_c_q8;
            toy_renderer_triangle_planar_vertex_lit(
                &renderer, &a, &b, &c, v->color, v->fog_q8);
        } else {
            toy_renderer_triangle_lit(&renderer, &a, &b, &c, t->color,
                                      t->light_q8, t->fog_q8);
        }
    }
    if (toy_renderer_flush(&renderer) < 0) goto done;
    for (y = 0; y < header->framebuffer_height; ++y) {
        uint32_t x;
        for (x = 0; x < header->framebuffer_width; ++x)
            color[(size_t)y * color_stride + x] |= 0xff000000u;
        memcpy(depth + (size_t)y * depth_stride,
               renderer.depth + (size_t)y * header->framebuffer_width,
               (size_t)header->framebuffer_width * sizeof(*depth));
    }
    if (timing) timing->raster_ms = cpu_now_ms() - start;
    result = 0;
done:
    toy_renderer_destroy(&renderer);
    free(texture_views);
    return result;
}

int rf_gpu_raster_cpu_reference_v1(const void *stream, size_t stream_size,
                                   uint32_t *color, int32_t *depth,
                                   uint32_t color_stride,
                                   uint32_t depth_stride,
                                   struct rf_gpu_cpu_reference_timing *timing)
{
    return rf_gpu_raster_cpu_reference_textured_v1(stream, stream_size,
        NULL, 0, NULL, 0, color, depth, color_stride, depth_stride, timing);
}
