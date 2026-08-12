#include "tlibc_types.h"

unsigned long tlibc_strtoul(char *str)
{
    unsigned long value = 0;
    int digits = 0;

    while (*str) {
        if (digits++ > 12 || *str < '0' || *str > '9')
            return (unsigned long)-1;
        value = value * 10 + (unsigned long)(*str - '0');
        str++;
    }
    return value;
}
