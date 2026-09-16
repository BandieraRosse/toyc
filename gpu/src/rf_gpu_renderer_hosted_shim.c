#include "core.h"
#include "pthread.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#endif

void *tlibc_malloc(unsigned long size) { return malloc((size_t)size); }
void tlibc_free(void *ptr) { free(ptr); }
int __clock_gettime(clockid_t id, struct timespec *value)
{
    (void)id;
#if defined(_WIN32)
    LARGE_INTEGER counter, frequency;
    if (!QueryPerformanceCounter(&counter) || !QueryPerformanceFrequency(&frequency)) return -1;
    value->tv_sec = (time_t)(counter.QuadPart / frequency.QuadPart);
    value->tv_nsec = (long)((counter.QuadPart % frequency.QuadPart) * 1000000000LL / frequency.QuadPart);
    return 0;
#else
    return timespec_get(value, TIME_UTC) == TIME_UTC ? 0 : -1;
#endif
}
long __futex(unsigned int *address, int op, unsigned int value,
             const struct timespec *timeout, unsigned int *address2,
             unsigned int value3)
{
    (void)address; (void)op; (void)value; (void)timeout;
    (void)address2; (void)value3;
    return 0;
}
void __fprintf(int fd, const char *format, ...)
{ (void)fd; (void)format; }
int __openat(int dirfd, const char *path, int flags, unsigned short mode)
{
#if defined(_WIN32)
    (void)dirfd; return _open(path, flags, mode);
#else
    return openat(dirfd, path, flags, mode);
#endif
}
long __read(int fd, void *buffer, int size) {
#if defined(_WIN32)
    return _read(fd, buffer, (unsigned int)size);
#else
    return read(fd, buffer, (size_t)size);
#endif
}
int __close(int fd) {
#if defined(_WIN32)
    return _close(fd);
#else
    return close(fd);
#endif
}
#if defined(_WIN32)
int pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
                   void *(*entry)(void *), void *argument)
{
    (void)thread; (void)attributes; (void)entry; (void)argument;
    return -1;
}
int pthread_join(pthread_t thread, void **result)
{ (void)thread; (void)result; return 0; }
#endif
