#ifndef TOYC_WINDOWS_IO_H
#define TOYC_WINDOWS_IO_H

#include "tlibc_types.h"

/* ReadFile/WriteFile take DWORD byte counts.  Never let a larger Toyc
 * request wrap around to a small Windows request. */
#define TOYC_WIN_MAX_IO_CHUNK 0xffffffffUL

static unsigned long toy_windows_io_chunk(size_t length)
{
    return length > (size_t)TOYC_WIN_MAX_IO_CHUNK ?
           TOYC_WIN_MAX_IO_CHUNK : (unsigned long)length;
}

#endif
