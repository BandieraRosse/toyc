#include "rf_gpu.h"
#include "rf_gpu_vulkan_backend.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int verify_frame(struct rf_gpu *gpu, struct rf_gpu_framebuffer *fb,
                        unsigned int width, unsigned int height,
                        uint64_t *hash_out)
{
    const unsigned int stride = width + 3;
    uint32_t *pixels = malloc((size_t)stride * height * sizeof(*pixels));
    uint64_t hash = UINT64_C(1469598103934665603);
    unsigned int x, y;
    if (!pixels) return -1;
    for (y = 0; y < height; ++y)
        for (x = 0; x < stride; ++x) pixels[(size_t)y * stride + x] = 0x13579bdfU;
    if (rf_gpu_framebuffer_render(gpu, fb, pixels, width, height, stride) < 0) {
        free(pixels);
        return -1;
    }
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            uint32_t index = y * width + x;
            uint32_t expected = 0xff000000U + index * 0x00010101U;
            uint32_t value = pixels[(size_t)y * stride + x];
            unsigned int byte;
            if (value != expected) {
                fprintf(stderr, "framebuffer mismatch at %u,%u: %08x != %08x\n",
                        x, y, value, expected);
                free(pixels);
                return -1;
            }
            for (byte = 0; byte < 4; ++byte) {
                hash ^= (value >> (byte * 8)) & 0xffU;
                hash *= UINT64_C(1099511628211);
            }
        }
        for (x = width; x < stride; ++x)
            if (pixels[(size_t)y * stride + x] != 0x13579bdfU) {
                fputs("framebuffer wrote destination stride padding\n", stderr);
                free(pixels);
                return -1;
            }
    }
    printf("framebuffer: %ux%u XRGB8888 stride=%u hash=%016llx PASS\n",
           width, height, stride, (unsigned long long)hash);
    *hash_out = hash;
    free(pixels);
    return 0;
}

int main(void)
{
    struct rf_gpu gpu;
    struct rf_gpu_vulkan_context context;
    struct rf_gpu_framebuffer framebuffer;
    uint64_t first_hash, resized_hash;
    int result = 1;
    memset(&context, 0, sizeof(context));
    memset(&framebuffer, 0, sizeof(framebuffer));
    if (rf_gpu_init(&gpu, RF_GPU_POLICY_REQUIRED, &rf_gpu_vulkan_backend,
                    &context) < 0) {
        fprintf(stderr, "GPU init failed: %s\n", gpu.message);
        return 2;
    }
    if (rf_gpu_framebuffer_init(&gpu, &framebuffer, 64, 48) < 0 ||
        verify_frame(&gpu, &framebuffer, 64, 48, &first_hash) < 0)
        goto done;
    if (rf_gpu_framebuffer_resize(&gpu, &framebuffer, 400, 240) < 0 ||
        verify_frame(&gpu, &framebuffer, 400, 240, &resized_hash) < 0)
        goto done;
    if (!first_hash || !resized_hash || first_hash == resized_hash) goto done;
    result = 0;
done:
    rf_gpu_framebuffer_shutdown(&framebuffer);
    rf_gpu_shutdown(&gpu);
    if (context.implementation) result = 1;
    puts(result ? "GPU framebuffer smoke: FAIL" :
                  "resize/shutdown: PASS\nGPU framebuffer smoke: PASS");
    return result;
}
