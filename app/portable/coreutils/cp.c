#include "core.h"

static size_t text_len(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static void error_text(const char *op, const char *path)
{
    const char *prefix = "cp: ";
    const char *sep = ": ";
    const char *suffix = "\n";
    __write(STDERR, prefix, text_len(prefix));
    __write(STDERR, op, text_len(op));
    __write(STDERR, sep, text_len(sep));
    __write(STDERR, path, text_len(path));
    __write(STDERR, suffix, 1);
}

static int write_all(int fd, const char *buf, size_t len)
{
    size_t done = 0;
    while (done < len) {
        ssize_t n = __write(fd, buf + done, len - done);
        if (n <= 0) return 1;
        done += (size_t)n;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    char buf[4096];
    int src;
    int dst;
    int failed = 0;

    if (argc != 3) {
        const char *usage = "usage: cp SOURCE DEST\n";
        __write(STDERR, usage, text_len(usage));
        return 1;
    }
    src = __openat(AT_FDCWD, argv[1], O_RDONLY, 0);
    if (src < 0) {
        error_text("cannot open", argv[1]);
        return 1;
    }
    dst = __openat(AT_FDCWD, argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst < 0) {
        error_text("cannot create", argv[2]);
        __close(src);
        return 1;
    }

    for (;;) {
        ssize_t n = __read(src, buf, sizeof(buf));
        if (n == 0) break;
        if (n < 0 || write_all(dst, buf, (size_t)n)) {
            error_text("I/O failure", argv[1]);
            failed = 1;
            break;
        }
    }
    if (__close(src) < 0) failed = 1;
    if (__close(dst) < 0) failed = 1;
    return failed;
}
