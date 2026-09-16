#ifndef RASTERFALL_RF_GPU_H
#define RASTERFALL_RF_GPU_H

#define RF_GPU_ADAPTER_NAME_CAPACITY 256
#define RF_GPU_MESSAGE_CAPACITY 192

#define RF_GPU_FRAMEBUFFER_FORMAT_XRGB8888 1

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

struct rf_gpu_backend_info {
    char adapter_name[RF_GPU_ADAPTER_NAME_CAPACITY];
    unsigned int adapter_type;
    unsigned int vendor_id;
    unsigned int device_id;
    unsigned int queue_family;
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
    char message[RF_GPU_MESSAGE_CAPACITY];
};

int rf_gpu_init(struct rf_gpu *gpu, enum rf_gpu_policy policy,
                const struct rf_gpu_backend *backend, void *backend_context);
void rf_gpu_shutdown(struct rf_gpu *gpu);
int rf_gpu_get_status(const struct rf_gpu *gpu, struct rf_gpu_status *status);
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

#endif
