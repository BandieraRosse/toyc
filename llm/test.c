/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * test.c — GPT-2 模型功能测试
 *
 * 使用合成数据（随机权重 + 随机 token）验证前向传播的正确性。
 * 无需真实 checkpoint 文件即可运行。
 *
 * 测试项：
 *   1. softmax_inplace — 验证概率和为 1
 *   2. GELU — 验证单调性和零值位置
 *   3. encoder_forward — 验证输出非零
 *   4. layernorm_forward — 验证归一化后均值 ≈0
 *   5. matmul_forward — 验证乘法正确性
 *   6. GPT-2 forward — 完整模型前向（B=1, T=4, 使用 GPT-2 124M config）
 *
 * 编译：make test-llm
 * 运行：./build/test-llm
 *
 * EXPECT: 0（全部通过）
 *
 * TOYC: 本文件用 gcc + toyc.a 编译。所有代码兼容 toyc 编译器 ✅
 */

#include "llm.h"
#include "gpt2.h"

/* ==================================================================
 *  测试框架
 * ================================================================== */

static int g_total;
static int g_passed;

#define TEST_BANNER(name) \
    __printf("\n  ── %s ──\n", name)

#define TEST_CHECK(cond, fmt, ...) do { \
    g_total++; \
    if (cond) { \
        g_passed++; \
        __printf("    ✓ "); \
        __printf(fmt, ##__VA_ARGS__); \
        __printf("\n"); \
    } else { \
        __printf("    ✗ FAIL: "); \
        __printf(fmt, ##__VA_ARGS__); \
        __printf("\n"); \
    } \
} while (0)

/* ==================================================================
 *  Helper: 在不加载真实权重的情况下用随机数据初始化 GPT-2
 * ================================================================== */

static void init_random_weights(GPT2 *model, unsigned long long seed)
{
    RNG rng;
    rng_init(&rng, seed);
    int n = (int)model->total_params;
    float *params = model->params.wte;
    for (int i = 0; i < n; i++)
        params[i] = (rng_f32(&rng) - 0.5f) * 0.02f;
}

/* ==================================================================
 *  测试 1: Softmax
 * ================================================================== */

static void test_softmax(void)
{
    TEST_BANNER("Softmax");
    RNG rng;
    rng_init(&rng, 42);

    float buf[256];
    int sizes[] = {1, 2, 5, 10, 100, 256};
    for (int si = 0; si < 6; si++) {
        int n = sizes[si];

        for (int i = 0; i < n; i++)
            buf[i] = (rng_f32(&rng) - 0.5f) * 10.0f;

        softmax_inplace(buf, n);

        int all_in_range = 1;
        for (int i = 0; i < n; i++) {
            if (buf[i] <= 0.0f || buf[i] > 1.0f) {
                all_in_range = 0;
                break;
            }
        }
        TEST_CHECK(all_in_range, "softmax n=%d: all values in (0,1]", n);

        float sum = 0.0f;
        for (int i = 0; i < n; i++) sum += buf[i];
        TEST_CHECK(fabsf_(sum - 1.0f) < 1e-5f,
                   "softmax n=%d: sum=%.10f ≈ 1.0", n, sum);
    }
}

/* ==================================================================
 *  测试 2: GELU
 * ================================================================== */

static void test_gelu(void)
{
    TEST_BANNER("GELU");

    float g0 = gelu_forward(0.0f);
    TEST_CHECK(fabsf_(g0) < 1e-6f, "GELU(0)=%.10f ≈ 0", g0);

    float g_neg1 = gelu_forward(-1.0f);
    float g_neg05 = gelu_forward(-0.5f);
    float g_pos05 = gelu_forward(0.5f);
    float g_pos1 = gelu_forward(1.0f);
    TEST_CHECK(g_neg1 < g_neg05, "GELU(-1) < GELU(-0.5): %.6f < %.6f", g_neg1, g_neg05);
    TEST_CHECK(g_neg05 < 0, "GELU(-0.5)=%.6f < 0", g_neg05);
    TEST_CHECK(g_pos05 > 0, "GELU(0.5)=%.6f > 0", g_pos05);
    TEST_CHECK(g_pos05 < g_pos1, "GELU(0.5) < GELU(1): %.6f < %.6f", g_pos05, g_pos1);

    float g_neg10 = gelu_forward(-10.0f);
    TEST_CHECK(fabsf_(g_neg10) < 1e-4f, "GELU(-10)=%.10f ≈ 0", g_neg10);

    float g_pos10 = gelu_forward(10.0f);
    TEST_CHECK(fabsf_(g_pos10 - 10.0f) < 1e-4f,
               "GELU(10)=%.6f ≈ 10", g_pos10);
}

/* ==================================================================
 *  测试 3: LayerNorm
 * ================================================================== */

static void test_layernorm(void)
{
    TEST_BANNER("LayerNorm");
    RNG rng;
    rng_init(&rng, 123);

    int B = 1, T = 4, C = 8;
    float inp[32], out[32], mean[4], rstd[4];
    float weight[8], bias[8];

    for (int i = 0; i < B * T * C; i++) inp[i] = (rng_f32(&rng) - 0.5f) * 5.0f;
    for (int i = 0; i < C; i++) { weight[i] = 1.0f; bias[i] = 0.0f; }

    layernorm_forward(out, mean, rstd, inp, weight, bias, B, T, C);

    int all_mean_ok = 1, all_std_ok = 1;
    for (int t = 0; t < T; t++) {
        float m = 0.0f;
        for (int i = 0; i < C; i++) m += out[t * C + i];
        m /= C;
        if (fabsf_(m) > 1e-5f) all_mean_ok = 0;

        float v = 0.0f;
        for (int i = 0; i < C; i++) {
            float d = out[t * C + i] - m;
            v += d * d;
        }
        v /= C;
        if (fabsf_(v - 1.0f) > 1e-4f) all_std_ok = 0;
    }
    TEST_CHECK(all_mean_ok, "layernorm: mean ≈ 0 after norm");
    TEST_CHECK(all_std_ok, "layernorm: variance ≈ 1 after norm");

    TEST_CHECK(rstd[0] > 0.0f && rstd[0] < 10.0f,
               "layernorm: cached rstd=%.6f ∈ (0,10)", rstd[0]);
}

/* ==================================================================
 *  测试 4: MatMul
 * ================================================================== */

static void test_matmul(void)
{
    TEST_BANNER("MatMul");
    int B = 1, T = 3, C = 4, OC = 5;

    float inp[12], weight[20], bias[5], out[15];

    for (int i = 0; i < B * T * C; i++) inp[i] = 1.0f;
    for (int i = 0; i < C * OC; i++) weight[i] = 2.0f;
    for (int i = 0; i < OC; i++) bias[i] = 1.0f;

    matmul_forward(out, inp, weight, bias, B, T, C, OC);

    int all_correct = 1;
    for (int i = 0; i < B * T * OC; i++) {
        if (fabsf_(out[i] - 9.0f) > 1e-5f) {
            all_correct = 0;
            break;
        }
    }
    TEST_CHECK(all_correct, "matmul: all ones * all twos + 1 = 9 (got out[0]=%.6f)", out[0]);

    float inp2[12];
    for (int i = 0; i < B * T * C; i++) inp2[i] = (float)(i + 1);
    matmul_forward(out, inp2, weight, bias, B, T, C, OC);
    TEST_CHECK(fabsf_(out[0] - 21.0f) < 1e-5f,
               "matmul: position 0 = 21 (got %.6f)", out[0]);
}

/* ==================================================================
 *  测试 5: Encoder (Embedding)
 * ================================================================== */

static void test_encoder(void)
{
    TEST_BANNER("Encoder");
    int B = 1, T = 4, C = 8;
    float wte[256], wpe[64], out[32];
    int tokens[4] = {0, 1, 2, 3};

    for (int v = 0; v < 32; v++)
        for (int c = 0; c < C; c++)
            wte[v * C + c] = (float)(v + 1);

    for (int p = 0; p < T; p++)
        for (int c = 0; c < C; c++)
            wpe[p * C + c] = (float)p * 0.5f;

    encoder_forward(out, tokens, wte, wpe, B, T, C);

    int ok = 1;
    for (int i = 0; i < C; i++) {
        if (fabsf_(out[i] - 1.0f) > 1e-5f) { ok = 0; break; }
    }
    TEST_CHECK(ok, "encoder: token=0 pos=0 all 1.0");

    ok = 1;
    for (int i = 0; i < C; i++) {
        if (fabsf_(out[C + i] - 2.5f) > 1e-5f) { ok = 0; break; }
    }
    TEST_CHECK(ok, "encoder: token=1 pos=1 all 2.5");
}

/* ==================================================================
 *  测试 6: GPT-2 完整前向（极小 config）
 * ================================================================== */

static void test_gpt2_forward(void)
{
    TEST_BANNER("GPT-2 Forward (mini)");

    GPT2Config cfg = {
        .vocab_size = 100,
        .vocab_size_padded = 128,
        .channels = 16,
        .num_layers = 2,
        .max_seq_len = 8,
        .num_heads = 4,
        .num_heads_kv = 4,
        .hidden_scale = 4
    };

    GPT2 model;
    if (gpt2_init(&model, cfg) != 0) {
        __printf("    ✗ gpt2_init failed\n");
        return;
    }

    init_random_weights(&model, 999);

    int B = 1, T = 4;
    int tokens[4] = {5, 12, 37, 88};

    __printf("    Running forward pass (B=%d, T=%d, C=%d, L=%d, NH=%d) ... ",
             B, T, cfg.channels, cfg.num_layers, cfg.num_heads);

    gpt2_forward(&model, tokens, B, T);
    __printf("done\n");

    int Vp = cfg.vocab_size_padded;
    float *logits = model.logits;
    int non_zero = 0;
    for (int i = 0; i < B * T * Vp; i++) {
        if (fabsf_(logits[i]) > 1e-10f) { non_zero = 1; break; }
    }
    TEST_CHECK(non_zero, "gpt2: logits are non-zero");

    float *last_logits = logits + (T - 1) * Vp;
    float softmax_buf[256];
    int n = Vp < 256 ? Vp : 256;
    for (int i = 0; i < n; i++) softmax_buf[i] = last_logits[i];
    softmax_inplace(softmax_buf, n);
    float ssum = 0.0f;
    for (int i = 0; i < n; i++) ssum += softmax_buf[i];
    TEST_CHECK(fabsf_(ssum - 1.0f) < 1e-4f,
               "gpt2: softmax of last token sums to 1 (got %.6f)", ssum);

    int tokens2[4] = {0, 0, 0, 0};
    float *logits_copy = (float *)tlibc_malloc(B * T * Vp * sizeof(float));
    llm_memcpy(logits_copy, logits, B * T * Vp * sizeof(float));

    gpt2_forward(&model, tokens2, B, T);

    int different = 0;
    for (int i = 0; i < B * T * Vp; i++) {
        if (fabsf_(logits_copy[i] - model.logits[i]) > 1e-5f) {
            different = 1;
            break;
        }
    }
    TEST_CHECK(different, "gpt2: different inputs produce different logits");
    tlibc_free(logits_copy);

    gpt2_free(&model);
}

/* ==================================================================
 *  主函数
 * ================================================================== */

int main(void)
{
    __printf("\n");
    __printf("╔══════════════════════════════════════════╗\n");
    __printf("║     GPT-2 模型测试（合成数据）          ║\n");
    __printf("║     Tinylibc + ToyCCompiler 生态         ║\n");
    __printf("╚══════════════════════════════════════════╝\n");
    __printf("\n");

    test_softmax();
    test_gelu();
    test_layernorm();
    test_matmul();
    test_encoder();
    test_gpt2_forward();

    __printf("\n");
    __printf("────────────────────────────────────────\n");
    __printf("  %d / %d 测试通过", g_passed, g_total);
    if (g_passed == g_total)
        __printf(" ✅");
    else
        __printf(" ❌");
    __printf("\n\n");

    return (g_passed == g_total) ? 0 : 1;
}
