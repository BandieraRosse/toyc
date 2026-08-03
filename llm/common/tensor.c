/* SPDX-License-Identifier: MIT */
#include "tensor.h"

float llm_dot_f32(const float *a, const float *b, int count)
{
    float sum = 0.0f;
    for (int i = 0; i < count; i++) sum += a[i] * b[i];
    return sum;
}

void llm_matvec_f32(float *out, const float *input, const float *weight,
                    const float *bias, int in_features, int out_features)
{
    for (int o = 0; o < out_features; o++) {
        float value = bias ? bias[o] : 0.0f;
        value += llm_dot_f32(input, weight + (size_t)o * in_features,
                             in_features);
        out[o] = value;
    }
}

void llm_vec_add_f32(float *out, const float *a, const float *b, int count)
{
    for (int i = 0; i < count; i++) out[i] = a[i] + b[i];
}
