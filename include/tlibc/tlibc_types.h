#ifndef __TLIBC_TYPES_H_
#define __TLIBC_TYPES_H_

typedef unsigned long dev_t;
typedef unsigned long ino_t;
typedef unsigned int mode_t;
typedef unsigned long nlink_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
/* Windows is LLP64: long remains 32-bit while pointers and size_t are 64-bit.
 * Keep the freestanding Linux ABI unchanged, but make the hosted MinGW
 * contract agree with the target compiler's native size width. */
#ifdef TOYC_WINDOWS
typedef long long ssize_t;
typedef unsigned long long size_t;
typedef long long off_t;
typedef long long blksize_t;
typedef long long blkcnt_t;
#else
typedef long ssize_t;
typedef unsigned long size_t;
typedef long off_t;
typedef long blksize_t;
typedef long blkcnt_t;
#endif
typedef int pid_t;

typedef long time_t;
typedef int clockid_t;

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef signed short int int16_t;
typedef unsigned short int uint16_t;
typedef signed int int32_t;
typedef unsigned int uint32_t;
/* LP64 (Linux) and LLP64 (64-bit Windows) disagree on the width of long.
 * Fixed-width data must therefore use long long, which is 64 bits on both
 * targets supported by Rasterfall. */
typedef signed long long int int64_t;
typedef unsigned long long int uint64_t;

/* Toyc does not yet parse sizeof expressions in array bounds.  Hosted GCC
 * builds on both platforms still enforce the fixed-width contract. */
#ifdef __GNUC__
typedef char tlibc_int64_must_be_8_bytes[(sizeof(int64_t) == 8) ? 1 : -1];
typedef char tlibc_uint64_must_be_8_bytes[(sizeof(uint64_t) == 8) ? 1 : -1];
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif
