#ifndef TOYC_WINDOWS_TLIBC_COMPAT_H
#define TOYC_WINDOWS_TLIBC_COMPAT_H

#define write(fd, buf, len) __write(fd, buf, len)
#define read(fd, buf, len) __read(fd, buf, len)
#define openat(fd, path, flags, mode) __openat(fd, path, flags, mode)
#define creat(path, mode) __creat(path, mode)
#define close(fd) __close(fd)
#define fstat(fd, st) __fstat(fd, st)
#define mmap(addr, len, prot, flags, fd, off) __mmap(addr, len, prot, flags, fd, off)
#define munmap(addr, len) __munmap(addr, len)
#define memset(dst, value, n) __memset(dst, value, n)
#define memmove(dst, src, n) __memmove(dst, src, n)
#define clock_gettime(id, ts) __clock_gettime(id, ts)
#define nanosleep(req, rem) __nanosleep(req, rem)

#endif
