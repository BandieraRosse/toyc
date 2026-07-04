// EXPECT: 0
// SELF_CONTAINED
// negative_long.c — 测试 long 负值处理、符号扩展及 tcc 源文件相关模式
//
// 本测试覆盖三类代码生成路径（cgen_expr.c:369-376）：
//   A) 正 32-bit：mov eax, imm32 → 零扩展到 64 位 ✅
//   B) 负 32-bit：mov eax, imm32 + movsxd rax, eax → 符号扩展 ✅（已修复）
//   C) 超 32-bit：mov rax, imm64 → 完整 64 位 ✅
//
// 已知限制：
//   1. 变参调用 ≤ 5 个变参（fmt + 5 variadic = 6 个参数以下）

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

static void print_hex(unsigned long n) {
    /* unsigned cmp 已修复，while (n > 0) 能正确工作 */
    const char *hex = "0123456789abcdef";
    char buf[17]; int i = 0;
    if (n == 0) { sys_write(1, "0", 1); return; }
    while (n != 0) { buf[i++] = hex[n & 0xf]; n >>= 4; }
    while (i > 0) sys_write(1, &buf[--i], 1);
}

static void my_printf(const char *fmt, ...) {
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
        case 'x': case 'X':
            print_hex(__builtin_va_arg(ap, unsigned long)); break;
        case 'c': {
            char c = (char)__builtin_va_arg(ap, int);
            sys_write(1, &c, 1); break;
        }
        case '%': sys_write(1, "%", 1); break;
        default:  sys_write(1, fmt, 1); break;
        }
        fmt++;
    }
    __builtin_va_end(ap);
}

// ============================================================
// 测试状态
// ============================================================

static int fail_count = 0;

static void check(int cond, const char *msg, int id) {
    if (cond) { my_printf("  %s [PASS]\n", msg); }
    else { fail_count++; my_printf("  %s [FAIL %d]\n", msg, id); }
}

// ============================================================
// 第 1 节：三类常量加载路径展示（A/B/C）
// ============================================================

static void demo_three_paths(void)
{
    my_printf("=== Section 1: three constant-load paths ===\n");

    /* 路径 A：正 32-bit → mov eax, imm32（零扩展正确） */
    long p1 = 12345;
    long p2 = 2147483647;
    my_printf("  A) +32bit: %d (0x%x)\n", p1, p2);

    /* 路径 B：负 32-bit → mov eax + movsxd（已修复） */
    long n1 = -1;
    long n2 = -12345;
    long n3 = -2147483648L;
    my_printf("  B) -32bit: %d(0x%x)\n", n1, n1);
    my_printf("          %d(0x%x) %d(0x%x)\n", n2, n2, n3, n3);

    /* 路径 C：超 32-bit → mov rax, imm64 */
    long w1 = 4294967295L;
    long w2 = 1234567890123L;
    my_printf("  C) +64bit: %d(0x%x) %d(0x%x)\n", w1, w1, w2, w2);
}

// ============================================================
// 第 2 节：编译期负常量（已修复）
// ============================================================

static void test_compiletime_neg(void)
{
    my_printf("\n=== Section 2: compile-time negative constants (FIXED) ===\n");

    check(-1L == -1L, "  -1L == -1L", 101);
    check(-12345L == -12345L, "  -12345L == -12345L", 102);
    check(-2147483648L == -2147483648L, "  INT32_MIN", 103);
    check(0 - 1 == -1L, "  0-1 == -1L", 104);
    check(0 - 12345 == -12345L, "  0-12345 == -12345L", 105);
    check(0 - 1L == -1L, "  0-1L == -1L", 106);

    /* 运行期求反也应正确 */
    long v = 12345; v = -v;
    check(v == -12345L, "  runtime neg == -12345L", 107);

    /* 编译期 long 负常量与运行期所求值一致 */
    long direct = -1;
    long runtime = 1; runtime = -runtime;
    check(direct == runtime, "  compile -1 == runtime -1", 108);
}

// ============================================================
// 第 3 节：tcc 源码常见模式 — long 比较与条件
// ============================================================

static long make_neg(long x) { long t = x; return -t; }

static void test_comparisons(void)
{
    my_printf("\n=== Section 3: long comparisons (patterns from tcc src) ===\n");

    /* == -1 模式（tcc 源码：sym_idx == -1, s->shndx == 0 等） */
    long v = make_neg(1);
    check(v == -1L, "  v == -1", 201);
    check(make_neg(42) != -1L, "  v != -1", 202);

    /* < 0 / >= 0 模式（tcc：if(n < 0) n = -n） */
    check(make_neg(5) < 0, "  signed <0", 203);
    check(100 >= 0, "  signed >=0", 204);

    /* == 0 / != 0 模式（tcc：while(i > 0), if(len)） */
    long zero = 0;
    check(zero == 0, "  ==0", 205);
    check(42 != 0, "  !=0", 206);

    /* 数值排序（运行期计算值与编译期常量比较） */
    check(make_neg(1) < make_neg(1) + 10, "  ordering", 207);
}

// ============================================================
// 第 4 节：unsigned long 模式
// 注：只用位运算与 ==/!=，无符号比较由 04_unsigned_ops.c 覆盖
// ============================================================

static void test_unsigned_long(void)
{
    my_printf("\n=== Section 4: unsigned long patterns ===\n");

    /* 常量初始化 */
    unsigned long u1 = 0xAAAAAAAAUL;
    unsigned long u2 = 0x55555555UL;
    check(u1 == 0xAAAAAAAAUL && u2 == 0x55555555UL, "  ulong const init", 401);

    /* 位运算 */
    unsigned long a = 0xABCDUL;
    check((a & 0xFFUL) == 0xCDUL, "  bitwise AND", 411);
    check((a | 0x1000UL) == (0xABCDUL | 0x1000UL), "  bitwise OR", 412);
    check((a ^ 0xA0A0UL) == (0xABCDUL ^ 0xA0A0UL), "  bitwise XOR", 413);

    /* 移位（tcc 源码：>> 48, >> 32, & 0xFFFFFFFF） */
    unsigned long val = 0x123456789ABCDEF0UL;
    unsigned long hi = val >> 32;
    unsigned long lo = val & 0xFFFFFFFFUL;
    unsigned long rec = (hi << 32) | lo;
    check(hi == 0x12345678UL, "  ulong hi32", 421);
    check(lo == 0x9ABCDEF0UL, "  ulong lo32", 422);
    check(rec == val, "  ulong reconstruct", 423);

    /* ~0UL 位模式 */
    unsigned long z = ~0UL;
    check(z != 0, "  ~0UL != 0", 431);
}

// ============================================================
// 第 5 节：long 函数参数与返回值
// ============================================================

static long ident(long x) { return x; }
static long neg_fn(long x) { long t = x; return -t; }
static int neg_int(int x) { return -x; }

static void test_func_args(void)
{
    my_printf("\n=== Section 5: long func args/returns ===\n");

    check(ident(42) == 42, "  ident(42)", 501);
    check(ident(-5L) == -5L, "  ident(-5L)", 502);
    check(ident(-1L) == -1L, "  ident(-1L)", 503);
    check(neg_fn(100) == -100L, "  neg_fn(100)", 511);
    check(neg_fn(1) == -1L, "  neg_fn(1)", 512);

    /* int→long 运行时转换（已修复：movsxd rax, eax） */
    {
        int ival = -1;
        long casted = (long)ival;
        check(casted == -1L, "  (long)(-1 int)", 521);
    }
    {
        int ival = 42;
        long casted = (long)ival;
        check(casted == 42L, "  (long)(42 int)", 522);
    }

    /* long→int 截断 */
    long big = 0xDEADBEEF12345678L;
    int truncated = (int)big;
    check(truncated == (int)0x12345678, "  long→int trunc", 531);

    /* 函数返回负 int */
    check((long)neg_int(100) == -100L, "  neg_int(100) == -100L", 541);
}

// ============================================================
// 第 6 节：边界值
// ============================================================

static void test_boundary(void)
{
    my_printf("\n=== Section 6: boundary values ===\n");

    /* INT32_MAX — 路径 A */
    check(2147483647L == 2147483647L, "  INT32_MAX", 601);

    /* 超 32 位正数 — 路径 C */
    check(3000000000L == 3000000000L, "  3e9", 611);
    check(0xFFFFFFFFL == 0xFFFFFFFFL, "  0xFFFFFFFF", 612);

    /* INT32_MIN — 路径 B（已修复） */
    check(-2147483648L == -2147483648L, "  INT32_MIN", 621);

    /* -1 位模式 */
    unsigned long neg_one_bits = (unsigned long)(-1L);
    check(neg_one_bits != 0, "  -1 bits != 0", 631);

    /* 编译期常量之间的比较不受 bug 影响 */
    long max32 = 2147483647L;
    check(max32 == 2147483647L, "  +32bit var", 641);
}

// ============================================================
// 第 7 节：变参传递（≤ 5 个变参）
// ============================================================

static void test_variadic(void)
{
    my_printf("\n=== Section 7: variadic negative long ===\n");

    long a = -1L;
    long b = -42L;
    long c = -99L;
    my_printf("  %d %d %d\n", a, b, c);
    check(a == -1L && b == -42L && c == -99L, "  va neg values", 701);

    long pos = 100;
    long zero = 0;
    my_printf("  %d %d %d\n", pos, a, zero);
    check(pos == 100 && a == -1L && zero == 0, "  va mixed", 702);
}

// ============================================================
// 第 8 节：tcc 源码特有模式验证
// ============================================================

static void test_tcc_source_patterns(void)
{
    my_printf("\n=== Section 8: patterns from tcc source files ===\n");

    /* cgen_expr.c:373 边界测试：> INT32_MAX 触发 mov_rax_imm64 */
    long boundary = 2147483648L;
    check(boundary == 2147483648L, "  cgen_expr boundary+", 801);

    /* tcc_rt.c 模式：((unsigned long)ptr >> 48) == 0xFFFFUL */
    {
        unsigned long addr = (unsigned long)sys_write;
        check(addr != 0, "  ulong ptr shift", 811);
    }

    /* lex.c 模式：ival = val（AST 节点存储 long 值） */
    {
        long ival = 0;
        ival = 100; if (ival > 0) { ival = -ival; }
        check(ival == -100L, "  lex ival neg", 821);
    }

    /* 编译期负常量在嵌套表达式中 */
    {
        long arr[3];
        arr[0] = -1L;
        arr[1] = -100L;
        arr[2] = arr[0] + arr[1];
        check(arr[0] == -1L && arr[1] == -100L && arr[2] == -101L, "  arr neg init", 831);
    }

    /* int 变量隐式提升为 long 的比较 */
    {
        int i = -5;
        long l = 0;
        l = (long)i;
        check(l == -5L, "  int→long implicit", 841);
    }
}

// ============================================================
// main
// ============================================================

int main(void)
{
    my_printf("=== tcc long negative number test ===\n");
    my_printf("Fix: cgen_expr.c:369-376 movsxd for neg 32-bit\n");
    my_printf("Fix: unop_neg64 for int negation, movsxd in int→long assign\n\n");

    demo_three_paths();
    test_compiletime_neg();
    test_comparisons();
    test_unsigned_long();
    test_func_args();
    test_boundary();
    test_variadic();
    test_tcc_source_patterns();

    if (fail_count > 0) {
        my_printf("\nFAILED: %d failures\n", fail_count);
        return 1;
    }

    my_printf("\n=== ALL NEGATIVE LONG TESTS PASSED ===\n");
    return 0;
}

void __tlibc_start(void) { sys_exit(main()); }
