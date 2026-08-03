/* SPDX-License-Identifier: MIT */
#ifndef QWEN2_H
#define QWEN2_H

#include "llm.h"
#include "checkpoint.h"
#include "tensor.h"

/* 从 Hugging Face config.json 读取的、前向计算实际需要的模型参数。 */
typedef struct {
    int vocab_size;
    int hidden_size;
    int intermediate_size;
    int num_layers;
    int num_attention_heads;
    int num_kv_heads;
    int head_dim;
    int max_seq_len;
    float rms_norm_eps;
    float rope_theta;
    int tie_word_embeddings;
} Qwen2Config;

/* Qwen2.5-0.5B 的内置配置，显式 --model 时会由 config.json 覆盖。 */
#define QWEN2_5_CONFIG_0_5B { \
    .vocab_size = 151936, .hidden_size = 896, .intermediate_size = 4864, \
    .num_layers = 24, .num_attention_heads = 14, .num_kv_heads = 2, \
    .head_dim = 64, .max_seq_len = 32768, .rms_norm_eps = 1e-6f, \
    .rope_theta = 1000000.0f, .tie_word_embeddings = 1 }

/* 单个 Transformer 层的权重视图；指针均指向 mmap 的 checkpoint。 */
typedef struct {
    const float *input_norm;
    const float *q_proj;
    const float *q_bias;
    const float *k_proj;
    const float *k_bias;
    const float *v_proj;
    const float *v_bias;
    const float *o_proj;
    const float *post_attention_norm;
    const float *gate_proj;
    const float *up_proj;
    const float *down_proj;
} Qwen2LayerWeights;

/* 模型级权重。tie_word_embeddings 时 lm_head 与 token_embedding 相同。 */
typedef struct {
    const float *token_embedding;
    Qwen2LayerWeights *layers;
    const float *final_norm;
    const float *lm_head;
} Qwen2Weights;

/*
 * 推理期可变状态。work 是一次性分配的连续工作区，其余临时向量指向
 * work 内部；KV cache 则按 [layer][position][kv_channel] 单独存放。
 */
typedef struct {
    float *key_cache;
    float *value_cache;
    float *work;
    float *x;
    float *norm;
    float *q;
    float *k;
    float *v;
    float *attention;
    float *projection;
    float *gate;
    float *up;
    float *mlp;
    float *logits;
    int cache_capacity;
    int cache_length;
} Qwen2Runtime;

/* 一个完整模型实例：配置、只读权重、运行时缓冲区和 checkpoint 映射。 */
typedef struct {
    Qwen2Config config;
    Qwen2Weights weights;
    Qwen2Runtime runtime;
    LLMCheckpoint checkpoint;
} Qwen2;

/* 配置及基础算子接口，主要也供小规模单元测试直接调用。 */
int qwen2_config_validate(const Qwen2Config *config);
int qwen2_config_load(Qwen2Config *config, const char *path);
void qwen2_rmsnorm(float *out, const float *input, const float *weight,
                   int count, float eps);
float qwen2_silu(float value);
void qwen2_swiglu(float *out, const float *gate, const float *up, int count);
void qwen2_apply_rope(float *q, float *k, int num_q_heads, int num_kv_heads,
                      int head_dim, int position, float theta);
void qwen2_gqa_attention(float *out, const float *q, const float *key_cache,
                         const float *value_cache, int position,
                         int num_q_heads, int num_kv_heads, int head_dim);
int qwen2_load(Qwen2 *model, const char *path, Qwen2Config config,
               int cache_capacity);
void qwen2_free(Qwen2 *model);
/* position 必须从 0 连续递增；返回的 logits 由 model 持有。 */
int qwen2_forward_token(Qwen2 *model, int token, int position,
                        const float **logits);

#endif
