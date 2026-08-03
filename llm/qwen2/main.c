/* SPDX-License-Identifier: MIT */
#include "qwen2.h"
#include "sampler.h"
#include "tokenizer.h"

int g_log_fd = -1;
int g_log_level = 0;

static int parse_int(const char *text)
{
    int value = 0;
    while (*text >= '0' && *text <= '9') value = value * 10 + (*text++ - '0');
    return value;
}

static float parse_float(const char *text)
{
    float value = 0.0f;
    float fraction = 0.1f;
    while (*text >= '0' && *text <= '9') value = value * 10.0f + (*text++ - '0');
    if (*text++ == '.') {
        while (*text >= '0' && *text <= '9') {
            value += (float)(*text++ - '0') * fraction;
            fraction *= 0.1f;
        }
    }
    return value;
}

static int load_prompt(const char *path, int **tokens, int *count)
{
    int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) return -1;
    off_t size = __lseek(fd, 0, SEEK_END);
    if (size < 4) { __close(fd); return -1; }
    void *mapping = __mmap(NULL, (size_t)size, PROT_READ, MAP_PRIVATE, fd, 0);
    __close(fd);
    if (mapping == MAP_FAILED) return -1;
    unsigned int n = *(unsigned int *)mapping;
    if (n == 0 || (size_t)size != 4 + (size_t)n * 4) {
        __munmap(mapping, (size_t)size);
        return -1;
    }
    int *copy = (int *)tlibc_malloc((size_t)n * sizeof(int));
    if (!copy) { __munmap(mapping, (size_t)size); return -1; }
    llm_memcpy(copy, (unsigned char *)mapping + 4, (size_t)n * 4);
    __munmap(mapping, (size_t)size);
    *tokens = copy;
    *count = (int)n;
    return 0;
}

static int dump_logits(const char *path, const float *logits, int count)
{
    int fd = __openat(AT_FDCWD, path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    const unsigned char *data = (const unsigned char *)logits;
    size_t remaining = (size_t)count * sizeof(float);
    while (remaining) {
        long written = __write(fd, data, remaining);
        if (written <= 0) { __close(fd); return -1; }
        data += written;
        remaining -= (size_t)written;
    }
    __close(fd);
    return 0;
}

static void usage(void)
{
    __printf("usage: llm-qwen2 --checkpoint FILE --prompt-tokens FILE"
             " | --prompt TEXT"
             " [--tokenizer FILE] [--dump-logits FILE]"
             " [--steps N] [--context N]"
             " [--temperature F] [--top-k N]\n");
}

int main(int argc, char **argv)
{
    const char *checkpoint_path = NULL;
    const char *prompt_path = NULL;
    const char *prompt_text = NULL;
    const char *tokenizer_path = NULL;
    const char *logits_path = NULL;
    int steps = 32;
    int context = 2048;
    float temperature = 0.0f;
    int top_k = 40;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--checkpoint") == 0 && i + 1 < argc)
            checkpoint_path = argv[++i];
        else if (strcmp(argv[i], "--prompt-tokens") == 0 && i + 1 < argc)
            prompt_path = argv[++i];
        else if (strcmp(argv[i], "--prompt") == 0 && i + 1 < argc)
            prompt_text = argv[++i];
        else if (strcmp(argv[i], "--tokenizer") == 0 && i + 1 < argc)
            tokenizer_path = argv[++i];
        else if (strcmp(argv[i], "--dump-logits") == 0 && i + 1 < argc)
            logits_path = argv[++i];
        else if (strcmp(argv[i], "--steps") == 0 && i + 1 < argc)
            steps = parse_int(argv[++i]);
        else if (strcmp(argv[i], "--context") == 0 && i + 1 < argc)
            context = parse_int(argv[++i]);
        else if (strcmp(argv[i], "--temperature") == 0 && i + 1 < argc)
            temperature = parse_float(argv[++i]);
        else if (strcmp(argv[i], "--top-k") == 0 && i + 1 < argc)
            top_k = parse_int(argv[++i]);
        else { usage(); return 1; }
    }
    if (!checkpoint_path || (!prompt_path && !prompt_text) ||
        (prompt_path && prompt_text) || steps < 0 || context <= 0) {
        usage();
        return 1;
    }
    int *tokens = NULL;
    int prompt_count = 0;
    Qwen2Tokenizer tokenizer;
    int have_tokenizer = tokenizer_path &&
        qwen2_tokenizer_load(&tokenizer, tokenizer_path) == 0;
    if (prompt_text) {
        char merges_path[512];
        if (!have_tokenizer) { __printf("--prompt requires --tokenizer\n"); return 1; }
        int n = 0;
        while (tokenizer_path[n] && n < (int)sizeof(merges_path) - 1) {
            merges_path[n] = tokenizer_path[n]; n++;
        }
        merges_path[n] = 0;
        while (n > 0 && merges_path[n - 1] != '/') n--;
        const char *merges_name = "merges.txt";
        int j = 0;
        while (merges_name[j] && n + j < (int)sizeof(merges_path) - 1) {
            merges_path[n + j] = merges_name[j]; j++;
        }
        merges_path[n + j] = 0;
        tokens = tlibc_malloc((size_t)context * sizeof(int));
        if (!tokens || qwen2_tokenizer_load_merges(&tokenizer, merges_path) != 0 ||
            (prompt_count = qwen2_tokenizer_encode(&tokenizer, prompt_text, tokens, context)) <= 0)
            prompt_count = 0;
    } else if (load_prompt(prompt_path, &tokens, &prompt_count) != 0) prompt_count = 0;
    if (prompt_count == 0 || prompt_count + steps > context) {
        __printf("failed to load prompt or context is too small\n");
        if (tokens) tlibc_free(tokens);
        return 1;
    }
    Qwen2 model;
    Qwen2Config config = QWEN2_5_CONFIG_0_5B;
    if (qwen2_load(&model, checkpoint_path, config, context) != 0) {
        __printf("failed to load Qwen2 checkpoint\n");
        tlibc_free(tokens);
        return 1;
    }
    const float *logits = NULL;
    for (int position = 0; position < prompt_count; position++) {
        if (qwen2_forward_token(&model, tokens[position], position, &logits) != 0) {
            __printf("prompt forward failed at position %d\n", position);
            qwen2_free(&model); tlibc_free(tokens); return 1;
        }
    }
    if (logits_path && dump_logits(logits_path, logits, config.vocab_size) != 0) {
        __printf("failed to write logits to %s\n", logits_path);
        qwen2_free(&model); tlibc_free(tokens); return 1;
    }
    Sampler sampler;
    sampler_init(&sampler, temperature, top_k, 42);
    if (tokenizer_path && !have_tokenizer)
        __printf("warning: failed to load tokenizer; printing token ids\n");
    int position = prompt_count;
    if (!have_tokenizer) __printf("generated token ids:");
    for (int generated = 0; generated < steps; generated++) {
        int token = sample_next(&sampler, logits, config.vocab_size);
        if (have_tokenizer) qwen2_tokenizer_write(&tokenizer, token);
        else __printf(" %d", token);
        if (token == 151645) break;
        if (qwen2_forward_token(&model, token, position++, &logits) != 0) break;
    }
    __printf("\n");
    if (have_tokenizer) qwen2_tokenizer_free(&tokenizer);
    qwen2_free(&model);
    tlibc_free(tokens);
    return 0;
}
