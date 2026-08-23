#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <io.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "core.h"

int toy_socket_close(int fd);

static int win_flags(int flags)
{
    int out = 0;
    if ((flags & 3) == O_WRONLY) out |= _O_WRONLY;
    else if ((flags & 3) == O_RDWR) out |= _O_RDWR;
    else out |= _O_RDONLY;
    if (flags & O_CREAT) out |= _O_CREAT;
    if (flags & O_TRUNC) out |= _O_TRUNC;
    if (flags & O_APPEND) out |= _O_APPEND;
    return out | _O_BINARY;
}

void toy_windows_log(const char *message)
{
    char path[MAX_PATH];
    DWORD length;
    FILE *file;
    if (!message) return;
    length = GetModuleFileNameA(NULL, path, sizeof(path));
    if (!length || length >= sizeof(path)) return;
    while (length > 0 && path[length - 1] != '\\' && path[length - 1] != '/')
        path[--length] = 0;
    if (length + 16 >= sizeof(path)) return;
    strcpy(path + length, "rasterfall.log");
    file = fopen(path, "ab");
    if (!file) return;
    fprintf(file, "%s\r\n", message);
    fclose(file);
}

long __write(int fd, const void *buf, size_t len)
{
    return _write(fd, buf, (unsigned int)len);
}

long __read(int fd, void *buf, size_t len)
{
    return _read(fd, buf, (unsigned int)len);
}

int __openat(int dirfd, const char *path, int flags, int mode)
{
    (void)dirfd;
    return _open(path, win_flags(flags), mode);
}

int __creat(const char *path, int mode)
{
    return _open(path, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, mode);
}

int __close(int fd)
{
    if (toy_socket_close(fd) == 0) return 0;
    return _close(fd);
}

int __fstat(int fd, struct stat *st)
{
    long long saved, end;
    if (!st) return -1;
    saved = _lseeki64(fd, 0, SEEK_CUR);
    end = _lseeki64(fd, 0, SEEK_END);
    if (saved < 0 || end < 0 || _lseeki64(fd, saved, SEEK_SET) < 0) return -1;
    st->st_size = end;
    return 0;
}

void *__mmap(void *addr, size_t length, int prot, int flags, int fd, off_t off)
{
    unsigned char *data;
    long long end;
    size_t got = 0;
    (void)addr; (void)prot; (void)flags;
    if (fd < 0) return calloc(1, (size_t)length);
    end = _lseeki64(fd, 0, SEEK_END);
    if (end < 0 || off < 0 || off > end || length > (size_t)(end - off))
        return MAP_FAILED;
    data = (unsigned char *)malloc((size_t)length);
    if (!data || _lseeki64(fd, off, SEEK_SET) < 0) {
        free(data); return MAP_FAILED;
    }
    while (got < length) {
        int n = _read(fd, data + got, (unsigned int)(length - got));
        if (n <= 0) { free(data); return MAP_FAILED; }
        got += (size_t)n;
    }
    return data;
}

int __munmap(void *addr, size_t length)
{
    (void)length;
    free(addr);
    return 0;
}

void *__memset(void *dst, int value, size_t n) { return memset(dst, value, (size_t)n); }
void *__memmove(void *dst, const void *src, size_t n) { return memmove(dst, src, (size_t)n); }

void *tlibc_malloc(size_t size) { return calloc(1, (size_t)(size ? size : 1)); }
void tlibc_free(void *ptr) { free(ptr); }

int tlibc_recursive_mkdir(const char *path)
{
    char buffer[MAX_PATH];
    char *p;
    DWORD attributes;
    if (!path || !*path || strlen(path) >= sizeof(buffer)) return -1;
    strcpy(buffer, path);
    for (p = buffer; *p; p++) {
        if (*p != '\\' && *p != '/') continue;
        if (p == buffer || (p == buffer + 2 && buffer[1] == ':')) continue;
        *p = 0;
        CreateDirectoryA(buffer, NULL);
        *p = '\\';
    }
    CreateDirectoryA(buffer, NULL);
    attributes = GetFileAttributesA(buffer);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) ? 0 : -1;
}

static LARGE_INTEGER qpc_frequency;

static void init_clock(void)
{
    if (!qpc_frequency.QuadPart) QueryPerformanceFrequency(&qpc_frequency);
}

int __clock_gettime(clockid_t id, struct timespec *ts)
{
    LARGE_INTEGER now;
    (void)id;
    if (!ts) return -1;
    init_clock();
    QueryPerformanceCounter(&now);
    ts->tv_sec = (time_t)(now.QuadPart / qpc_frequency.QuadPart);
    ts->tv_nsec = (long)(((now.QuadPart % qpc_frequency.QuadPart) * 1000000000LL) /
                         qpc_frequency.QuadPart);
    return 0;
}

int __nanosleep(const struct timespec *req, struct timespec *remain)
{
    (void)remain;
    if (!req) return -1;
    Sleep((DWORD)(req->tv_sec * 1000 + req->tv_nsec / 1000000));
    return 0;
}

int __clock_nanosleep(clockid_t id, int flags, const struct timespec *req,
                      struct timespec *remain)
{
    (void)id; (void)flags;
    return __nanosleep(req, remain);
}

int __getrandom(void *buf, size_t len, unsigned int flags)
{
    unsigned char *p = (unsigned char *)buf;
    (void)flags;
    for (size_t i = 0; i < len; i++) p[i] = (unsigned char)(rand() & 255);
    return 0;
}

void __printf(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vfprintf(stdout, fmt, ap); va_end(ap);
}

void __fprintf(int fd, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    vfprintf(fd == STDERR ? stderr : stdout, fmt, ap);
    va_end(ap);
}

void __exit(int status) { ExitProcess((UINT)status); }
void __exit_group(int status) { ExitProcess((UINT)status); }
int tlibc_sigaction(int signum, void (*handler)(int))
{ (void)signum; (void)handler; return 0; }
int __ioctl(int fd, unsigned long request, void *argp)
{ (void)fd; (void)request; (void)argp; return -1; }
long __getdents64(unsigned int fd, struct linux_dirent64 *dirp, unsigned int count)
{ (void)fd; (void)dirp; (void)count; return -1; }
static INIT_ONCE futex_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION futex_lock;
static CONDITION_VARIABLE futex_condition;

static BOOL WINAPI init_futex(PINIT_ONCE once, PVOID parameter, LPVOID *context)
{
    (void)once; (void)parameter; (void)context;
    InitializeCriticalSection(&futex_lock);
    InitializeConditionVariable(&futex_condition);
    return TRUE;
}

long __futex(unsigned int *uaddr, int op, unsigned int value,
             const struct timespec *timeout, unsigned int *uaddr2,
             unsigned int value3)
{
    DWORD milliseconds = INFINITE;
    (void)uaddr2; (void)value3;
    if (!uaddr) return -1;
    if (InitOnceExecuteOnce(&futex_once, init_futex, NULL, NULL) == FALSE)
        return -1;
    if (timeout)
        milliseconds = (DWORD)(timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000);
    if (op == 0) {
        EnterCriticalSection(&futex_lock);
        while (*uaddr == value &&
               SleepConditionVariableCS(&futex_condition, &futex_lock, milliseconds))
            milliseconds = INFINITE;
        LeaveCriticalSection(&futex_lock);
        return 0;
    }
    EnterCriticalSection(&futex_lock);
    WakeAllConditionVariable(&futex_condition);
    LeaveCriticalSection(&futex_lock);
    return 0;
}
