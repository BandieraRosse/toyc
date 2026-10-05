/* SDK-free Vulkan 1.2 + KHR ray query ABI subset. Values/layouts follow
 * Khronos Vulkan-Headers; no ray tracing pipeline or shader binding table. */
#ifndef RF_VULKAN_RAY_MIN_H
#define RF_VULKAN_RAY_MIN_H
#include "rf_vulkan_min.h"
#define RF_RT_ADDRESS_USAGE 0x00020000U
#define RF_RT_INPUT_USAGE 0x00080000U
#define RF_RT_STORAGE_USAGE 0x00100000U
#define RF_RT_BUILD_STAGE 0x02000000U
#define RF_RT_READ_ACCESS 0x00200000U
#define RF_RT_WRITE_ACCESS 0x00400000U
#define RF_RT_DESCRIPTOR 1000150000U
typedef struct rf_rt_as_t *rf_rt_as;
struct rf_rt_extensions { char name[256]; uint32_t version; };
struct rf_rt_features2 { uint32_t s_type; void *next; struct rf_vk_physical_device_features features; };
struct rf_rt_properties2 { uint32_t s_type; void *next; struct rf_vk_physical_device_properties properties; };
struct rf_rt_as_features { uint32_t s_type; void *next; uint32_t enabled,capture_replay,indirect,host,update_after_bind; };
struct rf_rt_query_features { uint32_t s_type; void *next; uint32_t enabled; };
struct rf_rt_address_features { uint32_t s_type; void *next; uint32_t enabled,capture_replay,multi_device; };
struct rf_rt_properties {
    uint32_t s_type; void *next;
    uint64_t max_geometry,max_instance,max_primitive;
    uint32_t per_stage,per_stage_update,set_count,set_update,scratch_alignment;
};
struct rf_rt_allocate_flags { uint32_t s_type; const void *next; uint32_t flags,device_mask; };
struct rf_rt_buffer_address { uint32_t s_type; const void *next; rf_vk_buffer buffer; };
struct rf_rt_as_address { uint32_t s_type; const void *next; rf_rt_as structure; };
union rf_rt_address { uint64_t device; const void *host; };
struct rf_rt_triangles {
    uint32_t s_type; const void *next; uint32_t format;
    union rf_rt_address vertices; uint64_t stride; uint32_t max_vertex,index_type;
    union rf_rt_address indices,transform;
};
struct rf_rt_instances { uint32_t s_type; const void *next; uint32_t pointers; union rf_rt_address data; };
struct rf_rt_geometry {
    uint32_t s_type; const void *next; uint32_t type;
    union { struct rf_rt_triangles triangles; struct rf_rt_instances instances; } data;
    uint32_t flags;
};
struct rf_rt_build {
    uint32_t s_type; const void *next; uint32_t type,flags,mode;
    rf_rt_as src,dst; uint32_t geometry_count;
    const struct rf_rt_geometry *geometries; const struct rf_rt_geometry *const *geometry_ptrs;
    union rf_rt_address scratch;
};
struct rf_rt_range { uint32_t count,offset,first_vertex,transform_offset; };
struct rf_rt_sizes { uint32_t s_type; void *next; uint64_t size,update_scratch,build_scratch; };
struct rf_rt_create {
    uint32_t s_type; const void *next; uint32_t flags; rf_vk_buffer buffer;
    uint64_t offset,size; uint32_t type; uint64_t address;
};
struct rf_rt_write { uint32_t s_type; const void *next; uint32_t count; const rf_rt_as *structures; };
struct rf_rt_instance { float transform[12]; uint32_t index_mask,offset_flags; uint64_t reference; };
_Static_assert(sizeof(struct rf_rt_instance)==64,"RT instance ABI");
_Static_assert(sizeof(struct rf_rt_geometry)==96,"RT geometry ABI");
_Static_assert(sizeof(struct rf_rt_build)==80,"RT build ABI");
struct rf_rt_api {
    rf_vk_result (RF_VK_CALL *extensions)(rf_vk_physical_device,const char *,uint32_t *,struct rf_rt_extensions *);
    void (RF_VK_CALL *features)(rf_vk_physical_device,struct rf_rt_features2 *);
    void (RF_VK_CALL *properties)(rf_vk_physical_device,struct rf_rt_properties2 *);
    uint64_t (RF_VK_CALL *buffer_address)(rf_vk_device,const struct rf_rt_buffer_address *);
    uint64_t (RF_VK_CALL *as_address)(rf_vk_device,const struct rf_rt_as_address *);
    void (RF_VK_CALL *sizes)(rf_vk_device,uint32_t,const struct rf_rt_build *,const uint32_t *,struct rf_rt_sizes *);
    rf_vk_result (RF_VK_CALL *create)(rf_vk_device,const struct rf_rt_create *,const void *,rf_rt_as *);
    void (RF_VK_CALL *destroy)(rf_vk_device,rf_rt_as,const void *);
    void (RF_VK_CALL *build)(rf_vk_command_buffer,uint32_t,const struct rf_rt_build *,const struct rf_rt_range *const *);
};
#endif
