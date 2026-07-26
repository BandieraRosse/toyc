/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * gpt2.c — GPT-2 前向传播实现
 *
 * 所有 layer 的 forward 函数，以及 gpt2_forward 完整模型前向传播。
 * 参考 Karpathy llm.c (train_gpt2.c) 的结构和命名。
 *
 * 约定：
 *   B  = batch size（当前仅支持 1）
 *   T  = sequence length
 *   C  = channels（embedding 维度）
 *   NH = num_heads
 *   V  = vocab_size
 *   Vp = padded_vocab_size
 *
 * TOYC 兼容性总览：
 *   - 多层嵌套 for：✅
 *   - float 指针运算：✅
 *   - 结构体赋值：✅
 *   - static 函数：✅
 *   ❌ 不适用的特性：无 VLA、无 goto、无复合字面量
 *   ❌ sqrtf_/expf_ 通过 double 包装，toyc 编译需提供相同接口
 */

#include "gpt2.h"

/* ==================================================================
 *  内部辅助
 * ================================================================== */

/* 行优先索引：matrix[row][col] */
/* TOYC: 宏定义 ✅ */
#define IDX2D(ROW, COL, STRIDE)  ((ROW) * (STRIDE) + (COL))
/* 3D 索引 */
#define IDX3D(BATCH, ROW, COL, STRIDE1, STRIDE2) \
    ((BATCH) * (STRIDE1) * (STRIDE2) + (ROW) * (STRIDE2) + (COL))

/* ==================================================================
 *  encoder_forward — Token + Position Embedding
 *
 *  out[b,t,c] = wte[tokens[b,t],c] + wpe[t,c]
 *
 *  inp:  (B, T)   token ids
 *  wte:  (V, C)   token embedding 表
 *  wpe:  (maxT,C) position embedding 表
 *  out:  (B, T, C)
 *
 *  TOYC: 简单循环 + 指针偏移 ✅
 * ================================================================== */

void encoder_forward(float *out,
                            const int *inp, const float *wte, const float *wpe,
                            int B, int T, int C)
{
    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            float *out_bt = out + b * T * C + t * C;
            int ix = inp[b * T + t];
            const float *wte_ix = wte + ix * C;
            const float *wpe_t  = wpe + t * C;
            for (int i = 0; i < C; i++) {
                out_bt[i] = wte_ix[i] + wpe_t[i];
            }
        }
    }
}

/* ==================================================================
 *  layernorm_forward — Layer Normalization
 *
 *  参考: https://pytorch.org/docs/stable/generated/torch.nn.LayerNorm.html
 *
 *  out[b,t,c] = weight[c] * (inp[b,t,c] - mean[b,t]) / sqrt(var[b,t] + eps) + bias[c]
 *
 *  inp:     (B, T, C)
 *  weight:  (C)
 *  bias:    (C)
 *  mean:    (B, T)   缓存，用于未来 backward
 *  rstd:    (B, T)   缓存，用于未来 backward
 *  out:     (B, T, C)
 *
 *  TOYC: sqrtf_ 为 llm.h 包装 ✅
 * ================================================================== */

void layernorm_forward(float *out, float *mean, float *rstd,
                              const float *inp, const float *weight, const float *bias,
                              int B, int T, int C)
{
    float eps = 1e-5f;
    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            const float *x = inp + b * T * C + t * C;

            /* 计算均值 */
            float m = 0.0f;
            for (int i = 0; i < C; i++) m += x[i];
            m /= (float)C;

            /* 计算方差 */
            float v = 0.0f;
            for (int i = 0; i < C; i++) {
                float xs = x[i] - m;
                v += xs * xs;
            }
            v /= (float)C;

            /* 计算 rstd = 1 / sqrt(var + eps) */
            float s = 1.0f / sqrtf_(v + eps);

            /* 归一化 + 缩放 + 平移 */
            float *out_bt = out + b * T * C + t * C;
            for (int i = 0; i < C; i++) {
                float n = s * (x[i] - m);
                out_bt[i] = n * weight[i] + bias[i];
            }

            /* 缓存 backward 需要 */
            mean[b * T + t] = m;
            rstd[b * T + t] = s;
        }
    }
}

/* ==================================================================
 *  matmul_forward — 矩阵乘法: out = inp @ weight + bias
 *
 *  inp:     (B, T, C)
 *  weight:  (C, OC)     OC = output_channels
 *  bias:    (OC)
 *  out:     (B, T, OC)
 *
 *  朴素 O(n³) 实现。后续可替换为分块或 SIMD 优化版本。
 *  TOYC: 三重循环 ✅
 * ================================================================== */

void matmul_forward(float *out,
                           const float *inp, const float *weight, const float *bias,
                           int B, int T, int C, int OC)
{
    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            const float *inp_bt = inp + b * T * C + t * C;
            float *out_bt = out + b * T * OC + t * OC;
            for (int o = 0; o < OC; o++) {
                float val = (bias != NULL) ? bias[o] : 0.0f;
                for (int i = 0; i < C; i++) {
                    val += inp_bt[i] * weight[o * C + i];
                }
                out_bt[o] = val;
            }
        }
    }
}

/* ==================================================================
 *  attention_forward — Causal Self-Attention
 *
 *  所有变换分别实现（不打包 QKV），便于理解。
 *  后续可优化为一次 matmul QKV。
 *
 *  TOYC: 无 VLA（固定 size=1 栈数组），无 goto ✅
 * ================================================================== */

void attention_forward(float *attn_output,
                              const float *inp,
                              const float *qkw, const float *qkb,
                              const float *attn_projw, const float *attn_projb,
                              int B, int T, int C, int NH)
{
    int C_per_head = C / NH;

    /* 分配临时缓冲区（栈上小数组或从参数传） */
    /* TOYC: 动态分配 ✅，VLA 不可用 ❌，故用 tlibc_malloc */
    float *q = (float *)tlibc_malloc(B * T * C * sizeof(float));
    float *k = (float *)tlibc_malloc(B * T * C * sizeof(float));
    float *v = (float *)tlibc_malloc(B * T * C * sizeof(float));
    float *preatt = (float *)tlibc_malloc(B * NH * T * T * sizeof(float));
    float *att = (float *)tlibc_malloc(B * NH * T * T * sizeof(float));
    if (!q || !k || !v || !preatt || !att) {
        __printf("attention_forward: malloc failed\n");
        __exit(1);
    }

    /* Q = inp @ Wq + bq, 同理 K, V */
    /* 这里模拟 QKV 打包权重: qkw 结构为 (C, 3*C) */
    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            const float *inp_bt = inp + b * T * C + t * C;
            float *q_bt = q + b * T * C + t * C;
            float *k_bt = k + b * T * C + t * C;
            float *v_bt = v + b * T * C + t * C;

            for (int o = 0; o < C; o++) {
                float qv = qkb ? qkb[o] : 0.0f;
                float kv = qkb ? qkb[C + o] : 0.0f;
                float vv = qkb ? qkb[2 * C + o] : 0.0f;
                for (int i = 0; i < C; i++) {
                    qv += inp_bt[i] * qkw[o * C + i];
                    kv += inp_bt[i] * qkw[(C + o) * C + i];
                    vv += inp_bt[i] * qkw[(2 * C + o) * C + i];
                }
                q_bt[o] = qv;
                k_bt[o] = kv;
                v_bt[o] = vv;
            }
        }
    }

    /* 注意力分数: preatt[b, nh, t, s] = q[b,nh,t,:] @ k[b,nh,s,:] / sqrt(C_per_head) */
    float scale = 1.0f / sqrtf_((float)C_per_head);
    for (int b = 0; b < B; b++) {
        for (int nh = 0; nh < NH; nh++) {
            for (int t = 0; t < T; t++) {
                for (int s = 0; s < T; s++) {
                    float val = 0.0f;
                    for (int i = 0; i < C_per_head; i++) {
                        val += q[b * T * C + t * C + nh * C_per_head + i]
                             * k[b * T * C + s * C + nh * C_per_head + i];
                    }
                    val *= scale;
                    /* Causal mask: s > t 时设为 -inf */
                    if (s > t) val = -1e10f;
                    preatt[b * NH * T * T + nh * T * T + t * T + s] = val;
                }
            }
        }
    }

    /* Softmax over s (每个 (b,nh,t) 对 s 做 softmax) */
    for (int b = 0; b < B; b++) {
        for (int nh = 0; nh < NH; nh++) {
            for (int t = 0; t < T; t++) {
                float *att_bt = att + b * NH * T * T + nh * T * T + t * T;
                float *pre_bt = preatt + b * NH * T * T + nh * T * T + t * T;
                llm_memcpy(att_bt, pre_bt, T * sizeof(float));
                softmax_inplace(att_bt, T);
            }
        }
    }

    /* 加权求和: attn_output[b,nh,t,:] = att[b,nh,t,:] @ v[b,nh,:,:] */
    for (int b = 0; b < B; b++) {
        for (int nh = 0; nh < NH; nh++) {
            for (int t = 0; t < T; t++) {
                float *out_nht = attn_output + b * T * C + t * C + nh * C_per_head;
                const float *att_t = att + b * NH * T * T + nh * T * T + t * T;
                for (int i = 0; i < C_per_head; i++) {
                    float val = 0.0f;
                    for (int s = 0; s < T; s++) {
                        val += att_t[s] * v[b * T * C + s * C + nh * C_per_head + i];
                    }
                    out_nht[i] = val;
                }
            }
        }
    }

    /* 输出投影: attn_output = attn_output @ attn_projw + attn_projb */
    /* attn_projw: (C, C), attn_projb: (C) */
    /* 复用 hidden 作为临时缓冲区，做 matmul */
    /* 这里直接做 in-place 不太方便，所以我们新算一次 */
    /* TOYC: 指针运算 ✅ */
    {
        float *tmp = (float *)tlibc_malloc(B * T * C * sizeof(float));
        if (!tmp) { __printf("attention_forward: tmp malloc failed\n"); __exit(1); }
        for (int b = 0; b < B; b++) {
            for (int t = 0; t < T; t++) {
                const float *x = attn_output + b * T * C + t * C;
                float *y = tmp + b * T * C + t * C;
                for (int o = 0; o < C; o++) {
                    float val = attn_projb ? attn_projb[o] : 0.0f;
                    for (int i = 0; i < C; i++) {
                        val += x[i] * attn_projw[o * C + i];
                    }
                    y[o] = val;
                }
            }
        }
        llm_memcpy(attn_output, tmp, B * T * C * sizeof(float));
        tlibc_free(tmp);
    }

    tlibc_free(q);
    tlibc_free(k);
    tlibc_free(v);
    tlibc_free(preatt);
    tlibc_free(att);
}

/* ==================================================================
 *  mlp_forward — MLP 层（GELU 激活）
 *
 *  结构：MLP(x) = proj(gelu(fc(x)))
 *    fc:     inp @ W_fc + b_fc     (C → 4C)
 *    gelu:   逐元素 GELU
 *    proj:   gelu_out @ W_proj + b_proj  (4C → C)
 *
 *  TOYC: gelu_forward 是 inline ✅，tlibc_malloc ✅
 * ================================================================== */

void mlp_forward(float *out,
                        const float *inp,
                        const float *fcw, const float *fcb,
                        const float *projw, const float *projb,
                        int B, int T, int C)
{
    int HC = 4 * C;  /* hidden channels */

    float *fc_out = (float *)tlibc_malloc(B * T * HC * sizeof(float));
    if (!fc_out) { __printf("mlp_forward: malloc failed\n"); __exit(1); }

    /* fc: (B,T,C) → (B,T,4C) */
    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            const float *x = inp + b * T * C + t * C;
            float *y = fc_out + b * T * HC + t * HC;
            for (int o = 0; o < HC; o++) {
                float val = fcb ? fcb[o] : 0.0f;
                for (int i = 0; i < C; i++) {
                    val += x[i] * fcw[o * C + i];
                }
                y[o] = val;
            }
        }
    }

    /* GELU 激活（in-place） */
    for (int i = 0; i < B * T * HC; i++) {
        fc_out[i] = gelu_forward(fc_out[i]);
    }

    /* proj: (B,T,4C) → (B,T,C) */
    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            const float *x = fc_out + b * T * HC + t * HC;
            float *y = out + b * T * C + t * C;
            for (int o = 0; o < C; o++) {
                float val = projb ? projb[o] : 0.0f;
                for (int i = 0; i < HC; i++) {
                    val += x[i] * projw[o * HC + i];
                }
                y[o] = val;
            }
        }
    }

    tlibc_free(fc_out);
}

/* ==================================================================
 *  gpt2_forward — 完整 GPT-2 前向传播
 *
 *  tokens: (B, T)  token ids, 值域 [0, vocab_size)
 *  B:      batch size
 *  T:      sequence length
 *
 *  流程：
 *    1. encoder:    tokens → encoded            (embedding lookup)
 *    2. for each layer:
 *       a. ln1:     encoded → ln1               (pre-attention layernorm)
 *       b. attn:    ln1 → attn_output           (causal self-attention)
 *       c. residual: encoded + attn_output      (shortcut connection)
 *       d. ln2:     residual → ln2              (pre-mlp layernorm)
 *       e. mlp:     ln2 → mlp_output            (fc + gelu + proj)
 *       f. residual: residual + mlp_output      (shortcut + residual)
 *    3. final_ln:  residual → normalized        (final layernorm)
 *    4. lm_head:   normalized @ wte.T → logits  (project to vocab)
 *
 *  TOYC: 动态内存已通过 gpt2_init 预分配，不涉及 VLA ✅
 * ================================================================== */

void gpt2_forward(GPT2 *model, int *tokens, int B, int T)
{
    GPT2Config cfg = model->config;
    ParameterTensors p = model->params;
    int C = cfg.channels;
    int NH = cfg.num_heads;
    int Vp = cfg.vocab_size_padded;

    /* ==============================================================
     *  Step 1: Embedding
     * ============================================================== */
    encoder_forward(model->encoded, tokens, p.wte, p.wpe, B, T, C);

    /* ==============================================================
     *  Step 2: Transformer layers
     * ============================================================== */
    for (int l = 0; l < cfg.num_layers; l++) {
        /* LayerNorm 1 (pre-attention) */
        layernorm_forward(model->ln1, model->ln1_mean, model->ln1_rstd,
                          model->encoded,
                          p.ln1w + l * C, p.ln1b + l * C,
                          B, T, C);

        /* Self-attention */
        attention_forward(model->attn_output,
                          model->ln1,
                          p.qkw + l * C * 3 * C, p.qkb + l * 3 * C,
                          p.attn_projw + l * C * C, p.attn_projb + l * C,
                          B, T, C, NH);

        /* Residual: encoded = encoded + attn_output */
        for (int i = 0; i < B * T * C; i++)
            model->encoded[i] += model->attn_output[i];

        /* LayerNorm 2 (pre-mlp) */
        layernorm_forward(model->ln2, model->ln2_mean, model->ln2_rstd,
                          model->encoded,
                          p.ln2w + l * C, p.ln2b + l * C,
                          B, T, C);

        /* MLP */
        /* TOYC: 如果 tlibc_malloc 耗时过多，可预分配缓冲区。
         *       MLP 内部使用临时堆分配目前不影响正确性。 */
        mlp_forward(model->hidden,
                    model->ln2,
                    p.fcw + l * C * 4 * C, p.fcb + l * 4 * C,
                    p.projw + l * 4 * C * C, p.projb + l * C,
                    B, T, C);

        /* Residual: encoded = encoded + mlp_output */
        for (int i = 0; i < B * T * C; i++)
            model->encoded[i] += model->hidden[i];
    }

    /* ==============================================================
     *  Step 3: Final LayerNorm
     * ============================================================== */
    /* 复用 ln1 缓存作为 final norm 输出 */
    layernorm_forward(model->ln1, model->ln1_mean, model->ln1_rstd,
                      model->encoded,
                      p.lnfw, p.lnfb,
                      B, T, C);

    /* ==============================================================
     *  Step 4: LM Head (logits) — 使用 wte 权重（权重绑定）
     *  logits[b,t,v] = ln1[b,t,:] @ wte[v,:]
     * ============================================================== */
    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            const float *x = model->ln1 + b * T * C + t * C;
            float *logit = model->logits + b * T * Vp + t * Vp;
            for (int v = 0; v < Vp; v++) {
                float val = 0.0f;
                for (int i = 0; i < C; i++) {
                    val += x[i] * p.wte[v * C + i];
                }
                logit[v] = val;
            }
        }
    }
}

/* ==================================================================
 *  gpt2_param_sizes — 计算各参数张量大小
 *
 *  按顺序填充 16 个 int，顺序与 ParameterTensors 字段一致。
 * ================================================================== */

void gpt2_param_sizes(GPT2Config cfg, int *sizes, size_t *total)
{
    int Vp = cfg.vocab_size_padded;
    int C  = cfg.channels;
    int L  = cfg.num_layers;
    int maxT = cfg.max_seq_len;

    sizes[0]  = Vp * C;             /* wte */
    sizes[1]  = maxT * C;           /* wpe */
    sizes[2]  = L * C;              /* ln1w */
    sizes[3]  = L * C;              /* ln1b */
    sizes[4]  = L * C * 3 * C;      /* qkw */
    sizes[5]  = L * 3 * C;          /* qkb */
    sizes[6]  = L * C * C;          /* attn_projw */
    sizes[7]  = L * C;              /* attn_projb */
    sizes[8]  = L * C;              /* ln2w */
    sizes[9]  = L * C;              /* ln2b */
    sizes[10] = L * C * 4 * C;      /* fcw */
    sizes[11] = L * 4 * C;          /* fcb */
    sizes[12] = L * 4 * C * C;      /* projw */
    sizes[13] = L * C;              /* projb */
    sizes[14] = C;                  /* lnfw */
    sizes[15] = C;                  /* lnfb */

    size_t sum = 0;
    for (int i = 0; i < 16; i++)
        sum += sizes[i];

    if (total) *total = sum;
}

/* ==================================================================
 *  gpt2_init — 初始化模型
 *
 *  分配:
 *    1. 参数内存（连续大块，所有权重挨着放）
 *    2. 运行时缓存（每个 buffer 独立分配）
 *
 *  返回 0=成功，-1=失败
 * ================================================================== */

int gpt2_init(GPT2 *model, GPT2Config config)
{
    model->config = config;

    /* 计算参数大小 */
    gpt2_param_sizes(config, model->param_sizes, &model->total_params);
    model->total_bytes = model->total_params * sizeof(float);

    /* 分配连续参数内存 */
    float *params_mem = (float *)tlibc_malloc(model->total_bytes);
    if (!params_mem) {
        __printf("gpt2_init: failed to allocate %zu bytes for params\n",
                 model->total_bytes);
        return -1;
    }

    /* 设置参数指针 */
    float *ptr = params_mem;
    model->params.wte       = ptr; ptr += model->param_sizes[0];
    model->params.wpe       = ptr; ptr += model->param_sizes[1];
    model->params.ln1w      = ptr; ptr += model->param_sizes[2];
    model->params.ln1b      = ptr; ptr += model->param_sizes[3];
    model->params.qkw       = ptr; ptr += model->param_sizes[4];
    model->params.qkb       = ptr; ptr += model->param_sizes[5];
    model->params.attn_projw = ptr; ptr += model->param_sizes[6];
    model->params.attn_projb = ptr; ptr += model->param_sizes[7];
    model->params.ln2w      = ptr; ptr += model->param_sizes[8];
    model->params.ln2b      = ptr; ptr += model->param_sizes[9];
    model->params.fcw       = ptr; ptr += model->param_sizes[10];
    model->params.fcb       = ptr; ptr += model->param_sizes[11];
    model->params.projw     = ptr; ptr += model->param_sizes[12];
    model->params.projb     = ptr; ptr += model->param_sizes[13];
    model->params.lnfw      = ptr; ptr += model->param_sizes[14];
    model->params.lnfb      = ptr; ptr += model->param_sizes[15];

    /* 分配运行时缓存（使用 B=1, T=max_seq_len 的最大大小） */
    int B = 1;
    int T = config.max_seq_len;
    int C = config.channels;
    int NH = config.num_heads;
    int Vp = config.vocab_size_padded;

    /* TOYC: 多次 tlibc_malloc ✅，已检查 NULL */
#define ALLOC_BUF(name, nelem) do { \
    model->name = (float *)tlibc_malloc((nelem) * sizeof(float)); \
    if (!model->name) { \
        __printf("gpt2_init: failed to allocate " #name "\n"); \
        gpt2_free(model); \
        return -1; \
    } \
} while (0)

    ALLOC_BUF(encoded,      B * T * C);
    ALLOC_BUF(ln1,          B * T * C);
    ALLOC_BUF(ln1_mean,     B * T);
    ALLOC_BUF(ln1_rstd,     B * T);
    ALLOC_BUF(qkv,          B * T * 3 * C);
    ALLOC_BUF(attn_scores,  B * NH * T * T);
    ALLOC_BUF(attn_output,  B * T * C);
    ALLOC_BUF(ln2,          B * T * C);
    ALLOC_BUF(ln2_mean,     B * T);
    ALLOC_BUF(ln2_rstd,     B * T);
    ALLOC_BUF(fc,           B * T * 4 * C);
    ALLOC_BUF(residual,     B * T * C);
    ALLOC_BUF(hidden,       B * T * C);
    ALLOC_BUF(logits,       B * T * Vp);

    model->tokens = (int *)tlibc_malloc(B * T * sizeof(int));
    if (!model->tokens) {
        __printf("gpt2_init: failed to allocate tokens\n");
        gpt2_free(model);
        return -1;
    }

#undef ALLOC_BUF

    return 0;
}

/* ==================================================================
 *  gpt2_free — 释放模型内存
 * ================================================================== */

void gpt2_free(GPT2 *model)
{
    /* 参数内存（从 wte 取起始地址，它指向连续大块的开始） */
    if (model->params.wte) tlibc_free(model->params.wte);
    model->params.wte = NULL;

    /* 运行时缓存 */
#define FREE_BUF(name) do { \
    if (model->name) tlibc_free(model->name); \
    model->name = NULL; \
} while (0)

    FREE_BUF(encoded);
    FREE_BUF(ln1);
    FREE_BUF(ln1_mean);
    FREE_BUF(ln1_rstd);
    FREE_BUF(qkv);
    FREE_BUF(attn_scores);
    FREE_BUF(attn_output);
    FREE_BUF(ln2);
    FREE_BUF(ln2_mean);
    FREE_BUF(ln2_rstd);
    FREE_BUF(fc);
    FREE_BUF(residual);
    FREE_BUF(hidden);
    FREE_BUF(logits);
    FREE_BUF(tokens);

#undef FREE_BUF
}

/* ==================================================================
 *  gpt2_load_weights — 从 checkpoint 文件加载权重
 *
 *  文件格式（Karpathy llm.c checkpoint v1/v3）：
 *    header[0] = 20240326 (magic)
 *    header[1] = version
 *    header[2..255] = config values
 *    然后按 param_sizes 顺序的 float 权重数据（16 个张量，无独立 lm_head）
 *
 *  TOYC: __openat 使用 AT_FDCWD = -100，flags = O_RDONLY = 0 ✅
 *        __mmap 使用 MAP_PRIVATE | MAP_ANONYMOUS... 不对，这里不需要
 *        匿名映射。需要的是文件映射：fd 有效时 mmap 映射文件内容。
 *        需要 flags = MAP_PRIVATE（不带 MAP_ANONYMOUS）。
 *
 *        注意：__mmap 的 flags 参数中 MAP_ANONYMOUS(0x20) 表示匿名映射。
 *        文件映射不传 MAP_ANONYMOUS。 ✅
 * ================================================================== */

int gpt2_load_weights(GPT2 *model, const char *path)
{
    /* 打开文件 */
    int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) {
        __printf("gpt2_load_weights: cannot open %s\n", path);
        return -1;
    }

    /* 获取文件大小 */
    off_t file_size = __lseek(fd, 0, SEEK_END);
    __lseek(fd, 0, SEEK_SET);  /* 归位到文件开头 */

    if (file_size < 256) {
        __printf("gpt2_load_weights: file too small (%ld bytes)\n", (long)file_size);
        __close(fd);
        return -1;
    }

    /* mmap 文件 */
    /* TOYC: __mmap 原型: void *__mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
     *       文件映射不需要 MAP_ANONYMOUS(0x20)，只用 MAP_PRIVATE(0x02) */
    void *mapped = __mmap(NULL, (size_t)file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        __printf("gpt2_load_weights: mmap failed (size=%ld)\n", (long)file_size);
        __close(fd);
        return -1;
    }

    /* 可以关闭 fd 了（mmap 持有引用） */
    __close(fd);

    /* 解析 header */
    int *header = (int *)mapped;
    if (header[0] != 20240326 && header[0] != 20240327) {
        __printf("gpt2_load_weights: bad magic 0x%x (expected 20240326/20240327)\n",
                 header[0]);
        __munmap(mapped, (size_t)file_size);
        return -1;
    }

    /* 权重数据紧跟在 256 ints (1024 bytes) 之后 */
    float *weights = (float *)mapped + 256;

    /* TODO: 检查 header 中的 config 是否匹配 */

    /* 复制权重 */
    size_t n_floats = model->total_params;
    /* TOYC: llm_memcpy 是逐字节复制 ✅ */
    llm_memcpy(model->params.wte, weights, n_floats * sizeof(float));

    __munmap(mapped, (size_t)file_size);
    return 0;
}

/* ==================================================================
 *  attention_forward_kvcache — 带 KV cache 的因果自注意力
 *
 *  只处理当前 position（pos）的 query，k、v 从缓存读取。
 *  用于 gpt2_forward_kv 每层的注意力计算。
 *
 *  out:       (C)           当前 position 的注意力输出
 *  q:         (C)           当前 position 的 query
 *  k_cache:   (maxT, C)     本层所有已缓存的 K
 *  v_cache:   (maxT, C)     本层所有已缓存的 V
 *  pos:                     当前 position（0-indexed，包含已缓存的历史）
 *  C/NH/T:                  模型维度
 *
 *  TOYC: preatt 用固定 1024 栈数组（非 VLA），C 语言标准支持 ✅
 * ================================================================== */

static void attention_forward_kvcache(float *out, const float *q,
                                       const float *k_cache, const float *v_cache,
                                       int pos, int C, int NH, int maxT)
{
    int hs = C / NH;
    float scale = 1.0f / sqrtf_((float)hs);

    for (int h = 0; h < NH; h++) {
        const float *q_h = q + h * hs;

        /* Pass 1: Q · K^T / sqrt(d) */
        float preatt[1024];  /* maxT-sized 栈缓冲（GPT-2 maxT=1024） */
        float maxval = -10000.0f;

        for (int t2 = 0; t2 <= pos; t2++) {
            const float *k_h = k_cache + t2 * C + h * hs;
            float val = 0.0f;
            for (int i = 0; i < hs; i++)
                val += q_h[i] * k_h[i];
            val *= scale;
            if (val > maxval) maxval = val;
            preatt[t2] = val;
        }

        /* Pass 2: softmax (exp, sum, normalize) */
        float expsum = 0.0f;
        for (int t2 = 0; t2 <= pos; t2++) {
            float expv = expf_(preatt[t2] - maxval);
            expsum += expv;
            preatt[t2] = expv;
        }
        float expsum_inv = expsum == 0.0f ? 0.0f : 1.0f / expsum;
        for (int t2 = 0; t2 <= pos; t2++)
            preatt[t2] *= expsum_inv;

        /* Pass 3: weighted sum of V */
        float *out_h = out + h * hs;
        for (int i = 0; i < hs; i++) out_h[i] = 0.0f;
        for (int t2 = 0; t2 <= pos; t2++) {
            const float *v_h = v_cache + t2 * C + h * hs;
            float att = preatt[t2];
            for (int i = 0; i < hs; i++)
                out_h[i] += att * v_h[i];
        }
    }
}

/* ==================================================================
 *  gpt2_forward_kv — 单 token 前向传播（带 KV cache）
 *
 *  只处理 position pos 的一个 token，缓存 K、V。
 *  对 prompt 编码和逐 step 生成统一使用。
 *
 *  TOYC: 一次 tlibc_malloc 分配全部临时缓冲，无 VLA ✅
 * ================================================================== */

void gpt2_forward_kv(GPT2 *model, KVCache *kv, int token, int pos)
{
    GPT2Config cfg = model->config;
    ParameterTensors p = model->params;
    int C = cfg.channels;
    int NH = cfg.num_heads;
    int Vp = cfg.vocab_size_padded;
    int L = cfg.num_layers;
    int maxT = kv->maxT;

    /* 一次分配全部临时缓冲（无 VLA） */
    int need = C     /* x/residual */
             + C     /* ln1 */
             + 3*C   /* qkv */
             + C     /* atty */
             + C     /* attproj */
             + C     /* residual2 */
             + C     /* ln2 */
             + 4*C   /* fch */
             + C;    /* fcproj */
    float *buf = (float *)tlibc_malloc((size_t)need * sizeof(float));
    if (!buf) { __printf("gpt2_forward_kv: malloc(%d) failed\n", need); __exit(1); }

    float *x         = buf;
    float *ln1       = x + C;
    float *qkv       = ln1 + C;
    float *atty      = qkv + 3*C;
    float *attproj   = atty + C;
    float *residual2 = attproj + C;
    float *ln2       = residual2 + C;
    float *fch       = ln2 + C;
    float *fcproj    = fch + 4*C;

    /* ── Step 1: 单个 token 的 embedding ── */
    {
        const float *wte_ix = p.wte + token * C;
        const float *wpe_t  = p.wpe + pos * C;
        for (int i = 0; i < C; i++)
            x[i] = wte_ix[i] + wpe_t[i];
    }
    float *residual = x;

    /* ── Step 2: transformer layers ── */
    for (int l = 0; l < L; l++) {
        /* LayerNorm 1 (pre-attention) */
        float ln1_mean, ln1_rstd;
        layernorm_forward(ln1, &ln1_mean, &ln1_rstd,
                          residual,
                          p.ln1w + l * C, p.ln1b + l * C,
                          1, 1, C);

        /* QKV projection */
        matmul_forward(qkv, ln1,
                       p.qkw + l * C * 3 * C, p.qkb + l * 3 * C,
                       1, 1, C, 3 * C);

        /* Cache K, V */
        float *k_layer = kv->k_cache + l * maxT * C;
        float *v_layer = kv->v_cache + l * maxT * C;
        for (int i = 0; i < C; i++) {
            k_layer[pos * C + i] = qkv[C + i];      /* K: qkv 的第二个 C 元素块 */
            v_layer[pos * C + i] = qkv[2 * C + i];  /* V: qkv 的第三个 C 元素块 */
        }

        /* Causal self-attention with KV cache */
        attention_forward_kvcache(atty, qkv, k_layer, v_layer,
                                  pos, C, NH, maxT);

        /* Attention output projection */
        matmul_forward(attproj, atty,
                       p.attn_projw + l * C * C, p.attn_projb + l * C,
                       1, 1, C, C);

        /* Residual 1: residual2 = residual + attproj */
        for (int i = 0; i < C; i++)
            residual2[i] = residual[i] + attproj[i];

        /* LayerNorm 2 (pre-MLP) */
        float ln2_mean, ln2_rstd;
        layernorm_forward(ln2, &ln2_mean, &ln2_rstd,
                          residual2,
                          p.ln2w + l * C, p.ln2b + l * C,
                          1, 1, C);

        /* MLP FC + GELU */
        matmul_forward(fch, ln2,
                       p.fcw + l * C * 4 * C, p.fcb + l * 4 * C,
                       1, 1, C, 4 * C);
        for (int i = 0; i < 4 * C; i++)
            fch[i] = gelu_forward(fch[i]);

        /* MLP projection */
        matmul_forward(fcproj, fch,
                       p.projw + l * 4 * C * C, p.projb + l * C,
                       1, 1, 4 * C, C);

        /* Residual 2: fcproj (复用为 residual3) = residual2 + fcproj */
        for (int i = 0; i < C; i++)
            fcproj[i] = residual2[i] + fcproj[i];

        residual = fcproj;
    }

    /* ── Step 3: Final LayerNorm（用 x 做输出缓冲，已不需要原始 embedding）─ */
    float lnf_mean, lnf_rstd;
    layernorm_forward(x, &lnf_mean, &lnf_rstd,
                      residual,
                      p.lnfw, p.lnfb,
                      1, 1, C);

    /* ── Step 4: LM Head（logits[pos] = x @ wte）─ */
    float *logits = model->logits + pos * Vp;
    matmul_forward(logits, x, p.wte, NULL, 1, 1, C, Vp);

    tlibc_free(buf);
}

/* ==================================================================
 *  gpt2_init_kvcache — 用 prompt token 初始化 KV cache
 *
 *  逐 token 调用 gpt2_forward_kv，完成后 model->logits[n_tokens-1]
 *  处有最后一个 prompt token 的 logits，可直接用于首次采样。
 * ================================================================== */

void gpt2_init_kvcache(GPT2 *model, KVCache *kv,
                        int *tokens, int n_tokens, int maxT)
{
    int L = model->config.num_layers;
    int C = model->config.channels;

    kv->k_cache = (float *)tlibc_malloc((size_t)L * maxT * C * sizeof(float));
    kv->v_cache = (float *)tlibc_malloc((size_t)L * maxT * C * sizeof(float));
    kv->maxT = maxT;

    if (!kv->k_cache || !kv->v_cache) {
        __printf("gpt2_init_kvcache: malloc failed\n");
        __exit(1);
    }

    for (int i = 0; i < n_tokens; i++)
        gpt2_forward_kv(model, kv, tokens[i], i);
}
