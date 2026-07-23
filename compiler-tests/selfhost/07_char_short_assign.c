// EXPECT: 0
// SELF_CONTAINED
// char_short_assign.c — 验证 char/short 局部变量直接赋值不覆写相邻栈变量
//
// 问题背景：
// cgen_expr.c ASSIGN 路径（direct local variable store）对 char/short
// 变量用了 32-bit store（mov [rbp+off], eax），会覆写相邻栈变量的高字节。
// 例如 char c 分配在 rbp-5，int sentinel 在 rbp-4，执行 c = 'A' 时
// 32-bit 写入 rbp-5~-2，破坏了 sentinel 在 rbp-4~-1 的 3 个字节。
//
// dfc1a5f 修复了 VAR_DECL 和 cgen_for 初始化路径中的同类问题，
// 但 ASSIGN 路径（cgen_expr.c:1362-1365）被遗漏，此处补齐。
//
// 最简复现:
//   int s = 0xAAAAAAAA;
//   char c;
//   c = 'A';   // 32-bit write corrupts s
//   if (s != 0xAAAAAAAA) crash;
//
// 编译：build/toyc  07_char_short_assign.c -o /tmp/07_char_short_assign.o
// 链接：ld -nostdlib -static -T ld.script /tmp/07_char_short_assign.o -o /tmp/07_char_short_assign
// 运行：/tmp/07_char_short_assign

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
// 测试框架
// ============================================================

static int failures = 0;
static int test_num = 0;
static int assert_num = 0;

static void test_start(const char *name) {
    test_num++;
    assert_num = 0;
    print_str("  test ");
    { char buf[4]; int i = 3; int v = test_num;
      buf[3] = 0;
      if (v >= 100) { buf[0] = '0' + v/100; v %= 100; } else buf[0] = ' ';
      if (v >= 10) { buf[1] = '0' + v/10; v %= 10; } else buf[1] = ' ';
      buf[2] = '0' + v;
      sys_write(1, buf, 3); }
    print_str(": ");
    print_str(name);
    print_str("\n");
}

static void assert_fail(const char *msg) {
    print_str("    FAIL assertion ");
    { char buf[4]; int i = 3; int v = assert_num;
      buf[3] = 0;
      if (v >= 100) { buf[0] = '0' + v/100; v %= 100; } else buf[0] = ' ';
      if (v >= 10) { buf[1] = '0' + v/10; v %= 10; } else buf[1] = ' ';
      buf[2] = '0' + v;
      sys_write(1, buf, 3); }
    print_str(": ");
    print_str(msg);
    print_str("\n");
    failures++;
}

#define CHECK(cond, msg) do { assert_num++; if (!(cond)) assert_fail(msg); } while (0)

// ============================================================
// 测试 1: char 直接赋值不覆写两侧 int 变量
// ============================================================

static void test_char_assign_no_clobber(void) {
    test_start("char direct assign — no clobber adjacent int");

    int left_sentinel  = 0xAAAAAAAA;
    char c;
    int right_sentinel = 0xBBBBBBBB;

    c = 0x42;  // 这里应该用 8-bit store，否则破坏左右哨兵

    CHECK(left_sentinel  == 0xAAAAAAAA, "left int sentinel intact");
    CHECK(right_sentinel == 0xBBBBBBBB, "right int sentinel intact");
    CHECK(c == 0x42, "char value correct");

    // 多次赋值
    c = 0x41;
    CHECK(c == 'A', "char reassign #1");
    CHECK(left_sentinel  == 0xAAAAAAAA, "left intact after #1");
    CHECK(right_sentinel == 0xBBBBBBBB, "right intact after #1");

    c = 0x43;
    CHECK(c == 'C', "char reassign #2");
    CHECK(left_sentinel  == 0xAAAAAAAA, "left intact after #2");
    CHECK(right_sentinel == 0xBBBBBBBB, "right intact after #2");
}

// ============================================================
// 测试 2: short 直接赋值不覆写两侧 int 变量
// ============================================================

static void test_short_assign_no_clobber(void) {
    test_start("short direct assign — no clobber adjacent int");

    int left_sentinel  = 0xAAAAAAAA;
    short s;
    int right_sentinel = 0xBBBBBBBB;

    s = 0x1234;  // 应使用 16-bit store

    CHECK(left_sentinel  == 0xAAAAAAAA, "left int sentinel intact");
    CHECK(right_sentinel == 0xBBBBBBBB, "right int sentinel intact");
    CHECK(s == 0x1234, "short value correct");

    // 负值
    s = -1;
    CHECK(s == -1, "short negative -1");
    CHECK(left_sentinel  == 0xAAAAAAAA, "left intact after negative");
    CHECK(right_sentinel == 0xBBBBBBBB, "right intact after negative");

    s = -32768;
    CHECK(s == -32768, "short min -32768");
    CHECK(left_sentinel  == 0xAAAAAAAA, "left intact after min");
    CHECK(right_sentinel == 0xBBBBBBBB, "right intact after min");
}

// ============================================================
// 测试 3: 多个 char 变量交替赋值
// ============================================================

static void test_multiple_chars(void) {
    test_start("multiple char variables interleaved with int");

    int sentinel = 0xDEADBEEF;
    char a, b, c, d;
    int guard = 0xCAFEBABE;

    a = 0x11; b = 0x22; c = 0x33; d = 0x44;

    CHECK(sentinel == 0xDEADBEEF, "sentinel intact after 4 char assigns");
    CHECK(guard    == 0xCAFEBABE, "guard intact after 4 char assigns");
    CHECK(a == 0x11, "char a correct");
    CHECK(b == 0x22, "char b correct");
    CHECK(c == 0x33, "char c correct");
    CHECK(d == 0x44, "char d correct");

    // 乱序重赋值：signed char 0xFF = -1，需用 (char) 或负值比较
    c = 0xFF; a = 0x00; d = 0x55; b = 0x7F;

    CHECK(sentinel == 0xDEADBEEF, "sentinel intact after re-assign");
    CHECK(guard    == 0xCAFEBABE, "guard intact after re-assign");
    CHECK(a == 0x00, "char a correct after re-assign");
    CHECK(b == 0x7F, "char b correct after re-assign (max positive)");
    /* signed char 0xFF = -1，C 规则下 sign-extend 后比较 */
    CHECK(c == -1, "char c == -1 (= 0xFF for signed char)");
    CHECK(d == 0x55, "char d correct after re-assign");
}

// ============================================================
// 测试 4: unsigned char / unsigned short
// ============================================================

static void test_unsigned_small_types(void) {
    test_start("unsigned char / unsigned short assign");

    int sentinel = 0x12345678;
    unsigned char uc;
    unsigned short us;
    int guard = 0x87654321;

    uc = 0x80;   // > 127, fits in unsigned char
    us = 0x8000; // > 32767, fits in unsigned short

    CHECK(sentinel == 0x12345678, "sentinel intact after unsigned assigns");
    CHECK(guard    == 0x87654321, "guard intact after unsigned assigns");
    CHECK(uc == 0x80u, "unsigned char 0x80 correct");
    CHECK(us == 0x8000u, "unsigned short 0x8000 correct");

    // 极限值
    uc = 0xFF;
    us = 0xFFFF;
    CHECK(uc == 0xFFu, "unsigned char max 0xFF");
    CHECK(us == 0xFFFFu, "unsigned short max 0xFFFF");
    CHECK(sentinel == 0x12345678, "sentinel intact after max values");
    CHECK(guard    == 0x87654321, "guard intact after max values");
}

// ============================================================
// 测试 5: 用下标数组赋值作对照（本应永远正确，验证环境没问题）
// ============================================================

static void test_array_assign_smoke(void) {
    test_start("array subscript assign smoke (always correct)");

    char buf[8];
    int sentinel = 0xA5A5A5A5;
    int i = 0;

    buf[i++] = 0x11;
    buf[i++] = 0x22;

    CHECK(sentinel == 0xA5A5A5A5, "sentinel intact after subscript write");
    CHECK(buf[0] == 0x11, "buf[0] correct");
    CHECK(buf[1] == 0x22, "buf[1] correct");
    CHECK(i == 2, "index i correct");

    short sbuf[4];
    int j = 2;
    sbuf[--j] = 0x3344;

    CHECK(sentinel == 0xA5A5A5A5, "sentinel intact after short subscript");
    CHECK(sbuf[1] == 0x3344, "sbuf[1] correct (--j = 1)");
    CHECK(j == 1, "index j correct");
}

// ============================================================
// 测试 6: 同一函数的 char/short 都赋值，混合顺序
// ============================================================

static void test_mixed_char_short(void) {
    test_start("mixed char and short assigns");

    int s1 = 0x11111111;
    char c1;
    short sh1;
    int s2 = 0x22222222;
    short sh2;
    char c2;
    int s3 = 0x33333333;

    /* 注意：signed char 0xAB = -85, signed short 0xCDEF = -12817，
       比较时必须用 (char)/(short) 强制转换或直接用负值 */
    c1 = 0xAB;
    sh1 = 0xCDEF;
    c2 = 0x12;
    sh2 = 0x3456;

    CHECK(s1 == 0x11111111, "s1 intact");
    CHECK(s2 == 0x22222222, "s2 intact");
    CHECK(s3 == 0x33333333, "s3 intact");
    CHECK(c1 == -85, "c1 == -85 (= (char)0xAB)");
    CHECK(c2 == 0x12, "c2 == 0x12");
    CHECK(sh1 == -12817, "sh1 == -12817 (= (short)0xCDEF)");
    CHECK(sh2 == 0x3456, "sh2 == 0x3456");

    // 交换写入
    sh2 = -1;
    c1 = 0xFF;
    sh1 = 0;
    c2 = 0;

    CHECK(s1 == 0x11111111, "s1 intact after re-assign");
    CHECK(s2 == 0x22222222, "s2 intact after re-assign");
    CHECK(s3 == 0x33333333, "s3 intact after re-assign");
    CHECK(c1 == -1, "c1 == -1 (= (char)0xFF after re-assign)");
    CHECK(c2 == 0, "c2 == 0 after re-assign");
    CHECK(sh1 == 0, "sh1 == 0 after re-assign");
    CHECK(sh2 == -1, "sh2 == -1 after re-assign");
}

// ============================================================
// 测试 7: signed char/short sign-extension 语义验证
// ============================================================

static void test_sign_extension(void) {
    test_start("signed char/short sign extension");

    int sentinel = 0x55555555;

    // signed char: 正数 < 128 不变
    signed char sc_pos = 0x7F;
    CHECK(sc_pos == 127, "signed char 0x7F = 127 (positive)");
    CHECK(sc_pos > 0, "signed char 127 > 0");

    // signed char: 0x80 = -128 (sign-extend to 0xFFFFFF80)
    signed char sc_neg = 0x80;
    CHECK(sc_neg == -128, "signed char 0x80 = -128 (sign-extend)");
    CHECK(sc_neg < 0, "signed char -128 < 0");
    /* 验证 unsigned char 的零扩展：加载后高 24 位应为 0 */
    do { unsigned char tmp = (unsigned char)0x80;
         CHECK(tmp == 0x80u, "(unsigned char)0x80 == 0x80 (zero-extend)");
    } while (0);

    // signed char: 0xFF = -1
    signed char sc_all = (signed char)0xFF;
    CHECK(sc_all == -1, "signed char 0xFF = -1 (sign-extend)");

    // signed short: 正数 < 32768
    signed short ss_pos = 0x7FFF;
    CHECK(ss_pos == 32767, "signed short 0x7FFF = 32767");

    // signed short: 0x8000 = -32768
    signed short ss_neg = (signed short)0x8000;
    CHECK(ss_neg == -32768, "signed short 0x8000 = -32768 (sign-extend)");
    CHECK(ss_neg < 0, "signed short -32768 < 0");

    // signed short: 0xFFFF = -1
    signed short ss_all = (signed short)0xFFFF;
    CHECK(ss_all == -1, "signed short 0xFFFF = -1 (sign-extend)");

    // signed char 提升后参与运算
    signed char a = 5, b = 3;
    CHECK(a + b == 8, "signed char 5 + 3 = 8");
    CHECK(a - b == 2, "signed char 5 - 3 = 2");

    // 负值运算
    signed char nx = -10, ny = -3;
    CHECK(nx + ny == -13, "signed char (-10) + (-3) = -13");
    CHECK(nx - ny == -7, "signed char (-10) - (-3) = -7");

    // sentinel 始终完好
    CHECK(sentinel == 0x55555555, "sentinel intact after signed tests");
}

// ============================================================
// 主入口
// ============================================================

static void run_tests(void) {
    print_str("=== char/short assign store width tests ===\n");

    test_char_assign_no_clobber();
    test_short_assign_no_clobber();
    test_multiple_chars();
    test_unsigned_small_types();
    test_array_assign_smoke();
    test_mixed_char_short();
    test_sign_extension();

    print_str("\n");
    if (failures == 0) {
        print_str("ALL PASSED\n");
        sys_exit(0);
    }
    sys_exit(failures);
}

void __tlibc_start(void) { run_tests(); }
