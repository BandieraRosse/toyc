/* SPDX-License-Identifier: MIT */
#ifndef LLM_CHECKPOINT_H
#define LLM_CHECKPOINT_H

#include "llm.h"

#define LLM_CHECKPOINT_MAGIC "TOYLLM1"
#define LLM_CHECKPOINT_VERSION 1
#define LLM_CHECKPOINT_HEADER_SIZE 32
#define LLM_CHECKPOINT_ENTRY_SIZE 128
#define LLM_TENSOR_NAME_SIZE 64
#define LLM_TENSOR_MAX_DIMS 4
#define LLM_DTYPE_F32 1

typedef struct {
    const char *name;
    const float *data;
    unsigned long long shape[LLM_TENSOR_MAX_DIMS];
    unsigned long long nbytes;
    int ndim;
    int dtype;
} LLMTensor;

typedef struct {
    void *mapping;
    size_t mapping_size;
    const unsigned char *directory;
    int tensor_count;
    LLMTensor *tensors;
    void **owned_data;
} LLMCheckpoint;

int llm_checkpoint_open(LLMCheckpoint *checkpoint, const char *path);
void llm_checkpoint_close(LLMCheckpoint *checkpoint);
int llm_checkpoint_tensor(const LLMCheckpoint *checkpoint, int index,
                          LLMTensor *tensor);
int llm_checkpoint_find(const LLMCheckpoint *checkpoint, const char *name,
                        LLMTensor *tensor);

#endif
