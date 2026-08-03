/* SPDX-License-Identifier: MIT */
#include "tokenizer.h"

static unsigned int read_u32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

int qwen2_tokenizer_load(Qwen2Tokenizer *tokenizer, const char *path)
{
    tokenizer->mapping = NULL;
    tokenizer->pieces = NULL;
    tokenizer->lengths = NULL;
    int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) return -1;
    off_t size = __lseek(fd, 0, SEEK_END);
    if (size < 20) { __close(fd); return -1; }
    void *mapping = __mmap(NULL, (size_t)size, PROT_READ, MAP_PRIVATE, fd, 0);
    __close(fd);
    if (mapping == MAP_FAILED) return -1;
    const unsigned char *data = (const unsigned char *)mapping;
    const char magic[8] = {'T','O','Y','T','O','K','1','\0'};
    for (int i = 0; i < 8; i++) if (data[i] != (unsigned char)magic[i]) goto fail;
    if (read_u32(data + 8) != 1) goto fail;
    unsigned int vocab_size = read_u32(data + 12);
    if (vocab_size == 0 || vocab_size > 1000000) goto fail;
    tokenizer->pieces = (const unsigned char **)tlibc_malloc(
        (size_t)vocab_size * sizeof(unsigned char *));
    tokenizer->lengths = (unsigned int *)tlibc_malloc(
        (size_t)vocab_size * sizeof(unsigned int));
    if (!tokenizer->pieces || !tokenizer->lengths) goto fail;
    size_t offset = 20;
    for (unsigned int i = 0; i < vocab_size; i++) {
        if (offset + 4 > (size_t)size) goto fail;
        unsigned int length = read_u32(data + offset);
        offset += 4;
        if (length > (size_t)size - offset) goto fail;
        tokenizer->pieces[i] = data + offset;
        tokenizer->lengths[i] = length;
        offset += length;
    }
    if (offset != (size_t)size) goto fail;
    tokenizer->mapping = mapping;
    tokenizer->mapping_size = (size_t)size;
    tokenizer->vocab_size = (int)vocab_size;
    tokenizer->eos_token = (int)read_u32(data + 16);
    return 0;
fail:
    if (tokenizer->pieces) tlibc_free(tokenizer->pieces);
    if (tokenizer->lengths) tlibc_free(tokenizer->lengths);
    tokenizer->pieces = NULL;
    tokenizer->lengths = NULL;
    __munmap(mapping, (size_t)size);
    return -1;
}

void qwen2_tokenizer_free(Qwen2Tokenizer *tokenizer)
{
    if (tokenizer->pieces) tlibc_free(tokenizer->pieces);
    if (tokenizer->lengths) tlibc_free(tokenizer->lengths);
    if (tokenizer->mapping) __munmap(tokenizer->mapping, tokenizer->mapping_size);
    tokenizer->pieces = NULL;
    tokenizer->lengths = NULL;
    tokenizer->mapping = NULL;
}

int qwen2_tokenizer_write(const Qwen2Tokenizer *tokenizer, int token)
{
    if (!tokenizer || token < 0 || token >= tokenizer->vocab_size) return -1;
    unsigned int length = tokenizer->lengths[token];
    if (length && __write(1, tokenizer->pieces[token], length) < 0) return -1;
    return 0;
}
