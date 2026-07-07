// EXPECT: 0
// SELF_CONTAINED
// 21_shortcircuit.c — 自举测试：短路求值 && 和 || 的正确性
// 零值参数（左侧为 false）的短路路径应正确跳过右侧求值

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

    /* === && 短路：左为 0 时不求值右 === */
    {
        int x = 0;
        if (a && (x = 100)) ;       /* a=0，短路，x 保持 0 */
        if (x != 0) sys_exit(1);
    }

    /* === || 短路：左为非零时不求值右 === */
    {
        int x = 0;
        if (b || (x = 200)) ;       /* b=1，短路，x 保持 0 */
        if (x != 0) sys_exit(2);
    }

    /* === 两边都求值的 && === */
    if (!(b && c)) sys_exit(3);     /* 1 && 2 → true */

    /* === 两边都求值的 ||（左侧 0 右侧 1） === */
    if (a || b) ; else sys_exit(4); /* 0 || 1 → true */

    /* === 嵌套：|| 在外 && 在内 === */
    if ((a || b) && c) ; else sys_exit(5);

    /* === 链式 && === */
    if (a && b && c) sys_exit(6);   /* a=0 → false */

    /* === 链式 || === */
    if (a || b || c) ; else sys_exit(7);  /* a=0, b=1 → true(短路) */

    /* === && 和 || 混合在 if 条件中 === */
    if (a == 0 && b == 1) ; else sys_exit(8);

    /* === 短路求值确保副作用不执行 === */
    {
        int side = 0;
        int x = a && (side = 1);    /* a=0，短路，side 不被赋值 */
        if (side != 0) sys_exit(9);
        x = b || (side = 2);        /* b=1，短路，side 不被赋值 */
        if (side != 0) sys_exit(10);
        x = a || (side = 3);        /* a=0，不短路，side=3 */
        if (side != 3) sys_exit(11);
    }

    /* === 返回值作为条件 === */
    {
        int x = 1;
        if (x && (x = 0)) sys_exit(12);  /* x=1, then x=0 → false */
        if (x != 0) sys_exit(13);
    }

    /* === 负数作为条件（也是非零） === */
    if (-1) ; else sys_exit(14);
    if (!(-1)) sys_exit(15);

    sys_exit(0);
}
