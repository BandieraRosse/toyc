/* SPDX-License-Identifier: MIT
 *
 * sync.c — 同步原语 syscall 包装
 *
 * 目前包含 futex，用于 pthread 同步和内存池。
 * 约定：返回 Linux 内核风格负 errno。
 */

#include "core.h"
#include "syscall.h"
#include "syscall_num.h"

void __sync_synchronize(void)
{
    /* x86-64 完整内存屏障：所有先前的 store/load 对后续 store/load 可见。
     * toyc 的 cgen_asm 现在支持 mfence 指令。 */
    __asm__ __volatile__ ("mfence" ::: "memory");
}

long __futex(unsigned int *uaddr, int futex_op, unsigned int val,
             const struct timespec *timeout,
             unsigned int *uaddr2, unsigned int val3)
{
    return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3);
}
