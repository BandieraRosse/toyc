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
    unsigned int native_presentation_v1;
};

struct rf_gpu_renderer_capabilities {
    unsigned int compute;
    unsigned int framebuffer;
    unsigned int raster_v1;
    unsigned int native_presentation_v1;
    unsigned int post_raster_v1;
    unsigned int raster_work_group_x;
    unsigned int raster_work_group_y;
};

enum rf_gpu_post_mode {
    RF_GPU_POST_DISABLED = 0,
    RF_GPU_POST_IDENTITY = 1,
    RF_GPU_POST_DEPTH_FOG_V0 = 2
};

/* Depth values are Raster V1 signed Q20 inverse camera-space Z: 1048576/z.
 * Fog thresholds therefore use this exact encoding and are ordered
 * far_inv_z < near_inv_z.  Color is canonical 0xffRRGGBB. */
struct rf_gpu_post_params_v1 {
    unsigned int mode;
    int fog_far_inv_z;
    int fog_near_inv_z;
    unsigned int fog_color;
    unsigned int max_density_q8;
};

struct rf_gpu_native_window {
    unsigned int type;
    unsigned long long window;
    unsigned long long instance;
};

struct rf_gpu_native_present_timing {
    double acquire_ms, gpu_raster_ms, buffer_to_swapchain_ms;
    double post_raster_ms, overlay_upload_ms, overlay_composite_ms;
    double submit_ms, present_ms, present_queue_idle_ms, total_ms;
    unsigned int color_readback_bytes, cpu_framebuffer_copy_bytes;
    unsigned int overlay_upload_bytes;
    unsigned int format, present_mode, image_count, width, height;
};

struct rf_gpu_backend_info {
    char adapter_name[RF_GPU_ADAPTER_NAME_CAPACITY];
    unsigned int adapter_type;
    unsigned int vendor_id;
    unsigned int device_id;
    unsigned int queue_family;
    struct rf_gpu_capabilities capabilities;
};

/* Hosted differential diagnostics only.  These are wall-clock segments;
 * execution_wait_ms includes GPU execution plus the blocking fence wait. */
struct rf_gpu_raster_timing {
    double pack_validation_ms;
    double cpu_binning_ms;
    double tile_upload_ms;
    double command_upload_ms;
    double texture_upload_ms;
    double upload_ms;
    double submit_ms;
    double execution_wait_ms;
    double readback_ms;
    double total_ms;
    unsigned int command_count;
    unsigned int tile_count;
    unsigned long long total_refs;
    unsigned int max_refs_per_tile;
    unsigned int texture_count;
    unsigned long long texture_bytes;
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
                         const void *texture_descs, unsigned int texture_count,
                         const void *texture_texels, unsigned long texture_bytes,
                         unsigned int *color, int *depth,
                         unsigned int width, unsigned int height,
                         unsigned int color_stride,
                         unsigned int depth_stride,
                         struct rf_gpu_raster_timing *timing,
                         char *message, unsigned long message_capacity);
    void (*raster_set_full_scan_diagnostic)(void *context, void *raster,
                                             int enabled);
    int (*set_native_window)(void *context,
                             const struct rf_gpu_native_window *window);
    int (*raster_present)(void *context, void *raster,
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
                         char *message, unsigned long message_capacity);
    int (*raster_composite_diagnostic)(void *context, void *raster,
                         const void *stream, unsigned long stream_size,
                         const unsigned int *overlay_color,
                         const unsigned char *overlay_coverage,
                         unsigned int overlay_stride,
                         unsigned int coverage_stride,
                         unsigned int *color, int *depth,
                         unsigned int width, unsigned int height,
                         unsigned int color_stride,
                         unsigned int depth_stride,
                         char *message, unsigned long message_capacity);
    int (*raster_set_post)(void *context, void *raster,
                          const struct rf_gpu_post_params_v1 *params);
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
int rf_gpu_set_native_window(const struct rf_gpu_backend *backend,
                             void *backend_context,
                             const struct rf_gpu_native_window *window);
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
int rf_gpu_raster_render_timed(struct rf_gpu *gpu, struct rf_gpu_raster *raster,
                         const void *stream, unsigned long stream_size,
                         unsigned int *color, int *depth,
                         unsigned int width, unsigned int height,
                         unsigned int color_stride,
                         unsigned int depth_stride,
                         struct rf_gpu_raster_timing *timing);
int rf_gpu_raster_render_textured_timed(
                         struct rf_gpu *gpu, struct rf_gpu_raster *raster,
                         const void *stream, unsigned long stream_size,
                         const void *texture_descs, unsigned int texture_count,
                         const void *texture_texels, unsigned long texture_bytes,
                         unsigned int *color, int *depth,
                         unsigned int width, unsigned int height,
                         unsigned int color_stride, unsigned int depth_stride,
                         struct rf_gpu_raster_timing *timing);
int rf_gpu_raster_set_full_scan_diagnostic(struct rf_gpu_raster *raster,
                                            int enabled);
int rf_gpu_raster_set_post(struct rf_gpu_raster *raster,
                           const struct rf_gpu_post_params_v1 *params);
void rf_gpu_raster_shutdown(struct rf_gpu_raster *raster);
int rf_gpu_raster_present_textured_timed(
                         struct rf_gpu *gpu, struct rf_gpu_raster *raster,
                         const void *stream, unsigned long stream_size,
                         const void *texture_descs, unsigned int texture_count,
                         const void *texture_texels, unsigned long texture_bytes,
                         const unsigned int *overlay_color,
                         const unsigned char *overlay_coverage,
                         unsigned int overlay_stride,
                         unsigned int coverage_stride,
                         unsigned int width, unsigned int height,
                         struct rf_gpu_raster_timing *raster_timing,
                         struct rf_gpu_native_present_timing *present_timing);
/* Explicit test-only readback oracle.  Native presentation never calls this. */
int rf_gpu_raster_composite_diagnostic(
                         struct rf_gpu *gpu, struct rf_gpu_raster *raster,
                         const void *stream, unsigned long stream_size,
                         const unsigned int *overlay_color,
                         const unsigned char *overlay_coverage,
                         unsigned int overlay_stride,
                         unsigned int coverage_stride,
                         unsigned int *color, int *depth,
                         unsigned int width, unsigned int height,
                         unsigned int color_stride,
                         unsigned int depth_stride);
int rf_gpu_overlay_composite_reference(unsigned int *world,
                         unsigned int world_stride,
                         const unsigned int *overlay_color,
                         unsigned int overlay_stride,
                         const unsigned char *coverage,
                         unsigned int coverage_stride,
                         unsigned int width, unsigned int height);

#endif
