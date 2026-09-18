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

static uint32_t sky_mix(uint32_t from, uint32_t to, int num, int den)
{
    uint32_t r=(((from>>16)&255U)*num+((to>>16)&255U)*(den-num))/den;
    uint32_t g=(((from>>8)&255U)*num+((to>>8)&255U)*(den-num))/den;
    uint32_t b=((from&255U)*num+(to&255U)*(den-num))/den;
    return 0xff000000U|(r<<16)|(g<<8)|b;
}

static void cpu_sky(uint32_t *color, uint32_t stride, uint32_t width,
                    uint32_t height, const struct rf_gpu_raster_sky_v1 *sky)
{
    static const int dir[3][3]={{340,248,-934},{-872,172,-512},{-488,380,-816}};
    static const int scale[3]={16,12,10};
    int focal=(int)width*3/4,horizon=(int)height/2;
    int pitch_cy=sky->pitch_cy<0?-sky->pitch_cy:sky->pitch_cy;
    uint32_t x,y;
    if(pitch_cy>=64){long long o=(long long)focal*sky->pitch_sy/sky->pitch_cy;
        if(o>2LL*height)o=2LL*height;
        if(o<-2LL*height)o=-2LL*height;
        horizon+=(int)o;}
    else horizon=sky->pitch_sy>0?-(int)height:(int)height*2;
    for(y=0;y<height;y++)for(x=0;x<width;x++){
        uint32_t c;
        if((int)y>=horizon && horizon>0)c=sky->ground_color|0xff000000U;
        else if(horizon<=0)c=sky->ground_color|0xff000000U;
        else {int bottom=horizon<(int)height?horizon:(int)height;
            int band_h=bottom/8+1,band=(int)y/band_h;if(band>7)band=7;
            c=sky_mix(sky->zenith_color,sky->horizon_color,7-band,8);
            for(int i=0;i<3;i++){int vx=(dir[i][0]*sky->direction_cy-dir[i][2]*sky->direction_sy)/1024;
                int vz0=(dir[i][0]*sky->direction_sy+dir[i][2]*sky->direction_cy)/1024;
                int vy2=(dir[i][1]*sky->pitch_cy-vz0*sky->pitch_sy)/1024;
                int vz2=(dir[i][1]*sky->pitch_sy+vz0*sky->pitch_cy)/1024;
                if(vz2>64){int cx=(int)width/2+vx*focal/vz2,cy=(int)height/2-vy2*focal/vz2,u=scale[i]/4;if(u<2)u=2;
                    if((int)x>=cx-u*7&&(int)x<cx+u*7&&(int)y>=cy+u*2&&(int)y<cy+u*3)c=0xffb9d9ecU;
                    if((int)x>=cx-u*6&&(int)x<cx+u*6&&(int)y>=cy&&(int)y<cy+u*3)c=0xffeaf7ffU;
                    if((int)x>=cx-u*3&&(int)x<cx+u*3&&(int)y>=cy-u*2&&(int)y<cy)c=0xffffffffU;
                    if((((int)x>=cx-u*5&&(int)x<cx-u*3)||((int)x>=cx+u*3&&(int)x<cx+u*5))&&(int)y>=cy-u&&(int)y<cy)c=0xffffffffU;
                }}
        }color[(size_t)y*stride+x]=c;
    }
}

int rf_gpu_raster_cpu_reference_textured_domains_v1(
                                   const void *stream, size_t stream_size,
                                   const struct rf_gpu_texture_desc_v1 *descs,
                                   uint32_t desc_count,
                                   const unsigned char *texels,
                                   size_t texel_size,
                                   uint32_t *color, int32_t *depth,
                                   unsigned char *viewmodel_coverage,
                                   uint32_t color_stride,
                                   uint32_t depth_stride,
                                   uint32_t coverage_stride,
                                   struct rf_gpu_cpu_reference_timing *timing)
{
    const struct rf_gpu_raster_stream_header_v1 *header = stream;
    const struct rf_gpu_raster_cmd_v1 *commands;
    struct toy_renderer renderer;
    struct toy_surface surface;
    double start;
    uint32_t i, y;
    struct toy_texture_view *texture_views = NULL;
    int result = -1, viewmodel_started = 0;
    int32_t *viewmodel_depth = NULL, *world_depth = NULL;
    if (timing) memset(timing, 0, sizeof(*timing));
    if (rf_gpu_raster_validate_v1(stream, stream_size) || !color || !depth ||
        color_stride < header->framebuffer_width ||
        depth_stride < header->framebuffer_width ||
        (viewmodel_coverage && coverage_stride < header->framebuffer_width)) return -1;
    surface.pixels = color;
    surface.width = (int)header->framebuffer_width;
    surface.height = (int)header->framebuffer_height;
    surface.stride = (int)(color_stride * sizeof(*color));
    toy_renderer_init(&renderer);
    /* Raster ABI is the ordered oracle: legacy compatibility sorting must
     * not alter packed stream source-over order. */
    toy_renderer_set_preserve_command_order(&renderer, 1);
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
    if (viewmodel_coverage)
        memset(viewmodel_coverage, 0,
               (size_t)coverage_stride * header->framebuffer_height);
    if (commands[0].kind == RF_GPU_RASTER_CMD_SKY_V1)
        cpu_sky(color,color_stride,header->framebuffer_width,
                header->framebuffer_height,&commands[0].payload.sky);
    for (i = 2; i < header->command_count; ++i) {
        const struct rf_gpu_raster_flat_triangle_v1 *t =
            &commands[i].payload.flat_triangle;
        struct toy_screen_vertex a, b, c;
        if (commands[i].kind == RF_GPU_RASTER_CMD_BEGIN_TRANSPARENT_V1)
            continue;
        if (commands[i].kind == RF_GPU_RASTER_CMD_BEGIN_VIEWMODEL_V1) {
            uint32_t row;
            unsigned long pixels = (unsigned long)header->framebuffer_width *
                                   header->framebuffer_height;
            if (viewmodel_started) goto done;
            viewmodel_depth = calloc(pixels, sizeof(*viewmodel_depth));
            if (!viewmodel_depth || toy_renderer_flush(&renderer) < 0)
                goto done;
            for (row = 0; row < header->framebuffer_height; ++row)
                memcpy(depth + (size_t)row * depth_stride,
                       renderer.depth + (size_t)row * header->framebuffer_width,
                       (size_t)header->framebuffer_width * sizeof(*depth));
            world_depth = renderer.depth;
            renderer.depth = viewmodel_depth;
            if (viewmodel_coverage)
                toy_renderer_bind_coverage(&renderer, viewmodel_coverage,
                                           coverage_stride);
            viewmodel_started = 1;
            continue;
        }
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
            texture->has_transparency = channels == 4;
            a.u_over_z = v->a_u_over_z; a.v_over_z = v->a_v_over_z;
            b.u_over_z = v->b_u_over_z; b.v_over_z = v->b_v_over_z;
            c.u_over_z = v->c_u_over_z; c.v_over_z = v->c_v_over_z;
            toy_renderer_triangle_textured_lit(&renderer, &a, &b, &c,
                texture,
                (commands[i].flags & RF_GPU_RASTER_FLAG_TEXTURE_REPEAT_V1) != 0,
                0, v->light_q8, v->fog_q8);
            renderer.cmds[renderer.cmd_count - 1].material_alpha =
                (commands[i].flags & RF_GPU_RASTER_FLAG_SOURCE_OVER_V1) ?
                (int)v->reserved : 255;
            renderer.cmds[renderer.cmd_count - 1].transparent =
                (commands[i].flags & RF_GPU_RASTER_FLAG_SOURCE_OVER_V1) != 0;
            renderer.cmds[renderer.cmd_count - 1].transparent_no_depth_write =
                renderer.cmds[renderer.cmd_count - 1].transparent;
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
            renderer.cmds[renderer.cmd_count - 1].material_alpha =
                (commands[i].flags & RF_GPU_RASTER_FLAG_SOURCE_OVER_V1) ?
                (int)t->reserved[0] : 255;
            renderer.cmds[renderer.cmd_count - 1].transparent =
                (commands[i].flags & RF_GPU_RASTER_FLAG_SOURCE_OVER_V1) != 0;
            renderer.cmds[renderer.cmd_count - 1].transparent_no_depth_write =
                renderer.cmds[renderer.cmd_count - 1].transparent;
        }
    }
    if (toy_renderer_flush(&renderer) < 0) goto done;
    for (y = 0; y < header->framebuffer_height; ++y) {
        uint32_t x;
        for (x = 0; x < header->framebuffer_width; ++x)
            color[(size_t)y * color_stride + x] |= 0xff000000u;
        if (!viewmodel_started)
            memcpy(depth + (size_t)y * depth_stride,
                   renderer.depth + (size_t)y * header->framebuffer_width,
                   (size_t)header->framebuffer_width * sizeof(*depth));
    }
    if (timing) timing->raster_ms = cpu_now_ms() - start;
    result = 0;
done:
    toy_renderer_bind_coverage(&renderer, NULL, 0);
    /* VM depth is caller-owned and replaces the renderer's world depth for
     * the second span; detach it before destroy to avoid a double free. */
    if (renderer.depth == viewmodel_depth) renderer.depth = NULL;
    toy_renderer_destroy(&renderer);
    free(world_depth);
    free(viewmodel_depth);
    free(texture_views);
    return result;
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
    return rf_gpu_raster_cpu_reference_textured_domains_v1(
        stream, stream_size, descs, desc_count, texels, texel_size,
        color, depth, NULL, color_stride, depth_stride, 0, timing);
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
