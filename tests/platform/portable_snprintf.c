#include "tlibc_types.h"

extern int snprintf(char *str, size_t size, const char *format, ...);

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    size_t i;
    for (i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void *__memset(void *dst, int value, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    size_t i;
    for (i = 0; i < n; i++) d[i] = (unsigned char)value;
    return dst;
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

int main(void)
{
    char buf[64];
    char small[4];
    int n;

    n = snprintf(buf, sizeof(buf), "%.18f", 1.25);
    if (n != 20 || !same(buf, "1.250000000000000000")) return 1;

    /* The first value above the V1 bound must be safe and deterministic. */
    n = snprintf(buf, sizeof(buf), "%.19f", 1.25);
    if (n != 20 || !same(buf, "1.250000000000000000")) return 2;

    /* Parsing a precision much larger than int must not overflow or walk off a buffer. */
    n = snprintf(buf, sizeof(buf), "%.999999999999999999999999999999f", 1.25);
    if (n != 20 || !same(buf, "1.250000000000000000")) return 3;

    /* Truncation still reports the full bounded result and writes only the target. */
    n = snprintf(small, sizeof(small), "%.18f", 1.25);
    if (n != 20 || small[0] != '1' || small[1] != '.' ||
        small[2] != '2' || small[3] != '\0') return 4;

    return 0;
}
