// EXPECT: 0
// SELF_CONTAINED
// 24_ptr_offset.c — 验证指针+偏移寻址模式的 elem_size 传播
//
// 问题背景：
// 1. (p+N)[i] 指针算术结果的下标用错 elem_size（cgen_addr / cgen_expr 读路径
//    漏掉通用 node->left->elem_size 回退）
// 2. ptr + long_offset 不被识别为指针算术（条件限制 type_size<=4，
//    long 的 type_size=8 被跳过）
// 3. 64-bit 整数偏移用 32-bit shift/imul 截断高 32 位
//
// 编译：build/tcc  24_ptr_offset.c -o /tmp/24_ptr_offset.o
// 链接：ld -nostdlib -static -T ld.script /tmp/24_ptr_offset.o -o /tmp/24_ptr_offset
// 运行：/tmp/24_ptr_offset

// ============================================================
// syscall 包装
// ============================================================

static long sys_write(int fd, const char *buf, unsigned long len)
{
    unsigned long ret;
    __asm__ __volatile__ ("syscall"
        : "=a"(ret)
        : "a"(1), "D"((long)fd), "S"((long)buf), "d"((long)len)
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

// ============================================================
// 简单输出和断言
// ============================================================

static int my_strlen(const char *s) {
    int n = 0; while (*s) { s++; n++; } return n;
}

static void print_str(const char *s) {
    sys_write(1, s, my_strlen(s));
}

static void print_dec(long n) {
    char buf[32];
    int i = 0;
    if (n < 0) { sys_write(1, "-", 1); n = -n; }
    if (n == 0) { sys_write(1, "0", 1); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) sys_write(1, &buf[--i], 1);
}

static int failures = 0;
static int test_id = 0;

static void test_start(const char *name) {
    test_id++;
    print_str("  test ");
    print_dec(test_id);
    print_str(": ");
    print_str(name);
    print_str("\n");
}

static void check(int cond, const char *msg, int line)
{
    if (!cond) {
        print_str("    FAIL (line ");
        print_dec(line);
        print_str("): ");
        print_str(msg);
        print_str("\n");
        failures++;
    }
}

// ============================================================
// 测试主体
// ============================================================

void __tlibc_start(void)
{
    int arr[10];
    int i;

    /* 初始化测试数组 */
    for (i = 0; i < 10; i++)
        arr[i] = i * 10;

    /* ─── Test 1: (p+N)[i] — 指针算术结果用作数组下标 ─── */
    test_start("(p+N)[i] with correct elem_size");
    {
        int *p = arr;
        int val;

        /* (p+1)[3] 应等价于 arr[1+3] = arr[4] = 40
         * Bug 1: 若 elem_size 回退到 1（char 缩放），(p+1)[3]
         * 会读出地址 p+1+3 = p+4 处的 4 字节（arr[1] 的低字节），
         * 而非 p+1+3*4 = p+13 处的 arr[4] */
        val = (p + 1)[3];
        check(val == 40, "(p+1)[3] == 40", 89);

        /* (p+2)[5] 应等价于 arr[2+5] = arr[7] = 70 */
        val = (p + 2)[5];
        check(val == 70, "(p+2)[5] == 70", 93);

        /* 链式：(q+1)[2] where q = p+2 → arr[2+1+2] = arr[5] = 50 */
        {
            int *q = p + 2;
            val = (q + 1)[2];
            check(val == 50, "(q+1)[2] == 50, q = arr+2", 98);
        }
    }

    /* ─── Test 2: ptr + long_offset — 64-bit 偏移的指针算术 ─── */
    test_start("ptr + long_offset pointer arithmetic");
    {
        int *p = arr;
        long step = 3;
        int *q;

        /* p + step（step 是 long）应缩放：p + 3 * sizeof(int) = p + 12
         * Bug 2: 若 long 不被识别为整数操作数，p + step 只是地址 + 3，
         * *q 会读出 arr[0] 的 2-5 字节（错误值）而非 arr[3]=30 */
        q = p + step;
        check(*q == 30, "*(p + 3L) == 30", 112);

        /* 更大的偏移 */
        step = 7;
        q = p + step;
        check(*q == 70, "*(p + 7L) == 70", 116);

        /* 减法：p - step (backwards) */
        q = p + 9;
        step = 4;
        q = q - step;
        check(*q == 50, "*(q - 4L) == 50 where q = arr+9", 121);
    }

    /* ─── Test 3: arr[long_idx] — 数组下标使用 long 索引 ─── */
    test_start("arr[long_idx] array subscript");
    {
        long idx = 0;

        idx = 5;
        check(arr[idx] == 50, "arr[5L] == 50", 129);

        idx = 8;
        check(arr[idx] == 80, "arr[8L] == 80", 132);

        /* 索引表达式 long + int */
        idx = 2;
        check(arr[idx + 1] == 30, "arr[2L + 1] == 30", 135);
    }

    /* ─── Test 4: long + long 纯整数加法不被误判为指针算术 ─── */
    test_start("long + long should NOT be pointer arithmetic");
    {
        long a = 100;
        long b = 200;
        long c = a + b;
        check(c == 300, "long + long == 300", 144);
    }

    /* ─── Test 5: 最大偏移边界（8 字节指针 + long 索引数组元素） ─── */
    test_start("address calculation via unsigned long cast");
    {
        /* 通过强制转换验证指针算术的正确性：
         * (int*)((unsigned long)arr + step * sizeof(int)) 应等价于 arr+step */
        unsigned long base = (unsigned long)arr;
        unsigned long step_ul = 3;
        int *r = (int *)(base + step_ul * (unsigned long)sizeof(int));
        check(*r == 30, "(unsigned long)arr + 3*4 points to arr[3]=30", 155);

        step_ul = 6;
        r = (int *)(base + step_ul * (unsigned long)sizeof(int));
        check(*r == 60, "(unsigned long)arr + 6*4 points to arr[6]=60", 159);
    }

    /* ─── Test 6: short 指针的 (p+N)[i] ─── */
    test_start("(short*)(p+N)[i] with non-int elem_size");
    {
        short sarr[10];
        for (i = 0; i < 10; i++)
            sarr[i] = (short)(i * 100);

        short *sp = sarr;
        /* (sp+2)[4] 应等价于 sarr[2+4] = sarr[6] = 600
         * elem_size=2 的缩放链：先 sp+2→sp+4 字节，再 [4]→+8 字节 */
        short sv = (sp + 2)[4];
        check(sv == 600, "(sp+2)[4] == 600 (short, elem_size=2)", 173);

        /* long 偏移 */
        long sl = 3;
        sp = sarr;
        sv = (sp + sl)[5];
        check(sv == 800, "(sp+3L)[5] == 800 (short+long)", 178);
    }

    /* ─── 汇总 ─── */
    print_str("\n  ");
    if (failures) {
        print_str("FAILED: ");
        print_dec(failures);
        print_str(" test(s) failed\n");
        sys_exit(1);
    }
    print_str("ALL PASS\n");
    sys_exit(0);
}
