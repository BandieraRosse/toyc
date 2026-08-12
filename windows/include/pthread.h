#ifndef TOYC_WINDOWS_PTHREAD_H
#define TOYC_WINDOWS_PTHREAD_H

#include "tlibc_types.h"

typedef struct pthread *pthread_t;
typedef struct { int unused; } pthread_attr_t;

struct pthread {
    void *handle;
    void *(*entry)(void *);
    void *arg;
    void *result;
};

int pthread_create(pthread_t *out, const pthread_attr_t *attr,
                   void *(*entry)(void *), void *arg);
int pthread_join(pthread_t thread, void **result);
void pthread_exit(void *result);
int pthread_equal(pthread_t a, pthread_t b);

#endif
