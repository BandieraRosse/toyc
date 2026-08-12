#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#include <stdlib.h>

#include "pthread.h"

static unsigned __stdcall thread_main(void *arg)
{
    struct pthread *thread = (struct pthread *)arg;
    thread->result = thread->entry(thread->arg);
    return 0;
}

int pthread_create(pthread_t *out, const pthread_attr_t *attr,
                   void *(*entry)(void *), void *arg)
{
    uintptr_t handle;
    struct pthread *thread;
    (void)attr;
    if (!out || !entry) return 11;
    thread = (struct pthread *)calloc(1, sizeof(*thread));
    if (!thread) return 12;
    thread->entry = entry;
    thread->arg = arg;
    handle = _beginthreadex(NULL, 0, thread_main, thread, 0, NULL);
    if (!handle) { free(thread); return 11; }
    thread->handle = (void *)handle;
    *out = thread;
    return 0;
}

int pthread_join(pthread_t thread, void **result)
{
    if (!thread) return 22;
    WaitForSingleObject((HANDLE)thread->handle, INFINITE);
    if (result) *result = thread->result;
    CloseHandle((HANDLE)thread->handle);
    free(thread);
    return 0;
}

void pthread_exit(void *result)
{
    _endthreadex((unsigned)(uintptr_t)result);
}

int pthread_equal(pthread_t a, pthread_t b) { return a == b; }
