/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * llm.h — LLM 公共头文件：类型定义、数学包装、工具宏
 *
 * 设计：
 *   以 Karpathy llm.c (train_gpt2.c) 为蓝本，适配到 ToyCCompiler + Tinylibc 生态：
 *     - 所有 float 数学通过 inline 包装调用 Tinylibc 的 double 函数
 *     - 无 libc 依赖（Tinylibc 提供 syscall、malloc、printf）
 *     - 文件 I/O 通过 __openat/__mmap 直接系统调用
 *
 * TOYC 兼容性标记说明：
 *     ✅  = toyc 已支持
 *     ❌  = toyc 不支持，当前用 gcc 编译，后续需改写
 *     无标记 = 未被 toyc 验证过，但预计可编译
 *
 * 参考：Karpathy llm.c ( https://github.com/karpathy/llm.c )
 */

#ifndef LLM_H
#define LLM_H

#include "toyc_need.h"  /* size_t, uint32_t, syscall, __write, tlibc_malloc ... */
#include "math.h"       /* Tinylibc: exp, log, sqrt, fabs, fmod, pow */

/* ==================================================================
 *  基础类型别名
 * ================================================================== */

typedef uint32_t u32;

/* ==================================================================
 *  数学包装
 *
 *  Tinylibc 提供 double 精度的数学函数库（泰勒级数，精度 ~1e-10），
 *  但没有 float 变体（expf, logf, sqrtf, fabsf）和 tanh/tanhf。
 *  以下 static inline 包装填补差异。
 *
 *  TOYC: 所有 float↔double 隐式转换已支持，SSE 无条件启用。
 *        未来如果 toyc 实现 __builtin_expf 等，可直接替换。
 * ================================================================== */

static inline float expf_(float x)  { return (float)exp((double)x); }
static inline float logf_(float x)  { return (float)log((double)x); }
static inline float sqrtf_(float x) { return (float)sqrt((double)x); }
static inline float fabsf_(float x) { return (float)fabs((double)x); }

/*
 * tanhf(x) = (e^(2x) - 1) / (e^(2x) + 1)
 * Tinylibc 无 tanh，利用已有的 exp 实现。
 * 大值饱和路径防止 exp 溢出。
 * TOYC: 条件分支 + 浮点运算，编译支持 ✅。
 */
static inline float tanhf_(float x)
{
    if (x >  10.0f) return  1.0f;
    if (x < -10.0f) return -1.0f;
    float e2x = expf_(2.0f * x);
    return (e2x - 1.0f) / (e2x + 1.0f);
}

/*
 * GELU 激活函数（tanh 近似）
 * GELU(x) ≈ 0.5 * x * (1 + tanh(√(2/π) * (x + 0.044715 * x³)))
 * TOYC: 无特殊语法，多级浮点运算 ✅。
 */
static inline float gelu_forward(float x)
{
    float s = 0.7978845608028654f;  /* sqrt(2.0 / M_PI) */
    float c = 0.044715f;
    return 0.5f * x * (1.0f + tanhf_(s * (x + c * x * x * x)));
}

/* ==================================================================
 *  基础常量
 * ================================================================== */

#ifndef M_PI
#define M_PI  3.14159265358979323846
#endif

/* ==================================================================
 *  内存工具
 *
 *  Tinylibc 的 toyc_need.h 提供 __memset，但无 memcpy。
 *  这里提供简单实现。
 *
 *  TOYC: 逐字节循环 ✅（无内置 memcpy 没问题）。
 * ================================================================== */

static inline void llm_memcpy(void *dst, const void *src, size_t n)
{
    char *d = (char *)dst;
    const char *s = (const char *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

static inline void llm_memset(float *buf, float val, int n)
{
    for (int i = 0; i < n; i++) buf[i] = val;
}

/* ==================================================================
 *  简易 LCG 随机数生成器
 *
 *  用于测试数据生成，不做密码学用途。
 *  TOYC: unsigned long long 运算 ✅，无 VLA ✅。
 * ================================================================== */

typedef struct {
    unsigned long long state;
} RNG;

static inline void rng_init(RNG *r, unsigned long long seed)
{
    r->state = seed;
}

static inline float rng_f32(RNG *r)
{
    r->state = r->state * 6364136223846793005ULL + 1;
    unsigned int bits = (unsigned int)(r->state >> 40);
    return (float)bits / 16777216.0f;
}

/* ==================================================================
 *  Softmax（in-place）
 *
 *  TOYC: 无 VLA（固定栈数组 size=1），循环 ✅。
 * ================================================================== */

static inline void softmax_inplace(float *x, int n)
{
    /* 找最大值（数值稳定） */
    float maxv = x[0];
    for (int i = 1; i < n; i++)
        if (x[i] > maxv) maxv = x[i];

    /* 指数 + 求和 */
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        x[i] = expf_(x[i] - maxv);
        sum += x[i];
    }

    /* 归一化 */
    float inv_sum = 1.0f / sum;
    for (int i = 0; i < n; i++)
        x[i] *= inv_sum;
}

/* ==================================================================
 *  断言宏
 *
 *  Tinylibc 有 misc/assert.c（tlibc_assert），但不想引入额外依赖。
 *  简单断言：条件不满足时打印并退出。
 *  TOYC: __printf + __exit 已支持 ✅。
 * ================================================================== */

#define LLM_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        __printf("LLM_ASSERT FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        __exit(1); \
    } \
} while (0)

#endif /* LLM_H */
