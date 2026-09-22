/* SPDX-License-Identifier: MIT
 *
 * io.c — 文件 I/O、文件系统操作、文件描述符管理的 syscall 包装
 *
 * 对应 core.h 中声明的 __ 前缀函数。
 * 约定：返回 Linux 内核风格负 errno（-ENOENT, -EINVAL 等）。
 */

#include "core.h"
#include "syscall.h"
#include "syscall_num.h"

/* ── 基础 I/O ── */

ssize_t __write(int fd, const void *buf, size_t len)
{
    return syscall(SYS_write, fd, buf, len);
}

ssize_t __read(int fd, void *buf, size_t len)
{
    return syscall(SYS_read, fd, buf, len);
}

int __openat(int fd, const char *pathname, int flags, unsigned int mode)
{
    return syscall(SYS_openat, fd, pathname, flags, mode);
}

int __creat(const char *pathname, unsigned short mode)
{
    return syscall(SYS_openat, AT_FDCWD, pathname,
                   O_CREAT | O_WRONLY | O_TRUNC, mode);
}

int __close(int fd)
{
    return syscall(SYS_close, fd);
}

/* ── 文件描述符操作 ── */

off_t __lseek(int fd, off_t offset, int whence)
{
    return syscall(SYS_lseek, fd, offset, whence);
}

int __ftruncate(int fd, off_t length)
{
    return syscall(SYS_ftruncate, fd, length);
}

int __memfd_create(const char *name, unsigned int flags)
{
    return (int)syscall(SYS_memfd_create, name, flags);
}

int __ioctl(int fd, unsigned long request, void *argp)
{
    return syscall(SYS_ioctl, fd, request, argp);
}

int __pipe2(int pipefd[2], int flags)
{
    return syscall(SYS_pipe2, pipefd, flags);
}

int __fcntl(int fd, int cmd, unsigned long arg)
{
    return syscall(SYS_fcntl, fd, cmd, arg);
}

int __dup(int oldfd)
{
    return syscall(SYS_dup, oldfd);
}

int __dup2(int oldfd, int newfd)
{
    return syscall(SYS_dup2, oldfd, newfd);
}

int __dup3(int oldfd, int newfd, int flags)
{
    return syscall(SYS_dup3, oldfd, newfd, flags);
}

ssize_t __readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz)
{
    return syscall(SYS_readlinkat, dirfd, pathname, buf, bufsiz);
}

/* ── 目录 / 文件系统 ── */

long __getdents64(unsigned int fd, struct linux_dirent64 *dirp, unsigned int count)
{
    return syscall(SYS_getdents64, fd, dirp, count);
}

int __fstat(int fd, struct stat *statbuf)
{
    return syscall(SYS_fstat, fd, statbuf);
}

int __unlinkat(int dirfd, const char *pathname, int flags)
{
    return syscall(SYS_unlinkat, dirfd, pathname, flags);
}

char *__getcwd(char *buf, size_t size)
{
    return (char *)syscall(SYS_getcwd, buf, size);
}

int __chdir(const char *path)
{
    return syscall(SYS_chdir, path);
}

int __mkdirat(int dirfd, const char *pathname, mode_t mode)
{
    return syscall(SYS_mkdirat, dirfd, pathname, mode);
}

int __rmdir(const char *pathname)
{
    return syscall(SYS_unlinkat, AT_FDCWD, pathname, AT_REMOVEDIR);
}

int __renameat(int olddirfd, const char *oldpath,
               int newdirfd, const char *newpath)
{
    return syscall(SYS_renameat2, olddirfd, oldpath, newdirfd, newpath, 0);
}

int __rename(const char *oldpath, const char *newpath)
{
    return syscall(SYS_renameat2, AT_FDCWD, oldpath, AT_FDCWD, newpath, 0);
}

/* ── Portable metadata/path/目录 contract ── */

static int toyc_linux_file_type(mode_t mode)
{
    if (S_ISREG(mode)) return TOYC_FILE_REGULAR;
    if (S_ISDIR(mode)) return TOYC_FILE_DIRECTORY;
    return TOYC_FILE_OTHER;
}

static void toyc_linux_info_from_stat(const struct stat *st,
                                      struct toyc_file_info *info)
{
    info->type = toyc_linux_file_type(st->st_mode);
    info->size = (int64_t)st->st_size;
    info->mtime_sec = (int64_t)st->st_mtim.tv_sec;
}

int64_t toyc_lseek(int fd, int64_t offset, int whence)
{
    return (int64_t)__lseek(fd, (off_t)offset, whence);
}

int toyc_fstat(int fd, struct toyc_file_info *info)
{
    struct stat st;
    int result;
    if (!info) return -EINVAL;
    result = __fstat(fd, &st);
    if (result < 0) return result;
    toyc_linux_info_from_stat(&st, info);
    return 0;
}

int toyc_stat(const char *utf8_path, struct toyc_file_info *info)
{
    struct stat st;
    int result;
    if (!info) return -EINVAL;
    result = tlibc_stat(utf8_path, &st);
    if (result < 0) return result;
    toyc_linux_info_from_stat(&st, info);
    return 0;
}

int toyc_getcwd(char *utf8_buf, size_t capacity)
{
    char *result = __getcwd(utf8_buf, capacity);
    if ((long)result < 0) return (int)(long)result;
    return result ? 0 : -EIO;
}

int toyc_chdir(const char *utf8_path)
{
    return __chdir(utf8_path);
}

int toyc_mkdir(const char *utf8_path, unsigned int mode)
{
    return __mkdirat(AT_FDCWD, utf8_path, (mode_t)mode);
}

int toyc_unlink(const char *utf8_path)
{
    return __unlinkat(AT_FDCWD, utf8_path, 0);
}

int toyc_rmdir(const char *utf8_path)
{
    return __unlinkat(AT_FDCWD, utf8_path, AT_REMOVEDIR);
}

int toyc_rename(const char *old_utf8_path, const char *new_utf8_path)
{
    return __rename(old_utf8_path, new_utf8_path);
}

static int toyc_linux_dir_type(unsigned char type)
{
    if (type == DT_REG) return TOYC_FILE_REGULAR;
    if (type == DT_DIR) return TOYC_FILE_DIRECTORY;
    if (type == DT_UNKNOWN) return TOYC_FILE_UNKNOWN;
    return TOYC_FILE_OTHER;
}

int toyc_dir_open(struct toyc_dir_iterator *iterator, const char *utf8_path)
{
    int fd;
    size_t i;
    if (!iterator || !utf8_path) return -EINVAL;
    fd = __openat(AT_FDCWD, utf8_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0);
    if (fd < 0) return fd;
    for (i = 0; i < sizeof(*iterator); i++)
        ((unsigned char *)iterator)[i] = 0;
    iterator->fd = fd;
    return 0;
}

int toyc_dir_next(struct toyc_dir_iterator *iterator, char *utf8_name,
                  size_t capacity, struct toyc_dir_entry *entry)
{
    struct linux_dirent64 *dirent;
    size_t name_len;
    long got;
    if (!iterator || !utf8_name || !entry || capacity == 0) return -EINVAL;
    for (;;) {
        if (iterator->pos >= iterator->len) {
            got = __getdents64((unsigned int)iterator->fd,
                               (struct linux_dirent64 *)iterator->buffer,
                               TOYC_DIR_BUFFER_SIZE);
            if (got < 0) return (int)got;
            if (got == 0) return 0;
            iterator->pos = 0;
            iterator->len = (size_t)got;
        }
        if (iterator->len - iterator->pos < sizeof(*dirent)) return -EIO;
        dirent = (struct linux_dirent64 *)(iterator->buffer + iterator->pos);
        if (dirent->d_reclen < sizeof(*dirent) ||
            dirent->d_reclen > iterator->len - iterator->pos)
            return -EIO;
        iterator->pos += dirent->d_reclen;
        name_len = 0;
        while (name_len + 1 < dirent->d_reclen -
               (size_t)((char *)dirent->d_name - (char *)dirent) &&
               dirent->d_name[name_len]) name_len++;
        if (name_len + 1 > capacity) return -ENAMETOOLONG;
        for (size_t i = 0; i < name_len; i++) utf8_name[i] = dirent->d_name[i];
        utf8_name[name_len] = '\0';
        entry->type = toyc_linux_dir_type(dirent->d_type);
        entry->name_len = name_len;
        return 1;
    }
}

int toyc_dir_close(struct toyc_dir_iterator *iterator)
{
    int result;
    if (!iterator || iterator->fd < 0) return -EBADF;
    result = __close(iterator->fd);
    iterator->fd = -1;
    return result;
}

/* ── 文件元数据 ── */

int tlibc_chmod(const char *pathname, mode_t mode)
{
    return syscall(SYS_chmod, pathname, mode);
}

int tlibc_stat(const char *pathname, struct stat *statbuf)
{
    return syscall(SYS_stat, pathname, statbuf);
}

/* ── 随机数 ── */

ssize_t __getrandom(void *buf, size_t buflen, unsigned int flags)
{
    return syscall(SYS_getrandom, buf, buflen, flags);
}

/* ── statfs（文件系统信息） ── */

int __statfs(const char *pathname, struct statfs *buf)
{
    return (int)syscall(SYS_statfs, pathname, buf);
}

int __fstatfs(int fd, struct statfs *buf)
{
    return (int)syscall(SYS_fstatfs, fd, buf);
}

/* ── I/O 多路复用 ── */

int __poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    return (int)syscall(SYS_poll, fds, nfds, timeout);
}

int __ppoll(struct pollfd *fds, nfds_t nfds,
            const struct timespec *timeout_ts, const sigset_t *sigmask)
{
    return (int)syscall(SYS_ppoll, fds, nfds, timeout_ts, sigmask, 8);
}

int __pselect6(int nfds, fd_set *readfds, fd_set *writefds,
               fd_set *exceptfds, const struct timespec *timeout,
               const sigset_t *sigmask)
{
    struct {
        const sigset_t *ss;
        size_t ss_len;
    } sigdata = { sigmask, 8 };
    return (int)syscall(SYS_pselect6, nfds, readfds, writefds,
                        exceptfds, timeout, &sigdata);
}

int __epoll_create1(int flags)
{
    return (int)syscall(SYS_epoll_create1, flags);
}

int __epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    return (int)syscall(SYS_epoll_ctl, epfd, op, fd, event);
}

int __epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    return (int)syscall(SYS_epoll_wait, epfd, events, maxevents, timeout);
}

int __epoll_pwait(int epfd, struct epoll_event *events, int maxevents,
                  int timeout, const sigset_t *sigmask)
{
    return (int)syscall(SYS_epoll_pwait, epfd, events, maxevents, timeout, sigmask, 8);
}
