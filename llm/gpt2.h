/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * gpt2.h — GPT-2 模型定义与 API
 *
 * 参考 Karpathy llm.c (train_gpt2.c) 的结构：
 *   - ParameterTensors: 所有权重（flat float*，模拟 model.parameters()）
 *   - GPT2: 模型状态（config + weights + 运行时缓存）
 *
 * 用法：
 *   1. gpt2_init(&model, config)        — 分配内存
 *   2. gpt2_load_weights(&model, path)   — 从 checkpoint 加载权重（可选）
 *   3. gpt2_forward(&model, tokens, B,T) — 前向传播 → model.logits
 *   4. gpt2_free(&model)                 — 释放内存
 *
 * TOYC 兼容性：
 *   - struct 定义：toyc 已支持 ✅
 *   - 多级指针（float***）：toyc 已支持 ✅
 *   - 结构体嵌套初始化：支持 ✅（如 GPT2_CONFIG_124M 宏）
 */

#ifndef GPT2_H
#define GPT2_H

#include "llm.h"

/* ==================================================================
 *  GPT-2 配置
 * ================================================================== */

/* TOYC: 花括号嵌套初始化 ✅ */
#define GPT2_CONFIG_124M  { .vocab_size = 50257, .vocab_size_padded = 50304, \
                            .channels = 768, .num_layers = 12, .max_seq_len = 1024, \
                            .num_heads = 12, .num_heads_kv = 12, .hidden_scale = 4 }

#define GPT2_CONFIG_355M  { .vocab_size = 50257, .vocab_size_padded = 50624, \
                            .channels = 1024, .num_layers = 24, .max_seq_len = 1024, \
                            .num_heads = 16, .num_heads_kv = 16, .hidden_scale = 4 }

typedef struct {
    int vocab_size;          /* V */
    int vocab_size_padded;   /* Vp：填充到对齐边界，GPU 访存优化 */
    int channels;            /* C：embedding 维度（d_model） */
    int num_layers;          /* L：transformer 块数 */
    int max_seq_len;         /* maxT：最大序列长度 */
    int num_heads;           /* NH：注意力头数 */
    int num_heads_kv;        /* 对 GPT-2 = num_heads（MHA） */
    int hidden_scale;        /* MLP 中间层缩放因子（GPT-2 = 4） */
} GPT2Config;

/* ==================================================================
 *  参数张量集合（所有权重，flat float* 指针）
 *
 *  在参数字典中查找 shape 时，按 "wte"→"wpe"→"ln1w"→… 顺序。
 *  每个指针指向 gpt2_param_sizes 计算出的 {offset} 位置。
 *  所有权重连续存储在一块 malloc 内存中。
 * ================================================================== */

typedef struct {
    float *wte;              /* (Vp, C)    token embedding */
    float *wpe;              /* (maxT, C)  position embedding */
    float *ln1w;             /* (L, C)     attention ln weight */
    float *ln1b;             /* (L, C)     attention ln bias */
    float *qkw;              /* (L, C, 3C) QKV weight (packed) */
    float *qkb;              /* (L, 3C)    QKV bias */
    float *attn_projw;       /* (L, C, C)  attention output weight */
    float *attn_projb;       /* (L, C)     attention output bias */
    float *ln2w;             /* (L, C)     MLP ln weight */
    float *ln2b;             /* (L, C)     MLP ln bias */
    float *fcw;              /* (L, C, 4C) MLP FC weight */
    float *fcb;              /* (L, 4C)    MLP FC bias */
    float *projw;            /* (L, 4C, C) MLP projection weight */
    float *projb;            /* (L, C)     MLP projection bias */
    float *lnfw;             /* (C)        final ln weight */
    float *lnfb;             /* (C)        final ln bias */
    float *lm_head;          /* (Vp, C)    lm_head (通常与 wte 共享) */
} ParameterTensors;

/* ==================================================================
 *  GPT-2 模型状态
 *
 *  包含配置、权重指针、运行时中间激活缓存（pre-allocated）。
 *  所有缓存在内核分配时按最大 B=1, T=max_seq_len 预留。
 * ================================================================== */

typedef struct {
    GPT2Config config;
    ParameterTensors params;

    /* 运行时缓存（一次分配，复用所有 forward 调用） */
    float *encoded;          /* (B, T, C) */
    float *ln1;              /* (B, T, C) */
    float *ln1_mean;         /* (B, T) */
    float *ln1_rstd;         /* (B, T) */
    float *qkv;              /* (B, T, 3C) */
    float *attn_scores;      /* (B, NH, T, T) */
    float *attn_output;      /* (B, T, C) */
    float *ln2;              /* (B, T, C) */
    float *ln2_mean;         /* (B, T) */
    float *ln2_rstd;         /* (B, T) */
    float *fc;               /* (B, T, 4C) */
    float *residual;         /* (B, T, C) */
    float *hidden;           /* (B, T, C) */
    float *logits;           /* (B, T, Vp) */

    /* 推理输入 */
    int *tokens;             /* (B, T) */

    /* 参数大小表（17 项，用于偏移计算） */
    int param_sizes[17];
    size_t total_params;     /* float 总数 */
    size_t total_bytes;      /* 参数字节数 */
} GPT2;

/* ==================================================================
 *  API
 * ================================================================== */

/* 计算参数大小表并返回总 float 数 */
void gpt2_param_sizes(GPT2Config config, int *sizes, size_t *total);

/* 初始化模型：分配所有权重 + 所有运行时缓存 */
int gpt2_init(GPT2 *model, GPT2Config config);

/* 释放模型内存 */
void gpt2_free(GPT2 *model);

/*
 * 前向传播：从 token ids 计算 logits
 *   tokens — (B, T) 的 token id 数组
 *   B      — batch size（当前仅支持 1）
 *   T      — 序列长度
 * 结果写入 model->logits[B][T][Vp]
 *
 * TOYC: 多层嵌套 for 循环，toyc 已支持 ✅。
 *       无 VLA、无 goto、无复合字面量。
 */
void gpt2_forward(GPT2 *model, int *tokens, int B, int T);

/*
 * 从 checkpoint 文件加载权重（使用 __openat + __mmap 系统调用）
 *
 * 文件格式（Karpathy llm.c 兼容）：
 *   前 256 字节：int header[256] (header[0]=20240326, header[1]=version)
 *   之后按 param_sizes 顺序：float 权重数据
 *
 * 返回 0=成功，-1=失败
 *
 * TOYC: __openat/__mmap/__read/__lseek 等系统调用已支持 ✅。
 *       __mmap 返回 void*，转型到 float* 无问题 ✅。
 */
int gpt2_load_weights(GPT2 *model, const char *path);

/* ==================================================================
 *  独立层函数（你也可在自己的代码中单独调用这些层做实验）
 *
 *  TOYC: 所有函数使用简单指针和循环 ✅
 * ================================================================== */

/* 编码器: embedding lookup + 位置编码
 *   out[b,t,c] = wte[inp[b,t],c] + wpe[t,c]                    */
void encoder_forward(float *out, const int *inp, const float *wte,
                     const float *wpe, int B, int T, int C);

/* Layer Normalization
 *   out = weight * (inp - mean) / sqrt(var + eps) + bias       */
void layernorm_forward(float *out, float *mean, float *rstd,
                       const float *inp, const float *weight,
                       const float *bias, int B, int T, int C);

/* 矩阵乘法: out = inp @ weight + bias
 *   inp: (B,T,C), weight: (C,OC), bias: (OC), out: (B,T,OC)   */
void matmul_forward(float *out, const float *inp, const float *weight,
                    const float *bias, int B, int T, int C, int OC);

/* Causal Self-Attention（独立实现，便于学习）
 *   内部分配临时缓冲区（tlibc_malloc）                            */
void attention_forward(float *attn_output, const float *inp,
                       const float *qkw, const float *qkb,
                       const float *attn_projw, const float *attn_projb,
                       int B, int T, int C, int NH);

/* MLP 层: fc → GELU → proj                                      */
void mlp_forward(float *out, const float *inp,
                 const float *fcw, const float *fcb,
                 const float *projw, const float *projb,
                 int B, int T, int C);

#endif /* GPT2_H */
