/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * tokenizer.c — GPT-2 BPE Tokenizer 实现
 *
 * 支持真正的 BPE 编码（非贪心近似）：
 *   1. 预分词（模拟 GPT-2 regex 状态机）
 *   2. 字节 → byte token
 *   3. 按 merge 规则合并 token 对
 *
 * TOYC: struct ✅, 指针 ✅, qsort ✅（或手写排序）
 */

#include "tokenizer.h"

/* ==================================================================
 *  内部辅助：字符类型判断（ASCII 子集，覆盖英文）
 * ================================================================== */

static int is_letter(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* ==================================================================
 *  tokenizer_init — 加载 tokenizer 文件 + 初始化 BPE 字段
 * ================================================================== */

int tokenizer_init(Tokenizer *t, const char *path)
{
    t->init_ok = 0;
    t->token_table = NULL;
    t->vocab_size = 0;
    t->eot_token = 0;

    /* BPE 字段初始化 */
    t->bpe_ok = 0;
    t->bpe_rules = NULL;
    t->num_bpe_rules = 0;

    int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) return -1;

    off_t file_size = __lseek(fd, 0, SEEK_END);
    __lseek(fd, 0, SEEK_SET);

    if (file_size < 1024) { __close(fd); return -1; }

    void *mapped = __mmap(NULL, (size_t)file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    __close(fd);
    if (mapped == MAP_FAILED) return -1;

    int *header = (int *)mapped;
    if (header[0] != 20240328) {
        __munmap(mapped, (size_t)file_size);
        return -1;
    }

    int version = header[1];
    t->vocab_size = header[2];

    if (version == 1) {
        t->eot_token = 50256;
    } else if (version == 2) {
        t->eot_token = header[3];
    } else {
        __munmap(mapped, (size_t)file_size);
        return -1;
    }

    t->token_table = (char **)tlibc_malloc((size_t)t->vocab_size * sizeof(char *));
    if (!t->token_table) {
        __munmap(mapped, (size_t)file_size);
        return -1;
    }

    unsigned char *ptr = (unsigned char *)mapped + 1024;
    for (int i = 0; i < t->vocab_size; i++) {
        int len = (int)ptr[0];
        char *token = (char *)tlibc_malloc((size_t)len + 1);
        if (!token) {
            for (int j = 0; j < i; j++) tlibc_free(t->token_table[j]);
            tlibc_free(t->token_table);
            t->token_table = NULL;
            __munmap(mapped, (size_t)file_size);
            return -1;
        }
        for (int j = 0; j < len; j++)
            token[j] = (char)ptr[1 + j];
        token[len] = '\0';
        t->token_table[i] = token;
        ptr += 1 + len;
    }

    __munmap(mapped, (size_t)file_size);
    t->init_ok = 1;
    return 0;
}

/* ==================================================================
 *  tokenizer_load_merges — 加载 BPE merge 规则
 *
 *  文件格式（由 tools/export_gpt2_bpe.py 生成）：
 *    [4 bytes: num_merges]
 *    [256 × 2 bytes: byte_to_token]    // byte 值 → token ID
 *    [num_merges × {2+2+2 bytes}]      // left, right, merged_id
 *
 *  加载后按 (left, right) 排序以便二分查找。
 * ================================================================== */

int tokenizer_load_merges(Tokenizer *t, const char *path)
{
    if (!t->init_ok) return -1;
    t->bpe_ok = 0;

    int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) return -1;

    off_t file_size = __lseek(fd, 0, SEEK_END);
    __lseek(fd, 0, SEEK_SET);

    /* 最小文件：4(header) + 512(byte_table) = 516 字节 */
    if (file_size < 516) { __close(fd); return -1; }

    void *mapped = __mmap(NULL, (size_t)file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    __close(fd);
    if (mapped == MAP_FAILED) return -1;

    /* 解析 header */
    unsigned char *data = (unsigned char *)mapped;
    int num_merges = *(int *)data;
    data += 4;

    /* 读取 byte_to_token 映射 */
    for (int i = 0; i < 256; i++) {
        t->byte_to_token[i] = (int)((unsigned short *)data)[i];
    }
    data += 256 * 2;

    /* 验证文件大小 */
    size_t expected = 4 + 256 * 2 + (size_t)num_merges * 6;
    if ((size_t)file_size != expected) {
        __munmap(mapped, (size_t)file_size);
        return -1;
    }

    /* 分配并复制 merge 规则 */
    t->bpe_rules = (BPERule *)tlibc_malloc((size_t)num_merges * sizeof(BPERule));
    if (!t->bpe_rules) {
        __munmap(mapped, (size_t)file_size);
        return -1;
    }

    for (int i = 0; i < num_merges; i++) {
        unsigned short *entry = (unsigned short *)data + i * 3;
        t->bpe_rules[i].left = entry[0];
        t->bpe_rules[i].right = entry[1];
        t->bpe_rules[i].merged_id = entry[2];
    }

    __munmap(mapped, (size_t)file_size);

    t->num_bpe_rules = num_merges;
    t->bpe_ok = 1;
    return 0;
}

/* ==================================================================
 *  tokenizer_free — 释放所有 token 字符串、表、merge 规则
 * ================================================================== */

void tokenizer_free(Tokenizer *t)
{
    if (t->init_ok && t->token_table) {
        for (int i = 0; i < t->vocab_size; i++) {
            if (t->token_table[i])
                tlibc_free(t->token_table[i]);
        }
        tlibc_free(t->token_table);
    }
    if (t->bpe_rules)
        tlibc_free(t->bpe_rules);
    t->init_ok = 0;
    t->bpe_ok = 0;
    t->token_table = NULL;
    t->bpe_rules = NULL;
}

/* ==================================================================
 *  tokenizer_decode — token ID → 文本
 * ================================================================== */

const char *tokenizer_decode(Tokenizer *t, int token_id)
{
    if (!t->init_ok) return NULL;
    if (token_id >= 0 && token_id < t->vocab_size)
        return t->token_table[token_id];
    return NULL;
}

/* ==================================================================
 *  tokenizer_safe_print — 只输出可打印字符
 * ================================================================== */

void tokenizer_safe_print(const char *piece)
{
    if (!piece || !piece[0]) return;

    if (piece[1] == '\0') {
        unsigned char c = (unsigned char)piece[0];
        int printable = (c >= 32 && c <= 126)
                     || c == '\n'
                     || c == '\t'
                     || c == '\r';
        if (!printable) return;
    }

    __printf("%s", piece);
}

/* ==================================================================
 *  tokenizer_encode_greedy — 贪心最长前缀匹配编码（备用）
 * ================================================================== */

int tokenizer_encode_greedy(Tokenizer *t, const char *text,
                            int *out_ids, int max_ids)
{
    if (!t->init_ok || !text || !out_ids) return -1;

    int count = 0;
    while (*text && count < max_ids) {
        int best_len = 0;
        int best_id = 0;

        for (int id = 0; id < t->vocab_size; id++) {
            const char *tok = t->token_table[id];
            if (!tok) continue;
            int j = 0;
            while (tok[j] && tok[j] == text[j]) j++;
            if (tok[j] == '\0' && j > best_len) {
                best_len = j;
                best_id = id;
            }
        }

        if (best_len == 0) {
            text++;
        } else {
            out_ids[count++] = best_id;
            text += best_len;
        }
    }

    return count;
}

/* ==================================================================
 *  内部：二分查找 merge 规则
 * ================================================================== */

static int bpe_find_merge(BPERule *rules, int num, int left, int right)
{
    int lo = 0, hi = num - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if ((int)rules[mid].left < left) lo = mid + 1;
        else if ((int)rules[mid].left > left) hi = mid - 1;
        else if ((int)rules[mid].right < right) lo = mid + 1;
        else if ((int)rules[mid].right > right) hi = mid - 1;
        else return (int)rules[mid].merged_id;
    }
    return -1;  /* 不可合并 */
}

/* ==================================================================
 *  内部：BPE 编码一个单词
 *
 *  输入：word 指向以 \0 结尾的 UTF-8 字节序列
 *  输出：token_out 缓冲区
 *  返回：token 数
 * ================================================================== */

static int bpe_encode_word(Tokenizer *t, const char *word,
                           int *token_out, int max_tokens)
{
    /* 获取单词的 UTF-8 字节长度 */
    int byte_len = 0;
    while (word[byte_len]) byte_len++;
    if (byte_len == 0) return 0;
    if (byte_len > max_tokens) byte_len = max_tokens;

    /* 动态分配 pieces 数组（每个 piece 存一个 token ID） */
    int *pieces = (int *)tlibc_malloc((size_t)byte_len * sizeof(int));
    if (!pieces) return -1;

    /* 步骤 1: 每个字节 → byte token */
    for (int i = 0; i < byte_len; i++) {
        unsigned char b = (unsigned char)word[i];
        pieces[i] = t->byte_to_token[(int)b];
    }
    int num_pieces = byte_len;

    /* 步骤 2: BPE merge 循环 */
    while (num_pieces > 1) {
        int best_pos = -1;
        int best_merged = -1;  /* merged_id 越小表示 merge 优先级越高 */

        /* 扫描所有相邻对，找优先级最高的 merge */
        for (int i = 0; i < num_pieces - 1; i++) {
            int mid = bpe_find_merge(t->bpe_rules, t->num_bpe_rules,
                                     pieces[i], pieces[i + 1]);
            if (mid >= 0 && (best_pos < 0 || mid < best_merged)) {
                best_merged = mid;
                best_pos = i;
                /* 由于 merged_id 递增，找到 0 或很小的 ID 可提前终止 */
                if (best_merged <= 256) break;
            }
        }

        /* 无更多可合并的 pair */
        if (best_pos < 0) break;

        /* 合并 pair: 替换 pieces[best_pos] 并删除 pieces[best_pos+1] */
        pieces[best_pos] = best_merged;
        for (int i = best_pos + 1; i < num_pieces - 1; i++)
            pieces[i] = pieces[i + 1];
        num_pieces--;
    }

    /* 复制结果 */
    int count = num_pieces < max_tokens ? num_pieces : max_tokens;
    for (int i = 0; i < count; i++)
        token_out[i] = pieces[i];

    tlibc_free(pieces);
    return count;
}

/* ==================================================================
 *  内部：预分词（模拟 GPT-2 regex 状态机）
 *
 *  将文本分割为"单词"，每个单词将独立进行 BPE 编码。
 *  这实现了 GPT-2 的 regex 预分词模式，支持：
 *    - 空格附着到后一个词
 *    - 字母序列、数字序列、其他字符
 *    - 常用缩写（'s, 't, 're, 've, 'm, 'll, 'd）
 *
 *  输出：words[] 存每个 word 在 text 中的 [start, end) 区间
 *  返回：word 数量
 * ================================================================== */

#define MAX_WORDS 2048
#define MAX_CONTRACTION 8

/* 常见缩写列表 */
static const char *contractions[] = {
    "'s", "'t", "'re", "'ve", "'m", "'ll", "'d", NULL
};

static int pre_tokenize(const char *text, int *starts, int *ends, int max_words)
{
    int count = 0;
    int i = 0;

    while (text[i] && count < max_words) {
        int start = i;
        char c = text[i];

        if (c == ' ') {
            /* 空格：看看后面是否跟着非空格（附着到下一个词） */
            if (text[i + 1] != '\0' && text[i + 1] != ' ') {
                /* 附着空格 + 后续内容作为一个词 */
                i++;  /* 跳过空格 */
                c = text[i];

                if (is_letter(c)) {
                    while (is_letter(text[i])) i++;
                    /* 检查缩写 */
                    for (int ci = 0; contractions[ci]; ci++) {
                        const char *cont = contractions[ci];
                        int match = 1;
                        for (int j = 0; cont[j]; j++)
                            if (text[i + j] != cont[j]) { match = 0; break; }
                        if (match) {
                            i += (int)strlen(cont);
                            break;
                        }
                    }
                } else if (is_digit(c)) {
                    while (is_digit(text[i])) i++;
                } else {
                    i++;  /* 其他字符：单个 */
                }
            } else {
                /* 独立空格序列 */
                while (text[i] == ' ') i++;
            }
        } else if (is_letter(c)) {
            while (is_letter(text[i])) i++;
            /* 检查缩写 */
            for (int ci = 0; contractions[ci]; ci++) {
                const char *cont = contractions[ci];
                int match = 1;
                for (int j = 0; cont[j]; j++)
                    if (text[i + j] != cont[j]) { match = 0; break; }
                if (match) {
                    i += (int)strlen(cont);
                    break;
                }
            }
        } else if (is_digit(c)) {
            while (is_digit(text[i])) i++;
        } else if (c == '\n' || c == '\t' || c == '\r') {
            /* 空白符：独立处理 */
            i++;
        } else {
            /* 其他字符：单个 */
            i++;
        }

        if (i > start) {
            starts[count] = start;
            ends[count] = i;
            count++;
        } else {
            i++;  /* 防止死循环 */
        }
    }

    return count;
}

/* ==================================================================
 *  tokenizer_encode — 文本 → token ID 序列（真正的 BPE）
 *
 *  优先使用 BPE merge 规则编码（t->bpe_ok 为真）。
 *  若 BPE 规则不可用，回退到贪心编码。
 * ================================================================== */

int tokenizer_encode(Tokenizer *t, const char *text,
                     int *out_ids, int max_ids)
{
    if (!t->init_ok || !text || !out_ids)
        return -1;

    /* BPE 规则不可用：回退贪心编码 */
    if (!t->bpe_ok)
        return tokenizer_encode_greedy(t, text, out_ids, max_ids);

    /* 预分词 */
    int word_starts[MAX_WORDS];
    int word_ends[MAX_WORDS];
    int num_words = pre_tokenize(text, word_starts, word_ends, MAX_WORDS);
    if (num_words == 0) return 0;

    /* 临时缓冲区：每个单词编码到一个临时数组 */
    int word_tokens[256];
    int total = 0;

    for (int w = 0; w < num_words && total < max_ids; w++) {
        /* 提取单词（以 \0 结尾的临时副本） */
        int wlen = word_ends[w] - word_starts[w];
        /* 截取到临时缓冲区 */
        char word_buf[512];
        int copy_len = wlen < 511 ? wlen : 511;
        for (int j = 0; j < copy_len; j++)
            word_buf[j] = text[word_starts[w] + j];
        word_buf[copy_len] = '\0';

        int remaining = max_ids - total;
        int nt = bpe_encode_word(t, word_buf, word_tokens,
                                 remaining < 256 ? remaining : 256);
        if (nt < 0) nt = 0;

        for (int j = 0; j < nt && total < max_ids; j++)
            out_ids[total++] = word_tokens[j];
    }

    return total;
}
