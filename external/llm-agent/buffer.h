#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>

/* ─── 动态缓冲区（字符串构建器） ───────────────────────── */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} Buffer;

/* 初始化空缓冲区（首次写入时 malloc） */
void buf_init(Buffer *b);

/* 释放缓冲区内存 */
void buf_free(Buffer *b);

/* 追加字符串 */
void buf_puts(Buffer *b, const char *s);

/* 追加指定长度的字符串 */
void buf_putn(Buffer *b, const char *s, size_t n);

/* 追加单个字符 */
void buf_putc(Buffer *b, char c);

/* 追加格式化整数 */
void buf_puti(Buffer *b, long n);

/* 返回 C 字符串（有效期到下次修改或 buf_free） */
const char *buf_cstr(Buffer *b);

#endif /* BUFFER_H */
