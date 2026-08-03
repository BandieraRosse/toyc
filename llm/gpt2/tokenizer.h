/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * tokenizer.h — GPT-2 BPE Tokenizer
 *
 * 支持：
 *   decode: token ID → 文本（查表）
 *   encode: 文本 → token ID（真正的 BPE merge，非贪心近似）
 *
 * BPE merge 规则从 llm/models/bpe_merges.bin 加载
 *（由 tools/export_gpt2_bpe.py 生成）。
 * 若 merge 规则不可用，回退到贪心编码。
 *
 * toyc: __openat/__mmap ✅, tlibc_malloc ✅
 */

#ifndef TOKENIZER_H
#define TOKENIZER_H

#include "llm.h"

/* ==================================================================
 *  BPE Merge 规则（排序数组，二分查找）
 * ================================================================== */

#define BPE_BYTE_TOKENS 256

typedef struct {
    unsigned short left;       /* 父 token 1 */
    unsigned short right;      /* 父 token 2 */
    unsigned short merged_id;  /* 合并后产生的新 token ID */
} BPERule;

/* ==================================================================
 *  Tokenizer 状态
 * ================================================================== */

typedef struct {
    int init_ok;               /* 加载成功标记 */
    int vocab_size;            /* 词表大小（GPT-2 = 50257） */
    int eot_token;             /* <|endoftext|> token ID（= 50256） */
    char **token_table;        /* 词表数组：token_table[id] = 字符串 */

    /* BPE 编码所需（加载 bpe_merges.bin 后填充） */
    int bpe_ok;                /* BPE merge 规则是否可用 */
    BPERule *bpe_rules;        /* 排序后的 merge 规则数组 */
    int num_bpe_rules;         /* 规则数量（GPT-2 = 50000） */
    int byte_to_token[BPE_BYTE_TOKENS]; /* byte value → token ID */
} Tokenizer;

/* ==================================================================
 *  API
 * ================================================================== */

/*
 * 从二进制文件加载 tokenizer 词表。
 * 返回 0 成功，-1 失败。
 */
int tokenizer_init(Tokenizer *t, const char *path);

/*
 * 从二进制文件加载 BPE merge 规则（bpe_merges.bin）。
 * 可选操作；未加载时 encode 会回退到 greedy 编码。
 * 返回 0 成功，-1 失败。
 */
int tokenizer_load_merges(Tokenizer *t, const char *path);

/* 释放 tokenizer 分配的所有内存 */
void tokenizer_free(Tokenizer *t);

/*
 * token ID → 文本字符串。
 */
const char *tokenizer_decode(Tokenizer *t, int token_id);

/*
 * 文本 → token ID 序列（真正的 BPE 编码）。
 * 若 merge 规则未加载，回退到贪心编码。
 * 返回 token 数，出错返回 -1。
 */
int tokenizer_encode(Tokenizer *t, const char *text,
                     int *out_ids, int max_ids);

/*
 * 安全打印 token 字符串。
 * 只输出可打印 ASCII 和常见空白字符，过滤控制字符。
 */
void tokenizer_safe_print(const char *piece);

#endif /* TOKENIZER_H */
