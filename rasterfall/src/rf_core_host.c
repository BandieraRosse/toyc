#include "core.h"
#include "tlibc_everything.h"
#include "rf_core_host.h"
#include "rf_gpu_raster_pack.h"
#include "fb_draw.h"
#ifdef TOYC_WINDOWS
#include "rf_gpu_raster_cpu_ref.h"
#endif

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
        if (cmd->edge) edge++;
        else if (cmd->overlay) overlay++;
        else if (cmd->transparent || cmd->material_alpha != 255) transparent++;
        else if (cmd->textured) texture++;
        else if (cmd->area >= 0) other++;
    }
    frame->stats.unsupported_transparent += transparent;
    frame->stats.unsupported_overlay += overlay;
    frame->stats.unsupported_edge += edge;
    frame->stats.unsupported_other += other;
    frame->stats.last_commands = (unsigned long)count;
    frame->stats.last_texture_commands = texture;
    frame->stats.last_transparent_commands = transparent;
    frame->stats.last_overlay_commands = overlay;
    frame->stats.last_edge_commands = edge;
    frame->stats.last_other_commands = other;
    if (transparent)
        core->render_frame.pre_post_fallback_reason |=
            RF_PRE_POST_FALLBACK_TRANSPARENT;
    if (overlay || edge || other)
        core->render_frame.pre_post_fallback_reason |=
            RF_PRE_POST_FALLBACK_UNSUPPORTED_COMMAND;
    snprintf(diagnostic, sizeof(diagnostic),
             "gpu-world: classification end commands=%d texture=%lu transparent=%lu overlay=%lu edge=%lu other=%lu",
             count, texture, transparent, overlay, edge, other);
    gpu_world_log(diagnostic);
    frame->stats.classification_ms =
        (double)(rf_core_clock_now_us() - stage_start) / 1000.0;
    if (transparent || overlay || edge || other) {
        frame->stats.last_path = 2;
        frame->stats.cpu_fallback_frames++;
        return -1;
    }
    needed = rf_gpu_raster_stream_size_v1((uint32_t)count + 2U);
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
    if (rf_gpu_raster_measure_textures_toy_v1(&packed, &unique_textures,
            &texture_bytes) != RF_GPU_RASTER_PACK_OK) {
        frame->stats.unsupported_texture += texture;
        frame->stats.cpu_fallback_frames++;
        return -1;
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
        if (rf_gpu_raster_pack_toy_textured_v1(&packed,
                renderer->job_clear_color, 0, frame->stream,
                frame->stream_capacity, &written, &resources) !=
            RF_GPU_RASTER_PACK_OK) {
            gpu_world_log("gpu-world: pack failed");
            frame->stats.cpu_fallback_frames++;
            return -1;
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
    memcpy(frame->retained_commands + frame->retained_command_count, commands,
           (unsigned long)count * sizeof(*commands));
    frame->retained_command_count = needed;
    frame->retained_batch_count[layer] += (unsigned long)count;
    core->render_frame.retained_pre_post_commands = needed;
    return 0;
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
    frame->retaining_pre_post = 0;
    core->render_frame.pre_post_fallback_reason |=
        rf_core_render_frame_fallback_reason_v1(&core->render_frame);
    unsupported_post_world =
        core->render_frame.direct_pixel_count[RF_RENDER_LAYER_EFFECTS] != 0 ||
        core->render_frame.command_count[RF_RENDER_LAYER_VIEWMODEL] != 0 ||
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
        frame->native_prepared = 0;
        core->render_frame.pre_post_cpu_fallback = 1;
        if (!core->render_frame.pre_post_fallback_reason)
            core->render_frame.pre_post_fallback_reason |=
                RF_PRE_POST_FALLBACK_CONSUMER_FAILURE;
        return gpu_pre_post_replay_cpu(core) < 0 ? -1 : 0;
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
    core->gpu_frame.native_present = config->native_present != 0;
    if (core->gpu_frame.native_present) {
        struct rf_gpu_status gpu_status;
        if (rf_gpu_get_status(&core->gpu, &gpu_status) < 0 ||
            !gpu_status.renderer.native_presentation_v1) {
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
                if (rf_gpu_raster_set_post(&core->gpu_frame.raster,&post)<0)
                    __printf("GPU Post-Raster V1 unavailable; bypassing post pass\n");
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
    return !core || !core->initialized || core->exit_requested;
}

int rf_core_begin_frame(struct rf_core *core, uint32_t clear_color)
{
    int ready;
    if (!core || !core->window) return -1;
    ready = toy_window_begin_frame(core->window, &core->surface);
    if (ready <= 0) return ready;
    if (toy_renderer_begin(core->renderer, &core->surface, clear_color) < 0)
        return -1;
    core->gpu_frame.frame_begin_us = rf_core_clock_now_us();
    core->gpu_frame.native_prepared = 0;
    core->gpu_frame.overlay_active = 0;
    core->gpu_frame.retaining_pre_post = 0;
    core->gpu_frame.retained_command_count = 0;
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
        if (commands[i].transparent || commands[i].material_alpha != 255)
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
    if (frame->command_count[RF_RENDER_LAYER_TRANSPARENT])
        reason |= RF_PRE_POST_FALLBACK_TRANSPARENT;
    if (frame->direct_pixel_count[RF_RENDER_LAYER_EFFECTS])
        reason |= RF_PRE_POST_FALLBACK_EFFECTS_DIRECT_PIXELS;
    if (frame->command_count[RF_RENDER_LAYER_VIEWMODEL])
        reason |= RF_PRE_POST_FALLBACK_VIEWMODEL_COMMANDS;
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
                &frame->stats.native_present_timing) < 0)
            return -1;
        frame->native_prepared = 0;
        frame->native_presented = 1;
        core->render_frame.layer_backend[RF_RENDER_LAYER_SKY] =
            RF_RENDER_BACKEND_GPU;
        core->render_frame.layer_backend[RF_RENDER_LAYER_WORLD] =
            RF_RENDER_BACKEND_GPU;
        core->render_frame.layer_backend[RF_RENDER_LAYER_TRANSPARENT] =
            RF_RENDER_BACKEND_UNSUPPORTED;
        core->render_frame.layer_backend[RF_RENDER_LAYER_EFFECTS] =
            RF_RENDER_BACKEND_UNSUPPORTED;
        core->render_frame.layer_backend[RF_RENDER_LAYER_VIEWMODEL] =
            RF_RENDER_BACKEND_UNSUPPORTED;
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
