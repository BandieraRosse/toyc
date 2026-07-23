// EXPECT: 0
// SELF_CONTAINED
// type_system.c — 测试类型系统最后一环：混合符号比较、long long、
//                  64 位循环条件、指针条件、变参 unsigned
//
// 编译：build/toyc  06_type_system.c -o /tmp/06_type_system.o
// 链接：ld -nostdlib -static -T ld.script /tmp/06_type_system.o -o /tmp/06_type_system
// 运行：/tmp/06_type_system

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
// 测试框架 — 不用 __LINE__（toyc __LINE__ 返回 0）
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
// 测试 1: 混合符号比较（C 规则：有符号提升为无符号）
// (int)-1 < (unsigned)1 → -1 提升为 0xFFFFFFFFu → 0xFFFFFFFF < 1 = FALSE
// ============================================================

static void test_mixed_sign_cmp(void) {
    test_start("mixed sign comparison");

    int x = -1;
    unsigned int u = 1;

    // C 规则：signed 提升为 unsigned
    // -1 变成 0xFFFFFFFFu，所以 0xFFFFFFFFu < 1u 为假
    CHECK(!(x < u), "0xFFFFFFFFu < 1u = false");

    // 反向：1u > 0xFFFFFFFFu 为假
    CHECK(!(u > x), "1u > 0xFFFFFFFFu = false");

    // 混合 long + unsigned int
    long y = -1L;
    unsigned int v = 1;
    CHECK(!(y < v), "(long)-1 < (unsigned)1 = false (C promotion)");
    CHECK(!(v > y), "(unsigned)1 > (long)-1 = false (C promotion)");

    // long 强转回 signed
    CHECK((long)(int)-1 == -1L, "(long)(int)-1 == -1L");
    CHECK((unsigned long)(int)-1 == 0xFFFFFFFFFFFFFFFFUL,
           "(unsigned long)(int)-1 == 0xFFFFFFFFFFFFFFFF");

    // 正常 unsigned 比较（确保没被 mixed-sign 破坏）
    unsigned int big = 0x80000000u;
    unsigned int one = 1u;
    CHECK(big > one, "unsigned 0x80000000 > 1");
    CHECK(one < big, "unsigned 1 < 0x80000000");
}

// ============================================================
// 测试 2: unsigned long long
// ============================================================

static void test_unsigned_long_long(void) {
    test_start("unsigned long long");

    unsigned long long a = 0x8000000000000000ULL;
    unsigned long long b = 2ULL;

    CHECK(a > b, "ULL 0x8000... > 2");
    CHECK(!(a < b), "ULL 0x8000... < 2 = false");
    CHECK(b < a, "ULL 2 < 0x8000...");
    CHECK(a >= a, "ULL >= itself");
    CHECK(b <= b, "ULL <= itself");

    CHECK(a / b == 0x4000000000000000ULL,
           "ULL 0x8000... / 2 = 0x4000...");

    unsigned long long m = 0x8000000000000005ULL;
    CHECK(m % b == 1ULL, "ULL 0x8000...5 %% 2 = 1");

    CHECK((a >> 1) == 0x4000000000000000ULL,
           "ULL 0x8000... >> 1 = 0x4000... (logical shift)");
    unsigned long long all_ones = 0xFFFFFFFFFFFFFFFFULL;
    CHECK((all_ones >> 4) == 0x0FFFFFFFFFFFFFFFULL,
           "ULL 0xFF... >> 4 = 0x0FFF...");

    // 无符号 long long 常量加法
    unsigned long long sum = 0xFFFFFFFF00000000ULL + 1ULL;
    CHECK(sum == 0xFFFFFFFF00000001ULL, "ULL addition with high bits");
}

// ============================================================
// 测试 3: signed long long
// ============================================================

static void test_signed_long_long(void) {
    test_start("signed long long");

    long long a = -16LL;
    long long b = 4LL;

    CHECK(a / b == -4LL, "LL (-16) / 4 = -4");
    CHECK(a % b == 0LL, "LL (-16) %% 4 = 0");

    CHECK(a < b, "LL -16 < 4");
    CHECK(!(a > b), "LL -16 > 4 = false");

    long long neg = -8LL;
    CHECK((neg >> 1) == -4LL, "LL (-8) >> 1 = -4 (arithmetic)");
    long long neg_one = -1LL;
    CHECK((neg_one >> 1) == -1LL, "LL (-1) >> 1 = -1 (sign propagate)");

    long long big_pos = 0x7FFFFFFFFFFFFFFFLL;
    CHECK(big_pos > 0LL, "LL 0x7FFF... > 0");
    CHECK(big_pos > b, "LL 0x7FFF... > 4");

    // 有符号 long long 比较（确保高位置 1 会被正确识别为负值）
    long long high_bit = 0x8000000000000000LL;
    CHECK(high_bit < 0LL, "LL 0x8000... < 0 (it's negative)");
}

// ============================================================
// 测试 4: for/while/do-while 的 64 位条件
// ============================================================

static void test_64bit_loop_cond(void) {
    test_start("64-bit loop conditions");

    // for (; n;) 走 cgen_for，之前没有 type_size==8 检查
    long n = 0x100000000L;  // 超出 32 位的值
    int count = 0;
    for (; n;) {
        count++;
        n = 0;
    }
    CHECK(count == 1, "for (; n;) with high-bit long executes once");

    // while (m) 走 cgen_while — 已有 fix
    long m = 0x100000000L;
    int wcount = 0;
    while (m) { wcount++; m = 0; }
    CHECK(wcount == 1, "while (m) with high-bit long executes once");

    // do-while — 已有 fix
    long k = 0x100000000L;
    int dwcount = 0;
    do { dwcount++; k = 0; } while (k);
    CHECK(dwcount == 1, "do-while (k) with high-bit long executes once");

    // 零值不循环
    long zero = 0L;
    int zcount = 0;
    for (; zero;) { zcount++; }
    CHECK(zcount == 0, "for (; 0l;) executes 0 times");
    while (zero) { zcount++; }
    CHECK(zcount == 0, "while (0L) executes 0 times");

    // 小正数（32 位内）
    long small = 2L;
    int scount = 0;
    for (; small > 0L; small--) { scount++; }
    CHECK(scount == 2, "for (; small > 0;) iterates 2 times");
}

// ============================================================
// 测试 5: while (ptr) / for (;ptr;) 指针条件
// ============================================================

static void test_ptr_conditions(void) {
    test_start("pointer conditions");

    long buf[4];
    buf[0] = 1; buf[1] = 2; buf[2] = 3; buf[3] = 4;

    long *p = buf;
    int sum = 0;
    if (p) sum += *p;
    CHECK(sum == 1, "if (ptr) with non-null loads value");

    // while (ptr) — 非零指针
    long *q = buf;
    int qcount = 0;
    while (q) { qcount++; q = 0; }
    CHECK(qcount == 1, "while (non-null ptr) executes once");

    // for (;ptr;) — 非零指针
    long *r = buf;
    int rcount = 0;
    for (; r;) { rcount++; r = 0; }
    CHECK(rcount == 1, "for (; non-null ptr;) executes once");

    // 空指针
    long *s = 0;
    int scount = 0;
    while (s) { scount++; }
    CHECK(scount == 0, "while (null ptr) executes 0 times");
    for (; s;) { scount++; }
    CHECK(scount == 0, "for (; null ptr;) executes 0 times");
}

// ============================================================
// 测试 6: unsigned 在函数参数传递中
// ============================================================

static void test_arg_unsigned(unsigned int a, unsigned long b,
                               unsigned int c, unsigned long d) {
    CHECK(a == 0xFFFFFFFFu, "unsigned int param 0xFFFFFFFF");
    CHECK(b == 0x8000000000000000UL, "unsigned long param 0x8000...");
    CHECK(c == 0x80000000u, "unsigned int param 0x80000000");
    CHECK(d == 0xFFFFFFFFFFFFFFFFUL, "unsigned long param 0xFF...");
}

static void test_func_arg_unsigned(void) {
    test_start("unsigned function arguments");

    test_arg_unsigned(0xFFFFFFFFu, 0x8000000000000000UL,
                      0x80000000u, 0xFFFFFFFFFFFFFFFFUL);
}

// ============================================================
// 测试 7: 类型大小
// ============================================================

static void test_type_sizes(void) {
    test_start("type sizes");

    long long ll = 0;
    CHECK(sizeof(ll) == 8, "sizeof(long long) = 8");
    CHECK(sizeof(unsigned long long) == 8, "sizeof(unsigned long long) = 8");
    CHECK(sizeof(long) == 8, "sizeof(long) = 8");
    CHECK(sizeof(unsigned long) == 8, "sizeof(unsigned long) = 8");
    CHECK(sizeof(unsigned int) == 4, "sizeof(unsigned int) = 4");
    CHECK(sizeof(int) == 4, "sizeof(int) = 4");

    unsigned char uc = 0xFF;
    CHECK(uc == 255, "unsigned char 0xFF = 255");
}

// ============================================================
// 测试 8: 指针 + 大偏移（验证 disp32 地址模式）
// ============================================================

static void test_ptr_big_offset(void) {
    test_start("pointer big offset");

    // 大偏移数组访问，需要 disp32 而非 disp8
    int arr[256];
    int i;
    for (i = 0; i < 256; i++) arr[i] = i;
    CHECK(arr[255] == 255, "arr[255] with large offset (disp32)");
    CHECK(arr[128] == 128, "arr[128] with moderate offset");
    CHECK(arr[0] == 0, "arr[0] with zero offset");

    // 超大偏移（超出 -128..127 范围）
    int far = arr[200];
    CHECK(far == 200, "arr[200] with far offset");
}

// ============================================================
// 测试 9: char 数组 + i++ 读写（验证 8/16-bit 存储不覆写相邻变量）
// ============================================================

static void test_char_arr_inc(void) {
    test_start("char/short store width");

    char buf[8];
    int i = 0;

    // 写入（走 ASSIGN 路径）
    buf[i++] = 'A';
    buf[i++] = 'B';
    CHECK(buf[0] == 'A', "char arr write buf[0] via i++");
    CHECK(buf[1] == 'B', "char arr write buf[1] via i++");
    CHECK(i == 2, "char arr write i == 2");

    // 读取（走 SUBSCRIPT + VAR_DECL 路径）
    char a = buf[i++];  /* i=2, buf[2]=undefined but we just need the store not to corrupt */
    CHECK(i == 3, "char a = buf[i++] keeps i correct");

    // short 类型
    short sarr[4];
    int j = 0;
    sarr[j++] = 100;
    sarr[j++] = 200;
    CHECK(sarr[0] == 100, "short arr write sarr[0] via j++");
    CHECK(sarr[1] == 200, "short arr write sarr[1] via j++");
    CHECK(j == 2, "short arr write j == 2");

    short sa = sarr[j++];
    CHECK(j == 3, "short sa = sarr[j++] keeps j correct");
}

// ============================================================
// 主入口
// ============================================================

static void run_tests(void) {
    print_str("=== type system comprehensive tests ===\n");

    test_mixed_sign_cmp();
    test_unsigned_long_long();
    test_signed_long_long();
    test_64bit_loop_cond();
    test_ptr_conditions();
    test_func_arg_unsigned();
    test_type_sizes();
    test_ptr_big_offset();
    test_char_arr_inc();

    print_str("\n");
    if (failures == 0) {
        print_str("ALL PASSED\n");
        sys_exit(0);
    }
    sys_exit(failures);
}

void __tlibc_start(void) { run_tests(); }
