#include "core.h"

#define CAT_BUFFER_SIZE 16384

static int write_all(int fd, const char *buffer, int length)
{
    int offset = 0;

    while (offset < length) {
        long written = __write(fd, buffer + offset, length - offset);
        if (written <= 0)
            return -1;
        offset += (int)written;
    }
    return 0;
}

static int write_message(const char *message)
{
    int length = 0;

    while (message[length])
        length++;
    return write_all(STDERR, message, length);
}

int main(int argc, char **argv)
{
    char buffer[CAT_BUFFER_SIZE];
    int fd;

    if (argc != 2) {
        write_message("usage: cat <file>\n");
        return 1;
    }

    fd = __openat(AT_FDCWD, argv[1], O_RDONLY, 0);
    if (fd < 0) {
        write_message("cat: cannot open file\n");
        return 1;
    }

    for (;;) {
        long count = __read(fd, buffer, sizeof(buffer));
        if (count < 0) {
            write_message("cat: read failed\n");
            __close(fd);
            return 1;
        }
        if (count == 0)
            break;
        if (write_all(STDOUT, buffer, (int)count) != 0) {
            write_message("cat: write failed\n");
            __close(fd);
            return 1;
        }
    }

    if (__close(fd) != 0) {
        write_message("cat: close failed\n");
        return 1;
    }
    return 0;
}
