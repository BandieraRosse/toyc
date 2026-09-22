#include "core.h"

static size_t text_len(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static int write_all(const void *data, size_t len)
{
    const char *p = (const char *)data;
    size_t done = 0;
    while (done < len) {
        ssize_t n = __write(STDOUT, p + done, len - done);
        if (n <= 0) return 1;
        done += (size_t)n;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    int i;
    for (i = 1; i < argc; i++) {
        if (i > 1 && write_all(" ", 1)) return 1;
        if (write_all(argv[i], text_len(argv[i]))) return 1;
    }
    return write_all("\n", 1);
}
