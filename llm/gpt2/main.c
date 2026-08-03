/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * main.c — GPT-2 自回归文本生成入口
 *
 * 用法：
 *   ./build/llm                         → 交互模式
 *   ./build/llm Hello world             → 直接 prompt，生成一次
 *   ./build/llm --prompt /tmp/p.bin     → 从 token 文件加载
 *   ./build/llm --temperature 0 --steps 50 Hello → 带参数
 *
 * TOYC: 所有 I/O 通过 __openat/__mmap/__read 系统调用，零 libc 依赖。
 */

#include "llm.h"
#include "gpt2.h"
#include "tokenizer.h"
#include "sampler.h"

/* ==================================================================
 *  辅助函数
 * ================================================================== */

static int parse_int(const char *s)
{
    int n = 0, sign = 1;
    if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9')
        n = n * 10 + (*s++ - '0');
    return n * sign;
}

static float parse_float(const char *s)
{
    float n = 0.0f, sign = 1.0f;
    if (*s == '-') { sign = -1.0f; s++; }
    while (*s >= '0' && *s <= '9')
        n = n * 10.0f + (float)(*s++ - '0');
    if (*s == '.') {
        s++;
        float frac = 0.0f, div = 10.0f;
        while (*s >= '0' && *s <= '9') {
            frac += (float)(*s++ - '0') / div;
            div *= 10.0f;
        }
        n += frac;
    }
    return n * sign;
}

static void print_help(void)
{
    __printf("Usage: ./build/llm [options] [prompt text...]\n");
    __printf("\n");
    __printf("  No arguments              Interactive mode\n");
    __printf("  prompt text               Tokenize greedily and generate\n");
    __printf("\n");
    __printf("Options:\n");
    __printf("  --model PATH        checkpoint (default: llm/models/gpt2_124M.bin)\n");
    __printf("  --tokenizer PATH    tokenizer  (default: llm/models/gpt2_tokenizer.bin)\n");
    __printf("  --steps N           generation steps (default: 100)\n");
    __printf("  --temperature T     temperature (default: 0.8, 0=greedy)\n");
    __printf("  --top-k K           top-k sampling (default: 40, 0=unlimited)\n");
    __printf("  --seed N            random seed (default: 42)\n");
    __printf("  --prompt PATH       load binary token file (overrides text prompt)\n");
    __printf("  --no-kv             disable KV cache (use naive full forward)\n");
    __printf("  --log FILE          override log path (default: llm/models/gpt2_debug.log)\n");
    __printf("  --no-log            disable log file\n");
    __printf("  --log-level N       1=step summary, 2=+per-layer, 3=+tensor diag\n");
    __printf("  -h, --help          show this help\n");
    __printf("\n");
    __printf("KV cache is enabled by default (~30-50x faster). Use --no-kv for\n");
    __printf("the original per-step full-sequence forward pass.\n");
}

/* ==================================================================
 *  DEBUG: Level 1 — 每步 top-5 候选词日志
 *
 *  从 raw logits 找 logit 最大的 5 个 token ID，做 top-5 内 softmax
 *  得相对概率，按  "token_id(token_text)=xx.x%"  格式输出到日志 fd。
 *  选中词前带 * 标记。token 文本中的 \n \r \t 会被转义以免断行。
 *
 *  不影响核心生成逻辑；无 --log 时 g_log_fd < 0，本函数不被调用。
 * ================================================================== */

/* 写 token 文本到日志 fd，转义 \n \r \t 避免断行 */
static void dump_token_text(int fd, const char *s)
{
    char buf[256];
    int pos = 0;
    if (!s) { __write(fd, "(null)", 6); return; }
    for (int i = 0; s[i] && pos < (int)sizeof(buf) - 5; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\n') { buf[pos++] = '\\'; buf[pos++] = 'n'; }
        else if (c == '\r') { buf[pos++] = '\\'; buf[pos++] = 'r'; }
        else if (c == '\t') { buf[pos++] = '\\'; buf[pos++] = 't'; }
        else if (c >= 32) { buf[pos++] = (char)c; }
        else {
            /* 其他控制字符转 ^X */
            buf[pos++] = '^';
            buf[pos++] = (char)('@' + c);
        }
    }
    buf[pos] = '\0';
    __fprintf(fd, "%s", buf);
}

static void log_top5(int fd, const float *logits, int V,
                     Tokenizer *tokenizer, int selected, int step, int abs_pos)
{
    int   top5_id[5] = {0};
    float top5_val[5] = {-1e10f, -1e10f, -1e10f, -1e10f, -1e10f};

    /* 找 raw logit 最大的 5 个 */
    for (int v = 0; v < V; v++) {
        float val = logits[v];
        for (int k = 0; k < 5; k++) {
            if (val > top5_val[k]) {
                for (int j = 4; j > k; j--) {
                    top5_id[j]  = top5_id[j-1];
                    top5_val[j] = top5_val[j-1];
                }
                top5_id[k]  = v;
                top5_val[k] = val;
                break;
            }
        }
    }

    /* top-5 内 softmax 得相对概率 */
    float maxv = top5_val[0];
    float sum_exp = 0.0f;
    for (int k = 0; k < 5; k++) {
        top5_val[k] = expf_(top5_val[k] - maxv);
        sum_exp += top5_val[k];
    }

    __fprintf(fd, "[GEN %d @%d]", step, abs_pos);
    __fprintf(fd, " token %d(", selected);
    dump_token_text(fd, tokenizer_decode(tokenizer, selected));
    __fprintf(fd, ") top5:");
    for (int k = 0; k < 5; k++) {
        float pct = 100.0f * (sum_exp > 0.0f ? top5_val[k] / sum_exp : 0.0f);
        const char *mark = (top5_id[k] == selected) ? "*" : " ";
        __fprintf(fd, " %s%d(", mark, top5_id[k]);
        dump_token_text(fd, tokenizer_decode(tokenizer, top5_id[k]));
        __fprintf(fd, ")=%.1f%%", pct);
    }
    __fprintf(fd, "\n");
}

/* ==================================================================
 *  核心生成逻辑（从 token 缓冲区生成文本）
 *
 *  参数：
 *    model, tokenizer, sampler — 已初始化
 *    tokens, pos               — token 缓冲区 + 已有 token 数
 *    steps                     — 最大生成步数
 *  返回：生成的 token 数
 * ================================================================== */

static int generate_tokens(GPT2 *model, Tokenizer *tokenizer,
                           Sampler *sampler, int steps, int start_pos,
                           int use_kv)
{
    int maxT = model->config.max_seq_len;
    int Vp = model->config.vocab_size_padded;
    int V = model->config.vocab_size;
    int gen_count = 0;

    /* ── 朴素模式（无 KV cache）：每步重新计算完整序列 ── */
    if (!use_kv) {
        int pos = start_pos;
        if (pos == 0) {
            model->tokens[pos++] = tokenizer->eot_token;
        }
        for (int t = pos; gen_count < steps && t < maxT; t++) {
            gpt2_forward(model, model->tokens, 1, t);
            float *logits = model->logits + (t - 1) * Vp;
            int next = sample_next(sampler, logits, V);
            if (next == tokenizer->eot_token) break;

            /* DEBUG: Level 1 — top-5 日志 */
            if (g_log_level >= 1)
                log_top5(g_log_fd, logits, V, tokenizer, next, gen_count, t - 1);

            const char *token_str = tokenizer_decode(tokenizer, next);
            tokenizer_safe_print(token_str);
            model->tokens[t] = next;
            gen_count++;
        }
        return gen_count;
    }

    /* ── KV Cache 模式：逐 token 前向（~30-50x 加速） ── */
    KVCache kv;
    gpt2_init_kvcache(model, &kv, model->tokens, start_pos, maxT);

    int pos = start_pos;
    if (pos == 0) {
        /* 无 prompt：从 EOT 种子开始 */
        model->tokens[0] = tokenizer->eot_token;
        gpt2_forward_kv(model, &kv, tokenizer->eot_token, 0);
        pos = 1;
    }

    for (int t = pos; gen_count < steps && t < maxT; t++) {
        /* 采样：从上一大步的 logits 预测当前 token */
        float *logits = model->logits + (t - 1) * Vp;
        int next = sample_next(sampler, logits, V);
        if (next == tokenizer->eot_token) break;

        /* DEBUG: Level 1 — top-5 日志 */
        if (g_log_level >= 1)
            log_top5(g_log_fd, logits, V, tokenizer, next, gen_count, t - 1);

        const char *token_str = tokenizer_decode(tokenizer, next);
        tokenizer_safe_print(token_str);
        model->tokens[t] = next;
        gen_count++;

        /* 新 token 前向：更新 KV cache + 写入当前位置的 logits */
        gpt2_forward_kv(model, &kv, next, t);
    }

    tlibc_free(kv.k_cache);
    tlibc_free(kv.v_cache);

    /* DEBUG: 生成完毕摘要 */
    if (g_log_level >= 1 && gen_count > 0)
        __fprintf(g_log_fd, "[GEN] done: %d tokens generated\n", gen_count);

    return gen_count;
}

/* ==================================================================
 *  交互模式
 * ================================================================== */

static void run_interactive(GPT2 *model, Tokenizer *tokenizer,
                            float temperature, int top_k, int seed, int steps,
                            int use_kv)
{
    Sampler sampler;
    sampler_init(&sampler, temperature, top_k, (unsigned long long)seed);

    __printf("Interactive mode. Type a prompt or 'quit' to exit.\n");

    char buf[4096];
    int prompt_tokens[1024];
    int maxT = model->config.max_seq_len;

    while (1) {
        __printf("\n> ");

        /* 从 stdin 读一行 */
        int len = 0;
        while (len < (int)sizeof(buf) - 1) {
            char c;
            long n = __read(0, &c, 1);
            if (n <= 0) { buf[len] = '\0'; break; }
            if (c == '\n') { buf[len] = '\0'; break; }
            buf[len++] = c;
        }
        buf[len] = '\0';

        if (len == 0) break;  /* EOF */
        if (strcmp(buf, "quit") == 0 || strcmp(buf, "exit") == 0) break;

        /* BPE 编码 */
        int nt = tokenizer_encode(tokenizer, buf, prompt_tokens,
                                         maxT - 1);
        if (nt <= 0) {
            __printf("(no tokens generated from that input)\n");
            continue;
        }

        __printf("  [%s]\n", buf);
        __printf("  (%d tokens:", nt);
        for (int i = 0; i < nt && i < 8; i++)
            __printf(" %d", prompt_tokens[i]);
        if (nt > 8) __printf(" ...");
        __printf(")\n");

        /* 填充 token 缓冲区并清空旧生成结果 */
        for (int i = 0; i < maxT; i++)
            model->tokens[i] = 0;
        for (int i = 0; i < nt; i++)
            model->tokens[i] = prompt_tokens[i];

        /* 生成 */
        __printf("---\n");
        int gc = generate_tokens(model, tokenizer, &sampler, steps, nt, use_kv);
        __printf("\n---\n");
        __printf("(%d tokens generated)\n", gc);
    }
}

/* ==================================================================
 *  主函数
 * ================================================================== */

int main(int argc, char *argv[])
{
    /* ── 默认参数 ── */
    const char *model_path = "llm/models/gpt2_124M.bin";
    const char *tokenizer_path = "llm/models/gpt2_tokenizer.bin";
    const char *prompt_path = NULL;
    int steps = 100;
    float temperature = 0.8f;
    int top_k = 40;
    int seed = 42;
    int use_kv = 1;

    /* ── 默认日志（覆盖 --log 可改路径，--no-log 可关闭） ── */
    g_log_fd = __openat(AT_FDCWD, "llm/models/gpt2_debug.log",
                        O_WRONLY | O_CREAT | O_TRUNC, 0644);
    /* 打开失败静默降级为无日志，不阻断运行 */

    /* 收集非选项参数作为 prompt 文本 */
    char prompt_text[4096] = {0};
    int prompt_pos = 0;

    /* ── 解析命令行参数 ── */
    int i = 1;
    while (i < argc) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
                model_path = argv[++i];
            } else if (strcmp(argv[i], "--tokenizer") == 0 && i + 1 < argc) {
                tokenizer_path = argv[++i];
            } else if (strcmp(argv[i], "--steps") == 0 && i + 1 < argc) {
                steps = parse_int(argv[++i]);
            } else if (strcmp(argv[i], "--temperature") == 0 && i + 1 < argc) {
                temperature = parse_float(argv[++i]);
            } else if (strcmp(argv[i], "--top-k") == 0 && i + 1 < argc) {
                top_k = parse_int(argv[++i]);
            } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
                seed = parse_int(argv[++i]);
            } else if (strcmp(argv[i], "--prompt") == 0 && i + 1 < argc) {
                prompt_path = argv[++i];
            } else if (strcmp(argv[i], "--no-kv") == 0) {
                use_kv = 0;
            } else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc && argv[i+1][0] != '-') {
                /* 覆盖日志路径 */
                if (g_log_fd >= 0) __close(g_log_fd);
                g_log_fd = __openat(AT_FDCWD, argv[++i],
                                    O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (g_log_fd < 0) {
                    __printf("Failed to open log file\n");
                    return 1;
                }
            } else if (strcmp(argv[i], "--no-log") == 0) {
                if (g_log_fd >= 0) { __close(g_log_fd); g_log_fd = -1; }
                g_log_level = 0;
            } else if (strcmp(argv[i], "--log-level") == 0 && i + 1 < argc) {
                g_log_level = parse_int(argv[++i]);
            } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
                print_help();
                return 0;
            } else {
                __printf("Unknown option: %s\n", argv[i]);
                __printf("Try ./build/llm --help\n");
                return 1;
            }
        } else {
            /* 非选项参数：拼接到 prompt_text */
            if (prompt_pos > 0 && prompt_pos < (int)sizeof(prompt_text) - 1)
                prompt_text[prompt_pos++] = ' ';
            while (*argv[i] && prompt_pos < (int)sizeof(prompt_text) - 1)
                prompt_text[prompt_pos++] = *argv[i]++;
        }
        i++;
    }
    prompt_text[prompt_pos] = '\0';

    /* ── 加载模型 ── */
    __printf("Loading model from %s ... ", model_path);
    GPT2 model;
    GPT2Config config = GPT2_CONFIG_124M;
    if (gpt2_init(&model, config) != 0) {
        __printf("FAILED (gpt2_init)\n");
        return 1;
    }
    if (gpt2_load_weights(&model, model_path) != 0) {
        __printf("FAILED (gpt2_load_weights)\n");
        gpt2_free(&model);
        return 1;
    }
    __printf("ok (%ld params, %ld MB)\n",
             (long)model.total_params,
             (long)(model.total_bytes / (1024 * 1024)));

    /* ── 加载 tokenizer ── */
    __printf("Loading tokenizer from %s ... ", tokenizer_path);
    Tokenizer tokenizer;
    if (tokenizer_init(&tokenizer, tokenizer_path) != 0) {
        __printf("FAILED\n");
        gpt2_free(&model);
        return 1;
    }
    __printf("ok (vocab=%d, eot=%d)\n",
             tokenizer.vocab_size, tokenizer.eot_token);

    /* 加载 BPE merge 规则（可选） */
    if (tokenizer_load_merges(&tokenizer, "llm/models/bpe_merges.bin") == 0) {
        __printf("Loaded %d BPE merge rules\n", tokenizer.num_bpe_rules);
    } else {
        __printf("BPE merges not available, using greedy encoding\n");
    }

    /* ── 初始化采样器 ── */
    Sampler sampler;
    sampler_init(&sampler, temperature, top_k, (unsigned long long)seed);

    int maxT = model.config.max_seq_len;
    int ret = 0;

    /* ── 模式派发 ── */
    if (prompt_path) {
        /* 模式 A：从二进制 token 文件加载 prompt */
        int fd = __openat(AT_FDCWD, prompt_path, O_RDONLY, 0);
        if (fd < 0) {
            __printf("Cannot open prompt file: %s\n", prompt_path);
            ret = 1;
            goto cleanup;
        }
        off_t fsize = __lseek(fd, 0, SEEK_END);
        __lseek(fd, 0, SEEK_SET);
        void *mapped = __mmap(NULL, (size_t)fsize, PROT_READ, MAP_PRIVATE, fd, 0);
        __close(fd);
        if (mapped == MAP_FAILED) {
            __printf("mmap prompt file failed\n");
            ret = 1;
            goto cleanup;
        }
        int count = *(int *)mapped;
        if (count > maxT - 1) {
            __printf("Truncating prompt from %d to %d tokens\n", count, maxT - 1);
            count = maxT - 1;
        }
        for (int j = 0; j < count; j++)
            model.tokens[j] = ((int *)mapped + 1)[j];
        __munmap(mapped, (size_t)fsize);
        __printf("Loaded %d prompt tokens\n", count);

        __printf("\n---\n");
        int gc = generate_tokens(&model, &tokenizer, &sampler, steps, count, use_kv);
        __printf("\n---\n");
        __printf("Generated %d tokens\n", gc);

    } else if (prompt_pos > 0) {
        /* 模式 B：命令行文本 prompt → BPE 编码 → 生成一次 */
        int prompt_tokens[1024];
        int nt = tokenizer_encode(&tokenizer, prompt_text,
                                         prompt_tokens, maxT - 1);
        if (nt <= 0) {
            __printf("Failed to tokenize prompt\n");
            ret = 1;
            goto cleanup;
        }
        for (int j = 0; j < nt; j++)
            model.tokens[j] = prompt_tokens[j];

        __printf("Prompt (%d tokens): %s\n", nt, prompt_text);
        __printf("KV cache: %s\n", use_kv ? "ON" : "OFF");
        __printf("\n---\n");
        int gc = generate_tokens(&model, &tokenizer, &sampler, steps, nt, use_kv);
        __printf("\n---\n");
        __printf("Generated %d tokens\n", gc);

    } else {
        /* 模式 C：交互模式 */
        run_interactive(&model, &tokenizer, temperature, top_k, seed, steps, use_kv);
    }

cleanup:
    tokenizer_free(&tokenizer);
    gpt2_free(&model);
    return ret;
}
