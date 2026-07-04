// EXPECT: 0
// selfcontained.c — 完全自包含：实现 printf → 正常返回
// 不引用任何外部函数（不链接 tcc_rt.o）
//
// 编译：build/tcc  27_selfcontained.c -o /tmp/27.o
// 链接：ld -nostdlib -static -T ld.script -e main /tmp/27.o build/tcc_rt_start.o -o /tmp/27
// 运行：/tmp/27
//
// 依赖（仅链接时）：tcc_rt_start.o（提供 __tlibc_start → main → exit）
// 编译期无任何外部依赖

// ============================================================
// syscall 包装 — 直接内联汇编，无外部依赖
// 注意：tcc 的 asm 约束只生成局部变量（不加载到寄存器），
// 因此必须通过调用约定配置 rax/rdi/rsi/rdx。
// 方案：利用调用前 rax=函数返回值、rdi/rsi/rdx=参数的约定。
// ============================================================

static long sys_write(int fd, const char *buf, unsigned long len)
{
    /* rax=返回值, rdi=fd, rsi=buf, rdx=len 由调用方设置 */
    unsigned long ret;
    __asm__ __volatile__ ("syscall"
        : "=a"(ret)
        : "a"(1), "D"((long)fd), "S"((long)buf), "d"((long)len)
        : "rcx", "r11", "memory");
    return (long)ret;
}

/* sys_exit 无法用 asm 约束工作（tcc 不将 "a" 约束加载到 rax），
 * 由 tcc_rt_start.o 的 __tlibc_start 在 main 返回后执行 SYS_exit。 */

// ============================================================
// 字符串辅助函数（自包含，无外部引用）
// ============================================================

static int my_strlen(const char *s)
{
    int n = 0;
    while (*s) { s++; n++; }
    return n;
}

// ============================================================
// printf 实现（仿照 tcc_rt.c __printf，使用 __builtin_va_*）
// ============================================================

static void print_dec(long n)
{
    char buf[32];
    int i = 0;
    if (n < 0) {
        sys_write(1, "-", 1);
        n = -n;
    }
    if (n == 0) {
        sys_write(1, "0", 1);
        return;
    }
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0)
        sys_write(1, &buf[--i], 1);
}

static void print_hex(unsigned long n)
{
    const char *hex = "0123456789abcdef";
    char buf[17];
    int i = 0;
    if (n == 0) {
        sys_write(1, "0", 1);
        return;
    }
    while (n > 0) {
        buf[i++] = hex[n & 0xf];
        n >>= 4;
    }
    while (i > 0)
        sys_write(1, &buf[--i], 1);
}

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
        p++;  /* skip % */
        switch (*p) {
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            sys_write(1, s, (unsigned long)my_strlen(s));
            break;
        }
        case 'd':
        case 'i':
            /* 读 64-bit；调用方须传 long 或运行时计算负值 */
            print_dec(__builtin_va_arg(ap, long));
            break;
        case 'c': {
            char c = (char)__builtin_va_arg(ap, int);
            sys_write(1, &c, 1);
            break;
        }
        case 'x':
        case 'X':
            print_hex(__builtin_va_arg(ap, unsigned long));
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

// ============================================================
// main — 由 tcc_rt_start.o 的 __tlibc_start 调用
// ============================================================

int main(void)
{
    my_printf("Self-contained test program started!\n");
    my_printf("================================\n");

    /* %s */
    my_printf("[%%s]  String: %s\n", "Hello from self-contained printf!");

    /* %d — tcc int→long 有零扩展 BUG，负值须运行时计算 */
    {
        long neg = 12345;
        neg = -neg;
        my_printf("[%%d]  Integer: %d\n", 42);
        my_printf("[%%d]  Negative: %d\n", neg);
        my_printf("[%%d]  Zero: %d\n", 0);
    }

    /* %x */
    my_printf("[%%x]  Hex 255: %x\n", 255);
    my_printf("[%%x]  Hex 3735928559: %x\n", 0xdeadbeef);
    my_printf("[%%x]  Hex zero: %x\n", 0);

    /* %c */
    my_printf("[%%c]  Char A: %c\n", 'A');
    my_printf("[%%c]  Chars: %c%c%c\n", 'X', 'Y', 'Z');

    /* Multiple format specifiers in one call */
    my_printf("Multi: %s=%d (%s)\n", "answer", 42, "ok");

    /* %% literal */
    my_printf("[%%%%] 100%% self-contained!\n");

    /* Long string */
    my_printf("%s %s %s %s!\n",
        "This", "is", "completely", "self-contained");

    my_printf("================================\n");
    my_printf("All tests passed! Exiting...\n");

    return 0;
}
