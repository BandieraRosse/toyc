#include "core.h"

#define HEXDUMP_COLS 16
#define HEXDUMP_BUF_SIZE 4096

static ssize_t write_all(int fd, const void *data, size_t len)
{
    const char *p = (const char *)data;
    size_t done = 0;
    while (done < len) {
        ssize_t n = __write(fd, p + done, len - done);
        if (n <= 0) return n < 0 ? n : -EIO;
        done += (size_t)n;
    }
    return (ssize_t)done;
}

static size_t text_len(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static int write_text(int fd, const char *s)
{
    return write_all(fd, s, text_len(s)) < 0;
}

static void print_hex8(unsigned long val)
{
    const char *hex = "0123456789abcdef";
    char buf[8];
    int i;
    for (i = 7; i >= 0; i--) {
        buf[i] = hex[val & 0xf];
        val >>= 4;
    }
    write_all(STDOUT, buf, sizeof(buf));
}

static void print_hex_byte(unsigned char val)
{
    const char *hex = "0123456789abcdef";
    char buf[3];
    buf[0] = hex[val >> 4];
    buf[1] = hex[val & 0xf];
    buf[2] = ' ';
    write_all(STDOUT, buf, sizeof(buf));
}

static int hexdump_fd(int fd, const char *label)
{
    unsigned char buf[HEXDUMP_BUF_SIZE];
    unsigned long offset = 0;
    ssize_t n;

    if (label) {
        if (write_text(STDOUT, "==> ") || write_text(STDOUT, label) ||
            write_text(STDOUT, " <==\n")) return 1;
    }
    while ((n = __read(fd, buf, sizeof(buf))) > 0) {
        ssize_t i;
        for (i = 0; i < n; i += HEXDUMP_COLS) {
            int j;
            print_hex8(offset + (unsigned long)i);
            write_text(STDOUT, "  ");
            for (j = 0; j < HEXDUMP_COLS; j++) {
                if (i + j < n) print_hex_byte(buf[i + j]);
                else write_text(STDOUT, "   ");
                if (j == 7) write_text(STDOUT, " ");
            }
            write_text(STDOUT, " |\033[36m");
            for (j = 0; j < HEXDUMP_COLS; j++) {
                if (i + j < n) {
                    unsigned char c = buf[i + j];
                    if (c >= 32 && c <= 126) write_all(STDOUT, &c, 1);
                    else write_text(STDOUT, ".");
                } else write_text(STDOUT, " ");
            }
            write_text(STDOUT, "|\033[0m\n");
        }
        offset += (unsigned long)n;
    }
    if (n < 0) return 1;
    print_hex8(offset);
    write_text(STDOUT, "\n");
    return 0;
}

int main(int argc, char *argv[])
{
    int ret = 0;
    int i;
    if (argc < 2) return hexdump_fd(STDIN, NULL);
    for (i = 1; i < argc; i++) {
        int fd = __openat(AT_FDCWD, argv[i], O_RDONLY, 0);
        if (fd < 0) {
            write_text(STDERR, "hexdump: ");
            write_text(STDERR, argv[i]);
            write_text(STDERR, ": open failed\n");
            ret = 1;
            continue;
        }
        if (hexdump_fd(fd, argc > 2 ? argv[i] : NULL)) ret = 1;
        if (__close(fd) < 0) ret = 1;
    }
    return ret;
}
