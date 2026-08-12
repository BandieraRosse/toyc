#ifndef TOYC_WINDOWS_STRING_H
#define TOYC_WINDOWS_STRING_H

#include_next <string.h>

unsigned long tlibc_strtoul(char *str);
void *__memset(void *dst, int value, size_t n);
void *__memmove(void *dst, const void *src, size_t n);

#endif
