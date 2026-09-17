#ifndef RF_GPU_VULKAN_BACKEND_H
#define RF_GPU_VULKAN_BACKEND_H

#include "rf_gpu.h"

/* Vulkan handles remain private to the hosted backend implementation. */
struct rf_gpu_vulkan_context {
    void *implementation;
    struct rf_gpu_native_window native_window;
};

extern const struct rf_gpu_backend rf_gpu_vulkan_backend;

#endif
