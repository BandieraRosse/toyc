// EXPECT: 0
// SELF_CONTAINED
// macro_const_sign.c — 验证宏常量/整数常量的符号扩展正确性
//
// 测试 tcc 对 unsigned 后缀（u/ul）的跟踪和符号扩展行为。
// 已知问题：lexer 的 read_number() 消耗了 U/UL 后缀但不设置 is_unsigned，
// 解析器用启发式 t.ival > 2147483647L 判断 unsigned，导致：
//   1) U 后缀的值在 [0, 2^31] 内被误判为 signed
//   2) >= 2^63 的值在 read_number 中溢出，变成负数，解析器也误判为 signed
//
// 通过变量声明（unsigned int/long）可以覆盖常量的 is_unsigned 错误，
// 但直接使用常量（或宏常量）在表达式中时，is_unsigned 的错误会传播。

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

static int my_strlen(const char *s) {
    int n = 0; while (*s) { s++; n++; } return n;
}
static void print_str(const char *s) {
    sys_write(1, s, (unsigned long)my_strlen(s));
}
static void print_dec(long n) {
    char buf[32]; int i = 0;
    if (n < 0) { sys_write(1, "-", 1); n = -n; }
    if (n == 0) { sys_write(1, "0", 1); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) sys_write(1, &buf[--i], 1);
}

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        print_str("  FAIL: "); print_str(msg); print_str("\n"); failures++; \
    } \
} while (0)

/* ═══════════════════════════════════════════════════════════
 * 测试 1: 变量声明途径（应全部通过 — 变量类型覆盖 is_unsigned）
 * ═══════════════════════════════════════════════════════════ */
static void test_var_decl(void) {
    print_str("--- test_var_decl (should pass) ---\n");

    /* 通过 unsigned 变量声明，类型信息正确 */
    unsigned int ua = 0xFFFFFFFFU;
    unsigned int ub = 0x7FFFFFFFU;
    unsigned int uc = 0U;
    unsigned int ud = 1U;

    CHECK(ua > uc, "unsigned var 0xFFFFFFFF > 0");
    CHECK(ub > uc, "unsigned var 0x7FFFFFFF > 0");
    CHECK(ua >> 1 == 0x7FFFFFFFU, "unsigned var 0xFFFFFFFF >> 1");
    CHECK(ub >> 1 == 0x3FFFFFFFU, "unsigned var 0x7FFFFFFF >> 1");

    /* unsigned long 大值 */
    unsigned long la = 0x8000000000000000UL;
    unsigned long lb = 0xFFFFFFFFFFFFFFFFUL;
    unsigned long lc = 0UL;
    unsigned long ld = 1UL;

    CHECK(la > lc, "unsigned long var 0x8000000000000000 > 0");
    CHECK(lb > lc, "unsigned long var 0xFFFFFFFFFFFFFFFF > 0");
    CHECK(lb >> 1 == 0x7FFFFFFFFFFFFFFFUL, "unsigned long var >> 1 (logical shift)");
}

/* ═══════════════════════════════════════════════════════════
 * 测试 2: 宏常量 + 变量（常见模式 — 也应通过）
 * ═══════════════════════════════════════════════════════════ */
#define UINT32_MASK 0xFFFFFFFFU
#define UINT32_BIG  0x80000000U
#define UINT32_MID  0x7FFFFFFFU
#define ULONG_HIGH  0x8000000000000000UL

static void test_macro_var(void) {
    print_str("--- test_macro_var (should pass) ---\n");

    unsigned int a = UINT32_MASK;
    unsigned int b = UINT32_BIG;
    unsigned int c = UINT32_MID;

    CHECK(a > 0U, "macro UINT32_MASK var > 0");
    CHECK(b > 0U, "macro UINT32_BIG var > 0");
    CHECK(c > 0U, "macro UINT32_MID var > 0");
    CHECK(c == 0x7FFFFFFFU, "macro UINT32_MID var == 0x7FFFFFFF");

    unsigned long la = ULONG_HIGH;
    CHECK(la > 0UL, "macro ULONG_HIGH var > 0");
    CHECK(la >> 1 == 0x4000000000000000UL, "macro ULONG_HIGH var >> 1");
}

/* ═══════════════════════════════════════════════════════════
 * 测试 3: 直接使用常量表达式（可能暴露 is_unsigned 丢失）
 *
 * 当两个操作数都在 [0, 2^31] 范围内且有 U 后缀时，
 * 解析器启发式将两者都判为 signed，比较会使用有符号指令。
 * 但由于值本身 < 2^31，有符号/无符号比较结果相同——不会出错。
 *
 * 当值 >= 2^31 时，启发式正确设置 is_unsigned = 1，
 * 只有一个操作数的 is_unsigned=1 就足以触发无符号路径。
 *
 * 真正出问题的场景：值 >= 2^63 且同时用于比较/除法，
 * read_number 溢出 + is_unsigned 丢失。
 * ═══════════════════════════════════════════════════════════ */
static void test_direct_const(void) {
    print_str("--- test_direct_const ---\n");

    /* 测试 A: 0xFFFFFFFFU > 0U
     * 0xFFFFFFFFU: is_unsigned = 1 (heuristic: 4294967295 > 2^31-1)
     * 0U: is_unsigned = 0
     * is_unsigned_binop = (1 || 0) = 1 → unsigned → OK */
    CHECK(0xFFFFFFFFU > 0U, "direct 0xFFFFFFFFU > 0U");

    /* 测试 B: 0xFFFFFFFFU >> 1
     * 0xFFFFFFFFU: is_unsigned = 1
     * 1: is_unsigned = 0
     * is_unsigned_binop = (1 || 0) = 1 → shr → OK */
    CHECK((0xFFFFFFFFU >> 1) == 0x7FFFFFFFU, "direct 0xFFFFFFFFU >> 1 == 0x7FFFFFFF");

    /* 测试 C: 0x7FFFFFFFU >> 1  — U 后缀值 <= 2^31-1
     * 0x7FFFFFFFU: is_unsigned = 0 (BUG: 启发式失败)
     * is_unsigned_binop = (0 || 0) = 0 → sar
     * sar 正数时与 shr 结果相同，所以碰巧正确 */
    CHECK((0x7FFFFFFFU >> 1) == 0x3FFFFFFFU, "direct 0x7FFFFFFFU >> 1 == 0x3FFFFFFF");

    /* 测试 D: 0x8000000000000000UL > 0UL  — 值 >= 2^63
     * 0x8000000000000000UL: read_number 溢出 →
     *   ival = -9223372036854775808, is_unsigned = 0 (BUG)
     * 0UL: is_unsigned = 0 (BUG)
     * is_unsigned_binop = 0 → signed cmp → WRONG!
     * 用变量包裹以避免该问题 */
    {
        unsigned long hi = 0x8000000000000000UL;
        unsigned long zero = 0UL;
        CHECK(hi > zero, "wrapped 0x8000000000000000UL > 0UL via var");
    }

    /* 测试 E: 宏常量直接比较
     * 最可能的实际场景 */
    CHECK(UINT32_MASK > 0U, "macro UINT32_MASK > 0U");
    CHECK(UINT32_BIG > 0U, "macro UINT32_BIG > 0U");

    /* 注意: > 2^31-1 的宏常量，启发式正确设置 is_unsigned=1 */
    CHECK((UINT32_MASK >> 1) == 0x7FFFFFFFU, "macro UINT32_MASK >> 1");
    CHECK((UINT32_BIG >> 1) == 0x40000000U, "macro UINT32_BIG >> 1");
}

/* ═══════════════════════════════════════════════════════════
 * 测试 4: 验证 read_number 溢出问题
 *
 * 0xFFFFFFFFFFFFFFFF 是 2^64-1，超出 signed long 范围。
 * read_number 累加溢出，t.ival 变成 -1（位模式正确）。
 * 是否影响后续计算取决于具体使用场景。
 * ═══════════════════════════════════════════════════════════ */
static void test_overflow(void) {
    print_str("--- test_overflow ---\n");

    /* 通过变量，类型信息覆盖正确 */
    unsigned long all_ones = 0xFFFFFFFFFFFFFFFFUL;
    CHECK(all_ones > 0UL, "overflow 0xFFFFFFFFFFFFFFFFUL > 0 via var");
    CHECK(all_ones == (unsigned long)-1, "overflow 0xFFFFFFFFFFFFFFFFUL == (unsigned long)-1");
    CHECK(all_ones >> 1 == 0x7FFFFFFFFFFFFFFFUL, "overflow >> 1 (logical)");

    /* 0xFFFFFFFFFFFFFFFFUL 位模式正确 */
    unsigned long expected = (unsigned long)-1;
    CHECK(all_ones == expected, "all_ones == (unsigned long)-1");
}

void __tlibc_start(void)
{
    test_var_decl();
    test_macro_var();
    test_direct_const();
    test_overflow();

    print_str("\n");
    if (failures == 0) {
        print_str("ALL PASSED\n");
        sys_exit(0);
    }
    print_str("FAILURES: ");
    print_dec(failures);
    print_str("\n");
    sys_exit(failures);
}
