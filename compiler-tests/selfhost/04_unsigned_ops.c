// EXPECT: 0
// SELF_CONTAINED
// unsigned_ops.c — 测试 unsigned 类型的比较、除法、右移位行为
//
// tcc 类型系统缺陷：不区分 signed/unsigned。
// 本测试暴露三类错误：
//   1) unsigned 比较（< > <= >=）使用了有符号比较指令
//   2) unsigned 除法使用了有符号 idiv + cdq
//   3) unsigned char/short 解引用使用了符号扩展 movsbl/movswl
//
// 编译：build/tcc  04_unsigned_ops.c -o /tmp/04_unsigned_ops.o
// 链接：ld -nostdlib -static -T ld.script /tmp/04_unsigned_ops.o -o /tmp/04_unsigned_ops
// 运行：/tmp/04_unsigned_ops

// ============================================================
// syscall 包装（约束驱动，不依赖外部 .o）
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
// 简单输出函数（自包含，无外部引用）
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

// ─── 测试 1: unsigned int 比较 ───
// tcc 当前对有符号/无符号一视同仁，对 0x80000000u > 0
// 使用 setg（有符号大于）而非 seta（无符号大于）。
// 0x80000000 在 signed int 中是 -2147483648，所以
// setg 判定 -2147483648 > 0 为假 → 错误！
static void test_unsigned_cmp(void) {
    unsigned int a = 0x80000000u;  // 2147483648（signed: -2147483648）
    unsigned int b = 0xFFFFFFFFu;  // 4294967295（signed: -1）
    unsigned int c = 1u;
    unsigned int d = 0u;

    // 高位设 1 的无符号数大于 0
    check(a > d, "unsigned 0x80000000 > 0");
    check(b > d, "unsigned 0xFFFFFFFF > 0");

    // 无符号 0x80000000 不小于 1
    check(!(a < c), "unsigned 0x80000000 < 1 应为假");

    // 无符号 0xFFFFFFFF 不小于 1
    check(!(b < c), "unsigned 0xFFFFFFFF < 1 应为假");

    // 边界值：相等（这组应通过）
    check(a >= a, "unsigned 0x80000000 >= 自身");
    check(d <= d, "unsigned 0 <= 0");

    // 混合正常值
    check(c > d, "unsigned 1 > 0");
    check(c < a, "unsigned 1 < 0x80000000");

    // 与自身比较（== 和 != 不受有符号/无符号影响）
    check(b == b, "unsigned 0xFFFFFFFF == 自身");
    check(a != c, "unsigned 0x80000000 != 1");
}

// ─── 测试 2: unsigned long 比较 ───
static void test_unsigned_long_cmp(void) {
    unsigned long a = 0x8000000000000000ul;
    unsigned long b = 1ul;
    unsigned long d = 0ul;

    check(a > d, "unsigned long 0x8000000000000000 > 0");
    check(!(a < b), "unsigned long 0x8000000000000000 < 1 应为假");
    check(b > d, "unsigned long 1 > 0");
    check(b < a, "unsigned long 1 < 0x8000000000000000");
}

// ─── 测试 3: unsigned int 除法 ───
// tcc 使用 idiv（有符号除法）+ cdq（符号扩展）而非
// div（无符号）+ xor edx,edx（零扩展），因此高位设 1 的值
// 在除法中会先被符号扩展为负值，导致错误结果。
static void test_unsigned_div(void) {
    unsigned int x = 0x80000000u;
    unsigned int y = 2u;

    check(x / y == 0x40000000u, "unsigned 0x80000000 / 2 应为 0x40000000");

    unsigned int m = 0x80000003u;
    check(m % y == 1u, "unsigned 0x80000003 %% 2 应为 1");

    // 边界：无符号最大值
    unsigned int big = 0xFFFFFFFFu;
    check(big / 0xFFFFFFFFu == 1u, "unsigned 0xFFFFFFFF / 自身 应为 1");
    check(big % 0xFFFFFFFFu == 0u, "unsigned 0xFFFFFFFF %% 自身 应为 0");
}

// ─── 测试 4: unsigned char 解引用 ───
// tcc 对 unsigned char* 的 *p 使用 movsbl（符号扩展字节加载），
// 这会将 0x80 扩展为 0xFFFFFF80（-128），而非 0x00000080（128）。
// 正确行为是使用 movzbl（零扩展字节加载）。
static void test_unsigned_deref(void) {
    unsigned char buf[4];
    buf[0] = 0x80;
    buf[1] = 0xFF;

    // 解引用 unsigned char 应零扩展到 int
    unsigned char *p = buf;
    check(*p == 0x80u, "unsigned char 解引用 0x80 应为 0x80");
    check(p[1] == 0xFFu, "unsigned char 解引用 0xFF 应为 255");

    // unsigned short 解引用
    unsigned short arr[2];
    arr[0] = 0x8000u;
    arr[1] = 0xFFFFu;

    unsigned short *sp = arr;
    check(sp[0] == 0x8000u, "unsigned short 解引用 0x8000 应为 0x8000");
    check(sp[1] == 0xFFFFu, "unsigned short 解引用 0xFFFF 应为 0xFFFF");
}

// ─── 测试 5: unsigned long 除法 ───
static void test_unsigned_long_div(void) {
    unsigned long x = 0x8000000000000000ul;
    unsigned long y = 2ul;

    check(x / y == 0x4000000000000000ul,
           "unsigned long 0x8000000000000000 / 2 应为 0x4000000000000000");

    unsigned long m = 0x8000000000000005ul;
    check(m % y == 1ul,
           "unsigned long 0x8000000000000005 %% 2 应为 1");
}

// ─── 测试 6: unsigned 右移位 ───
// unsigned >> 应使用 shr（逻辑右移），将高位补 0。
// 如果 tcc 因为修复 signed 移位而把 unsigned 也改成 sar，
// 最高位为 1 的值移位后会得到错误结果。
static void test_unsigned_rshift(void) {
    unsigned int x = 0x80000000u;
    check((x >> 1) == 0x40000000u,
           "unsigned 0x80000000 >> 1 应为 0x40000000（逻辑移位）");

    unsigned int all_ones = 0xFFFFFFFFu;
    check((all_ones >> 4) == 0x0FFFFFFFu,
           "unsigned 0xFFFFFFFF >> 4 应为 0x0FFFFFFF");

    unsigned long y = 0x8000000000000000ul;
    check((y >> 1) == 0x4000000000000000ul,
           "unsigned long 0x8000000000000000 >> 1 应为 0x4000000000000000");

    // 小值：0 移位
    unsigned int zero = 0u;
    check((zero >> 1) == 0u,
           "unsigned 0 >> 1 应为 0");
}

// ============================================================
// main + entry point (selfhost tests need explicit __tlibc_start)
// ============================================================

static void run_tests(void) {
    print_str("=== unsigned operations tests ===\n");

    print_str("--- test_unsigned_cmp ---\n");
    test_unsigned_cmp();

    print_str("--- test_unsigned_long_cmp ---\n");
    test_unsigned_long_cmp();

    print_str("--- test_unsigned_div ---\n");
    test_unsigned_div();

    print_str("--- test_unsigned_long_div ---\n");
    test_unsigned_long_div();

    print_str("--- test_unsigned_deref ---\n");
    test_unsigned_deref();

    print_str("--- test_unsigned_rshift ---\n");
    test_unsigned_rshift();

    print_str("\n");

    if (failures == 0) {
        print_str("ALL PASSED\n");
        sys_exit(0);
    }

    // Exit with failure count (avoid returning from main to work
    // around a tcc codegen issue with function epilogue)
    sys_exit(failures);
}

void __tlibc_start(void) { run_tests(); }
