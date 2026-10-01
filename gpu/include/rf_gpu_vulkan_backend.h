#ifndef RF_GPU_VULKAN_BACKEND_H
#define RF_GPU_VULKAN_BACKEND_H

#include "rf_gpu.h"

/* Vulkan handles remain private to the hosted backend implementation. */
struct rf_gpu_vulkan_context {
    void *implementation;
    struct rf_gpu_native_window native_window;
    /* Hosted HG-2 proof requests a joint graphics/compute queue. */
    unsigned int require_graphics;
    unsigned int prefer_high_rate_present;
    /* HG-2C5 diagnostics only.  Zero keeps the production path unchanged.
     * The selected fault is injected once on the numbered native-present
     * attempt (one-based). */
    unsigned int present_fault;
    unsigned int present_fault_frame;
};

enum rf_gpu_present_fault {
    RF_GPU_PRESENT_FAULT_NONE = 0,
    RF_GPU_PRESENT_FAULT_ACQUIRE_OUT_OF_DATE,
    RF_GPU_PRESENT_FAULT_RECORD_FAILURE,
    RF_GPU_PRESENT_FAULT_SUBMIT_FAILURE,
    RF_GPU_PRESENT_FAULT_PRESENT_OUT_OF_DATE,
    RF_GPU_PRESENT_FAULT_PRESENT_SUBOPTIMAL
};

extern const struct rf_gpu_backend rf_gpu_vulkan_backend;

#endif
