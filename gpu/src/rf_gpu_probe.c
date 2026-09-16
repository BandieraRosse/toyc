/* Hosted, SDK-free Vulkan discovery and device/queue smoke test. */

#include "rf_vulkan_min.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

struct rf_vk_api {
    void *library;
    rf_vk_get_instance_proc_addr_fn get_instance_proc_addr;
    rf_vk_enumerate_instance_version_fn enumerate_instance_version;
    rf_vk_create_instance_fn create_instance;
    rf_vk_destroy_instance_fn destroy_instance;
    rf_vk_enumerate_physical_devices_fn enumerate_physical_devices;
    rf_vk_get_physical_device_properties_fn get_physical_device_properties;
    rf_vk_get_physical_device_queue_family_properties_fn
        get_physical_device_queue_family_properties;
    rf_vk_create_device_fn create_device;
    rf_vk_destroy_device_fn destroy_device;
    rf_vk_get_device_queue_fn get_device_queue;
    rf_vk_get_physical_device_memory_properties_fn get_memory_properties;
    rf_vk_create_buffer_fn create_buffer;
    rf_vk_destroy_buffer_fn destroy_buffer;
    rf_vk_get_buffer_memory_requirements_fn get_buffer_memory_requirements;
    rf_vk_allocate_memory_fn allocate_memory;
    rf_vk_free_memory_fn free_memory;
    rf_vk_bind_buffer_memory_fn bind_buffer_memory;
    rf_vk_map_memory_fn map_memory;
    rf_vk_unmap_memory_fn unmap_memory;
    rf_vk_create_descriptor_set_layout_fn create_descriptor_set_layout;
    rf_vk_destroy_descriptor_set_layout_fn destroy_descriptor_set_layout;
    rf_vk_create_descriptor_pool_fn create_descriptor_pool;
    rf_vk_destroy_descriptor_pool_fn destroy_descriptor_pool;
    rf_vk_allocate_descriptor_sets_fn allocate_descriptor_sets;
    rf_vk_update_descriptor_sets_fn update_descriptor_sets;
    rf_vk_create_shader_module_fn create_shader_module;
    rf_vk_destroy_shader_module_fn destroy_shader_module;
    rf_vk_create_pipeline_layout_fn create_pipeline_layout;
    rf_vk_destroy_pipeline_layout_fn destroy_pipeline_layout;
    rf_vk_create_compute_pipelines_fn create_compute_pipelines;
    rf_vk_destroy_pipeline_fn destroy_pipeline;
    rf_vk_create_command_pool_fn create_command_pool;
    rf_vk_destroy_command_pool_fn destroy_command_pool;
    rf_vk_allocate_command_buffers_fn allocate_command_buffers;
    rf_vk_begin_command_buffer_fn begin_command_buffer;
    rf_vk_end_command_buffer_fn end_command_buffer;
    rf_vk_cmd_bind_pipeline_fn cmd_bind_pipeline;
    rf_vk_cmd_bind_descriptor_sets_fn cmd_bind_descriptor_sets;
    rf_vk_cmd_dispatch_fn cmd_dispatch;
    rf_vk_create_fence_fn create_fence;
    rf_vk_destroy_fence_fn destroy_fence;
    rf_vk_queue_submit_fn queue_submit;
    rf_vk_wait_for_fences_fn wait_for_fences;
};

#if defined(_WIN32)
static void *rf_library_open(void) { return (void *)LoadLibraryA("vulkan-1.dll"); }
static void *rf_library_symbol(void *library, const char *name)
{ return (void *)GetProcAddress((HMODULE)library, name); }
static void rf_library_close(void *library) { FreeLibrary((HMODULE)library); }
#else
static void *rf_library_open(void)
{ return dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL); }
static void *rf_library_symbol(void *library, const char *name)
{ return dlsym(library, name); }
static void rf_library_close(void *library) { dlclose(library); }
#endif

static void *load_global(struct rf_vk_api *api, const char *name)
{
    rf_vk_void_function function = api->get_instance_proc_addr(NULL, name);
    void *result = NULL;
    /* ISO C does not promise that function and data pointers have the same
     * representation.  POSIX dlsym does; memcpy keeps strict compilers quiet. */
    memcpy(&result, &function,
           sizeof(result) < sizeof(function) ? sizeof(result) : sizeof(function));
    return result;
}

static void *load_instance(struct rf_vk_api *api, rf_vk_instance instance,
                           const char *name)
{
    rf_vk_void_function function = api->get_instance_proc_addr(instance, name);
    void *result = NULL;
    memcpy(&result, &function,
           sizeof(result) < sizeof(function) ? sizeof(result) : sizeof(function));
    return result;
}

#define RF_LOAD(target, loader, instance, name) do {                         \
    void *rf_symbol = loader(api, instance, name);                            \
    if (!rf_symbol) {                                                         \
        fprintf(stderr, "rf-gpu-probe: missing Vulkan entry %s\n", name);    \
        return -1;                                                            \
    }                                                                         \
    memcpy(&(target), &rf_symbol,                                             \
           sizeof(target) < sizeof(rf_symbol) ? sizeof(target)                \
                                               : sizeof(rf_symbol));           \
} while (0)

static int api_open(struct rf_vk_api *api)
{
    void *symbol;
    memset(api, 0, sizeof(*api));
    api->library = rf_library_open();
    if (!api->library) {
        fprintf(stderr, "rf-gpu-probe: Vulkan loader unavailable\n");
        return -1;
    }
    symbol = rf_library_symbol(api->library, "vkGetInstanceProcAddr");
    if (!symbol) {
        fprintf(stderr, "rf-gpu-probe: vkGetInstanceProcAddr unavailable\n");
        rf_library_close(api->library);
        memset(api, 0, sizeof(*api));
        return -1;
    }
    memcpy(&api->get_instance_proc_addr, &symbol,
           sizeof(api->get_instance_proc_addr) < sizeof(symbol)
               ? sizeof(api->get_instance_proc_addr) : sizeof(symbol));

    /* vkEnumerateInstanceVersion is optional before Vulkan 1.1. */
    symbol = load_global(api, "vkEnumerateInstanceVersion");
    if (symbol)
        memcpy(&api->enumerate_instance_version, &symbol,
               sizeof(api->enumerate_instance_version) < sizeof(symbol)
                   ? sizeof(api->enumerate_instance_version) : sizeof(symbol));
    symbol = load_global(api, "vkCreateInstance");
    if (!symbol) {
        fprintf(stderr, "rf-gpu-probe: vkCreateInstance unavailable\n");
        rf_library_close(api->library);
        memset(api, 0, sizeof(*api));
        return -1;
    }
    memcpy(&api->create_instance, &symbol,
           sizeof(api->create_instance) < sizeof(symbol)
               ? sizeof(api->create_instance) : sizeof(symbol));
    return 0;
}

static void api_close(struct rf_vk_api *api)
{
    if (api->library) rf_library_close(api->library);
    memset(api, 0, sizeof(*api));
}

static int api_load_instance(struct rf_vk_api *api, rf_vk_instance instance)
{
    RF_LOAD(api->destroy_instance, load_instance, instance,
            "vkDestroyInstance");
    RF_LOAD(api->enumerate_physical_devices, load_instance, instance,
            "vkEnumeratePhysicalDevices");
    RF_LOAD(api->get_physical_device_properties, load_instance, instance,
            "vkGetPhysicalDeviceProperties");
    RF_LOAD(api->get_physical_device_queue_family_properties, load_instance,
            instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    RF_LOAD(api->create_device, load_instance, instance, "vkCreateDevice");
    RF_LOAD(api->destroy_device, load_instance, instance, "vkDestroyDevice");
    RF_LOAD(api->get_device_queue, load_instance, instance,
            "vkGetDeviceQueue");
    RF_LOAD(api->get_memory_properties, load_instance, instance,
            "vkGetPhysicalDeviceMemoryProperties");
#define RF_LOAD_DEVICE(member, name) \
    RF_LOAD(api->member, load_instance, instance, name)
    RF_LOAD_DEVICE(create_buffer, "vkCreateBuffer");
    RF_LOAD_DEVICE(destroy_buffer, "vkDestroyBuffer");
    RF_LOAD_DEVICE(get_buffer_memory_requirements, "vkGetBufferMemoryRequirements");
    RF_LOAD_DEVICE(allocate_memory, "vkAllocateMemory");
    RF_LOAD_DEVICE(free_memory, "vkFreeMemory");
    RF_LOAD_DEVICE(bind_buffer_memory, "vkBindBufferMemory");
    RF_LOAD_DEVICE(map_memory, "vkMapMemory");
    RF_LOAD_DEVICE(unmap_memory, "vkUnmapMemory");
    RF_LOAD_DEVICE(create_descriptor_set_layout, "vkCreateDescriptorSetLayout");
    RF_LOAD_DEVICE(destroy_descriptor_set_layout, "vkDestroyDescriptorSetLayout");
    RF_LOAD_DEVICE(create_descriptor_pool, "vkCreateDescriptorPool");
    RF_LOAD_DEVICE(destroy_descriptor_pool, "vkDestroyDescriptorPool");
    RF_LOAD_DEVICE(allocate_descriptor_sets, "vkAllocateDescriptorSets");
    RF_LOAD_DEVICE(update_descriptor_sets, "vkUpdateDescriptorSets");
    RF_LOAD_DEVICE(create_shader_module, "vkCreateShaderModule");
    RF_LOAD_DEVICE(destroy_shader_module, "vkDestroyShaderModule");
    RF_LOAD_DEVICE(create_pipeline_layout, "vkCreatePipelineLayout");
    RF_LOAD_DEVICE(destroy_pipeline_layout, "vkDestroyPipelineLayout");
    RF_LOAD_DEVICE(create_compute_pipelines, "vkCreateComputePipelines");
    RF_LOAD_DEVICE(destroy_pipeline, "vkDestroyPipeline");
    RF_LOAD_DEVICE(create_command_pool, "vkCreateCommandPool");
    RF_LOAD_DEVICE(destroy_command_pool, "vkDestroyCommandPool");
    RF_LOAD_DEVICE(allocate_command_buffers, "vkAllocateCommandBuffers");
    RF_LOAD_DEVICE(begin_command_buffer, "vkBeginCommandBuffer");
    RF_LOAD_DEVICE(end_command_buffer, "vkEndCommandBuffer");
    RF_LOAD_DEVICE(cmd_bind_pipeline, "vkCmdBindPipeline");
    RF_LOAD_DEVICE(cmd_bind_descriptor_sets, "vkCmdBindDescriptorSets");
    RF_LOAD_DEVICE(cmd_dispatch, "vkCmdDispatch");
    RF_LOAD_DEVICE(create_fence, "vkCreateFence");
    RF_LOAD_DEVICE(destroy_fence, "vkDestroyFence");
    RF_LOAD_DEVICE(queue_submit, "vkQueueSubmit");
    RF_LOAD_DEVICE(wait_for_fences, "vkWaitForFences");
#undef RF_LOAD_DEVICE
    return 0;
}

static const char *device_type_name(uint32_t type)
{
    switch (type) {
    case RF_VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
    case RF_VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
    case RF_VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
    case RF_VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu";
    default: return "other";
    }
}

static void print_version(uint32_t version)
{
    printf("%u.%u.%u", RF_VK_VERSION_MAJOR(version),
           RF_VK_VERSION_MINOR(version), RF_VK_VERSION_PATCH(version));
}

static int choose_queue(const struct rf_vk_queue_family_properties *families,
                        uint32_t count)
{
    uint32_t i;
    for (i = 0; i < count; ++i)
        if (families[i].queue_count &&
            (families[i].queue_flags & RF_VK_QUEUE_COMPUTE_BIT))
            return (int)i;
    return -1;
}

/* SPIR-V 1.0 for:
 *   data[gl_GlobalInvocationID.x] = data[...] * 3u + 1u;
 * Keeping the fixed program in-tree makes the probe independent of a runtime
 * shader compiler while still exercising RF-owned pipeline creation. */
static const uint32_t compute_spirv[] = {
    0x07230203, 0x00010000, 0x00000000, 23, 0,
    0x00020011, 1,
    0x0003000e, 0, 1,
    0x0006000f, 5, 15, 0x6e69616d, 0, 6,
    0x00060010, 15, 17, 1, 1, 1,
    0x00040047, 6, 11, 28,
    0x00040047, 7, 6, 4,
    0x00050048, 8, 0, 35, 0,
    0x00030047, 8, 3,
    0x00040047, 10, 34, 0,
    0x00040047, 10, 33, 0,
    0x00020013, 1,
    0x00030021, 2, 1,
    0x00040015, 3, 32, 0,
    0x00040017, 4, 3, 3,
    0x00040020, 5, 1, 4,
    0x0004003b, 5, 6, 1,
    0x0003001d, 7, 3,
    0x0003001e, 8, 7,
    0x00040020, 9, 2, 8,
    0x0004003b, 9, 10, 2,
    0x0004002b, 3, 11, 0,
    0x00040020, 12, 2, 3,
    0x0004002b, 3, 13, 3,
    0x0004002b, 3, 14, 1,
    0x00050036, 1, 15, 0, 2,
    0x000200f8, 16,
    0x0004003d, 4, 17, 6,
    0x00050051, 3, 18, 17, 0,
    0x00060041, 12, 19, 10, 11, 18,
    0x0004003d, 3, 20, 19,
    0x00050084, 3, 21, 20, 13,
    0x00050080, 3, 22, 21, 14,
    0x0003003e, 19, 22,
    0x000100fd,
    0x00010038
};

static double now_ms(void)
{
#if defined(_WIN32)
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return (double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart;
#else
    struct timespec value;
    timespec_get(&value, TIME_UTC);
    return (double)value.tv_sec * 1000.0 + (double)value.tv_nsec / 1000000.0;
#endif
}

static int find_memory_type(struct rf_vk_api *api,
                            rf_vk_physical_device physical_device,
                            uint32_t allowed_bits)
{
    struct rf_vk_physical_device_memory_properties properties;
    uint32_t i;
    memset(&properties, 0, sizeof(properties));
    api->get_memory_properties(physical_device, &properties);
    for (i = 0; i < properties.memory_type_count; ++i) {
        rf_vk_flags required = RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if ((allowed_bits & (1U << i)) &&
            (properties.memory_types[i].property_flags & required) == required)
            return (int)i;
    }
    return -1;
}

static int device_smoke(struct rf_vk_api *api,
                        rf_vk_physical_device physical_device,
                        uint32_t family_index)
{
    static const uint32_t input[4] = {1, 2, 3, 4};
    static const uint32_t expected[4] = {4, 7, 10, 13};
    float priority = 1.0f;
    struct rf_vk_device_queue_create_info queue_info;
    struct rf_vk_device_create_info device_info;
    rf_vk_device device = NULL;
    rf_vk_queue queue = NULL;
    rf_vk_buffer buffer = NULL;
    rf_vk_device_memory memory = NULL;
    rf_vk_descriptor_set_layout set_layout = NULL;
    rf_vk_descriptor_pool descriptor_pool = NULL;
    rf_vk_descriptor_set descriptor_set = NULL;
    rf_vk_shader_module shader = NULL;
    rf_vk_pipeline_layout pipeline_layout = NULL;
    rf_vk_pipeline pipeline = NULL;
    rf_vk_command_pool command_pool = NULL;
    rf_vk_command_buffer command_buffer = NULL;
    rf_vk_fence fence = NULL;
    void *mapped = NULL;
    int ok = -1;
    double upload_start, upload_end, submit_start, submit_end, wait_end;
    double readback_end;
    rf_vk_result result;

    memset(&queue_info, 0, sizeof(queue_info));
    queue_info.s_type = RF_VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queue_family_index = family_index;
    queue_info.queue_count = 1;
    queue_info.queue_priorities = &priority;
    memset(&device_info, 0, sizeof(device_info));
    device_info.s_type = RF_VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queue_create_info_count = 1;
    device_info.queue_create_infos = &queue_info;
    result = api->create_device(physical_device, &device_info, NULL, &device);
    if (result != RF_VK_SUCCESS || !device) {
        fprintf(stderr, "rf-gpu-probe: vkCreateDevice failed (%d)\n", result);
        return -1;
    }
    api->get_device_queue(device, family_index, 0, &queue);
    if (!queue) {
        fprintf(stderr, "rf-gpu-probe: vkGetDeviceQueue returned NULL\n");
        api->destroy_device(device, NULL);
        goto cleanup;
    }
    {
        struct rf_vk_buffer_create_info info;
        struct rf_vk_memory_requirements requirements;
        struct rf_vk_memory_allocate_info allocation;
        int memory_type;
        memset(&info, 0, sizeof(info));
        info.s_type = RF_VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = sizeof(input);
        info.usage = RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        info.sharing_mode = RF_VK_SHARING_MODE_EXCLUSIVE;
        result = api->create_buffer(device, &info, NULL, &buffer);
        if (result != RF_VK_SUCCESS) goto vk_failure;
        api->get_buffer_memory_requirements(device, buffer, &requirements);
        memory_type = find_memory_type(api, physical_device,
                                       requirements.memory_type_bits);
        if (memory_type < 0) {
            fprintf(stderr, "rf-gpu-probe: no coherent host-visible memory\n");
            goto cleanup;
        }
        memset(&allocation, 0, sizeof(allocation));
        allocation.s_type = RF_VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocation_size = requirements.size;
        allocation.memory_type_index = (uint32_t)memory_type;
        result = api->allocate_memory(device, &allocation, NULL, &memory);
        if (result != RF_VK_SUCCESS) goto vk_failure;
        result = api->bind_buffer_memory(device, buffer, memory, 0);
        if (result != RF_VK_SUCCESS) goto vk_failure;
    }
    upload_start = now_ms();
    result = api->map_memory(device, memory, 0, sizeof(input), 0, &mapped);
    if (result != RF_VK_SUCCESS) goto vk_failure;
    memcpy(mapped, input, sizeof(input));
    api->unmap_memory(device, memory);
    mapped = NULL;
    upload_end = now_ms();
    {
        struct rf_vk_descriptor_set_layout_binding binding;
        struct rf_vk_descriptor_set_layout_create_info info;
        memset(&binding, 0, sizeof(binding));
        binding.descriptor_type = RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptor_count = 1;
        binding.stage_flags = RF_VK_SHADER_STAGE_COMPUTE_BIT;
        memset(&info, 0, sizeof(info));
        info.s_type = RF_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.binding_count = 1;
        info.bindings = &binding;
        result = api->create_descriptor_set_layout(device, &info, NULL,
                                                    &set_layout);
        if (result != RF_VK_SUCCESS) goto vk_failure;
    }
    {
        struct rf_vk_descriptor_pool_size size;
        struct rf_vk_descriptor_pool_create_info info;
        struct rf_vk_descriptor_set_allocate_info allocation;
        struct rf_vk_descriptor_buffer_info buffer_info;
        struct rf_vk_write_descriptor_set write;
        size.type = RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        size.descriptor_count = 1;
        memset(&info, 0, sizeof(info));
        info.s_type = RF_VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.max_sets = 1; info.pool_size_count = 1; info.pool_sizes = &size;
        result = api->create_descriptor_pool(device, &info, NULL,
                                             &descriptor_pool);
        if (result != RF_VK_SUCCESS) goto vk_failure;
        memset(&allocation, 0, sizeof(allocation));
        allocation.s_type = RF_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocation.descriptor_pool = descriptor_pool;
        allocation.descriptor_set_count = 1;
        allocation.set_layouts = &set_layout;
        result = api->allocate_descriptor_sets(device, &allocation,
                                               &descriptor_set);
        if (result != RF_VK_SUCCESS) goto vk_failure;
        buffer_info.buffer = buffer; buffer_info.offset = 0;
        buffer_info.range = sizeof(input);
        memset(&write, 0, sizeof(write));
        write.s_type = RF_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dst_set = descriptor_set; write.descriptor_count = 1;
        write.descriptor_type = RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.buffer_info = &buffer_info;
        api->update_descriptor_sets(device, 1, &write, 0, NULL);
    }
    {
        struct rf_vk_shader_module_create_info info;
        memset(&info, 0, sizeof(info));
        info.s_type = RF_VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.code_size = sizeof(compute_spirv); info.code = compute_spirv;
        result = api->create_shader_module(device, &info, NULL, &shader);
        if (result != RF_VK_SUCCESS) goto vk_failure;
    }
    {
        struct rf_vk_pipeline_layout_create_info layout_info;
        struct rf_vk_compute_pipeline_create_info pipeline_info;
        memset(&layout_info, 0, sizeof(layout_info));
        layout_info.s_type = RF_VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.set_layout_count = 1; layout_info.set_layouts = &set_layout;
        result = api->create_pipeline_layout(device, &layout_info, NULL,
                                             &pipeline_layout);
        if (result != RF_VK_SUCCESS) goto vk_failure;
        memset(&pipeline_info, 0, sizeof(pipeline_info));
        pipeline_info.s_type = RF_VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeline_info.stage.s_type =
            RF_VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipeline_info.stage.stage = RF_VK_SHADER_STAGE_COMPUTE_BIT;
        pipeline_info.stage.module = shader; pipeline_info.stage.name = "main";
        pipeline_info.layout = pipeline_layout;
        pipeline_info.base_pipeline_index = -1;
        result = api->create_compute_pipelines(device, NULL, 1, &pipeline_info,
                                               NULL, &pipeline);
        if (result != RF_VK_SUCCESS) goto vk_failure;
    }
    {
        struct rf_vk_command_pool_create_info pool_info;
        struct rf_vk_command_buffer_allocate_info allocation;
        struct rf_vk_command_buffer_begin_info begin;
        memset(&pool_info, 0, sizeof(pool_info));
        pool_info.s_type = RF_VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.queue_family_index = family_index;
        result = api->create_command_pool(device, &pool_info, NULL, &command_pool);
        if (result != RF_VK_SUCCESS) goto vk_failure;
        memset(&allocation, 0, sizeof(allocation));
        allocation.s_type = RF_VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocation.command_pool = command_pool;
        allocation.level = RF_VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.command_buffer_count = 1;
        result = api->allocate_command_buffers(device, &allocation,
                                               &command_buffer);
        if (result != RF_VK_SUCCESS) goto vk_failure;
        memset(&begin, 0, sizeof(begin));
        begin.s_type = RF_VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        result = api->begin_command_buffer(command_buffer, &begin);
        if (result != RF_VK_SUCCESS) goto vk_failure;
        api->cmd_bind_pipeline(command_buffer, RF_VK_PIPELINE_BIND_POINT_COMPUTE,
                               pipeline);
        api->cmd_bind_descriptor_sets(command_buffer,
            RF_VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1,
            &descriptor_set, 0, NULL);
        api->cmd_dispatch(command_buffer, 4, 1, 1);
        result = api->end_command_buffer(command_buffer);
        if (result != RF_VK_SUCCESS) goto vk_failure;
    }
    {
        struct rf_vk_fence_create_info fence_info;
        struct rf_vk_submit_info submit;
        memset(&fence_info, 0, sizeof(fence_info));
        fence_info.s_type = RF_VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        result = api->create_fence(device, &fence_info, NULL, &fence);
        if (result != RF_VK_SUCCESS) goto vk_failure;
        memset(&submit, 0, sizeof(submit));
        submit.s_type = RF_VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.command_buffer_count = 1;
        submit.command_buffers = &command_buffer;
        submit_start = now_ms();
        result = api->queue_submit(queue, 1, &submit, fence);
        submit_end = now_ms();
        if (result != RF_VK_SUCCESS) goto vk_failure;
        result = api->wait_for_fences(device, 1, &fence, RF_VK_TRUE,
                                      5000000000ULL);
        wait_end = now_ms();
        if (result != RF_VK_SUCCESS) goto vk_failure;
    }
    result = api->map_memory(device, memory, 0, sizeof(input), 0, &mapped);
    if (result != RF_VK_SUCCESS) goto vk_failure;
    if (memcmp(mapped, expected, sizeof(expected)) != 0) {
        uint32_t *values = mapped;
        fprintf(stderr, "rf-gpu-probe: compute mismatch: %u %u %u %u\n",
                values[0], values[1], values[2], values[3]);
        goto cleanup;
    }
    readback_end = now_ms();
    printf("compute: PASS (1 2 3 4 -> 4 7 10 13)\n");
    printf("timing-ms: upload=%.3f submit=%.3f execution-wait=%.3f "
           "readback=%.3f total=%.3f\n",
           upload_end - upload_start, submit_end - submit_start,
           wait_end - submit_end, readback_end - wait_end,
           readback_end - upload_start);
    ok = 0;
    goto cleanup;

vk_failure:
    fprintf(stderr, "rf-gpu-probe: Vulkan compute operation failed (%d)\n",
            result);
cleanup:
    if (mapped) api->unmap_memory(device, memory);
    if (fence) api->destroy_fence(device, fence, NULL);
    if (command_pool) api->destroy_command_pool(device, command_pool, NULL);
    if (pipeline) api->destroy_pipeline(device, pipeline, NULL);
    if (pipeline_layout)
        api->destroy_pipeline_layout(device, pipeline_layout, NULL);
    if (shader) api->destroy_shader_module(device, shader, NULL);
    if (descriptor_pool)
        api->destroy_descriptor_pool(device, descriptor_pool, NULL);
    if (set_layout)
        api->destroy_descriptor_set_layout(device, set_layout, NULL);
    if (buffer) api->destroy_buffer(device, buffer, NULL);
    if (memory) api->free_memory(device, memory, NULL);
    api->destroy_device(device, NULL);
    return ok;
}

int main(void)
{
    struct rf_vk_api api;
    struct rf_vk_application_info app_info;
    struct rf_vk_instance_create_info instance_info;
    rf_vk_instance instance = NULL;
    rf_vk_physical_device *devices = NULL;
    uint32_t loader_version = RF_VK_MAKE_VERSION(1, 0, 0);
    uint32_t device_count = 0;
    rf_vk_result result;
    int exit_code = 1;
    rf_vk_physical_device selected_device = NULL;
    uint32_t selected_family = 0;
    uint32_t selected_type = RF_VK_PHYSICAL_DEVICE_TYPE_OTHER;
    char selected_name[RF_VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = {0};
    uint32_t i;

    if (api_open(&api) < 0) return 2;
    if (api.enumerate_instance_version) {
        result = api.enumerate_instance_version(&loader_version);
        if (result != RF_VK_SUCCESS) {
            fprintf(stderr, "rf-gpu-probe: version query failed (%d)\n", result);
            goto done;
        }
    }
    printf("loader-api: ");
    print_version(loader_version);
    putchar('\n');

    memset(&app_info, 0, sizeof(app_info));
    app_info.s_type = RF_VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.application_name = "rf-gpu-probe";
    app_info.application_version = RF_VK_MAKE_VERSION(0, 1, 0);
    app_info.engine_name = "Rasterfall";
    app_info.engine_version = RF_VK_MAKE_VERSION(0, 1, 0);
    app_info.api_version = RF_VK_MAKE_VERSION(1, 0, 0);
    memset(&instance_info, 0, sizeof(instance_info));
    instance_info.s_type = RF_VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.application_info = &app_info;
    result = api.create_instance(&instance_info, NULL, &instance);
    if (result != RF_VK_SUCCESS || !instance) {
        fprintf(stderr, "rf-gpu-probe: vkCreateInstance failed (%d)\n", result);
        goto done;
    }
    if (api_load_instance(&api, instance) < 0) goto done;

    result = api.enumerate_physical_devices(instance, &device_count, NULL);
    if (result != RF_VK_SUCCESS || !device_count) {
        fprintf(stderr, "rf-gpu-probe: no Vulkan physical device (%d)\n",
                result);
        goto done;
    }
    devices = calloc(device_count, sizeof(*devices));
    if (!devices) {
        fprintf(stderr, "rf-gpu-probe: out of memory\n");
        goto done;
    }
    result = api.enumerate_physical_devices(instance, &device_count, devices);
    if (result != RF_VK_SUCCESS && result != RF_VK_INCOMPLETE) {
        fprintf(stderr, "rf-gpu-probe: device enumeration failed (%d)\n",
                result);
        goto done;
    }
    printf("adapters: %u\n", device_count);
    for (i = 0; i < device_count; ++i) {
        union {
            uint64_t alignment;
            unsigned char bytes[
                RF_VK_PHYSICAL_DEVICE_PROPERTIES_STORAGE_SIZE];
        } property_storage;
        struct rf_vk_physical_device_properties_prefix *properties =
            (struct rf_vk_physical_device_properties_prefix *)
                property_storage.bytes;
        struct rf_vk_queue_family_properties *families = NULL;
        uint32_t family_count = 0, q;
        int selected_queue;

        memset(&property_storage, 0, sizeof(property_storage));
        api.get_physical_device_properties(devices[i], property_storage.bytes);
        api.get_physical_device_queue_family_properties(devices[i],
                                                         &family_count, NULL);
        if (family_count)
            families = calloc(family_count, sizeof(*families));
        if (family_count && !families) {
            fprintf(stderr, "rf-gpu-probe: out of memory\n");
            goto done;
        }
        if (families)
            api.get_physical_device_queue_family_properties(
                devices[i], &family_count, families);
        printf("adapter[%u]: %s\n", i, properties->device_name);
        printf("  type: %s\n", device_type_name(properties->device_type));
        printf("  api: "); print_version(properties->api_version); putchar('\n');
        printf("  vendor/device: %04x:%04x\n", properties->vendor_id,
               properties->device_id);
        printf("  driver: 0x%08x\n", properties->driver_version);
        printf("  queue-families: %u\n", family_count);
        for (q = 0; q < family_count; ++q) {
            printf("    [%u] count=%u flags=%s%s%s\n", q,
                   families[q].queue_count,
                   families[q].queue_flags & RF_VK_QUEUE_GRAPHICS_BIT
                       ? "graphics " : "",
                   families[q].queue_flags & RF_VK_QUEUE_COMPUTE_BIT
                       ? "compute " : "",
                   families[q].queue_flags & RF_VK_QUEUE_TRANSFER_BIT
                       ? "transfer" : "");
        }
        selected_queue = choose_queue(families, family_count);
        if (selected_queue >= 0 &&
            (!selected_device ||
             (selected_type != RF_VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
              properties->device_type ==
                  RF_VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU))) {
            selected_device = devices[i];
            selected_family = (uint32_t)selected_queue;
            selected_type = properties->device_type;
            snprintf(selected_name, sizeof(selected_name), "%s",
                     properties->device_name);
        }
        free(families);
    }
    if (!selected_device) {
        fprintf(stderr, "rf-gpu-probe: no compute-capable queue family\n");
        goto done;
    }
    printf("selected: %s (%s, queue-family=%u)\n", selected_name,
           device_type_name(selected_type), selected_family);
    if (device_smoke(&api, selected_device, selected_family) < 0)
        goto done;
    exit_code = 0;

done:
    free(devices);
    if (instance && api.destroy_instance)
        api.destroy_instance(instance, NULL);
    api_close(&api);
    return exit_code;
}
