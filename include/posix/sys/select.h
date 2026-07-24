#ifndef __SYS_SELECT_H
#define __SYS_SELECT_H

#include "tlibc_types.h"  /* size_t, time_t */

/* suseconds_t — 微秒精度有符号整数 */
typedef long suseconds_t;

/* timeval 由 time.h 提供（POSIX 要求 sys/select.h 使其可见） */
#include "time.h"

/* ── fd_set — 位图实现（FD_SETSIZE=1024） ── */
#define FD_SETSIZE  1024

/* toyc 限制：
 *
 * 1. sizeof(struct_member_array) 返回 8（指针大小）而非数组大小，
 *    因此所有 fd_set 宏用 (unsigned long*) 强制转换绕开。
 *
 * 2. struct 数组成员维度不支持常量表达式（仅接受 TOK_NUMBER），
 *    因此 fd_set 用硬编码值 [16]（1024 / 64）而非 __FD_ELTS。
 *
 * __FD_ELTS 仅用于 FD_ZERO 的循环上界：sizeof 在表达式上下文中正确返回 8。 */
#define __FD_ELTS (FD_SETSIZE / (8 * (int)sizeof(unsigned long)))

typedef struct {
    unsigned long fds_bits[16];    /* 16 × 8 bytes = 1024 bits */
} fd_set;

/* ── 宏 ── */
#define FD_ZERO(set)                                         \
    do {                                                     \
        unsigned long *__bits = (unsigned long *)(set);      \
        int __i;                                             \
        for (__i = 0; __i < __FD_ELTS; __i++)                \
            __bits[__i] = 0UL;                               \
    } while (0)

#define FD_SET(fd, set)                                      \
    (((unsigned long *)(set))[(fd) / (8 * (int)sizeof(unsigned long))] \
        |= (1UL << ((fd) % (8 * (int)sizeof(unsigned long)))))

#define FD_CLR(fd, set)                                      \
    (((unsigned long *)(set))[(fd) / (8 * (int)sizeof(unsigned long))] \
        &= ~(1UL << ((fd) % (8 * (int)sizeof(unsigned long)))))

#define FD_ISSET(fd, set)                                    \
    (((unsigned long *)(set))[(fd) / (8 * (int)sizeof(unsigned long))] \
        & (1UL << ((fd) % (8 * (int)sizeof(unsigned long)))))

/* ── 函数声明 ── */
int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout);

/* 注：pselect6 的声明在 core.h（__pselect6），这里不重复，
 * 因为 POSIX pselect() 由 lib/select.c 提供。 */
int pselect(int nfds, fd_set *readfds, fd_set *writefds,
            fd_set *exceptfds, const struct timespec *timeout,
            const sigset_t *sigmask);

#endif /* __SYS_SELECT_H */
