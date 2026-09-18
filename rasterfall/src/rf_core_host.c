#include "core.h"
#include "tlibc_everything.h"
#include "rf_core_host.h"
#include "rf_gpu_raster_pack.h"
#include "fb_draw.h"
#include <limits.h>
#ifdef TOYC_WINDOWS
#include "rf_gpu_raster_cpu_ref.h"
#endif

static int rf_core_cmd_is_transparent_v1(const struct toy_raster_cmd *cmd)
{
    return cmd && (cmd->transparent || cmd->material_alpha != 255 ||
        (cmd->textured && cmd->texture && cmd->texture->has_transparency));
}

static int rf_core_texture_view_valid_v1(const struct toy_texture_view *texture)
{
    unsigned long pixels, channels;
    if (!texture || !texture->data || !texture->width || !texture->height)
        return 0;
    if (texture->width > 8192 || texture->height > 8192) return 0;
    channels = texture->channels;
    if (channels != 3 && channels != 4) return 0;
    pixels = (unsigned long)texture->width * texture->height;
    return pixels <= 0xffffffffUL / channels &&
           texture->data_size == pixels * channels;
}

/* Keep this classifier adjacent to the Core consumer.  It mirrors the
 * packer's deliberately small supported subset, but only reports one reason
 * per command so a diagnostic cannot claim both material and texture debt for
 * the same input. */
static unsigned int rf_core_cmd_fallback_reason_v1(
    const struct toy_raster_cmd *cmd)
{
    if (!cmd) return RF_PRE_POST_FALLBACK_UNSUPPORTED_GENERIC_COMMAND;
    if (cmd->edge) return RF_PRE_POST_FALLBACK_UNSUPPORTED_EDGE;
    if (cmd->overlay) return RF_PRE_POST_FALLBACK_UNSUPPORTED_OVERLAY;
    if (cmd->area >= 0 || cmd->a.inv_z < INT_MIN ||
        cmd->a.inv_z > INT_MAX || cmd->b.inv_z < INT_MIN ||
        cmd->b.inv_z > INT_MAX || cmd->c.inv_z < INT_MIN ||
        cmd->c.inv_z > INT_MAX)
        return RF_PRE_POST_FALLBACK_UNSUPPORTED_GENERIC_COMMAND;
    if (cmd->textured) {
        if (!cmd->base_texture_valid || !rf_core_texture_view_valid_v1(
                cmd->texture))
            return RF_PRE_POST_FALLBACK_UNSUPPORTED_TEXTURE;
        if (cmd->base_texture_bilinear || cmd->material_features ||
            cmd->has_toon || cmd->texture2 || cmd->texture3 ||
            cmd->material_add || cmd->material_tint != 0x00ffffffU ||
            cmd->light < 0 || cmd->fog < 0 ||
            cmd->a.u_over_z < INT_MIN || cmd->a.u_over_z > INT_MAX ||
            cmd->a.v_over_z < INT_MIN || cmd->a.v_over_z > INT_MAX ||
            cmd->b.u_over_z < INT_MIN || cmd->b.u_over_z > INT_MAX ||
            cmd->b.v_over_z < INT_MIN || cmd->b.v_over_z > INT_MAX ||
            cmd->c.u_over_z < INT_MIN || cmd->c.u_over_z > INT_MAX ||
            cmd->c.v_over_z < INT_MIN || cmd->c.v_over_z > INT_MAX)
            return RF_PRE_POST_FALLBACK_UNSUPPORTED_MATERIAL;
    }
    return RF_PRE_POST_FALLBACK_NONE;
}

static int gpu_pre_post_finalize(struct rf_core *core);

const char *rf_core_renderer_name(int renderer)
{
    return renderer == RF_CORE_RENDERER_GPU_COMPUTE ? "gpu-compute" : "cpu";
}

static void gpu_world_log(const char *message)
{
#ifdef TOYC_WINDOWS
    toy_windows_log(message);
#else
    (void)message;
#endif
}

#ifdef TOYC_WINDOWS
static int gpu_oracle_write_file(const char *path, const void *data, size_t size)
{
    FILE *file = fopen(path, "wb");
    int ok;
    if (!file) return -1;
    ok = fwrite(data, 1, size, file) == size;
    if (fclose(file) != 0) ok = 0;
    return ok ? 0 : -1;
}

static int gpu_oracle_write_bmp(const char *path, const unsigned int *pixels,
                                unsigned int width, unsigned int height)
{
    unsigned char header[54] = { 'B', 'M' };
    uint32_t file_size = 54U + width * height * 4U;
    uint32_t offset = 54, dib = 40, planes_bpp = 0x00200001U;
    int32_t signed_width = (int32_t)width;
    int32_t signed_height = -(int32_t)height;
    FILE *file;
    memcpy(header + 2, &file_size, 4);
    memcpy(header + 10, &offset, 4);
    memcpy(header + 14, &dib, 4);
    memcpy(header + 18, &signed_width, 4);
    memcpy(header + 22, &signed_height, 4);
    memcpy(header + 26, &planes_bpp, 4);
    file = fopen(path, "wb");
    if (!file) return -1;
    if (fwrite(header, 1, sizeof(header), file) != sizeof(header) ||
        fwrite(pixels, 4, (size_t)width * height, file) !=
            (size_t)width * height) {
        fclose(file);
        return -1;
    }
    return fclose(file);
}

static void gpu_oracle_save_artifacts(
    const unsigned char *stream, size_t stream_size,
    const struct rf_gpu_texture_resources_v1 *textures,
    const unsigned int *cpu_color, const int *cpu_depth,
    const unsigned int *gpu_color, const int *gpu_depth,
    unsigned int width, unsigned int height, unsigned int gpu_color_stride,
    const char *report)
{
    const char *dir = "gpu-oracle-mismatch";
    unsigned long pixels = (unsigned long)width * height;
    unsigned int *packed_gpu = NULL, *diff = NULL;
    int *packed_depth = NULL;
    uint32_t texture_header[4];
    char path[256];
    FILE *file;
    unsigned int x, y;
    if (tlibc_recursive_mkdir(dir) < 0) return;
    packed_gpu = tlibc_malloc(pixels * sizeof(*packed_gpu));
    packed_depth = tlibc_malloc(pixels * sizeof(*packed_depth));
    diff = tlibc_malloc(pixels * sizeof(*diff));
    if (!packed_gpu || !packed_depth || !diff) goto done;
    for (y = 0; y < height; ++y)
    for (x = 0; x < width; ++x) {
        unsigned long packed = (unsigned long)y * width + x;
        unsigned long color_at = (unsigned long)y * gpu_color_stride + x;
        packed_gpu[packed] = gpu_color[color_at];
        packed_depth[packed] = gpu_depth[packed];
        diff[packed] = ((cpu_color[packed] ^ packed_gpu[packed]) & 0xffffffU) ||
                       cpu_depth[packed] != packed_depth[packed] ?
                       0xffff00ffU : 0xff000000U;
    }
    snprintf(path, sizeof(path), "%s/commands.bin", dir);
    gpu_oracle_write_file(path, stream, stream_size);
    snprintf(path, sizeof(path), "%s/commands.bin.textures", dir);
    file = fopen(path, "wb");
    if (file) {
        texture_header[0] = 0x31544652U;
        texture_header[1] = textures->desc_count;
        texture_header[2] = sizeof(struct rf_gpu_texture_desc_v1);
        texture_header[3] = (uint32_t)textures->texel_size;
        fwrite(texture_header, 1, sizeof(texture_header), file);
        fwrite(textures->descs, sizeof(*textures->descs), textures->desc_count,
               file);
        fwrite(textures->texels, 1, textures->texel_size, file);
        fclose(file);
    }
    snprintf(path, sizeof(path), "%s/cpu-color.bmp", dir);
    gpu_oracle_write_bmp(path, cpu_color, width, height);
    snprintf(path, sizeof(path), "%s/gpu-color.bmp", dir);
    gpu_oracle_write_bmp(path, packed_gpu, width, height);
    snprintf(path, sizeof(path), "%s/diff-color.bmp", dir);
    gpu_oracle_write_bmp(path, diff, width, height);
    snprintf(path, sizeof(path), "%s/cpu-depth.bin", dir);
    gpu_oracle_write_file(path, cpu_depth, pixels * sizeof(*cpu_depth));
    snprintf(path, sizeof(path), "%s/gpu-depth.bin", dir);
    gpu_oracle_write_file(path, packed_depth, pixels * sizeof(*packed_depth));
    snprintf(path, sizeof(path), "%s/report.txt", dir);
    gpu_oracle_write_file(path, report, strlen(report));
done:
    tlibc_free(packed_gpu);
    tlibc_free(packed_depth);
    tlibc_free(diff);
}
#endif

static int gpu_world_consume(struct toy_renderer *renderer,
                             const struct toy_raster_cmd *commands, int count,
                             void *opaque)
{
    struct rf_core *core = opaque;
    struct rf_core_gpu_frame *frame = &core->gpu_frame;
    struct toy_renderer packed;
    size_t needed, written = 0;
    size_t texture_bytes = 0;
    uint32_t unique_textures = 0;
    unsigned long texture = 0, transparent = 0, overlay = 0, edge = 0, other = 0;
    unsigned long unsupported_texture = 0;
    unsigned int fallback_reason = RF_PRE_POST_FALLBACK_NONE;
    uint32_t transparent_offset = UINT_MAX, viewmodel_offset = UINT_MAX;
    int has_transparent = 0, has_viewmodel = 0;
    unsigned int color_stride;
    char diagnostic[192];
    int64_t stage_start, consumer_start = rf_core_clock_now_us();
    int i;
    if (!frame->armed) return -1;
    frame->armed = 0;
    frame->stats.frames_attempted++;
    if (frame->frontend_begin_us)
        frame->stats.frontend_ms =
            (double)(consumer_start - frame->frontend_begin_us) / 1000.0;
    stage_start = consumer_start;
    gpu_world_log("gpu-world: classification begin");
    for (i = 0; i < count; i++) {
        const struct toy_raster_cmd *cmd = &commands[i];
        unsigned int command_reason = rf_core_cmd_fallback_reason_v1(cmd);
        fallback_reason |= command_reason;
        if (command_reason == RF_PRE_POST_FALLBACK_UNSUPPORTED_TEXTURE)
            unsupported_texture++;
        if (cmd->edge) edge++;
        else if (cmd->overlay) overlay++;
        else if (rf_core_cmd_is_transparent_v1(cmd)) transparent++;
        else if (cmd->textured) texture++;
        else if (cmd->area >= 0) other++;
    }
    frame->stats.unsupported_overlay += overlay;
    frame->stats.unsupported_edge += edge;
    frame->stats.unsupported_other += other;
    frame->stats.last_commands = (unsigned long)count;
    frame->stats.last_texture_commands = texture;
    frame->stats.last_transparent_commands = transparent;
    frame->stats.last_overlay_commands = overlay;
    frame->stats.last_edge_commands = edge;
    frame->stats.last_other_commands = other;
    frame->stats.unsupported_texture += unsupported_texture;
    core->render_frame.pre_post_fallback_reason |= fallback_reason;
    snprintf(diagnostic, sizeof(diagnostic),
             "gpu-world: classification end commands=%d texture=%lu transparent=%lu overlay=%lu edge=%lu other=%lu",
             count, texture, transparent, overlay, edge, other);
    gpu_world_log(diagnostic);
    frame->stats.classification_ms =
        (double)(rf_core_clock_now_us() - stage_start) / 1000.0;
    if (fallback_reason) {
        for (i = 0; i < count; ++i) {
            const struct toy_raster_cmd *cmd = &commands[i];
            unsigned int command_reason = rf_core_cmd_fallback_reason_v1(cmd);
            if (!command_reason) continue;
            snprintf(diagnostic, sizeof(diagnostic),
                "gpu-world: first unsupported index=%d reason=0x%x textured=%d transparent=%d planar=%d alpha=%d bilinear=%d features=0x%x toon=%d tint=0x%x light=%d fog=%d",
                i, command_reason, cmd->textured, cmd->transparent,
                cmd->planar_vertex_lit, cmd->material_alpha,
                cmd->base_texture_bilinear, cmd->material_features,
                cmd->has_toon, cmd->material_tint, cmd->light, cmd->fog);
            gpu_world_log(diagnostic);
            break;
        }
        frame->stats.last_path = 2;
        frame->stats.cpu_fallback_frames++;
        return -1;
    }
    if (frame->retained_batch_count[RF_RENDER_LAYER_TRANSPARENT]) {
        transparent_offset = (uint32_t)frame->retained_batch_count[
            RF_RENDER_LAYER_WORLD];
        has_transparent = 1;
    }
    if (core->gpu_frame.retained_batch_count[RF_RENDER_LAYER_VIEWMODEL]) {
        unsigned long offset =
            frame->retained_batch_count[RF_RENDER_LAYER_WORLD] +
            frame->retained_batch_count[RF_RENDER_LAYER_TRANSPARENT] +
            frame->retained_batch_count[RF_RENDER_LAYER_EFFECTS];
        if (offset > (unsigned long)count || offset > UINT_MAX) {
            frame->stats.cpu_fallback_frames++;
            return -1;
        }
        viewmodel_offset = (uint32_t)offset;
        has_viewmodel = 1;
    }
    needed = rf_gpu_raster_stream_size_v1((uint32_t)count + 2U +
                                           (has_transparent ? 1U : 0U) +
                                           (has_viewmodel ? 1U : 0U));
    if (needed > frame->stream_capacity) {
        unsigned char *grown = tlibc_malloc(needed);
        if (!grown) { frame->stats.cpu_fallback_frames++; return -1; }
        tlibc_free(frame->stream);
        frame->stream = grown;
        frame->stream_capacity = needed;
    }
    memset(&packed, 0, sizeof(packed));
    packed.surface = renderer->surface;
    packed.cmds = (struct toy_raster_cmd *)commands;
    packed.cmd_count = count;
    gpu_world_log("gpu-world: texture measure begin");
    stage_start = rf_core_clock_now_us();
    {
        int measure_result = rf_gpu_raster_measure_textures_toy_v1(
            &packed, &unique_textures, &texture_bytes);
        if (measure_result != RF_GPU_RASTER_PACK_OK) {
            /* Semantic texture rejection remains a producer diagnosis.
             * Invalid/capacity failures after preflight belong to the
             * consumer/resource path instead. */
            core->render_frame.pre_post_fallback_reason |=
                measure_result == RF_GPU_RASTER_PACK_UNSUPPORTED ?
                RF_PRE_POST_FALLBACK_UNSUPPORTED_TEXTURE :
                RF_PRE_POST_FALLBACK_CONSUMER_FAILURE;
            frame->stats.cpu_fallback_frames++;
            return -1;
        }
    }
    snprintf(diagnostic, sizeof(diagnostic),
             "gpu-world: texture measure end unique=%u bytes=%llu",
             unique_textures, (unsigned long long)texture_bytes);
    gpu_world_log(diagnostic);
    frame->stats.texture_measure_ms =
        (double)(rf_core_clock_now_us() - stage_start) / 1000.0;
    if (unique_textures > frame->texture_desc_capacity) {
        struct rf_gpu_texture_desc_v1 *grown = tlibc_malloc(
            (size_t)unique_textures * sizeof(*grown));
        if (!grown) { frame->stats.cpu_fallback_frames++; return -1; }
        tlibc_free(frame->texture_descs); frame->texture_descs = grown;
        frame->texture_desc_capacity = unique_textures;
    }
    if (texture_bytes > frame->texture_texel_capacity) {
        unsigned char *grown = tlibc_malloc(texture_bytes);
        if (!grown) { frame->stats.cpu_fallback_frames++; return -1; }
        tlibc_free(frame->texture_texels); frame->texture_texels = grown;
        frame->texture_texel_capacity = texture_bytes;
    }
    {
        struct rf_gpu_texture_resources_v1 resources;
        memset(&resources, 0, sizeof(resources));
        resources.descs = frame->texture_descs;
        resources.desc_capacity = frame->texture_desc_capacity;
        resources.texels = frame->texture_texels;
        resources.texel_capacity = frame->texture_texel_capacity;
        gpu_world_log("gpu-world: pack begin");
        stage_start = rf_core_clock_now_us();
        {
            int pack_result = rf_gpu_raster_pack_toy_textured_spans_v2(&packed,
                renderer->job_clear_color, 0, frame->stream,
                frame->stream_capacity, &written, &resources,
                transparent_offset, viewmodel_offset);
            if (pack_result != RF_GPU_RASTER_PACK_OK) {
                /* All semantic command classes were rejected by preflight.
                 * A later pack failure is therefore a consumer/resource
                 * failure, including texture-table capacity exhaustion. */
                core->render_frame.pre_post_fallback_reason |=
                    RF_PRE_POST_FALLBACK_CONSUMER_FAILURE;
                gpu_world_log("gpu-world: pack failed");
                frame->stats.cpu_fallback_frames++;
                return -1;
            }
        }
        if (core->render_frame.sky_enabled) {
            struct rf_gpu_raster_stream_header_v1 *header =
                (struct rf_gpu_raster_stream_header_v1 *)(void *)frame->stream;
            struct rf_gpu_raster_cmd_v1 *commands =
                (struct rf_gpu_raster_cmd_v1 *)(void *)(header + 1);
            memset(&commands[0], 0, sizeof(commands[0]));
            commands[0].kind = RF_GPU_RASTER_CMD_SKY_V1;
            commands[0].byte_size = RF_GPU_RASTER_CMD_V1_SIZE;
            commands[0].payload.sky.direction_sy =
                core->render_frame.direction_sy;
            commands[0].payload.sky.direction_cy =
                core->render_frame.direction_cy;
            commands[0].payload.sky.pitch_sy = core->render_frame.pitch_sy;
            commands[0].payload.sky.pitch_cy = core->render_frame.pitch_cy;
            commands[0].payload.sky.zenith_color = 0x3B82C4U;
            commands[0].payload.sky.horizon_color = 0xB9E3FFU;
            commands[0].payload.sky.ground_color = 0x0F1218U;
        }
        snprintf(diagnostic, sizeof(diagnostic),
                 "gpu-world: pack end stream=%llu textures=%u bytes=%llu",
                 (unsigned long long)written, resources.desc_count,
                 (unsigned long long)resources.texel_size);
        gpu_world_log(diagnostic);
        frame->stats.raster_abi_pack_ms =
            (double)(rf_core_clock_now_us() - stage_start) / 1000.0;
        /* Texture table construction is performed by the packer while it
         * assigns handles and copies texels, so V1 reports it combined with
         * Raster ABI pack rather than inventing a separate precision. */
        frame->stats.texture_table_build_ms =
            frame->stats.raster_abi_pack_ms;
        if (renderer->surface.stride < 0 ||
            renderer->surface.stride % (int)sizeof(*renderer->surface.pixels)) {
            gpu_world_log("gpu-world: invalid surface byte stride");
            frame->stats.cpu_fallback_frames++;
            return -1;
        }
        color_stride = (unsigned int)renderer->surface.stride /
                       (unsigned int)sizeof(*renderer->surface.pixels);
        if (color_stride < (unsigned int)renderer->surface.width ||
            rf_gpu_raster_resize(&core->gpu, &frame->raster,
                (unsigned int)renderer->surface.width,
                (unsigned int)renderer->surface.height) < 0) {
            gpu_world_log("gpu-world: raster resize/stride failed");
            frame->stats.cpu_fallback_frames++;
            return -1;
        }
        if (frame->native_present) {
            /* Preserve the packed world batch until Core end-frame.  Game can
             * now draw its existing screen UI into the independent overlay. */
            frame->native_stream_size = written;
            frame->native_texture_count = resources.desc_count;
            frame->native_texture_bytes = resources.texel_size;
            frame->native_prepared = 1;
            frame->stats.unique_textures = resources.desc_count;
            frame->stats.texture_upload_bytes = resources.texel_size;
            frame->stats.texture_commands += texture;
            frame->stats.gpu_frames++;
            frame->stats.last_path = 1;
            return 0;
        }
#ifdef TOYC_WINDOWS
        {
            unsigned long pixels = (unsigned long)renderer->surface.width *
                                   (unsigned long)renderer->surface.height;
            struct rf_gpu_cpu_reference_timing cpu_timing;
            if (pixels > frame->oracle_pixel_capacity) {
                unsigned int *new_color = tlibc_malloc(
                    pixels * sizeof(*new_color));
                int *new_depth = tlibc_malloc(pixels * sizeof(*new_depth));
                if (!new_color || !new_depth) {
                    tlibc_free(new_color);
                    tlibc_free(new_depth);
                    frame->stats.cpu_fallback_frames++;
                    return -1;
                }
                tlibc_free(frame->oracle_color);
                tlibc_free(frame->oracle_depth);
                frame->oracle_color = new_color;
                frame->oracle_depth = new_depth;
                frame->oracle_pixel_capacity = pixels;
            }
            if (rf_gpu_raster_cpu_reference_textured_v1(frame->stream, written,
                    resources.descs, resources.desc_count, resources.texels,
                    resources.texel_size, frame->oracle_color,
                    frame->oracle_depth, renderer->surface.width,
                    renderer->surface.width, &cpu_timing) < 0) {
                frame->stats.cpu_fallback_frames++;
                return -1;
            }
            frame->stats.cpu_oracle_ms = cpu_timing.raster_ms;
            frame->stats.oracle_frames++;
        }
#endif
        frame->stats.last_path = 3;
        gpu_world_log("gpu-world: raster call begin");
        if (rf_gpu_raster_render_textured_timed(&core->gpu, &frame->raster,
            frame->stream, written, resources.descs, resources.desc_count,
            resources.texels, resources.texel_size,
            renderer->surface.pixels, renderer->depth,
            renderer->surface.width, renderer->surface.height,
            color_stride, (unsigned int)renderer->surface.width,
            &frame->stats.last_timing) < 0) {
            gpu_world_log("gpu-world: raster call failed");
            frame->stats.cpu_fallback_frames++;
            return -1;
        }
        gpu_world_log("gpu-world: raster call end");
#ifdef TOYC_WINDOWS
        {
            unsigned long color_mismatches = 0, depth_mismatches = 0;
            unsigned int x, y;
            unsigned int max_color_delta = 0;
            unsigned long long max_depth_delta = 0;
            for (y = 0; y < (unsigned int)renderer->surface.height; ++y)
            for (x = 0; x < (unsigned int)renderer->surface.width; ++x) {
                unsigned long p = (unsigned long)y *
                                  (unsigned long)renderer->surface.width + x;
                unsigned long gpu_color_at = (unsigned long)y * color_stride + x;
                unsigned int cpu = frame->oracle_color[p];
                unsigned int gpu = renderer->surface.pixels[gpu_color_at];
                int dr = (int)((cpu >> 16) & 255) -
                         (int)((gpu >> 16) & 255);
                int dg = (int)((cpu >> 8) & 255) -
                         (int)((gpu >> 8) & 255);
                int db = (int)(cpu & 255) - (int)(gpu & 255);
                unsigned int delta;
                long long dd;
                if (dr < 0) dr = -dr;
                if (dg < 0) dg = -dg;
                if (db < 0) db = -db;
                delta = (unsigned int)(dr > dg ? (dr > db ? dr : db) :
                                                       (dg > db ? dg : db));
                if (delta) color_mismatches++;
                if (delta > max_color_delta) max_color_delta = delta;
                dd = (long long)frame->oracle_depth[p] - renderer->depth[p];
                if (dd < 0) dd = -dd;
                if (dd) depth_mismatches++;
                if ((unsigned long long)dd > max_depth_delta)
                    max_depth_delta = (unsigned long long)dd;
            }
            frame->stats.oracle_color_mismatches += color_mismatches;
            frame->stats.oracle_depth_mismatches += depth_mismatches;
            if (max_color_delta > frame->stats.oracle_max_color_delta)
                frame->stats.oracle_max_color_delta = max_color_delta;
            if (max_depth_delta > frame->stats.oracle_max_depth_delta)
                frame->stats.oracle_max_depth_delta = max_depth_delta;
            if (color_mismatches || depth_mismatches) {
                frame->stats.oracle_failures++;
                snprintf(diagnostic, sizeof(diagnostic),
                    "gpu-world: oracle FAIL color=%lu depth=%lu max-color=%u max-depth=%llu",
                    color_mismatches, depth_mismatches, max_color_delta,
                    max_depth_delta);
                gpu_world_log(diagnostic);
                gpu_oracle_save_artifacts(frame->stream, written, &resources,
                    frame->oracle_color, frame->oracle_depth,
                    renderer->surface.pixels, renderer->depth,
                    (unsigned int)renderer->surface.width,
                    (unsigned int)renderer->surface.height, color_stride,
                    diagnostic);
                for (y = 0; y < (unsigned int)renderer->surface.height; ++y) {
                    unsigned int *destination = renderer->surface.pixels +
                        (unsigned long)y * color_stride;
                    const unsigned int *source = frame->oracle_color +
                        (unsigned long)y * renderer->surface.width;
                    memcpy(destination, source,
                           (size_t)renderer->surface.width * sizeof(*source));
                    memcpy(renderer->depth +
                               (unsigned long)y * renderer->surface.width,
                           frame->oracle_depth +
                               (unsigned long)y * renderer->surface.width,
                           (size_t)renderer->surface.width *
                               sizeof(*frame->oracle_depth));
                }
                frame->stats.cpu_fallback_frames++;
                return 0;
            }
        }
#endif
        frame->stats.unique_textures = resources.desc_count;
        frame->stats.texture_upload_bytes = resources.texel_size;
    }
    frame->stats.texture_commands += texture;
    frame->stats.gpu_frames++;
    return 0;
}

static int gpu_pre_post_retain_consume(struct toy_renderer *renderer,
                                       const struct toy_raster_cmd *commands,
                                       int count, void *opaque)
{
    struct rf_core *core = opaque;
    struct rf_core_gpu_frame *frame;
    unsigned long needed;
    unsigned int layer;
    if (!core || !renderer || !commands || count <= 0) return -1;
    frame = &core->gpu_frame;
    if (!frame->retaining_pre_post) return -1;
    layer = core->render_frame.current_layer;
    if (layer < RF_RENDER_LAYER_WORLD || layer > RF_RENDER_LAYER_VIEWMODEL)
        return -1;
    needed = frame->retained_command_count + (unsigned long)count;
    if (needed > frame->retained_command_capacity) {
        unsigned long capacity = frame->retained_command_capacity ?
            frame->retained_command_capacity : 1024;
        struct toy_raster_cmd *grown;
        while (capacity < needed) capacity *= 2;
        grown = tlibc_malloc(capacity * sizeof(*grown));
        if (!grown) return -1;
        if (frame->retained_command_count)
            memcpy(grown, frame->retained_commands,
                   frame->retained_command_count * sizeof(*grown));
        tlibc_free(frame->retained_commands);
        frame->retained_commands = grown;
        frame->retained_command_capacity = capacity;
    }
    /* WORLD is collected as one physical prefix.  Do not partition here:
     * multiple WORLD flushes must not produce O,T,O,T in the retained stream.
     * Finalization performs one stable partition over the complete prefix. */
    if (layer == RF_RENDER_LAYER_WORLD) {
        if (frame->retained_batch_count[RF_RENDER_LAYER_EFFECTS] ||
            frame->retained_batch_count[RF_RENDER_LAYER_VIEWMODEL])
            return -1;
        memcpy(frame->retained_commands + frame->retained_command_count,
               commands, (unsigned long)count * sizeof(*commands));
        frame->retained_command_count = needed;
        frame->retained_world_raw_count += (unsigned long)count;
    } else {
        memcpy(frame->retained_commands + frame->retained_command_count, commands,
               (unsigned long)count * sizeof(*commands));
        frame->retained_command_count = needed;
        frame->retained_batch_count[layer] += (unsigned long)count;
    }
    core->render_frame.retained_pre_post_commands = needed;
    return 0;
}

static int gpu_pre_post_partition_world(struct rf_core_gpu_frame *frame)
{
    unsigned long world_count = frame->retained_world_raw_count;
    unsigned long opaque_count = 0, transparent_count = 0, i;
    struct toy_raster_cmd *partitioned;
    if (!world_count) return 0;
    for (i = 0; i < world_count; ++i) {
        if (rf_core_cmd_is_transparent_v1(&frame->retained_commands[i]))
            ++transparent_count;
        else
            ++opaque_count;
    }
    partitioned = tlibc_malloc(frame->retained_command_count *
                               sizeof(*partitioned));
    if (!partitioned) {
        /* A contiguous raw WORLD prefix is still a valid compatibility span;
         * leave it unsplit so legacy CPU replay can process it atomically. */
        frame->retained_batch_count[RF_RENDER_LAYER_WORLD] = world_count;
        frame->retained_batch_count[RF_RENDER_LAYER_TRANSPARENT] = 0;
        frame->retained_world_raw_count = 0;
        return -1;
    }
    {
        unsigned long opaque_at = 0, transparent_at = opaque_count;
        for (i = 0; i < world_count; ++i) {
            if (rf_core_cmd_is_transparent_v1(&frame->retained_commands[i]))
                partitioned[transparent_at++] = frame->retained_commands[i];
            else
                partitioned[opaque_at++] = frame->retained_commands[i];
        }
        memcpy(partitioned + opaque_count + transparent_count,
               frame->retained_commands + world_count,
               (frame->retained_command_count - world_count) *
                   sizeof(*partitioned));
    }
    tlibc_free(frame->retained_commands);
    frame->retained_commands = partitioned;
    frame->retained_batch_count[RF_RENDER_LAYER_WORLD] = opaque_count;
    frame->retained_batch_count[RF_RENDER_LAYER_TRANSPARENT] =
        transparent_count;
    frame->retained_world_raw_count = 0;
    return 0;
}

int rf_core_retained_span_logic_test_v1(void)
{
    struct rf_core core;
    struct toy_renderer renderer;
    struct toy_raster_cmd first[2], second[2], effects[2], viewmodel[2];
    struct toy_texture_view rgba;
    unsigned char rgba_texel[4] = { 255, 160, 32, 128 };
    unsigned int pixels[64 * 64];
    struct rf_core_gpu_frame *frame;
    struct rf_gpu_raster_stream_header_v1 *header;
    struct rf_gpu_raster_cmd_v1 *packed;
    size_t packed_size;
    memset(&core, 0, sizeof(core));
    memset(&renderer, 0, sizeof(renderer));
    memset(first, 0, sizeof(first));
    memset(second, 0, sizeof(second));
    memset(effects, 0, sizeof(effects));
    memset(viewmodel, 0, sizeof(viewmodel));
    memset(&rgba, 0, sizeof(rgba));
    memset(pixels, 0, sizeof(pixels));
    rgba.channels = 4;
    rgba.has_transparency = 1;
    rgba.data = rgba_texel;
    rgba.width = rgba.height = 1;
    rgba.data_size = sizeof(rgba_texel);
    /* Keep the fixture in the same supported flat/texture command subset as
     * the normal retained consumer.  The packed validator deliberately sees
     * real triangle geometry rather than zeroed synthetic records. */
    first[0].area = first[1].area = second[0].area = second[1].area =
        effects[0].area = effects[1].area = viewmodel[0].area =
        viewmodel[1].area = -64;
    first[0].bbox_minx = first[1].bbox_minx = second[0].bbox_minx =
        second[1].bbox_minx = effects[0].bbox_minx =
        effects[1].bbox_minx = viewmodel[0].bbox_minx =
        viewmodel[1].bbox_minx = 0;
    first[0].bbox_maxx = first[1].bbox_maxx = second[0].bbox_maxx =
        second[1].bbox_maxx = effects[0].bbox_maxx =
        effects[1].bbox_maxx = viewmodel[0].bbox_maxx =
        viewmodel[1].bbox_maxx = 8;
    first[0].bbox_miny = first[1].bbox_miny = second[0].bbox_miny =
        second[1].bbox_miny = effects[0].bbox_miny =
        effects[1].bbox_miny = viewmodel[0].bbox_miny =
        viewmodel[1].bbox_miny = 0;
    first[0].bbox_maxy = first[1].bbox_maxy = second[0].bbox_maxy =
        second[1].bbox_maxy = effects[0].bbox_maxy =
        effects[1].bbox_maxy = viewmodel[0].bbox_maxy =
        viewmodel[1].bbox_maxy = 8;
    first[0].a.x = first[1].a.x = second[0].a.x = second[1].a.x =
        effects[0].a.x = effects[1].a.x = viewmodel[0].a.x =
        viewmodel[1].a.x = 0;
    first[0].a.y = first[1].a.y = second[0].a.y = second[1].a.y =
        effects[0].a.y = effects[1].a.y = viewmodel[0].a.y =
        viewmodel[1].a.y = 0;
    first[0].b.x = first[1].b.x = second[0].b.x = second[1].b.x =
        effects[0].b.x = effects[1].b.x = viewmodel[0].b.x =
        viewmodel[1].b.x = 8;
    first[0].b.y = first[1].b.y = second[0].b.y = second[1].b.y =
        effects[0].b.y = effects[1].b.y = viewmodel[0].b.y =
        viewmodel[1].b.y = 0;
    first[0].c.x = first[1].c.x = second[0].c.x = second[1].c.x =
        effects[0].c.x = effects[1].c.x = viewmodel[0].c.x =
        viewmodel[1].c.x = 0;
    first[0].c.y = first[1].c.y = second[0].c.y = second[1].c.y =
        effects[0].c.y = effects[1].c.y = viewmodel[0].c.y =
        viewmodel[1].c.y = 8;
    first[0].a.inv_z = first[0].b.inv_z = first[0].c.inv_z =
        first[1].a.inv_z = first[1].b.inv_z = first[1].c.inv_z =
        second[0].a.inv_z = second[0].b.inv_z = second[0].c.inv_z =
        second[1].a.inv_z = second[1].b.inv_z = second[1].c.inv_z =
        effects[0].a.inv_z = effects[0].b.inv_z = effects[0].c.inv_z =
        effects[1].a.inv_z = effects[1].b.inv_z = effects[1].c.inv_z =
        viewmodel[0].a.inv_z = viewmodel[0].b.inv_z =
        viewmodel[0].c.inv_z = viewmodel[1].a.inv_z =
        viewmodel[1].b.inv_z = viewmodel[1].c.inv_z = 1024;
    second[1].base_texture_valid = 1;
    first[0].material_alpha = 255;
    first[1].material_alpha = 255;
    first[1].transparent = 1;
    second[0].material_alpha = 255;
    second[1].material_alpha = 255;
    second[1].textured = 1;
    second[1].texture = &rgba;
    second[1].base_texture_valid = 1;
    second[1].material_tint = 0x00ffffffU;
    effects[0].material_alpha = 255;
    effects[1].material_alpha = 128;
    effects[1].transparent = 1;
    effects[1].transparent_no_depth_write = 1;
    viewmodel[0].material_alpha = 255;
    viewmodel[1].material_alpha = 96;
    viewmodel[1].transparent = 1;
    viewmodel[1].transparent_no_depth_write = 1;
    core.renderer = &renderer;
    core.render_frame.current_layer = RF_RENDER_LAYER_WORLD;
    frame = &core.gpu_frame;
    frame->retaining_pre_post = 1;
    if (gpu_pre_post_retain_consume(&renderer, first, 2, &core) < 0 ||
        gpu_pre_post_retain_consume(&renderer, second, 2, &core) < 0)
        return -1;
    core.render_frame.current_layer = RF_RENDER_LAYER_EFFECTS;
    if (gpu_pre_post_retain_consume(&renderer, effects, 2, &core) < 0)
        goto fail;
    core.render_frame.current_layer = RF_RENDER_LAYER_VIEWMODEL;
    if (gpu_pre_post_retain_consume(&renderer, viewmodel, 2, &core) < 0 ||
        frame->retained_world_raw_count != 4 ||
        frame->retained_command_count != 8)
        goto fail;
    if (gpu_pre_post_partition_world(frame) < 0 ||
        frame->retained_batch_count[RF_RENDER_LAYER_WORLD] != 2 ||
        frame->retained_batch_count[RF_RENDER_LAYER_TRANSPARENT] != 2 ||
        frame->retained_batch_count[RF_RENDER_LAYER_EFFECTS] != 2 ||
        frame->retained_batch_count[RF_RENDER_LAYER_VIEWMODEL] != 2 ||
        frame->retained_world_raw_count != 0)
        goto fail;
    /* B2d-5 normal-frame closure: O(world), T(world), EFFECTS (opaque then
     * transparent), VIEWMODEL (opaque then transparent).  These exact spans
     * are passed to the two ABI marker barriers by the normal finalize path. */
    if (frame->retained_commands[0].transparent ||
        frame->retained_commands[1].textured ||
        !frame->retained_commands[2].transparent ||
        frame->retained_commands[3].texture != &rgba ||
        frame->retained_commands[4].material_alpha != 255 ||
        !frame->retained_commands[5].transparent ||
        frame->retained_commands[6].material_alpha != 255 ||
        !frame->retained_commands[7].transparent ||
        rf_core_render_frame_fallback_reason_v1(&core.render_frame) !=
            RF_PRE_POST_FALLBACK_NONE)
        goto fail;

    /* Exercise the actual normal retained consumer up to its native backend
     * hand-off.  A non-null same-sized raster implementation is sufficient
     * here: the fixture must not create a window or depend on Vulkan, while
     * gpu_world_consume still performs classification, texture measurement,
     * ABI packing and stream validation exactly as the normal path does. */
    renderer.surface.width = 64;
    renderer.surface.height = 64;
    renderer.surface.stride = 64 * (int)sizeof(*renderer.surface.pixels);
    renderer.surface.pixels = pixels;
    renderer.job_clear_color = 0x112233;
    frame->raster.implementation = (void *)1;
    frame->raster.width = 64;
    frame->raster.height = 64;
    frame->native_present = 1;
    frame->armed = 1;
    if (gpu_world_consume(&renderer, frame->retained_commands,
                          (int)frame->retained_command_count, &core) < 0 ||
        !frame->native_prepared || frame->stats.last_path != 1 ||
        frame->native_stream_size == 0)
        goto fail;
    packed_size = frame->native_stream_size;
    header = (struct rf_gpu_raster_stream_header_v1 *)(void *)frame->stream;
    packed = (struct rf_gpu_raster_cmd_v1 *)(void *)(header + 1);
    if (header->command_count != 12 ||
        packed[4].kind != RF_GPU_RASTER_CMD_BEGIN_TRANSPARENT_V1 ||
        packed[9].kind != RF_GPU_RASTER_CMD_BEGIN_VIEWMODEL_V1 ||
        !frame->native_prepared || frame->stats.last_path != 1 ||
        core.render_frame.pre_post_cpu_fallback ||
        rf_core_render_frame_fallback_reason_v1(&core.render_frame) !=
            RF_PRE_POST_FALLBACK_NONE ||
        rf_gpu_raster_validate_v1(frame->stream, packed_size) !=
            RF_GPU_RASTER_PACK_OK)
        goto fail;

    /* An unsupported command must reject the whole retained batch; it must
     * never be partially uploaded after the supported path above. */
    {
        struct toy_raster_cmd unsupported;
        memset(&unsupported, 0, sizeof(unsupported));
        unsupported.area = -64;
        unsupported.edge = 1;
        frame->native_prepared = 0;
        frame->armed = 1;
        if (gpu_world_consume(&renderer, &unsupported, 1, &core) >= 0 ||
            frame->stats.last_path != 2 ||
            !core.render_frame.pre_post_fallback_reason)
            goto fail;
    }

    /* B2d-3 reason contract: supported source-over remains reason-free, and
     * every unsupported class rejects before any native stream/resource is
     * marked ready.  The commands below deliberately use one command each so
     * the reason bits are mutually exclusive and easy to audit. */
    {
        struct toy_raster_cmd probe;
        struct toy_texture_view invalid_texture;
        unsigned int reason;

        memset(&invalid_texture, 0, sizeof(invalid_texture));
        memset(frame->retained_batch_count, 0,
               sizeof(frame->retained_batch_count));
        frame->native_present = 1;
        frame->native_prepared = 0;
        frame->native_stream_size = 0;
        frame->native_texture_count = 0;
        frame->native_texture_bytes = 0;

        probe = first[0];
        probe.transparent = 1;
        probe.material_alpha = 128;
        frame->armed = 1;
        core.render_frame.pre_post_fallback_reason = 0;
        if (gpu_world_consume(&renderer, &probe, 1, &core) < 0 ||
            !frame->native_prepared ||
            rf_core_render_frame_fallback_reason_v1(&core.render_frame) !=
                RF_PRE_POST_FALLBACK_NONE) {
            goto fail;
        }

        probe = first[0];
        probe.transparent = 1;
        probe.material_alpha = 128;
        probe.planar_vertex_lit = 1;
        frame->native_prepared = 0;
        frame->native_stream_size = 0;
        frame->native_texture_count = 0;
        frame->native_texture_bytes = 0;
        frame->armed = 1;
        core.render_frame.pre_post_fallback_reason = 0;
        if (gpu_world_consume(&renderer, &probe, 1, &core) < 0 ||
            !frame->native_prepared || !frame->native_stream_size ||
            rf_core_render_frame_fallback_reason_v1(&core.render_frame) !=
                RF_PRE_POST_FALLBACK_NONE) {
            goto fail;
        }

        probe = first[0];
        probe.textured = 1;
        probe.texture = &invalid_texture;
        probe.base_texture_valid = 0;
        frame->native_prepared = 0;
        frame->native_stream_size = 0;
        frame->native_texture_count = 0;
        frame->native_texture_bytes = 0;
        frame->armed = 1;
        core.render_frame.pre_post_fallback_reason = 0;
        if (gpu_world_consume(&renderer, &probe, 1, &core) >= 0 ||
            frame->native_prepared || frame->native_stream_size ||
            rf_core_render_frame_fallback_reason_v1(&core.render_frame) !=
                RF_PRE_POST_FALLBACK_UNSUPPORTED_TEXTURE) {
            goto fail;
        }

        probe = first[0];
        probe.edge = 1;
        frame->native_prepared = 0;
        frame->native_stream_size = 0;
        frame->armed = 1;
        core.render_frame.pre_post_fallback_reason = 0;
        if (gpu_world_consume(&renderer, &probe, 1, &core) >= 0 ||
            rf_core_render_frame_fallback_reason_v1(&core.render_frame) !=
                RF_PRE_POST_FALLBACK_UNSUPPORTED_EDGE)
            goto fail;

        probe = first[0];
        probe.overlay = 1;
        frame->native_prepared = 0;
        frame->native_stream_size = 0;
        frame->armed = 1;
        core.render_frame.pre_post_fallback_reason = 0;
        if (gpu_world_consume(&renderer, &probe, 1, &core) >= 0 ||
            rf_core_render_frame_fallback_reason_v1(&core.render_frame) !=
                RF_PRE_POST_FALLBACK_UNSUPPORTED_OVERLAY)
            goto fail;

        probe = first[0];
        probe.area = 0;
        frame->native_prepared = 0;
        frame->native_stream_size = 0;
        frame->armed = 1;
        core.render_frame.pre_post_fallback_reason = 0;
        if (gpu_world_consume(&renderer, &probe, 1, &core) >= 0 ||
            rf_core_render_frame_fallback_reason_v1(&core.render_frame) !=
                RF_PRE_POST_FALLBACK_UNSUPPORTED_GENERIC_COMMAND)
            goto fail;

        /* A failure after classification/packing is a consumer failure, not
         * a material or texture diagnosis.  An unavailable raster backend
         * makes resize fail, while the retained replay also stays all-or-
         * nothing because this metadata fixture has no command capacity. */
        frame->retained_commands[0] = first[0];
        frame->retained_command_count = 1;
        frame->retained_batch_count[RF_RENDER_LAYER_WORLD] = 1;
        frame->retained_world_raw_count = 0;
        frame->retaining_pre_post = 0;
        frame->native_prepared = 0;
        frame->native_stream_size = 0;
        frame->raster.width = 63;
        frame->raster.height = 64;
        core.render_frame.pre_post_cpu_fallback = 0;
        core.render_frame.pre_post_fallback_reason = 0;
        if (gpu_pre_post_finalize(&core) >= 0 || frame->native_prepared ||
            rf_core_render_frame_fallback_reason_v1(&core.render_frame) !=
                RF_PRE_POST_FALLBACK_CONSUMER_FAILURE)
            goto fail;
        reason = rf_core_render_frame_fallback_reason_v1(&core.render_frame);
        if (reason & (RF_PRE_POST_FALLBACK_UNSUPPORTED_MATERIAL |
                      RF_PRE_POST_FALLBACK_UNSUPPORTED_TEXTURE |
                      RF_PRE_POST_FALLBACK_UNSUPPORTED_EDGE |
                      RF_PRE_POST_FALLBACK_UNSUPPORTED_OVERLAY |
                      RF_PRE_POST_FALLBACK_UNSUPPORTED_GENERIC_COMMAND))
            goto fail;
    }
    tlibc_free(frame->retained_commands);
    tlibc_free(frame->stream);
    tlibc_free(frame->texture_descs);
    tlibc_free(frame->texture_texels);
    return 0;
fail:
    tlibc_free(frame->retained_commands);
    tlibc_free(frame->stream);
    tlibc_free(frame->texture_descs);
    tlibc_free(frame->texture_texels);
    return -1;
}

static int rf_core_transparent_command_equal_v1(
    const struct toy_raster_cmd *a, const struct toy_raster_cmd *b)
{
    if (!a || !b || a->textured != b->textured ||
        a->material_alpha != b->material_alpha ||
        a->transparent != b->transparent ||
        a->transparent_no_depth_write != b->transparent_no_depth_write ||
        a->color != b->color || a->area != b->area ||
        a->texture != b->texture)
        return 0;
    return a->a.x == b->a.x && a->a.y == b->a.y && a->a.z == b->a.z &&
           a->a.inv_z == b->a.inv_z && a->b.x == b->b.x &&
           a->b.y == b->b.y && a->b.z == b->b.z &&
           a->b.inv_z == b->b.inv_z && a->c.x == b->c.x &&
           a->c.y == b->c.y && a->c.z == b->c.z &&
           a->c.inv_z == b->c.inv_z;
}

int rf_core_transparent_command_logic_test_v1(
    const struct toy_raster_cmd *commands, int count)
{
    struct rf_core core;
    struct toy_renderer renderer;
    struct rf_core_gpu_frame *frame;
    unsigned int pixel = 0;
    unsigned long opaque_count = 0, transparent_count = 0;
    unsigned long unique_textures = 0;
    unsigned long opaque_at, transparent_at, i;
    int result = -1, consumed;

    if (!commands || count <= 0 || count > 4096)
        return -1;
    memset(&core, 0, sizeof(core));
    memset(&renderer, 0, sizeof(renderer));
    renderer.surface.width = 320;
    renderer.surface.height = 180;
    renderer.surface.stride = 320 * (int)sizeof(*renderer.surface.pixels);
    /* This helper only partitions and packs commands; it never rasterizes.
     * A single placeholder pixel avoids making producer gates depend on the
     * freestanding test heap while retaining the real surface extent. */
    renderer.surface.pixels = &pixel;
    renderer.cmds = (struct toy_raster_cmd *)commands;
    renderer.cmd_count = count;
    renderer.cmd_cap = count;
    renderer.job_clear_color = 0x112233;
    core.renderer = &renderer;
    frame = &core.gpu_frame;
    if (rf_core_render_frame_enter_layer_v1(&core, RF_RENDER_LAYER_WORLD) < 0)
        goto done;
    frame->retaining_pre_post = 1;
    toy_renderer_set_command_consumer(&renderer,
                                      gpu_pre_post_retain_consume, &core);
    if (toy_renderer_flush(&renderer) < 0 || renderer.cmd_count != 0 ||
        frame->retained_command_count != (unsigned long)count ||
        frame->retained_world_raw_count != (unsigned long)count)
        goto done;
    for (i = 0; i < (unsigned long)count; ++i)
        if (rf_core_cmd_is_transparent_v1(&commands[i]))
            ++transparent_count;
        else
            ++opaque_count;
    for (i = 0; i < (unsigned long)count; ++i) {
        unsigned long j;
        if (!commands[i].textured) continue;
        for (j = 0; j < i; ++j)
            if (commands[j].textured &&
                commands[j].texture == commands[i].texture)
                break;
        if (j == i) ++unique_textures;
    }
    if (gpu_pre_post_partition_world(frame) < 0 ||
        frame->retained_world_raw_count != 0 ||
        frame->retained_batch_count[RF_RENDER_LAYER_WORLD] != opaque_count ||
        frame->retained_batch_count[RF_RENDER_LAYER_TRANSPARENT] !=
            transparent_count || frame->retained_command_count !=
            (unsigned long)count)
        goto done;
    opaque_at = transparent_at = 0;
    for (i = 0; i < (unsigned long)count; ++i) {
        const struct toy_raster_cmd *source = &commands[i];
        const struct toy_raster_cmd *retained;
        if (rf_core_cmd_is_transparent_v1(source)) {
            retained = &frame->retained_commands[opaque_count + transparent_at++];
        } else {
            retained = &frame->retained_commands[opaque_at++];
        }
        if (!rf_core_transparent_command_equal_v1(source, retained))
            goto done;
    }
    frame->retaining_pre_post = 0;
    frame->raster.implementation = (void *)1;
    frame->raster.width = 320;
    frame->raster.height = 180;
    frame->native_present = 1;
    frame->armed = 1;
    consumed = gpu_world_consume(&renderer, frame->retained_commands, count,
                                 &core);
    if (consumed < 0 ||
        !frame->native_prepared || !frame->native_stream_size ||
        frame->native_texture_count != unique_textures ||
        frame->stats.last_path != 1 ||
        rf_core_render_frame_fallback_reason_v1(&core.render_frame) !=
            RF_PRE_POST_FALLBACK_NONE)
        goto done;
    result = 0;
done:
    toy_renderer_set_command_consumer(&renderer, NULL, NULL);
    tlibc_free(frame->retained_commands);
    tlibc_free(frame->stream);
    tlibc_free(frame->texture_descs);
    tlibc_free(frame->texture_texels);
    return result;
}

static int gpu_pre_post_replay_cpu(struct rf_core *core)
{
    struct rf_core_gpu_frame *frame = &core->gpu_frame;
    struct toy_renderer *renderer = core->renderer;
    unsigned long offset = 0;
    unsigned int layer;
    int total = 0;
    toy_renderer_set_command_consumer(renderer, NULL, NULL);
    for (layer = RF_RENDER_LAYER_WORLD;
         layer <= RF_RENDER_LAYER_VIEWMODEL; ++layer) {
        unsigned long count = frame->retained_batch_count[layer];
        int flushed;
        if (!count) continue;
        if (layer == RF_RENDER_LAYER_VIEWMODEL &&
            rf_core_viewmodel_begin_v1(core) < 0) {
            toy_renderer_set_command_consumer(renderer,
                gpu_pre_post_retain_consume, core);
            return -1;
        }
        if (count > (unsigned long)renderer->cmd_cap) {
            toy_renderer_set_command_consumer(renderer,
                gpu_pre_post_retain_consume, core);
            return -1;
        }
        memcpy(renderer->cmds, frame->retained_commands + offset,
               count * sizeof(*renderer->cmds));
        renderer->cmd_count = (int)count;
        flushed = toy_renderer_flush(renderer);
        if (flushed < 0) {
            toy_renderer_set_command_consumer(renderer,
                gpu_pre_post_retain_consume, core);
            return -1;
        }
        total += flushed;
        offset += count;
        if (layer == RF_RENDER_LAYER_VIEWMODEL)
            rf_core_viewmodel_end_v1(core);
    }
    toy_renderer_set_command_consumer(renderer, gpu_pre_post_retain_consume,
                                      core);
    return total;
}

static int gpu_pre_post_finalize(struct rf_core *core)
{
    struct rf_core_gpu_frame *frame = &core->gpu_frame;
    int unsupported_post_world;
    int consumed = -1;

    /* Freeze the physical layer contract once all WORLD flushes have arrived:
     * WORLD opaque, WORLD transparent, EFFECTS, VIEWMODEL.  Relative order is
     * preserved inside both WORLD spans and no depth/material sort occurs. */
    if (gpu_pre_post_partition_world(frame) < 0) {
        frame->retaining_pre_post = 0;
        frame->native_prepared = 0;
        frame->stats.frames_attempted++;
        frame->stats.cpu_fallback_frames++;
        frame->stats.last_path = 2;
        core->render_frame.pre_post_cpu_fallback = 1;
        core->render_frame.pre_post_fallback_reason |=
            RF_PRE_POST_FALLBACK_CONSUMER_FAILURE;
        if (frame->strict_gpu_only) {
            frame->runtime_failed = 1;
            gpu_world_log("gpu-required: retained WORLD partition failed");
            return -1;
        }
        return gpu_pre_post_replay_cpu(core) < 0 ? -1 : 0;
    }
    frame->retaining_pre_post = 0;
    core->render_frame.pre_post_fallback_reason |=
        rf_core_render_frame_fallback_reason_v1(&core->render_frame);
    unsupported_post_world =
        core->render_frame.invalid_layer_transitions != 0 ||
        core->render_frame.direct_pixel_count[RF_RENDER_LAYER_WORLD] != 0 ||
        core->render_frame.direct_pixel_count[RF_RENDER_LAYER_TRANSPARENT] != 0 ||
        core->render_frame.direct_pixel_count[RF_RENDER_LAYER_EFFECTS] != 0 ||
        core->render_frame.direct_pixel_count[RF_RENDER_LAYER_VIEWMODEL] != 0;
    if (!unsupported_post_world && frame->retained_command_count) {
        frame->armed = 1;
        consumed = gpu_world_consume(core->renderer,
            frame->retained_commands, (int)frame->retained_command_count, core);
    } else if (unsupported_post_world) {
        frame->stats.frames_attempted++;
        frame->stats.cpu_fallback_frames++;
        frame->stats.last_path = 2;
        core->render_frame.pre_post_cpu_fallback = 1;
    }
    if (consumed < 0) {
        int replay_result;
        frame->native_prepared = 0;
        core->render_frame.pre_post_cpu_fallback = 1;
        if (!core->render_frame.pre_post_fallback_reason)
            core->render_frame.pre_post_fallback_reason |=
                RF_PRE_POST_FALLBACK_CONSUMER_FAILURE;
        if (frame->strict_gpu_only) {
            frame->runtime_failed = 1;
            gpu_world_log("gpu-required: unsupported command/direct pixel/consumer failure");
            return -1;
        }
        replay_result = gpu_pre_post_replay_cpu(core);
        /* Never alternate SDL software presentation and the Win32 Vulkan
         * swapchain on one window after an unsupported normal frame.  Tear
         * down native GPU ownership before the replayed frame is presented,
         * then keep this run on the compatibility renderer. */
        if (frame->native_present) {
            toy_renderer_set_command_consumer(core->renderer, NULL, NULL);
            rf_gpu_raster_shutdown(&frame->raster);
            rf_gpu_shutdown(&core->gpu);
            frame->native_present = 0;
            frame->initialized = 0;
            frame->renderer = RF_CORE_RENDERER_CPU;
            gpu_world_log(
                "gpu-world: native GPU disabled after whole-frame CPU fallback");
        }
        return replay_result < 0 ? -1 : 0;
    }
    return 0;
}

int rf_core_init(struct rf_core *core, const char *title, int width, int height,
                 struct toy_input *input, struct toy_renderer *renderer)
{
    if (!core || !input || !renderer) return -1;
    memset(core, 0, sizeof(*core));
    if (rf_core_filesystem_init(&core->filesystem) < 0) return -1;
    core->input = input;
    core->renderer = renderer;
    toy_input_init(core->input);
    toy_renderer_init(core->renderer);
    core->window = toy_window_open(title, width, height);
    if (!core->window) {
        toy_renderer_destroy(core->renderer);
        rf_core_filesystem_shutdown(&core->filesystem);
        return -1;
    }
    /* Audio is optional, matching the existing Rasterfall startup policy. */
    if (toy_audio_open(&core->audio, 48000, 2) == 0)
        core->audio_ready = 1;
    core->initialized = 1;
    return 0;
}

int rf_core_init_config(struct rf_core *core,
                        const struct rf_core_config *config)
{
    int result;
    if (!config) return -1;
    result = rf_core_init(core, config->title, config->width, config->height,
                          config->input, config->renderer);
    if (result < 0) return result;
    if (config->native_present && config->gpu_backend) {
        struct toy_native_window_handle native;
        struct rf_gpu_native_window gpu_native;
        int native_result = toy_window_get_native_handle(core->window, &native);
        memset(&gpu_native, 0, sizeof(gpu_native));
        if (native_result > 0) {
            gpu_native.type = native.type;
            gpu_native.window = native.window;
            gpu_native.instance = native.instance;
        }
        if (native_result <= 0 || rf_gpu_set_native_window(config->gpu_backend,
                config->gpu_backend_context, &gpu_native) < 0) {
            if (config->gpu_policy == RF_GPU_POLICY_REQUIRED) {
                rf_core_shutdown(core);
                return -1;
            }
        }
    }
    if (rf_gpu_init(&core->gpu, config->gpu_policy, config->gpu_backend,
                    config->gpu_backend_context) < 0) {
        rf_core_shutdown(core);
        return -1;
    }
    core->gpu_frame.renderer = config->renderer_mode;
    core->gpu_frame.strict_gpu_only =
        config->gpu_policy == RF_GPU_POLICY_REQUIRED;
    core->gpu_frame.native_present = config->native_present != 0;
    if (core->gpu_frame.native_present) {
        struct rf_gpu_status gpu_status;
        if (rf_gpu_get_status(&core->gpu, &gpu_status) < 0 ||
            !gpu_status.renderer.native_presentation_v1) {
            if (core->gpu_frame.strict_gpu_only) {
                __fprintf(2, "gpu-required: native presentation V1 unsupported\n");
                rf_core_shutdown(core);
                return -1;
            }
            __printf("GPU native presentation V1 unsupported; using software-present fallback\n");
            core->gpu_frame.native_present = 0;
        }
    }
    if (config->renderer_mode == RF_CORE_RENDERER_GPU_COMPUTE) {
        if (rf_gpu_raster_init(&core->gpu, &core->gpu_frame.raster,
                (unsigned int)config->width, (unsigned int)config->height) < 0) {
            if (config->gpu_policy == RF_GPU_POLICY_REQUIRED) {
                rf_core_shutdown(core);
                return -1;
            }
            core->gpu_frame.renderer = RF_CORE_RENDERER_CPU;
        } else {
            core->gpu_frame.initialized = 1;
            if (config->gpu_post_fog && core->gpu_frame.native_present) {
                struct rf_gpu_post_params_v1 post;
                memset(&post,0,sizeof(post));
                post.mode=RF_GPU_POST_DEPTH_FOG_V0;
                /* Raster depth is Q20 inverse Z.  512--4096 RFU provides a
                 * monotonic diagnostic fog without claiming metre units. */
                post.fog_near_inv_z=1048576/512;
                post.fog_far_inv_z=1048576/4096;
                post.fog_color=0xff7890a0U;
                post.max_density_q8=192;
                if (rf_gpu_raster_set_post(&core->gpu_frame.raster,&post)<0) {
                    if (core->gpu_frame.strict_gpu_only) {
                        __fprintf(2, "gpu-required: requested GPU Post-Raster V1 unavailable\n");
                        rf_core_shutdown(core);
                        return -1;
                    }
                    __printf("GPU Post-Raster V1 unavailable; bypassing post pass\n");
                }
            }
            toy_renderer_set_command_consumer(core->renderer,
                                               gpu_pre_post_retain_consume,
                                               core);
        }
    }
    return 0;
}

int rf_core_init_headless(struct rf_core *core, struct toy_input *input,
                          struct toy_renderer *renderer)
{
    if (!core || !input || !renderer) return -1;
    memset(core, 0, sizeof(*core));
    if (rf_core_filesystem_init(&core->filesystem) < 0) return -1;
    core->input = input;
    core->renderer = renderer;
    toy_input_init(core->input);
    toy_renderer_init(core->renderer);
    core->initialized = 1;
    return 0;
}

int rf_core_poll_events(struct rf_core *core)
{
    return rf_core_poll_events_timeout(core, 0);
}

int rf_core_poll_events_timeout(struct rf_core *core, int timeout_ms)
{
    if (!core || !core->window) return -1;
    toy_input_begin_frame(core->input);
    if (toy_window_poll(core->window, &core->events, timeout_ms) < 0) return -1;
    toy_input_apply(core->input, &core->events);
    if (core->events.close_requested) core->exit_requested = 1;
    return 0;
}

int64_t rf_core_begin_tick(struct rf_core *core)
{
    return rf_core_time_us(core);
}

int rf_core_should_exit(const struct rf_core *core)
{
    return !core || !core->initialized || core->exit_requested ||
           core->gpu_frame.runtime_failed;
}

int rf_core_runtime_failed(const struct rf_core *core)
{
    return !core || core->gpu_frame.runtime_failed;
}

int rf_core_begin_frame(struct rf_core *core, uint32_t clear_color)
{
    int ready;
    if (!core || !core->window) return -1;
    if (core->viewmodel_active) rf_core_viewmodel_end_v1(core);
    ready = toy_window_begin_frame(core->window, &core->surface);
    if (ready <= 0) return ready;
    if (toy_renderer_begin(core->renderer, &core->surface, clear_color) < 0)
        return -1;
    core->world_depth = core->renderer->depth;
    core->viewmodel_active = 0;
    core->gpu_frame.frame_begin_us = rf_core_clock_now_us();
    core->gpu_frame.native_prepared = 0;
    core->gpu_frame.overlay_active = 0;
    core->gpu_frame.retaining_pre_post = 0;
    core->gpu_frame.retained_command_count = 0;
    core->gpu_frame.retained_world_raw_count = 0;
    memset(core->gpu_frame.retained_batch_count, 0,
           sizeof(core->gpu_frame.retained_batch_count));
    core->gpu_frame.stats.last_path = 0;
    return ready;
}

void rf_core_render_frame_begin_v1(struct rf_core *core, int camera_x,
                                  int camera_z, int direction_sy,
                                  int direction_cy, int pitch_sy,
                                  int pitch_cy)
{
    unsigned long long next;
    if (!core) return;
    next = core->render_frame.frame_id + 1;
    memset(&core->render_frame, 0, sizeof(core->render_frame));
    core->render_frame.frame_id = next;
    core->render_frame.camera_x = camera_x;
    core->render_frame.camera_z = camera_z;
    core->render_frame.direction_sy = direction_sy;
    core->render_frame.direction_cy = direction_cy;
    core->render_frame.pitch_sy = pitch_sy;
    core->render_frame.pitch_cy = pitch_cy;
    core->render_frame.width = core->surface.width;
    core->render_frame.height = core->surface.height;
    core->render_frame.current_layer = RF_RENDER_LAYER_SKY;
    core->render_frame.sky_enabled = 1;
    core->render_frame.viewmodel_near_z = RF_VIEWMODEL_NEAR_Z_V1;
}

void rf_core_render_frame_record_v1(struct rf_core *core,
                                    enum rf_render_layer_v1 layer,
                                    unsigned long commands,
                                    unsigned long pixels)
{
    if (!core || layer < 0 || layer >= RF_RENDER_LAYER_COUNT) return;
    if ((unsigned int)layer != core->render_frame.current_layer) {
        core->render_frame.invalid_layer_transitions++;
        return;
    }
    core->render_frame.command_count[layer] += commands;
    core->render_frame.pixel_count[layer] += pixels;
}

void rf_core_render_frame_record_direct_pixels_v1(
    struct rf_core *core, enum rf_render_layer_v1 layer,
    unsigned long pixels)
{
    if (!core || layer < 0 || layer >= RF_RENDER_LAYER_COUNT) return;
    if ((unsigned int)layer != core->render_frame.current_layer) {
        core->render_frame.invalid_layer_transitions++;
        return;
    }
    core->render_frame.direct_pixel_count[layer] += pixels;
}

int rf_core_viewmodel_begin_v1(struct rf_core *core)
{
    unsigned long pixels;
    unsigned long i;
    if (!core || !core->renderer) return -1;
    if (!core->renderer->surface.pixels || core->renderer->surface.width <= 0 ||
        core->renderer->surface.height <= 0)
        return 0; /* Metadata-only frame tests have no raster target. */
    pixels = (unsigned long)core->renderer->surface.width *
             (unsigned long)core->renderer->surface.height;
    if (!pixels) return -1;
    if (pixels > core->viewmodel_pixel_capacity) {
        int *depth = tlibc_malloc(pixels * sizeof(*depth));
        unsigned char *coverage = tlibc_malloc(pixels);
        if (!depth || !coverage) {
            tlibc_free(depth); tlibc_free(coverage); return -1;
        }
        tlibc_free(core->viewmodel_depth);
        tlibc_free(core->viewmodel_coverage);
        core->viewmodel_depth = depth;
        core->viewmodel_coverage = coverage;
        core->viewmodel_pixel_capacity = pixels;
    }
    for (i = 0; i < pixels; ++i) core->viewmodel_depth[i] = 0;
    memset(core->viewmodel_coverage, 0, pixels);
    core->viewmodel_pixel_count = pixels;
    if (!core->world_depth) core->world_depth = core->renderer->depth;
    core->renderer->depth = core->viewmodel_depth;
    toy_renderer_bind_coverage(core->renderer, core->viewmodel_coverage,
                                core->renderer->surface.width);
    core->viewmodel_active = 1;
    core->render_frame.viewmodel_near_z = RF_VIEWMODEL_NEAR_Z_V1;
    return 0;
}

int rf_core_viewmodel_end_v1(struct rf_core *core)
{
    if (!core || !core->renderer) return -1;
    if (!core->viewmodel_active) return 0;
    core->renderer->depth = core->world_depth ? core->world_depth :
                            core->renderer->depth;
    toy_renderer_bind_coverage(core->renderer, NULL, 0);
    core->viewmodel_active = 0;
    core->render_frame.viewmodel_coverage_pixels =
        rf_core_viewmodel_coverage_count_v1(core);
    return 0;
}

const int *rf_core_viewmodel_depth_v1(const struct rf_core *core)
{
    return core ? core->viewmodel_depth : NULL;
}

const unsigned char *rf_core_viewmodel_coverage_v1(const struct rf_core *core)
{
    return core ? core->viewmodel_coverage : NULL;
}

unsigned long rf_core_viewmodel_coverage_count_v1(const struct rf_core *core)
{
    unsigned long count = 0, i;
    if (!core || !core->viewmodel_coverage) return 0;
    for (i = 0; i < core->viewmodel_pixel_count; ++i)
        if (core->viewmodel_coverage[i]) ++count;
    return count;
}

void rf_core_render_frame_record_world_v1(
    struct rf_core *core, const struct toy_raster_cmd *commands, int count)
{
    unsigned long transparent = 0;
    int i;
    if (!core || !commands || count <= 0) return;
    if (core->render_frame.current_layer != RF_RENDER_LAYER_WORLD) {
        core->render_frame.invalid_layer_transitions++;
        return;
    }
    for (i = 0; i < count; ++i)
        if (rf_core_cmd_is_transparent_v1(&commands[i]))
            transparent++;
    core->render_frame.command_count[RF_RENDER_LAYER_WORLD] +=
        (unsigned long)count - transparent;
    core->render_frame.command_count[RF_RENDER_LAYER_TRANSPARENT] +=
        transparent;
}

int rf_core_render_frame_enter_layer_v1(
    struct rf_core *core, enum rf_render_layer_v1 layer)
{
    if (!core || layer < RF_RENDER_LAYER_SKY ||
        layer >= RF_RENDER_LAYER_COUNT) return -1;
    if ((unsigned int)layer < core->render_frame.current_layer ||
        (unsigned int)layer > core->render_frame.current_layer + 1U) {
        core->render_frame.invalid_layer_transitions++;
        return -1;
    }
    core->render_frame.current_layer = (unsigned int)layer;
    if (layer == RF_RENDER_LAYER_VIEWMODEL &&
        rf_core_viewmodel_begin_v1(core) < 0) {
        core->render_frame.invalid_layer_transitions++;
        return -1;
    }
    return 0;
}

int rf_core_get_render_frame_v1(const struct rf_core *core,
                                struct rf_render_frame_v1 *frame)
{
    if (!core || !frame) return -1;
    *frame = core->render_frame;
    return 0;
}

unsigned int rf_core_render_frame_fallback_reason_v1(
    const struct rf_render_frame_v1 *frame)
{
    unsigned int reason = RF_PRE_POST_FALLBACK_NONE;
    if (!frame) return reason;
    reason = frame->pre_post_fallback_reason;
    if (frame->direct_pixel_count[RF_RENDER_LAYER_EFFECTS])
        reason |= RF_PRE_POST_FALLBACK_EFFECTS_DIRECT_PIXELS;
    if (frame->direct_pixel_count[RF_RENDER_LAYER_VIEWMODEL])
        reason |= RF_PRE_POST_FALLBACK_VIEWMODEL_DIRECT_PIXELS;
    return reason;
}

struct toy_surface *rf_core_begin_screen_overlay(struct rf_core *core)
{
    struct rf_core_gpu_frame *frame;
    unsigned long pixels;
    if (!core || core->render_frame.current_layer != RF_RENDER_LAYER_VIEWMODEL)
        return NULL;
    if (rf_core_viewmodel_end_v1(core) < 0) return NULL;
    if (core->gpu_frame.retaining_pre_post &&
        gpu_pre_post_finalize(core) < 0) return NULL;
    if (rf_core_render_frame_enter_layer_v1(
            core, RF_RENDER_LAYER_OVERLAY) < 0) return NULL;
    if (!core->gpu_frame.native_present ||
        !core->gpu_frame.native_prepared) {
        core->renderer->surface = core->surface;
        return &core->surface;
    }
    frame = &core->gpu_frame;
    pixels = (unsigned long)core->surface.width * core->surface.height;
    if (!pixels) return NULL;
    if (pixels > frame->overlay_pixel_capacity) {
        unsigned int *color = tlibc_malloc(pixels * sizeof(*color));
        unsigned char *coverage = tlibc_malloc(pixels);
        if (!color || !coverage) {
            tlibc_free(color); tlibc_free(coverage); return NULL;
        }
        tlibc_free(frame->overlay_surface.pixels);
        tlibc_free(frame->overlay_coverage);
        frame->overlay_surface.pixels = color;
        frame->overlay_coverage = coverage;
        frame->overlay_pixel_capacity = pixels;
    }
    frame->overlay_surface.width = core->surface.width;
    frame->overlay_surface.height = core->surface.height;
    frame->overlay_surface.stride = core->surface.width * (int)sizeof(uint32_t);
    memset(frame->overlay_surface.pixels, 0, pixels * sizeof(uint32_t));
    memset(frame->overlay_coverage, 0, pixels);
    fb_coverage_bind((unsigned char *)frame->overlay_surface.pixels,
                     frame->overlay_coverage, core->surface.width,
                     core->surface.height, core->surface.width);
    frame->overlay_active = 1;
    frame->overlay_draw_begin_us = rf_core_clock_now_us();
    /* Overlay helpers historically receive either a surface or the renderer.
     * Make both routes target the one Core-owned post-stage overlay truth. */
    core->renderer->surface = frame->overlay_surface;
    return &frame->overlay_surface;
}

int rf_core_end_frame(struct rf_core *core)
{
    int64_t present_start;
    int result;
    if (!core || !core->window) return -1;
    if (toy_renderer_flush(core->renderer) < 0) return -1;
    if (core->gpu_frame.overlay_active) {
        fb_coverage_unbind();
        core->gpu_frame.overlay_active = 0;
        core->gpu_frame.stats.overlay_cpu_draw_ms =
            (double)(rf_core_clock_now_us() -
                     core->gpu_frame.overlay_draw_begin_us) / 1000.0;
    }
    if (core->gpu_frame.native_prepared) {
        struct rf_core_gpu_frame *frame = &core->gpu_frame;
        if (rf_gpu_raster_present_textured_timed(&core->gpu, &frame->raster,
                frame->stream, frame->native_stream_size,
                frame->texture_descs, frame->native_texture_count,
                frame->texture_texels, frame->native_texture_bytes,
                frame->overlay_surface.pixels, frame->overlay_coverage,
                (unsigned int)frame->overlay_surface.width,
                (unsigned int)frame->overlay_surface.width,
                (unsigned int)core->surface.width,
                (unsigned int)core->surface.height, &frame->stats.last_timing,
                &frame->stats.native_present_timing) < 0) {
            if (frame->strict_gpu_only) frame->runtime_failed = 1;
            return -1;
        }
        if (frame->strict_gpu_only &&
            (frame->stats.native_present_timing.color_readback_bytes ||
             frame->stats.native_present_timing.cpu_framebuffer_copy_bytes)) {
            frame->runtime_failed = 1;
            gpu_world_log("gpu-required: native frame performed forbidden readback/copy");
            return -1;
        }
        frame->native_prepared = 0;
        frame->native_presented = 1;
        core->render_frame.layer_backend[RF_RENDER_LAYER_SKY] =
            RF_RENDER_BACKEND_GPU;
        core->render_frame.layer_backend[RF_RENDER_LAYER_WORLD] =
            RF_RENDER_BACKEND_GPU;
        core->render_frame.layer_backend[RF_RENDER_LAYER_TRANSPARENT] =
            RF_RENDER_BACKEND_GPU;
        core->render_frame.layer_backend[RF_RENDER_LAYER_EFFECTS] =
            core->render_frame.direct_pixel_count[RF_RENDER_LAYER_EFFECTS] ?
            RF_RENDER_BACKEND_UNSUPPORTED : RF_RENDER_BACKEND_GPU;
        core->render_frame.layer_backend[RF_RENDER_LAYER_VIEWMODEL] =
            RF_RENDER_BACKEND_GPU;
        core->render_frame.layer_backend[RF_RENDER_LAYER_OVERLAY] =
            RF_RENDER_BACKEND_COMPOSITE;
        frame->stats.overlay_upload_bytes +=
            frame->stats.native_present_timing.overlay_upload_bytes;
        frame->stats.overlay_composite_frames++;
    }
    if (core->gpu_frame.native_presented) {
        core->gpu_frame.native_presented = 0;
        return 0;
    }
    if (core->gpu_frame.strict_gpu_only) {
        core->gpu_frame.runtime_failed = 1;
        gpu_world_log("gpu-required: frame reached CPU presentation");
        return -1;
    }
    core->render_frame.layer_backend[RF_RENDER_LAYER_SKY] =
        RF_RENDER_BACKEND_CPU;
    core->render_frame.layer_backend[RF_RENDER_LAYER_WORLD] =
        RF_RENDER_BACKEND_CPU;
    core->render_frame.layer_backend[RF_RENDER_LAYER_TRANSPARENT] =
        RF_RENDER_BACKEND_CPU;
    core->render_frame.layer_backend[RF_RENDER_LAYER_EFFECTS] =
        RF_RENDER_BACKEND_CPU;
    core->render_frame.layer_backend[RF_RENDER_LAYER_VIEWMODEL] =
        RF_RENDER_BACKEND_CPU;
    core->render_frame.layer_backend[RF_RENDER_LAYER_OVERLAY] =
        RF_RENDER_BACKEND_CPU;
    present_start = rf_core_clock_now_us();
    result = toy_window_present(core->window);
    core->gpu_frame.stats.present_ms =
        (double)(rf_core_clock_now_us() - present_start) / 1000.0;
    if (core->gpu_frame.frame_begin_us)
        core->gpu_frame.stats.frame_total_ms =
            (double)(rf_core_clock_now_us() -
                     core->gpu_frame.frame_begin_us) / 1000.0;
    return result;
}

void rf_core_gpu_world_begin(struct rf_core *core)
{
    if (core && core->gpu_frame.initialized) {
        core->gpu_frame.frontend_begin_us = rf_core_clock_now_us();
        core->gpu_frame.retaining_pre_post = 1;
        core->gpu_frame.retained_command_count = 0;
        core->gpu_frame.retained_world_raw_count = 0;
        memset(core->gpu_frame.retained_batch_count, 0,
               sizeof(core->gpu_frame.retained_batch_count));
    }
}

void rf_core_gpu_world_flush(struct rf_core *core)
{
    if (core && core->gpu_frame.initialized) core->gpu_frame.armed = 1;
}

int rf_core_get_gpu_frame_stats(const struct rf_core *core,
                                struct rf_core_gpu_frame_stats *stats)
{
    if (!core || !stats) return -1;
    *stats = core->gpu_frame.stats;
    return 0;
}

int rf_core_flush(struct rf_core *core)
{
    if (!core || !core->window || !core->renderer) return -1;
    return toy_renderer_flush(core->renderer);
}

void rf_core_shutdown(struct rf_core *core)
{
    if (!core) return;
    /* A failed frame may leave the renderer bound to the borrowed VM domain;
     * restore the renderer-owned depth before freeing Core-owned buffers. */
    rf_core_viewmodel_end_v1(core);
    if (core->gpu_frame.stats.frames_attempted) {
        struct rf_core_gpu_frame_stats *s = &core->gpu_frame.stats;
        __printf("GPU-FRAME attempted=%llu rendered=%llu cpu_fallback=%llu "
                 "unsupported_texture=%llu unsupported_transparent=%llu "
                 "unsupported_overlay=%llu unsupported_edge=%llu other=%llu\n",
                 s->frames_attempted, s->gpu_frames, s->cpu_fallback_frames,
                 s->unsupported_texture, s->unsupported_transparent,
                 s->unsupported_overlay, s->unsupported_edge,
                 s->unsupported_other);
        __printf("GPU-FRAME timing-ms binning=%.3f tile-upload=%.3f "
                 "command-upload=%.3f texture-upload=%.3f submit=%.3f execution-wait=%.3f "
                 "readback=%.3f total=%.3f\n",
                 s->last_timing.cpu_binning_ms, s->last_timing.tile_upload_ms,
                 s->last_timing.command_upload_ms,
                 s->last_timing.texture_upload_ms, s->last_timing.submit_ms,
                 s->last_timing.execution_wait_ms, s->last_timing.readback_ms,
                 s->last_timing.total_ms);
        __printf("GPU-FRAME textures commands=%llu unique=%u bytes=%llu\n",
                 s->texture_commands, s->unique_textures,
                 s->texture_upload_bytes);
        __printf("GPU-FRAME oracle frames=%llu failures=%llu color-mismatch=%llu "
                 "depth-mismatch=%llu max-color-delta=%u max-depth-delta=%llu\n",
                 s->oracle_frames, s->oracle_failures,
                 s->oracle_color_mismatches, s->oracle_depth_mismatches,
                 s->oracle_max_color_delta, s->oracle_max_depth_delta);
        __printf("GPU-FRAME stages-ms frontend=%.3f classification=%.3f "
                 "texture-measure=%.3f raster-abi-pack+texture-table=%.3f "
                 "cpu-oracle=%.3f presentation-copy=combined-with-readback "
                 "present=%.3f frame-total=%.3f\n",
                 s->frontend_ms, s->classification_ms,
                 s->texture_measure_ms, s->raster_abi_pack_ms,
                 s->cpu_oracle_ms, s->present_ms, s->frame_total_ms);
        if (core->gpu_frame.native_present)
            __printf("GPU-NATIVE overlay-composite=ready acquire=%.3f raster=%.3f "
                     "overlay-draw=%.3f overlay-upload=%.3f overlay-composite=%.3f "
                     "buffer-copy=%.3f submit=%.3f present=%.3f total=%.3f "
                     "overlay-bytes=%u color-readback=%u cpu-framebuffer-copy=%u "
                     "format=%u mode=%u images=%u extent=%ux%u\n",
                     s->native_present_timing.acquire_ms,
                     s->native_present_timing.gpu_raster_ms,
                     s->overlay_cpu_draw_ms,
                     s->native_present_timing.overlay_upload_ms,
                     s->native_present_timing.overlay_composite_ms,
                     s->native_present_timing.buffer_to_swapchain_ms,
                     s->native_present_timing.submit_ms,
                     s->native_present_timing.present_ms,
                     s->native_present_timing.total_ms,
                     s->native_present_timing.overlay_upload_bytes,
                     s->native_present_timing.color_readback_bytes,
                     s->native_present_timing.cpu_framebuffer_copy_bytes,
                     s->native_present_timing.format,
                     s->native_present_timing.present_mode,
                     s->native_present_timing.image_count,
                     s->native_present_timing.width,
                     s->native_present_timing.height);
    }
    if (core->renderer)
        toy_renderer_set_command_consumer(core->renderer, NULL, NULL);
    rf_gpu_raster_shutdown(&core->gpu_frame.raster);
    tlibc_free(core->gpu_frame.stream);
    tlibc_free(core->gpu_frame.texture_descs);
    tlibc_free(core->gpu_frame.texture_texels);
    tlibc_free(core->gpu_frame.oracle_color);
    tlibc_free(core->gpu_frame.oracle_depth);
    tlibc_free(core->gpu_frame.overlay_surface.pixels);
    tlibc_free(core->gpu_frame.overlay_coverage);
    tlibc_free(core->gpu_frame.retained_commands);
    tlibc_free(core->viewmodel_depth);
    tlibc_free(core->viewmodel_coverage);
    rf_gpu_shutdown(&core->gpu);
    if (core->audio_ready) toy_audio_close(&core->audio);
    if (core->window) toy_window_close(core->window);
    if (core->renderer) toy_renderer_destroy(core->renderer);
    rf_core_filesystem_shutdown(&core->filesystem);
    memset(core, 0, sizeof(*core));
}

int64_t rf_core_time_us(struct rf_core *core)
{
    if (!core || !core->initialized) return 0;
    return rf_core_clock_now_us();
}

int64_t rf_core_clock_now_us(void)
{
    struct timespec now;
    if (__clock_gettime(CLOCK_MONOTONIC, &now) < 0) return 0;
    return (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}

int rf_core_get_input_frame(const struct rf_core *core,
                            struct rf_input_frame *frame)
{
    if (!frame) return -1;
    memset(frame, 0, sizeof(*frame));
    if (!core || !core->input) return -1;
    memcpy(frame->key_down, core->input->key_down,
           sizeof(frame->key_down));
    memcpy(frame->key_pressed, core->input->key_pressed,
           sizeof(frame->key_pressed));
    memcpy(frame->key_released, core->input->key_released,
           sizeof(frame->key_released));
    frame->keyboard_focused = core->input->keyboard_focused;
    frame->pointer_x = core->input->pointer_x;
    frame->pointer_y = core->input->pointer_y;
    frame->pointer_moved = core->input->pointer_moved;
    frame->relative_x = core->input->relative_x;
    frame->relative_y = core->input->relative_y;
    frame->pointer_locked = core->input->pointer_locked;
    frame->mouse_buttons = core->input->mouse_buttons;
    return 0;
}

int rf_input_down(const struct rf_input_frame *input, unsigned int key)
{
    return input && key < RF_INPUT_KEY_COUNT && input->key_down[key];
}

int rf_input_pressed(const struct rf_input_frame *input, unsigned int key)
{
    return input && key < RF_INPUT_KEY_COUNT && input->key_pressed[key];
}

int rf_input_released(const struct rf_input_frame *input, unsigned int key)
{
    return input && key < RF_INPUT_KEY_COUNT && input->key_released[key];
}

int rf_core_get_status(const struct rf_core *core,
                       struct rf_core_status *status)
{
    if (!status) return -1;
    memset(status, 0, sizeof(*status));
    status->version = RF_CORE_VERSION;
    status->build = RF_CORE_BUILD;
    if (!core) return -1;
    status->initialized = core->initialized;
    status->window_ready = core->window != NULL;
    status->renderer_ready = core->renderer != NULL;
    status->filesystem_ready = core->filesystem.initialized;
    status->audio_ready = core->audio_ready;
    status->clock_ready = core->initialized;
    status->gpu_ready = core->gpu.state == RF_GPU_STATE_READY;
    status->gpu_policy = core->gpu.policy;
    status->gpu_state = core->gpu.state;
    status->renderer_mode = core->gpu_frame.renderer;
    status->gpu_frames_attempted = core->gpu_frame.stats.frames_attempted;
    status->gpu_frames_rendered = core->gpu_frame.stats.gpu_frames;
    status->gpu_frames_fallback = core->gpu_frame.stats.cpu_fallback_frames;
    status->native_present_ready = core->gpu_frame.native_present;
    status->screen_overlay_composite_ready = core->gpu_frame.native_present;
    status->overlay_upload_bytes_per_frame =
        core->gpu_frame.stats.native_present_timing.overlay_upload_bytes;
    status->overlay_composite_frames =
        core->gpu_frame.stats.overlay_composite_frames;
    return 0;
}

int rf_core_get_gpu_status(const struct rf_core *core,
                           struct rf_gpu_status *status)
{
    return core ? rf_gpu_get_status(&core->gpu, status) : -1;
}

struct toy_window *rf_core_window(struct rf_core *core) { return core ? core->window : NULL; }
struct toy_window_events *rf_core_events(struct rf_core *core) { return core ? &core->events : NULL; }
struct toy_input *rf_core_input(struct rf_core *core) { return core ? core->input : NULL; }
struct toy_surface *rf_core_surface(struct rf_core *core) { return core ? &core->surface : NULL; }
struct toy_renderer *rf_core_renderer(struct rf_core *core) { return core ? core->renderer : NULL; }
struct rf_core_filesystem *rf_core_filesystem_service(struct rf_core *core)
{
    return core ? &core->filesystem : NULL;
}
struct toy_audio *rf_core_audio(struct rf_core *core) { return core ? &core->audio : NULL; }
int rf_core_audio_ready(const struct rf_core *core) { return core && core->audio_ready; }
int rf_core_set_pointer_lock(struct rf_core *core, int locked)
{
    return core && core->window ? toy_window_set_pointer_lock(core->window, locked) : -1;
}
int rf_core_move_window(struct rf_core *core, uint32_t serial)
{
    return core && core->window ? toy_window_move(core->window, serial) : -1;
}
void rf_core_reset_input(struct rf_core *core)
{
    if (!core || !core->input) return;
    memset(core->input, 0, sizeof(*core->input));
    toy_input_init(core->input);
}
