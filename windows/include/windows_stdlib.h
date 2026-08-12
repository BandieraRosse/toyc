#ifndef TOYC_WINDOWS_STDLIB_H
#define TOYC_WINDOWS_STDLIB_H

/* MinGW headers use the host C library memset while the game later maps the
 * project compatibility macro. Include the native declaration first. */
#include_next <string.h>

#endif
