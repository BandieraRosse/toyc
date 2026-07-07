// EXPECT: 0
// SELF_CONTAINED
// 22_ternary.c — 自举测试：三元运算符 ?: 的正确性
// 零值条件应走 false 分支，非零条件走 true 分支

static long sys_write(int fd, const char *buf, unsigned long len) {
    unsigned long ret;
    __asm__ __volatile__ ("syscall"
        : "=a"(ret) : "a"(1), "D"((long)fd), "S"((long)buf), "d"((long)len)
        : "rcx", "r11", "memory");
    return (long)ret;
}
static void sys_exit(int code) {
    __asm__ __volatile__ ("syscall"
        : : "a"((long)60), "D"((long)code)
        : "rcx", "r11", "memory");
    for (;;) ;
}

void __tlibc_start(void) {
    int a = 0, b = 1, c = 2;

    /* 基础三元：零值条件取 false 分支 */
    if ((a ? 10 : 20) != 20) sys_exit(1);   /* a=0 → 20 */
    if ((b ? 10 : 20) != 10) sys_exit(2);   /* b=1 → 10 */
    if ((c ? 30 : 40) != 30) sys_exit(3);   /* c=2 → 30 */

    /* 嵌套三元 */
    {
        int r = a ? 100 : (b ? 200 : 300);
        if (r != 200) sys_exit(4);           /* a=0, b=1 → 200 */
    }
    {
        int r = c ? (a ? 1 : 2) : 3;
        if (r != 2) sys_exit(5);             /* c=2, a=0 → 2 */
    }

    /* 三元在赋值中 */
    {
        long v = (c > 0) ? (long)c * 10 : 0;
        if (v != 20) sys_exit(6);
    }

    /* 三元结果为指针 */
    {
        int data1 = 100, data2 = 200;
        int *p = (a == 0) ? &data2 : &data1;
        if (*p != 200) sys_exit(7);
    }

    /* 三元结果为 long（64 位） */
    {
        long la = 0x100000000L;
        long lb = 0x200000000L;
        long r = (b) ? la : lb;
        if (r != 0x100000000L) sys_exit(8);
    }

    /* 三元表达式作为子表达式 */
    {
        int r = (a ? 1 : (b ? 2 : 3)) + 10;
        if (r != 12) sys_exit(9);            /* a=0, b=1 → 2+10=12 */
    }

    /* 比较结果在三元中 */
    {
        int r = (a == b) ? 5 : 10;
        if (r != 10) sys_exit(10);           /* 0==1 false → 10 */
    }

    /* 多个三元链 */
    {
        int r = a ? 1 : b ? 2 : c ? 3 : 4;
        if (r != 2) sys_exit(11);            /* a=0, b=1 → 2 */
    }

    /* 指针作条件 */
    {
        int *p = (b) ? &a : 0;
        if (p == 0) sys_exit(12);
        int *q = (a) ? &b : 0;
        if (q != 0) sys_exit(13);
    }

    sys_exit(0);
}
