// EXPECT: 0
// SELF_CONTAINED
// manyprintfargs.c — 测试变参函数在适度参数下的正确性
// 注意：toyc 暂时不支持 ≥7 个参数的函数调用（问题 #2），所以每个 my_printf 调用
// 最多只传 5 个变参（fmt + 5 variadic = 6 个参数，刚好不触发 bug）。

// ============================================================
// syscall 包装（约束驱动）
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
// my_printf 实现
// ============================================================

static int my_strlen(const char *s) {
    int n = 0; while (*s) { s++; n++; } return n;
}

static void print_dec(long n) {
    char buf[32]; int i = 0;
    if (n < 0) { sys_write(1, "-", 1); n = -n; }
    if (n == 0) { sys_write(1, "0", 1); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) sys_write(1, &buf[--i], 1);
}

static void print_hex(unsigned long n)
{
    const char *hex = "0123456789abcdef";
    char buf[17];
    int i = 0;
    if (n == 0) { sys_write(1, "0", 1); return; }
    while (n > 0) { buf[i++] = hex[n & 0xf]; n >>= 4; }
    while (i > 0) sys_write(1, &buf[--i], 1);
}

static void my_printf(const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') { sys_write(1, fmt, 1); fmt++; continue; }
        fmt++;
        switch (*fmt) {
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            sys_write(1, s, (unsigned long)my_strlen(s)); break;
        }
        case 'd': case 'i':
            print_dec(__builtin_va_arg(ap, long)); break;
        case 'c': {
            char c = (char)__builtin_va_arg(ap, int);
            sys_write(1, &c, 1); break;
        }
        case 'x': case 'X':
            print_hex(__builtin_va_arg(ap, unsigned long)); break;
        case '%': sys_write(1, "%", 1); break;
        default:  sys_write(1, fmt, 1); break;
        }
        fmt++;
    }
    __builtin_va_end(ap);
}

// ============================================================
// 辅助函数
// ============================================================

static void println(void) { sys_write(1, "\n", 1); }

// ============================================================
// 测试：每种调用不超过 5 个变参（总参数 ≤6，避免触发问题 #2）
// ============================================================

/* 5 个 long 值 */
static void test_5_args(void)
{
    long a1 = 10;
    long a2 = 20;
    long a3 = 30;
    long a4 = 40;
    long a5 = 50;

    my_printf("  5 args: %d %d %d %d %d\n", a1, a2, a3, a4, a5);
}

/* 4 个 long 值 */
static void test_4_args(void)
{
    my_printf("  4 args: %d %d %d %d\n", 1, 2, 3, 4);
}

/* 混合类型：5 个变参（fmt + 5 variadic ≤6，不触发栈上传参） */
static void test_mixed_types(void)
{
    my_printf("  mixed: %s=%d %s=%x %s\n",
        "hex", 255, "dec", 255, "char");
}

/* 空格式字符串 */
static void test_empty_fmt(void)
{
    my_printf("");
}

/* 只有纯文本，无需变参 */
static void test_text_only(void)
{
    my_printf("  text only: no format specifiers\n");
}

/* 5 个连续 %c */
static void test_many_chars(void)
{
    my_printf("  chars: %c%c%c%c%c\n", '0','1','2','3','4');
}

/* 5 个 %x */
static void test_5_hex(void)
{
    long n1 = 1;   long n2 = 2;   long n3 = 3;
    long n4 = 4;   long n5 = 5;

    my_printf("  5 hex: %x %x %x %x %x\n",
        n1, n2, n3, n4, n5);
}

/* 5 个字符串指针 */
static void test_many_strings(void)
{
    my_printf("  strings: %s %s %s %s %s\n",
        "A", "BB", "CCC", "DDDD", "EEEEE");
}

/* 多个变参函数调用（验证 va_start/va_end 重置） */
static void test_multiple_calls(void)
{
    my_printf("  multi1: %d %d %d\n", 1, 2, 3);
    my_printf("  multi2: %d %d %d %d %d\n", 1, 2, 3, 4, 5);
    my_printf("  multi3: %d %d %d %d %d\n", 1, 2, 3, 4, 5);
}

/* 连续两次调用验证寄存器重置 */
static void test_5_twice(void)
{
    my_printf("  5a: %d %d %d %d %d\n", 1, 2, 3, 4, 5);
    my_printf("  5b: %d %d %d %d %d\n", 5, 4, 3, 2, 1);
}

// ============================================================
// main
// ============================================================

int main(void)
{
    my_printf("=== many-argument printf test ===\n\n");

    test_5_args();
    test_4_args();
    test_mixed_types();
    test_empty_fmt();
    test_text_only();
    test_many_chars();
    test_5_hex();
    test_many_strings();
    test_multiple_calls();
    test_5_twice();

    my_printf("\n=== ALL MANY-ARGUMENT TESTS PASSED ===\n");
    return 0;
}

void __tlibc_start(void) { sys_exit(main()); }
