#ifndef TOYC_WINDOWS_DIRENT_H
#define TOYC_WINDOWS_DIRENT_H

#include "tlibc_types.h"

struct linux_dirent64 {
    unsigned long long d_ino;
    unsigned long long d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[260];
};

#define DT_REG 8
#define DT_DIR 4

#endif
