// EXPECT: 0
// SELF_CONTAINED
// 36_sign_extension.c — int→long 符号扩展全面测试
//
// 验证 tcc 在有符号 32 位（及更窄）表达式赋值/返回/传参到
// 64 位位置时正确发射 movsxd（符号扩展）。
//
// 负的 int 在位模式上是 0xFFFFFFFF（低32位全1）。
// x86-64 的 32 位操作自动零扩展高 32 位，所以 rax=0x00000000FFFFFFFF。
// 正确符号扩展到 long 后应为 rax=0xFFFFFFFFFFFFFFFF。

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

static void putchar(char c)
{
    char buf[1]; buf[0] = c;
    sys_write(1, buf, 1);
}

static void puts(const char *s)
{
    int len = 0;
    while (s[len]) len++;
    sys_write(1, s, len);
}

static void putlong(long v)
{
    if (v < 0) { putchar('-'); v = -v; }
    if (v == 0) { putchar('0'); return; }
    char buf[24]; int i, n = 0;
    while (v) { buf[n++] = '0' + (v % 10); v /= 10; }
    for (i = n-1; i >= 0; i--) putchar(buf[i]);
}

// ============================================================
// 全局变量
// ============================================================

long g_long;
int g_int;

// ============================================================
// 测试用函数：返回 int 但声明为 long
// ============================================================

static long return_int_var(void)
{
    int x = -11;
    return x;  // int 变量 → long 返回值：需要符号扩展
}

static long return_int_expr(void)
{
    int a = -7, b = -6;
    return a + b;  // int 表达式 → long 返回值：需要符号扩展
}

static long return_char(void)
{
    signed char c = -8;
    return c;  // signed char → long：需要符号扩展
}

static long return_short(void)
{
    short s = -16;
    return s;  // short → long：需要符号扩展
}

static long return_neg_const(void)
{
    return -20;  // 负常量 → long：需要符号扩展
}

static long return_unsigned_int(void)
{
    unsigned int u = 0x80000000U;
    return u;  // unsigned int → long：不应符号扩展（结果应为 0x80000000 正数）
}

// ============================================================
// main
// ============================================================

int main(void)
{
    // ─── 1. 局部变量赋值 ───

    // 1a. int 变量 → long
    {
        int a = -1;
        long b = a;
        if (b != -1L) { puts("FAIL 1a: int->long = "); putlong(b); putchar('\n'); return 1; }
    }

    // 1b. int 常量 → long
    {
        long c = -2;
        if (c != -2L) { puts("FAIL 1b: int const->long\n"); return 2; }
    }

    // 1c. int 表达式（binop）→ long
    {
        int d = 3, e = -5;
        long f = d + e;
        if (f != -2L) { puts("FAIL 1c: int binop->long = "); putlong(f); putchar('\n'); return 3; }
    }

    // 1d. signed char → long
    {
        signed char g = -8;
        long h = g;
        if (h != -8L) { puts("FAIL 1d: char->long\n"); return 4; }
    }

    // 1e. short → long
    {
        short i = -16;
        long j = i;
        if (j != -16L) { puts("FAIL 1e: short->long\n"); return 5; }
    }

    // 1f. unsigned int → long（不应符号扩展）
    {
        unsigned int k = 0x80000000U;
        long l = k;
        /* 0x80000000 as unsigned long should be positive */
        if (l < 0) { puts("FAIL 1f: unsigned->long sign-extended\n"); return 6; }
    }

    // 1g. unsigned char → long
    {
        unsigned char m = 0x80;
        long n = m;
        /* 0x80 as long should be positive 128 */
        if (n != 128L) { puts("FAIL 1g: unsigned char->long\n"); return 7; }
    }

    // ─── 2. 函数返回值 ───

    // 2a. int 返回 long
    {
        long r = return_int_var();
        if (r != -11L) { puts("FAIL 2a: return int->long = "); putlong(r); putchar('\n'); return 8; }
    }

    // 2b. int 表达式返回 long
    {
        long r = return_int_expr();
        if (r != -13L) { puts("FAIL 2b: return int expr->long\n"); return 9; }
    }

    // 2c. char 返回 long
    {
        long r = return_char();
        if (r != -8L) { puts("FAIL 2c: return char->long\n"); return 10; }
    }

    // 2d. short 返回 long
    {
        long r = return_short();
        if (r != -16L) { puts("FAIL 2d: return short->long\n"); return 11; }
    }

    // 2e. 负常量返回 long
    {
        long r = return_neg_const();
        if (r != -20L) { puts("FAIL 2e: return neg const->long\n"); return 12; }
    }

    // 2f. unsigned int 返回 long（不应符号扩展）
    {
        long r = return_unsigned_int();
        if (r < 0) { puts("FAIL 2f: return unsigned->long sign-extended\n"); return 13; }
    }

    // ─── 3. 全局变量赋值 ───

    {
        int x = -50;
        g_long = x;     // 全局 long = int 变量
        if (g_long != -50L) { puts("FAIL 3a: global long = int var\n"); return 14; }

        g_long = -60;   // 全局 long = int 常量
        if (g_long != -60L) { puts("FAIL 3b: global long = int const\n"); return 15; }

        int a2 = 7, b2 = -9;
        g_long = a2 + b2;  // 全局 long = int 表达式
        if (g_long != -2L) { puts("FAIL 3c: global long = int expr\n"); return 16; }

        // 全局 long = unsigned int → 不应符号扩展
        unsigned int u2 = 0x80000000U;
        g_long = u2;
        if (g_long < 0) { puts("FAIL 3d: global long = unsigned int\n"); return 17; }

        // 全局 int → 再赋值给 long（先存 int，再读出来赋给 long）
        g_int = -70;
        g_long = g_int;
        if (g_long != -70L) { puts("FAIL 3e: global long = global int\n"); return 18; }
    }

    // ─── 4. 指针解引用赋值（*long_ptr = int_expr） ───

    {
        long target = 0L;
        long *p = &target;
        int src = -80;
        *p = src;  // *long_ptr = int
        if (target != -80L) { puts("FAIL 4a: *long_ptr = int\n"); return 19; }

        *p = -90;  // *long_ptr = int_const
        if (target != -90L) { puts("FAIL 4b: *long_ptr = int_const\n"); return 20; }

        int a3 = 10, b3 = -15;
        *p = a3 + b3;  // *long_ptr = int_expr (binop)
        if (target != -5L) { puts("FAIL 4c: *long_ptr = int_expr\n"); return 21; }
    }

    // ─── 5. 数组元素赋值（long_arr[i] = int_expr） ───

    {
        long arr[4];
        int i;
        for (i = 0; i < 4; i++) arr[i] = 0L;

        int src5 = -100;
        arr[0] = src5;  // long_arr[i] = int
        if (arr[0] != -100L) { puts("FAIL 5a: long_arr[i]=int\n"); return 22; }

        arr[1] = -110;  // long_arr[i] = int_const
        if (arr[1] != -110L) { puts("FAIL 5b: long_arr[i]=int_const\n"); return 23; }

        int a4 = 5, b4 = -8;
        arr[2] = a4 + b4;  // long_arr[i] = int_expr
        if (arr[2] != -3L) { puts("FAIL 5c: long_arr[i]=int_expr\n"); return 24; }

        // unsigned int → long_arr：不应符号扩展
        unsigned int u3 = 0x80000000U;
        arr[3] = u3;
        if (arr[3] < 0) { puts("FAIL 5d: long_arr[i]=unsigned int\n"); return 25; }
    }

    // ─── 6. 结构体成员赋值 ───

    {
        struct { long x; int y; } s;
        s.x = 0L;

        int src6 = -200;
        s.x = src6;  // struct.long_member = int
        if (s.x != -200L) { puts("FAIL 6a: struct.long=int\n"); return 26; }

        s.x = -210;  // struct.long_member = int_const
        if (s.x != -210L) { puts("FAIL 6b: struct.long=int_const\n"); return 27; }

        int a5 = 3, b5 = -7;
        s.x = a5 + b5;  // struct.long_member = int_expr
        if (s.x != -4L) { puts("FAIL 6c: struct.long=int_expr\n"); return 28; }
    }

    // ─── 7. 三元表达式 ───

    {
        int cond_flag = 1;
        int pos = 42, neg = -42;
        long result = cond_flag ? (long)pos : (long)neg;
        if (result != 42L) { puts("FAIL 7: ternary\n"); return 29; }
    }

    // ─── 8. for 循环初始化 ───

    {
        int x8 = -300;
        long y8;
        for (y8 = x8; y8 < -290L; ) break;  // for init: long = int
        if (y8 != -300L) { puts("FAIL 8a: for init int->long\n"); return 30; }

        long z8;
        for (z8 = -310; z8 < -300L; ) break;  // for init: long = int_const
        if (z8 != -310L) { puts("FAIL 8b: for init const->long\n"); return 31; }
    }

    // ─── 9. 一元负号表达式赋值 ───

    {
        int x9 = 4;
        long y9 = -x9;   // unary neg: -4 (int) assigned to long: needs sext
        if (y9 != -4L) { puts("FAIL 9a: unary neg int->long\n"); return 32; }
    }

    // ─── 10. unsigned 边界值保证不会符号扩展 ───

    {
        unsigned int u10 = 0xFFFFFFFFU;
        long v10 = u10;
        // 作为有符号 long，0xFFFFFFFFFFFFFFFF 是 -1
        // 但我们期望 0x00000000FFFFFFFF = 4294967295
        if (v10 < 0) { puts("FAIL 10a: 0xFFFFFFFF unsigned should not sign-ext\n"); return 33; }
        if (v10 == -1L) { puts("FAIL 10b: 0xFFFFFFFF unsigned should not sign-ext\n"); return 34; }
    }

    // ─── 11. 全局变量 main 初始化代码路径 ───

    {
        // 前面的测试已通过 g_long 覆盖全局赋值
        // 补充一个负值验证
        g_long = -500;
        if (g_long != -500L) { puts("FAIL 11: global neg long\n"); return 35; }
    }

    puts("PASS: all sign-extension tests passed\n");
    return 0;
}

void __tlibc_start(void) { sys_exit(main()); }
