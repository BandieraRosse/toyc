/* SPDX-License-Identifier: MIT */
#include "tokenizer.h"

/*
 * Hugging Face tokenizer.json 的专用轻量读取器。
 * 这里只识别 Qwen2 ByteLevel BPE 所需的 vocab、added_tokens 和 merges，
 * 不是通用 JSON/tokenizers 实现，目的是让最终程序完全脱离 Python。
 */

static unsigned char *load_file(const char *path, size_t *size)
{
    int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) return NULL;
    off_t n = __lseek(fd, 0, SEEK_END);
    if (n <= 0 || __lseek(fd, 0, SEEK_SET) < 0) { __close(fd); return NULL; }
    unsigned char *data = tlibc_malloc((size_t)n + 1);
    if (!data) { __close(fd); return NULL; }
    size_t got = 0;
    while (got < (size_t)n) { long r = __read(fd, data + got, (size_t)n - got); if (r <= 0) { tlibc_free(data); __close(fd); return NULL; } got += (size_t)r; }
    __close(fd); data[n] = 0; *size = (size_t)n; return data;
}

static unsigned char *find_text(unsigned char *p, const char *text)
{
    int n = strlen(text);
    for (; *p; p++) { int i = 0; while (i < n && p[i] == (unsigned char)text[i]) i++; if (i == n) return p; }
    return NULL;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int append_utf8(unsigned char *out, int n, unsigned int cp)
{
    if (cp < 0x80) out[n++] = (unsigned char)cp;
    else if (cp < 0x800) { out[n++] = 0xc0 | (cp >> 6); out[n++] = 0x80 | (cp & 63); }
    else { out[n++] = 0xe0 | (cp >> 12); out[n++] = 0x80 | ((cp >> 6) & 63); out[n++] = 0x80 | (cp & 63); }
    return n;
}

static int json_string(unsigned char **cursor, unsigned char *out, int capacity)
{
    /* 解码 JSON 字符串及常见转义；Qwen2 词表只需要 BMP 范围的 \uXXXX。 */
    unsigned char *p = *cursor; if (*p++ != '"') return -1; int n = 0;
    while (*p && *p != '"') {
        unsigned int c = *p++;
        if (c == '\\') {
            c = *p++;
            if (c == 'u') { c = 0; for (int i = 0; i < 4; i++) { int h = hexval(*p++); if (h < 0) return -1; c = c * 16 + (unsigned int)h; } if (n + 3 > capacity) return -1; n = append_utf8(out, n, c); continue; }
            if (c == 'n') c = '\n'; else if (c == 'r') c = '\r'; else if (c == 't') c = '\t'; else if (c == 'b') c = '\b'; else if (c == 'f') c = '\f';
        }
        if (n == capacity) return -1;
        out[n++] = (unsigned char)c;
    }
    if (*p != '"') return -1;
    *cursor = p + 1;
    return n;
}

static int utf8_cp(const unsigned char **cursor, const unsigned char *end)
{
    const unsigned char *p = *cursor; if (p >= end) return -1; int cp, extra;
    if (*p < 128) { cp = *p++; extra = 0; } else if ((*p & 224) == 192) { cp = *p++ & 31; extra = 1; } else if ((*p & 240) == 224) { cp = *p++ & 15; extra = 2; } else return -1;
    while (extra--) { if (p >= end || (*p & 192) != 128) return -1; cp = cp * 64 + (*p++ & 63); }
    *cursor = p; return cp;
}

static int byte_unicode(int cp)
{
    if ((cp >= 33 && cp <= 126) || (cp >= 161 && cp <= 172) || (cp >= 174 && cp <= 255)) return cp;
    int extra = 0; for (int b = 0; b < 256; b++) { if ((b >= 33 && b <= 126) || (b >= 161 && b <= 172) || (b >= 174 && b <= 255)) continue; if (cp == 256 + extra++) return b; }
    return -1;
}

static int token_bytes(const unsigned char *encoded, int length, unsigned char *out)
{
    /* tokenizer.json 中的 ByteLevel Unicode 表示还原成模型实际处理的字节。 */
    const unsigned char *p = encoded, *end = encoded + length; int n = 0;
    while (p < end) { int b = byte_unicode(utf8_cp(&p, end)); if (b < 0) return -1; out[n++] = (unsigned char)b; }
    return n;
}

static int parse_number(unsigned char **p)
{
    while (**p == ' ' || **p == '\n') (*p)++;
    int n = 0;
    if (**p < '0' || **p > '9') return -1;
    while (**p >= '0' && **p <= '9') n = n * 10 + *(*p)++ - '0';
    return n;
}

static int add_piece(Qwen2Tokenizer *t, int id, const unsigned char *piece, int n)
{
    if (id < 0 || id >= t->vocab_size || t->pieces[id]) return -1;
    unsigned char *copy = tlibc_malloc((size_t)n + 1); if (!copy) return -1;
    llm_memcpy(copy, piece, (size_t)n); copy[n] = 0; t->pieces[id] = copy; t->lengths[id] = (unsigned int)n; return 0;
}

int qwen2_tokenizer_load_json(Qwen2Tokenizer *t, const char *path)
{
    size_t size; unsigned char *doc = load_file(path, &size); (void)size; if (!doc) return -1;
    llm_memcpy(t, &(Qwen2Tokenizer){0}, sizeof(*t)); t->eos_token = 151645; t->owns_pieces = 1;
    unsigned char *vocab = find_text(doc, "\"vocab\": {"); unsigned char *merges = find_text(doc, "\"merges\": [");
    if (!vocab || !merges) goto fail;
    /* 第一遍只确定基础词表和含 added token 在内的最大 token id。 */
    int max_id = -1; unsigned char tmp[1024]; unsigned char *p = vocab + 10;
    while (p < merges) { while (p < merges && *p != '"' && *p != '}') p++; if (*p == '}') break; int n = json_string(&p, tmp, sizeof(tmp)); if (n < 0) goto fail; while (*p && *p != ':') p++; if (!*p) goto fail; p++; int id = parse_number(&p); if (id > max_id) max_id = id; }
    t->base_vocab_size = max_id + 1;
    unsigned char *added_end = find_text(doc, "\"normalizer\"");
    for (p = doc; p && p < added_end; p = find_text(p + 1, "\"id\":")) { if (p == doc) p = find_text(p, "\"id\":"); if (!p || p >= added_end) break; unsigned char *q = p + 5; int id = parse_number(&q); if (id > max_id) max_id = id; }
    t->vocab_size = max_id + 1; t->pieces = tlibc_malloc((size_t)t->vocab_size * sizeof(*t->pieces)); t->lengths = tlibc_malloc((size_t)t->vocab_size * sizeof(*t->lengths));
    if (!t->pieces || !t->lengths) goto fail;
    for (int i = 0; i < t->vocab_size; i++) {
        t->pieces[i] = NULL;
        t->lengths[i] = 0;
    }
    /* 第二遍复制基础词表，并把 ByteLevel 字符串转换为原始字节。 */
    p = vocab + 10;
    while (p < merges) { while (p < merges && *p != '"' && *p != '}') p++; if (*p == '}') break; int n = json_string(&p, tmp, sizeof(tmp)); while (*p && *p != ':') p++; p++; int id = parse_number(&p); unsigned char raw[1024]; int rn = token_bytes(tmp, n, raw); if (rn < 0 || add_piece(t, id, raw, rn) != 0) goto fail; }
    /* added token 保持 content 的 UTF-8 原文，编码时会优先整体匹配。 */
    p = find_text(doc, "\"added_tokens\": [");
    while (p && p < added_end) { unsigned char *idp = find_text(p, "\"id\":"); if (!idp || idp >= added_end) break; idp += 5; int id = parse_number(&idp); unsigned char *cp = find_text(idp, "\"content\":"); if (!cp || cp >= added_end) break; cp += 10; while (*cp == ' ') cp++; int n = json_string(&cp, tmp, sizeof(tmp)); if (n < 0 || add_piece(t, id, tmp, n) != 0) goto fail; p = cp; }
    if (qwen2_tokenizer_build_hash(t) != 0) goto fail;
    /* Qwen2 规模已知，固定上限可避免引入 realloc 依赖。数组下标即 rank。 */
    int merge_capacity = 200000; t->merge_left = tlibc_malloc((size_t)merge_capacity * sizeof(int)); t->merge_right = tlibc_malloc((size_t)merge_capacity * sizeof(int)); t->merge_result = tlibc_malloc((size_t)merge_capacity * sizeof(int)); if (!t->merge_left || !t->merge_right || !t->merge_result) goto fail;
    p = merges + 11;
    while (*p && *p != ']') { while (*p && *p != '"' && *p != ']') p++; if (*p == ']') break; int n = json_string(&p, tmp, sizeof(tmp)); if (n < 0) goto fail; int split = 0; while (split < n && tmp[split] != ' ') split++; if (split == n) goto fail; unsigned char a_raw[1024], b_raw[1024], joined[2048]; int an = token_bytes(tmp, split, a_raw), bn = token_bytes(tmp + split + 1, n - split - 1, b_raw); int a = qwen2_tokenizer_piece_id(t, a_raw, an), b = qwen2_tokenizer_piece_id(t, b_raw, bn); llm_memcpy(joined, a_raw, an); llm_memcpy(joined + an, b_raw, bn); int result = qwen2_tokenizer_piece_id(t, joined, an + bn); if (a < 0 || b < 0 || result < 0 || t->merge_count == merge_capacity) goto fail; int m = t->merge_count++; t->merge_left[m] = a; t->merge_right[m] = b; t->merge_result[m] = result; }
    if (qwen2_tokenizer_build_merge_hash(t) != 0) goto fail;
    tlibc_free(doc); return 0;
fail:
    tlibc_free(doc); qwen2_tokenizer_free(t); return -1;
}
