#include "buffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define BUF_INIT_CAP 256

void buf_init(Buffer *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void buf_free(Buffer *b)
{
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void buf_grow(Buffer *b, size_t needed)
{
    if (b->cap >= needed)
        return;
    size_t newcap = b->cap ? b->cap : BUF_INIT_CAP;
    while (newcap < needed)
        newcap *= 2;
    char *p = realloc(b->data, newcap);
    if (!p) {
        fprintf(stderr, "buffer: realloc failed\n");
        exit(1);
    }
    b->data = p;
    b->cap = newcap;
}

void buf_puts(Buffer *b, const char *s)
{
    buf_putn(b, s, strlen(s));
}

void buf_putn(Buffer *b, const char *s, size_t n)
{
    if (n == 0) return;
    size_t needed = b->len + n + 1; /* +1 给 '\0' */
    buf_grow(b, needed);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void buf_putc(Buffer *b, char c)
{
    buf_putn(b, &c, 1);
}

void buf_puti(Buffer *b, long n)
{
    char tmp[32];
    int len = snprintf(tmp, sizeof(tmp), "%ld", n);
    buf_putn(b, tmp, (size_t)len);
}

const char *buf_cstr(Buffer *b)
{
    if (!b->data) {
        buf_grow(b, 1);
        b->data[0] = '\0';
    }
    return b->data;
}
