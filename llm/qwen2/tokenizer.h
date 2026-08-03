/* SPDX-License-Identifier: MIT */
#ifndef QWEN2_TOKENIZER_H
#define QWEN2_TOKENIZER_H

#include "llm.h"

typedef struct {
    void *mapping;
    size_t mapping_size;
    const unsigned char **pieces;
    unsigned int *lengths;
    int vocab_size;
    int eos_token;
} Qwen2Tokenizer;

int qwen2_tokenizer_load(Qwen2Tokenizer *tokenizer, const char *path);
void qwen2_tokenizer_free(Qwen2Tokenizer *tokenizer);
int qwen2_tokenizer_write(const Qwen2Tokenizer *tokenizer, int token);

#endif
