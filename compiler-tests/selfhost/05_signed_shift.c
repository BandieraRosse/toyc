// EXPECT: 0
// SELF_CONTAINED
// signed_shift.c — 测试 signed 类型的右移位行为
//
// toyc 代码生成缺陷：对有符号负数 >> 使用了 shr（逻辑右移）
// 而非 sar（算术右移），导致高位补 0 而非补符号位。
//
// 编译：build/toyc  05_signed_shift.c -o /tmp/05_signed_shift.o
// 链接：ld -nostdlib -static -T ld.script /tmp/05_signed_shift.o -o /tmp/05_signed_shift
// 运行：/tmp/05_signed_shift

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
// 简单输出函数
// ============================================================

static int my_strlen(const char *s) {
    int n = 0; while (*s) { s++; n++; } return n;
}

static void print_str(const char *s) {
    sys_write(1, s, my_strlen(s));
}

// ============================================================
// 测试函数
// ============================================================

static int failures = 0;

static void check(int cond, const char *msg) {
    if (!cond) {
        print_str("  FAIL: ");
        print_str(msg);
        print_str("\n");
        failures++;
    }
}

// ─── 测试 1: signed int 右移位 ───
// toyc 曾对 >> 始终使用 shr（逻辑右移），但 signed 类型应
// 使用 sar（算术右移，高位补符号位）。
// (-8) >> 1 应为 -4，但 shr 给出 0x7FFFFFFC。
static void test_int_rshift(void) {
    int x = -8;
    int r = x >> 1;
    check(r == -4, "signed int (-8) >> 1 应为 -4（算术移位）");

    // 保持符号位的移位
    int neg = -1;
    check(neg >> 1 == -1, "signed int (-1) >> 1 应为 -1（符号位传播）");

    // 正数（signed 和 unsigned 行为一致，但还是要保证正确）
    int pos = 8;
    check(pos >> 1 == 4, "signed int 8 >> 1 应为 4");
}

// ─── 测试 2: signed long 右移位 ───
static void test_long_rshift(void) {
    long y = -16L;
    long s = y >> 2;
    check(s == -4L, "signed long (-16) >> 2 应为 -4（算术移位）");

    long neg = -1L;
    check(neg >> 1 == -1L, "signed long (-1) >> 1 应为 -1（符号位传播）");

    long pos = 16L;
    check(pos >> 2 == 4L, "signed long 16 >> 2 应为 4");
}

// ============================================================
// entry point
// ============================================================

static void run_tests(void) {
    print_str("=== signed right shift tests ===\n");

    print_str("--- test_int_rshift ---\n");
    test_int_rshift();

    print_str("--- test_long_rshift ---\n");
    test_long_rshift();

    print_str("\n");

    if (failures == 0) {
        print_str("ALL PASSED\n");
        sys_exit(0);
    }

    sys_exit(failures);
}

void __tlibc_start(void) { run_tests(); }
