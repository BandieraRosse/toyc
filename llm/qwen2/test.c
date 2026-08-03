/* SPDX-License-Identifier: MIT */
#include "qwen2.h"

static int failures;

#define CHECK(condition, message) do { \
    if (condition) __printf("  ok: %s\n", message); \
    else { __printf("FAIL: %s\n", message); failures++; } \
} while (0)

static int near(float a, float b, float tolerance)
{
    return fabsf_(a - b) <= tolerance;
}

static void test_config(void)
{
    Qwen2Config config = QWEN2_5_CONFIG_0_5B;
    CHECK(qwen2_config_validate(&config) == 0, "Qwen2.5-0.5B config");
    config.num_kv_heads = 3;
    CHECK(qwen2_config_validate(&config) != 0, "reject invalid GQA config");
}

static void test_rmsnorm(void)
{
    float input[2] = {3.0f, 4.0f};
    float weight[2] = {1.0f, 2.0f};
    float out[2];
    qwen2_rmsnorm(out, input, weight, 2, 0.0f);
    float rms = sqrtf_(12.5f);
    CHECK(near(out[0], 3.0f / rms, 1e-5f), "RMSNorm first channel");
    CHECK(near(out[1], 8.0f / rms, 1e-5f), "RMSNorm weight scaling");
}

static void test_swiglu(void)
{
    float gate[3] = {0.0f, 1.0f, -1.0f};
    float up[3] = {2.0f, 2.0f, 2.0f};
    float out[3];
    qwen2_swiglu(out, gate, up, 3);
    CHECK(near(out[0], 0.0f, 1e-6f), "SwiGLU zero gate");
    CHECK(out[1] > 1.0f && out[2] < 0.0f, "SwiGLU SiLU gating");
}

static void test_rope(void)
{
    float q[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    float k[4] = {5.0f, 6.0f, 7.0f, 8.0f};
    qwen2_apply_rope(q, k, 1, 1, 4, 0, 10000.0f);
    CHECK(near(q[0], 1.0f, 1e-6f) && near(q[3], 4.0f, 1e-6f),
          "RoPE position zero is identity");
}

static void test_gqa(void)
{
    float q[8] = {1, 0, 0, 1, 1, 0, 0, 1};
    float keys[4] = {1, 0, 0, 1};
    float values[4] = {2, 3, 5, 7};
    float out[8];
    qwen2_gqa_attention(out, q, keys, values, 0, 4, 2, 2);
    CHECK(near(out[0], 2, 1e-6f) && near(out[3], 3, 1e-6f),
          "GQA shares first KV head");
    CHECK(near(out[4], 5, 1e-6f) && near(out[7], 7, 1e-6f),
          "GQA shares second KV head");
}

static void test_checkpoint_forward(void)
{
    Qwen2Config config = {
        .vocab_size = 8, .hidden_size = 4, .intermediate_size = 8,
        .num_layers = 1, .num_attention_heads = 2, .num_kv_heads = 1,
        .head_dim = 2, .max_seq_len = 4, .rms_norm_eps = 1e-6f,
        .rope_theta = 10000.0f, .tie_word_embeddings = 1
    };
    Qwen2 model;
    const float *logits = NULL;
    int loaded = qwen2_load(&model, "build/qwen2-test.bin", config, 4) == 0;
    CHECK(loaded, "load mmap checkpoint and bind tensor shapes");
    if (!loaded) return;
    CHECK(qwen2_forward_token(&model, 1, 0, &logits) == 0,
          "first token forward");
    float magnitude = 0.0f;
    for (int i = 0; i < config.vocab_size; i++) magnitude += fabsf_(logits[i]);
    CHECK(magnitude > 0.0f, "forward produces non-zero logits");
    CHECK(qwen2_forward_token(&model, 2, 1, &logits) == 0 &&
          model.runtime.cache_length == 2, "second token advances KV cache");
    CHECK(qwen2_forward_token(&model, 2, 3, &logits) != 0,
          "reject non-sequential cache position");
    qwen2_free(&model);
}

int main(void)
{
    __printf("Qwen2 common operator tests\n");
    test_config();
    test_rmsnorm();
    test_swiglu();
    test_rope();
    test_gqa();
    test_checkpoint_forward();
    __printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
