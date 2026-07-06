// EXPECT: 0
// SELF_CONTAINED
// self_chain.c — 自举测试：链式成员访问（AST 类型传播）
// 测试 a.b.c 和 ptr->member 在无运行时依赖下的正确性

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

typedef struct { int x; int y; } Point;
typedef struct { Point start; Point end; long len; } Line;

void __tlibc_start(void) {
    Line line;
    Line *lp;

    /* 链式 . 访问 */
    line.start.x = 10;
    line.start.y = 20;
    line.end.x = 30;
    line.end.y = 40;
    line.len = 50;

    if (line.start.x != 10) sys_exit(1);
    if (line.end.y != 40) sys_exit(2);
    if (line.len != 50) sys_exit(3);

    /* 指针 -> 链式 */
    lp = &line;
    if (lp->start.x != 10) sys_exit(10);
    if (lp->end.y != 40) sys_exit(11);

    /* 修改成员 */
    lp->start.x = 99;
    if (line.start.x != 99) sys_exit(20);

    /* 指针重新赋值 */
    {
        Point p1, p2;
        p1.x = 1; p1.y = 2;
        Point *pp = &p1;
        if (pp->x != 1) sys_exit(30);
        if (pp->y != 2) sys_exit(31);
    }

    sys_exit(0);
}
