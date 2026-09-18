#include <stddef.h>

typedef long (*write_fn)(void *ctx, const void *buf, size_t len);

struct fake_writer {
    unsigned char *dst;
    size_t used;
    size_t limit;
};

static long fake_partial_write(void *ctx, const void *buf, size_t len)
{
    struct fake_writer *writer = (struct fake_writer *)ctx;
    size_t n = len > writer->limit ? writer->limit : len;
    const unsigned char *src = (const unsigned char *)buf;
    size_t i;
    for (i = 0; i < n; i++) writer->dst[writer->used + i] = src[i];
    writer->used += n;
    return (long)n;
}

static long fake_zero_write(void *ctx, const void *buf, size_t len)
{
    (void)ctx;
    (void)buf;
    (void)len;
    return 0;
}

static int write_all(write_fn fn, void *ctx, const unsigned char *buf, size_t len)
{
    size_t done = 0;
    while (done < len) {
        long n = fn(ctx, buf + done, len - done);
        if (n <= 0) return 1;
        done += (size_t)n;
    }
    return 0;
}

int main(void)
{
    unsigned char input[8193];
    unsigned char output[8193];
    struct fake_writer writer;
    size_t i;
    for (i = 0; i < sizeof(input); i++) input[i] = (unsigned char)(i * 37U);
    writer.dst = output;
    writer.used = 0;
    writer.limit = 7;
    if (write_all(fake_partial_write, &writer, input, sizeof(input))) return 1;
    if (writer.used != sizeof(input)) return 2;
    for (i = 0; i < sizeof(input); i++) if (output[i] != input[i]) return 3;
    if (!write_all(fake_zero_write, &writer, input, 1)) return 4;
    if (write_all(fake_partial_write, &writer, input, 0)) return 5;
    return 0;
}
