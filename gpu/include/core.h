#ifndef RF_GPU_HOSTED_CORE_H
#define RF_GPU_HOSTED_CORE_H

#include <stddef.h>
#include <time.h>
#include <fcntl.h>

#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif

void *tlibc_malloc(unsigned long size);
void tlibc_free(void *ptr);
int __clock_gettime(clockid_t id, struct timespec *value);
long __futex(unsigned int *address, int op, unsigned int value,
             const struct timespec *timeout, unsigned int *address2,
             unsigned int value3);
void __fprintf(int fd, const char *format, ...);
int __openat(int dirfd, const char *path, int flags, unsigned short mode);
long __read(int fd, void *buffer, int size);
int __close(int fd);

#endif
