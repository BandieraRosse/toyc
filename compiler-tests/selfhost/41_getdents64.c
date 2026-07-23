/*
 * 41_getdents64.c — getdents64 + struct dirent64 pointer iteration
 *
 * Regression test for toyc bug: -> member access (TOK_ARROW) was missing
 * is_array propagation, causing d_name[] to be loaded as a single char
 * instead of decaying to pointer. This tests the full iteration pattern
 * used by tlibc_get_file_count / tlibc_get_dir_count / tlibc_list_pids.
 *
 * EXPECT: 0
 */

#include "toyc_need.h"

#define SYS_getdents64  217

struct linux_dirent64 {
    unsigned long  d_ino;
    long           d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[256];
};

/* Self-contained wrappers and entry point (no toyc_rt dependency) */
static long sys_exit(int code) {
    return __syscall1(SYS_exit, code);
}

static long my_close(int fd) {
    return __syscall1(SYS_close, fd);
}

static long my_openat(int dirfd, const char *path, int flags, int mode) {
    return __syscall4(SYS_openat, dirfd, path, flags, mode);
}

static void *my_mmap(void *addr, unsigned long len, int prot, int flags, int fd, long off) {
    return (void *)__syscall6(SYS_mmap, addr, len, prot, flags, fd, off);
}

static long my_munmap(void *addr, unsigned long len) {
    return __syscall2(SYS_munmap, addr, len);
}

static long my_getdents64(int fd, void *buf, unsigned long count) {
    return __syscall3(SYS_getdents64, fd, buf, count);
}

/* Entry point: toyld expects __tlibc_start */
void __tlibc_start(void) { sys_exit(main()); }

int main(void) {
    int fd = my_openat(AT_FDCWD, ".", 0x90000, 0);
    if (fd < 0) return 1;

    void *buf = my_mmap(0, 4096, 3, 0x22, -1, 0);
    if ((long)buf < 0) { my_close(fd); return 1; }

    int ret = my_getdents64(fd, buf, 4096);
    if (ret <= 0) { my_munmap(buf, 4096); my_close(fd); return 1; }

    {
        struct linux_dirent64 *data = (struct linux_dirent64 *)buf;
        int count = 0;
        /* Test the getdents64 loop pattern — pointer iteration with
         * d_name[] accessed via ->. Bug: d_name[] was loaded as char
         * (movsbl) instead of decaying to pointer */
        while ((char *)data < buf + ret) {
            if (data->d_name[0] != '.')
                count++;
            data = (struct linux_dirent64 *)((char *)data + data->d_reclen);
        }
        if (count <= 0) { my_munmap(buf, 4096); my_close(fd); return 1; }
    }

    my_munmap(buf, 4096);
    my_close(fd);
    return 0;
}
