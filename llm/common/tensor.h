/* SPDX-License-Identifier: MIT */
#ifndef LLM_TENSOR_H
#define LLM_TENSOR_H

#include "llm.h"

/* Weight layout is [out_features][in_features]. */
void llm_matvec_f32(float *out, const float *input, const float *weight,
                    const float *bias, int in_features, int out_features);
float llm_dot_f32(const float *a, const float *b, int count);
void llm_vec_add_f32(float *out, const float *a, const float *b, int count);

#endif
