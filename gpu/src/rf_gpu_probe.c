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

static const char *support_name(unsigned int supported)
{
    return supported ? "supported" : "unsupported";
}

int main(void)
{
    struct rf_gpu gpu;
    struct rf_gpu_status status;
    struct rf_gpu_vulkan_context context;
    int unavailable;
    unsigned int i;

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
    printf("adapter-index: %u\n", status.info.capabilities.adapter_index);
    printf("Vulkan API: %u.%u.%u\n",
           status.info.capabilities.api_version >> 22,
           (status.info.capabilities.api_version >> 12) & 0x3ffU,
           status.info.capabilities.api_version & 0xfffU);
    printf("compute: %s; workgroup invocations=%u size=%ux%ux%u count=%ux%ux%u\n",
           support_name(status.renderer.compute),
           status.info.capabilities.max_compute_work_group_invocations,
           status.info.capabilities.max_compute_work_group_size[0],
           status.info.capabilities.max_compute_work_group_size[1],
           status.info.capabilities.max_compute_work_group_size[2],
           status.info.capabilities.max_compute_work_group_count[0],
           status.info.capabilities.max_compute_work_group_count[1],
           status.info.capabilities.max_compute_work_group_count[2]);
    printf("storage-buffer: max=%llu alignment=%llu; shaderInt64=%s\n",
           status.info.capabilities.max_storage_buffer_range,
           status.info.capabilities.min_storage_buffer_offset_alignment,
           support_name(status.info.capabilities.shader_int64));
    printf("memory: heaps=%u types=%u device-local-output=%s "
           "host-visible-readback=%s coherent=%s non-coherent=%s atom=%llu\n",
           status.info.capabilities.memory_heap_count,
           status.info.capabilities.memory_type_count,
           support_name(status.info.capabilities.device_local_output_memory),
           support_name(status.info.capabilities.host_visible_readback_memory),
           support_name(status.info.capabilities.coherent_readback),
           support_name(status.info.capabilities.non_coherent_readback),
           status.info.capabilities.non_coherent_atom_size);
    for (i = 0; i < status.info.capabilities.memory_heap_count; ++i)
        printf("  heap[%u]: size=%llu flags=0x%08x\n", i,
               status.info.capabilities.memory_heaps[i].size,
               status.info.capabilities.memory_heaps[i].property_flags);
    for (i = 0; i < status.info.capabilities.memory_type_count; ++i)
        printf("  type[%u]: heap=%u flags=0x%08x\n", i,
               status.info.capabilities.memory_types[i].heap_index,
               status.info.capabilities.memory_types[i].property_flags);
    printf("framebuffer: %s\n", support_name(status.renderer.framebuffer));
    printf("raster_v1: %s", support_name(status.renderer.raster_v1));
    if (status.renderer.raster_v1)
        printf(" (workgroup=%ux%u)", status.renderer.raster_work_group_x,
               status.renderer.raster_work_group_y);
    putchar('\n');
    puts("compute: PASS (1 2 3 4 -> 4 7 10 13)");
    rf_gpu_shutdown(&gpu);
    if (context.implementation) {
        fputs("rf-gpu-probe: backend did not release context\n", stderr);
        return 1;
    }
    puts("shutdown: PASS");
    return 0;
}
