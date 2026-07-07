// EXPECT: 0
// SELF_CONTAINED
//
// 25_neg_ptr_arith.c — 测试负数指针偏移（指针 + 有符号 32 位负数）
//
// 已知 bug：stage-1 tcc 指针偏移使用 32 位算术（shl eax），
// 负数索引经 32 位运算后高 32 位被清零，导致地址溢出 0xffffffff 前缀。
// 修复后使用 cdqe 符号扩展后执行 64 位移位/imul。

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

#define STR(x) x, sizeof(x) - 1
static void print(const char *s, unsigned long len) {
    sys_write(1, s, len);
}

static long g_longs[4] = {1000, 2000, 3000, 4000};
static int  g_ints[4]   = {10, 20, 30, 40};
static short g_shorts[4] = {1, 2, 3, 4};
static char  g_chars[4]  = {'a', 'b', 'c', 'd'};

void __tlibc_start(void)
{
    /* ═══ 局部变量负数下标 ═══ */

    /* 局部 long 数组 */
    long larr[4] = {111, 222, 333, 444};
    long *lp = &larr[3];

    if (lp[-1] != 333) { print("F1\n", 3); sys_exit(1); }
    if (lp[-2] != 222) { print("F2\n", 3); sys_exit(2); }
    if (lp[-3] != 111) { print("F3\n", 3); sys_exit(3); }

    /* 局部 int 数组 */
    int iarr[5] = {1, 2, 3, 4, 5};
    int *ip = &iarr[4];

    if (ip[-1] != 4) { print("F4\n", 3); sys_exit(4); }
    if (ip[-2] != 3) { print("F5\n", 3); sys_exit(5); }
    if (ip[-4] != 1) { print("F6\n", 3); sys_exit(6); }

    /* 局部 short 数组 */
    short sarr[4] = {10, 20, 30, 40};
    short *sp = &sarr[3];

    if (sp[-1] != 30) { print("F7\n", 3); sys_exit(7); }
    if (sp[-2] != 20) { print("F8\n", 3); sys_exit(8); }

    /* 局部 char 数组 */
    char carr[4] = {'x', 'y', 'z', 'w'};
    char *cp = &carr[3];

    if (cp[-1] != 'z') { print("F9\n", 3); sys_exit(9); }
    if (cp[-2] != 'y') { print("F10\n", 3); sys_exit(10); }

    /* ═══ 全局变量负数下标 ═══ */

    long *glp = &g_longs[3];
    if (glp[-1] != 3000) { print("F11\n", 4); sys_exit(11); }
    if (glp[-2] != 2000) { print("F12\n", 4); sys_exit(12); }
    if (glp[-3] != 1000) { print("F13\n", 4); sys_exit(13); }

    int *gip = &g_ints[3];
    if (gip[-1] != 30) { print("F14\n", 4); sys_exit(14); }
    if (gip[-2] != 20) { print("F15\n", 4); sys_exit(15); }

    short *gsp = &g_shorts[3];
    if (gsp[-1] != 3) { print("F16\n", 4); sys_exit(16); }

    char *gcp = &g_chars[3];
    if (gcp[-1] != 'c') { print("F17\n", 4); sys_exit(17); }

    /* ═══ 指针算术 + 负数 ═══ */

    long *qp = &larr[2];
    if (*(qp + (-1)) != 222) { print("F18\n", 4); sys_exit(18); }
    if (*(qp + (-2)) != 111) { print("F19\n", 4); sys_exit(19); }

    int *rip = &iarr[3];
    if (*(rip + (-1)) != 3) { print("F20\n", 4); sys_exit(20); }
    if (*(rip + (-2)) != 2) { print("F21\n", 4); sys_exit(21); }

    /* ═══ int 变量做负数下标 ═══ */
    {
        int idx = -1;
        if (ip[idx] != 4) { print("F22\n", 4); sys_exit(22); }
    }
    {
        int idx = -3;
        if (ip[idx] != 2) { print("F23\n", 4); sys_exit(23); }
    }
    {
        int idx = -1;
        if (glp[idx] != 3000) { print("F24\n", 4); sys_exit(24); }
    }
    {
        int idx = -2;
        if (glp[idx] != 2000) { print("F25\n", 4); sys_exit(25); }
    }

    /* ═══ long long（8 字节元素）负数下标 ═══ */
    long long llarr[3] = {11111, 22222, 33333};
    long long *llp = &llarr[2];
    if (llp[-1] != 22222) { print("F26\n", 4); sys_exit(26); }

    /* ═══ 结构体指针负数下标 ═══ */
    /* 简单测试：用局部 long 数组验证大元素 */
    /* 已由 long 测试覆盖 */

    print("PASS\n", 5);
    sys_exit(0);
}
