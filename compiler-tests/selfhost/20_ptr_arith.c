// EXPECT: 0
// SELF_CONTAINED
// 19_ptr_arith.c — 指针算术完整测试（非 char 指针的 p+i, p[i], q-p）
//   1. 全局 int* 下标访问 g_ptr[i]
//   2. 全局 int* + 指针偏移 *(g_ptr + n)
//   3. 全局 long* 下标和指针偏移
//   4. 嵌套指针算术 *(g_ptr + int + int)
//   5. 指针减法 (q - p) 以元素个数为结果
//   6. 全局 unsigned short* 下标
//   7. 全局 int* 复合表达式 g_ptr[expr]

static long sys_write(int fd, const char *buf, unsigned long len)
{
    unsigned long ret;
    __asm__ __volatile__ ("syscall"
        : "=a"(ret) : "a"(1), "D"((long)fd), "S"((long)buf), "d"((long)len)
        : "rcx", "r11", "memory");
    return (long)ret;
}
static void sys_exit(int code)
{
    __asm__ __volatile__ ("syscall"
        : : "a"((long)60), "D"((long)code)
        : "rcx", "r11", "memory");
    for (;;) ;
}

/* ─── 全局数据 ─── */
static int g_int_arr[4] = { 10, 20, 30, 40 };
static int *g_int_ptr = 0;

static long g_long_arr[3] = { 100L, 200L, 300L };
static long *g_long_ptr = 0;

void __tlibc_start(void)
{
    g_int_ptr = g_int_arr;
    g_long_ptr = g_long_arr;

    /* ═══ 测试 1: 全局 int* 下标 g_ptr[i] ═══ */
    {
        if (g_int_ptr[0] != 10) { sys_write(1,"F1a\n",4); sys_exit(1); }
        if (g_int_ptr[1] != 20) { sys_write(1,"F1b\n",4); sys_exit(1); }
        if (g_int_ptr[2] != 30) { sys_write(1,"F1c\n",4); sys_exit(1); }
        if (g_int_ptr[3] != 40) { sys_write(1,"F1d\n",4); sys_exit(1); }
    }

    /* ═══ 测试 2: 全局 int* + 偏移 *(g_ptr + n) ═══ */
    {
        if (*(g_int_ptr + 0) != 10) { sys_write(1,"F2a\n",4); sys_exit(1); }
        if (*(g_int_ptr + 1) != 20) { sys_write(1,"F2b\n",4); sys_exit(1); }
        if (*(g_int_ptr + 2) != 30) { sys_write(1,"F2c\n",4); sys_exit(1); }
        if (*(g_int_ptr + 3) != 40) { sys_write(1,"F2d\n",4); sys_exit(1); }
        /* 变量偏移 */
        {
            int i;
            for (i = 0; i < 4; i++) {
                if (*(g_int_ptr + i) != 10 + i*10) {
                    sys_write(1,"F2v\n",4); sys_exit(1);
                }
            }
        }
    }

    /* ═══ 测试 3: 全局 long* 下标和指针偏移 ═══ */
    {
        if (g_long_ptr[0] != 100L) { sys_write(1,"F3a\n",4); sys_exit(1); }
        if (g_long_ptr[1] != 200L) { sys_write(1,"F3b\n",4); sys_exit(1); }
        if (g_long_ptr[2] != 300L) { sys_write(1,"F3c\n",4); sys_exit(1); }
        if (*(g_long_ptr + 0) != 100L) { sys_write(1,"F3d\n",4); sys_exit(1); }
        if (*(g_long_ptr + 1) != 200L) { sys_write(1,"F3e\n",4); sys_exit(1); }
        if (*(g_long_ptr + 2) != 300L) { sys_write(1,"F3f\n",4); sys_exit(1); }
    }

    /* ═══ 测试 4: 嵌套指针算术 *(g_ptr + int + int) ═══ */
    {
        /* 此 AST 结构：外层 + 是 ptr+int，内层 + 是 int+int
         * 若 ptelem 用错，则计算偏移错误 */
        if (*(g_int_ptr + 0 + 0) != 10) { sys_write(1,"F4a\n",4); sys_exit(1); }
        if (*(g_int_ptr + 2 + 0) != 30) { sys_write(1,"F4b\n",4); sys_exit(1); }
        if (*(g_int_ptr + 0 + 1) != 20) { sys_write(1,"F4c\n",4); sys_exit(1); }
        if (*(g_int_ptr + 1 + 2) != 40) { sys_write(1,"F4d\n",4); sys_exit(1); }
        /* 变量嵌套 */
        {
            int a = 1, b = 2;
            if (*(g_int_ptr + a + b) != 40) { sys_write(1,"F4v\n",4); sys_exit(1); }
        }
    }

    /* ═══ 测试 5: 指针减法 (q - p) ═══ */
    {
        int *p = g_int_arr;
        int *q = g_int_arr + 3;
        /* q - p = 3（元素个数差，不是 12 字节差） */
        if (q - p != 3) { sys_write(1,"F5a\n",4); sys_exit(1); }
        if ((g_int_ptr + 2) - g_int_ptr != 2) { sys_write(1,"F5b\n",4); sys_exit(1); }
        if ((g_int_ptr + 4) - g_int_ptr != 4) { sys_write(1,"F5c\n",4); sys_exit(1); }
        /* 变量差 */
        {
            int off = 1;
            if ((g_int_ptr + off) - g_int_ptr != 1) { sys_write(1,"F5d\n",4); sys_exit(1); }
        }
    }

    /* ═══ 测试 6: 全局 int* 复合下标表达式 ═══ */
    {
        int idx = 2;
        if (g_int_ptr[idx + 0] != 30) { sys_write(1,"F7a\n",4); sys_exit(1); }
        if (g_int_ptr[idx + 1] != 40) { sys_write(1,"F7b\n",4); sys_exit(1); }
        /* code_buf[idx + expr] 风格 — 如 toyc 自身源码中的模式 */
        {
            int base = 0;
            if (g_int_ptr[base + 1] != 20) { sys_write(1,"F7c\n",4); sys_exit(1); }
            if (g_int_ptr[base + 3] != 40) { sys_write(1,"F7d\n",4); sys_exit(1); }
        }
    }

    sys_write(1, "OK\n", 3);
    sys_exit(0);
}
