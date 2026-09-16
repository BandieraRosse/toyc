#include "rf_gpu.h"

#include <stdio.h>
#include <string.h>

struct fake_backend {
    int result;
    int init_count;
    int shutdown_count;
    unsigned int adapter_type;
    int raster_create_count;
    int raster_destroy_count;
    int raster_fail;
};

static int fake_init(void *context, struct rf_gpu_backend_info *info,
                     char *message, unsigned long message_capacity)
{
    struct fake_backend *fake = context;
    ++fake->init_count;
    if (fake->result != RF_GPU_BACKEND_READY) {
        snprintf(message, message_capacity, "%s",
                 fake->result == RF_GPU_BACKEND_UNAVAILABLE
                     ? "fake unavailable" : "fake failure");
        return fake->result;
    }
    snprintf(info->adapter_name, sizeof(info->adapter_name), "fake discrete");
    info->adapter_type = fake->adapter_type ? fake->adapter_type
                                           : RF_GPU_ADAPTER_DISCRETE;
    info->vendor_id = 0x10de;
    info->device_id = 0x1234;
    info->queue_family = 2;
    info->capabilities.api_version = (1U << 22) | (2U << 12);
    info->capabilities.adapter_index = 1;
    info->capabilities.compute_queue = 1;
    info->capabilities.max_compute_work_group_invocations = 256;
    info->capabilities.max_compute_work_group_size[0] = 256;
    info->capabilities.max_compute_work_group_size[1] = 256;
    info->capabilities.max_compute_work_group_size[2] = 64;
    info->capabilities.max_compute_work_group_count[0] = 65535;
    info->capabilities.max_compute_work_group_count[1] = 65535;
    info->capabilities.max_compute_work_group_count[2] = 65535;
    info->capabilities.max_storage_buffer_range = 1U << 27;
    info->capabilities.shader_int64 = 1;
    info->capabilities.device_local_output_memory = 1;
    info->capabilities.host_visible_readback_memory = 1;
    info->capabilities.coherent_readback = 1;
    snprintf(message, message_capacity, "fake ready");
    return RF_GPU_BACKEND_READY;
}

static void fake_shutdown(void *context)
{
    struct fake_backend *fake = context;
    ++fake->shutdown_count;
}

static int fake_raster_create(void *context, unsigned int width,
                              unsigned int height, unsigned int work_group_x,
                              unsigned int work_group_y, void **raster,
                              char *message, unsigned long message_capacity)
{
    struct fake_backend *fake = context;
    (void)width; (void)height; (void)work_group_x; (void)work_group_y;
    (void)message; (void)message_capacity;
    ++fake->raster_create_count;
    if (fake->raster_fail) return -1;
    *raster = fake;
    return 0;
}

static void fake_raster_destroy(void *context, void *raster)
{
    struct fake_backend *fake = context;
    (void)raster;
    ++fake->raster_destroy_count;
}

static int fake_raster_render(void *context, void *raster,
                              const void *stream, unsigned long stream_size,
                              unsigned int *color, int *depth,
                              unsigned int width, unsigned int height,
                              unsigned int color_stride,
                              unsigned int depth_stride,
                              char *message, unsigned long message_capacity)
{
    (void)context; (void)raster; (void)stream; (void)stream_size;
    (void)color; (void)depth; (void)width; (void)height;
    (void)color_stride; (void)depth_stride; (void)message;
    (void)message_capacity;
    return 0;
}

static const struct rf_gpu_backend backend = {
    .init = fake_init,
    .shutdown = fake_shutdown,
    .raster_create = fake_raster_create,
    .raster_destroy = fake_raster_destroy,
    .raster_render = fake_raster_render
};

#define CHECK(condition) do {                                                \
    if (!(condition)) {                                                      \
        fprintf(stderr, "rf-gpu-service-test: check failed at line %d\n",  \
                __LINE__);                                                   \
        return 1;                                                            \
    }                                                                        \
} while (0)

int main(void)
{
    struct rf_gpu gpu;
    struct rf_gpu_status status;
    struct fake_backend fake;
    struct rf_gpu_capabilities caps;
    struct rf_gpu_renderer_capabilities renderer;
    struct rf_gpu_capabilities copied_caps;
    static const unsigned int adapter_types[] = {
        RF_GPU_ADAPTER_INTEGRATED, RF_GPU_ADAPTER_DISCRETE,
        RF_GPU_ADAPTER_CPU
    };
    unsigned int i;

    memset(&fake, 0, sizeof(fake));
    CHECK(rf_gpu_init(&gpu, RF_GPU_POLICY_DISABLED, &backend, &fake) == 0);
    CHECK(gpu.state == RF_GPU_STATE_DISABLED && fake.init_count == 0);
    rf_gpu_shutdown(&gpu);

    CHECK(rf_gpu_init(&gpu, RF_GPU_POLICY_OPTIONAL, NULL, NULL) == 0);
    CHECK(rf_gpu_get_status(&gpu, &status) == 0);
    CHECK(status.state == RF_GPU_STATE_UNAVAILABLE && !status.ready);
    rf_gpu_shutdown(&gpu);
    CHECK(rf_gpu_init(&gpu, RF_GPU_POLICY_REQUIRED, NULL, NULL) < 0);
    CHECK(gpu.state == RF_GPU_STATE_UNAVAILABLE);
    rf_gpu_shutdown(&gpu);

    memset(&fake, 0, sizeof(fake));
    fake.result = RF_GPU_BACKEND_FAILED;
    CHECK(rf_gpu_init(&gpu, RF_GPU_POLICY_OPTIONAL, &backend, &fake) == 0);
    CHECK(gpu.state == RF_GPU_STATE_FAILED && fake.init_count == 1);
    rf_gpu_shutdown(&gpu);
    CHECK(fake.shutdown_count == 0);

    memset(&fake, 0, sizeof(fake));
    CHECK(rf_gpu_init(&gpu, RF_GPU_POLICY_REQUIRED, &backend, &fake) == 0);
    CHECK(rf_gpu_get_status(&gpu, &status) == 0);
    CHECK(status.ready && status.info.adapter_type == RF_GPU_ADAPTER_DISCRETE);
    CHECK(strcmp(status.info.adapter_name, "fake discrete") == 0);
    CHECK(status.renderer.compute && status.renderer.framebuffer);
    CHECK(status.renderer.raster_v1);
    CHECK(status.renderer.raster_work_group_x == 16 &&
          status.renderer.raster_work_group_y == 16);
    {
        struct rf_gpu_raster raster;
        void *old_implementation;
        CHECK(rf_gpu_raster_init(&gpu, &raster, 16, 16) == 0);
        old_implementation = raster.implementation;
        fake.raster_fail = 1;
        CHECK(rf_gpu_raster_resize(&gpu, &raster, 32, 24) < 0);
        CHECK(raster.implementation == old_implementation &&
              raster.width == 16 && raster.height == 16 &&
              fake.raster_destroy_count == 0);
        rf_gpu_raster_shutdown(&raster);
        CHECK(fake.raster_destroy_count == 1);
    }
    rf_gpu_shutdown(&gpu);
    CHECK(fake.shutdown_count == 1);

    /* READY service and framebuffer remain usable when Raster V1's exact
     * integer contract cannot be met. This is the CPU fallback boundary. */
    memset(&fake, 0, sizeof(fake));
    CHECK(rf_gpu_init(&gpu, RF_GPU_POLICY_REQUIRED, &backend, &fake) == 0);
    gpu.info.capabilities.shader_int64 = 0;
    CHECK(rf_gpu_get_status(&gpu, &status) == 0);
    CHECK(status.ready && status.renderer.compute && status.renderer.framebuffer);
    CHECK(!status.renderer.raster_v1);
    {
        struct rf_gpu_raster raster;
        CHECK(rf_gpu_raster_init(&gpu, &raster, 16, 16) < 0);
    }
    rf_gpu_shutdown(&gpu);

    memset(&caps, 0, sizeof(caps));
    caps.compute_queue = 1;
    caps.max_storage_buffer_range = 65536;
    caps.shader_int64 = 1;
    caps.device_local_output_memory = 1;
    caps.host_visible_readback_memory = 1;
    caps.non_coherent_readback = 1;
    caps.max_compute_work_group_invocations = 128;
    caps.max_compute_work_group_size[0] = 8;
    caps.max_compute_work_group_size[1] = 8;
    rf_gpu_evaluate_capabilities(&caps, &renderer);
    CHECK(renderer.framebuffer && renderer.raster_v1);
    CHECK(renderer.raster_work_group_x == 8 &&
          renderer.raster_work_group_y == 8);
    caps.max_compute_work_group_invocations = 32;
    rf_gpu_evaluate_capabilities(&caps, &renderer);
    CHECK(renderer.framebuffer && !renderer.raster_v1);

    /* Snapshot storage is fixed-width data only: exercise representative
     * integrated, discrete and CPU/software classifications without backend
     * objects or device handles. */
    CHECK(sizeof(caps.memory_types[0].property_flags) == sizeof(unsigned int));
    CHECK(sizeof(caps.memory_heaps[0].size) == 8);
    caps.memory_type_count = 1;
    caps.memory_types[0].property_flags = RF_GPU_MEMORY_DEVICE_LOCAL |
        RF_GPU_MEMORY_HOST_VISIBLE | RF_GPU_MEMORY_HOST_COHERENT;
    CHECK((caps.memory_types[0].property_flags & RF_GPU_MEMORY_DEVICE_LOCAL) != 0);
    memcpy(&copied_caps, &caps, sizeof(copied_caps));
    CHECK(memcmp(&copied_caps, &caps, sizeof(caps)) == 0);
    for (i = 0; i < sizeof(adapter_types) / sizeof(adapter_types[0]); ++i) {
        memset(&fake, 0, sizeof(fake));
        fake.adapter_type = adapter_types[i];
        CHECK(rf_gpu_init(&gpu, RF_GPU_POLICY_OPTIONAL, &backend, &fake) == 0);
        CHECK(rf_gpu_get_status(&gpu, &status) == 0);
        CHECK(status.info.adapter_type == adapter_types[i] && status.ready);
        rf_gpu_shutdown(&gpu);
    }

    puts("rf-gpu-service-test: PASS");
    return 0;
}
