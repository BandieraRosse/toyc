/* Hosted, SDK-free persistent Vulkan backend for the RF Core GPU service. */

#include "rf_vulkan_min.h"
#include "rf_gpu.h"
#include "rf_gpu_raster_bin.h"
#include "rf_gpu_vulkan_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Kept opaque here because the hosted Vulkan translation unit uses the host
 * stdint ABI while GPU-4's public header uses Tinylibc fixed-width aliases. */
int rf_gpu_raster_validate_v1(const void *stream, size_t stream_size);

struct rf_gpu_texture_desc_host_v1 {
    uint32_t texel_offset, width, height, stride, format, sampling;
};
#define RF_GPU_TEXTURE_FORMAT_RGB8_HOST_V1 1U
#define RF_GPU_TEXTURE_FORMAT_RGBA8_HOST_V1 2U
#define RF_GPU_TEXTURE_SAMPLING_NEAREST_HOST_V1 1U

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

struct rf_gpu_vulkan_impl {
    struct rf_vk_api api;
    rf_vk_instance instance;
    rf_vk_physical_device physical_device;
    rf_vk_device device;
    rf_vk_queue queue;
    uint32_t queue_family;
    uint32_t queue_flags;
    uint32_t shader_int64_enabled;
    uint64_t max_storage_buffer_range;
    float timestamp_period;
    uint32_t timestamp_valid_bits;
    int timestamp_supported;
    rf_vk_surface surface;
    int native_presentation_supported;
    rf_vk_swapchain swapchain;
    rf_vk_image *swapchain_images;
    uint32_t swapchain_image_count, swapchain_width, swapchain_height;
    uint32_t swapchain_format, present_mode;
    struct rf_gpu_vulkan_present_image *present_images;
    uint64_t presenter_generation, next_present_generation, next_frame_number;
    uint64_t mixed_cpu_frame, mixed_completed_frame;
    uint64_t hot_queue_idle_count, recreate_queue_idle_count;
    uint32_t outstanding_presents;
    int presenter_poisoned;
    uint32_t next_slot_id;
    uint32_t present_fault, present_fault_frame;
    uint64_t present_attempt;
    int present_fault_triggered;
};

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

#include "rf_gpu_raster_v1_spirv.inc"
#include "rf_gpu_raster_v1_full_spirv.inc"
#include "rf_gpu_raster_v1_image_spirv.inc"
#include "rf_gpu_overlay_spirv.inc"
#include "rf_gpu_post_spirv.inc"
#include "rf_gpu_post_image_spirv.inc"

struct rf_gpu_vulkan_raster_buffer {
    rf_vk_buffer buffer;
    rf_vk_device_memory memory;
    uint64_t size;
    uint64_t allocation_size;
    rf_vk_flags memory_flags;
};

#define RF_GPU_RASTER_DESCRIPTOR_SET_COUNT 32

/* Raw segmented callers may change input between recordings. Keep each
 * uploaded version alive until all commands referencing it have completed. */
struct rf_gpu_raster_input_version {
    struct rf_gpu_vulkan_raster_buffer command, tile_offsets, tile_indices;
    struct rf_gpu_vulkan_raster_buffer texture_descs, texture_texels;
    struct rf_gpu_raster_input_version *next;
};

struct rf_gpu_vulkan_raster {
    struct rf_gpu_vulkan_impl *owner;
    struct rf_gpu_vulkan_raster_buffer color;
    struct rf_gpu_vulkan_raster_buffer depth;
    struct rf_gpu_vulkan_raster_buffer command;
    struct rf_gpu_vulkan_raster_buffer tile_offsets;
    struct rf_gpu_vulkan_raster_buffer tile_indices;
    struct rf_gpu_vulkan_raster_buffer texture_descs;
    struct rf_gpu_vulkan_raster_buffer texture_texels;
    struct rf_gpu_vulkan_raster_buffer viewmodel_coverage;
    struct rf_gpu_vulkan_raster_buffer overlay_color;
    struct rf_gpu_vulkan_raster_buffer overlay_coverage;
    struct rf_gpu_vulkan_raster_buffer post_color;
    struct rf_gpu_vulkan_raster_buffer post_params;
    struct rf_gpu_vulkan_raster_buffer color_readback;
    struct rf_gpu_vulkan_raster_buffer depth_readback;
    rf_vk_descriptor_set_layout set_layout;
    rf_vk_descriptor_pool descriptor_pool;
    rf_vk_descriptor_set descriptor_sets[RF_GPU_RASTER_DESCRIPTOR_SET_COUNT];
    uint32_t descriptor_set_cursor;
    rf_vk_shader_module shader;
    rf_vk_shader_module full_scan_shader;
    rf_vk_shader_module image_shader;
    rf_vk_shader_module full_scan_image_shader;
    rf_vk_shader_module overlay_shader;
    rf_vk_shader_module post_shader;
    rf_vk_shader_module post_image_shader;
    rf_vk_pipeline_layout pipeline_layout;
    rf_vk_pipeline pipeline;
    rf_vk_pipeline full_scan_pipeline;
    rf_vk_pipeline image_pipeline;
    rf_vk_pipeline full_scan_image_pipeline;
    rf_vk_pipeline overlay_pipeline;
    rf_vk_pipeline post_pipeline;
    rf_vk_pipeline post_image_pipeline;
    rf_vk_command_pool command_pool;
    rf_vk_command_buffer command_buffer;
    rf_vk_fence render_fence;
    int render_in_flight;
    uint32_t width, height;
    rf_vk_image shared_color_image;
    rf_vk_image_view shared_color_view;
    struct rf_gpu_graphics *shared_color_owner;
    struct rf_gpu_vulkan_raster *shared_color_next;
    int shared_color_enabled, shared_color_layout_valid;
    uint32_t work_group_x, work_group_y;
    int full_scan_diagnostic;
    int segment_valid;
    int segment_graphics_compatible;
    int frame_recording;
    struct rf_gpu_raster_tile_lists tile_lists;
    const void *binned_stream;
    unsigned long binned_stream_size;
    const void *uploaded_stream;
    unsigned long uploaded_stream_size;
    int preflight_reuse;
    struct rf_gpu_raster_input_version *input_versions;
    rf_vk_semaphore acquire_semaphore;
    uint32_t audit_slot_id;
    uint32_t acquire_semaphore_state;
    uint64_t slot_generation, submitted_frame;
    struct rf_gpu_native_present_timing *pending_present_timing;
    const uint32_t *pending_overlay_color;
    const unsigned char *pending_overlay_coverage;
    uint32_t pending_overlay_stride, pending_coverage_stride;
    uint32_t *pending_capture_color;
    struct rf_gpu_post_params_v1 post;
    rf_vk_query_pool timestamp_pool;
    uint32_t timestamp_count;
    uint32_t timestamp_capacity;
    unsigned char *timestamp_category;
    uint64_t *timestamp_values;
    struct rf_gpu_mixed_gpu_timing gpu_timing;
};

static void gfx_raster_unshare(struct rf_gpu_vulkan_raster *r);

static int present_audit_fail(const char *invariant)
{
    fprintf(stderr, "PRESENT-AUDIT invariant-failure=%s\n", invariant);
    return -1;
}

static const char *present_fault_name(uint32_t fault)
{
    switch (fault) {
    case RF_GPU_PRESENT_FAULT_ACQUIRE_OUT_OF_DATE:
        return "acquire-out-of-date";
    case RF_GPU_PRESENT_FAULT_RECORD_FAILURE: return "record-failure";
    case RF_GPU_PRESENT_FAULT_SUBMIT_FAILURE: return "submit-failure";
    case RF_GPU_PRESENT_FAULT_PRESENT_OUT_OF_DATE:
        return "present-out-of-date";
    case RF_GPU_PRESENT_FAULT_PRESENT_SUBOPTIMAL:
        return "present-suboptimal";
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

static void present_audit_poison(struct rf_gpu_vulkan_impl *impl,
    struct rf_gpu_vulkan_raster *r, uint32_t image, int image_valid)
{
    if (!impl || !r) return;
    impl->presenter_poisoned = 1;
    if (r->acquire_semaphore_state != RF_GPU_PRESENT_AUDIT_REUSABLE)
        r->acquire_semaphore_state = RF_GPU_PRESENT_AUDIT_POISONED;
    if (image_valid && impl->present_images &&
        image < impl->swapchain_image_count) {
        impl->present_images[image].state = RF_GPU_PRESENT_AUDIT_POISONED;
        if (impl->present_images[image].render_finished_state !=
                RF_GPU_PRESENT_AUDIT_REUSABLE)
            impl->present_images[image].render_finished_state =
                RF_GPU_PRESENT_AUDIT_POISONED;
    }
}

static void present_audit_snapshot(struct rf_gpu_vulkan_impl *impl,
    struct rf_gpu_vulkan_raster *r, uint32_t image,
    struct rf_gpu_native_present_timing *timing, uint32_t completion_source)
{
    struct rf_gpu_vulkan_present_image *pi = NULL;
    if (!impl || !r || !timing) return;
    if (impl->present_images && image < impl->swapchain_image_count)
        pi = &impl->present_images[image];
    timing->audit_frame = r->submitted_frame;
    timing->audit_swapchain_generation = impl->presenter_generation;
    timing->audit_slot_generation = r->slot_generation;
    timing->audit_image_generation = pi ? pi->generation : 0;
    timing->audit_acquire_generation = pi ? pi->acquire_generation : 0;
    timing->audit_submit_generation = pi ? pi->submit_generation : 0;
    timing->audit_present_generation = pi ? pi->present_generation : 0;
    timing->audit_retire_generation = pi ? pi->retire_generation : 0;
    timing->audit_hot_queue_idle_count = impl->hot_queue_idle_count;
    timing->audit_recreate_queue_idle_count = impl->recreate_queue_idle_count;
    timing->audit_slot = r->audit_slot_id;
    timing->audit_image = image;
    timing->audit_image_owner_slot = pi ? pi->owner_slot : UINT32_MAX;
    timing->audit_slot_fence_state = r->render_in_flight ?
        RF_GPU_PRESENT_AUDIT_PENDING : RF_GPU_PRESENT_AUDIT_RETIRED;
    timing->audit_acquire_semaphore_state = r->acquire_semaphore_state;
    timing->audit_image_state = pi ? pi->state : RF_GPU_PRESENT_AUDIT_POISONED;
    timing->audit_render_finished_semaphore_state = pi ?
        pi->render_finished_state : RF_GPU_PRESENT_AUDIT_POISONED;
    timing->audit_outstanding_presents = impl->outstanding_presents;
    timing->audit_presenter_poisoned = impl->presenter_poisoned != 0;
    timing->audit_completion_source = completion_source;
}

enum { RF_GPU_TS_RASTER, RF_GPU_TS_IMPORT, RF_GPU_TS_DRAW,
       RF_GPU_TS_EXPORT, RF_GPU_TS_POST, RF_GPU_TS_OVERLAY,
       RF_GPU_TS_PRESENT_COPY };

void rf_gpu_vulkan_measure_frame(struct rf_gpu_vulkan_context *context,
    uint64_t frame)
{
    struct rf_gpu_vulkan_impl *impl = context ? context->implementation : NULL;
    if (impl) impl->mixed_cpu_frame = frame;
}

static uint32_t timestamp_begin(struct rf_gpu_vulkan_raster *r,
    rf_vk_command_buffer command, unsigned int category)
{
    struct rf_gpu_vulkan_impl *impl = r ? r->owner : NULL;
    uint32_t first;
    if (!impl) return UINT32_MAX;
    r->gpu_timing.requested++;
    if (!r->timestamp_pool || r->timestamp_count / 2 >= r->timestamp_capacity) {
        r->gpu_timing.dropped++;
        return UINT32_MAX;
    }
    r->gpu_timing.recorded++;
    first = r->timestamp_count;
    r->timestamp_category[first / 2] = (unsigned char)category;
    impl->api.cmd_write_timestamp(command, RF_VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        r->timestamp_pool, first);
    r->timestamp_count += 2;
    return first;
}

static void timestamp_end(struct rf_gpu_vulkan_raster *r,
    rf_vk_command_buffer command, uint32_t first)
{
    if (r && first != UINT32_MAX)
        r->owner->api.cmd_write_timestamp(command,
            RF_VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, r->timestamp_pool, first + 1);
}

static void timestamp_collect(struct rf_gpu_vulkan_raster *r)
{
    uint64_t *values = r ? r->timestamp_values : NULL;
    double *fields[7];
    uint64_t mask;
    if (!r || !r->timestamp_pool || !r->timestamp_count) return;
    fields[0]=&r->gpu_timing.raster_ms; fields[1]=&r->gpu_timing.bridge_import_ms;
    fields[2]=&r->gpu_timing.draw_ms; fields[3]=&r->gpu_timing.bridge_export_ms;
    fields[4]=&r->gpu_timing.post_ms; fields[5]=&r->gpu_timing.overlay_ms;
    fields[6]=&r->gpu_timing.present_copy_ms;
    if (r->owner->api.get_query_pool_results(r->owner->device,r->timestamp_pool,
            0,r->timestamp_count,r->timestamp_count*sizeof(*values),values,sizeof(values[0]),
            RF_VK_QUERY_RESULT_64_BIT|RF_VK_QUERY_RESULT_WAIT_BIT)!=RF_VK_SUCCESS) return;
    mask = r->owner->timestamp_valid_bits >= 64 ? UINT64_MAX :
        ((1ULL << r->owner->timestamp_valid_bits) - 1ULL);
    for (uint32_t n=0;n<r->timestamp_count;n+=2) {
        uint64_t delta=(values[n+1]-values[n])&mask;
        *fields[r->timestamp_category[n/2]] +=
            (double)delta * r->owner->timestamp_period / 1000000.0;
    }
    r->gpu_timing.valid=!r->gpu_timing.dropped &&
        r->gpu_timing.requested==r->gpu_timing.recorded;
}

int rf_gpu_vulkan_timestamp_reserve(void *raster, unsigned int intervals)
{
    struct rf_gpu_vulkan_raster *r = raster;
    struct rf_vk_query_pool_create_info info = {0};
    rf_vk_query_pool pool = 0;
    unsigned char *categories;
    uint64_t *values;
    if (!r || !r->owner || r->render_in_flight || r->frame_recording) return -1;
    if (!r->owner->timestamp_supported || intervals <= r->timestamp_capacity) return 0;
    if (intervals > UINT32_MAX / 2) return -1;
#if SIZE_MAX < UINT64_MAX
    if (intervals > SIZE_MAX / (2*sizeof(*values))) return -1;
#endif
    categories = malloc(intervals);
    values = malloc((size_t)intervals * 2 * sizeof(*values));
    info.s_type = RF_VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.query_type = RF_VK_QUERY_TYPE_TIMESTAMP;
    info.query_count = intervals * 2;
    if (!categories || !values || r->owner->api.create_query_pool(
            r->owner->device, &info, NULL, &pool) != RF_VK_SUCCESS) {
        free(categories); free(values); return -1;
    }
    if (r->timestamp_pool)
        r->owner->api.destroy_query_pool(r->owner->device,r->timestamp_pool,NULL);
    free(r->timestamp_category); free(r->timestamp_values);
    r->timestamp_pool = pool; r->timestamp_capacity = intervals;
    r->timestamp_category = categories; r->timestamp_values = values;
    return 0;
}

static int raster_recycle_frame(struct rf_gpu_vulkan_raster *r,
                                uint64_t timeout)
{
    rf_vk_result result;
    if (!r || !r->render_in_flight) return 0;
    result = r->owner->api.wait_for_fences(r->owner->device, 1,
        &r->render_fence, RF_VK_TRUE, timeout);
    if (result != RF_VK_SUCCESS) return result == RF_VK_TIMEOUT ? 1 : -1;
    timestamp_collect(r);
    if (r->submitted_frame > r->owner->mixed_completed_frame)
        r->owner->mixed_completed_frame = r->submitted_frame;
    r->render_in_flight = 0;
    return 0;
}

int rf_gpu_vulkan_raster_recycle(void *raster)
{
    return raster_recycle_frame(raster,5000000000ULL);
}

void rf_gpu_vulkan_mixed_gpu_timing(void *raster,
    struct rf_gpu_mixed_gpu_timing *timing)
{
    struct rf_gpu_vulkan_raster *r=raster;
    if (!timing) return;
    memset(timing,0,sizeof(*timing));
    if (!r) return;
    *timing=r->gpu_timing;
}

static int raster_buffer_create(struct rf_gpu_vulkan_impl *impl,
                                uint64_t size, rf_vk_flags usage,
                                rf_vk_flags required, rf_vk_flags preferred,
                                struct rf_gpu_vulkan_raster_buffer *out)
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
    if (impl->api.allocate_memory(impl->device, &allocation, NULL,
                                  &out->memory) != RF_VK_SUCCESS) return -1;
    if (impl->api.bind_buffer_memory(impl->device, out->buffer, out->memory, 0) !=
        RF_VK_SUCCESS) return -1;
    out->size = size;
    out->allocation_size = requirements.size;
    return 0;
}

static void raster_buffer_destroy(struct rf_gpu_vulkan_impl *impl,
                                  struct rf_gpu_vulkan_raster_buffer *buffer)
{
    if (buffer->buffer)
        impl->api.destroy_buffer(impl->device, buffer->buffer, NULL);
    if (buffer->memory)
        impl->api.free_memory(impl->device, buffer->memory, NULL);
    memset(buffer, 0, sizeof(*buffer));
}

static void raster_input_versions_destroy(struct rf_gpu_vulkan_raster *r)
{
    while (r->input_versions) {
        struct rf_gpu_raster_input_version *v = r->input_versions;
        r->input_versions = v->next;
        raster_buffer_destroy(r->owner, &v->command);
        raster_buffer_destroy(r->owner, &v->tile_offsets);
        raster_buffer_destroy(r->owner, &v->tile_indices);
        raster_buffer_destroy(r->owner, &v->texture_descs);
        raster_buffer_destroy(r->owner, &v->texture_texels);
        free(v);
    }
}

static int raster_input_version_retain(struct rf_gpu_vulkan_raster *r)
{
    struct rf_gpu_raster_input_version *v = calloc(1, sizeof(*v));
    if (!v) return -1;
    v->command = r->command; v->tile_offsets = r->tile_offsets;
    v->tile_indices = r->tile_indices; v->texture_descs = r->texture_descs;
    v->texture_texels = r->texture_texels;
    memset(&r->command, 0, sizeof(r->command));
    memset(&r->tile_offsets, 0, sizeof(r->tile_offsets));
    memset(&r->tile_indices, 0, sizeof(r->tile_indices));
    memset(&r->texture_descs, 0, sizeof(r->texture_descs));
    memset(&r->texture_texels, 0, sizeof(r->texture_texels));
    v->next = r->input_versions; r->input_versions = v;
    return 0;
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
    /* FIFO is required by Vulkan and gives the diagnostic deterministic pacing. */
    for (i = 0; i < mode_count; ++i)
        if (modes[i] == RF_VK_PRESENT_MODE_FIFO_KHR) chosen_mode = modes[i];
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

static int raster_surface_recreate(struct rf_gpu_vulkan_context *context,
                                   struct rf_gpu_vulkan_impl *impl)
{
    struct rf_vk_win32_surface_create_info info;
    rf_vk_surface replacement = NULL;
    rf_vk_bool32 supported = 0;
    if (!context || !context->native_window.window ||
        !context->native_window.instance || !impl->api.create_win32_surface)
        return -1;
    memset(&info, 0, sizeof(info));
    info.s_type = RF_VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.instance = (void *)(uintptr_t)context->native_window.instance;
    info.window = (void *)(uintptr_t)context->native_window.window;
    if (impl->api.create_win32_surface(impl->instance, &info, NULL,
            &replacement) != RF_VK_SUCCESS || !replacement)
        return -1;
    if (impl->api.get_surface_support(impl->physical_device,
            impl->queue_family, replacement, &supported) != RF_VK_SUCCESS ||
        !supported) {
        impl->api.destroy_surface(impl->instance, replacement, NULL);
        return -1;
    }
    presenter_swapchain_destroy(impl);
    if (impl->surface)
        impl->api.destroy_surface(impl->instance, impl->surface, NULL);
    impl->surface = replacement;
    return 0;
}

static void raster_destroy(void *context, void *raster)
{
    struct rf_gpu_vulkan_context *backend_context = context;
    struct rf_gpu_vulkan_raster *r = raster;
    struct rf_gpu_vulkan_impl *impl = backend_context
        ? backend_context->implementation : NULL;
    if (!r) return;
    if (!impl || r->owner != impl) { free(r); return; }
    raster_recycle_frame(r, UINT64_MAX);
    gfx_raster_unshare(r);
    if (r->render_fence)
        impl->api.destroy_fence(impl->device,r->render_fence,NULL);
    if (r->timestamp_pool)
        impl->api.destroy_query_pool(impl->device,r->timestamp_pool,NULL);
    free(r->timestamp_category); free(r->timestamp_values);
    if (r->acquire_semaphore)
        impl->api.destroy_semaphore(impl->device,r->acquire_semaphore,NULL);
    if (r->command_pool)
        impl->api.destroy_command_pool(impl->device, r->command_pool, NULL);
    if (r->pipeline) impl->api.destroy_pipeline(impl->device, r->pipeline, NULL);
    if (r->full_scan_pipeline) impl->api.destroy_pipeline(impl->device, r->full_scan_pipeline, NULL);
    if (r->image_pipeline) impl->api.destroy_pipeline(impl->device, r->image_pipeline, NULL);
    if (r->full_scan_image_pipeline) impl->api.destroy_pipeline(impl->device, r->full_scan_image_pipeline, NULL);
    if (r->overlay_pipeline) impl->api.destroy_pipeline(impl->device, r->overlay_pipeline, NULL);
    if (r->post_pipeline) impl->api.destroy_pipeline(impl->device, r->post_pipeline, NULL);
    if (r->post_image_pipeline) impl->api.destroy_pipeline(impl->device, r->post_image_pipeline, NULL);
    if (r->pipeline_layout)
        impl->api.destroy_pipeline_layout(impl->device, r->pipeline_layout, NULL);
    if (r->shader)
        impl->api.destroy_shader_module(impl->device, r->shader, NULL);
    if (r->full_scan_shader)
        impl->api.destroy_shader_module(impl->device, r->full_scan_shader, NULL);
    if (r->image_shader) impl->api.destroy_shader_module(impl->device, r->image_shader, NULL);
    if (r->full_scan_image_shader) impl->api.destroy_shader_module(impl->device, r->full_scan_image_shader, NULL);
    if (r->overlay_shader)
        impl->api.destroy_shader_module(impl->device, r->overlay_shader, NULL);
    if (r->post_shader)
        impl->api.destroy_shader_module(impl->device, r->post_shader, NULL);
    if (r->post_image_shader) impl->api.destroy_shader_module(impl->device, r->post_image_shader, NULL);
    if (r->descriptor_pool)
        impl->api.destroy_descriptor_pool(impl->device, r->descriptor_pool, NULL);
    if (r->set_layout)
        impl->api.destroy_descriptor_set_layout(impl->device, r->set_layout, NULL);
    raster_input_versions_destroy(r);
    raster_buffer_destroy(impl, &r->command);
    raster_buffer_destroy(impl, &r->tile_indices);
    raster_buffer_destroy(impl, &r->tile_offsets);
    raster_buffer_destroy(impl, &r->texture_descs);
    raster_buffer_destroy(impl, &r->texture_texels);
    raster_buffer_destroy(impl, &r->viewmodel_coverage);
    raster_buffer_destroy(impl, &r->overlay_color);
    raster_buffer_destroy(impl, &r->overlay_coverage);
    raster_buffer_destroy(impl, &r->post_params);
    raster_buffer_destroy(impl, &r->post_color);
    raster_buffer_destroy(impl, &r->depth_readback);
    raster_buffer_destroy(impl, &r->color_readback);
    raster_buffer_destroy(impl, &r->depth);
    raster_buffer_destroy(impl, &r->color);
    rf_gpu_raster_tile_lists_destroy(&r->tile_lists);
    free(r);
}

static int raster_create(void *context, unsigned int width,
                         unsigned int height, unsigned int work_group_x,
                         unsigned int work_group_y, void **raster,
                         char *message, unsigned long message_capacity)
{
    struct rf_gpu_vulkan_context *backend_context = context;
    struct rf_gpu_vulkan_impl *impl = backend_context
        ? backend_context->implementation : NULL;
    struct rf_gpu_vulkan_raster *r = NULL;
    uint64_t byte_size = (uint64_t)width * height * 4;
    const unsigned char *spirv;
    unsigned int spirv_size;
    unsigned int i;
    if (!impl || !raster || !width || !height ||
        !((work_group_x == 16 && work_group_y == 16) ||
          (work_group_x == 8 && work_group_y == 8))) return -1;
    *raster = NULL;
    r = calloc(1, sizeof(*r));
    if (!r) goto failed;
    r->owner = impl; r->width = width; r->height = height;
    r->audit_slot_id = impl->next_slot_id++;
    r->acquire_semaphore_state = RF_GPU_PRESENT_AUDIT_REUSABLE;
    r->work_group_x = work_group_x; r->work_group_y = work_group_y;
    {
        struct rf_vk_fence_create_info fence_info;
        struct rf_vk_semaphore_create_info semaphore_info;
        memset(&fence_info,0,sizeof(fence_info));
        fence_info.s_type=RF_VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (!impl->api.reset_fences || impl->api.create_fence(impl->device,
                &fence_info,NULL,&r->render_fence)!=RF_VK_SUCCESS) goto failed;
        if (impl->native_presentation_supported) {
            memset(&semaphore_info,0,sizeof(semaphore_info));
            semaphore_info.s_type=RF_VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            if (impl->api.create_semaphore(impl->device,&semaphore_info,NULL,
                    &r->acquire_semaphore)!=RF_VK_SUCCESS) goto failed;
        }
    }
    r->gpu_timing.supported=impl->timestamp_supported;
    if (rf_gpu_vulkan_timestamp_reserve(r,16)<0) goto failed;
    if (raster_buffer_create(impl, byte_size,
            RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            RF_VK_BUFFER_USAGE_TRANSFER_SRC_BIT | RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &r->color) < 0 ||
        raster_buffer_create(impl, byte_size,
            RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            RF_VK_BUFFER_USAGE_TRANSFER_SRC_BIT | RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &r->depth) < 0 ||
        raster_buffer_create(impl, byte_size,
            RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
            &r->viewmodel_coverage) < 0 ||
        raster_buffer_create(impl, byte_size,
            RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            RF_VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, &r->post_color) < 0 ||
        raster_buffer_create(impl, byte_size, RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &r->color_readback) < 0 ||
        raster_buffer_create(impl, byte_size, RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &r->depth_readback) < 0) goto failed;
    {
        struct rf_vk_descriptor_set_layout_binding bindings[13];
        struct rf_vk_descriptor_set_layout_create_info info;
        memset(bindings, 0, sizeof(bindings));
        for (i = 0; i < 12; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptor_type = RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptor_count = 1;
            bindings[i].stage_flags = RF_VK_SHADER_STAGE_COMPUTE_BIT;
        }
        bindings[12].binding = 12;
        bindings[12].descriptor_type = RF_VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[12].descriptor_count = 1;
        bindings[12].stage_flags = RF_VK_SHADER_STAGE_COMPUTE_BIT;
        memset(&info, 0, sizeof(info));
        info.s_type = RF_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.binding_count = 13; info.bindings = bindings;
        if (impl->api.create_descriptor_set_layout(impl->device, &info, NULL,
                                                   &r->set_layout) != RF_VK_SUCCESS)
            goto failed;
    }
    {
        struct rf_vk_descriptor_pool_size sizes[2];
        struct rf_vk_descriptor_pool_create_info pool_info;
        struct rf_vk_descriptor_set_allocate_info allocation;
        sizes[0].type = RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        sizes[0].descriptor_count =
            12 * RF_GPU_RASTER_DESCRIPTOR_SET_COUNT;
        sizes[1].type = RF_VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        sizes[1].descriptor_count = RF_GPU_RASTER_DESCRIPTOR_SET_COUNT;
        memset(&pool_info, 0, sizeof(pool_info));
        pool_info.s_type = RF_VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.max_sets = RF_GPU_RASTER_DESCRIPTOR_SET_COUNT;
        pool_info.pool_size_count = 2;
        pool_info.pool_sizes = sizes;
        if (impl->api.create_descriptor_pool(impl->device, &pool_info, NULL,
                                             &r->descriptor_pool) != RF_VK_SUCCESS)
            goto failed;
        memset(&allocation, 0, sizeof(allocation));
        allocation.s_type = RF_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocation.descriptor_pool = r->descriptor_pool;
        {
            rf_vk_descriptor_set_layout layouts[
                RF_GPU_RASTER_DESCRIPTOR_SET_COUNT];
            for (i = 0; i < RF_GPU_RASTER_DESCRIPTOR_SET_COUNT; ++i)
                layouts[i] = r->set_layout;
            allocation.descriptor_set_count =
                RF_GPU_RASTER_DESCRIPTOR_SET_COUNT;
            allocation.set_layouts = layouts;
            if (impl->api.allocate_descriptor_sets(impl->device, &allocation,
                    r->descriptor_sets) != RF_VK_SUCCESS)
                goto failed;
        }
    }
    spirv = work_group_x == 16 ? rf_gpu_raster_v1_16_spirv
                               : rf_gpu_raster_v1_8_spirv;
    spirv_size = work_group_x == 16 ? rf_gpu_raster_v1_16_spirv_len
                                    : rf_gpu_raster_v1_8_spirv_len;
    {
        struct rf_vk_shader_module_create_info shader_info;
        struct rf_vk_pipeline_layout_create_info layout_info;
        struct rf_vk_compute_pipeline_create_info pipeline_info;
        const struct { uint32_t stages, offset, size; } push = {
            RF_VK_SHADER_STAGE_COMPUTE_BIT, 0, 12 };
        memset(&shader_info, 0, sizeof(shader_info));
        shader_info.s_type = RF_VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shader_info.code_size = spirv_size;
        shader_info.code = (const uint32_t *)(const void *)spirv;
        if (impl->api.create_shader_module(impl->device, &shader_info, NULL,
                                           &r->shader) != RF_VK_SUCCESS) goto failed;
        memset(&layout_info, 0, sizeof(layout_info));
        layout_info.s_type = RF_VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.set_layout_count = 1; layout_info.set_layouts = &r->set_layout;
        layout_info.push_constant_range_count = 1;
        layout_info.push_constant_ranges = &push;
        if (impl->api.create_pipeline_layout(impl->device, &layout_info, NULL,
                                             &r->pipeline_layout) != RF_VK_SUCCESS)
            goto failed;
        memset(&pipeline_info, 0, sizeof(pipeline_info));
        pipeline_info.s_type = RF_VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipeline_info.stage.s_type =
            RF_VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipeline_info.stage.stage = RF_VK_SHADER_STAGE_COMPUTE_BIT;
        pipeline_info.stage.module = r->shader;
        pipeline_info.stage.name = "main";
        pipeline_info.layout = r->pipeline_layout;
        pipeline_info.base_pipeline_index = -1;
        if (impl->api.create_compute_pipelines(impl->device, NULL, 1,
                &pipeline_info, NULL, &r->pipeline) != RF_VK_SUCCESS) goto failed;
        shader_info.code_size = work_group_x == 16 ? rf_gpu_raster_v1_full_16_spirv_len : rf_gpu_raster_v1_full_8_spirv_len;
        shader_info.code = (const uint32_t *)(const void *)(work_group_x == 16 ? rf_gpu_raster_v1_full_16_spirv : rf_gpu_raster_v1_full_8_spirv);
        if (impl->api.create_shader_module(impl->device,&shader_info,NULL,&r->full_scan_shader)!=RF_VK_SUCCESS)goto failed;
        pipeline_info.stage.module=r->full_scan_shader;
        if(impl->api.create_compute_pipelines(impl->device,NULL,1,&pipeline_info,NULL,&r->full_scan_pipeline)!=RF_VK_SUCCESS)goto failed;
        shader_info.code_size = work_group_x == 16 ? rf_gpu_raster_v1_image_16_spirv_len : rf_gpu_raster_v1_image_8_spirv_len;
        shader_info.code = (const uint32_t *)(const void *)(work_group_x == 16 ? rf_gpu_raster_v1_image_16_spirv : rf_gpu_raster_v1_image_8_spirv);
        if (impl->api.create_shader_module(impl->device,&shader_info,NULL,&r->image_shader)!=RF_VK_SUCCESS) goto failed;
        pipeline_info.stage.module=r->image_shader;
        if(impl->api.create_compute_pipelines(impl->device,NULL,1,&pipeline_info,NULL,&r->image_pipeline)!=RF_VK_SUCCESS)goto failed;
        shader_info.code_size = work_group_x == 16 ? rf_gpu_raster_v1_full_image_16_spirv_len : rf_gpu_raster_v1_full_image_8_spirv_len;
        shader_info.code = (const uint32_t *)(const void *)(work_group_x == 16 ? rf_gpu_raster_v1_full_image_16_spirv : rf_gpu_raster_v1_full_image_8_spirv);
        if (impl->api.create_shader_module(impl->device,&shader_info,NULL,&r->full_scan_image_shader)!=RF_VK_SUCCESS) goto failed;
        pipeline_info.stage.module=r->full_scan_image_shader;
        if(impl->api.create_compute_pipelines(impl->device,NULL,1,&pipeline_info,NULL,&r->full_scan_image_pipeline)!=RF_VK_SUCCESS)goto failed;
        shader_info.code_size = work_group_x == 16 ? rf_gpu_overlay_16_spirv_len : rf_gpu_overlay_8_spirv_len;
        shader_info.code = (const uint32_t *)(const void *)(work_group_x == 16 ? rf_gpu_overlay_16_spirv : rf_gpu_overlay_8_spirv);
        if (impl->api.create_shader_module(impl->device,&shader_info,NULL,
                &r->overlay_shader)!=RF_VK_SUCCESS) goto failed;
        pipeline_info.stage.module=r->overlay_shader;
        if (impl->api.create_compute_pipelines(impl->device,NULL,1,
                &pipeline_info,NULL,&r->overlay_pipeline)!=RF_VK_SUCCESS) goto failed;
        shader_info.code_size = work_group_x == 16 ? rf_gpu_post_16_spirv_len : rf_gpu_post_8_spirv_len;
        shader_info.code = (const uint32_t *)(const void *)(work_group_x == 16 ? rf_gpu_post_16_spirv : rf_gpu_post_8_spirv);
        if (impl->api.create_shader_module(impl->device,&shader_info,NULL,
                &r->post_shader)==RF_VK_SUCCESS) {
            pipeline_info.stage.module=r->post_shader;
            if (impl->api.create_compute_pipelines(impl->device,NULL,1,
                    &pipeline_info,NULL,&r->post_pipeline)!=RF_VK_SUCCESS) {
                impl->api.destroy_shader_module(impl->device,r->post_shader,NULL);
                r->post_shader=NULL;
            }
        }
        shader_info.code_size = work_group_x == 16 ? rf_gpu_post_image_16_spirv_len : rf_gpu_post_image_8_spirv_len;
        shader_info.code = (const uint32_t *)(const void *)(work_group_x == 16 ? rf_gpu_post_image_16_spirv : rf_gpu_post_image_8_spirv);
        if (impl->api.create_shader_module(impl->device,&shader_info,NULL,&r->post_image_shader)==RF_VK_SUCCESS) {
            pipeline_info.stage.module=r->post_image_shader;
            if (impl->api.create_compute_pipelines(impl->device,NULL,1,&pipeline_info,NULL,&r->post_image_pipeline)!=RF_VK_SUCCESS) {
                impl->api.destroy_shader_module(impl->device,r->post_image_shader,NULL);
                r->post_image_shader=NULL;
            }
        }
    }
    {
        struct rf_vk_command_pool_create_info pool_info;
        struct rf_vk_command_buffer_allocate_info allocation;
        memset(&pool_info, 0, sizeof(pool_info));
        pool_info.s_type = RF_VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = RF_VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queue_family_index = impl->queue_family;
        if (impl->api.create_command_pool(impl->device, &pool_info, NULL,
                                          &r->command_pool) != RF_VK_SUCCESS)
            goto failed;
        memset(&allocation, 0, sizeof(allocation));
        allocation.s_type = RF_VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocation.command_pool = r->command_pool;
        allocation.level = RF_VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.command_buffer_count = 1;
        if (impl->api.allocate_command_buffers(impl->device, &allocation,
                                               &r->command_buffer) != RF_VK_SUCCESS)
            goto failed;
    }
    *raster = r;
    snprintf(message, message_capacity, "GPU Raster V1 ready (%ux%u, %ux%u)",
             width, height, work_group_x, work_group_y);
    return 0;
failed:
    snprintf(message, message_capacity, "Vulkan Raster V1 creation failed");
    raster_destroy(context, r);
    return -1;
}

static int raster_upload_buffer_grow(struct rf_gpu_vulkan_impl *impl,
                               struct rf_gpu_vulkan_raster_buffer *buffer,
                               uint64_t required)
{
    struct rf_gpu_vulkan_raster_buffer replacement;
    uint64_t capacity = buffer->size ? buffer->size : 4096;
    if (buffer->size >= required) return 0;
    while (capacity < required) {
        if (capacity > UINT64_MAX / 2) { capacity = required; break; }
        capacity *= 2;
    }
    if (raster_buffer_create(impl, capacity,
            RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &replacement) < 0) {
        raster_buffer_destroy(impl,&replacement);
        return -1;
    }
    raster_buffer_destroy(impl, buffer);
    *buffer = replacement;
    return 0;
}

static int raster_upload(struct rf_gpu_vulkan_impl *impl,
                         struct rf_gpu_vulkan_raster_buffer *buffer,
                         const void *data, uint64_t size)
{
    void *mapped=NULL;
    if (!size || raster_upload_buffer_grow(impl,buffer,size)<0 ||
        impl->api.map_memory(impl->device,buffer->memory,0,
                            buffer->allocation_size,0,&mapped)!=RF_VK_SUCCESS)
        return -1;
    memcpy(mapped,data,(size_t)size);
    if (!(buffer->memory_flags & RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        struct rf_vk_mapped_memory_range range;
        memset(&range,0,sizeof(range));range.s_type=RF_VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory=buffer->memory;range.offset=0;range.size=buffer->allocation_size;
        if(impl->api.flush_mapped_memory_ranges(impl->device,1,&range)!=RF_VK_SUCCESS){
            impl->api.unmap_memory(impl->device,buffer->memory);return -1;
        }
    }
    impl->api.unmap_memory(impl->device,buffer->memory);return 0;
}

static int raster_invalidate(struct rf_gpu_vulkan_impl *impl,
                             struct rf_gpu_vulkan_raster_buffer *buffer)
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

static int raster_preflight(struct rf_gpu_vulkan_context *context,
    void *raster, const void *stream, unsigned long stream_size,
    const void *texture_descs, unsigned int texture_count,
    const void *texture_texels, unsigned long texture_bytes,
    unsigned int width, unsigned int height)
{
    struct rf_gpu_vulkan_impl *impl = context ? context->implementation : NULL;
    struct rf_gpu_vulkan_raster *r = raster;
    if (!impl || !r || r->owner != impl || r->width != width ||
        r->height != height || !stream_size ||
        stream_size > impl->max_storage_buffer_range ||
        rf_gpu_raster_validate_v1(stream, stream_size) ||
        ((const uint32_t *)stream)[5] != width ||
        ((const uint32_t *)stream)[6] != height)
        return -1;
    if ((texture_count && (!texture_descs || !texture_texels || !texture_bytes)) ||
        (uint64_t)texture_count * sizeof(struct rf_gpu_texture_desc_host_v1) >
            impl->max_storage_buffer_range ||
        texture_bytes > impl->max_storage_buffer_range)
        return -1;
    {
        const uint32_t *words = stream;
        uint32_t ci, command_count = words[4];
        for (ci = 0; ci < command_count; ++ci) {
            uint32_t base = 8U + ci * 24U;
            if (words[base] == 5U &&
                (!words[base + 3U] || words[base + 3U] > texture_count))
                return -1;
        }
    }
    if (texture_count) {
        const struct rf_gpu_texture_desc_host_v1 *descs = texture_descs;
        unsigned int ti;
        for (ti = 0; ti < texture_count; ++ti) {
            uint64_t row_bytes = (uint64_t)descs[ti].width *
                (descs[ti].format == RF_GPU_TEXTURE_FORMAT_RGBA8_HOST_V1 ? 4 : 3);
            uint64_t end = (uint64_t)descs[ti].texel_offset +
                (uint64_t)descs[ti].stride * descs[ti].height;
            if (!descs[ti].width || !descs[ti].height ||
                (descs[ti].format != RF_GPU_TEXTURE_FORMAT_RGB8_HOST_V1 &&
                 descs[ti].format != RF_GPU_TEXTURE_FORMAT_RGBA8_HOST_V1) ||
                descs[ti].sampling != RF_GPU_TEXTURE_SAMPLING_NEAREST_HOST_V1 ||
                descs[ti].stride < row_bytes || end > texture_bytes)
                return -1;
        }
    }
    return 0;
}

int rf_gpu_vulkan_raster_preflight(struct rf_gpu_vulkan_context *context,
    void *raster, const void *stream, unsigned long stream_size,
    const void *texture_descs, unsigned int texture_count,
    const void *texture_texels, unsigned long texture_bytes,
    unsigned int width, unsigned int height)
{
    struct rf_gpu_vulkan_impl *impl = context ? context->implementation : NULL;
    struct rf_gpu_vulkan_raster *r = raster;
    if (raster_recycle_frame(r, 5000000000ULL) != 0) return -1;
    if (r) {
        r->binned_stream = NULL; r->binned_stream_size = 0;
        r->uploaded_stream = NULL; r->uploaded_stream_size = 0;
        r->preflight_reuse = 0;
    }
    if (raster_preflight(context, raster, stream, stream_size, texture_descs,
        texture_count, texture_texels, texture_bytes, width, height) < 0) {
        fprintf(stderr,"Vulkan Raster preflight: stream/texture contract failed size=%lu textures=%u texels=%lu extent=%ux%u\n",
            stream_size,texture_count,texture_bytes,width,height);
        return -1;
    }
    if (!r->full_scan_diagnostic) {
        int bin_result=rf_gpu_raster_bin_v1(stream, stream_size, r->work_group_x,
            r->work_group_y, &r->tile_lists);
        uint64_t tile_bytes=((uint64_t)r->tile_lists.stats.tile_count+1)*4;
        uint64_t ref_bytes=r->tile_lists.stats.total_refs*4;
        if (bin_result<0 || tile_bytes>impl->max_storage_buffer_range ||
            ref_bytes>impl->max_storage_buffer_range) {
            fprintf(stderr,"Vulkan Raster preflight: binning failed result=%d tiles=%u refs=%llu tile-bytes=%llu ref-bytes=%llu limit=%llu\n",
                bin_result,r->tile_lists.stats.tile_count,
                (unsigned long long)r->tile_lists.stats.total_refs,
                (unsigned long long)tile_bytes,(unsigned long long)ref_bytes,
                (unsigned long long)impl->max_storage_buffer_range);
            return -1;
        }
    }
    if (!r->full_scan_diagnostic) {
        r->binned_stream = stream;
        r->binned_stream_size = stream_size;
    }
    r->preflight_reuse = 1;
    return 0;
}

static int raster_render_range(void *context, void *raster,
                         const void *stream, unsigned long stream_size,
                         const void *texture_descs, unsigned int texture_count,
                         const void *texture_texels, unsigned long texture_bytes,
                         unsigned int *color, int *depth,
                         unsigned int width, unsigned int height,
                         unsigned int color_stride, unsigned int depth_stride,
                         struct rf_gpu_raster_timing *timing,
                         char *message, unsigned long message_capacity,
                         uint32_t first, uint32_t end, uint32_t load,
                         int final)
{
    struct rf_gpu_vulkan_context *backend_context = context;
    struct rf_gpu_vulkan_impl *impl = backend_context
        ? backend_context->implementation : NULL;
    struct rf_gpu_vulkan_raster *r = raster;
    void *mapped_color = NULL, *mapped_depth = NULL;
    uint64_t byte_size = (uint64_t)width * height * 4;
    unsigned int y;
    rf_vk_result result;
    int native_present = final && color == NULL && depth == NULL;
    int composite = final && r && (native_present ||
        (r->pending_overlay_color && r->pending_overlay_coverage));
    int post_enabled = final && r &&
        ((r->shared_color_enabled && r->post_image_pipeline) ||
         (r->post.mode != RF_GPU_POST_DISABLED && r->post_pipeline));
    struct rf_gpu_vulkan_raster_buffer *presentation_color =
        r ? (post_enabled ? &r->post_color : &r->color) : NULL;
    uint32_t swapchain_image = 0;
    int swapchain_image_acquired = 0;
    int present_transaction_complete = 0;
    int image_reacquired = 0;
    struct rf_gpu_native_present_timing *native_timing = NULL;
    rf_vk_descriptor_set descriptor_set = NULL;
    double total_start = now_ms(), segment_start;
    static const uint32_t empty_texture[6] = {0, 1, 1, 4, 2, 1};
    static const uint32_t empty_texel = 0xffffffffU;
    if (timing) memset(timing, 0, sizeof(*timing));
    segment_start = now_ms();
    if (raster_preflight(backend_context, raster, stream, stream_size,
            texture_descs, texture_count, texture_texels, texture_bytes,
            width, height) < 0) return -1;
    if (end == UINT32_MAX) end = ((const uint32_t *)stream)[4];
    if (load > RF_GPU_RASTER_LOAD_EXISTING || first > end ||
        end > ((const uint32_t *)stream)[4] ||
        (load == RF_GPU_RASTER_CLEAR && (first != 0 || end < 2)) ||
        (load == RF_GPU_RASTER_LOAD_EXISTING && (!r->segment_valid || first < 2)) ||
        (final && !native_present && (!color || !depth ||
            color_stride < width || depth_stride < width))) return -1;
    /* Never overwrite an input buffer still owned by a submitted frame. */
    if (r->render_in_flight) return -1;
    if (!r->frame_recording) r->descriptor_set_cursor = 0;
    if (r->descriptor_set_cursor >= RF_GPU_RASTER_DESCRIPTOR_SET_COUNT)
        return -1;
    descriptor_set = r->descriptor_sets[r->descriptor_set_cursor++];
    /* VIEWMODEL depth is local to a dispatch. Keep its entire domain in the
     * terminal segment; never silently reset it across a cut. */
    if (first || !final || end != ((const uint32_t *)stream)[4]) {
        const uint32_t *words = stream;
        uint32_t ci;
        for (ci = 2; ci < words[4]; ++ci)
            if (words[8 + ci * 24] == 7U &&
                (ci < first || (ci < end && (!final || end != words[4])))) return -1;
    }
    if (native_present) {
        double acquire_start;
        impl->present_attempt++;
        native_timing = r->pending_present_timing;
        if (!native_timing) return -1;
        if (!impl->swapchain || impl->swapchain_width != width ||
            impl->swapchain_height != height)
            if (presenter_swapchain_create(impl, width, height) < 0) return -1;
        if (impl->presenter_poisoned)
            return present_audit_fail("presenter-generation-poisoned");
        if (r->acquire_semaphore_state != RF_GPU_PRESENT_AUDIT_REUSABLE)
            return present_audit_fail("acquire-semaphore-not-reusable");
        acquire_start = now_ms();
        result = present_fault_take(impl,
            RF_GPU_PRESENT_FAULT_ACQUIRE_OUT_OF_DATE) ?
            RF_VK_ERROR_OUT_OF_DATE_KHR :
            impl->api.acquire_next_image(impl->device, impl->swapchain,
                UINT64_MAX, r->acquire_semaphore, NULL, &swapchain_image);
        if (result == RF_VK_ERROR_SURFACE_LOST_KHR) {
            if (raster_surface_recreate(backend_context, impl) < 0)
                return -1;
            result = RF_VK_ERROR_OUT_OF_DATE_KHR;
        }
        if (result == RF_VK_ERROR_OUT_OF_DATE_KHR) {
            if (presenter_swapchain_create(impl, width, height) < 0) return -1;
            result = impl->api.acquire_next_image(impl->device, impl->swapchain,
                UINT64_MAX, r->acquire_semaphore, NULL, &swapchain_image);
        }
        if (result != RF_VK_SUCCESS && result != RF_VK_SUBOPTIMAL_KHR) return -1;
        swapchain_image_acquired = 1;
        if (swapchain_image >= impl->swapchain_image_count ||
            !impl->present_images ||
            impl->present_images[swapchain_image].generation !=
                impl->presenter_generation) {
            present_audit_poison(impl, r, swapchain_image,
                swapchain_image < impl->swapchain_image_count);
            return present_audit_fail("image-generation-mismatch");
        }
        if (impl->present_images[swapchain_image].state ==
                RF_GPU_PRESENT_AUDIT_PENDING) {
            impl->present_images[swapchain_image].state =
                RF_GPU_PRESENT_AUDIT_RETIRED;
            impl->present_images[swapchain_image].render_finished_state =
                RF_GPU_PRESENT_AUDIT_REUSABLE;
            impl->present_images[swapchain_image].retire_generation =
                ++impl->next_present_generation;
            if (impl->outstanding_presents) impl->outstanding_presents--;
            image_reacquired = 1;
        }
        if (impl->present_images[swapchain_image].render_finished_state !=
                RF_GPU_PRESENT_AUDIT_REUSABLE) {
            present_audit_poison(impl, r, swapchain_image, 1);
            return present_audit_fail("image-render-finished-not-reusable");
        }
        r->slot_generation++;
        r->acquire_semaphore_state = RF_GPU_PRESENT_AUDIT_SIGNALED;
        impl->present_images[swapchain_image].state =
            RF_GPU_PRESENT_AUDIT_REUSABLE;
        impl->present_images[swapchain_image].owner_slot = r->audit_slot_id;
        impl->present_images[swapchain_image].acquire_generation =
            ++impl->next_present_generation;
        native_timing->acquire_ms = now_ms() - acquire_start;
    }
    if (timing) timing->pack_validation_ms = now_ms() - segment_start;
    if (!r->frame_recording) raster_input_versions_destroy(r);
    else if ((!r->preflight_reuse || r->uploaded_stream != stream ||
              r->uploaded_stream_size != stream_size) &&
             raster_input_version_retain(r) < 0) goto failed;
    segment_start = now_ms();
    if(!r->full_scan_diagnostic &&
       (!r->preflight_reuse || r->binned_stream != stream || r->binned_stream_size != stream_size)) {
        if(rf_gpu_raster_bin_v1(stream,stream_size,r->work_group_x,r->work_group_y,
                               &r->tile_lists)<0)goto failed;
        if (r->preflight_reuse) {
            r->binned_stream=stream;
            r->binned_stream_size=stream_size;
        }
    }
    if(!r->full_scan_diagnostic &&
       (((uint64_t)r->tile_lists.stats.tile_count+1)*4>impl->max_storage_buffer_range ||
        r->tile_lists.stats.total_refs*4>impl->max_storage_buffer_range))goto failed;
    if(timing){uint64_t tiles=((uint64_t)width+r->work_group_x-1)/r->work_group_x*((height+r->work_group_y-1)/r->work_group_y);
        timing->cpu_binning_ms=now_ms()-segment_start;
        timing->command_count=((const uint32_t*)stream)[4];
        timing->tile_count=(unsigned int)tiles;
        timing->total_refs=r->full_scan_diagnostic?tiles*timing->command_count:r->tile_lists.stats.total_refs;
        timing->max_refs_per_tile=r->full_scan_diagnostic?timing->command_count:r->tile_lists.stats.max_refs_per_tile;}
    segment_start=now_ms();
    if(!r->preflight_reuse || r->uploaded_stream != stream || r->uploaded_stream_size != stream_size)
        if(raster_upload(impl,&r->command,stream,stream_size)<0)goto failed;
    if(timing)timing->command_upload_ms=now_ms()-segment_start;
    segment_start=now_ms();
    if((!r->preflight_reuse || r->uploaded_stream != stream || r->uploaded_stream_size != stream_size) &&
       !r->full_scan_diagnostic && (raster_upload(impl,&r->tile_offsets,r->tile_lists.offsets,
        ((uint64_t)r->tile_lists.stats.tile_count+1)*4)<0 ||
       (r->tile_lists.stats.total_refs && raster_upload(impl,&r->tile_indices,
        r->tile_lists.indices,r->tile_lists.stats.total_refs*4)<0)))goto failed;
    if(timing){timing->tile_upload_ms=now_ms()-segment_start;
        timing->upload_ms=timing->command_upload_ms+timing->tile_upload_ms;}
    segment_start=now_ms();
    if ((!r->preflight_reuse || r->uploaded_stream != stream || r->uploaded_stream_size != stream_size) &&
        (raster_upload(impl, &r->texture_descs,
            texture_count ? texture_descs : empty_texture,
            texture_count ? (uint64_t)texture_count * sizeof(struct rf_gpu_texture_desc_host_v1) : sizeof(empty_texture)) < 0 ||
        raster_upload(impl, &r->texture_texels,
            texture_count ? texture_texels : &empty_texel,
            texture_count ? texture_bytes : sizeof(empty_texel)) < 0))
        goto failed;
    if (r->preflight_reuse) {
        r->uploaded_stream=stream;
        r->uploaded_stream_size=stream_size;
    }
    if (timing) {
        timing->texture_upload_ms=now_ms()-segment_start;
        timing->texture_count=texture_count;
        timing->texture_bytes=texture_bytes;
        timing->upload_ms+=timing->texture_upload_ms;
    }
    if (composite) {
        uint64_t color_bytes = byte_size;
        uint64_t coverage_bytes = ((uint64_t)width * height + 3U) & ~3ULL;
        double overlay_start = now_ms();
        if (!r->pending_overlay_color || !r->pending_overlay_coverage ||
            raster_upload(impl, &r->overlay_color,
                          r->pending_overlay_color, color_bytes) < 0 ||
            raster_upload(impl, &r->overlay_coverage,
                          r->pending_overlay_coverage, coverage_bytes) < 0)
            goto failed;
        if (native_timing) {
            native_timing->overlay_upload_ms = now_ms() - overlay_start;
            native_timing->overlay_upload_bytes =
                (uint32_t)(color_bytes + (uint64_t)width * height);
        }
    }
    if (post_enabled && raster_upload(impl, &r->post_params, &r->post,
                                     sizeof(r->post)) < 0) goto failed;
    {
        struct rf_vk_descriptor_buffer_info infos[12];
        struct rf_vk_write_descriptor_set writes[12];
        memset(infos, 0, sizeof(infos)); memset(writes, 0, sizeof(writes));
        infos[0].buffer = r->command.buffer; infos[0].range = stream_size;
        infos[1].buffer = r->color.buffer; infos[1].range = byte_size;
        infos[2].buffer = r->depth.buffer; infos[2].range = byte_size;
        infos[3].buffer = r->tile_offsets.buffer;
        infos[3].range = ((uint64_t)r->tile_lists.stats.tile_count+1)*4;
        infos[4].buffer = r->tile_indices.buffer;
        infos[4].range = r->tile_lists.stats.total_refs*4;
        /* Full-scan has no tile buffers; empty bins may have no index buffer.
         * Vulkan still requires valid descriptors for every written binding. */
        if (!infos[3].buffer || !infos[3].range) infos[3] = infos[0];
        if (!infos[4].buffer || !infos[4].range) infos[4] = infos[0];
        infos[5].buffer = r->texture_descs.buffer;
        infos[5].range = texture_count ?
            (uint64_t)texture_count * sizeof(struct rf_gpu_texture_desc_host_v1) : sizeof(empty_texture);
        infos[6].buffer = r->texture_texels.buffer;
        infos[6].range = texture_count ? texture_bytes : sizeof(empty_texel);
        infos[11].buffer = r->viewmodel_coverage.buffer;
        infos[11].range = byte_size;
        if (composite) {
            infos[7].buffer = r->overlay_color.buffer;
            infos[7].range = byte_size;
            infos[8].buffer = r->overlay_coverage.buffer;
            infos[8].range = ((uint64_t)width * height + 3U) & ~3ULL;
        } else {
            infos[7] = infos[1]; infos[8] = infos[1];
        }
        infos[9].buffer = presentation_color->buffer;
        infos[9].range = byte_size;
        infos[10].buffer = post_enabled ? r->post_params.buffer : r->command.buffer;
        infos[10].range = post_enabled ? sizeof(r->post) : stream_size;
        for (y = 0; y < 12; ++y) {
            writes[y].s_type = RF_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[y].dst_set = descriptor_set; writes[y].dst_binding = y;
            writes[y].descriptor_count = 1;
            writes[y].descriptor_type = RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[y].buffer_info = &infos[y];
        }
        impl->api.update_descriptor_sets(impl->device, 12, writes, 0, NULL);
        if (r->shared_color_enabled) {
            struct rf_vk_descriptor_image_info image;
            struct rf_vk_write_descriptor_set write;
            memset(&image,0,sizeof(image)); memset(&write,0,sizeof(write));
            image.image_view=r->shared_color_view;
            image.image_layout=RF_VK_IMAGE_LAYOUT_GENERAL;
            write.s_type=RF_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dst_set=descriptor_set; write.dst_binding=12;
            write.descriptor_count=1;
            write.descriptor_type=RF_VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            write.image_info=&image;
            impl->api.update_descriptor_sets(impl->device,1,&write,0,NULL);
        }
    }
    if (!r->frame_recording &&
        impl->api.reset_command_pool(impl->device, r->command_pool, 0) !=
            RF_VK_SUCCESS) goto failed;
    {
        struct rf_vk_command_buffer_begin_info begin;
        struct rf_vk_memory_barrier barrier;
        struct rf_vk_buffer_copy copies[2];
        memset(&begin, 0, sizeof(begin));
        begin.s_type = RF_VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (!r->frame_recording) {
            if (impl->api.begin_command_buffer(r->command_buffer, &begin) !=
                RF_VK_SUCCESS) goto failed;
            r->frame_recording = 1;
        }
        if (load==RF_GPU_RASTER_CLEAR) {
            memset(&r->gpu_timing,0,sizeof(r->gpu_timing));
            r->gpu_timing.supported=impl->timestamp_supported; r->timestamp_count=0;
            if (r->timestamp_pool)
                impl->api.cmd_reset_query_pool(r->command_buffer,r->timestamp_pool,0,
                    r->timestamp_capacity*2);
        }
        if (r->shared_color_enabled) {
            struct rf_vk_image_memory_barrier image;
            memset(&image,0,sizeof(image));
            image.s_type=RF_VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            image.old_layout=r->shared_color_layout_valid ?
                RF_VK_IMAGE_LAYOUT_GENERAL : RF_VK_IMAGE_LAYOUT_UNDEFINED;
            image.new_layout=RF_VK_IMAGE_LAYOUT_GENERAL;
            image.src_access_mask=r->shared_color_layout_valid ?
                (RF_VK_ACCESS_SHADER_WRITE_BIT|RF_VK_ACCESS_SHADER_READ_BIT) : 0;
            image.dst_access_mask=RF_VK_ACCESS_SHADER_READ_BIT|RF_VK_ACCESS_SHADER_WRITE_BIT;
            image.src_queue_family_index=image.dst_queue_family_index=UINT32_MAX;
            image.image=r->shared_color_image;
            image.subresource_range.aspect_mask=RF_VK_IMAGE_ASPECT_COLOR_BIT;
            image.subresource_range.level_count=image.subresource_range.layer_count=1;
            impl->api.cmd_pipeline_barrier(r->command_buffer,
                r->shared_color_layout_valid ?
                    (RF_VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT) :
                    RF_VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                RF_VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,NULL,0,NULL,1,&image);
            r->shared_color_layout_valid=1;
        }
        memset(&barrier, 0, sizeof(barrier));
        barrier.s_type = RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.src_access_mask = RF_VK_ACCESS_HOST_WRITE_BIT;
        barrier.dst_access_mask = RF_VK_ACCESS_SHADER_READ_BIT;
        impl->api.cmd_pipeline_barrier(r->command_buffer,
            RF_VK_PIPELINE_STAGE_HOST_BIT, RF_VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &barrier, 0, NULL, 0, NULL);
        impl->api.cmd_bind_pipeline(r->command_buffer,
            RF_VK_PIPELINE_BIND_POINT_COMPUTE,
            r->shared_color_enabled ?
                (r->full_scan_diagnostic?r->full_scan_image_pipeline:r->image_pipeline) :
                (r->full_scan_diagnostic?r->full_scan_pipeline:r->pipeline));
        impl->api.cmd_bind_descriptor_sets(r->command_buffer,
            RF_VK_PIPELINE_BIND_POINT_COMPUTE, r->pipeline_layout, 0, 1,
            &descriptor_set, 0, NULL);
        /* Earlier segments may have been sampled/copied as well as written.
         * This dependency also protects CLEAR after a prior diagnostic read. */
        barrier.src_access_mask = RF_VK_ACCESS_SHADER_WRITE_BIT |
            RF_VK_ACCESS_SHADER_READ_BIT | RF_VK_ACCESS_TRANSFER_READ_BIT | RF_VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dst_access_mask = RF_VK_ACCESS_SHADER_READ_BIT |
            RF_VK_ACCESS_SHADER_WRITE_BIT;
        impl->api.cmd_pipeline_barrier(r->command_buffer,
            RF_VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | RF_VK_PIPELINE_STAGE_TRANSFER_BIT,
            RF_VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
        {
            const uint32_t range[3] = { first, end, load };
            impl->api.cmd_push_constants(r->command_buffer, r->pipeline_layout,
                RF_VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(range), range);
        }
        {
        uint32_t timestamp=timestamp_begin(r,r->command_buffer,RF_GPU_TS_RASTER);
        impl->api.cmd_dispatch(r->command_buffer,
            (width + r->work_group_x - 1) / r->work_group_x,
            (height + r->work_group_y - 1) / r->work_group_y, 1);
        timestamp_end(r,r->command_buffer,timestamp);
        }
        if (post_enabled) {
            double post_start = now_ms();
            uint32_t timestamp;
            memset(&barrier, 0, sizeof(barrier));
            barrier.s_type = RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            barrier.src_access_mask = RF_VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dst_access_mask = RF_VK_ACCESS_SHADER_READ_BIT;
            impl->api.cmd_pipeline_barrier(r->command_buffer,
                RF_VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                RF_VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0,1,&barrier,0,NULL,0,NULL);
            impl->api.cmd_bind_pipeline(r->command_buffer,
                RF_VK_PIPELINE_BIND_POINT_COMPUTE,
                r->shared_color_enabled?r->post_image_pipeline:r->post_pipeline);
            timestamp=timestamp_begin(r,r->command_buffer,RF_GPU_TS_POST);
            impl->api.cmd_dispatch(r->command_buffer,
                (width+r->work_group_x-1)/r->work_group_x,
                (height+r->work_group_y-1)/r->work_group_y,1);
            timestamp_end(r,r->command_buffer,timestamp);
            if (native_timing) native_timing->post_raster_ms=now_ms()-post_start;
        }
        if (composite) {
            double composite_start = now_ms();
            uint32_t timestamp;
            memset(&barrier, 0, sizeof(barrier));
            barrier.s_type = RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            barrier.src_access_mask = RF_VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dst_access_mask = RF_VK_ACCESS_SHADER_READ_BIT |
                                      RF_VK_ACCESS_SHADER_WRITE_BIT;
            impl->api.cmd_pipeline_barrier(r->command_buffer,
                RF_VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                RF_VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 1, &barrier, 0, NULL, 0, NULL);
            impl->api.cmd_bind_pipeline(r->command_buffer,
                RF_VK_PIPELINE_BIND_POINT_COMPUTE, r->overlay_pipeline);
            timestamp=timestamp_begin(r,r->command_buffer,RF_GPU_TS_OVERLAY);
            impl->api.cmd_dispatch(r->command_buffer,
                (width + r->work_group_x - 1) / r->work_group_x,
                (height + r->work_group_y - 1) / r->work_group_y, 1);
            timestamp_end(r,r->command_buffer,timestamp);
            if (native_timing)
                native_timing->overlay_composite_ms = now_ms() - composite_start;
        }
        memset(&barrier, 0, sizeof(barrier));
        barrier.s_type = RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.src_access_mask = RF_VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dst_access_mask = RF_VK_ACCESS_TRANSFER_READ_BIT;
        impl->api.cmd_pipeline_barrier(r->command_buffer,
            RF_VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, RF_VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 1, &barrier, 0, NULL, 0, NULL);
        if (native_present) {
            uint32_t timestamp=timestamp_begin(r,r->command_buffer,RF_GPU_TS_PRESENT_COPY);
            if (r->pending_capture_color) {
                memset(copies, 0, sizeof(copies));
                copies[0].size = byte_size;
                impl->api.cmd_copy_buffer(r->command_buffer, presentation_color->buffer,
                    r->color_readback.buffer, 1, &copies[0]);
            }
            struct rf_vk_image_memory_barrier image_barrier;
            struct rf_vk_buffer_image_copy region;
            double copy_start = now_ms();
            memset(&image_barrier, 0, sizeof(image_barrier));
            image_barrier.s_type = RF_VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            image_barrier.old_layout = RF_VK_IMAGE_LAYOUT_UNDEFINED;
            image_barrier.new_layout = RF_VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            image_barrier.dst_access_mask = RF_VK_ACCESS_TRANSFER_WRITE_BIT;
            image_barrier.src_queue_family_index = 0xffffffffU;
            image_barrier.dst_queue_family_index = 0xffffffffU;
            image_barrier.image = impl->swapchain_images[swapchain_image];
            image_barrier.subresource_range.aspect_mask = RF_VK_IMAGE_ASPECT_COLOR_BIT;
            image_barrier.subresource_range.level_count = 1;
            image_barrier.subresource_range.layer_count = 1;
            impl->api.cmd_pipeline_barrier(r->command_buffer,
                RF_VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, RF_VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, NULL, 0, NULL, 1, &image_barrier);
            memset(&region, 0, sizeof(region));
            region.image_subresource.aspect_mask = RF_VK_IMAGE_ASPECT_COLOR_BIT;
            region.image_subresource.layer_count = 1;
            region.image_extent.width = width; region.image_extent.height = height;
            region.image_extent.depth = 1;
            impl->api.cmd_copy_buffer_to_image(r->command_buffer, presentation_color->buffer,
                impl->swapchain_images[swapchain_image],
                RF_VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            image_barrier.src_access_mask = RF_VK_ACCESS_TRANSFER_WRITE_BIT;
            image_barrier.dst_access_mask = 0;
            image_barrier.old_layout = RF_VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            image_barrier.new_layout = RF_VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            impl->api.cmd_pipeline_barrier(r->command_buffer,
                RF_VK_PIPELINE_STAGE_TRANSFER_BIT, RF_VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                0, 0, NULL, 0, NULL, 1, &image_barrier);
            timestamp_end(r,r->command_buffer,timestamp);
            native_timing->buffer_to_swapchain_ms = now_ms() - copy_start;
        } else if (final) {
            memset(copies, 0, sizeof(copies));
            copies[0].size = byte_size; copies[1].size = byte_size;
            impl->api.cmd_copy_buffer(r->command_buffer, presentation_color->buffer,
                                      r->color_readback.buffer, 1, &copies[0]);
            impl->api.cmd_copy_buffer(r->command_buffer, r->depth.buffer,
                                      r->depth_readback.buffer, 1, &copies[1]);
        }
        if (final) {
            if (native_present && present_fault_take(impl,
                    RF_GPU_PRESENT_FAULT_RECORD_FAILURE))
                goto failed;
            if (impl->api.end_command_buffer(r->command_buffer) != RF_VK_SUCCESS)
                goto failed;
            r->frame_recording = 0;
        }
    }
    if (!final) {
        if(load==RF_GPU_RASTER_CLEAR)r->segment_graphics_compatible=1;
        {
            const uint32_t *words=stream;
            for(uint32_t ci=first;ci<end;++ci) {
                const uint32_t *c=words+8+ci*24;
                if(c[0]==2U && c[4]>16384U)r->segment_graphics_compatible=0;
                if(c[0]>=3U && c[0]<=5U &&
                   (c[8]>16384U || c[11]>16384U || c[14]>16384U))
                    r->segment_graphics_compatible=0;
            }
        }
        r->segment_valid=1;
        snprintf(message,message_capacity,"GPU Raster V1 segment recorded");
        return 0;
    }
    {
        struct rf_vk_submit_info submit;
        if (!r->render_fence || r->render_in_flight || !impl->api.reset_fences ||
            impl->api.reset_fences(impl->device,1,&r->render_fence)!=RF_VK_SUCCESS)
            goto failed;
        memset(&submit, 0, sizeof(submit));
        submit.s_type = RF_VK_STRUCTURE_TYPE_SUBMIT_INFO;
        if (native_present) {
            /* The command buffer first transitions the acquired swapchain
             * image before the transfer copy.  Waiting only at TRANSFER does
             * not order that earlier layout transition against acquire. */
            static const rf_vk_flags wait_stage =
                RF_VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            submit.wait_semaphore_count = 1;
            submit.wait_semaphores = &r->acquire_semaphore;
            submit.wait_dst_stage_mask = &wait_stage;
            submit.signal_semaphore_count = 1;
            submit.signal_semaphores =
                &impl->present_images[swapchain_image].render_finished;
        }
        submit.command_buffer_count = 1;
        submit.command_buffers = &r->command_buffer;
        if (native_present &&
            (r->acquire_semaphore_state != RF_GPU_PRESENT_AUDIT_SIGNALED ||
             impl->present_images[swapchain_image].render_finished_state !=
                RF_GPU_PRESENT_AUDIT_REUSABLE)) {
            present_audit_poison(impl, r, swapchain_image,
                swapchain_image_acquired);
            return present_audit_fail("submit-semaphore-state");
        }
        segment_start = now_ms();
        if (present_fault_take(impl, RF_GPU_PRESENT_FAULT_SUBMIT_FAILURE) ||
            impl->api.queue_submit(impl->queue, 1, &submit, r->render_fence) != RF_VK_SUCCESS)
            goto failed;
        r->render_in_flight=1;
        r->submitted_frame = ++impl->next_frame_number;
        if (native_present) {
            r->acquire_semaphore_state = RF_GPU_PRESENT_AUDIT_REUSABLE;
            impl->present_images[swapchain_image].render_finished_state =
                RF_GPU_PRESENT_AUDIT_SIGNALED;
            impl->present_images[swapchain_image].submit_generation =
                ++impl->next_present_generation;
        }
        if (timing) timing->submit_ms = now_ms() - segment_start;
        if (!native_present || r->pending_capture_color) {
            segment_start = now_ms();
            result = raster_recycle_frame(r,5000000000ULL);
            if (result == 1) {
                snprintf(message, message_capacity,
                         "GPU Raster V1 fence timed out after 5 seconds");
                goto cleanup;
            }
            if (result != 0) goto failed;
            if (timing) timing->execution_wait_ms = now_ms() - segment_start;
        }
    }
    r->segment_valid = !final;
    if (final) {
        r->binned_stream = NULL; r->binned_stream_size = 0;
        r->uploaded_stream = NULL; r->uploaded_stream_size = 0;
        r->preflight_reuse = 0;
    }
    if (native_present) {
        struct rf_vk_present_info present;
        double present_start = now_ms();
        if (r->pending_capture_color) {
            if (impl->api.map_memory(impl->device, r->color_readback.memory, 0,
                    r->color_readback.allocation_size, 0, &mapped_color) != RF_VK_SUCCESS)
                goto failed;
            if (raster_invalidate(impl, &r->color_readback) < 0) goto failed;
            memcpy(r->pending_capture_color, mapped_color, (size_t)byte_size);
            impl->api.unmap_memory(impl->device, r->color_readback.memory);
            mapped_color = NULL;
        }
        memset(&present, 0, sizeof(present));
        present.s_type = RF_VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.wait_semaphore_count = 1;
        present.wait_semaphores =
            &impl->present_images[swapchain_image].render_finished;
        present.swapchain_count = 1; present.swapchains = &impl->swapchain;
        present.image_indices = &swapchain_image;
        if (present_fault_take(impl, RF_GPU_PRESENT_FAULT_PRESENT_OUT_OF_DATE))
            result = RF_VK_ERROR_OUT_OF_DATE_KHR;
        else if (impl->present_fault == RF_GPU_PRESENT_FAULT_PRESENT_SUBOPTIMAL &&
                 !impl->present_fault_triggered &&
                 impl->present_attempt == impl->present_fault_frame) {
            result = impl->api.queue_present(impl->queue, &present);
            if (result == RF_VK_SUCCESS && present_fault_take(impl,
                    RF_GPU_PRESENT_FAULT_PRESENT_SUBOPTIMAL))
                result = RF_VK_SUBOPTIMAL_KHR;
        }
        else
            result = impl->api.queue_present(impl->queue, &present);
        native_timing->present_ms = now_ms() - present_start;
        if (result == RF_VK_SUCCESS || result == RF_VK_SUBOPTIMAL_KHR) {
            if (impl->present_images[swapchain_image].render_finished_state !=
                    RF_GPU_PRESENT_AUDIT_SIGNALED) {
                present_audit_poison(impl, r, swapchain_image, 1);
                return present_audit_fail("present-wait-semaphore-not-signaled");
            }
            impl->present_images[swapchain_image].render_finished_state =
                RF_GPU_PRESENT_AUDIT_PENDING;
            impl->present_images[swapchain_image].state =
                RF_GPU_PRESENT_AUDIT_PENDING;
            impl->present_images[swapchain_image].present_generation =
                ++impl->next_present_generation;
            impl->outstanding_presents++;
            present_transaction_complete = 1;
        } else {
            present_audit_poison(impl, r, swapchain_image, 1);
        }
        native_timing->submit_ms = timing ? timing->submit_ms : 0.0;
        native_timing->gpu_raster_ms = timing ? timing->execution_wait_ms : 0.0;
        native_timing->total_ms = now_ms() - total_start;
        native_timing->color_readback_bytes = 0;
        native_timing->cpu_framebuffer_copy_bytes = 0;
        native_timing->format = impl->swapchain_format;
        native_timing->present_mode = impl->present_mode;
        native_timing->image_count = impl->swapchain_image_count;
        native_timing->width = impl->swapchain_width;
        native_timing->height = impl->swapchain_height;
        present_audit_snapshot(impl, r, swapchain_image, native_timing,
            image_reacquired ? RF_GPU_PRESENT_COMPLETION_IMAGE_REACQUIRED :
            RF_GPU_PRESENT_COMPLETION_NONE);
        if (result == RF_VK_ERROR_SURFACE_LOST_KHR) {
            if (raster_surface_recreate(backend_context, impl) < 0)
                return -1;
        } else if (result == RF_VK_ERROR_OUT_OF_DATE_KHR ||
                   result == RF_VK_SUBOPTIMAL_KHR) {
            if (presenter_swapchain_create(impl, width, height) < 0)
                return -1;
        }
        else if (result != RF_VK_SUCCESS) return -1;
        snprintf(message, message_capacity, "GPU Raster V1 native-presented");
        return 0;
    }
    segment_start = now_ms();
    if (impl->api.map_memory(impl->device, r->color_readback.memory, 0,
            r->color_readback.allocation_size, 0, &mapped_color) != RF_VK_SUCCESS ||
        impl->api.map_memory(impl->device, r->depth_readback.memory, 0,
            r->depth_readback.allocation_size, 0, &mapped_depth) != RF_VK_SUCCESS ||
        raster_invalidate(impl, &r->color_readback) < 0 ||
        raster_invalidate(impl, &r->depth_readback) < 0) goto failed;
    for (y = 0; y < height; ++y) {
        memcpy(color + (uint64_t)y * color_stride,
               (const uint32_t *)mapped_color + (uint64_t)y * width,
               (size_t)width * 4);
        memcpy(depth + (uint64_t)y * depth_stride,
               (const int32_t *)mapped_depth + (uint64_t)y * width,
               (size_t)width * 4);
    }
    impl->api.unmap_memory(impl->device, r->depth_readback.memory);
    impl->api.unmap_memory(impl->device, r->color_readback.memory);
    if (timing) {
        timing->readback_ms = now_ms() - segment_start;
        timing->total_ms = now_ms() - total_start;
    }
    snprintf(message, message_capacity, "GPU Raster V1 rendered");
    return 0;
failed:
    if (native_present && swapchain_image_acquired &&
        !present_transaction_complete)
        present_audit_poison(impl, r, swapchain_image,
            swapchain_image < impl->swapchain_image_count);
    snprintf(message, message_capacity, "Vulkan Raster V1 operation failed");
cleanup:
    r->frame_recording = 0;
    r->segment_valid = 0;
    r->binned_stream = NULL; r->binned_stream_size = 0;
    r->uploaded_stream = NULL; r->uploaded_stream_size = 0;
    r->preflight_reuse = 0;
    if (mapped_depth) impl->api.unmap_memory(impl->device, r->depth_readback.memory);
    if (mapped_color) impl->api.unmap_memory(impl->device, r->color_readback.memory);
    return -1;
}

static int raster_render(void *context, void *raster,
    const void *stream, unsigned long stream_size,
    const void *texture_descs, unsigned int texture_count,
    const void *texture_texels, unsigned long texture_bytes,
    unsigned int *color, int *depth, unsigned int width, unsigned int height,
    unsigned int color_stride, unsigned int depth_stride,
    struct rf_gpu_raster_timing *timing, char *message, unsigned long capacity)
{
    return raster_render_range(context, raster, stream, stream_size,
        texture_descs, texture_count, texture_texels, texture_bytes,
        color, depth, width, height, color_stride, depth_stride, timing,
        message, capacity, 0, UINT32_MAX, RF_GPU_RASTER_CLEAR, 1);
}

int rf_gpu_vulkan_raster_segment(struct rf_gpu_vulkan_context *context,
    void *raster, const void *stream, unsigned long stream_size,
    const void *texture_descs, unsigned int texture_count,
    const void *texture_texels, unsigned long texture_bytes,
    unsigned int first, unsigned int end, enum rf_gpu_raster_load load,
    int final, unsigned int *color, int *depth,
    unsigned int width, unsigned int height,
    unsigned int color_stride, unsigned int depth_stride,
    char *message, unsigned long capacity)
{
    if (!message || !capacity || (final != 0 && final != 1) ||
        end == UINT32_MAX || (final && (!color || !depth)) ||
        (!final && (color || depth))) return -1;
    return raster_render_range(context, raster, stream, stream_size,
        texture_descs, texture_count, texture_texels, texture_bytes,
        color, depth, width, height, color_stride, depth_stride, NULL,
        message, capacity, first, end, (uint32_t)load, final);
}

int rf_gpu_vulkan_raster_segment_present(struct rf_gpu_vulkan_context *context,
    void *raster, const void *stream, unsigned long stream_size,
    const void *texture_descs, unsigned int texture_count,
    const void *texture_texels, unsigned long texture_bytes,
    unsigned int first, unsigned int end, enum rf_gpu_raster_load load,
    const unsigned int *overlay_color, const unsigned char *overlay_coverage,
    unsigned int overlay_stride, unsigned int coverage_stride,
    unsigned int width, unsigned int height,
    struct rf_gpu_native_present_timing *timing,
    unsigned int *capture_color,
    char *message, unsigned long capacity)
{
    struct rf_gpu_vulkan_raster *r = raster;
    uint64_t pixels = (uint64_t)width * height;
    const uint32_t *colors;
    const unsigned char *coverage;
    int packed = 0;
    int result;
    if (!r || !timing || !message || !capacity || !overlay_color ||
        !overlay_coverage || !width || !height || pixels > SIZE_MAX / 4 ||
        overlay_stride < width || coverage_stride < width ||
        first > end || end == UINT32_MAX) return -1;
    colors = overlay_color;
    coverage = overlay_coverage;
    /* The normal Core path already owns tightly packed, synchronous overlay
     * buffers.  Borrow them directly instead of allocating and copying a
     * full color and coverage plane every frame.  Odd pixel counts still
     * need a padded coverage copy for the 32-bit storage-buffer upload. */
    if (overlay_stride != width || coverage_stride != width || (pixels & 3U)) {
        uint32_t *packed_colors = malloc((size_t)pixels * 4);
        unsigned char *packed_coverage = calloc(1, (size_t)((pixels + 3U) & ~3ULL));
        if (!packed_colors || !packed_coverage) {
            free(packed_colors); free(packed_coverage); return -1;
        }
        for (unsigned int y = 0; y < height; ++y) {
            memcpy(packed_colors + (size_t)y * width,
                overlay_color + (size_t)y * overlay_stride, (size_t)width * 4);
            memcpy(packed_coverage + (size_t)y * width,
                overlay_coverage + (size_t)y * coverage_stride, width);
        }
        colors = packed_colors;
        coverage = packed_coverage;
        packed = 1;
    }
    memset(timing, 0, sizeof(*timing));
    r->pending_present_timing = timing;
    r->pending_capture_color = capture_color;
    r->pending_overlay_color = colors;
    r->pending_overlay_coverage = coverage;
    r->pending_overlay_stride = width;
    r->pending_coverage_stride = width;
    result = raster_render_range(context, raster, stream, stream_size,
        texture_descs, texture_count, texture_texels, texture_bytes,
        NULL, NULL, width, height, 0, 0, NULL, message, capacity,
        first, end, (uint32_t)load, 1);
    r->pending_present_timing = NULL;
    r->pending_capture_color = NULL;
    r->pending_overlay_color = NULL;
    r->pending_overlay_coverage = NULL;
    if (packed) { free((void *)colors); free((void *)coverage); }
    return result;
}

static int raster_set_post(void *context, void *raster,
                           const struct rf_gpu_post_params_v1 *params)
{
    struct rf_gpu_vulkan_context *backend_context=context;
    struct rf_gpu_vulkan_impl *impl=backend_context?backend_context->implementation:NULL;
    struct rf_gpu_vulkan_raster *r=raster;
    if(!impl||!r||r->owner!=impl||!params||params->mode>RF_GPU_POST_DEPTH_FOG_V0)
        return -1;
    if(params->mode!=RF_GPU_POST_DISABLED && !r->post_pipeline)return -1;
    if(params->mode==RF_GPU_POST_DEPTH_FOG_V0 &&
       (params->fog_far_inv_z>=params->fog_near_inv_z||params->max_density_q8>256))
        return -1;
    r->post=*params;
    r->post.fog_color|=0xff000000U;
    return 0;
}

static void raster_set_full_scan_diagnostic(void *context, void *raster,
                                             int enabled)
{
    struct rf_gpu_vulkan_context *backend_context=context;
    struct rf_gpu_vulkan_raster *r=raster;
    if(backend_context && r && r->owner==backend_context->implementation)
        r->full_scan_diagnostic=enabled!=0;
}

static int raster_present(void *context, void *raster,
                         const void *stream, unsigned long stream_size,
                         const void *texture_descs, unsigned int texture_count,
                         const void *texture_texels, unsigned long texture_bytes,
                         const unsigned int *overlay_color,
                         const unsigned char *overlay_coverage,
                         unsigned int overlay_stride,
                         unsigned int coverage_stride,
                         unsigned int width, unsigned int height,
                         struct rf_gpu_raster_timing *raster_timing,
                         struct rf_gpu_native_present_timing *present_timing,
                         char *message, unsigned long message_capacity)
{
    struct rf_gpu_vulkan_raster *r = raster;
    uint32_t *packed_color = NULL;
    unsigned char *packed_coverage = NULL;
    uint64_t pixels = (uint64_t)width * height;
    unsigned int y;
    int result;
    if (!r || !present_timing || !overlay_color || !overlay_coverage ||
        overlay_stride < width || coverage_stride < width) return -1;
    packed_color = malloc((size_t)pixels * 4);
    packed_coverage = calloc(1, (size_t)((pixels + 3U) & ~3ULL));
    if (!packed_color || !packed_coverage) {
        free(packed_color); free(packed_coverage); return -1;
    }
    for (y = 0; y < height; ++y) {
        memcpy(packed_color + (uint64_t)y * width,
               overlay_color + (uint64_t)y * overlay_stride,
               (size_t)width * 4);
        memcpy(packed_coverage + (uint64_t)y * width,
               overlay_coverage + (uint64_t)y * coverage_stride, width);
    }
    memset(present_timing, 0, sizeof(*present_timing));
    r->pending_present_timing = present_timing;
    r->pending_overlay_color = packed_color;
    r->pending_overlay_coverage = packed_coverage;
    r->pending_overlay_stride = width;
    r->pending_coverage_stride = width;
    result = raster_render(context, raster, stream, stream_size, texture_descs,
        texture_count, texture_texels, texture_bytes, NULL, NULL, width, height,
        0, 0, raster_timing, message, message_capacity);
    r->pending_present_timing = NULL;
    r->pending_overlay_color = NULL;
    r->pending_overlay_coverage = NULL;
    free(packed_color); free(packed_coverage);
    return result;
}

static int raster_composite_diagnostic(void *context, void *raster,
                         const void *stream, unsigned long stream_size,
                         const unsigned int *overlay_color,
                         const unsigned char *overlay_coverage,
                         unsigned int overlay_stride,
                         unsigned int coverage_stride,
                         unsigned int *color, int *depth,
                         unsigned int width, unsigned int height,
                         unsigned int color_stride,
                         unsigned int depth_stride,
                         char *message, unsigned long message_capacity)
{
    struct rf_gpu_vulkan_raster *r = raster;
    uint32_t *packed_color = NULL;
    unsigned char *packed_coverage = NULL;
    uint64_t pixels = (uint64_t)width * height;
    unsigned int y;
    int result;
    if (!r || !overlay_color || !overlay_coverage || !color || !depth ||
        overlay_stride < width || coverage_stride < width) return -1;
    packed_color = malloc((size_t)pixels * 4);
    packed_coverage = calloc(1, (size_t)((pixels + 3U) & ~3ULL));
    if (!packed_color || !packed_coverage) {
        free(packed_color); free(packed_coverage); return -1;
    }
    for (y = 0; y < height; ++y) {
        memcpy(packed_color + (uint64_t)y * width,
               overlay_color + (uint64_t)y * overlay_stride,
               (size_t)width * 4);
        memcpy(packed_coverage + (uint64_t)y * width,
               overlay_coverage + (uint64_t)y * coverage_stride, width);
    }
    r->pending_overlay_color = packed_color;
    r->pending_overlay_coverage = packed_coverage;
    result = raster_render(context, raster, stream, stream_size, NULL, 0,
        NULL, 0, color, depth, width, height, color_stride, depth_stride,
        NULL, message, message_capacity);
    r->pending_overlay_color = NULL;
    r->pending_overlay_coverage = NULL;
    free(packed_color); free(packed_coverage);
    return result;
}

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
    if (impl->device && impl->api.destroy_device)
        impl->api.destroy_device(impl->device, NULL);
    if (impl->surface && impl->api.destroy_surface)
        impl->api.destroy_surface(impl->instance, impl->surface, NULL);
    if (impl->instance && impl->api.destroy_instance)
        impl->api.destroy_instance(impl->instance, NULL);
    api_close(&impl->api);
    free(impl);
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
    impl = calloc(1, sizeof(*impl));
    if (!impl) {
        snprintf(message, message_capacity, "out of memory creating Vulkan backend");
        return RF_GPU_BACKEND_FAILED;
    }
    impl->present_fault = backend_context->present_fault;
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
    app_info.api_version = RF_VK_MAKE_VERSION(1, 0, 0);
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
        device_info.s_type = RF_VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_info.queue_create_info_count = 1;
        device_info.queue_create_infos = &queue_info;
        device_info.enabled_features = &enabled_features;
        if (want_present) {
            static const char *extensions[] = { "VK_KHR_swapchain" };
            device_info.enabled_extension_count = 1;
            device_info.enabled_extension_names = extensions;
        }
        result = api->create_device(selected_device, &device_info, NULL,
                                    &impl->device);
        if ((result != RF_VK_SUCCESS || !impl->device) && want_present) {
            want_present = 0;
            device_info.enabled_extension_count = 0;
            device_info.enabled_extension_names = NULL;
            impl->device = NULL;
            result = api->create_device(selected_device, &device_info, NULL,
                                        &impl->device);
        }
        if (result != RF_VK_SUCCESS || !impl->device) goto done;
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
    backend_init,
    backend_shutdown,
    framebuffer_create,
    framebuffer_destroy,
    framebuffer_render,
    raster_create,
    raster_destroy,
    raster_render,
    raster_set_full_scan_diagnostic,
    backend_set_native_window,
    raster_present,
    raster_composite_diagnostic,
    raster_set_post
};
