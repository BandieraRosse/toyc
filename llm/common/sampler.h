/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * sampler.h — 自回归生成采样策略
 *
 * 支持：
 *   - Greedy（temperature = 0）
 *   - Temperature scaling（默认 0.8）
 *   - Top-K 过滤（默认 40）
 *
 * 所有函数为 static inline，无需单独编译。
 * 内部通过 llm.h 的 RNG 和 expf_/softmax_inplace 实现。
 *
 * TOYC: 纯 float 运算 + 循环 ✅
 */

#ifndef SAMPLER_H
#define SAMPLER_H

#include "llm.h"

/* ==================================================================
 *  Sampler 状态
 * ================================================================== */

typedef struct {
    float temperature;          /* 0 = greedy */
    int top_k;                  /* 0 = unlimited */
    unsigned long long rng_state;
} Sampler;

/* ==================================================================
 *  初始化采样器
 * ================================================================== */

static inline void sampler_init(Sampler *s, float temperature,
                                int top_k, unsigned long long seed)
{
    s->temperature = temperature;
    s->top_k = top_k;
    s->rng_state = seed;
}

/* ==================================================================
 *  从 logits 中采样一个 token ID
 *
 *  logits: (vocab_size,) — 原始模型分数（未 softmax）
 *  返回: 采样的 token ID（值域 [0, vocab_size)）
 *
 *  内部会分配临时概率缓冲区。对每步生成调用一次。
 * ================================================================== */

static inline int sample_next(Sampler *s, const float *logits, int vocab_size)
{
    /* ── Greedy: argmax ── */
    if (s->temperature == 0.0f) {
        int best = 0;
        float max_val = logits[0];
        for (int i = 1; i < vocab_size; i++) {
            if (logits[i] > max_val) {
                max_val = logits[i];
                best = i;
            }
        }
        return best;
    }

    /* 分配临时缓冲 */
    float *probs = (float *)tlibc_malloc((size_t)vocab_size * sizeof(float));
    if (!probs) {
        __printf("sampler: malloc failed, falling back to greedy\n");
        int best = 0;
        float max_val = logits[0];
        for (int i = 1; i < vocab_size; i++)
            if (logits[i] > max_val) { max_val = logits[i]; best = i; }
        return best;
    }

    /* 温度缩放 */
    float inv_temp = 1.0f / s->temperature;
    for (int i = 0; i < vocab_size; i++)
        probs[i] = logits[i] * inv_temp;

    /* ── Top-K 过滤 ── */
    if (s->top_k > 0 && s->top_k < vocab_size) {
        /* 找第 top_k 大的值（两次扫描：先找 top_k 个元素，取最小值） */
        /* 拷贝一份，逐 top_k 次找最大值 */
        float *copy = (float *)tlibc_malloc((size_t)vocab_size * sizeof(float));
        if (!copy) {
            tlibc_free(probs);
            __printf("sampler: malloc failed for top-k, falling back to greedy\n");
            int best = 0;
            float max_val = logits[0];
            for (int i = 1; i < vocab_size; i++)
                if (logits[i] > max_val) { max_val = logits[i]; best = i; }
            return best;
        }

        for (int i = 0; i < vocab_size; i++)
            copy[i] = probs[i];

        float kth_val = -1e10f;
        for (int ki = 0; ki < s->top_k; ki++) {
            float maxv = -1e10f;
            int maxi = -1;
            for (int i = 0; i < vocab_size; i++) {
                if (copy[i] > maxv) {
                    maxv = copy[i];
                    maxi = i;
                }
            }
            if (ki == s->top_k - 1)
                kth_val = maxv;
            copy[maxi] = -1e10f;   /* 标记已选 */
        }

        tlibc_free(copy);

        /* 低于阈值的设为 -inf */
        for (int i = 0; i < vocab_size; i++) {
            if (probs[i] < kth_val)
                probs[i] = -1e10f;
        }
    }

    /* ── Softmax ── */
    float maxv = probs[0];
    for (int i = 1; i < vocab_size; i++)
        if (probs[i] > maxv) maxv = probs[i];

    float sum = 0.0f;
    for (int i = 0; i < vocab_size; i++) {
        probs[i] = expf_(probs[i] - maxv);
        sum += probs[i];
    }

    /* ── 随机采样 ── */
    RNG rng;
    rng_init(&rng, s->rng_state);
    float coin = rng_f32(&rng);
    s->rng_state = rng.state;

    float cdf = 0.0f;
    for (int i = 0; i < vocab_size; i++) {
        cdf += probs[i] / sum;
        if (coin < cdf) {
            tlibc_free(probs);
            return i;
        }
    }

    tlibc_free(probs);
    return vocab_size - 1;   /* 浮点舍入误差兜底 */
}

#endif /* SAMPLER_H */
