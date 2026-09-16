#ifndef RF_VULKAN_MIN_H
#define RF_VULKAN_MIN_H

/*
 * Minimal Vulkan 1.0 ABI used by rf-gpu-probe.
 *
 * This is deliberately not a replacement for the Khronos Vulkan headers.
 * Keeping the probe's ABI surface here makes the hosted discovery tool build
 * without a Vulkan SDK.  Add declarations only when the implementation starts
 * using them, and verify every addition against the Vulkan registry.
 */

#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32)
#define RF_VK_CALL __stdcall
#else
#define RF_VK_CALL
#endif

#define RF_VK_MAKE_VERSION(major, minor, patch) \
    (((uint32_t)(major) << 22) | ((uint32_t)(minor) << 12) | (uint32_t)(patch))
#define RF_VK_VERSION_MAJOR(version) ((uint32_t)(version) >> 22)
#define RF_VK_VERSION_MINOR(version) (((uint32_t)(version) >> 12) & 0x3ffU)
#define RF_VK_VERSION_PATCH(version) ((uint32_t)(version) & 0xfffU)

#define RF_VK_SUCCESS 0
#define RF_VK_INCOMPLETE 5
#define RF_VK_STRUCTURE_TYPE_APPLICATION_INFO 0
#define RF_VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO 1
#define RF_VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO 2
#define RF_VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO 3
#define RF_VK_STRUCTURE_TYPE_SUBMIT_INFO 4
#define RF_VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO 5
#define RF_VK_STRUCTURE_TYPE_FENCE_CREATE_INFO 8
#define RF_VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO 12
#define RF_VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO 16
#define RF_VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO 18
#define RF_VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO 29
#define RF_VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO 30
#define RF_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO 32
#define RF_VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO 33
#define RF_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO 34
#define RF_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET 35
#define RF_VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO 39
#define RF_VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO 40
#define RF_VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO 42
#define RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER 46
#define RF_VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE 6

#define RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT 0x00000020U
#define RF_VK_BUFFER_USAGE_TRANSFER_SRC_BIT 0x00000001U
#define RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT 0x00000002U
#define RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT 0x00000001U
#define RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT 0x00000002U
#define RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT 0x00000004U
#define RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER 7
#define RF_VK_SHADER_STAGE_COMPUTE_BIT 0x00000020U
#define RF_VK_COMMAND_BUFFER_LEVEL_PRIMARY 0
#define RF_VK_PIPELINE_BIND_POINT_COMPUTE 1
#define RF_VK_SHARING_MODE_EXCLUSIVE 0
#define RF_VK_WHOLE_SIZE (~(uint64_t)0)
#define RF_VK_TRUE 1
#define RF_VK_TIMEOUT 2
#define RF_VK_ACCESS_SHADER_WRITE_BIT 0x00000040U
#define RF_VK_ACCESS_TRANSFER_READ_BIT 0x00000800U
#define RF_VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT 0x00000800U
#define RF_VK_PIPELINE_STAGE_TRANSFER_BIT 0x00001000U

#define RF_VK_QUEUE_GRAPHICS_BIT 0x00000001U
#define RF_VK_QUEUE_COMPUTE_BIT  0x00000002U
#define RF_VK_QUEUE_TRANSFER_BIT 0x00000004U

#define RF_VK_PHYSICAL_DEVICE_TYPE_OTHER 0
#define RF_VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU 1
#define RF_VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU 2
#define RF_VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU 3
#define RF_VK_PHYSICAL_DEVICE_TYPE_CPU 4

#define RF_VK_MAX_PHYSICAL_DEVICE_NAME_SIZE 256
/* VkPhysicalDeviceProperties is currently 824 bytes on the Vulkan 1.0 ABI.
 * Reserve considerably more so newer headers may append fields without a
 * write past the probe's storage.  The prefix read below is stable. */
#define RF_VK_PHYSICAL_DEVICE_PROPERTIES_STORAGE_SIZE 2048

typedef int32_t rf_vk_result;
typedef uint32_t rf_vk_flags;
typedef uint32_t rf_vk_bool32;
typedef struct rf_vk_instance_t *rf_vk_instance;
typedef struct rf_vk_physical_device_t *rf_vk_physical_device;
typedef struct rf_vk_device_t *rf_vk_device;
typedef struct rf_vk_queue_t *rf_vk_queue;
typedef struct rf_vk_device_memory_t *rf_vk_device_memory;
typedef struct rf_vk_buffer_t *rf_vk_buffer;
typedef struct rf_vk_descriptor_set_layout_t *rf_vk_descriptor_set_layout;
typedef struct rf_vk_descriptor_pool_t *rf_vk_descriptor_pool;
typedef struct rf_vk_descriptor_set_t *rf_vk_descriptor_set;
typedef struct rf_vk_pipeline_layout_t *rf_vk_pipeline_layout;
typedef struct rf_vk_shader_module_t *rf_vk_shader_module;
typedef struct rf_vk_pipeline_t *rf_vk_pipeline;
typedef struct rf_vk_pipeline_cache_t *rf_vk_pipeline_cache;
typedef struct rf_vk_command_pool_t *rf_vk_command_pool;
typedef struct rf_vk_command_buffer_t *rf_vk_command_buffer;
typedef struct rf_vk_fence_t *rf_vk_fence;
typedef void (RF_VK_CALL *rf_vk_void_function)(void);

struct rf_vk_application_info {
    uint32_t s_type;
    const void *next;
    const char *application_name;
    uint32_t application_version;
    const char *engine_name;
    uint32_t engine_version;
    uint32_t api_version;
};

struct rf_vk_instance_create_info {
    uint32_t s_type;
    const void *next;
    rf_vk_flags flags;
    const struct rf_vk_application_info *application_info;
    uint32_t enabled_layer_count;
    const char *const *enabled_layer_names;
    uint32_t enabled_extension_count;
    const char *const *enabled_extension_names;
};

struct rf_vk_device_queue_create_info {
    uint32_t s_type;
    const void *next;
    rf_vk_flags flags;
    uint32_t queue_family_index;
    uint32_t queue_count;
    const float *queue_priorities;
};

struct rf_vk_device_create_info {
    uint32_t s_type;
    const void *next;
    rf_vk_flags flags;
    uint32_t queue_create_info_count;
    const struct rf_vk_device_queue_create_info *queue_create_infos;
    uint32_t enabled_layer_count;
    const char *const *enabled_layer_names;
    uint32_t enabled_extension_count;
    const char *const *enabled_extension_names;
    const void *enabled_features;
};

/* Stable prefix of VkPhysicalDeviceProperties. */
struct rf_vk_physical_device_properties_prefix {
    uint32_t api_version;
    uint32_t driver_version;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t device_type;
    char device_name[RF_VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
};

struct rf_vk_queue_family_properties {
    rf_vk_flags queue_flags;
    uint32_t queue_count;
    uint32_t timestamp_valid_bits;
    struct {
        uint32_t width;
        uint32_t height;
        uint32_t depth;
    } min_image_transfer_granularity;
};

struct rf_vk_memory_type { rf_vk_flags property_flags; uint32_t heap_index; };
struct rf_vk_memory_heap { uint64_t size; rf_vk_flags flags; };
struct rf_vk_physical_device_memory_properties {
    uint32_t memory_type_count;
    struct rf_vk_memory_type memory_types[32];
    uint32_t memory_heap_count;
    struct rf_vk_memory_heap memory_heaps[16];
};
struct rf_vk_buffer_create_info {
    uint32_t s_type; const void *next; rf_vk_flags flags; uint64_t size;
    rf_vk_flags usage; uint32_t sharing_mode; uint32_t queue_family_index_count;
    const uint32_t *queue_family_indices;
};
struct rf_vk_memory_requirements {
    uint64_t size; uint64_t alignment; uint32_t memory_type_bits;
};
struct rf_vk_memory_allocate_info {
    uint32_t s_type; const void *next; uint64_t allocation_size;
    uint32_t memory_type_index;
};
struct rf_vk_mapped_memory_range {
    uint32_t s_type; const void *next; rf_vk_device_memory memory;
    uint64_t offset; uint64_t size;
};
struct rf_vk_memory_barrier {
    uint32_t s_type; const void *next; rf_vk_flags src_access_mask;
    rf_vk_flags dst_access_mask;
};
struct rf_vk_buffer_copy { uint64_t src_offset; uint64_t dst_offset; uint64_t size; };
struct rf_vk_descriptor_set_layout_binding {
    uint32_t binding; uint32_t descriptor_type; uint32_t descriptor_count;
    rf_vk_flags stage_flags; const void *immutable_samplers;
};
struct rf_vk_descriptor_set_layout_create_info {
    uint32_t s_type; const void *next; rf_vk_flags flags;
    uint32_t binding_count;
    const struct rf_vk_descriptor_set_layout_binding *bindings;
};
struct rf_vk_descriptor_pool_size {
    uint32_t type; uint32_t descriptor_count;
};
struct rf_vk_descriptor_pool_create_info {
    uint32_t s_type; const void *next; rf_vk_flags flags;
    uint32_t max_sets; uint32_t pool_size_count;
    const struct rf_vk_descriptor_pool_size *pool_sizes;
};
struct rf_vk_descriptor_set_allocate_info {
    uint32_t s_type; const void *next; rf_vk_descriptor_pool descriptor_pool;
    uint32_t descriptor_set_count;
    const rf_vk_descriptor_set_layout *set_layouts;
};
struct rf_vk_descriptor_buffer_info {
    rf_vk_buffer buffer; uint64_t offset; uint64_t range;
};
struct rf_vk_write_descriptor_set {
    uint32_t s_type; const void *next; rf_vk_descriptor_set dst_set;
    uint32_t dst_binding; uint32_t dst_array_element;
    uint32_t descriptor_count; uint32_t descriptor_type;
    const void *image_info;
    const struct rf_vk_descriptor_buffer_info *buffer_info;
    const void *texel_buffer_view;
};
struct rf_vk_shader_module_create_info {
    uint32_t s_type; const void *next; rf_vk_flags flags;
    size_t code_size; const uint32_t *code;
};
struct rf_vk_pipeline_layout_create_info {
    uint32_t s_type; const void *next; rf_vk_flags flags;
    uint32_t set_layout_count;
    const rf_vk_descriptor_set_layout *set_layouts;
    uint32_t push_constant_range_count; const void *push_constant_ranges;
};
struct rf_vk_pipeline_shader_stage_create_info {
    uint32_t s_type; const void *next; rf_vk_flags flags;
    rf_vk_flags stage; rf_vk_shader_module module; const char *name;
    const void *specialization_info;
};
struct rf_vk_compute_pipeline_create_info {
    uint32_t s_type; const void *next; rf_vk_flags flags;
    struct rf_vk_pipeline_shader_stage_create_info stage;
    rf_vk_pipeline_layout layout; rf_vk_pipeline base_pipeline_handle;
    int32_t base_pipeline_index;
};
struct rf_vk_command_pool_create_info {
    uint32_t s_type; const void *next; rf_vk_flags flags;
    uint32_t queue_family_index;
};
struct rf_vk_command_buffer_allocate_info {
    uint32_t s_type; const void *next; rf_vk_command_pool command_pool;
    uint32_t level; uint32_t command_buffer_count;
};
struct rf_vk_command_buffer_begin_info {
    uint32_t s_type; const void *next; rf_vk_flags flags;
    const void *inheritance_info;
};
struct rf_vk_submit_info {
    uint32_t s_type; const void *next; uint32_t wait_semaphore_count;
    const void *wait_semaphores; const rf_vk_flags *wait_dst_stage_mask;
    uint32_t command_buffer_count;
    const rf_vk_command_buffer *command_buffers;
    uint32_t signal_semaphore_count; const void *signal_semaphores;
};
struct rf_vk_fence_create_info {
    uint32_t s_type; const void *next; rf_vk_flags flags;
};

typedef rf_vk_void_function (RF_VK_CALL *rf_vk_get_instance_proc_addr_fn)(
    rf_vk_instance instance, const char *name);
typedef rf_vk_result (RF_VK_CALL *rf_vk_enumerate_instance_version_fn)(
    uint32_t *api_version);
typedef rf_vk_result (RF_VK_CALL *rf_vk_create_instance_fn)(
    const struct rf_vk_instance_create_info *create_info,
    const void *allocator, rf_vk_instance *instance);
typedef void (RF_VK_CALL *rf_vk_destroy_instance_fn)(
    rf_vk_instance instance, const void *allocator);
typedef rf_vk_result (RF_VK_CALL *rf_vk_enumerate_physical_devices_fn)(
    rf_vk_instance instance, uint32_t *count,
    rf_vk_physical_device *devices);
typedef void (RF_VK_CALL *rf_vk_get_physical_device_properties_fn)(
    rf_vk_physical_device device, void *properties);
typedef void (RF_VK_CALL *rf_vk_get_physical_device_queue_family_properties_fn)(
    rf_vk_physical_device device, uint32_t *count,
    struct rf_vk_queue_family_properties *properties);
typedef rf_vk_result (RF_VK_CALL *rf_vk_create_device_fn)(
    rf_vk_physical_device physical_device,
    const struct rf_vk_device_create_info *create_info,
    const void *allocator, rf_vk_device *device);
typedef void (RF_VK_CALL *rf_vk_destroy_device_fn)(
    rf_vk_device device, const void *allocator);
typedef void (RF_VK_CALL *rf_vk_get_device_queue_fn)(
    rf_vk_device device, uint32_t queue_family_index,
    uint32_t queue_index, rf_vk_queue *queue);

typedef void (RF_VK_CALL *rf_vk_get_physical_device_memory_properties_fn)(
    rf_vk_physical_device, struct rf_vk_physical_device_memory_properties *);
typedef rf_vk_result (RF_VK_CALL *rf_vk_create_buffer_fn)(rf_vk_device,
    const struct rf_vk_buffer_create_info *, const void *, rf_vk_buffer *);
typedef void (RF_VK_CALL *rf_vk_destroy_buffer_fn)(rf_vk_device, rf_vk_buffer,
    const void *);
typedef void (RF_VK_CALL *rf_vk_get_buffer_memory_requirements_fn)(rf_vk_device,
    rf_vk_buffer, struct rf_vk_memory_requirements *);
typedef rf_vk_result (RF_VK_CALL *rf_vk_allocate_memory_fn)(rf_vk_device,
    const struct rf_vk_memory_allocate_info *, const void *, rf_vk_device_memory *);
typedef void (RF_VK_CALL *rf_vk_free_memory_fn)(rf_vk_device,
    rf_vk_device_memory, const void *);
typedef rf_vk_result (RF_VK_CALL *rf_vk_bind_buffer_memory_fn)(rf_vk_device,
    rf_vk_buffer, rf_vk_device_memory, uint64_t);
typedef rf_vk_result (RF_VK_CALL *rf_vk_map_memory_fn)(rf_vk_device,
    rf_vk_device_memory, uint64_t, uint64_t, rf_vk_flags, void **);
typedef void (RF_VK_CALL *rf_vk_unmap_memory_fn)(rf_vk_device,
    rf_vk_device_memory);
typedef rf_vk_result (RF_VK_CALL *rf_vk_invalidate_mapped_memory_ranges_fn)(
    rf_vk_device, uint32_t, const struct rf_vk_mapped_memory_range *);
typedef rf_vk_result (RF_VK_CALL *rf_vk_create_descriptor_set_layout_fn)(
    rf_vk_device, const struct rf_vk_descriptor_set_layout_create_info *,
    const void *, rf_vk_descriptor_set_layout *);
typedef void (RF_VK_CALL *rf_vk_destroy_descriptor_set_layout_fn)(rf_vk_device,
    rf_vk_descriptor_set_layout, const void *);
typedef rf_vk_result (RF_VK_CALL *rf_vk_create_descriptor_pool_fn)(rf_vk_device,
    const struct rf_vk_descriptor_pool_create_info *, const void *,
    rf_vk_descriptor_pool *);
typedef void (RF_VK_CALL *rf_vk_destroy_descriptor_pool_fn)(rf_vk_device,
    rf_vk_descriptor_pool, const void *);
typedef rf_vk_result (RF_VK_CALL *rf_vk_allocate_descriptor_sets_fn)(
    rf_vk_device, const struct rf_vk_descriptor_set_allocate_info *,
    rf_vk_descriptor_set *);
typedef void (RF_VK_CALL *rf_vk_update_descriptor_sets_fn)(rf_vk_device,
    uint32_t, const struct rf_vk_write_descriptor_set *, uint32_t, const void *);
typedef rf_vk_result (RF_VK_CALL *rf_vk_create_shader_module_fn)(rf_vk_device,
    const struct rf_vk_shader_module_create_info *, const void *,
    rf_vk_shader_module *);
typedef void (RF_VK_CALL *rf_vk_destroy_shader_module_fn)(rf_vk_device,
    rf_vk_shader_module, const void *);
typedef rf_vk_result (RF_VK_CALL *rf_vk_create_pipeline_layout_fn)(rf_vk_device,
    const struct rf_vk_pipeline_layout_create_info *, const void *,
    rf_vk_pipeline_layout *);
typedef void (RF_VK_CALL *rf_vk_destroy_pipeline_layout_fn)(rf_vk_device,
    rf_vk_pipeline_layout, const void *);
typedef rf_vk_result (RF_VK_CALL *rf_vk_create_compute_pipelines_fn)(rf_vk_device,
    rf_vk_pipeline_cache, uint32_t,
    const struct rf_vk_compute_pipeline_create_info *, const void *, rf_vk_pipeline *);
typedef void (RF_VK_CALL *rf_vk_destroy_pipeline_fn)(rf_vk_device,
    rf_vk_pipeline, const void *);
typedef rf_vk_result (RF_VK_CALL *rf_vk_create_command_pool_fn)(rf_vk_device,
    const struct rf_vk_command_pool_create_info *, const void *, rf_vk_command_pool *);
typedef void (RF_VK_CALL *rf_vk_destroy_command_pool_fn)(rf_vk_device,
    rf_vk_command_pool, const void *);
typedef rf_vk_result (RF_VK_CALL *rf_vk_allocate_command_buffers_fn)(rf_vk_device,
    const struct rf_vk_command_buffer_allocate_info *, rf_vk_command_buffer *);
typedef rf_vk_result (RF_VK_CALL *rf_vk_begin_command_buffer_fn)(
    rf_vk_command_buffer, const struct rf_vk_command_buffer_begin_info *);
typedef rf_vk_result (RF_VK_CALL *rf_vk_end_command_buffer_fn)(rf_vk_command_buffer);
typedef void (RF_VK_CALL *rf_vk_cmd_bind_pipeline_fn)(rf_vk_command_buffer,
    uint32_t, rf_vk_pipeline);
typedef void (RF_VK_CALL *rf_vk_cmd_bind_descriptor_sets_fn)(rf_vk_command_buffer,
    uint32_t, rf_vk_pipeline_layout, uint32_t, uint32_t,
    const rf_vk_descriptor_set *, uint32_t, const uint32_t *);
typedef void (RF_VK_CALL *rf_vk_cmd_dispatch_fn)(rf_vk_command_buffer,
    uint32_t, uint32_t, uint32_t);
typedef void (RF_VK_CALL *rf_vk_cmd_pipeline_barrier_fn)(rf_vk_command_buffer,
    rf_vk_flags, rf_vk_flags, rf_vk_flags, uint32_t,
    const struct rf_vk_memory_barrier *, uint32_t, const void *, uint32_t,
    const void *);
typedef void (RF_VK_CALL *rf_vk_cmd_copy_buffer_fn)(rf_vk_command_buffer,
    rf_vk_buffer, rf_vk_buffer, uint32_t, const struct rf_vk_buffer_copy *);
typedef rf_vk_result (RF_VK_CALL *rf_vk_create_fence_fn)(rf_vk_device,
    const struct rf_vk_fence_create_info *, const void *, rf_vk_fence *);
typedef void (RF_VK_CALL *rf_vk_destroy_fence_fn)(rf_vk_device, rf_vk_fence,
    const void *);
typedef rf_vk_result (RF_VK_CALL *rf_vk_queue_submit_fn)(rf_vk_queue, uint32_t,
    const struct rf_vk_submit_info *, rf_vk_fence);
typedef rf_vk_result (RF_VK_CALL *rf_vk_wait_for_fences_fn)(rf_vk_device,
    uint32_t, const rf_vk_fence *, rf_vk_bool32, uint64_t);

#endif
