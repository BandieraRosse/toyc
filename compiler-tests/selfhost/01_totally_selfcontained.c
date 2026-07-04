// EXPECT: 0
// totally_selfcontained.c — 零外部依赖：自身实现 _start + printf + exit
// 编译：build/tcc 28_totally_selfcontained.c -o /tmp/28.o
// 链接：ld -nostdlib -static -T ld.script /tmp/28.o -o /tmp/28
// 不依赖 tcc_rt.o 也不依赖 tcc_rt_start.o
//
// 验证 tcc 的内联汇编约束加载功能（2026-07-04 新增特性）

// ============================================================
// syscall 包装 — 使用约束驱动的内联汇编
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
    for (;;) /* unreachable */ ;
}

// ============================================================
// 字符串辅助函数
// ============================================================

static int my_strlen(const char *s)
{
    int n = 0;
    while (*s) { s++; n++; }
    return n;
}

// ============================================================
// printf 实现（使用 __builtin_va_*）
// ============================================================

static void print_dec(long n)
{
    char buf[32];
    int i = 0;
    if (n < 0) { sys_write(1, "-", 1); n = -n; }
    if (n == 0) { sys_write(1, "0", 1); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0)
        sys_write(1, &buf[--i], 1);
}

static void print_hex(unsigned long n)
{
    const char *hex = "0123456789abcdef";
    char buf[17];
    int i = 0;
    if (n == 0) { sys_write(1, "0", 1); return; }
    while (n > 0) { buf[i++] = hex[n & 0xf]; n >>= 4; }
    while (i > 0)
        sys_write(1, &buf[--i], 1);
}

static void my_printf(const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    const char *p = fmt;
    while (*p) {
        if (*p != '%') { sys_write(1, p, 1); p++; continue; }
        p++;
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
// main — 测试代码
// ============================================================

int main(void)
{
    my_printf("Totally self-contained program!\n");
    my_printf("================================\n");
    my_printf("No external .o files needed.\n");
    my_printf("Entry point: __tlibc_start\n");
    my_printf("Exit via sys_exit with constraints\n");
    my_printf("================================\n");

    /* 测试 %s */
    my_printf("[%%s] %s\n", "Hello from zero-dependency C!");

    /* 测试 %d */
    {
        long neg = 42;
        neg = -neg;
        my_printf("[%%d] pos=%d neg=%d zero=%d\n", 123, neg, 0);
    }

    /* 测试 %x */
    my_printf("[%%x] 255=%x deadbeef=%x zero=%x\n", 255, 0xdeadbeef, 0);

    /* 测试 %c */
    my_printf("[%%c] %c%c%c\n", 'A', 'B', 'C');

    /* 测试 %% */
    my_printf("[%%%%] 100%% stand-alone!\n");

    my_printf("\nstatus: ALL TESTS PASSED\n");
    return 0;
}

// ============================================================
// 入口点 __tlibc_start — 匹配 ld.script 的 ENTRY(__tlibc_start)
// 作为 C 函数：prologue (push rbp) 无害，因为 sys_exit 不返回
// ============================================================

void __tlibc_start(void)
{
    sys_exit(main());
}
