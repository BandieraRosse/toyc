/* SPDX-License-Identifier: MIT */
#ifndef QWEN2_TOKENIZER_H
#define QWEN2_TOKENIZER_H

#include "llm.h"

/*
 * ByteLevel BPE tokenizer。
 *
 * 二进制词表模式下 piece 字节位于 mapping 中；tokenizer.json 模式下
 * 每个 piece 单独分配并由 owns_pieces 标记。merge_* 按 BPE rank 排列，
 * 两张开放寻址哈希表分别加速 piece -> id 和 token pair -> merge rank。
 */
typedef struct {
    void *mapping;
    size_t mapping_size;
    const unsigned char **pieces;
    unsigned int *lengths;
    int vocab_size;
    int eos_token;
    int *merge_left;
    int *merge_right;
    int *merge_result;
    int merge_count;
    int *piece_hash;
    int piece_hash_size;
    int owns_pieces;
    int *merge_hash;
    int merge_hash_size;
    int base_vocab_size;
} Qwen2Tokenizer;

/* 加载仓库自定义的 TOYTOK1 二进制词表。 */
int qwen2_tokenizer_load(Qwen2Tokenizer *tokenizer, const char *path);
/* 直接加载 Hugging Face tokenizer.json，不依赖 Python。 */
int qwen2_tokenizer_load_json(Qwen2Tokenizer *tokenizer, const char *path);
void qwen2_tokenizer_free(Qwen2Tokenizer *tokenizer);
/* 将一个 token 对应的原始字节直接写到 stdout。 */
int qwen2_tokenizer_write(const Qwen2Tokenizer *tokenizer, int token);
/* 为只有词表的 TOYTOK1 tokenizer 补充 merges.txt 中的 BPE 规则。 */
int qwen2_tokenizer_load_merges(Qwen2Tokenizer *tokenizer, const char *path);
int qwen2_tokenizer_encode(const Qwen2Tokenizer *tokenizer, const char *text,
                           int *tokens, int capacity);
int qwen2_tokenizer_build_hash(Qwen2Tokenizer *tokenizer);
int qwen2_tokenizer_piece_id(const Qwen2Tokenizer *tokenizer,
                             const unsigned char *data, unsigned int length);
int qwen2_tokenizer_build_merge_hash(Qwen2Tokenizer *tokenizer);

#endif
