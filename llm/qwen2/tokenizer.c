/* SPDX-License-Identifier: MIT */
#include "tokenizer.h"

/* ByteLevel BPE 的二进制词表加载、编码和 token 输出实现。 */

static unsigned int read_u32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static unsigned int piece_hash_value(const unsigned char *data, unsigned int n)
{
    unsigned int h = 2166136261u;
    for (unsigned int i = 0; i < n; i++) h = (h ^ data[i]) * 16777619u;
    return h;
}

int qwen2_tokenizer_build_hash(Qwen2Tokenizer *t)
{
    /* 低于 25% 的装载率，使简单线性探测也能保持较短探测链。 */
    int size = 1;
    while (size < t->vocab_size * 4) size <<= 1;
    t->piece_hash = tlibc_malloc((size_t)size * sizeof(int));
    if (!t->piece_hash) return -1;
    for (int i = 0; i < size; i++) t->piece_hash[i] = -1;
    for (int id = 0; id < t->vocab_size; id++) {
        if (!t->pieces[id]) continue;
        unsigned int slot = piece_hash_value(t->pieces[id], t->lengths[id]) & (unsigned int)(size - 1);
        while (t->piece_hash[slot] >= 0) slot = (slot + 1) & (unsigned int)(size - 1);
        t->piece_hash[slot] = id;
    }
    t->piece_hash_size = size;
    return 0;
}

static unsigned int pair_hash(int left, int right)
{
    unsigned int x = (unsigned int)left * 0x9e3779b1u ^ (unsigned int)right;
    x ^= x >> 16; x *= 0x85ebca6bu; return x ^ (x >> 13);
}

int qwen2_tokenizer_build_merge_hash(Qwen2Tokenizer *t)
{
    int size = 1; while (size < t->merge_count * 4) size <<= 1;
    t->merge_hash = tlibc_malloc((size_t)size * sizeof(int)); if (!t->merge_hash) return -1;
    for (int i = 0; i < size; i++) t->merge_hash[i] = -1;
    for (int rank = 0; rank < t->merge_count; rank++) {
        unsigned int slot = pair_hash(t->merge_left[rank], t->merge_right[rank]) & (unsigned int)(size - 1);
        while (t->merge_hash[slot] >= 0) slot = (slot + 1) & (unsigned int)(size - 1);
        t->merge_hash[slot] = rank;
    }
    t->merge_hash_size = size; return 0;
}

static int merge_rank(const Qwen2Tokenizer *t, int left, int right)
{
    if (!t->merge_hash) return -1;
    unsigned int slot = pair_hash(left, right) & (unsigned int)(t->merge_hash_size - 1);
    while (t->merge_hash[slot] >= 0) { int rank = t->merge_hash[slot]; if (t->merge_left[rank] == left && t->merge_right[rank] == right) return rank; slot = (slot + 1) & (unsigned int)(t->merge_hash_size - 1); }
    return -1;
}

int qwen2_tokenizer_load(Qwen2Tokenizer *tokenizer, const char *path)
{
    tokenizer->mapping = NULL;
    tokenizer->pieces = NULL;
    tokenizer->lengths = NULL;
    tokenizer->merge_left = tokenizer->merge_right = tokenizer->merge_result = NULL;
    tokenizer->merge_count = 0;
    tokenizer->piece_hash = NULL;
    tokenizer->piece_hash_size = 0;
    tokenizer->owns_pieces = 0;
    tokenizer->merge_hash = NULL;
    tokenizer->merge_hash_size = 0;
    int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) return -1;
    off_t size = __lseek(fd, 0, SEEK_END);
    if (size < 20) { __close(fd); return -1; }
    void *mapping = __mmap(NULL, (size_t)size, PROT_READ, MAP_PRIVATE, fd, 0);
    __close(fd);
    if (mapping == MAP_FAILED) return -1;
    const unsigned char *data = (const unsigned char *)mapping;
    /*
     * TOYTOK1: magic[8], version(u32), vocab_size(u32), eos(u32)，随后是
     * vocab_size 个 length(u32)+原始字节。所有整数均为 little-endian。
     */
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
        /* piece 不复制，直接引用只读 mmap；pieces/lengths 仅保存索引。 */
        tokenizer->pieces[i] = data + offset;
        tokenizer->lengths[i] = length;
        offset += length;
    }
    if (offset != (size_t)size) goto fail;
    tokenizer->mapping = mapping;
    tokenizer->mapping_size = (size_t)size;
    tokenizer->vocab_size = (int)vocab_size;
    tokenizer->eos_token = (int)read_u32(data + 16);
    if (qwen2_tokenizer_build_hash(tokenizer) != 0) goto fail;
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
    if (tokenizer->owns_pieces && tokenizer->pieces)
        for (int i = 0; i < tokenizer->vocab_size; i++)
            if (tokenizer->pieces[i]) tlibc_free((void *)tokenizer->pieces[i]);
    if (tokenizer->pieces) tlibc_free(tokenizer->pieces);
    if (tokenizer->lengths) tlibc_free(tokenizer->lengths);
    if (tokenizer->mapping) __munmap(tokenizer->mapping, tokenizer->mapping_size);
    if (tokenizer->piece_hash) tlibc_free(tokenizer->piece_hash);
    if (tokenizer->merge_hash) tlibc_free(tokenizer->merge_hash);
    if (tokenizer->merge_left) tlibc_free(tokenizer->merge_left);
    if (tokenizer->merge_right) tlibc_free(tokenizer->merge_right);
    if (tokenizer->merge_result) tlibc_free(tokenizer->merge_result);
    tokenizer->pieces = NULL;
    tokenizer->lengths = NULL;
    tokenizer->mapping = NULL;
}

int qwen2_tokenizer_piece_id(const Qwen2Tokenizer *t,
                             const unsigned char *data, unsigned int length)
{
    if (t->piece_hash) {
        unsigned int slot = piece_hash_value(data, length) & (unsigned int)(t->piece_hash_size - 1);
        while (t->piece_hash[slot] >= 0) {
            int id = t->piece_hash[slot];
            if (t->lengths[id] == length) {
                unsigned int j = 0; while (j < length && t->pieces[id][j] == data[j]) j++;
                if (j == length) return id;
            }
            slot = (slot + 1) & (unsigned int)(t->piece_hash_size - 1);
        }
        return -1;
    }
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
    /* 逆转 GPT-2/Qwen ByteLevel alphabet 的 byte -> Unicode 映射。 */
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
    /* merges.txt 的行序就是 BPE rank，越靠前的合并优先级越高。 */
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
            int a = qwen2_tokenizer_piece_id(t, left, nl), b = qwen2_tokenizer_piece_id(t, right, nr);
            if (a >= 0 && b >= 0) {
                llm_memcpy(joined, left, nl); llm_memcpy(joined + nl, right, nr);
                int result = qwen2_tokenizer_piece_id(t, joined, nl + nr);
                if (result >= 0) { int n = t->merge_count++; t->merge_left[n] = a; t->merge_right[n] = b; t->merge_result[n] = result; }
            }
        }
        p = line_end + (line_end < end);
    }
    __munmap(data, (size_t)size);
    return qwen2_tokenizer_build_merge_hash(t);
}

static int encode_bytes(const Qwen2Tokenizer *t, const unsigned char *text,
                        int length, int *tokens, int capacity)
{
    /* ByteLevel BPE 先把每个原始字节变成 token，再反复应用最优 merge。 */
    int count = 0;
    for (int p = 0; p < length; p++) {
        int id = qwen2_tokenizer_piece_id(t, text + p, 1);
        if (id < 0 || count == capacity) return -1;
        tokens[count++] = id;
    }
    for (;;) {
        int best_pos = -1, best_rank = t->merge_count;
        for (int i = 0; i + 1 < count; i++) {
            int rank = merge_rank(t, tokens[i], tokens[i + 1]);
            if (rank >= 0 && rank < best_rank) { best_rank = rank; best_pos = i; }
        }
        if (best_pos < 0) break;
        tokens[best_pos] = t->merge_result[best_rank];
        for (int i = best_pos + 1; i + 1 < count; i++) tokens[i] = tokens[i + 1];
        count--;
    }
    return count;
}

int qwen2_tokenizer_encode(const Qwen2Tokenizer *t, const char *text,
                           int *tokens, int capacity)
{
    /*
     * added token（如 <|im_end|>）必须作为整体保留。普通片段才进入 BPE；
     * 同一位置若多个特殊 token 匹配，选择最长者。
     */
    int total = 0, start = 0, position = 0;
    int length = strlen(text);
    while (position < length) {
        int special = -1, special_length = 0;
        if (t->base_vocab_size > 0) {
            for (int id = t->base_vocab_size; id < t->vocab_size; id++) {
                int n = (int)t->lengths[id];
                if (n <= special_length || position + n > length) continue;
                int j = 0;
                while (j < n && t->pieces[id][j] == (unsigned char)text[position + j]) j++;
                if (j == n) { special = id; special_length = n; }
            }
        }
        if (special < 0) { position++; continue; }
        int n = encode_bytes(t, (const unsigned char *)text + start,
                             position - start, tokens + total, capacity - total);
        if (n < 0 || total + n == capacity) return -1;
        total += n; tokens[total++] = special;
        position += special_length; start = position;
    }
    int n = encode_bytes(t, (const unsigned char *)text + start,
                         length - start, tokens + total, capacity - total);
    return n < 0 ? -1 : total + n;
}

int qwen2_tokenizer_write(const Qwen2Tokenizer *tokenizer, int token)
{
    /* pieces 已在加载时还原为原始字节，写出时无需再次做 UTF-8 转换。 */
    if (!tokenizer || token < 0 || token >= tokenizer->vocab_size) return -1;
    unsigned int length = tokenizer->lengths[token];
    if (length && __write(1, tokenizer->pieces[token], length) < 0) return -1;
    return 0;
}
