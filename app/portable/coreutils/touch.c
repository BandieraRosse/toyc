#include "core.h"

static size_t text_len(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static void error_text(const char *path)
{
    const char *prefix = "touch: cannot create ";
    const char *suffix = "\n";
    __write(STDERR, prefix, text_len(prefix));
    __write(STDERR, path, text_len(path));
    __write(STDERR, suffix, 1);
}

int main(int argc, char *argv[])
{
    int fd;
    if (argc != 2) {
        const char *usage = "usage: touch PATH (create and truncate)\n";
        __write(STDERR, usage, text_len(usage));
        return 1;
    }
    /* CP3 intentionally keeps the existing create+truncate behavior. */
    fd = __openat(AT_FDCWD, argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        error_text(argv[1]);
        return 1;
    }
    return __close(fd) < 0 ? 1 : 0;
}
