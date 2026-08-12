#ifndef TOYC_WINDOWS_CORE_H
#define TOYC_WINDOWS_CORE_H

#include "tlibc_types.h"
#include "errno.h"
#include "fcntl.h"
#include "mman.h"
#include "time.h"
#include "dirent.h"
#include <string.h>
#include <sys/stat.h>

#define TOYC_WINDOWS 1

long __write(int fd, const void *buf, size_t len);
long __read(int fd, void *buf, size_t len);
int __openat(int fd, const char *path, int flags, int mode);
int __creat(const char *path, int mode);
int __close(int fd);
int __fstat(int fd, struct stat *st);
void *__mmap(void *addr, size_t length, int prot, int flags, int fd, off_t off);
int __munmap(void *addr, size_t length);
void *__memset(void *dst, int value, size_t n);
void *__memmove(void *dst, const void *src, size_t n);
int __clock_gettime(clockid_t id, struct timespec *ts);
int __clock_nanosleep(clockid_t id, int flags, const struct timespec *req,
                      struct timespec *remain);
int __nanosleep(const struct timespec *req, struct timespec *remain);
int __getrandom(void *buf, size_t len, unsigned int flags);
void __exit(int status);
void __exit_group(int status);
void toy_windows_log(const char *message);
int tlibc_sigaction(int signum, void (*handler)(int));
int __ioctl(int fd, unsigned long request, void *argp);
long __getdents64(unsigned int fd, struct linux_dirent64 *dirp, unsigned int count);

void *tlibc_malloc(size_t size);
void tlibc_free(void *ptr);
long __futex(unsigned int *uaddr, int op, unsigned int value,
             const struct timespec *timeout, unsigned int *uaddr2,
             unsigned int value3);

void __printf(const char *fmt, ...);
void __fprintf(int fd, const char *fmt, ...);

#define AT_FDCWD (-100)
#define STDIN 0
#define STDOUT 1
#define STDERR 2

#endif
