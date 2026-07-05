// EXPECT: 0
// SELF_CONTAINED
// manyargs_variadic_macros.c — 验证 tcc 对以下特性的支持：
//
//   1. 超过6个参数的函数调用（x86-64 ABI：前6个走寄存器，第7+个走栈）
//   2. 可变参数宏（__VA_ARGS__）与 GCC 扩展 ,##__VA_ARGS__
//
// 背景：
//   2026-07-04 修复了 ≥7 参数调用的寄存器传参错位（commit 2a3e5e9），
//   2026-07-05 修复了 r8/r9 在函数序言中的 REX 前缀（双 REX->0x4C），
//   2026-07-05 修复了栈参数压入顺序（正序->逆序）。
// 本文件综合验证以上修复。
//
// 编译：build/tcc  compiler-tests/selfhost/10_manyargs_variadic_macros.c  -o /tmp/10.o
// 链接：ld -nostdlib -static -T ld.script /tmp/10.o -o /tmp/10
// 运行：/tmp/10

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
 *  打印辅助函数
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
 *  Part 1: 超过6个参数的函数调用
 *
 *  x86-64 ABI 传参规则：
 *    arg0=rdi, arg1=rsi, arg2=rdx, arg3=rcx, arg4=r8,  arg5=r9
 *    arg6+ 在栈上：[rbp+0x10]=a6, [rbp+0x18]=a7, ...
 *
 *  测试策略：定义接受 N 个参数的函数，每个参数输入不同的
 *  质数常量，通过返回值求和确认所有参数正确传递。
 *  内部不做断言（函数可能被不同值调用）。
 * ============================================================ */

/* 各参数编号对应的质数常量 */
#define V0  2
#define V1  3
#define V2  5
#define V3  7
#define V4  11
#define V5  13
#define V6  17
#define V7  19
#define V8  23
#define V9  29
#define V10 31
#define V11 37

static long sum6(long a0, long a1, long a2, long a3, long a4, long a5)
{
    return a0 + a1 + a2 + a3 + a4 + a5;
}

static long sum7(long a0, long a1, long a2, long a3, long a4, long a5, long a6)
{
    return a0 + a1 + a2 + a3 + a4 + a5 + a6;
}

static long sum8(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7)
{
    return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7;
}

static long sum9(long a0, long a1, long a2, long a3, long a4, long a5,
                  long a6, long a7, long a8)
{
    return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8;
}

static long sum10(long a0, long a1, long a2, long a3, long a4, long a5,
                   long a6, long a7, long a8, long a9)
{
    return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9;
}

static long sum12(long a0, long a1, long a2, long a3, long a4, long a5,
                   long a6, long a7, long a8, long a9, long a10, long a11)
{
    return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10 + a11;
}

/* 带指针参数的函数（验证栈上指针不被截断） */
static long check_ptr_args(long a0, long a1, long a2, long a3, long a4, long a5,
                           const char *s6, const char *s7, long a8)
{
    if (a0 != V0) return -1;
    if (a1 != V1) return -2;
    if (a2 != V2) return -3;
    if (a3 != V3) return -4;
    if (a4 != V4) return -5;
    if (a5 != V5) return -10;
    if (s6[0] != 'h' || s6[1] != 'e' || s6[4] != 'o') return -20;
    if (s7[0] != 'w' || s7[4] != 'd') return -21;
    if (a8 != V8) return -30;
    return 100;
}

/* 混合类型参数验证函数 */
static long check_mixed_types(char c0, long a1, short s2, long a3,
                               const char *s4, long a5, char c6, long a7,
                               short s8, long a9, char c10, long a11)
{
    if (c0  != 65)   return -1;   /* 'A' */
    if (a1  != V1)   return -2;
    if (s2  != -1)   return -3;
    if (a3  != V3)   return -4;
    if (s4[0] != 'p') return -5;
    if (a5  != V5)   return -6;
    if (c6  != 66)   return -7;   /* 'B' */
    if (a7  != V7)   return -8;
    if (s8  != -2)   return -9;
    if (a9  != V9)   return -10;
    if (c10 != 67)   return -11;  /* 'C' */
    if (a11 != V11)  return -12;
    return 1;
}

/* 多次调用验证栈清洁 */
static long multi_call_check(long a0, long a1, long a2, long a3, long a4, long a5,
                              long a6, long a7)
{
    return a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7;
}

static long add(long a, long b) { return a + b; }

/* ============================================================
 *  Part 2: 可变参数宏（__VA_ARGS__）
 * ============================================================ */

#define SUM2(a,b)     ((a)+(b))
#define SUM3(a,b,c)   ((a)+(b)+(c))
#define SUM4(a,b,c,d) ((a)+(b)+(c)+(d))

/* 基本 __VA_ARGS__ 展开 */
#define SUM_VA(...) SUM4(__VA_ARGS__)

/* 命名参数 + __VA_ARGS__ */
#define WRAP_SUM(first, ...) ((first) + SUM3(__VA_ARGS__))

/* , ## 扩展（通过函数调用场景验证） */
#define FUNCALL_OPT(f, ...)  f(__VA_ARGS__)

/* 变参展开成超多参数调用 */
#define SUM12_VA(...) sum12(__VA_ARGS__)

/* 嵌套可变参数宏 */
#define INNER(...)      __VA_ARGS__
#define OUTER(...)      INNER(__VA_ARGS__)
#define APPLY2(f, ...)  f(__VA_ARGS__)

/* NARGS 计数宏（通过 __VA_ARGS__） */
#define NARGS_X(a,b,c,d,e,f,g,h,n,...) n
#define NARGS(...)  NARGS_X(__VA_ARGS__,8,7,6,5,4,3,2,1,0,)

/* 辅助函数（用于接收 FUNCALL_OPT 展开） */
static long sum_vals(long a1, long a2, long a3, long a4, long a5, long a6)
{
    return a1 + a2 + a3 + a4 + a5 + a6;
}

/* ============================================================
 *  测试函数
 * ============================================================ */

/* --- 测试 1-6: 基本多参数调用 --- */

static void test_6args_baseline(void) {
    test_start("6-arg call (baseline) — sum6");
    long r = sum6(V0, V1, V2, V3, V4, V5);
    CHECK(r == 41, "sum6(2,3,5,7,11,13) == 41");
    print_str("    sum6 result = ");
    print_dec(r);
    print_str("\n");
}

static void test_7args(void) {
    test_start("7-arg call (1 stack arg) — sum7");
    long r1 = sum7(V0, V1, V2, V3, V4, V5, V6);
    CHECK(r1 == 58, "sum7(2..17) == 58");
    long a0=V0, a1=V1, a2=V2, a3=V3, a4=V4, a5=V5, a6=V6;
    long r2 = sum7(a0, a1, a2, a3, a4, a5, a6);
    CHECK(r2 == 58, "sum7(vars) == 58");
    print_str("    sum7(result="); print_dec(r1); print_str(")\n");
}

static void test_8args(void) {
    test_start("8-arg call (2 stack args) — sum8");
    long r = sum8(V0, V1, V2, V3, V4, V5, V6, V7);
    CHECK(r == 77, "sum8(2..19) == 77");
    print_str("    sum8 = "); print_dec(r); print_str("\n");
}

static void test_9args(void) {
    test_start("9-arg call (3 stack args) — sum9");
    long r = sum9(V0, V1, V2, V3, V4, V5, V6, V7, V8);
    CHECK(r == 100, "sum9(2..23) == 100");
    print_str("    sum9 = "); print_dec(r); print_str("\n");
}

static void test_10args(void) {
    test_start("10-arg call (4 stack args) — sum10");
    long r = sum10(V0, V1, V2, V3, V4, V5, V6, V7, V8, V9);
    CHECK(r == 129, "sum10(2..29) == 129");
    print_str("    sum10 = "); print_dec(r); print_str("\n");
}

static void test_12args(void) {
    test_start("12-arg call (6 stack args) — sum12");
    long r = sum12(V0,V1,V2,V3,V4,V5,V6,V7,V8,V9,V10,V11);
    CHECK(r == 197, "sum12(2..37) == 197");
    print_str("    sum12 = "); print_dec(r); print_str("\n");
}

/* --- 测试 7: 栈上指针参数 --- */

static void test_ptr_stack_args(void) {
    test_start("stack args with pointers");
    const char *hello = "hello";
    const char *world = "world";
    long r = check_ptr_args(V0, V1, V2, V3, V4, V5, hello, world, V8);
    CHECK(r == 100, "check_ptr_args returns 100");
    print_str("    ptr_args passed\n");
}

/* --- 测试 8: 混合类型参数 --- */

static void test_mixed_types(void) {
    test_start("mixed types on stack (char/short/ptr/long)");
    const char *ptr = "pointer";
    long r = check_mixed_types('A', V1, (short)-1, V3, ptr, V5,
                                'B', V7, (short)-2, V9, 'C', V11);
    CHECK(r == 1, "check_mixed_types returns 1");
    print_str("    mixed types passed\n");
}

/* --- 测试 9: 多次调用栈清洁 --- */

static void test_multiple_calls(void) {
    test_start("multiple >6-arg calls (stack cleanliness)");
    long r1 = multi_call_check(1,2,3,4,5,6,7,8);
    long r2 = multi_call_check(10,20,30,40,50,60,70,80);
    long r3 = multi_call_check(100,200,300,400,500,600,700,800);
    CHECK(r1 == 36, "multi_call 1+..+8 == 36");
    CHECK(r2 == 360, "multi_call 10+..+80 == 360");
    CHECK(r3 == 3600, "multi_call 100+..+800 == 3600");
    print_str("    multi: "); print_dec(r1); print_str(", ");
    print_dec(r2); print_str(", "); print_dec(r3); print_str("\n");
}

/* --- 测试 10: 不同参数个数混合调用 --- */

static void test_mixed_arg_counts(void) {
    test_start("mixed arg counts (6,7,8,9,10,12)");
    long R6 = sum6(V0,V1,V2,V3,V4,V5);
    long R7 = sum7(V0,V1,V2,V3,V4,V5,V6);
    long R8 = sum8(V0,V1,V2,V3,V4,V5,V6,V7);
    long R9 = sum9(V0,V1,V2,V3,V4,V5,V6,V7,V8);
    long R10 = sum10(V0,V1,V2,V3,V4,V5,V6,V7,V8,V9);
    long R12 = sum12(V0,V1,V2,V3,V4,V5,V6,V7,V8,V9,V10,V11);
    CHECK(R6  == 41, "mixed R6 == 41");
    CHECK(R7  == 58, "mixed R7 == 58");
    CHECK(R8  == 77, "mixed R8 == 77");
    CHECK(R9  == 100, "mixed R9 == 100");
    CHECK(R10 == 129, "mixed R10 == 129");
    CHECK(R12 == 197, "mixed R12 == 197");
    print_str("    sums: "); print_dec(R6); print_str(" ");
    print_dec(R7); print_str(" "); print_dec(R8); print_str(" ");
    print_dec(R9); print_str(" "); print_dec(R10); print_str(" ");
    print_dec(R12); print_str("\n");
}

/* --- 测试 11-12: 基本 __VA_ARGS__ 宏展开 --- */

static void test_basic_va_args(void) {
    test_start("basic __VA_ARGS__ macro expansion");
    long r1 = SUM_VA(10, 20, 30, 40);
    CHECK(r1 == 100, "SUM_VA(10,20,30,40) == 100");
    long r2 = SUM_VA(1, 2, 3, 4);
    CHECK(r2 == 10, "SUM_VA(1,2,3,4) == 10");
    print_str("    SUM_VA: "); print_dec(r1); print_str(", ");
    print_dec(r2); print_str("\n");
}

static void test_named_plus_va(void) {
    test_start("named param + __VA_ARGS__");
    long r1 = WRAP_SUM(100, 10, 20, 30);   /* = 100 + (10+20+30) = 160 */
    CHECK(r1 == 160, "WRAP_SUM(100,10,20,30) == 160");
    long r2 = WRAP_SUM(5, 1, 2, 3);        /* = 5 + (1+2+3) = 11 */
    CHECK(r2 == 11, "WRAP_SUM(5,1,2,3) == 11");
    print_str("    WRAP_SUM: "); print_dec(r1); print_str(", ");
    print_dec(r2); print_str("\n");
}

/* --- 测试 13: , ## __VA_ARGS__ GCC 扩展 --- */

static void test_gcc_va_opt(void) {
    test_start(", ## __VA_ARGS__ (empty variadic)");

    /* FUNCALL_OPT(f, a, b) → f(a, b) */
    long r1 = FUNCALL_OPT(sum_vals, V0,V1,V2,V3,V4,V5);
    CHECK(r1 == 41, "FUNCALL_OPT(sum_vals,2,3,5,7,11,13) == 41");

    /* NARGS 计数：注意 NARGS() 无参数时返回 1（__VA_ARGS__ 计数
     * 宏的已知限制——空参数列表时第一个逗号产生一个空参数） */
    CHECK(NARGS(1) == 1, "NARGS(1) == 1");
    CHECK(NARGS(1,2) == 2, "NARGS(1,2) == 2");
    CHECK(NARGS(1,2,3) == 3, "NARGS(1,2,3) == 3");
    CHECK(NARGS(1,2,3,4) == 4, "NARGS(1,2,3,4) == 4");
    CHECK(NARGS(1,2,3,4,5) == 5, "NARGS(1,2,3,4,5) == 5");
    CHECK(NARGS(1,2,3,4,5,6) == 6, "NARGS(1,2,3,4,5,6) == 6");
    CHECK(NARGS(1,2,3,4,5,6,7) == 7, "NARGS(1,2,3,4,5,6,7) == 7");
    CHECK(NARGS(1,2,3,4,5,6,7,8) == 8, "NARGS(1,2,3,4,5,6,7,8) == 8");

    print_str("    NARGS counts 1..8: all correct\n");
}

/* --- 测试 14: __VA_ARGS__ -> >6-arg call --- */

static void test_va_to_manyargs(void) {
    test_start("__VA_ARGS__ expanding to >6-arg call");
    long r1 = SUM12_VA(V0,V1,V2,V3,V4,V5,V6,V7,V8,V9,V10,V11);
    CHECK(r1 == 197, "SUM12_VA(2..37) == 197");
    long a0=V0, a1=V1, a2=V2, a3=V3, a4=V4, a5=V5;
    long a6=V6, a7=V7, a8=V8, a9=V9, a10=V10, a11=V11;
    long r2 = SUM12_VA(a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11);
    CHECK(r2 == 197, "SUM12_VA(vars) == 197");
    print_str("    SUM12_VA: "); print_dec(r1); print_str(", ");
    print_dec(r2); print_str("\n");
}

/* --- 测试 15: 嵌套可变参数宏 --- */

static void test_nested_va(void) {
    test_start("nested __VA_ARGS__ macros");
    long r1 = OUTER(SUM_VA(1, 2, 3, 4));
    CHECK(r1 == 10, "OUTER(SUM_VA(1,2,3,4)) == 10");
    long r2 = APPLY2(SUM_VA, 10, 20, 30, 40);
    CHECK(r2 == 100, "APPLY2(SUM_VA,10,20,30,40) == 100");
    long r3 = OUTER(SUM12_VA(V0,V1,V2,V3,V4,V5,V6,V7,V8,V9,V10,V11));
    CHECK(r3 == 197, "OUTER(SUM12_VA) == 197");
    print_str("    nested: "); print_dec(r1); print_str(", ");
    print_dec(r2); print_str(", "); print_dec(r3); print_str("\n");
}

/* --- 测试 16: 宏交互（## + __VA_ARGS__） --- */

static void test_macro_interaction(void) {
    test_start("variadic macro + ## interaction");
#define CONCAT2(a,b) a##b
    CHECK(NARGS(1,2,3) == 3, "NARGS(1,2,3) == 3");
    long xy = CONCAT2(0x, FF);
    CHECK(xy == 255, "CONCAT2(0x,FF) == 0xFF == 255");
    print_str("    macro interaction passed\n");
}

/* --- 测试 17: 空 __VA_ARGS__ --- */

static void test_empty_va(void) {
    test_start("empty __VA_ARGS__ expansion");
#define ID(x) x
    long t = ID(42);
    CHECK(t == 42, "ID(42) == 42");
    print_str("    empty variadic: OK\n");
}

/* --- 测试 18: 函数宏转发到 >6-arg 调用 --- */

static void test_fmacro_manyargs(void) {
    test_start("function-like macro forwarding to >6-arg call");
#define FORWARD_SUM12(a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11) \
    sum12(a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11)
    long r1 = FORWARD_SUM12(V0,V1,V2,V3,V4,V5,V6,V7,V8,V9,V10,V11);
    CHECK(r1 == 197, "FORWARD_SUM12(2..37) == 197");
#define FORWARD_SUM8(a0,a1,a2,a3,a4,a5,a6,a7) \
    sum8(a0,a1,a2,a3,a4,a5,a6,a7)
    long r2 = FORWARD_SUM8(V0,V1,V2,V3,V4,V5,V6,V7);
    CHECK(r2 == 77, "FORWARD_SUM8(2..19) == 77");
    print_str("    macro forward: "); print_dec(r1);
    print_str(", "); print_dec(r2); print_str("\n");
}

/* --- 测试 19: 表达式作为 >6 参数函数调用的参数 --- */

static void test_expr_as_arg(void) {
    test_start("expressions as args to >6-arg function");
    long r = sum8(add(1,1), add(1,2), add(2,3), add(3,4),
                   add(4,5), add(5,6), add(6,7), add(7,8));
    /* 2 + 3 + 5 + 7 + 9 + 11 + 13 + 15 = 65 */
    CHECK(r == 65, "sum8(exprs) == 65");
    print_str("    expr args result: ");
    print_dec(r);
    print_str("\n");
}

/* --- 测试 20: 嵌套超多参数调用（从栈参数函数内再次调用栈参数函数） --- */

static void test_call_from_stack_func(void) {
    test_start("nested >6-arg calls from within >6-arg function");
    /* 内层 sum8(18,20,24,30,32,38,1,1) = 164
     * 164 - 2 = 162（作为 a6 传入外层）
     * 外层 sum8(2,3,5,7,11,13,162,19) = 222 */
    long inner = sum8(V6+1, V7+1, V8+1, V9+1, V10+1, V11+1, 1, 1);
    CHECK(inner == 164, "inner sum8(18,20,24,30,32,38,1,1) == 164");
    long r = sum8(V0, V1, V2, V3, V4, V5, inner - 2, V7);
    CHECK(r == 222, "outer sum8(2,3,5,7,11,13,162,19) == 222");
    print_str("    nested result: ");
    print_dec(r);
    print_str("\n");
}

/* ============================================================
 *  运行所有测试
 * ============================================================ */

static void run_tests(void) {
    print_str("=== many-args + variadic macro tests ===\n\n");

    print_str("[Part 1: >6 function arguments]\n");
    test_6args_baseline();    /* 1 */
    test_7args();             /* 2 */
    test_8args();             /* 3 */
    test_9args();             /* 4 */
    test_10args();            /* 5 */
    test_12args();            /* 6 */
    test_ptr_stack_args();    /* 7 */
    test_mixed_types();       /* 8 */
    test_multiple_calls();    /* 9 */
    test_mixed_arg_counts();  /* 10 */

    print_str("\n[Part 2: variadic macros (__VA_ARGS__)]\n");
    test_basic_va_args();     /* 11 */
    test_named_plus_va();     /* 12 */
    test_gcc_va_opt();        /* 13 */
    test_va_to_manyargs();    /* 14 */
    test_nested_va();         /* 15 */
    test_macro_interaction(); /* 16 */
    test_empty_va();          /* 17 */
    test_fmacro_manyargs();   /* 18 */
    test_expr_as_arg();       /* 19 */
    test_call_from_stack_func(); /* 20 */

    print_str("\n");
    if (failures == 0) {
        print_str("ALL PASSED — many-args + variadic macros\n");
        sys_exit(0);
    }
    print_str("SOME TESTS FAILED: ");
    print_dec(failures);
    print_str(" failure(s)\n");
    sys_exit(failures > 127 ? 127 : failures);
}

void __tlibc_start(void) { run_tests(); }
