/* SPDX-License-Identifier: MIT */
#include "qwen2.h"

extern int snprintf(char *str, size_t size, const char *format, ...);

int qwen2_config_validate(const Qwen2Config *cfg)
{
    if (!cfg || cfg->vocab_size <= 0 || cfg->hidden_size <= 0) return -1;
    if (cfg->num_layers <= 0 || cfg->num_attention_heads <= 0) return -1;
    if (cfg->num_kv_heads <= 0 || cfg->head_dim <= 0) return -1;
    if (cfg->hidden_size != cfg->num_attention_heads * cfg->head_dim) return -1;
    if (cfg->num_attention_heads % cfg->num_kv_heads != 0) return -1;
    if (cfg->head_dim % 2 != 0 || cfg->max_seq_len <= 0) return -1;
    return 0;
}

void qwen2_rmsnorm(float *out, const float *input, const float *weight,
                   int count, float eps)
{
    float sum_sq = 0.0f;
    for (int i = 0; i < count; i++) sum_sq += input[i] * input[i];
    float scale = 1.0f / sqrtf_(sum_sq / (float)count + eps);
    for (int i = 0; i < count; i++) out[i] = input[i] * scale * weight[i];
}

float qwen2_silu(float value)
{
    return value / (1.0f + expf_(-value));
}

void qwen2_swiglu(float *out, const float *gate, const float *up, int count)
{
    for (int i = 0; i < count; i++) out[i] = qwen2_silu(gate[i]) * up[i];
}

static void rope_heads(float *values, int num_heads, int head_dim,
                       int position, float theta)
{
    int half = head_dim / 2;
    for (int h = 0; h < num_heads; h++) {
        float *head = values + h * head_dim;
        for (int i = 0; i < half; i++) {
            float exponent = (2.0f * (float)i) / (float)head_dim;
            float frequency = 1.0f / (float)pow((double)theta,
                                                (double)exponent);
            float angle = (float)position * frequency;
            float cosine = (float)cos((double)angle);
            float sine = (float)sin((double)angle);
            float first = head[i];
            float second = head[i + half];
            head[i] = first * cosine - second * sine;
            head[i + half] = second * cosine + first * sine;
        }
    }
}

void qwen2_apply_rope(float *q, float *k, int num_q_heads, int num_kv_heads,
                      int head_dim, int position, float theta)
{
    rope_heads(q, num_q_heads, head_dim, position, theta);
    rope_heads(k, num_kv_heads, head_dim, position, theta);
}

void qwen2_gqa_attention(float *out, const float *q, const float *key_cache,
                         const float *value_cache, int position,
                         int num_q_heads, int num_kv_heads, int head_dim)
{
    int kv_dim = num_kv_heads * head_dim;
    int queries_per_kv = num_q_heads / num_kv_heads;
    float scale = 1.0f / sqrtf_((float)head_dim);
    int tokens = position + 1;
    float *scores = (float *)tlibc_malloc((size_t)tokens * sizeof(float));
    if (!scores) { __printf("qwen2_gqa_attention: malloc failed\n"); __exit(1); }

    for (int qh = 0; qh < num_q_heads; qh++) {
        int kvh = qh / queries_per_kv;
        const float *q_head = q + qh * head_dim;
        float max_score = -1e30f;
        for (int t = 0; t < tokens; t++) {
            const float *k_head = key_cache + t * kv_dim + kvh * head_dim;
            scores[t] = llm_dot_f32(q_head, k_head, head_dim) * scale;
            if (scores[t] > max_score) max_score = scores[t];
        }
        float sum = 0.0f;
        for (int t = 0; t < tokens; t++) {
            scores[t] = expf_(scores[t] - max_score);
            sum += scores[t];
        }
        float inv_sum = 1.0f / sum;
        float *out_head = out + qh * head_dim;
        for (int i = 0; i < head_dim; i++) out_head[i] = 0.0f;
        for (int t = 0; t < tokens; t++) {
            const float *v_head = value_cache + t * kv_dim + kvh * head_dim;
            float probability = scores[t] * inv_sum;
            for (int i = 0; i < head_dim; i++)
                out_head[i] += probability * v_head[i];
        }
    }
    tlibc_free(scores);
}

static int tensor_shape(const LLMCheckpoint *checkpoint, const char *name,
                        int ndim, unsigned long long dim0,
                        unsigned long long dim1, const float **data)
{
    LLMTensor tensor;
    if (llm_checkpoint_find(checkpoint, name, &tensor) != 0 ||
        tensor.ndim != ndim || tensor.shape[0] != dim0 ||
        (ndim == 2 && tensor.shape[1] != dim1))
        return -1;
    *data = tensor.data;
    return 0;
}

static int bind_layer(Qwen2 *model, int layer)
{
    Qwen2Config *cfg = &model->config;
    Qwen2LayerWeights *w = &model->weights.layers[layer];
    int C = cfg->hidden_size;
    int I = cfg->intermediate_size;
    int KV = cfg->num_kv_heads * cfg->head_dim;
    char name[64];
#define BIND1(field, suffix, size) do { \
    snprintf(name, sizeof(name), "model.layers.%d.%s", layer, suffix); \
    if (tensor_shape(&model->checkpoint, name, 1, size, 0, &w->field) != 0) \
        return -1; \
} while (0)
#define BIND2(field, suffix, rows, cols) do { \
    snprintf(name, sizeof(name), "model.layers.%d.%s", layer, suffix); \
    if (tensor_shape(&model->checkpoint, name, 2, rows, cols, &w->field) != 0) \
        return -1; \
} while (0)
    BIND1(input_norm, "input_layernorm.weight", C);
    BIND2(q_proj, "self_attn.q_proj.weight", C, C);
    BIND1(q_bias, "self_attn.q_proj.bias", C);
    BIND2(k_proj, "self_attn.k_proj.weight", KV, C);
    BIND1(k_bias, "self_attn.k_proj.bias", KV);
    BIND2(v_proj, "self_attn.v_proj.weight", KV, C);
    BIND1(v_bias, "self_attn.v_proj.bias", KV);
    BIND2(o_proj, "self_attn.o_proj.weight", C, C);
    BIND1(post_attention_norm, "post_attention_layernorm.weight", C);
    BIND2(gate_proj, "mlp.gate_proj.weight", I, C);
    BIND2(up_proj, "mlp.up_proj.weight", I, C);
    BIND2(down_proj, "mlp.down_proj.weight", C, I);
#undef BIND1
#undef BIND2
    return 0;
}

static int allocate_runtime(Qwen2 *model, int capacity)
{
    Qwen2Config *cfg = &model->config;
    Qwen2Runtime *rt = &model->runtime;
    int C = cfg->hidden_size;
    int I = cfg->intermediate_size;
    int Q = cfg->num_attention_heads * cfg->head_dim;
    int KV = cfg->num_kv_heads * cfg->head_dim;
    size_t work_count = (size_t)C * 4 + Q + (size_t)KV * 2 +
                        (size_t)I * 3 + cfg->vocab_size;
    size_t cache_count = (size_t)cfg->num_layers * capacity * KV;
    rt->work = (float *)tlibc_malloc(work_count * sizeof(float));
    rt->key_cache = (float *)tlibc_malloc(cache_count * sizeof(float));
    rt->value_cache = (float *)tlibc_malloc(cache_count * sizeof(float));
    if (!rt->work || !rt->key_cache || !rt->value_cache) return -1;
    float *p = rt->work;
    rt->x = p; p += C;
    rt->norm = p; p += C;
    rt->q = p; p += Q;
    rt->k = p; p += KV;
    rt->v = p; p += KV;
    rt->attention = p; p += C;
    rt->projection = p; p += C;
    rt->gate = p; p += I;
    rt->up = p; p += I;
    rt->mlp = p; p += I;
    rt->logits = p;
    rt->cache_capacity = capacity;
    rt->cache_length = 0;
    return 0;
}

int qwen2_load(Qwen2 *model, const char *path, Qwen2Config config,
               int cache_capacity)
{
    llm_memcpy(&model->config, &config, sizeof(config));
    model->weights.token_embedding = NULL;
    model->weights.layers = NULL;
    model->weights.final_norm = NULL;
    model->weights.lm_head = NULL;
    model->runtime.key_cache = NULL;
    model->runtime.value_cache = NULL;
    model->runtime.work = NULL;
    model->checkpoint.mapping = NULL;
    if (qwen2_config_validate(&config) != 0 || cache_capacity <= 0 ||
        cache_capacity > config.max_seq_len)
        return -1;
    if (llm_checkpoint_open(&model->checkpoint, path) != 0) return -1;
    if (tensor_shape(&model->checkpoint, "model.embed_tokens.weight", 2,
                     config.vocab_size, config.hidden_size,
                     &model->weights.token_embedding) != 0 ||
        tensor_shape(&model->checkpoint, "model.norm.weight", 1,
                     config.hidden_size, 0, &model->weights.final_norm) != 0)
        goto fail;
    model->weights.layers = (Qwen2LayerWeights *)tlibc_malloc(
        (size_t)config.num_layers * sizeof(Qwen2LayerWeights));
    if (!model->weights.layers) goto fail;
    for (int layer = 0; layer < config.num_layers; layer++)
        if (bind_layer(model, layer) != 0) goto fail;
    if (config.tie_word_embeddings) {
        model->weights.lm_head = model->weights.token_embedding;
    } else if (tensor_shape(&model->checkpoint, "lm_head.weight", 2,
                            config.vocab_size, config.hidden_size,
                            &model->weights.lm_head) != 0) {
        goto fail;
    }
    if (allocate_runtime(model, cache_capacity) != 0) goto fail;
    return 0;
fail:
    qwen2_free(model);
    return -1;
}

void qwen2_free(Qwen2 *model)
{
    if (model->runtime.work) tlibc_free(model->runtime.work);
    if (model->runtime.key_cache) tlibc_free(model->runtime.key_cache);
    if (model->runtime.value_cache) tlibc_free(model->runtime.value_cache);
    if (model->weights.layers) tlibc_free(model->weights.layers);
    model->runtime.work = NULL;
    model->runtime.key_cache = NULL;
    model->runtime.value_cache = NULL;
    model->weights.layers = NULL;
    llm_checkpoint_close(&model->checkpoint);
}

int qwen2_forward_token(Qwen2 *model, int token, int position,
                        const float **logits)
{
    Qwen2Config *cfg = &model->config;
    Qwen2Runtime *rt = &model->runtime;
    int C = cfg->hidden_size;
    int I = cfg->intermediate_size;
    int KV = cfg->num_kv_heads * cfg->head_dim;
    if (!rt->work || token < 0 || token >= cfg->vocab_size || position < 0 ||
        position >= rt->cache_capacity || position != rt->cache_length)
        return -1;
    llm_memcpy(rt->x, model->weights.token_embedding + (size_t)token * C,
               (size_t)C * sizeof(float));
    for (int layer = 0; layer < cfg->num_layers; layer++) {
        Qwen2LayerWeights *w = &model->weights.layers[layer];
        qwen2_rmsnorm(rt->norm, rt->x, w->input_norm, C, cfg->rms_norm_eps);
        llm_matvec_f32(rt->q, rt->norm, w->q_proj, w->q_bias, C, C);
        llm_matvec_f32(rt->k, rt->norm, w->k_proj, w->k_bias, C, KV);
        llm_matvec_f32(rt->v, rt->norm, w->v_proj, w->v_bias, C, KV);
        qwen2_apply_rope(rt->q, rt->k, cfg->num_attention_heads,
                         cfg->num_kv_heads, cfg->head_dim, position,
                         cfg->rope_theta);
        size_t layer_offset = (size_t)layer * rt->cache_capacity * KV;
        float *keys = rt->key_cache + layer_offset;
        float *values = rt->value_cache + layer_offset;
        llm_memcpy(keys + (size_t)position * KV, rt->k,
                   (size_t)KV * sizeof(float));
        llm_memcpy(values + (size_t)position * KV, rt->v,
                   (size_t)KV * sizeof(float));
        qwen2_gqa_attention(rt->attention, rt->q, keys, values, position,
                            cfg->num_attention_heads, cfg->num_kv_heads,
                            cfg->head_dim);
        llm_matvec_f32(rt->projection, rt->attention, w->o_proj, NULL, C, C);
        for (int i = 0; i < C; i++) rt->x[i] += rt->projection[i];
        qwen2_rmsnorm(rt->norm, rt->x, w->post_attention_norm, C,
                      cfg->rms_norm_eps);
        llm_matvec_f32(rt->gate, rt->norm, w->gate_proj, NULL, C, I);
        llm_matvec_f32(rt->up, rt->norm, w->up_proj, NULL, C, I);
        qwen2_swiglu(rt->mlp, rt->gate, rt->up, I);
        llm_matvec_f32(rt->projection, rt->mlp, w->down_proj, NULL, I, C);
        for (int i = 0; i < C; i++) rt->x[i] += rt->projection[i];
    }
    qwen2_rmsnorm(rt->norm, rt->x, model->weights.final_norm, C,
                  cfg->rms_norm_eps);
    llm_matvec_f32(rt->logits, rt->norm, model->weights.lm_head, NULL, C,
                   cfg->vocab_size);
    rt->cache_length++;
    *logits = rt->logits;
    return 0;
}
