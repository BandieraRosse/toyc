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
    tokenizer->merge_left = tokenizer->merge_right = tokenizer->merge_result = NULL;
    tokenizer->merge_count = 0;
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
    if (tokenizer->merge_left) tlibc_free(tokenizer->merge_left);
    if (tokenizer->merge_right) tlibc_free(tokenizer->merge_right);
    if (tokenizer->merge_result) tlibc_free(tokenizer->merge_result);
    tokenizer->pieces = NULL;
    tokenizer->lengths = NULL;
    tokenizer->mapping = NULL;
}

static int piece_id(const Qwen2Tokenizer *t, const unsigned char *data,
                    unsigned int length)
{
    for (int i = 0; i < t->vocab_size; i++) {
        if (t->lengths[i] != length) continue;
        unsigned int j = 0;
        while (j < length && t->pieces[i][j] == data[j]) j++;
        if (j == length) return i;
    }
    return -1;
}

static int decode_utf8_char(const unsigned char **cursor,
                            const unsigned char *end)
{
    const unsigned char *p = *cursor;
    if (p >= end) return -1;
    int value, extra;
    if (*p < 0x80) { value = *p++; extra = 0; }
    else if ((*p & 0xe0) == 0xc0) { value = *p++ & 0x1f; extra = 1; }
    else if ((*p & 0xf0) == 0xe0) { value = *p++ & 0x0f; extra = 2; }
    else return -1;
    while (extra--) { if (p >= end || (*p & 0xc0) != 0x80) return -1; value = (value << 6) | (*p++ & 0x3f); }
    *cursor = p; return value;
}

static int byte_unicode_decode(int cp)
{
    if ((cp >= 33 && cp <= 126) || (cp >= 161 && cp <= 172) ||
        (cp >= 174 && cp <= 255)) return cp;
    int extra = 0;
    for (int b = 0; b < 256; b++) {
        if ((b >= 33 && b <= 126) || (b >= 161 && b <= 172) ||
            (b >= 174 && b <= 255)) continue;
        if (cp == 256 + extra) return b;
        extra++;
    }
    return -1;
}

int qwen2_tokenizer_load_merges(Qwen2Tokenizer *t, const char *path)
{
    int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) return -1;
    off_t size = __lseek(fd, 0, SEEK_END);
    if (size <= 0) { __close(fd); return -1; }
    unsigned char *data = __mmap(NULL, (size_t)size, PROT_READ, MAP_PRIVATE, fd, 0);
    __close(fd); if (data == MAP_FAILED) return -1;
    int lines = 0; for (off_t i = 0; i < size; i++) if (data[i] == '\n') lines++;
    t->merge_left = tlibc_malloc((size_t)lines * sizeof(int));
    t->merge_right = tlibc_malloc((size_t)lines * sizeof(int));
    t->merge_result = tlibc_malloc((size_t)lines * sizeof(int));
    if (!t->merge_left || !t->merge_right || !t->merge_result) { __munmap(data, (size_t)size); return -1; }
    const unsigned char *p = data, *end = data + size;
    while (p < end) {
        const unsigned char *line_end = p; while (line_end < end && *line_end != '\n') line_end++;
        if (p < line_end && *p != '#') {
            unsigned char left[256], right[256], joined[512]; unsigned int nl = 0, nr = 0;
            while (p < line_end && *p != ' ') { int cp = decode_utf8_char(&p, line_end); int b = byte_unicode_decode(cp); if (b < 0 || nl == 256) break; left[nl++] = (unsigned char)b; }
            if (p < line_end && *p == ' ') p++;
            while (p < line_end && *p != '\r') { int cp = decode_utf8_char(&p, line_end); int b = byte_unicode_decode(cp); if (b < 0 || nr == 256) break; right[nr++] = (unsigned char)b; }
            int a = piece_id(t, left, nl), b = piece_id(t, right, nr);
            if (a >= 0 && b >= 0) {
                llm_memcpy(joined, left, nl); llm_memcpy(joined + nl, right, nr);
                int result = piece_id(t, joined, nl + nr);
                if (result >= 0) { int n = t->merge_count++; t->merge_left[n] = a; t->merge_right[n] = b; t->merge_result[n] = result; }
            }
        }
        p = line_end + (line_end < end);
    }
    __munmap(data, (size_t)size); return 0;
}

int qwen2_tokenizer_encode(const Qwen2Tokenizer *t, const char *text,
                           int *tokens, int capacity)
{
    int count = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        int id = piece_id(t, p, 1);
        if (id < 0 || count == capacity) return -1;
        tokens[count++] = id;
    }
    for (;;) {
        int best_pos = -1, best_rank = t->merge_count;
        for (int i = 0; i + 1 < count; i++)
            for (int rank = 0; rank < best_rank; rank++)
                if (t->merge_left[rank] == tokens[i] && t->merge_right[rank] == tokens[i + 1]) { best_rank = rank; best_pos = i; break; }
        if (best_pos < 0) break;
        tokens[best_pos] = t->merge_result[best_rank];
        for (int i = best_pos + 1; i + 1 < count; i++) tokens[i] = tokens[i + 1];
        count--;
    }
    return count;
}

int qwen2_tokenizer_write(const Qwen2Tokenizer *tokenizer, int token)
{
    if (!tokenizer || token < 0 || token >= tokenizer->vocab_size) return -1;
    unsigned int length = tokenizer->lengths[token];
    if (length && __write(1, tokenizer->pieces[token], length) < 0) return -1;
    return 0;
}
