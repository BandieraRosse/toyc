#include "core.h"
#include "tlibc_everything.h"
#include "rf_core_host.h"
#include "rf_gpu_raster_pack.h"

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
    int i;
    if (!frame->armed) return -1;
    frame->armed = 0;
    frame->stats.frames_attempted++;
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
    snprintf(diagnostic, sizeof(diagnostic),
             "gpu-world: classification end commands=%d texture=%lu transparent=%lu overlay=%lu edge=%lu other=%lu",
             count, texture, transparent, overlay, edge, other);
    gpu_world_log(diagnostic);
    if (transparent || overlay || edge || other) {
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
        if (rf_gpu_raster_pack_toy_textured_v1(&packed,
                renderer->job_clear_color, 0, frame->stream,
                frame->stream_capacity, &written, &resources) !=
            RF_GPU_RASTER_PACK_OK) {
            gpu_world_log("gpu-world: pack failed");
            frame->stats.cpu_fallback_frames++;
            return -1;
        }
        snprintf(diagnostic, sizeof(diagnostic),
                 "gpu-world: pack end stream=%llu textures=%u bytes=%llu",
                 (unsigned long long)written, resources.desc_count,
                 (unsigned long long)resources.texel_size);
        gpu_world_log(diagnostic);
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
        frame->stats.unique_textures = resources.desc_count;
        frame->stats.texture_upload_bytes = resources.texel_size;
    }
    frame->stats.texture_commands += texture;
    frame->stats.gpu_frames++;
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
    if (rf_gpu_init(&core->gpu, config->gpu_policy, config->gpu_backend,
                    config->gpu_backend_context) < 0) {
        rf_core_shutdown(core);
        return -1;
    }
    core->gpu_frame.renderer = config->renderer_mode;
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
            toy_renderer_set_command_consumer(core->renderer,
                                               gpu_world_consume, core);
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
    return ready;
}

int rf_core_end_frame(struct rf_core *core)
{
    if (!core || !core->window) return -1;
    if (toy_renderer_flush(core->renderer) < 0) return -1;
    return toy_window_present(core->window);
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
    }
    if (core->renderer)
        toy_renderer_set_command_consumer(core->renderer, NULL, NULL);
    rf_gpu_raster_shutdown(&core->gpu_frame.raster);
    tlibc_free(core->gpu_frame.stream);
    tlibc_free(core->gpu_frame.texture_descs);
    tlibc_free(core->gpu_frame.texture_texels);
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
