#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "core.h"

static int native_open_flags(int flags)
{
    int native_flags;

    if ((flags & 3) == O_WRONLY)
        native_flags = _O_WRONLY;
    else if ((flags & 3) == O_RDWR)
        native_flags = _O_RDWR;
    else
        native_flags = _O_RDONLY;
    if (flags & O_CREAT) native_flags |= _O_CREAT;
    if (flags & O_TRUNC) native_flags |= _O_TRUNC;
    if (flags & O_APPEND) native_flags |= _O_APPEND;
    return native_flags | _O_BINARY;
}

long __write(int fd, const void *buffer, size_t length)
{
    return _write(fd, buffer, (unsigned int)length);
}

long __read(int fd, void *buffer, size_t length)
{
    return _read(fd, buffer, (unsigned int)length);
}

int __openat(int dirfd, const char *path, int flags, int mode)
{
    (void)dirfd;
    return _open(path, native_open_flags(flags), mode);
}

int __close(int fd)
{
    return _close(fd);
}
