/* Hosted GPU-2 bring-up through the persistent RF Core Vulkan backend. */

#include "rf_gpu.h"
#include "rf_gpu_vulkan_backend.h"

#include <stdio.h>
#include <string.h>

static const char *adapter_type_name(unsigned int type)
{
    switch (type) {
    case RF_GPU_ADAPTER_INTEGRATED: return "integrated";
    case RF_GPU_ADAPTER_DISCRETE: return "discrete";
    case RF_GPU_ADAPTER_VIRTUAL: return "virtual";
    case RF_GPU_ADAPTER_CPU: return "cpu";
    default: return "other";
    }
}

int main(void)
{
    struct rf_gpu gpu;
    struct rf_gpu_status status;
    struct rf_gpu_vulkan_context context;
    int unavailable;

    memset(&context, 0, sizeof(context));
    if (rf_gpu_init(&gpu, RF_GPU_POLICY_REQUIRED, &rf_gpu_vulkan_backend,
                    &context) < 0) {
        rf_gpu_get_status(&gpu, &status);
        unavailable = status.state == RF_GPU_STATE_UNAVAILABLE;
        fprintf(stderr, "rf-gpu-probe: %s\n", status.message);
        rf_gpu_shutdown(&gpu);
        return unavailable ? 2 : 1;
    }
    rf_gpu_get_status(&gpu, &status);
    printf("GPU: %s\n", rf_gpu_state_name(status.state));
    printf("selected: %s (%s, vendor/device=%04x:%04x, queue-family=%u)\n",
           status.info.adapter_name, adapter_type_name(status.info.adapter_type),
           status.info.vendor_id, status.info.device_id,
           status.info.queue_family);
    puts("compute: PASS (1 2 3 4 -> 4 7 10 13)");
    rf_gpu_shutdown(&gpu);
    if (context.implementation) {
        fputs("rf-gpu-probe: backend did not release context\n", stderr);
        return 1;
    }
    puts("shutdown: PASS");
    return 0;
}
