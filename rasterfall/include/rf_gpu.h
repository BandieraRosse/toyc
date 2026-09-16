#ifndef RASTERFALL_RF_GPU_H
#define RASTERFALL_RF_GPU_H

#define RF_GPU_ADAPTER_NAME_CAPACITY 256
#define RF_GPU_MESSAGE_CAPACITY 192
#define RF_GPU_MAX_MEMORY_HEAPS 16
#define RF_GPU_MAX_MEMORY_TYPES 32

#define RF_GPU_FRAMEBUFFER_FORMAT_XRGB8888 1

_Static_assert(sizeof(unsigned int) == 4 && sizeof(unsigned long long) == 8,
               "RF GPU capability snapshot requires 32/64-bit integers");

enum rf_gpu_policy {
    RF_GPU_POLICY_DISABLED = 0,
    RF_GPU_POLICY_OPTIONAL = 1,
    RF_GPU_POLICY_REQUIRED = 2
};

enum rf_gpu_state {
    RF_GPU_STATE_DISABLED = 0,
    RF_GPU_STATE_UNAVAILABLE = 1,
    RF_GPU_STATE_READY = 2,
    RF_GPU_STATE_FAILED = 3
};

enum rf_gpu_adapter_type {
    RF_GPU_ADAPTER_OTHER = 0,
    RF_GPU_ADAPTER_INTEGRATED = 1,
    RF_GPU_ADAPTER_DISCRETE = 2,
    RF_GPU_ADAPTER_VIRTUAL = 3,
    RF_GPU_ADAPTER_CPU = 4
};

enum rf_gpu_capability_state {
    RF_GPU_CAPABILITY_UNSUPPORTED = 0,
    RF_GPU_CAPABILITY_SUPPORTED = 1
};

/* Values intentionally match Vulkan 1.0 property bits, but this public
 * snapshot contains no Vulkan types or handles. */
#define RF_GPU_MEMORY_DEVICE_LOCAL 0x00000001U
#define RF_GPU_MEMORY_HOST_VISIBLE 0x00000002U
#define RF_GPU_MEMORY_HOST_COHERENT 0x00000004U
#define RF_GPU_MEMORY_HOST_CACHED 0x00000008U
#define RF_GPU_MEMORY_LAZILY_ALLOCATED 0x00000010U
#define RF_GPU_MEMORY_HEAP_DEVICE_LOCAL 0x00000001U

struct rf_gpu_memory_heap_capability {
    unsigned long long size;
    unsigned int property_flags;
};

struct rf_gpu_memory_type_capability {
    unsigned int property_flags;
    unsigned int heap_index;
};

struct rf_gpu_capabilities {
    unsigned int api_version;
    unsigned int adapter_index;
    unsigned int compute_queue;
    unsigned int max_compute_work_group_invocations;
    unsigned int max_compute_work_group_size[3];
    unsigned int max_compute_work_group_count[3];
    unsigned long long max_storage_buffer_range;
    unsigned long long min_storage_buffer_offset_alignment;
    unsigned long long non_coherent_atom_size;
    unsigned int shader_int64;
    unsigned int memory_heap_count;
    struct rf_gpu_memory_heap_capability memory_heaps[RF_GPU_MAX_MEMORY_HEAPS];
    unsigned int memory_type_count;
    struct rf_gpu_memory_type_capability memory_types[RF_GPU_MAX_MEMORY_TYPES];
    unsigned int device_local_output_memory;
    unsigned int host_visible_readback_memory;
    unsigned int coherent_readback;
    unsigned int non_coherent_readback;
};

struct rf_gpu_renderer_capabilities {
    unsigned int compute;
    unsigned int framebuffer;
    unsigned int raster_v1;
    unsigned int raster_work_group_x;
    unsigned int raster_work_group_y;
};

struct rf_gpu_backend_info {
    char adapter_name[RF_GPU_ADAPTER_NAME_CAPACITY];
    unsigned int adapter_type;
    unsigned int vendor_id;
    unsigned int device_id;
    unsigned int queue_family;
    struct rf_gpu_capabilities capabilities;
};

/* Backend return values distinguish ordinary absence from a backend that was
 * found but failed during initialization. */
#define RF_GPU_BACKEND_READY 0
#define RF_GPU_BACKEND_UNAVAILABLE 1
#define RF_GPU_BACKEND_FAILED (-1)

struct rf_gpu_backend {
    int (*init)(void *context, struct rf_gpu_backend_info *info,
                char *message, unsigned long message_capacity);
    void (*shutdown)(void *context);
    int (*framebuffer_create)(void *context, unsigned int width,
                              unsigned int height, void **framebuffer,
                              char *message, unsigned long message_capacity);
    void (*framebuffer_destroy)(void *context, void *framebuffer);
    int (*framebuffer_render)(void *context, void *framebuffer,
                              unsigned int *pixels, unsigned int width,
                              unsigned int height, unsigned int stride,
                              char *message, unsigned long message_capacity);
    int (*raster_create)(void *context, unsigned int width,
                         unsigned int height, unsigned int work_group_x,
                         unsigned int work_group_y, void **raster,
                         char *message, unsigned long message_capacity);
    void (*raster_destroy)(void *context, void *raster);
    int (*raster_render)(void *context, void *raster,
                         const void *stream, unsigned long stream_size,
                         unsigned int *color, int *depth,
                         unsigned int width, unsigned int height,
                         unsigned int color_stride,
                         unsigned int depth_stride,
                         char *message, unsigned long message_capacity);
};

struct rf_gpu_framebuffer {
    const struct rf_gpu_backend *backend;
    void *backend_context;
    void *implementation;
    unsigned int format;
    unsigned int width;
    unsigned int height;
    unsigned int stride;
};

struct rf_gpu_raster {
    const struct rf_gpu_backend *backend;
    void *backend_context;
    void *implementation;
    unsigned int format;
    unsigned int width;
    unsigned int height;
    unsigned int work_group_x;
    unsigned int work_group_y;
};

struct rf_gpu {
    const struct rf_gpu_backend *backend;
    void *backend_context;
    struct rf_gpu_backend_info info;
    char message[RF_GPU_MESSAGE_CAPACITY];
    int policy;
    int state;
};

struct rf_gpu_status {
    int policy;
    int state;
    int ready;
    struct rf_gpu_backend_info info;
    struct rf_gpu_renderer_capabilities renderer;
    char message[RF_GPU_MESSAGE_CAPACITY];
};

int rf_gpu_init(struct rf_gpu *gpu, enum rf_gpu_policy policy,
                const struct rf_gpu_backend *backend, void *backend_context);
void rf_gpu_shutdown(struct rf_gpu *gpu);
int rf_gpu_get_status(const struct rf_gpu *gpu, struct rf_gpu_status *status);
void rf_gpu_evaluate_capabilities(const struct rf_gpu_capabilities *capabilities,
                                  struct rf_gpu_renderer_capabilities *renderer);
const char *rf_gpu_policy_name(int policy);
const char *rf_gpu_state_name(int state);
int rf_gpu_framebuffer_init(struct rf_gpu *gpu,
                            struct rf_gpu_framebuffer *framebuffer,
                            unsigned int width, unsigned int height);
int rf_gpu_framebuffer_resize(struct rf_gpu *gpu,
                              struct rf_gpu_framebuffer *framebuffer,
                              unsigned int width, unsigned int height);
int rf_gpu_framebuffer_render(struct rf_gpu *gpu,
                              struct rf_gpu_framebuffer *framebuffer,
                              unsigned int *pixels, unsigned int width,
                              unsigned int height, unsigned int stride);
void rf_gpu_framebuffer_shutdown(struct rf_gpu_framebuffer *framebuffer);
int rf_gpu_raster_init(struct rf_gpu *gpu, struct rf_gpu_raster *raster,
                       unsigned int width, unsigned int height);
int rf_gpu_raster_resize(struct rf_gpu *gpu, struct rf_gpu_raster *raster,
                         unsigned int width, unsigned int height);
int rf_gpu_raster_render(struct rf_gpu *gpu, struct rf_gpu_raster *raster,
                         const void *stream, unsigned long stream_size,
                         unsigned int *color, int *depth,
                         unsigned int width, unsigned int height,
                         unsigned int color_stride,
                         unsigned int depth_stride);
void rf_gpu_raster_shutdown(struct rf_gpu_raster *raster);

#endif
