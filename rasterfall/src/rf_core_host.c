#include "core.h"
#include "tlibc_everything.h"
#include "rf_core_host.h"
#include "rasterfall_render_resources.h"
#include "fb_draw.h"
#include <limits.h>

static int rf_core_cmd_is_transparent_v1(const struct toy_raster_cmd *cmd)
{
    return cmd && (cmd->transparent || cmd->material_alpha != 255 ||
        (cmd->textured && cmd->texture && cmd->texture->has_transparency));
}

const char *rf_core_renderer_name(int renderer)
{
    if (renderer == RF_CORE_RENDERER_GPU_SCENE) return "gpu-scene";
    return "cpu";
}

static int rf_core_init_window(struct rf_core *core, const char *title,
                              int width, int height, struct toy_input *input,
                              struct toy_renderer *renderer, int native_present,
                              const struct rf_core_config *config)
{
    int64_t started;
    int result;
    if (!core || !input || !renderer) return -1;
    memset(core, 0, sizeof(*core));
    core->input = input;
    core->renderer = renderer;
    started = rf_core_clock_now_us();
    core->window = native_present ? toy_window_open_native(title, width, height) :
        toy_window_open(title, width, height);
    if (config && config->init_event)
        config->init_event(config->init_event_context, "window",
                           core->window ? 0 : -1,
                           rf_core_clock_now_us() - started);
    if (!core->window) return -1;
    if (config && config->init_display &&
        config->init_display(config->init_event_context, core, "filesystem") < 0) {
        toy_window_close(core->window);
        core->window = NULL;
        return -1;
    }
    started = rf_core_clock_now_us();
    result = rf_core_filesystem_init(&core->filesystem);
    if (config && config->init_event)
        config->init_event(config->init_event_context, "filesystem", result,
                           rf_core_clock_now_us() - started);
    if (result < 0) {
        toy_window_close(core->window);
        core->window = NULL;
        return -1;
    }
    if (config && config->init_display &&
        config->init_display(config->init_event_context, core, "input") < 0) {
        rf_core_filesystem_shutdown(&core->filesystem);
        toy_window_close(core->window);
        core->window = NULL;
        return -1;
    }
    started = rf_core_clock_now_us();
    toy_input_init(core->input);
    if (config && config->init_event)
        config->init_event(config->init_event_context, "input", 0,
                           rf_core_clock_now_us() - started);
    if (config && config->init_display &&
        config->init_display(config->init_event_context, core, "software-renderer") < 0) {
        rf_core_filesystem_shutdown(&core->filesystem);
        toy_window_close(core->window);
        core->window = NULL;
        return -1;
    }
    started = rf_core_clock_now_us();
    toy_renderer_init(core->renderer);
    if (config && config->init_event)
        config->init_event(config->init_event_context, "software-renderer", 0,
                           rf_core_clock_now_us() - started);
    if (config && config->init_display &&
        config->init_display(config->init_event_context, core, "audio") < 0) {
        toy_renderer_destroy(core->renderer);
        rf_core_filesystem_shutdown(&core->filesystem);
        toy_window_close(core->window);
        core->window = NULL;
        return -1;
    }
    /* Audio is optional, matching the existing Rasterfall startup policy. */
    started = rf_core_clock_now_us();
    if (toy_audio_open(&core->audio, 48000, 2) == 0)
        core->audio_ready = 1;
    if (config && config->init_event)
        config->init_event(config->init_event_context, "audio",
                           core->audio_ready ? 0 : 1,
                           rf_core_clock_now_us() - started);
    if (config && config->init_display &&
        config->init_display(config->init_event_context, core, "gpu-backend") < 0) {
        if (core->audio_ready) toy_audio_close(&core->audio);
        toy_renderer_destroy(core->renderer);
        rf_core_filesystem_shutdown(&core->filesystem);
        toy_window_close(core->window);
        core->window = NULL;
        return -1;
    }
    core->initialized = 1;
    return 0;
}

int rf_core_init(struct rf_core *core, const char *title, int width, int height,
                 struct toy_input *input, struct toy_renderer *renderer)
{
    return rf_core_init_window(core, title, width, height, input, renderer, 0,
                               NULL);
}

int rf_core_init_config(struct rf_core *core,
                        const struct rf_core_config *config)
{
    int result;
    if (!config || (config->renderer_mode != RF_CORE_RENDERER_CPU &&
                    config->renderer_mode != RF_CORE_RENDERER_GPU_SCENE)) return -1;
    result = rf_core_init_window(core, config->title, config->width, config->height,
                                 config->input, config->renderer,
                                 config->native_present != 0, config);
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
    {
    int64_t started = rf_core_clock_now_us();
    result = rf_gpu_init(&core->gpu, config->gpu_policy, config->gpu_backend,
                         config->gpu_backend_context);
    if (config->init_event)
        config->init_event(config->init_event_context,
                           config->gpu_policy == RF_GPU_POLICY_DISABLED ?
                           "gpu-backend-disabled" : "gpu-backend",
                           config->gpu_policy == RF_GPU_POLICY_DISABLED ?
                           1 : result,
                           rf_core_clock_now_us() - started);
    }
    if (result < 0) {
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
    if (config->init_display &&
        config->init_display(config->init_event_context, core, NULL) < 0) {
        rf_core_shutdown(core);
        return -1;
    }
    return 0;
}

int rf_core_switch_renderer(struct rf_core *core,
                            const struct rf_core_config *config)
{
    struct toy_native_window_handle native;
    struct rf_gpu_native_window gpu_native;
    struct rf_gpu_status status;
    int64_t started;
    int result;
    if (!core || !core->initialized || !core->window || !config ||
        (config->renderer_mode != RF_CORE_RENDERER_CPU &&
         config->renderer_mode != RF_CORE_RENDERER_GPU_SCENE) ||
        (config->renderer_mode == RF_CORE_RENDERER_GPU_SCENE &&
         (!config->native_present || !config->gpu_backend))) return -1;
    rf_gpu_shutdown(&core->gpu);
    memset(&core->gpu_frame, 0, sizeof(core->gpu_frame));
    core->gpu_frame.renderer = RF_CORE_RENDERER_CPU;
    if (config->renderer_mode == RF_CORE_RENDERER_CPU) {
        rf_gpu_init(&core->gpu, RF_GPU_POLICY_DISABLED, NULL, NULL);
        return 0;
    }
    memset(&gpu_native, 0, sizeof(gpu_native));
    result = toy_window_prepare_native(core->window);
    if (result < 0) goto failed;
    result = toy_window_get_native_handle(core->window, &native);
    if (result > 0) {
        gpu_native.type = native.type;
        gpu_native.window = native.window;
        gpu_native.instance = native.instance;
    }
    if (result <= 0 || rf_gpu_set_native_window(config->gpu_backend,
            config->gpu_backend_context, &gpu_native) < 0) goto failed;
    started = rf_core_clock_now_us();
    result = rf_gpu_init(&core->gpu, config->gpu_policy,
                         config->gpu_backend, config->gpu_backend_context);
    if (config->init_event)
        config->init_event(config->init_event_context, "gpu-backend", result,
                           rf_core_clock_now_us() - started);
    if (result < 0 || rf_gpu_get_status(&core->gpu, &status) < 0 ||
        !status.renderer.native_presentation_v1) goto failed;
    core->gpu_frame.renderer = RF_CORE_RENDERER_GPU_SCENE;
    core->gpu_frame.native_present = 1;
    core->gpu_frame.strict_gpu_only =
        config->gpu_policy == RF_GPU_POLICY_REQUIRED;
    return 0;
failed:
    rf_gpu_shutdown(&core->gpu);
    rf_gpu_init(&core->gpu, RF_GPU_POLICY_DISABLED, NULL, NULL);
    return -1;
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

int rf_core_present_boot_frame(struct rf_core *core)
{
    int result;
    if (!core || !core->window || !core->renderer ||
        core->render_frame.current_layer != RF_RENDER_LAYER_OVERLAY ||
        core->gpu_frame.stats.frames_attempted) return -1;
    result = toy_renderer_flush(core->renderer);
    if (result >= 0) result = toy_window_present(core->window);
    rasterfall_resources_frame_complete(rasterfall_render_resources());
    return result;
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

int rf_core_request_exit(struct rf_core *core)
{
    if (!core || !core->initialized) return -1;
    core->exit_requested = 1;
    return 0;
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

int rf_core_begin_scene_frame(struct rf_core *core)
{
    int ready;
    if (!core || !core->window || !core->renderer ||
        core->gpu_frame.renderer != RF_CORE_RENDERER_GPU_SCENE ||
        core->gpu_frame.initialized ||
        core->gpu.state != RF_GPU_STATE_READY || !core->gpu_frame.native_present ||
        rf_core_runtime_failed(core)) return -1;
    /* No toy_renderer_begin, mixed recording, legacy registry pin or CPU clear.
     * The Scene owner handles preflight, submit, present and retirement. */
    ready = toy_window_begin_frame(core->window, &core->surface);
    if (ready <= 0) return ready;
    core->renderer->surface = core->surface;
    core->gpu_frame.frame_begin_us = rf_core_clock_now_us();
    return ready;
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
    if (rasterfall_resources_frame_begin(rasterfall_render_resources()) < 0)
        return -1;
    core->world_depth = core->renderer->depth;
    core->viewmodel_active = 0;
    core->gpu_frame.frame_begin_us = rf_core_clock_now_us();
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
    if (!core || core->render_frame.current_layer != RF_RENDER_LAYER_VIEWMODEL)
        return NULL;
    if (rf_core_viewmodel_end_v1(core) < 0 ||
        rf_core_render_frame_enter_layer_v1(core, RF_RENDER_LAYER_OVERLAY) < 0)
        return NULL;
    core->renderer->surface = core->surface;
    return &core->surface;
}

static int core_end_frame_present(struct rf_core *core)
{
    int64_t present_start;
    int result;
    if (!core || !core->window) return -1;
    if (toy_renderer_flush(core->renderer) < 0) return -1;
    if (core->gpu_frame.strict_gpu_only) {
        core->gpu_frame.runtime_failed = 1;
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

int rf_core_end_frame(struct rf_core *core)
{
    int result = core_end_frame_present(core);
    if (result >= 0)
        rasterfall_resources_frame_complete(rasterfall_render_resources());
    return result;
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
    if (core->renderer)
        toy_renderer_set_command_consumer(core->renderer, NULL, NULL);
    tlibc_free(core->viewmodel_depth);
    tlibc_free(core->viewmodel_coverage);
    rf_gpu_shutdown(&core->gpu);
    if (core->audio_ready) toy_audio_close(&core->audio);
    if (core->window) toy_window_close(core->window);
    if (core->renderer) toy_renderer_destroy(core->renderer);
    rasterfall_resources_invalidate(rasterfall_render_resources());
    rasterfall_resources_frame_complete(rasterfall_render_resources());
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
    memcpy(frame->physical_down, core->input->physical_down,
           sizeof(frame->physical_down));
    memcpy(frame->physical_pressed, core->input->physical_pressed,
           sizeof(frame->physical_pressed));
    memcpy(frame->physical_released, core->input->physical_released,
           sizeof(frame->physical_released));
    frame->keyboard_focused = core->input->keyboard_focused;
    frame->pointer_x = core->input->pointer_x;
    frame->pointer_y = core->input->pointer_y;
    frame->pointer_moved = core->input->pointer_moved;
    frame->relative_x = core->input->relative_x;
    frame->relative_y = core->input->relative_y;
    frame->pointer_locked = core->input->pointer_locked;
    frame->mouse_buttons = core->input->mouse_buttons;
    frame->wheel_y = core->input->wheel_y;
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
