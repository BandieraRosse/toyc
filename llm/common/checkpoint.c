/* SPDX-License-Identifier: MIT */
#include "checkpoint.h"

static unsigned int read_u32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static unsigned long long read_u64(const unsigned char *p)
{
    unsigned long long lo = read_u32(p);
    unsigned long long hi = read_u32(p + 4);
    return lo | (hi << 32);
}

static int bytes_equal(const char *a, const char *b, int count)
{
    for (int i = 0; i < count; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static int bounded_name_equal(const char *entry, const char *name)
{
    for (int i = 0; i < LLM_TENSOR_NAME_SIZE; i++) {
        if (entry[i] != name[i]) return 0;
        if (name[i] == '\0') return 1;
    }
    return 0;
}

int llm_checkpoint_open(LLMCheckpoint *checkpoint, const char *path)
{
    checkpoint->mapping = NULL;
    checkpoint->mapping_size = 0;
    checkpoint->directory = NULL;
    checkpoint->tensor_count = 0;

    int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) return -1;
    off_t file_size = __lseek(fd, 0, SEEK_END);
    if (file_size < LLM_CHECKPOINT_HEADER_SIZE) { __close(fd); return -1; }
    void *mapping = __mmap(NULL, (size_t)file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    __close(fd);
    if (mapping == MAP_FAILED) return -1;

    const unsigned char *header = (const unsigned char *)mapping;
    if (!bytes_equal((const char *)header, LLM_CHECKPOINT_MAGIC, 8) ||
        read_u32(header + 8) != LLM_CHECKPOINT_VERSION) {
        __munmap(mapping, (size_t)file_size);
        return -1;
    }
    unsigned int count = read_u32(header + 12);
    unsigned long long directory_offset = read_u64(header + 16);
    unsigned long long directory_end = directory_offset +
        (unsigned long long)count * LLM_CHECKPOINT_ENTRY_SIZE;
    if (directory_offset < LLM_CHECKPOINT_HEADER_SIZE ||
        directory_end < directory_offset || directory_end > (size_t)file_size) {
        __munmap(mapping, (size_t)file_size);
        return -1;
    }
    checkpoint->mapping = mapping;
    checkpoint->mapping_size = (size_t)file_size;
    checkpoint->directory = header + directory_offset;
    checkpoint->tensor_count = (int)count;
    return 0;
}

void llm_checkpoint_close(LLMCheckpoint *checkpoint)
{
    if (checkpoint->mapping)
        __munmap(checkpoint->mapping, checkpoint->mapping_size);
    checkpoint->mapping = NULL;
    checkpoint->mapping_size = 0;
    checkpoint->directory = NULL;
    checkpoint->tensor_count = 0;
}

int llm_checkpoint_tensor(const LLMCheckpoint *checkpoint, int index,
                          LLMTensor *tensor)
{
    if (!checkpoint->mapping || index < 0 || index >= checkpoint->tensor_count)
        return -1;
    const unsigned char *entry = checkpoint->directory +
        (size_t)index * LLM_CHECKPOINT_ENTRY_SIZE;
    int dtype = (int)read_u32(entry + 64);
    int ndim = (int)read_u32(entry + 68);
    unsigned long long offset = read_u64(entry + 104);
    unsigned long long nbytes = read_u64(entry + 112);
    if (dtype != LLM_DTYPE_F32 || ndim < 1 || ndim > LLM_TENSOR_MAX_DIMS ||
        offset > checkpoint->mapping_size ||
        nbytes > checkpoint->mapping_size - offset)
        return -1;
    tensor->name = (const char *)entry;
    tensor->data = (const float *)((const unsigned char *)checkpoint->mapping + offset);
    tensor->dtype = dtype;
    tensor->ndim = ndim;
    tensor->nbytes = nbytes;
    for (int i = 0; i < LLM_TENSOR_MAX_DIMS; i++)
        tensor->shape[i] = read_u64(entry + 72 + i * 8);
    unsigned long long elements = 1;
    for (int i = 0; i < ndim; i++) {
        if (tensor->shape[i] == 0 || elements > nbytes / 4 / tensor->shape[i])
            return -1;
        elements *= tensor->shape[i];
    }
    if (elements * 4 != nbytes) return -1;
    return 0;
}

int llm_checkpoint_find(const LLMCheckpoint *checkpoint, const char *name,
                        LLMTensor *tensor)
{
    for (int i = 0; i < checkpoint->tensor_count; i++) {
        const char *entry_name = (const char *)(checkpoint->directory +
            (size_t)i * LLM_CHECKPOINT_ENTRY_SIZE);
        if (bounded_name_equal(entry_name, name))
            return llm_checkpoint_tensor(checkpoint, i, tensor);
    }
    return -1;
}
