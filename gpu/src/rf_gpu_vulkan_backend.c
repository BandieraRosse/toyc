/* Hosted, SDK-free persistent Vulkan backend for the RF Core GPU service. */

#include "rf_vulkan_min.h"
#include "rf_vulkan_ray_min.h"
#include "rf_gpu.h"
#include "rf_gpu_vulkan_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

struct rf_vk_api {
    void *library;
    rf_vk_get_instance_proc_addr_fn get_instance_proc_addr;
    rf_vk_enumerate_instance_version_fn enumerate_instance_version;
    rf_vk_create_instance_fn create_instance;
    rf_vk_destroy_instance_fn destroy_instance;
    rf_vk_enumerate_physical_devices_fn enumerate_physical_devices;
    rf_vk_get_physical_device_properties_fn get_physical_device_properties;
    rf_vk_get_physical_device_features_fn get_physical_device_features;
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
    rf_vk_invalidate_mapped_memory_ranges_fn invalidate_mapped_memory_ranges;
    rf_vk_flush_mapped_memory_ranges_fn flush_mapped_memory_ranges;
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
    rf_vk_create_pipeline_cache_fn create_pipeline_cache;
    rf_vk_destroy_pipeline_cache_fn destroy_pipeline_cache;
    rf_vk_get_pipeline_cache_data_fn get_pipeline_cache_data;
    rf_vk_destroy_pipeline_fn destroy_pipeline;
    rf_vk_create_command_pool_fn create_command_pool;
    rf_vk_destroy_command_pool_fn destroy_command_pool;
    rf_vk_reset_command_pool_fn reset_command_pool;
    rf_vk_allocate_command_buffers_fn allocate_command_buffers;
    rf_vk_begin_command_buffer_fn begin_command_buffer;
    rf_vk_end_command_buffer_fn end_command_buffer;
    rf_vk_cmd_bind_pipeline_fn cmd_bind_pipeline;
    rf_vk_cmd_bind_descriptor_sets_fn cmd_bind_descriptor_sets;
    rf_vk_cmd_dispatch_fn cmd_dispatch;
    rf_vk_cmd_push_constants_fn cmd_push_constants;
    rf_vk_cmd_pipeline_barrier_fn cmd_pipeline_barrier;
    rf_vk_cmd_copy_buffer_fn cmd_copy_buffer;
    rf_vk_create_fence_fn create_fence;
    rf_vk_destroy_fence_fn destroy_fence;
    rf_vk_queue_submit_fn queue_submit;
    rf_vk_wait_for_fences_fn wait_for_fences;
    rf_vk_create_win32_surface_fn create_win32_surface;
    rf_vk_destroy_surface_fn destroy_surface;
    rf_vk_get_surface_support_fn get_surface_support;
    rf_vk_get_surface_capabilities_fn get_surface_capabilities;
    rf_vk_get_surface_formats_fn get_surface_formats;
    rf_vk_get_surface_present_modes_fn get_surface_present_modes;
    rf_vk_create_swapchain_fn create_swapchain;
    rf_vk_destroy_swapchain_fn destroy_swapchain;
    rf_vk_get_swapchain_images_fn get_swapchain_images;
    rf_vk_acquire_next_image_fn acquire_next_image;
    rf_vk_queue_present_fn queue_present;
    rf_vk_queue_wait_idle_fn queue_wait_idle;
    rf_vk_create_semaphore_fn create_semaphore;
    rf_vk_destroy_semaphore_fn destroy_semaphore;
    rf_vk_reset_fences_fn reset_fences;
    rf_vk_cmd_copy_buffer_to_image_fn cmd_copy_buffer_to_image;
    rf_vk_create_query_pool_fn create_query_pool;
    rf_vk_destroy_query_pool_fn destroy_query_pool;
    rf_vk_cmd_reset_query_pool_fn cmd_reset_query_pool;
    rf_vk_cmd_write_timestamp_fn cmd_write_timestamp;
    rf_vk_get_query_pool_results_fn get_query_pool_results;
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
    RF_LOAD(api->get_physical_device_features, load_instance, instance,
            "vkGetPhysicalDeviceFeatures");
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
    RF_LOAD_DEVICE(invalidate_mapped_memory_ranges,
                   "vkInvalidateMappedMemoryRanges");
    RF_LOAD_DEVICE(flush_mapped_memory_ranges, "vkFlushMappedMemoryRanges");
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
    RF_LOAD_DEVICE(create_pipeline_cache, "vkCreatePipelineCache");
    RF_LOAD_DEVICE(destroy_pipeline_cache, "vkDestroyPipelineCache");
    RF_LOAD_DEVICE(get_pipeline_cache_data, "vkGetPipelineCacheData");
    RF_LOAD_DEVICE(destroy_pipeline, "vkDestroyPipeline");
    RF_LOAD_DEVICE(create_command_pool, "vkCreateCommandPool");
    RF_LOAD_DEVICE(destroy_command_pool, "vkDestroyCommandPool");
    RF_LOAD_DEVICE(reset_command_pool, "vkResetCommandPool");
    RF_LOAD_DEVICE(allocate_command_buffers, "vkAllocateCommandBuffers");
    RF_LOAD_DEVICE(begin_command_buffer, "vkBeginCommandBuffer");
    RF_LOAD_DEVICE(end_command_buffer, "vkEndCommandBuffer");
    RF_LOAD_DEVICE(cmd_bind_pipeline, "vkCmdBindPipeline");
    RF_LOAD_DEVICE(cmd_bind_descriptor_sets, "vkCmdBindDescriptorSets");
    RF_LOAD_DEVICE(cmd_dispatch, "vkCmdDispatch");
    RF_LOAD_DEVICE(cmd_push_constants, "vkCmdPushConstants");
    RF_LOAD_DEVICE(cmd_pipeline_barrier, "vkCmdPipelineBarrier");
    RF_LOAD_DEVICE(cmd_copy_buffer, "vkCmdCopyBuffer");
    RF_LOAD_DEVICE(create_fence, "vkCreateFence");
    RF_LOAD_DEVICE(destroy_fence, "vkDestroyFence");
    RF_LOAD_DEVICE(queue_submit, "vkQueueSubmit");
    RF_LOAD_DEVICE(wait_for_fences, "vkWaitForFences");
#define RF_LOAD_OPTIONAL(member, name) do {                                  \
    void *rf_optional = load_instance(api, instance, name);                  \
    if (rf_optional) memcpy(&api->member, &rf_optional,                       \
        sizeof(api->member) < sizeof(rf_optional) ? sizeof(api->member)      \
                                                   : sizeof(rf_optional));    \
} while (0)
    RF_LOAD_OPTIONAL(create_win32_surface, "vkCreateWin32SurfaceKHR");
    RF_LOAD_OPTIONAL(destroy_surface, "vkDestroySurfaceKHR");
    RF_LOAD_OPTIONAL(get_surface_support, "vkGetPhysicalDeviceSurfaceSupportKHR");
    RF_LOAD_OPTIONAL(get_surface_capabilities, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    RF_LOAD_OPTIONAL(get_surface_formats, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    RF_LOAD_OPTIONAL(get_surface_present_modes, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    RF_LOAD_OPTIONAL(create_swapchain, "vkCreateSwapchainKHR");
    RF_LOAD_OPTIONAL(destroy_swapchain, "vkDestroySwapchainKHR");
    RF_LOAD_OPTIONAL(get_swapchain_images, "vkGetSwapchainImagesKHR");
    RF_LOAD_OPTIONAL(acquire_next_image, "vkAcquireNextImageKHR");
    RF_LOAD_OPTIONAL(queue_present, "vkQueuePresentKHR");
    RF_LOAD_OPTIONAL(queue_wait_idle, "vkQueueWaitIdle");
    RF_LOAD_OPTIONAL(create_semaphore, "vkCreateSemaphore");
    RF_LOAD_OPTIONAL(destroy_semaphore, "vkDestroySemaphore");
    RF_LOAD_OPTIONAL(reset_fences, "vkResetFences");
    RF_LOAD_OPTIONAL(cmd_copy_buffer_to_image, "vkCmdCopyBufferToImage");
    RF_LOAD_OPTIONAL(create_query_pool, "vkCreateQueryPool");
    RF_LOAD_OPTIONAL(destroy_query_pool, "vkDestroyQueryPool");
    RF_LOAD_OPTIONAL(cmd_reset_query_pool, "vkCmdResetQueryPool");
    RF_LOAD_OPTIONAL(cmd_write_timestamp, "vkCmdWriteTimestamp");
    RF_LOAD_OPTIONAL(get_query_pool_results, "vkGetQueryPoolResults");
#undef RF_LOAD_OPTIONAL
#undef RF_LOAD_DEVICE
    return 0;
}

static int choose_queue(const struct rf_vk_queue_family_properties *families,
                        uint32_t count, uint32_t required)
{
    uint32_t i;
    for (i = 0; i < count; ++i)
        if (families[i].queue_count &&
            (families[i].queue_flags & required) == required)
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
                        rf_vk_device device, rf_vk_queue queue,
                        uint32_t family_index)
{
    static const uint32_t input[4] = {1, 2, 3, 4};
    static const uint32_t expected[4] = {4, 7, 10, 13};
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
    return ok;
}

struct rf_gpu_vulkan_present_image {
    rf_vk_semaphore render_finished;
    uint64_t generation;
    uint64_t acquire_generation, submit_generation;
    uint64_t present_generation, retire_generation;
    uint32_t owner_slot;
    uint32_t state, render_finished_state;
};

struct rf_pipeline_cache_disk {
    uint32_t magic,version,bytes,checksum,vendor,device,driver;
    uint8_t uuid[16];
};
struct rf_gpu_vulkan_impl {
    struct rf_vk_api api;
    struct rf_rt_api rt;
    struct rf_rt_properties rt_properties;
    int ray_query_enabled;
    rf_vk_instance instance;
    rf_vk_physical_device physical_device;
    rf_vk_device device;
    rf_vk_pipeline_cache pipeline_cache;
    int pipeline_cache_attempted;
    struct rf_pipeline_cache_disk pipeline_cache_identity;
    char pipeline_cache_path[1024];
    rf_vk_queue queue;
    uint32_t queue_family;
    uint32_t queue_flags;
    uint32_t shader_int64_enabled;
    int light_profile_enabled;
    uint64_t max_storage_buffer_range;
    uint64_t non_coherent_atom_size;
    float timestamp_period;
    uint32_t timestamp_valid_bits;
    int timestamp_supported;
    rf_vk_surface surface;
    int native_presentation_supported;
    int prefer_high_rate_present;
    rf_vk_swapchain swapchain;
    rf_vk_image *swapchain_images;
    uint32_t swapchain_image_count, swapchain_width, swapchain_height;
    uint32_t swapchain_format, present_mode;
    struct rf_gpu_vulkan_present_image *present_images;
    uint64_t presenter_generation, next_present_generation, next_frame_number;
    uint64_t hot_queue_idle_count, recreate_queue_idle_count;
    uint32_t outstanding_presents;
    int presenter_poisoned;
    uint32_t next_slot_id;
    uint32_t present_fault, present_fault_frame;
    uint64_t present_attempt;
    int present_fault_triggered;
};

static uint32_t pipeline_cache_checksum(const unsigned char *bytes,size_t count)
{
    uint32_t hash=2166136261u;
    for(size_t i=0;i<count;++i)hash=(hash^bytes[i])*16777619u;
    return hash;
}
static void pipeline_cache_prepare(struct rf_gpu_vulkan_impl *p)
{
    if(p->pipeline_cache_attempted)return;
    p->pipeline_cache_attempted=1;
    const char *enabled=getenv("RF_GPU_PIPELINE_CACHE"),*path=getenv("RF_GPU_PIPELINE_CACHE_PATH");
    if(enabled && !strcmp(enabled,"0")) {
        fprintf(stderr,"rf-gpu-pipeline-cache: enabled=0 loaded-bytes=0\n");return;
    }
    struct rf_vk_physical_device_properties properties={0};
    p->api.get_physical_device_properties(p->physical_device,&properties);
    struct rf_pipeline_cache_disk *identity=&p->pipeline_cache_identity;
    identity->magic=0x43504652u;identity->version=1;
    identity->vendor=properties.vendor_id;identity->device=properties.device_id;
    identity->driver=properties.driver_version;memcpy(identity->uuid,properties.pipeline_cache_uuid,16);
    if(!path)snprintf(p->pipeline_cache_path,sizeof(p->pipeline_cache_path),
        "build/rf-gpu-pipelines-%08x-%08x.bin",identity->vendor,identity->device);
    else if(strcmp(path,"-") && strlen(path)<sizeof(p->pipeline_cache_path))
        memcpy(p->pipeline_cache_path,path,strlen(path)+1);
    struct rf_vk_pipeline_cache_create_info info={0};
    info.s_type=RF_VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    struct rf_pipeline_cache_disk disk={0};unsigned char *data=NULL;
    FILE *file=p->pipeline_cache_path[0]?fopen(p->pipeline_cache_path,"rb"):NULL;
    if(file) {
        if(fread(&disk,sizeof(disk),1,file)==1 && disk.magic==identity->magic && disk.version==1 &&
            disk.vendor==identity->vendor && disk.device==identity->device && disk.driver==identity->driver &&
            !memcmp(disk.uuid,identity->uuid,16) && disk.bytes>=32 && disk.bytes<=64u*1024u*1024u) {
            data=malloc(disk.bytes);
            if(data && fread(data,1,disk.bytes,file)==disk.bytes && fgetc(file)==EOF &&
                pipeline_cache_checksum(data,disk.bytes)==disk.checksum) {
                uint32_t header[4];memcpy(header,data,16);
                if(header[0]==32 && header[1]==1 && header[2]==identity->vendor && header[3]==identity->device &&
                    !memcmp(data+16,identity->uuid,16)) {info.initial_data_size=disk.bytes;info.initial_data=data;}
            }
        }
        fclose(file);
    }
    if(p->api.create_pipeline_cache(p->device,&info,NULL,&p->pipeline_cache)!=RF_VK_SUCCESS && info.initial_data_size) {
        info.initial_data_size=0;info.initial_data=NULL;
        p->api.create_pipeline_cache(p->device,&info,NULL,&p->pipeline_cache);
    }
    fprintf(stderr,"rf-gpu-pipeline-cache: enabled=%d loaded-bytes=%llu persistent=%d\n",
        p->pipeline_cache!=NULL,(unsigned long long)info.initial_data_size,p->pipeline_cache_path[0]!=0);
    free(data);
}
static void pipeline_cache_save(struct rf_gpu_vulkan_impl *p)
{
    if(!p->pipeline_cache || !p->pipeline_cache_path[0])return;
    size_t bytes=0;
    if(p->api.get_pipeline_cache_data(p->device,p->pipeline_cache,&bytes,NULL)!=RF_VK_SUCCESS ||
        bytes<32 || bytes>64u*1024u*1024u)return;
    unsigned char *data=malloc(bytes);if(!data)return;
    if(p->api.get_pipeline_cache_data(p->device,p->pipeline_cache,&bytes,data)!=RF_VK_SUCCESS){free(data);return;}
    struct rf_pipeline_cache_disk disk=p->pipeline_cache_identity;
    disk.bytes=(uint32_t)bytes;disk.checksum=pipeline_cache_checksum(data,bytes);
    if(!getenv("RF_GPU_PIPELINE_CACHE_PATH")) {
#if defined(_WIN32)
        CreateDirectoryA("build",NULL);
#else
        mkdir("build",0700);
#endif
    }
    char temporary[1088];
#if defined(_WIN32)
    unsigned long process=(unsigned long)GetCurrentProcessId();
#else
    unsigned long process=(unsigned long)getpid();
#endif
    snprintf(temporary,sizeof(temporary),"%s.%lu.tmp",p->pipeline_cache_path,process);
    FILE *file=fopen(temporary,"wb");int ok=0;
    if(file) {
        ok=fwrite(&disk,sizeof(disk),1,file)==1 && fwrite(data,1,bytes,file)==bytes;
        if(fclose(file))ok=0;
        if(ok) {
#if defined(_WIN32)
            ok=MoveFileExA(temporary,p->pipeline_cache_path,MOVEFILE_REPLACE_EXISTING)!=0;
#else
            ok=rename(temporary,p->pipeline_cache_path)==0;
#endif
        }
        if(!ok)remove(temporary);
    }
    fprintf(stderr,"rf-gpu-pipeline-cache: saved-bytes=%llu result=%s\n",
        (unsigned long long)bytes,ok?"ok":"unavailable");
    free(data);
}

struct rf_gpu_vulkan_framebuffer {
    struct rf_gpu_vulkan_impl *owner;
    rf_vk_buffer output_buffer;
    rf_vk_device_memory output_memory;
    rf_vk_buffer readback_buffer;
    rf_vk_device_memory readback_memory;
    int readback_coherent;
    rf_vk_descriptor_set_layout set_layout;
    rf_vk_descriptor_pool descriptor_pool;
    rf_vk_descriptor_set descriptor_set;
    rf_vk_shader_module shader;
    rf_vk_pipeline_layout pipeline_layout;
    rf_vk_pipeline pipeline;
    rf_vk_command_pool command_pool;
    rf_vk_command_buffer command_buffer;
    uint32_t width, height;
    uint64_t byte_size;
    uint64_t storage_size;
    uint32_t dispatch_x, dispatch_y;
};

/* One invocation writes one XRGB8888 pixel. */
static const uint32_t framebuffer_spirv[] = {
    0x07230203, 0x00010000, 0x00000000, 26, 0,
    0x00020011, 1, 0x0003000e, 0, 1,
    0x0006000f, 5, 15, 0x6e69616d, 0, 6,
    0x00060010, 15, 17, 1, 1, 1,
    0x00040047, 6, 11, 28, 0x00040047, 7, 6, 4,
    0x00050048, 8, 0, 35, 0, 0x00030047, 8, 3,
    0x00040047, 10, 34, 0, 0x00040047, 10, 33, 0,
    0x00020013, 1, 0x00030021, 2, 1,
    0x00040015, 3, 32, 0, 0x00040017, 4, 3, 3,
    0x00040020, 5, 1, 4, 0x0004003b, 5, 6, 1,
    0x0003001d, 7, 3, 0x0003001e, 8, 7,
    0x00040020, 9, 2, 8, 0x0004003b, 9, 10, 2,
    0x0004002b, 3, 11, 0, 0x00040020, 12, 2, 3,
    0x0004002b, 3, 13, 0x00010101,
    0x0004002b, 3, 14, 0xff000000,
    0x0004002b, 3, 23, 65535,
    0x00050036, 1, 15, 0, 2, 0x000200f8, 16,
    0x0004003d, 4, 17, 6, 0x00050051, 3, 18, 17, 0,
    0x00050051, 3, 22, 17, 1,
    0x00050084, 3, 24, 22, 23,
    0x00050080, 3, 25, 24, 18,
    0x00060041, 12, 19, 10, 11, 25,
    0x00050084, 3, 20, 25, 13,
    0x00050080, 3, 21, 20, 14,
    0x0003003e, 19, 21, 0x000100fd, 0x00010038
};

static int framebuffer_memory_type(struct rf_gpu_vulkan_impl *impl,
                                   uint32_t allowed, rf_vk_flags required,
                                   rf_vk_flags preferred,
                                   rf_vk_flags *selected_flags)
{
    struct rf_vk_physical_device_memory_properties properties;
    int fallback = -1;
    uint32_t i;
    memset(&properties, 0, sizeof(properties));
    impl->api.get_memory_properties(impl->physical_device, &properties);
    for (i = 0; i < properties.memory_type_count; ++i) {
        rf_vk_flags flags = properties.memory_types[i].property_flags;
        if (!(allowed & (1U << i)) || (flags & required) != required) continue;
        if (fallback < 0) fallback = (int)i;
        if ((flags & preferred) == preferred) {
            if (selected_flags) *selected_flags = flags;
            return (int)i;
        }
    }
    if (fallback >= 0 && selected_flags)
        *selected_flags = properties.memory_types[fallback].property_flags;
    return fallback;
}

static int framebuffer_buffer(struct rf_gpu_vulkan_impl *impl, uint64_t size,
                              rf_vk_flags usage, rf_vk_flags required,
                              rf_vk_flags preferred, rf_vk_buffer *buffer,
                              rf_vk_device_memory *memory,
                              rf_vk_flags *memory_flags)
{
    struct rf_vk_buffer_create_info info;
    struct rf_vk_memory_requirements requirements;
    struct rf_vk_memory_allocate_info allocation;
    int memory_type;
    memset(&info, 0, sizeof(info));
    info.s_type = RF_VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size; info.usage = usage;
    info.sharing_mode = RF_VK_SHARING_MODE_EXCLUSIVE;
    if (impl->api.create_buffer(impl->device, &info, NULL, buffer) != RF_VK_SUCCESS)
        return -1;
    impl->api.get_buffer_memory_requirements(impl->device, *buffer, &requirements);
    memory_type = framebuffer_memory_type(impl, requirements.memory_type_bits,
                                          required, preferred, memory_flags);
    if (memory_type < 0) return -1;
    memset(&allocation, 0, sizeof(allocation));
    allocation.s_type = RF_VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocation_size = requirements.size;
    allocation.memory_type_index = (uint32_t)memory_type;
    if (impl->api.allocate_memory(impl->device, &allocation, NULL, memory) != RF_VK_SUCCESS)
        return -1;
    return impl->api.bind_buffer_memory(impl->device, *buffer, *memory, 0) ==
        RF_VK_SUCCESS ? 0 : -1;
}

static void framebuffer_destroy(void *context, void *framebuffer)
{
    struct rf_gpu_vulkan_context *backend_context = context;
    struct rf_gpu_vulkan_framebuffer *fb = framebuffer;
    struct rf_gpu_vulkan_impl *impl = backend_context
        ? backend_context->implementation : NULL;
    if (!fb) return;
    if (!impl || fb->owner != impl) { free(fb); return; }
    if (fb->command_pool) impl->api.destroy_command_pool(impl->device, fb->command_pool, NULL);
    if (fb->pipeline) impl->api.destroy_pipeline(impl->device, fb->pipeline, NULL);
    if (fb->pipeline_layout) impl->api.destroy_pipeline_layout(impl->device, fb->pipeline_layout, NULL);
    if (fb->shader) impl->api.destroy_shader_module(impl->device, fb->shader, NULL);
    if (fb->descriptor_pool) impl->api.destroy_descriptor_pool(impl->device, fb->descriptor_pool, NULL);
    if (fb->set_layout) impl->api.destroy_descriptor_set_layout(impl->device, fb->set_layout, NULL);
    if (fb->readback_buffer) impl->api.destroy_buffer(impl->device, fb->readback_buffer, NULL);
    if (fb->readback_memory) impl->api.free_memory(impl->device, fb->readback_memory, NULL);
    if (fb->output_buffer) impl->api.destroy_buffer(impl->device, fb->output_buffer, NULL);
    if (fb->output_memory) impl->api.free_memory(impl->device, fb->output_memory, NULL);
    free(fb);
}

static int framebuffer_create(void *context, unsigned int width,
                              unsigned int height, void **framebuffer,
                              char *message, unsigned long message_capacity)
{
    struct rf_gpu_vulkan_context *backend_context = context;
    struct rf_gpu_vulkan_impl *impl = backend_context ? backend_context->implementation : NULL;
    struct rf_gpu_vulkan_framebuffer *fb = NULL;
    rf_vk_flags readback_flags = 0;
    if (!impl || !framebuffer || !width || !height) return -1;
    *framebuffer = NULL;
    fb = calloc(1, sizeof(*fb));
    if (!fb) goto failed;
    fb->owner = impl; fb->width = width; fb->height = height;
    fb->byte_size = (uint64_t)width * height * 4;
    fb->dispatch_x = width * height > 65535U ? 65535U : width * height;
    fb->dispatch_y = (width * height + fb->dispatch_x - 1) / fb->dispatch_x;
    fb->storage_size = (uint64_t)fb->dispatch_x * fb->dispatch_y * 4;
    if (framebuffer_buffer(impl, fb->storage_size,
            RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | RF_VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
            &fb->output_buffer, &fb->output_memory, NULL) < 0) goto failed;
    if (framebuffer_buffer(impl, fb->byte_size, RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &fb->readback_buffer, &fb->readback_memory, &readback_flags) < 0)
        goto failed;
    fb->readback_coherent = !!(readback_flags & RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    {
        struct rf_vk_descriptor_set_layout_binding binding;
        struct rf_vk_descriptor_set_layout_create_info info;
        memset(&binding, 0, sizeof(binding));
        binding.descriptor_type = RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptor_count = 1; binding.stage_flags = RF_VK_SHADER_STAGE_COMPUTE_BIT;
        memset(&info, 0, sizeof(info));
        info.s_type = RF_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.binding_count = 1; info.bindings = &binding;
        if (impl->api.create_descriptor_set_layout(impl->device, &info, NULL,
                                                   &fb->set_layout) != RF_VK_SUCCESS)
            goto failed;
    }
    {
        struct rf_vk_descriptor_pool_size size;
        struct rf_vk_descriptor_pool_create_info info;
        struct rf_vk_descriptor_set_allocate_info allocation;
        struct rf_vk_descriptor_buffer_info buffer_info;
        struct rf_vk_write_descriptor_set write;
        size.type = RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; size.descriptor_count = 1;
        memset(&info, 0, sizeof(info));
        info.s_type = RF_VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.max_sets = 1; info.pool_size_count = 1; info.pool_sizes = &size;
        if (impl->api.create_descriptor_pool(impl->device, &info, NULL,
                                             &fb->descriptor_pool) != RF_VK_SUCCESS)
            goto failed;
        memset(&allocation, 0, sizeof(allocation));
        allocation.s_type = RF_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocation.descriptor_pool = fb->descriptor_pool;
        allocation.descriptor_set_count = 1; allocation.set_layouts = &fb->set_layout;
        if (impl->api.allocate_descriptor_sets(impl->device, &allocation,
                                               &fb->descriptor_set) != RF_VK_SUCCESS)
            goto failed;
        buffer_info.buffer = fb->output_buffer; buffer_info.offset = 0; buffer_info.range = fb->storage_size;
        memset(&write, 0, sizeof(write));
        write.s_type = RF_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dst_set = fb->descriptor_set; write.descriptor_count = 1;
        write.descriptor_type = RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.buffer_info = &buffer_info;
        impl->api.update_descriptor_sets(impl->device, 1, &write, 0, NULL);
    }
    {
        struct rf_vk_shader_module_create_info shader_info;
        struct rf_vk_pipeline_layout_create_info layout_info;
        struct rf_vk_compute_pipeline_create_info pipeline_info;
        memset(&shader_info, 0, sizeof(shader_info));
        shader_info.s_type = RF_VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shader_info.code_size = sizeof(framebuffer_spirv); shader_info.code = framebuffer_spirv;
        if (impl->api.create_shader_module(impl->device, &shader_info, NULL,
                                           &fb->shader) != RF_VK_SUCCESS) goto failed;
        memset(&layout_info, 0, sizeof(layout_info));
        layout_info.s_type = RF_VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.set_layout_count = 1; layout_info.set_layouts = &fb->set_layout;
        if (impl->api.create_pipeline_layout(impl->device, &layout_info, NULL,
                                             &fb->pipeline_layout) != RF_VK_SUCCESS) goto failed;
        memset(&pipeline_info, 0, sizeof(pipeline_info));
        pipeline_info.s_type = RF_VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeline_info.stage.s_type = RF_VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipeline_info.stage.stage = RF_VK_SHADER_STAGE_COMPUTE_BIT;
        pipeline_info.stage.module = fb->shader; pipeline_info.stage.name = "main";
        pipeline_info.layout = fb->pipeline_layout; pipeline_info.base_pipeline_index = -1;
        if (impl->api.create_compute_pipelines(impl->device, NULL, 1,
                &pipeline_info, NULL, &fb->pipeline) != RF_VK_SUCCESS) goto failed;
    }
    {
        struct rf_vk_command_pool_create_info pool_info;
        struct rf_vk_command_buffer_allocate_info allocation;
        struct rf_vk_command_buffer_begin_info begin;
        struct rf_vk_memory_barrier barrier;
        struct rf_vk_buffer_copy copy;
        memset(&pool_info, 0, sizeof(pool_info));
        pool_info.s_type = RF_VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.queue_family_index = impl->queue_family;
        if (impl->api.create_command_pool(impl->device, &pool_info, NULL,
                                          &fb->command_pool) != RF_VK_SUCCESS) goto failed;
        memset(&allocation, 0, sizeof(allocation));
        allocation.s_type = RF_VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocation.command_pool = fb->command_pool; allocation.level = RF_VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.command_buffer_count = 1;
        if (impl->api.allocate_command_buffers(impl->device, &allocation,
                                               &fb->command_buffer) != RF_VK_SUCCESS) goto failed;
        memset(&begin, 0, sizeof(begin)); begin.s_type = RF_VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (impl->api.begin_command_buffer(fb->command_buffer, &begin) != RF_VK_SUCCESS) goto failed;
        impl->api.cmd_bind_pipeline(fb->command_buffer, RF_VK_PIPELINE_BIND_POINT_COMPUTE, fb->pipeline);
        impl->api.cmd_bind_descriptor_sets(fb->command_buffer, RF_VK_PIPELINE_BIND_POINT_COMPUTE,
            fb->pipeline_layout, 0, 1, &fb->descriptor_set, 0, NULL);
        impl->api.cmd_dispatch(fb->command_buffer, fb->dispatch_x,
                               fb->dispatch_y, 1);
        memset(&barrier, 0, sizeof(barrier)); barrier.s_type = RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.src_access_mask = RF_VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dst_access_mask = RF_VK_ACCESS_TRANSFER_READ_BIT;
        impl->api.cmd_pipeline_barrier(fb->command_buffer,
            RF_VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, RF_VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 1, &barrier, 0, NULL, 0, NULL);
        copy.src_offset = 0; copy.dst_offset = 0; copy.size = fb->byte_size;
        impl->api.cmd_copy_buffer(fb->command_buffer, fb->output_buffer,
                                  fb->readback_buffer, 1, &copy);
        if (impl->api.end_command_buffer(fb->command_buffer) != RF_VK_SUCCESS) goto failed;
    }
    *framebuffer = fb;
    snprintf(message, message_capacity, "GPU framebuffer ready (%ux%u XRGB8888)", width, height);
    return 0;
failed:
    snprintf(message, message_capacity, "Vulkan framebuffer creation failed");
    framebuffer_destroy(context, fb);
    return -1;
}

static int framebuffer_render(void *context, void *framebuffer,
                              unsigned int *pixels, unsigned int width,
                              unsigned int height, unsigned int stride,
                              char *message, unsigned long message_capacity)
{
    struct rf_gpu_vulkan_context *backend_context = context;
    struct rf_gpu_vulkan_impl *impl = backend_context ? backend_context->implementation : NULL;
    struct rf_gpu_vulkan_framebuffer *fb = framebuffer;
    rf_vk_fence fence = NULL;
    void *mapped = NULL;
    rf_vk_result result;
    unsigned int y;
    if (!impl || !fb || fb->owner != impl || fb->width != width || fb->height != height)
        return -1;
    {
        struct rf_vk_fence_create_info fence_info;
        struct rf_vk_submit_info submit;
        memset(&fence_info, 0, sizeof(fence_info)); fence_info.s_type = RF_VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (impl->api.create_fence(impl->device, &fence_info, NULL, &fence) != RF_VK_SUCCESS) goto failed;
        memset(&submit, 0, sizeof(submit)); submit.s_type = RF_VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.command_buffer_count = 1; submit.command_buffers = &fb->command_buffer;
        result = impl->api.queue_submit(impl->queue, 1, &submit, fence);
        if (result != RF_VK_SUCCESS) goto failed;
        result = impl->api.wait_for_fences(impl->device, 1, &fence, RF_VK_TRUE, 5000000000ULL);
        if (result == RF_VK_TIMEOUT) {
            snprintf(message, message_capacity, "GPU framebuffer fence timed out after 5 seconds");
            goto cleanup;
        }
        if (result != RF_VK_SUCCESS) goto failed;
    }
    if (impl->api.map_memory(impl->device, fb->readback_memory, 0, RF_VK_WHOLE_SIZE,
                             0, &mapped) != RF_VK_SUCCESS) goto failed;
    if (!fb->readback_coherent) {
        struct rf_vk_mapped_memory_range range;
        memset(&range, 0, sizeof(range)); range.s_type = RF_VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = fb->readback_memory; range.offset = 0; range.size = RF_VK_WHOLE_SIZE;
        if (impl->api.invalidate_mapped_memory_ranges(impl->device, 1, &range) != RF_VK_SUCCESS)
            goto failed;
    }
    for (y = 0; y < height; ++y)
        memcpy(pixels + (uint64_t)y * stride,
               (const uint32_t *)mapped + (uint64_t)y * width,
               (size_t)width * sizeof(*pixels));
    impl->api.unmap_memory(impl->device, fb->readback_memory);
    impl->api.destroy_fence(impl->device, fence, NULL);
    snprintf(message, message_capacity, "GPU framebuffer rendered");
    return 0;
failed:
    snprintf(message, message_capacity, "Vulkan framebuffer operation failed");
cleanup:
    if (mapped) impl->api.unmap_memory(impl->device, fb->readback_memory);
    if (fence) impl->api.destroy_fence(impl->device, fence, NULL);
    return -1;
}

struct rf_gpu_vulkan_buffer {
    rf_vk_buffer buffer;
    rf_vk_device_memory memory;
    uint64_t size;
    uint64_t allocation_size;
    rf_vk_flags memory_flags;
};

static const char *present_fault_name(uint32_t fault)
{
    switch (fault) {
    case RF_GPU_PRESENT_FAULT_ACQUIRE_OUT_OF_DATE: return "acquire-out-of-date";
    case RF_GPU_PRESENT_FAULT_RECORD_FAILURE: return "record-failure";
    case RF_GPU_PRESENT_FAULT_SUBMIT_FAILURE: return "submit-failure";
    case RF_GPU_PRESENT_FAULT_PRESENT_OUT_OF_DATE: return "present-out-of-date";
    case RF_GPU_PRESENT_FAULT_PRESENT_SUBOPTIMAL: return "present-suboptimal";
    default: return "none";
    }
}

static int present_fault_take(struct rf_gpu_vulkan_impl *impl,
                              uint32_t fault)
{
    if (!impl || impl->present_fault_triggered ||
        impl->present_fault != fault ||
        impl->present_attempt != impl->present_fault_frame)
        return 0;
    impl->present_fault_triggered = 1;
    fprintf(stderr, "PRESENT-AUDIT fault-injection=%s frame=%llu\n",
        present_fault_name(fault),
        (unsigned long long)impl->present_attempt);
    return 1;
}

static int gpu_buffer_create(struct rf_gpu_vulkan_impl *impl,
                                uint64_t size, rf_vk_flags usage,
                                rf_vk_flags required, rf_vk_flags preferred,
                                struct rf_gpu_vulkan_buffer *out)
{
    struct rf_vk_buffer_create_info info;
    struct rf_vk_memory_requirements requirements;
    struct rf_vk_memory_allocate_info allocation;
    int memory_type;
    memset(out, 0, sizeof(*out));
    memset(&info, 0, sizeof(info));
    info.s_type = RF_VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size; info.usage = usage;
    info.sharing_mode = RF_VK_SHARING_MODE_EXCLUSIVE;
    if (impl->api.create_buffer(impl->device, &info, NULL, &out->buffer) !=
        RF_VK_SUCCESS) return -1;
    memset(&requirements, 0, sizeof(requirements));
    impl->api.get_buffer_memory_requirements(impl->device, out->buffer,
                                              &requirements);
    memory_type = framebuffer_memory_type(impl, requirements.memory_type_bits,
                                          required, preferred,
                                          &out->memory_flags);
    if (memory_type < 0) return -1;
    memset(&allocation, 0, sizeof(allocation));
    allocation.s_type = RF_VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocation_size = requirements.size;
    allocation.memory_type_index = (uint32_t)memory_type;
    struct rf_rt_allocate_flags address_flags = {1000060000,NULL,2,0};
    if (usage & RF_RT_ADDRESS_USAGE) allocation.next = &address_flags;
    if (impl->api.allocate_memory(impl->device, &allocation, NULL,
                                  &out->memory) != RF_VK_SUCCESS) return -1;
    if (impl->api.bind_buffer_memory(impl->device, out->buffer, out->memory, 0) !=
        RF_VK_SUCCESS) return -1;
    out->size = size;
    out->allocation_size = requirements.size;
    return 0;
}

static void gpu_buffer_destroy(struct rf_gpu_vulkan_impl *impl,
                                  struct rf_gpu_vulkan_buffer *buffer)
{
    if (buffer->buffer)
        impl->api.destroy_buffer(impl->device, buffer->buffer, NULL);
    if (buffer->memory)
        impl->api.free_memory(impl->device, buffer->memory, NULL);
    memset(buffer, 0, sizeof(*buffer));
}

static void presenter_swapchain_destroy(struct rf_gpu_vulkan_impl *impl)
{
    uint32_t i;
    /* Present completion is not covered by a slot's render fence.  Rebuild and
     * teardown retire the unique presenter before destroying its images. */
    if (impl->swapchain && impl->api.queue_wait_idle) {
        impl->api.queue_wait_idle(impl->queue);
        impl->recreate_queue_idle_count++;
    }
    if (impl->swapchain)
        impl->api.destroy_swapchain(impl->device, impl->swapchain, NULL);
    for (i = 0; impl->present_images && i < impl->swapchain_image_count; ++i)
        if (impl->present_images[i].render_finished)
            impl->api.destroy_semaphore(impl->device,
                impl->present_images[i].render_finished, NULL);
    free(impl->swapchain_images);
    free(impl->present_images);
    impl->swapchain = NULL; impl->swapchain_images = NULL;
    impl->present_images = NULL;
    impl->swapchain_image_count = 0;
    impl->outstanding_presents = 0;
}

static int presenter_swapchain_create(struct rf_gpu_vulkan_impl *impl,
                                      uint32_t width, uint32_t height)
{
    struct rf_vk_surface_capabilities caps;
    struct rf_vk_surface_format *formats = NULL;
    uint32_t *modes = NULL, format_count = 0, mode_count = 0, i;
    struct rf_vk_swapchain_create_info info;
    rf_vk_swapchain replacement = NULL;
    rf_vk_image *images = NULL;
    struct rf_gpu_vulkan_present_image *present_images = NULL;
    struct rf_vk_semaphore_create_info semaphore_info;
    uint32_t image_count, chosen_format = 0, chosen_mode = RF_VK_PRESENT_MODE_FIFO_KHR;
    struct rf_vk_extent2d extent;
    if (!impl->native_presentation_supported || !width || !height) return -1;
    if (impl->api.get_surface_capabilities(impl->physical_device, impl->surface,
            &caps) != RF_VK_SUCCESS ||
        !(caps.supported_usage_flags & RF_VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return -1;
    if (impl->api.get_surface_formats(impl->physical_device, impl->surface,
            &format_count, NULL) != RF_VK_SUCCESS || !format_count) return -1;
    formats = calloc(format_count, sizeof(*formats));
    if (!formats || impl->api.get_surface_formats(impl->physical_device,
            impl->surface, &format_count, formats) != RF_VK_SUCCESS) goto fail;
    for (i = 0; i < format_count; ++i)
        if (formats[i].format == RF_VK_FORMAT_B8G8R8A8_UNORM &&
            formats[i].color_space == RF_VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen_format = i + 1; break;
        }
    if (!chosen_format) goto fail;
    if (impl->api.get_surface_present_modes(impl->physical_device, impl->surface,
            &mode_count, NULL) != RF_VK_SUCCESS || !mode_count) goto fail;
    modes = calloc(mode_count, sizeof(*modes));
    if (!modes || impl->api.get_surface_present_modes(impl->physical_device,
            impl->surface, &mode_count, modes) != RF_VK_SUCCESS) goto fail;
    /* Keep FIFO for fixed-frame diagnostics.  Interactive frames use the
     * host's 120 Hz pacing instead of the display's 60 Hz refresh. */
    if (impl->prefer_high_rate_present) {
        for (i = 0; i < mode_count; ++i)
            if (modes[i] == RF_VK_PRESENT_MODE_MAILBOX_KHR)
                chosen_mode = modes[i];
        for (i = 0; i < mode_count; ++i)
            if (modes[i] == RF_VK_PRESENT_MODE_IMMEDIATE_KHR)
                chosen_mode = modes[i];
    }
    extent = caps.current_extent;
    if (extent.width == RF_VK_EXTENT_UNDEFINED) {
        extent.width = width < caps.min_image_extent.width ? caps.min_image_extent.width :
            (width > caps.max_image_extent.width ? caps.max_image_extent.width : width);
        extent.height = height < caps.min_image_extent.height ? caps.min_image_extent.height :
            (height > caps.max_image_extent.height ? caps.max_image_extent.height : height);
    }
    if (!extent.width || !extent.height) goto fail;
    image_count = caps.min_image_count + 1;
    if (caps.max_image_count && image_count > caps.max_image_count)
        image_count = caps.max_image_count;
    memset(&info, 0, sizeof(info));
    info.s_type = RF_VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = impl->surface; info.min_image_count = image_count;
    info.image_format = formats[chosen_format - 1].format;
    info.image_color_space = formats[chosen_format - 1].color_space;
    info.image_extent = extent; info.image_array_layers = 1;
    info.image_usage = RF_VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.image_sharing_mode = RF_VK_SHARING_MODE_EXCLUSIVE;
    info.pre_transform = caps.current_transform;
    info.composite_alpha = RF_VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.present_mode = chosen_mode; info.clipped = RF_VK_TRUE;
    info.old_swapchain = impl->swapchain;
    if (impl->api.create_swapchain(impl->device, &info, NULL, &replacement) !=
        RF_VK_SUCCESS) goto fail;
    image_count = 0;
    if (impl->api.get_swapchain_images(impl->device, replacement, &image_count,
            NULL) != RF_VK_SUCCESS || !image_count) goto fail;
    images = calloc(image_count, sizeof(*images));
    if (!images || impl->api.get_swapchain_images(impl->device, replacement,
            &image_count, images) != RF_VK_SUCCESS) goto fail;
    present_images = calloc(image_count, sizeof(*present_images));
    if (!present_images) goto fail;
    memset(&semaphore_info, 0, sizeof(semaphore_info));
    semaphore_info.s_type = RF_VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (i = 0; i < image_count; ++i)
        if (impl->api.create_semaphore(impl->device, &semaphore_info, NULL,
                &present_images[i].render_finished) != RF_VK_SUCCESS)
            goto fail;
    presenter_swapchain_destroy(impl);
    impl->swapchain = replacement; impl->swapchain_images = images;
    impl->present_images = present_images;
    impl->swapchain_image_count = image_count;
    impl->presenter_generation++;
    impl->presenter_poisoned = 0;
    for (i = 0; i < image_count; ++i) {
        impl->present_images[i].generation = impl->presenter_generation;
        impl->present_images[i].owner_slot = UINT32_MAX;
        impl->present_images[i].state = RF_GPU_PRESENT_AUDIT_REUSABLE;
        impl->present_images[i].render_finished_state =
            RF_GPU_PRESENT_AUDIT_REUSABLE;
    }
    impl->swapchain_width = extent.width; impl->swapchain_height = extent.height;
    impl->swapchain_format = info.image_format; impl->present_mode = chosen_mode;
    free(formats); free(modes); return 0;
fail:
    for (i = 0; present_images && i < image_count; ++i)
        if (present_images[i].render_finished)
            impl->api.destroy_semaphore(impl->device,
                present_images[i].render_finished, NULL);
    if (replacement) impl->api.destroy_swapchain(impl->device, replacement, NULL);
    free(present_images); free(images); free(formats); free(modes); return -1;
}

static int gpu_buffer_invalidate(struct rf_gpu_vulkan_impl *impl,
                             struct rf_gpu_vulkan_buffer *buffer)
{
    struct rf_vk_mapped_memory_range range;
    if (buffer->memory_flags & RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) return 0;
    memset(&range, 0, sizeof(range));
    range.s_type = RF_VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = buffer->memory; range.offset = 0;
    range.size = buffer->allocation_size;
    return impl->api.invalidate_mapped_memory_ranges(impl->device, 1, &range) ==
        RF_VK_SUCCESS ? 0 : -1;
}

#include "rf_gpu_vulkan_graphics.inc"

static unsigned int adapter_type(uint32_t type)
{
    switch (type) {
    case RF_VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return RF_GPU_ADAPTER_INTEGRATED;
    case RF_VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return RF_GPU_ADAPTER_DISCRETE;
    case RF_VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return RF_GPU_ADAPTER_VIRTUAL;
    case RF_VK_PHYSICAL_DEVICE_TYPE_CPU: return RF_GPU_ADAPTER_CPU;
    default: return RF_GPU_ADAPTER_OTHER;
    }
}

static void snapshot_capabilities(struct rf_vk_api *api,
                                  rf_vk_physical_device device,
                                  rf_vk_device logical_device,
                                  unsigned int adapter_index,
                                  unsigned int shader_int64_enabled,
                                  struct rf_gpu_capabilities *caps)
{
    struct rf_vk_physical_device_properties properties;
    struct rf_vk_physical_device_features features;
    struct rf_vk_physical_device_memory_properties memory;
    struct rf_vk_buffer_create_info buffer_info;
    struct rf_vk_memory_requirements output_requirements, readback_requirements;
    rf_vk_buffer output_buffer = NULL, readback_buffer = NULL;
    uint32_t i;
    memset(caps, 0, sizeof(*caps));
    memset(&properties, 0, sizeof(properties));
    memset(&features, 0, sizeof(features));
    memset(&memory, 0, sizeof(memory));
    api->get_physical_device_properties(device, &properties);
    api->get_physical_device_features(device, &features);
    api->get_memory_properties(device, &memory);
    memset(&buffer_info, 0, sizeof(buffer_info));
    memset(&output_requirements, 0, sizeof(output_requirements));
    memset(&readback_requirements, 0, sizeof(readback_requirements));
    buffer_info.s_type = RF_VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = 4;
    buffer_info.sharing_mode = RF_VK_SHARING_MODE_EXCLUSIVE;
    buffer_info.usage = RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                        RF_VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (api->create_buffer(logical_device, &buffer_info, NULL, &output_buffer) ==
        RF_VK_SUCCESS)
        api->get_buffer_memory_requirements(logical_device, output_buffer,
                                            &output_requirements);
    buffer_info.usage = RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (api->create_buffer(logical_device, &buffer_info, NULL, &readback_buffer) ==
        RF_VK_SUCCESS)
        api->get_buffer_memory_requirements(logical_device, readback_buffer,
                                            &readback_requirements);
    caps->api_version = properties.api_version;
    caps->adapter_index = adapter_index;
    caps->compute_queue = 1;
    caps->max_compute_work_group_invocations =
        properties.limits.max_compute_work_group_invocations;
    memcpy(caps->max_compute_work_group_size,
           properties.limits.max_compute_work_group_size,
           sizeof(caps->max_compute_work_group_size));
    memcpy(caps->max_compute_work_group_count,
           properties.limits.max_compute_work_group_count,
           sizeof(caps->max_compute_work_group_count));
    caps->max_storage_buffer_range = properties.limits.max_storage_buffer_range;
    caps->min_storage_buffer_offset_alignment =
        properties.limits.min_storage_buffer_offset_alignment;
    caps->non_coherent_atom_size = properties.limits.non_coherent_atom_size;
    /* Raster capability is a logical-device fact.  Advertised support alone
     * is insufficient if device creation policy did not enable the feature. */
    caps->shader_int64 = features.shader_int64 && shader_int64_enabled;
    caps->memory_heap_count = memory.memory_heap_count;
    if (caps->memory_heap_count > RF_GPU_MAX_MEMORY_HEAPS)
        caps->memory_heap_count = RF_GPU_MAX_MEMORY_HEAPS;
    for (i = 0; i < caps->memory_heap_count; ++i) {
        caps->memory_heaps[i].size = memory.memory_heaps[i].size;
        caps->memory_heaps[i].property_flags = memory.memory_heaps[i].flags;
    }
    caps->memory_type_count = memory.memory_type_count;
    if (caps->memory_type_count > RF_GPU_MAX_MEMORY_TYPES)
        caps->memory_type_count = RF_GPU_MAX_MEMORY_TYPES;
    for (i = 0; i < caps->memory_type_count; ++i) {
        rf_vk_flags flags = memory.memory_types[i].property_flags;
        caps->memory_types[i].property_flags = flags;
        caps->memory_types[i].heap_index = memory.memory_types[i].heap_index;
        if ((output_requirements.memory_type_bits & (1U << i)) &&
            (flags & RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            caps->device_local_output_memory = 1;
        if ((readback_requirements.memory_type_bits & (1U << i)) &&
            (flags & RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            caps->host_visible_readback_memory = 1;
            if (flags & RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                caps->coherent_readback = 1;
            else
                caps->non_coherent_readback = 1;
        }
    }
    if (output_buffer) api->destroy_buffer(logical_device, output_buffer, NULL);
    if (readback_buffer)
        api->destroy_buffer(logical_device, readback_buffer, NULL);
}

static void backend_cleanup(struct rf_gpu_vulkan_impl *impl)
{
    if (!impl) return;
    if (impl->device) presenter_swapchain_destroy(impl);
    pipeline_cache_save(impl);
    if (impl->pipeline_cache)
        impl->api.destroy_pipeline_cache(impl->device,impl->pipeline_cache,NULL);
    if (impl->device && impl->api.destroy_device)
        impl->api.destroy_device(impl->device, NULL);
    if (impl->surface && impl->api.destroy_surface)
        impl->api.destroy_surface(impl->instance, impl->surface, NULL);
    if (impl->instance && impl->api.destroy_instance)
        impl->api.destroy_instance(impl->instance, NULL);
    api_close(&impl->api);
    free(impl);
}

/* Capability policy is fixed for the device lifetime; never silently replace a
 * requested hardware run with software when collecting comparative evidence. */
static int ray_query_support(struct rf_gpu_vulkan_impl *p,rf_vk_physical_device device,
                             uint32_t instance_version)
{
    struct rf_vk_api *api=&p->api;
    const char *mode=getenv("RF_GPU_ARCHITECTURE");
    int required=mode && !strcmp(mode,"hardware"),supported=0;
    if(mode && strcmp(mode,"auto") && strcmp(mode,"software") && !required) {
        fprintf(stderr,"rf-gpu-ray: invalid RF_GPU_ARCHITECTURE (auto/software/hardware)\n");return -1;
    }
    if(mode && !strcmp(mode,"software"))goto done;
    struct rf_vk_physical_device_properties props={0};
    api->get_physical_device_properties(device,&props);
    if(instance_version<RF_VK_MAKE_VERSION(1,2,0) || props.api_version<RF_VK_MAKE_VERSION(1,2,0))goto done;
    RF_LOAD(p->rt.extensions,load_instance,p->instance,"vkEnumerateDeviceExtensionProperties");
    RF_LOAD(p->rt.features,load_instance,p->instance,"vkGetPhysicalDeviceFeatures2");
    RF_LOAD(p->rt.properties,load_instance,p->instance,"vkGetPhysicalDeviceProperties2");
    uint32_t count=0,found=0;
    if(p->rt.extensions(device,NULL,&count,NULL)!=RF_VK_SUCCESS) return -1;
    struct rf_rt_extensions *ext=calloc(count,sizeof(*ext));
    if(!ext)return -1;
    rf_vk_result result=p->rt.extensions(device,NULL,&count,ext);
    if(result==RF_VK_SUCCESS)for(uint32_t i=0;i<count;++i) {
        if(!strcmp(ext[i].name,"VK_KHR_acceleration_structure"))found|=1;
        if(!strcmp(ext[i].name,"VK_KHR_ray_query"))found|=2;
        if(!strcmp(ext[i].name,"VK_KHR_deferred_host_operations"))found|=4;
    }
    free(ext);
    if(result!=RF_VK_SUCCESS)return -1;
    if(found!=7)goto done;
    struct rf_rt_query_features query={1000348013,NULL,0};
    struct rf_rt_as_features acceleration={1000150013,&query,0,0,0,0,0};
    struct rf_rt_address_features address={1000257000,&acceleration,0,0,0};
    struct rf_rt_features2 features={0};features.s_type=1000059000;features.next=&address;
    p->rt.features(device,&features);
    p->rt_properties.s_type=1000150014;
    struct rf_rt_properties2 properties={0};properties.s_type=1000059001;properties.next=&p->rt_properties;
    p->rt.properties(device,&properties);
    supported=query.enabled && acceleration.enabled && address.enabled &&
        p->rt_properties.scratch_alignment && p->rt_properties.per_stage && p->rt_properties.set_count;
done:
    p->ray_query_enabled=supported;
    fprintf(stderr,"rf-gpu-ray: requested=%s selected=%s\n",mode?mode:"auto",supported?"hardware":"software");
    if(required && !supported){fprintf(stderr,"rf-gpu-ray: required hardware ray query unavailable\n");return -1;}
    return 0;
}

static int ray_query_load(struct rf_gpu_vulkan_impl *p)
{
    if(!p->ray_query_enabled)return 0;
    struct rf_vk_api *api=&p->api;
    RF_LOAD(p->rt.buffer_address,load_instance,p->instance,"vkGetBufferDeviceAddress");
    RF_LOAD(p->rt.as_address,load_instance,p->instance,"vkGetAccelerationStructureDeviceAddressKHR");
    RF_LOAD(p->rt.sizes,load_instance,p->instance,"vkGetAccelerationStructureBuildSizesKHR");
    RF_LOAD(p->rt.create,load_instance,p->instance,"vkCreateAccelerationStructureKHR");
    RF_LOAD(p->rt.destroy,load_instance,p->instance,"vkDestroyAccelerationStructureKHR");
    RF_LOAD(p->rt.build,load_instance,p->instance,"vkCmdBuildAccelerationStructuresKHR");
    fprintf(stderr,"rf-gpu-ray: enabled rayQuery+accelerationStructure+bufferDeviceAddress scratch-alignment=%u\n",
            p->rt_properties.scratch_alignment);
    return 0;
}

static int backend_init(void *context, struct rf_gpu_backend_info *info,
                        char *message, unsigned long message_capacity)
{
    struct rf_gpu_vulkan_context *backend_context = context;
    struct rf_gpu_vulkan_impl *impl = NULL;
    struct rf_vk_api *api;
    struct rf_vk_application_info app_info;
    struct rf_vk_instance_create_info instance_info;
    rf_vk_physical_device *devices = NULL;
    uint32_t loader_version = RF_VK_MAKE_VERSION(1, 0, 0);
    uint32_t device_count = 0;
    rf_vk_result result;
    int backend_result = RF_GPU_BACKEND_FAILED;
    rf_vk_physical_device selected_device = NULL;
    uint32_t selected_family = 0;
    uint32_t selected_type = RF_VK_PHYSICAL_DEVICE_TYPE_OTHER;
    uint32_t selected_index = 0;
    const char *vendor_filter = getenv("RF_GPU_VULKAN_VENDOR_ID");
    unsigned long required_vendor = 0;
    char selected_name[RF_VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = {0};
    uint32_t i;
    int want_present = backend_context &&
        backend_context->native_window.type == 1 &&
        backend_context->native_window.window &&
        backend_context->native_window.instance;

    if (!backend_context || !info || backend_context->implementation) {
        snprintf(message, message_capacity, "invalid Vulkan backend context");
        return RF_GPU_BACKEND_FAILED;
    }
    if (vendor_filter && *vendor_filter) {
        char *end = NULL;
        required_vendor = strtoul(vendor_filter, &end, 16);
        if (!end || *end || !required_vendor || required_vendor > 0xffff) {
            snprintf(message, message_capacity, "invalid RF_GPU_VULKAN_VENDOR_ID (hex vendor required)");
            return RF_GPU_BACKEND_FAILED;
        }
    }
    impl = calloc(1, sizeof(*impl));
    if (!impl) {
        snprintf(message, message_capacity, "out of memory creating Vulkan backend");
        return RF_GPU_BACKEND_FAILED;
    }
    impl->present_fault = backend_context->present_fault;
    impl->prefer_high_rate_present =
        backend_context->prefer_high_rate_present != 0;
    impl->present_fault_frame = backend_context->present_fault_frame ?
        backend_context->present_fault_frame : 1;
    api = &impl->api;
    if (api_open(api) < 0) {
        snprintf(message, message_capacity, "Vulkan loader unavailable");
        backend_result = RF_GPU_BACKEND_UNAVAILABLE;
        goto done;
    }
    if (api->enumerate_instance_version) {
        result = api->enumerate_instance_version(&loader_version);
        if (result != RF_VK_SUCCESS) {
            fprintf(stderr, "rf-gpu-probe: version query failed (%d)\n", result);
            goto done;
        }
    }
    memset(&app_info, 0, sizeof(app_info));
    app_info.s_type = RF_VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.application_name = "rf-gpu-probe";
    app_info.application_version = RF_VK_MAKE_VERSION(0, 1, 0);
    app_info.engine_name = "Rasterfall";
    app_info.engine_version = RF_VK_MAKE_VERSION(0, 1, 0);
    app_info.api_version = loader_version>=RF_VK_MAKE_VERSION(1,2,0) ?
        RF_VK_MAKE_VERSION(1,2,0) : RF_VK_MAKE_VERSION(1,0,0);
    memset(&instance_info, 0, sizeof(instance_info));
    instance_info.s_type = RF_VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.application_info = &app_info;
    if (want_present) {
        static const char *extensions[] = {
            "VK_KHR_surface", "VK_KHR_win32_surface"
        };
        instance_info.enabled_extension_count = 2;
        instance_info.enabled_extension_names = extensions;
    }
    result = api->create_instance(&instance_info, NULL, &impl->instance);
    if ((result != RF_VK_SUCCESS || !impl->instance) && want_present) {
        /* Presentation is an independent optional capability.  Retry the
         * persistent compute service without WSI if the extensions are absent. */
        want_present = 0;
        instance_info.enabled_extension_count = 0;
        instance_info.enabled_extension_names = NULL;
        impl->instance = NULL;
        result = api->create_instance(&instance_info, NULL, &impl->instance);
    }
    if (result != RF_VK_SUCCESS || !impl->instance) {
        fprintf(stderr, "rf-gpu-probe: vkCreateInstance failed (%d)\n", result);
        goto done;
    }
    if (api_load_instance(api, impl->instance) < 0) goto done;
    if (want_present) {
        struct rf_vk_win32_surface_create_info surface_info;
        if (!api->create_win32_surface || !api->destroy_surface ||
            !api->get_surface_support || !api->get_surface_capabilities ||
            !api->get_surface_formats || !api->get_surface_present_modes)
            want_present = 0;
        else {
            memset(&surface_info, 0, sizeof(surface_info));
            surface_info.s_type = RF_VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
            surface_info.instance = (void *)(uintptr_t)
                backend_context->native_window.instance;
            surface_info.window = (void *)(uintptr_t)
                backend_context->native_window.window;
            if (api->create_win32_surface(impl->instance, &surface_info, NULL,
                                          &impl->surface) != RF_VK_SUCCESS)
                want_present = 0;
        }
    }

    result = api->enumerate_physical_devices(impl->instance, &device_count, NULL);
    if (result != RF_VK_SUCCESS || !device_count) {
        fprintf(stderr, "rf-gpu-probe: no Vulkan physical device (%d)\n",
                result);
        if (result == RF_VK_SUCCESS)
            backend_result = RF_GPU_BACKEND_UNAVAILABLE;
        goto done;
    }
    devices = calloc(device_count, sizeof(*devices));
    if (!devices) {
        fprintf(stderr, "rf-gpu-probe: out of memory\n");
        goto done;
    }
    result = api->enumerate_physical_devices(impl->instance, &device_count, devices);
    if (result != RF_VK_SUCCESS && result != RF_VK_INCOMPLETE) {
        fprintf(stderr, "rf-gpu-probe: device enumeration failed (%d)\n",
                result);
        goto done;
    }
    for (i = 0; i < device_count; ++i) {
        union {
            uint64_t alignment;
            unsigned char bytes[
                RF_VK_PHYSICAL_DEVICE_PROPERTIES_STORAGE_SIZE];
        } property_storage;
        struct rf_vk_physical_device_properties *properties =
            (struct rf_vk_physical_device_properties *)
                property_storage.bytes;
        struct rf_vk_queue_family_properties *families = NULL;
        uint32_t family_count = 0;
        int selected_queue;

        memset(&property_storage, 0, sizeof(property_storage));
        api->get_physical_device_properties(devices[i], property_storage.bytes);
        fprintf(stderr, "rf-gpu-device: index=%u vendor=%04x device=%04x driver=%u api=%u name=%s\n",
            i, properties->vendor_id, properties->device_id,
            properties->driver_version, properties->api_version, properties->device_name);
        if (required_vendor && properties->vendor_id != required_vendor) continue;
        api->get_physical_device_queue_family_properties(devices[i],
                                                         &family_count, NULL);
        if (family_count)
            families = calloc(family_count, sizeof(*families));
        if (family_count && !families) {
            fprintf(stderr, "rf-gpu-probe: out of memory\n");
            goto done;
        }
        if (families)
            api->get_physical_device_queue_family_properties(
                devices[i], &family_count, families);
        selected_queue = choose_queue(families, family_count,
            RF_VK_QUEUE_COMPUTE_BIT | (backend_context->require_graphics ? 1U : 0U));
        if (selected_queue >= 0 &&
            (!selected_device ||
             (selected_type != RF_VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
              properties->device_type ==
                  RF_VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU))) {
            selected_device = devices[i];
            selected_family = (uint32_t)selected_queue;
            impl->queue_flags = families[selected_queue].queue_flags;
            selected_type = properties->device_type;
            selected_index = i;
            snprintf(selected_name, sizeof(selected_name), "%s",
                     properties->device_name);
            info->vendor_id = properties->vendor_id;
            info->device_id = properties->device_id;
        }
        free(families);
    }
    if (!selected_device) {
        if (required_vendor)
            fprintf(stderr, "rf-gpu-device: required vendor=%04lx unavailable; no device substitution\n", required_vendor);
        fprintf(stderr, "rf-gpu-probe: no %s queue family\n",
            backend_context->require_graphics ? "graphics+compute" : "compute-capable");
        backend_result = RF_GPU_BACKEND_UNAVAILABLE;
        goto done;
    }
    if (want_present) {
        rf_vk_bool32 supported = 0;
        if (api->get_surface_support(selected_device, selected_family,
                                     impl->surface, &supported) != RF_VK_SUCCESS ||
            !supported)
            want_present = 0;
    }
    if(ray_query_support(impl,selected_device,app_info.api_version)<0)goto done;
    {
        float priority = 1.0f;
        struct rf_vk_device_queue_create_info queue_info;
        struct rf_vk_device_create_info device_info;
        struct rf_vk_physical_device_features supported_features;
        struct rf_vk_physical_device_features enabled_features;
        memset(&queue_info, 0, sizeof(queue_info));
        queue_info.s_type = RF_VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queue_family_index = selected_family;
        queue_info.queue_count = 1;
        queue_info.queue_priorities = &priority;
        memset(&device_info, 0, sizeof(device_info));
        memset(&supported_features, 0, sizeof(supported_features));
        memset(&enabled_features, 0, sizeof(enabled_features));
        api->get_physical_device_features(selected_device, &supported_features);
        enabled_features.shader_int64 = supported_features.shader_int64;
        const char *light_profile=getenv("RF_GPU_LIGHT_PROFILE");
        impl->light_profile_enabled=light_profile && !strcmp(light_profile,"1");
        if(impl->light_profile_enabled) {
            if(!supported_features.fragment_stores_and_atomics) {
                fprintf(stderr,"rf-gpu-light: profiling requires fragmentStoresAndAtomics\n");goto done;
            }
            enabled_features.fragment_stores_and_atomics=RF_VK_TRUE;
        }
        device_info.s_type = RF_VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_info.queue_create_info_count = 1;
        device_info.queue_create_infos = &queue_info;
        device_info.enabled_features = &enabled_features;
        struct rf_rt_query_features query={1000348013,NULL,1};
        struct rf_rt_as_features acceleration={1000150013,&query,1,0,0,0,0};
        struct rf_rt_address_features address={1000257000,&acceleration,1,0,0};
        const char *extensions[4];uint32_t extension_count=0;
        if(impl->ray_query_enabled) {
            extensions[extension_count++]="VK_KHR_acceleration_structure";
            extensions[extension_count++]="VK_KHR_ray_query";
            extensions[extension_count++]="VK_KHR_deferred_host_operations";
            device_info.next=&address;
        }
        if(want_present)extensions[extension_count++]="VK_KHR_swapchain";
        device_info.enabled_extension_count=extension_count;
        device_info.enabled_extension_names=extensions;
        result = api->create_device(selected_device, &device_info, NULL,
                                    &impl->device);
        if ((result != RF_VK_SUCCESS || !impl->device) && want_present) {
            want_present = 0;
            device_info.enabled_extension_count = --extension_count;
            impl->device = NULL;
            result = api->create_device(selected_device, &device_info, NULL,
                                        &impl->device);
        }
        if (result != RF_VK_SUCCESS || !impl->device) goto done;
        if(ray_query_load(impl)<0)goto done;
        api->get_device_queue(impl->device, selected_family, 0, &impl->queue);
        if (!impl->queue) goto done;
        impl->shader_int64_enabled = enabled_features.shader_int64 != 0;
    }
    impl->physical_device = selected_device;
    impl->queue_family = selected_family;
    {
        struct rf_vk_physical_device_properties properties;
        struct rf_vk_queue_family_properties *families=NULL;
        uint32_t family_count=0;
        memset(&properties,0,sizeof(properties));
        api->get_physical_device_properties(selected_device,&properties);
        api->get_physical_device_queue_family_properties(selected_device,&family_count,NULL);
        if (family_count) families=calloc(family_count,sizeof(*families));
        if (families) api->get_physical_device_queue_family_properties(
            selected_device,&family_count,families);
        impl->timestamp_period=properties.limits.timestamp_period;
        impl->non_coherent_atom_size=properties.limits.non_coherent_atom_size;
        impl->timestamp_valid_bits=families && selected_family<family_count ?
            families[selected_family].timestamp_valid_bits : 0;
        impl->timestamp_supported=properties.limits.timestamp_compute_and_graphics &&
            impl->timestamp_valid_bits && api->create_query_pool &&
            api->destroy_query_pool && api->cmd_reset_query_pool &&
            api->cmd_write_timestamp && api->get_query_pool_results;
        free(families);
    }
    if (device_smoke(api, selected_device, impl->device, impl->queue,
                     selected_family) < 0)
        goto done;
    snprintf(info->adapter_name, sizeof(info->adapter_name), "%s", selected_name);
    info->adapter_type = adapter_type(selected_type);
    info->queue_family = selected_family;
    snapshot_capabilities(api, selected_device, impl->device, selected_index,
                          impl->shader_int64_enabled,
                          &info->capabilities);
    fprintf(stderr,"rf-gpu-device: selected index=%u queue-family=%u name=%s\n",
        selected_index,selected_family,info->adapter_name);
    impl->max_storage_buffer_range =
        info->capabilities.max_storage_buffer_range;
    impl->native_presentation_supported = want_present && impl->surface &&
        api->create_swapchain && api->destroy_swapchain &&
        api->get_swapchain_images && api->acquire_next_image &&
        api->queue_present && api->queue_wait_idle && api->create_semaphore &&
        api->destroy_semaphore && api->cmd_copy_buffer_to_image;
    info->capabilities.native_presentation_v1 =
        impl->native_presentation_supported != 0;
    snprintf(message, message_capacity, "Vulkan ready; compute/readback passed");
    backend_context->implementation = impl;
    impl = NULL;
    backend_result = RF_GPU_BACKEND_READY;

done:
    free(devices);
    if (impl) backend_cleanup(impl);
    if (backend_result == RF_GPU_BACKEND_FAILED && message && !message[0])
        snprintf(message, message_capacity, "Vulkan backend initialization failed");
    return backend_result;
}

static void backend_shutdown(void *context)
{
    struct rf_gpu_vulkan_context *backend_context = context;
    if (!backend_context) return;
    backend_cleanup(backend_context->implementation);
    backend_context->implementation = NULL;
}

static int backend_set_native_window(void *context,
                                     const struct rf_gpu_native_window *window)
{
    struct rf_gpu_vulkan_context *backend_context = context;
    if (!backend_context || backend_context->implementation) return -1;
    memset(&backend_context->native_window, 0,
           sizeof(backend_context->native_window));
    if (window) backend_context->native_window = *window;
    return 0;
}

const struct rf_gpu_backend rf_gpu_vulkan_backend = {
    .init = backend_init,
    .shutdown = backend_shutdown,
    .framebuffer_create = framebuffer_create,
    .framebuffer_destroy = framebuffer_destroy,
    .framebuffer_render = framebuffer_render,
    .set_native_window = backend_set_native_window
};
