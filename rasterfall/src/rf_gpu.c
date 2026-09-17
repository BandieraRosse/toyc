#include "rf_gpu.h"

static void zero_bytes(void *memory, unsigned long count)
{
    unsigned char *bytes = memory;
    while (count--) *bytes++ = 0;
}

static void copy_bytes(void *destination, const void *source,
                       unsigned long count)
{
    unsigned char *out = destination;
    const unsigned char *in = source;
    while (count--) *out++ = *in++;
}

static void copy_message(char *destination, unsigned long capacity,
                         const char *source)
{
    unsigned long i = 0;
    if (!destination || !capacity) return;
    if (source)
        while (i + 1 < capacity && source[i]) {
            destination[i] = source[i];
            ++i;
        }
    destination[i] = 0;
}

const char *rf_gpu_policy_name(int policy)
{
    switch (policy) {
    case RF_GPU_POLICY_DISABLED: return "disabled";
    case RF_GPU_POLICY_OPTIONAL: return "optional";
    case RF_GPU_POLICY_REQUIRED: return "required";
    default: return "invalid";
    }
}

const char *rf_gpu_state_name(int state)
{
    switch (state) {
    case RF_GPU_STATE_DISABLED: return "disabled";
    case RF_GPU_STATE_UNAVAILABLE: return "unavailable";
    case RF_GPU_STATE_READY: return "ready";
    case RF_GPU_STATE_FAILED: return "failed";
    default: return "invalid";
    }
}

void rf_gpu_evaluate_capabilities(const struct rf_gpu_capabilities *caps,
                                  struct rf_gpu_renderer_capabilities *renderer)
{
    unsigned int x = 0, y = 0;
    if (!renderer) return;
    zero_bytes(renderer, sizeof(*renderer));
    if (!caps) return;
    renderer->compute = caps->compute_queue != 0;
    renderer->framebuffer = renderer->compute &&
        caps->device_local_output_memory &&
        caps->host_visible_readback_memory &&
        (caps->coherent_readback || caps->non_coherent_readback);
    if (caps->max_compute_work_group_invocations >= 256 &&
        caps->max_compute_work_group_size[0] >= 16 &&
        caps->max_compute_work_group_size[1] >= 16) {
        x = 16; y = 16;
    } else if (caps->max_compute_work_group_invocations >= 64 &&
               caps->max_compute_work_group_size[0] >= 8 &&
               caps->max_compute_work_group_size[1] >= 8) {
        x = 8; y = 8;
    }
    renderer->raster_work_group_x = x;
    renderer->raster_work_group_y = y;
    renderer->raster_v1 = renderer->framebuffer && caps->shader_int64 &&
        caps->max_storage_buffer_range && x && y;
    renderer->native_presentation_v1 = renderer->raster_v1 &&
        caps->native_presentation_v1;
}

int rf_gpu_set_native_window(const struct rf_gpu_backend *backend,
                             void *backend_context,
                             const struct rf_gpu_native_window *window)
{
    if (!backend || !backend->set_native_window) return 0;
    return backend->set_native_window(backend_context, window);
}

static int framebuffer_create(struct rf_gpu *gpu,
                              struct rf_gpu_framebuffer *framebuffer,
                              unsigned int width, unsigned int height)
{
    void *implementation = 0;
    if (!gpu || !framebuffer || gpu->state != RF_GPU_STATE_READY ||
        !width || !height || width > 16384 || height > 16384 ||
        !gpu->backend || !gpu->backend->framebuffer_create ||
        !gpu->backend->framebuffer_destroy ||
        !gpu->backend->framebuffer_render)
        return -1;
    if (gpu->backend->framebuffer_create(gpu->backend_context, width, height,
                                         &implementation, gpu->message,
                                         sizeof(gpu->message)) < 0)
        return -1;
    zero_bytes(framebuffer, sizeof(*framebuffer));
    framebuffer->backend = gpu->backend;
    framebuffer->backend_context = gpu->backend_context;
    framebuffer->implementation = implementation;
    framebuffer->format = RF_GPU_FRAMEBUFFER_FORMAT_XRGB8888;
    framebuffer->width = width;
    framebuffer->height = height;
    framebuffer->stride = width;
    return 0;
}

int rf_gpu_framebuffer_init(struct rf_gpu *gpu,
                            struct rf_gpu_framebuffer *framebuffer,
                            unsigned int width, unsigned int height)
{
    if (!framebuffer) return -1;
    zero_bytes(framebuffer, sizeof(*framebuffer));
    return framebuffer_create(gpu, framebuffer, width, height);
}

int rf_gpu_framebuffer_resize(struct rf_gpu *gpu,
                              struct rf_gpu_framebuffer *framebuffer,
                              unsigned int width, unsigned int height)
{
    struct rf_gpu_framebuffer replacement;
    if (!framebuffer || !framebuffer->implementation) return -1;
    if (framebuffer->width == width && framebuffer->height == height) return 0;
    if (framebuffer_create(gpu, &replacement, width, height) < 0) return -1;
    rf_gpu_framebuffer_shutdown(framebuffer);
    *framebuffer = replacement;
    return 0;
}

int rf_gpu_framebuffer_render(struct rf_gpu *gpu,
                              struct rf_gpu_framebuffer *framebuffer,
                              unsigned int *pixels, unsigned int width,
                              unsigned int height, unsigned int stride)
{
    if (!gpu || gpu->state != RF_GPU_STATE_READY || !framebuffer ||
        framebuffer->backend != gpu->backend || !framebuffer->implementation ||
        !pixels || width != framebuffer->width ||
        height != framebuffer->height || stride < width)
        return -1;
    return framebuffer->backend->framebuffer_render(
        framebuffer->backend_context, framebuffer->implementation, pixels,
        width, height, stride, gpu->message, sizeof(gpu->message));
}

void rf_gpu_framebuffer_shutdown(struct rf_gpu_framebuffer *framebuffer)
{
    if (!framebuffer) return;
    if (framebuffer->implementation && framebuffer->backend &&
        framebuffer->backend->framebuffer_destroy)
        framebuffer->backend->framebuffer_destroy(framebuffer->backend_context,
                                                   framebuffer->implementation);
    zero_bytes(framebuffer, sizeof(*framebuffer));
}

static int raster_create(struct rf_gpu *gpu, struct rf_gpu_raster *raster,
                         unsigned int width, unsigned int height)
{
    struct rf_gpu_status status;
    void *implementation = 0;
    if (!gpu || !raster || gpu->state != RF_GPU_STATE_READY || !width ||
        !height || width > 16384 || height > 16384 ||
        rf_gpu_get_status(gpu, &status) < 0 || !status.renderer.raster_v1 ||
        !gpu->backend || !gpu->backend->raster_create ||
        !gpu->backend->raster_destroy || !gpu->backend->raster_render)
        return -1;
    if (gpu->backend->raster_create(
            gpu->backend_context, width, height,
            status.renderer.raster_work_group_x,
            status.renderer.raster_work_group_y, &implementation,
            gpu->message, sizeof(gpu->message)) < 0)
        return -1;
    zero_bytes(raster, sizeof(*raster));
    raster->backend = gpu->backend;
    raster->backend_context = gpu->backend_context;
    raster->implementation = implementation;
    raster->format = RF_GPU_FRAMEBUFFER_FORMAT_XRGB8888;
    raster->width = width;
    raster->height = height;
    raster->work_group_x = status.renderer.raster_work_group_x;
    raster->work_group_y = status.renderer.raster_work_group_y;
    return 0;
}

int rf_gpu_raster_init(struct rf_gpu *gpu, struct rf_gpu_raster *raster,
                       unsigned int width, unsigned int height)
{
    if (!raster) return -1;
    zero_bytes(raster, sizeof(*raster));
    return raster_create(gpu, raster, width, height);
}

int rf_gpu_raster_resize(struct rf_gpu *gpu, struct rf_gpu_raster *raster,
                         unsigned int width, unsigned int height)
{
    struct rf_gpu_raster replacement;
    if (!raster || !raster->implementation) return -1;
    if (raster->width == width && raster->height == height) return 0;
    if (raster_create(gpu, &replacement, width, height) < 0) return -1;
    rf_gpu_raster_shutdown(raster);
    *raster = replacement;
    return 0;
}

int rf_gpu_raster_render(struct rf_gpu *gpu, struct rf_gpu_raster *raster,
                         const void *stream, unsigned long stream_size,
                         unsigned int *color, int *depth,
                         unsigned int width, unsigned int height,
                         unsigned int color_stride,
                         unsigned int depth_stride)
{
    return rf_gpu_raster_render_timed(gpu, raster, stream, stream_size,
                                      color, depth, width, height,
                                      color_stride, depth_stride, 0);
}

int rf_gpu_raster_render_timed(struct rf_gpu *gpu, struct rf_gpu_raster *raster,
                         const void *stream, unsigned long stream_size,
                         unsigned int *color, int *depth,
                         unsigned int width, unsigned int height,
                         unsigned int color_stride,
                         unsigned int depth_stride,
                         struct rf_gpu_raster_timing *timing)
{
    if (!gpu || gpu->state != RF_GPU_STATE_READY || !raster ||
        raster->backend != gpu->backend || !raster->implementation ||
        !stream || !stream_size || !color || !depth ||
        width != raster->width || height != raster->height ||
        color_stride < width || depth_stride < width)
        return -1;
    return raster->backend->raster_render(
        raster->backend_context, raster->implementation, stream, stream_size,
        0, 0, 0, 0,
        color, depth, width, height, color_stride, depth_stride,
        timing,
        gpu->message, sizeof(gpu->message));
}

int rf_gpu_raster_render_textured_timed(
                         struct rf_gpu *gpu, struct rf_gpu_raster *raster,
                         const void *stream, unsigned long stream_size,
                         const void *texture_descs, unsigned int texture_count,
                         const void *texture_texels, unsigned long texture_bytes,
                         unsigned int *color, int *depth,
                         unsigned int width, unsigned int height,
                         unsigned int color_stride, unsigned int depth_stride,
                         struct rf_gpu_raster_timing *timing)
{
    if (!gpu || gpu->state != RF_GPU_STATE_READY || !raster ||
        raster->backend != gpu->backend || !raster->implementation ||
        !stream || !stream_size || !color || !depth ||
        (texture_count && (!texture_descs || !texture_texels || !texture_bytes)) ||
        width != raster->width || height != raster->height ||
        color_stride < width || depth_stride < width)
        return -1;
    return raster->backend->raster_render(
        raster->backend_context, raster->implementation, stream, stream_size,
        texture_descs, texture_count, texture_texels, texture_bytes,
        color, depth, width, height, color_stride, depth_stride, timing,
        gpu->message, sizeof(gpu->message));
}

int rf_gpu_raster_set_full_scan_diagnostic(struct rf_gpu_raster *raster,
                                            int enabled)
{
    if(!raster||!raster->implementation||!raster->backend||
       !raster->backend->raster_set_full_scan_diagnostic)return -1;
    raster->backend->raster_set_full_scan_diagnostic(raster->backend_context,
                                                       raster->implementation,
                                                       enabled);
    return 0;
}

void rf_gpu_raster_shutdown(struct rf_gpu_raster *raster)
{
    if (!raster) return;
    if (raster->implementation && raster->backend &&
        raster->backend->raster_destroy)
        raster->backend->raster_destroy(raster->backend_context,
                                        raster->implementation);
    zero_bytes(raster, sizeof(*raster));
}

int rf_gpu_raster_present_textured_timed(
                         struct rf_gpu *gpu, struct rf_gpu_raster *raster,
                         const void *stream, unsigned long stream_size,
                         const void *texture_descs, unsigned int texture_count,
                         const void *texture_texels, unsigned long texture_bytes,
                         unsigned int width, unsigned int height,
                         struct rf_gpu_raster_timing *raster_timing,
                         struct rf_gpu_native_present_timing *present_timing)
{
    if (!gpu || gpu->state != RF_GPU_STATE_READY || !raster ||
        raster->backend != gpu->backend || !gpu->backend->raster_present)
        return -1;
    return gpu->backend->raster_present(gpu->backend_context,
        raster->implementation, stream, stream_size, texture_descs,
        texture_count, texture_texels, texture_bytes, width, height,
        raster_timing, present_timing, gpu->message, sizeof(gpu->message));
}

int rf_gpu_init(struct rf_gpu *gpu, enum rf_gpu_policy policy,
                const struct rf_gpu_backend *backend, void *backend_context)
{
    int result;
    if (!gpu || policy < RF_GPU_POLICY_DISABLED ||
        policy > RF_GPU_POLICY_REQUIRED)
        return -1;
    zero_bytes(gpu, sizeof(*gpu));
    gpu->policy = policy;
    gpu->backend = backend;
    gpu->backend_context = backend_context;
    if (policy == RF_GPU_POLICY_DISABLED) {
        gpu->state = RF_GPU_STATE_DISABLED;
        copy_message(gpu->message, sizeof(gpu->message), "GPU disabled by policy");
        return 0;
    }
    if (!backend || !backend->init || !backend->shutdown) {
        gpu->state = RF_GPU_STATE_UNAVAILABLE;
        copy_message(gpu->message, sizeof(gpu->message),
                     "no GPU backend is available in this build");
        return policy == RF_GPU_POLICY_REQUIRED ? -1 : 0;
    }
    result = backend->init(backend_context, &gpu->info, gpu->message,
                           sizeof(gpu->message));
    if (result == RF_GPU_BACKEND_READY) {
        gpu->state = RF_GPU_STATE_READY;
        if (!gpu->message[0])
            copy_message(gpu->message, sizeof(gpu->message), "GPU ready");
        return 0;
    }
    zero_bytes(&gpu->info, sizeof(gpu->info));
    gpu->state = result == RF_GPU_BACKEND_UNAVAILABLE
                     ? RF_GPU_STATE_UNAVAILABLE : RF_GPU_STATE_FAILED;
    if (!gpu->message[0])
        copy_message(gpu->message, sizeof(gpu->message),
                     result == RF_GPU_BACKEND_UNAVAILABLE
                         ? "GPU unavailable" : "GPU initialization failed");
    return policy == RF_GPU_POLICY_REQUIRED ? -1 : 0;
}

void rf_gpu_shutdown(struct rf_gpu *gpu)
{
    if (!gpu) return;
    if (gpu->state == RF_GPU_STATE_READY && gpu->backend &&
        gpu->backend->shutdown)
        gpu->backend->shutdown(gpu->backend_context);
    zero_bytes(gpu, sizeof(*gpu));
}

int rf_gpu_get_status(const struct rf_gpu *gpu, struct rf_gpu_status *status)
{
    if (!gpu || !status) return -1;
    zero_bytes(status, sizeof(*status));
    status->policy = gpu->policy;
    status->state = gpu->state;
    status->ready = gpu->state == RF_GPU_STATE_READY;
    status->info = gpu->info;
    if (status->ready)
        rf_gpu_evaluate_capabilities(&status->info.capabilities,
                                     &status->renderer);
    copy_bytes(status->message, gpu->message, sizeof(status->message));
    return 0;
}
