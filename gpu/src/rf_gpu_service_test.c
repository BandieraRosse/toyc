#include "rf_gpu.h"

#include <stdio.h>
#include <string.h>

struct fake_backend {
    int result;
    int init_count;
    int shutdown_count;
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
    info->adapter_type = RF_GPU_ADAPTER_DISCRETE;
    info->vendor_id = 0x10de;
    info->device_id = 0x1234;
    info->queue_family = 2;
    snprintf(message, message_capacity, "fake ready");
    return RF_GPU_BACKEND_READY;
}

static void fake_shutdown(void *context)
{
    struct fake_backend *fake = context;
    ++fake->shutdown_count;
}

static const struct rf_gpu_backend backend = {fake_init, fake_shutdown};

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
    rf_gpu_shutdown(&gpu);
    CHECK(fake.shutdown_count == 1);

    puts("rf-gpu-service-test: PASS");
    return 0;
}
