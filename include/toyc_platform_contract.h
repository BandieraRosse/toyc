#ifndef TOYC_PLATFORM_CONTRACT_H
#define TOYC_PLATFORM_CONTRACT_H

/*
 * Common Toyc userland contract.
 *
 * This header deliberately contains only declarations and values that are
 * shared by the Linux syscall implementation and the future Win32
 * implementation.  Linux UAPI structures and Windows HANDLE declarations
 * belong in their respective platform headers instead.
 *
 * Contract rules frozen by Windows platform Checkpoint 1:
 *   - paths at this boundary are UTF-8 and use '/' separators;
 *   - a failure is returned as a negative Toyc errno (never a host errno or
 *     raw Win32 error);
 *   - fd 0/1/2 are stdin/stdout/stderr, and newly allocated fds are non-
 *     negative Toyc ints;
 *   - V1 openat accepts AT_FDCWD. Other dirfds are reserved for a later
 *     directory-handle implementation and must not be silently ignored.
 *   - metadata and directory iteration use the opaque Toyc records below;
 *     neither linux_dirent64 nor Win32 structs cross this boundary.
 */

#include "tlibc_types.h"
#include "errno.h"
#include "fcntl.h"
#include "unistd.h"

#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif
#ifndef STDIN
#define STDIN 0
#endif
#ifndef STDOUT
#define STDOUT 1
#endif
#ifndef STDERR
#define STDERR 2
#endif

#define TOYC_PATH_ENCODING_UTF8 1
#define TOYC_PATH_SEPARATOR '/'
#define TOYC_NEGATIVE_ERRNO 1
#define TOYC_FD_STDIN 0
#define TOYC_FD_STDOUT 1
#define TOYC_FD_STDERR 2
#define TOYC_FD_MIN 0
#define TOYC_OPENAT_V1_DIRFD AT_FDCWD
#define TOYC_OPENAT_V1_OTHER_DIRFD ENOSYS

enum toyc_file_type {
    TOYC_FILE_UNKNOWN = 0,
    TOYC_FILE_REGULAR = 1,
    TOYC_FILE_DIRECTORY = 2,
    TOYC_FILE_OTHER = 3
};

struct toyc_file_info {
    int type;
    int64_t size;
    int64_t mtime_sec;
};

/* A fixed caller-owned record keeps directory iteration ABI-stable.  The
 * byte buffer is only provider storage; its contents are never interpreted
 * by portable applications. */
#define TOYC_DIR_NAME_MAX 1024
#define TOYC_DIR_BUFFER_SIZE 4096
struct toyc_dir_entry {
    int type;
    size_t name_len;
};
struct toyc_dir_iterator {
    int fd;
    uint32_t dir_state;
    uint64_t native;
    size_t pos;
    size_t len;
    unsigned char buffer[TOYC_DIR_BUFFER_SIZE];
    char pending_name[TOYC_DIR_NAME_MAX];
    size_t pending_len;
    int pending_type;
};

/* Convert a common errno to the value returned by a platform provider. */
#define TOYC_NEGERR(error_number) (-(error_number))
#define TOYC_IS_ERROR(value) ((value) < 0)

/* The only common provider declarations in Checkpoint 1. */
ssize_t __read(int fd, void *buf, size_t len);
ssize_t __write(int fd, const void *buf, size_t len);
int __openat(int dirfd, const char *utf8_path, int flags, unsigned int mode);
int __close(int fd);

/* Cross-platform file-system subset used by portable applications. */
/* File offsets are fixed-width at the common boundary; hosted Windows off_t
 * is a platform typedef and must not leak its LLP64 representation here. */
int64_t toyc_lseek(int fd, int64_t offset, int whence);
int toyc_fstat(int fd, struct toyc_file_info *info);
int toyc_stat(const char *utf8_path, struct toyc_file_info *info);
int toyc_getcwd(char *utf8_buf, size_t capacity);
int toyc_chdir(const char *utf8_path);
int toyc_mkdir(const char *utf8_path, unsigned int mode);
int toyc_unlink(const char *utf8_path);
int toyc_rmdir(const char *utf8_path);
int toyc_rename(const char *old_utf8_path, const char *new_utf8_path);
int toyc_dir_open(struct toyc_dir_iterator *iterator, const char *utf8_path);
int toyc_dir_next(struct toyc_dir_iterator *iterator, char *utf8_name,
                  size_t capacity, struct toyc_dir_entry *entry);
int toyc_dir_close(struct toyc_dir_iterator *iterator);

#endif /* TOYC_PLATFORM_CONTRACT_H */
