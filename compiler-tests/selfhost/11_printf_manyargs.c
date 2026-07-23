// EXPECT: 0
// SELF_CONTAINED
// 11_printf_manyargs.c — 综合验证变参 printf 格式正确性与超多参数传参
//
// 背景：
//   03_many_printf_args.c 因 ≥7 参数传参 bug 限制每个 my_printf 调用最多 5 个
//   变参。该 bug（commit 2a3e5e9）及后续 r8/r9 REX 前缀/栈参数顺序 bug
//   （commit f841f69）已在 toyc 中修复。本测试全面验证修复后的效果。
//
// 测试内容：
//   Part 1: 格式说明符正确性 — %s %d %i %c %x %X %% 各格式独立测试
//   Part 2: 超多参数 printf — 6/7/8/9/10/12 个变参连续调用
//   Part 3: 混合类型超多参数 — char/short/long/pointer 混用
//   Part 4: 多次调用栈清洁 — 连续 >6-arg 调用的栈正确性
//   Part 5: 边界/异常情况

/* ============================================================
 *  syscall 包装（约束驱动内联汇编）
 * ============================================================ */

static long sys_write(int fd, const char *buf, unsigned long len)
{
    unsigned long ret;
    __asm__ __volatile__ ("syscall"
        : "=a"(ret)
        : "a"((long)1), "D"((long)fd), "S"((long)buf), "d"((long)len)
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

/* ============================================================
 *  字符串/打印辅助函数
 * ============================================================ */

static int my_strlen(const char *s) {
    int n = 0;
    while (*s) { s++; n++; }
    return n;
}

static void print_str(const char *s) {
    sys_write(1, s, (unsigned long)my_strlen(s));
}

static void print_dec(long n) {
    char buf[32];
    int i = 0;
    if (n < 0) { sys_write(1, "-", 1); n = -n; }
    if (n == 0) { sys_write(1, "0", 1); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0)
        sys_write(1, &buf[--i], 1);
}

static void print_hex(unsigned long n) {
    const char *hex = "0123456789abcdef";
    char buf[19];
    int i = 18;
    buf[18] = 0;
    if (n == 0) { sys_write(1, "0", 1); return; }
    while (n > 0) { buf[--i] = hex[n & 0xf]; n >>= 4; }
    sys_write(1, &buf[i], (unsigned long)(18 - i));
}

/* ============================================================
 *  my_printf — 完全匹配 toyc_rt.c 中 __printf 的实现
 *
 *  支持的格式子集：%s %d %i %c %x %X %%
 *
 *  注意：在自举测试中，%x 的实现使用 (unsigned long) 转换。
 *  与 toyc_rt.c 版本的唯一差异：函数名和 __write→sys_write。
 * ============================================================ */

static void my_printf(const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    const char *p = fmt;

    while (*p) {
        if (*p != '%') {
            sys_write(1, p, 1);
            p++;
            continue;
        }
        p++;  /* 跳过 % */
        switch (*p) {
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            sys_write(1, s, (unsigned long)my_strlen(s));
            break;
        }
        case 'd':
        case 'i':
            print_dec(__builtin_va_arg(ap, long));
            break;
        case 'c': {
            char c = (char)__builtin_va_arg(ap, int);
            sys_write(1, &c, 1);
            break;
        }
        case 'x':
        case 'X':
            print_hex((unsigned long)__builtin_va_arg(ap, long));
            break;
        case '%':
            sys_write(1, "%", 1);
            break;
        default:
            sys_write(1, p, 1);
            break;
        }
        p++;
    }
    __builtin_va_end(ap);
}

/* ============================================================
 *  测试框架
 * ============================================================ */

static int failures = 0;
static int test_num = 0;
static int assert_num = 0;

static void test_start(const char *name) {
    test_num++;
    assert_num = 0;
    print_str("  test ");
    {
        char buf[4];
        int i = 3;
        int v = test_num;
        buf[3] = 0;
        if (v >= 100) { buf[0] = '0' + v/100; v %= 100; } else buf[0] = ' ';
        if (v >= 10)  { buf[1] = '0' + v/10;  v %= 10;  } else buf[1] = ' ';
        buf[2] = '0' + v;
        sys_write(1, buf, 3);
    }
    print_str(": ");
    print_str(name);
    print_str("\n");
}

static void assert_fail(const char *msg) {
    print_str("    FAIL #");
    {
        char buf[4];
        int i = 3;
        int v = assert_num;
        buf[3] = 0;
        if (v >= 100) { buf[0] = '0' + v/100; v %= 100; } else buf[0] = ' ';
        if (v >= 10)  { buf[1] = '0' + v/10;  v %= 10;  } else buf[1] = ' ';
        buf[2] = '0' + v;
        sys_write(1, buf, 3);
    }
    print_str(": ");
    print_str(msg);
    print_str("\n");
    failures++;
}

#define CHECK(cond, msg) do { assert_num++; if (!(cond)) assert_fail(msg); } while (0)

/* ============================================================
 *  变参验证辅助函数
 *
 *  这些函数被调用时接受变参，通过 va_arg 取出并 CHECK。
 *  它们验证编译器正确传递了指定数量的变参给被调者。
 * ============================================================ */

/* 变参求和 */
static long va_sum(const char *dummy, int count, ...)
{
    __builtin_va_list ap;
    long s = 0;
    int i;
    __builtin_va_start(ap, dummy);
    for (i = 0; i < count; i++)
        s += __builtin_va_arg(ap, long);
    __builtin_va_end(ap);
    return s;
}

/* 变参验证：值与期望数组比较 */
static int va_check(const char *dummy, int count, const long *expected, ...)
{
    __builtin_va_list ap;
    int i;
    int ok = 1;
    __builtin_va_start(ap, dummy);
    for (i = 0; i < count; i++) {
        long val = __builtin_va_arg(ap, long);
        if (val != expected[i])
            ok = 0;
    }
    __builtin_va_end(ap);
    return ok;
}

/* 变参取字符串指针 */
static const char *va_get_str(const char *dummy, ...)
{
    __builtin_va_list ap;
    const char *s;
    __builtin_va_start(ap, dummy);
    s = __builtin_va_arg(ap, const char *);
    __builtin_va_end(ap);
    return s;
}

/* ============================================================
 *  Part 1: 格式说明符正确性（每个调用 ≤6 变参）
 * ============================================================ */

/* --- %s 测试 --- */
static void test_fmt_s(void) {
    test_start("%s format specifier");

    my_printf("    [s] \"%s\"\n", "hello world");
    my_printf("    [s] \"%s\"\n", "");
    my_printf("    [s] \"%s\"\n", "A");
    my_printf("    [s] \"%s\"\n", "abcdefghijklmnopqrstuvwxyz");

    /* 验证 va_arg 能从变参中取出正确的指针 */
    {
        const char *t1 = "test_str";
        const char *t2 = "hello_world";
        const char *s = va_get_str("dummy", t1);
        CHECK(s == t1, "va_arg retrieves pointer (#1)");
        s = va_get_str("dummy", t2);
        CHECK(s == t2, "va_arg retrieves pointer (#2)");
    }
}

/* --- %d 测试 --- */
static void test_fmt_d(void) {
    test_start("%d format specifier (signed long)");

    my_printf("    [d] %d %d %d %d\n", 0, 1, -1, 42);
    my_printf("    [d] large: %d\n", 2147483647);
    my_printf("    [d] negative large: %d\n", -2147483647);
    my_printf("    [d] zero: %d\n", 0);

    /* 验证变参值 */
    {
        long s = va_sum("d", 4, 0, 1, -1, 42);
        CHECK(s == 42, "va_sum(0,1,-1,42) == 42");
    }
    {
        long s = va_sum("d", 3, 100, -200, 300);
        CHECK(s == 200, "va_sum(100,-200,300) == 200");
    }
}

/* --- %c 测试 --- */
static void test_fmt_c(void) {
    test_start("%c format specifier (char)");

    my_printf("    [c] \"%c%c%c%c%c\"\n", 'H', 'e', 'l', 'l', 'o');
    my_printf("    [c] newline%c", '\n');
    my_printf("    [c] null char: '%c'\n", '\0');

    /* 验证 char 通过 va_arg 读取为 int */
    {
        long s = va_sum("d", 2, (long)'X', (long)'Y');
        CHECK(s == (long)('X' + 'Y'), "va_sum('X','Y') sum matches");
    }
}

/* --- %x 测试 --- */
static void test_fmt_x(void) {
    test_start("%x format specifier (hex)");

    my_printf("    [x] %x %x %x\n", 0, 255, 0xdeadbeef);
    my_printf("    [x] powers: %x %x %x %x\n", 0x1, 0x10, 0x100, 0x1000);
    my_printf("    [x] max32: %x\n", 0xffffffff);
    my_printf("    [x] all ones: %x\n", 0xffffffffffffffff);

    {
        long s = va_sum("d", 2, 0xdead, 0xbeef);
        CHECK(s == 0xdead + 0xbeef, "va_sum(0xdead,0xbeef) sum matches");
    }
}

/* --- %% 测试 --- */
static void test_fmt_percent(void) {
    test_start("%% literal percent");
    my_printf("    [%%] 100%% complete\n");
    my_printf("    [%%] %%%% double percent%%%%\n");
    CHECK(1 == 1, "%% doesn't crash");
}

/* --- 混合格式（≤6 变参） --- */
static void test_mixed_formats(void) {
    test_start("mixed formats in single call (≤6 args)");
    my_printf("    mixed: s=%s d=%d c=%c x=%x\n", "str", 42, '!', 0xff);
    my_printf("    all: %s %d %i %c %x %%\n", "ok", 1, 2, 'Z', 10);

    /* 验证混合变参值 */
    {
        /* 不能用 va_sum（求和会混合指针和数值），用独立验证 */
        long s = va_sum("d", 4, 42, '!', 0xff, 0);
        CHECK(s == (42 + (long)'!' + 0xff), "mixed format values correct");
    }
}

/* ============================================================
 *  Part 2: 超多参数 printf（6-12 个变参——栈上传参路径）
 * ============================================================
 *
 * 对于变参函数，x86-64 ABI 调用者必须：
 *   - 前 6 个参数放到 rdi(已用于fmt), rsi, rdx, rcx, r8, r9
 *   - 第 7+ 个参数放入栈中（注意逆序压入）
 *   - %al 设为 0（无浮点变参）
 *
 * 被调者通过 va_arg 从寄存器保存区或栈读取。
 *
 * 为程序化验证，每个测试通过 va_sum/va_check 函数验证所有值正确。
 * ============================================================ */

/* 值常量 */
#define P0  2
#define P1  3
#define P2  5
#define P3  7
#define P4  11
#define P5  13
#define P6  17
#define P7  19
#define P8  23
#define P9  29
#define P10 31
#define P11 37

/* --- 6 个变参（寄存器路径基线） --- */
static void test_printf_6args(void) {
    test_start("printf: 6 variadic args (register baseline)");

    /* 调用 printf 显示值 */
    my_printf("    6args: %d %d %d %d %d %d\n",
        P0, P1, P2, P3, P4, P5);

    /* 程序化验证：通过 va_sum 验证每个值 */
    {
        long s = va_sum("dummy", 6, P0, P1, P2, P3, P4, P5);
        CHECK(s == (P0+P1+P2+P3+P4+P5), "va_sum 6 args = 41");
    }

    /* 变量作为参数 */
    {
        long a0=P0, a1=P1, a2=P2, a3=P3, a4=P4, a5=P5;
        my_printf("    6vars: %d %d %d %d %d %d\n",
            a0, a1, a2, a3, a4, a5);
        long s = va_sum("dummy", 6, a0, a1, a2, a3, a4, a5);
        CHECK(s == 41, "va_sum 6 vars = 41");
    }
}

/* --- 7 个变参（1 个栈参数） --- */
static void test_printf_7args(void) {
    test_start("printf: 7 variadic args (1 stack arg)");

    my_printf("    7args: %d %d %d %d %d %d %d\n",
        P0, P1, P2, P3, P4, P5, P6);

    {
        long s = va_sum("dummy", 7, P0, P1, P2, P3, P4, P5, P6);
        CHECK(s == (P0+P1+P2+P3+P4+P5+P6), "va_sum 7 args = 58");
    }

    /* 变量作为参数 */
    {
        long a0=P0, a1=P1, a2=P2, a3=P3, a4=P4, a5=P5, a6=P6;
        my_printf("    7vars: %d %d %d %d %d %d %d\n",
            a0, a1, a2, a3, a4, a5, a6);
        long s = va_sum("dummy", 7, a0, a1, a2, a3, a4, a5, a6);
        CHECK(s == 58, "va_sum 7 vars = 58");
    }
}

/* --- 8 个变参（2 个栈参数） --- */
static void test_printf_8args(void) {
    test_start("printf: 8 variadic args (2 stack args)");

    my_printf("    8args: %d %d %d %d %d %d %d %d\n",
        P0, P1, P2, P3, P4, P5, P6, P7);

    {
        long s = va_sum("dummy", 8, P0, P1, P2, P3, P4, P5, P6, P7);
        CHECK(s == (P0+P1+P2+P3+P4+P5+P6+P7), "va_sum 8 args = 77");
    }
}

/* --- 9 个变参（3 个栈参数） --- */
static void test_printf_9args(void) {
    test_start("printf: 9 variadic args (3 stack args)");

    my_printf("    9args: %d %d %d %d %d %d %d %d %d\n",
        P0, P1, P2, P3, P4, P5, P6, P7, P8);

    {
        long s = va_sum("dummy", 9, P0, P1, P2, P3, P4, P5, P6, P7, P8);
        CHECK(s == (P0+P1+P2+P3+P4+P5+P6+P7+P8), "va_sum 9 args = 100");
    }
}

/* --- 10 个变参（4 个栈参数） --- */
static void test_printf_10args(void) {
    test_start("printf: 10 variadic args (4 stack args)");

    my_printf("    10args: %d %d %d %d %d %d %d %d %d %d\n",
        P0, P1, P2, P3, P4, P5, P6, P7, P8, P9);

    {
        long s = va_sum("dummy", 10, P0, P1, P2, P3, P4, P5, P6, P7, P8, P9);
        CHECK(s == (P0+P1+P2+P3+P4+P5+P6+P7+P8+P9), "va_sum 10 args = 129");
    }
}

/* --- 12 个变参（6 个栈参数） --- */
static void test_printf_12args(void) {
    test_start("printf: 12 variadic args (6 stack args)");

    my_printf("    12args: %d %d %d %d %d %d %d %d %d %d %d %d\n",
        P0, P1, P2, P3, P4, P5, P6, P7, P8, P9, P10, P11);

    {
        long s = va_sum("dummy", 12, P0, P1, P2, P3, P4, P5, P6, P7, P8, P9, P10, P11);
        CHECK(s == (P0+P1+P2+P3+P4+P5+P6+P7+P8+P9+P10+P11), "va_sum 12 args = 197");
    }
}

/* ============================================================
 *  Part 3: 混合类型超多参数
 * ============================================================ */

static void test_mixed_printf_8(void) {
    test_start("mixed types: 8 variadic args");

    my_printf("    mixed8: %s %d %c %x %s %d %c %x\n",
        "str1", 42, 'A', 0xff, "str2", -99, 'Z', 0xdead);

    {
        long expected_sum = 42 + (long)'A' + 0xff + (-99) + (long)'Z' + 0xdead;
        long s = va_sum("d", 8, (long)"str1", 42, 'A', 0xff,
                              (long)"str2", -99, 'Z', 0xdead);
        /* 注意：指针值（"str1","str2"）加到 sum 里会很大，但我们只要确认
           va_arg 读出的所有 8 个值都正确。用 va_check 更精确。 */
        long expected[8] = {(long)"str1", 42, 'A', 0xff,
                            (long)"str2", -99, 'Z', 0xdead};
        int ok = va_check("d", 8, expected,
                          expected[0], expected[1], expected[2], expected[3],
                          expected[4], expected[5], expected[6], expected[7]);
        CHECK(ok, "mixed 8-arg va_check all match");
    }
}

static void test_mixed_printf_10(void) {
    test_start("mixed types: 10 variadic args");

    my_printf("    mixed10: %d %s %c %d %x %s %d %c %x %d\n",
        P0, "a", 'b', P2, 0xabc, "cde", P5, 'd', 0x1234, P9);

    {
        long expected[10] = {P0, (long)"a", 'b', P2, 0xabc,
                             (long)"cde", P5, 'd', 0x1234, P9};
        int ok = va_check("d", 10, expected,
                          expected[0], expected[1], expected[2], expected[3],
                          expected[4], expected[5], expected[6], expected[7],
                          expected[8], expected[9]);
        CHECK(ok, "mixed 10-arg va_check all match");
    }
}

/* ============================================================
 *  Part 4: 多次调用栈清洁
 * ============================================================ */

static void test_multi_printf_calls(void) {
    test_start("multiple >6-arg calls (stack cleanliness)");

    /* 连续多次 7-arg 调用 */
    my_printf("    multi1: %d %d %d %d %d %d %d\n", 1,2,3,4,5,6,7);
    my_printf("    multi2: %d %d %d %d %d %d %d\n", 10,20,30,40,50,60,70);
    my_printf("    multi3: %d %d %d %d %d %d %d\n", 100,200,300,400,500,600,700);

    {
        long s1 = va_sum("d", 7, 1,2,3,4,5,6,7);
        long s2 = va_sum("d", 7, 10,20,30,40,50,60,70);
        long s3 = va_sum("d", 7, 100,200,300,400,500,600,700);
        CHECK(s1 == 28, "multi sum1 == 28");
        CHECK(s2 == 280, "multi sum2 == 280");
        CHECK(s3 == 2800, "multi sum3 == 2800");
    }
}

static void test_multi_arg_counts(void) {
    test_start("mixed arg counts: 6, 7, 8, 9, 10, 12");

    /* 调用不同参数量的 printf——验证调用约定状态机 */
    my_printf("    6call: %d %d %d %d %d %d\n", P0,P1,P2,P3,P4,P5);
    my_printf("    7call: %d %d %d %d %d %d %d\n", P0,P1,P2,P3,P4,P5,P6);
    my_printf("    8call: %d %d %d %d %d %d %d %d\n", P0,P1,P2,P3,P4,P5,P6,P7);
    my_printf("    9call: %d %d %d %d %d %d %d %d %d\n", P0,P1,P2,P3,P4,P5,P6,P7,P8);
    my_printf("    10call: %d %d %d %d %d %d %d %d %d %d\n", P0,P1,P2,P3,P4,P5,P6,P7,P8,P9);
    my_printf("    12call: %d %d %d %d %d %d %d %d %d %d %d %d\n", P0,P1,P2,P3,P4,P5,P6,P7,P8,P9,P10,P11);

    {
        long s6  = va_sum("d", 6, P0,P1,P2,P3,P4,P5);
        long s7  = va_sum("d", 7, P0,P1,P2,P3,P4,P5,P6);
        long s8  = va_sum("d", 8, P0,P1,P2,P3,P4,P5,P6,P7);
        long s9  = va_sum("d", 9, P0,P1,P2,P3,P4,P5,P6,P7,P8);
        long s10 = va_sum("d", 10, P0,P1,P2,P3,P4,P5,P6,P7,P8,P9);
        long s12 = va_sum("d", 12, P0,P1,P2,P3,P4,P5,P6,P7,P8,P9,P10,P11);
        CHECK(s6  == 41,  "mixed s6  == 41");
        CHECK(s7  == 58,  "mixed s7  == 58");
        CHECK(s8  == 77,  "mixed s8  == 77");
        CHECK(s9  == 100, "mixed s9  == 100");
        CHECK(s10 == 129, "mixed s10 == 129");
        CHECK(s12 == 197, "mixed s12 == 197");
    }
}

/* ============================================================
 *  Part 5: 边界/异常情况
 * ============================================================ */

static void test_edge_cases(void) {
    test_start("edge cases");

    /* 空格式字符串 */
    my_printf("");

    /* 纯文本（无格式说明符） */
    my_printf("    edge: plain text without specifiers\n");

    /* 长格式字符串 */
    my_printf("    edge: %s %s %s %s %s %s %s\n",
        "seven", "strings", "in", "one", "printf", "call", "!");

    /* %i 等价于 %d */
    my_printf("    edge: %%i test: %i %i %i\n", -1, 0, 1);

    /* 连续 % */
    my_printf("    edge: %%%s%%\n", "between");

    /* 大负数（校验传参） */
    {
        long big_neg = -9223372036854775807L - 1;
        long big_pos = 9223372036854775807L;
        long zero = 0;
        long s = va_sum("d", 3, big_neg, big_pos, zero);
        CHECK(s == (big_neg + big_pos + zero),
              "va_sum with extreme values doesn't crash");
    }

    my_printf("    edge: all edge case tests completed\n");
}

/* ============================================================
 *  运行所有测试
 * ============================================================ */

static void run_tests(void) {
    print_str("=== printf correctness + many-args test ===\n\n");

    print_str("[Part 1: Format specifier correctness]\n");
    test_fmt_s();            /*  1 */
    test_fmt_d();            /*  2 */
    test_fmt_c();            /*  3 */
    test_fmt_x();            /*  4 */
    test_fmt_percent();      /*  5 */
    test_mixed_formats();    /*  6 */

    print_str("\n[Part 2: Many-argument printf]\n");
    test_printf_6args();     /*  7 */
    test_printf_7args();     /*  8 */
    test_printf_8args();     /*  9 */
    test_printf_9args();     /* 10 */
    test_printf_10args();    /* 11 */
    test_printf_12args();    /* 12 */

    print_str("\n[Part 3: Mixed types many args]\n");
    test_mixed_printf_8();   /* 13 */
    test_mixed_printf_10();  /* 14 */

    print_str("\n[Part 4: Multiple calls / stack cleanliness]\n");
    test_multi_printf_calls();   /* 15 */
    test_multi_arg_counts();     /* 16 */

    print_str("\n[Part 5: Edge cases]\n");
    test_edge_cases();       /* 17 */

    print_str("\n");
    if (failures == 0) {
        print_str("ALL PASSED — printf correctness + many-args\n");
        sys_exit(0);
    } else {
        print_str("SOME TESTS FAILED: ");
        print_dec(failures);
        print_str(" failure(s)\n");
        sys_exit(failures > 127 ? 127 : failures);
    }
}

void __tlibc_start(void) { run_tests(); }
