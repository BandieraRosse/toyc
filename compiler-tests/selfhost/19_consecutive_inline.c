// EXPECT: 0
// SELF_CONTAINED
// consecutive_inline.c — 测试连续的 static inline 函数调用的代码生成正确性
//
// 背景：toyc 在自举过程中编译自身时，cgen.c/toyc.h 大量使用了连续的 static inline
// 函数调用模式（如 emit1/e1/emit4/e4），每行连续调用 2-6 次。
//
// 验证模式：
//   1. 连续相同参数的 inline void 调用（emit 类模式）
//   2. 连续不同参数的 inline void 调用（emit4 类模式）
//   3. 连续相同参数的有返回值 inline 调用
//   4. 混合参数模式的连续 inline 调用
//   5. 带分支的 static inline 函数连续调用
//   6. 表达式内嵌套 inline 调用连续出现
//   7. 模拟实际 emit4/fixup 的连续 4-5 次调用模式
//   8. 大量连续相同参数调用（压力测试）

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

/* ─── 打印辅助 ─── */
static void print_str(const char *s)
{
    int len = 0;
    while (s[len]) len++;
    sys_write(1, s, len);
}
static void print_dec(int v)
{
    char buf[16];
    int i = 0, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    do { buf[i++] = '0' + (v % 10); v /= 10; } while (v > 0);
    if (neg) buf[i++] = '-';
    while (i > 0) sys_write(1, buf + (--i), 1);
}

/* ─── 测试状态 ─── */
static int failures = 0;
static int test_num = 0;

static void check(int cond, const char *msg)
{
    test_num++;
    if (!cond) {
        print_str("FAIL ");
        print_dec(test_num);
        print_str(": ");
        print_str(msg);
        print_str("\n");
        failures = 1;
    }
}

/* ================================================================
 *  被测函数：模拟 emit1/e1 的带副作用 static inline
 * ================================================================ */

static unsigned char code_buf[32];
static int code_size;

static void emit_reset(void)
{
    code_size = 0;
}

static inline void emit(int b)
{
    if (code_size < 32)
        code_buf[code_size++] = (unsigned char)(b & 0xFF);
}

/* 模拟 emit4 */
static inline void emit4(int v)
{
    emit(v); emit(v >> 8); emit(v >> 16); emit(v >> 24);
}

/* 模拟 e1（toyc.h 中的 inline 版本） */
static inline void e1(int b)
{
    if (code_size < 32)
        code_buf[code_size++] = (unsigned char)(b & 0xFF);
}

/* ================================================================
 *  简单算术 inline 函数
 * ================================================================ */

static inline int add(int a, int b) { return a + b; }
static inline int sub(int a, int b) { return a - b; }
static inline int mul(int a, int b) { return a * b; }

/* ================================================================
 *  带分支的 inline 函数（模拟 clamp/align_up 等模式）
 * ================================================================ */

static inline int clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline int max3(int a, int b, int c)
{
    int m = a;
    if (b > m) m = b;
    if (c > m) m = c;
    return m;
}

/* ================================================================
 *  选择器 inline（函数指针数组调用模拟）
 * ================================================================ */

static int fn_a(int x) { return x + 1; }
static int fn_b(int x) { return x * 2; }
static int fn_c(int x) { return x * x; }

static inline int dispatch(int sel, int x)
{
    if (sel == 0) return fn_a(x);
    if (sel == 1) return fn_b(x);
    return fn_c(x);
}

/* ================================================================
 *  主测试
 * ================================================================ */

void __tlibc_start(void)
{
    int i;

    /* ════════════════════════════════════════════════
       测试组 1：emit 模式 — 连续相同参数
       ════════════════════════════════════════════════ */

    emit_reset();
    emit(0); emit(0); emit(0); emit(0);
    check(code_size == 4, "emit4(0): code_size == 4");
    check(code_buf[0] == 0, "emit4(0): byte[0]==0");
    check(code_buf[1] == 0, "emit4(0): byte[1]==0");
    check(code_buf[2] == 0, "emit4(0): byte[2]==0");
    check(code_buf[3] == 0, "emit4(0): byte[3]==0");

    /* ════════════════════════════════════════════════
       测试组 2：emit 模式 — 连续不同参数（prologue 模式）
       ════════════════════════════════════════════════ */

    emit_reset();
    emit(0x55);                   /* push rbp */
    emit(0x48); emit(0x89); emit(0xE5);  /* mov rbp, rsp */
    check(code_size == 4, "prologue: code_size == 4");
    check(code_buf[0] == 0x55, "prologue: byte[0]=0x55");
    check(code_buf[1] == 0x48, "prologue: byte[1]=0x48");
    check(code_buf[2] == 0x89, "prologue: byte[2]=0x89");
    check(code_buf[3] == 0xE5, "prologue: byte[3]=0xE5");

    /* ════════════════════════════════════════════════
       测试组 3：emit 模式 — 连续相同+不同混合（jmp rel32 模式）
       ════════════════════════════════════════════════ */

    emit_reset();
    emit(0xE9); emit(0x00); emit(0x00); emit(0x00); emit(0x00);
    check(code_size == 5, "jmp5: code_size == 5");
    check(code_buf[0] == 0xE9, "jmp5: byte[0]=0xE9");
    check(code_buf[1] == 0x00, "jmp5: byte[1]=0x00");
    check(code_buf[2] == 0x00, "jmp5: byte[2]=0x00");
    check(code_buf[3] == 0x00, "jmp5: byte[3]=0x00");
    check(code_buf[4] == 0x00, "jmp5: byte[4]=0x00");

    /* ════════════════════════════════════════════════
       测试组 4：emit4 — 模拟 emit4(v) 发射 4 字节
       ════════════════════════════════════════════════ */

    emit_reset();
    emit4(0x12345678);
    check(code_size == 4, "emit4: code_size == 4");
    check(code_buf[0] == 0x78, "emit4: byte[0]=0x78 (LE low)");
    check(code_buf[1] == 0x56, "emit4: byte[1]=0x56");
    check(code_buf[2] == 0x34, "emit4: byte[2]=0x34");
    check(code_buf[3] == 0x12, "emit4: byte[3]=0x12 (LE high)");

    emit_reset();
    emit4(0x00000000);
    check(code_size == 4, "emit4(0): code_size == 4");
    check(code_buf[0] == 0, "emit4(0): all zero");
    check(code_buf[1] == 0, "emit4(0): all zero");
    check(code_buf[2] == 0, "emit4(0): all zero");
    check(code_buf[3] == 0, "emit4(0): all zero");

    emit_reset();
    emit4(0xFFFFFFFF);
    check(code_size == 4, "emit4(-1): code_size == 4");
    check(code_buf[0] == 0xFF, "emit4(-1): all 0xFF");
    check(code_buf[1] == 0xFF, "emit4(-1): all 0xFF");
    check(code_buf[2] == 0xFF, "emit4(-1): all 0xFF");
    check(code_buf[3] == 0xFF, "emit4(-1): all 0xFF");

    /* ════════════════════════════════════════════════
       测试组 5：e1 连续相同参数（toyc.h 版本）
       ════════════════════════════════════════════════ */

    emit_reset();
    e1(0x90); e1(0x90); e1(0x90);  /* 3 NOPs */
    check(code_size == 3, "e1 NOP x3: code_size == 3");
    check(code_buf[0] == 0x90, "e1 NOP: byte[0]=0x90");
    check(code_buf[1] == 0x90, "e1 NOP: byte[1]=0x90");
    check(code_buf[2] == 0x90, "e1 NOP: byte[2]=0x90");

    /* ════════════════════════════════════════════════
       测试组 6：简单算术 inline — 相同参数连续
       ════════════════════════════════════════════════ */

    check(add(5, 3) == 8, "add(5,3) == 8");
    check(add(5, 3) == 8, "add(5,3) == 8 (2nd)");
    check(add(5, 3) == 8, "add(5,3) == 8 (3rd)");
    check(add(5, 3) == 8, "add(5,3) == 8 (4th)");

    /* ════════════════════════════════════════════════
       测试组 7：简单算术 inline — 不同参数连续
       ════════════════════════════════════════════════ */

    check(add(1, 2) == 3, "add(1,2) == 3");
    check(add(3, 4) == 7, "add(3,4) == 7");
    check(add(5, 6) == 11, "add(5,6) == 11");
    check(add(7, 8) == 15, "add(7,8) == 15");

    /* ════════════════════════════════════════════════
       测试组 8：混合连续 inline 调用（切换函数）
       ════════════════════════════════════════════════ */

    check(add(10, 5) == 15, "add(10,5) == 15");
    check(sub(10, 5) == 5, "sub(10,5) == 5");
    check(mul(10, 5) == 50, "mul(10,5) == 50");
    check(add(10, 5) == 15, "add(10,5) == 15 (again after sub/mul)");

    /* ════════════════════════════════════════════════
       测试组 9：带分支的 inline — 连续相同参数
       ════════════════════════════════════════════════ */

    check(clamp(5, 0, 10) == 5, "clamp(5,0,10) == 5");
    check(clamp(5, 0, 10) == 5, "clamp(5,0,10) == 5 (2nd)");
    check(clamp(5, 0, 10) == 5, "clamp(5,0,10) == 5 (3rd)");

    /* ════════════════════════════════════════════════
       测试组 10：带分支的 inline — 覆盖全部路径
       ════════════════════════════════════════════════ */

    check(clamp(-5, 0, 10) == 0, "clamp(-5,0,10) == 0");
    check(clamp(0, 0, 10) == 0, "clamp(0,0,10) == 0");
    check(clamp(5, 0, 10) == 5, "clamp(5,0,10) == 5");
    check(clamp(10, 0, 10) == 10, "clamp(10,0,10) == 10");
    check(clamp(20, 0, 10) == 10, "clamp(20,0,10) == 10");

    /* ════════════════════════════════════════════════
       测试组 11：max3 — 覆盖各种分支模式
       ════════════════════════════════════════════════ */

    check(max3(1, 5, 3) == 5, "max3(1,5,3) == 5");
    check(max3(9, 2, 7) == 9, "max3(9,2,7) == 9");
    check(max3(1, 2, 8) == 8, "max3(1,2,8) == 8");
    check(max3(5, 5, 5) == 5, "max3(5,5,5) == 5 (all equal)");
    check(max3(3, 7, 1) == 7, "max3(3,7,1) == 7");

    /* ════════════════════════════════════════════════
       测试组 12：dispatch — inline + 真实的函数调用组合
       ════════════════════════════════════════════════ */

    check(dispatch(0, 5) == 6, "dispatch(0,5) == fn_a(5) == 6");
    check(dispatch(1, 5) == 10, "dispatch(1,5) == fn_b(5) == 10");
    check(dispatch(2, 5) == 25, "dispatch(2,5) == fn_c(5) == 25");
    /* 连续相同 dispatch */
    check(dispatch(0, 5) == 6, "dispatch(0,5) == 6 (2nd)");
    check(dispatch(0, 5) == 6, "dispatch(0,5) == 6 (3rd)");

    /* ════════════════════════════════════════════════
       测试组 13：嵌套 inline + 连续相同
       ════════════════════════════════════════════════ */

    check(add(mul(2, 3), mul(4, 5)) == 26, "add(mul(2,3),mul(4,5)) == 26");
    check(add(mul(2, 3), mul(4, 5)) == 26, "add(mul(2,3),mul(4,5)) == 26 (2nd)");
    check(add(mul(2, 3), mul(4, 5)) == 26, "add(mul(2,3),mul(4,5)) == 26 (3rd)");

    /* ════════════════════════════════════════════════
       测试组 14：复杂表达式中的连续 inline
       ════════════════════════════════════════════════ */

    {
        int r;
        r = add(1, 2) + add(3, 4);
        check(r == 10, "add+add == 10");

        r = mul(clamp(5, 0, 10), clamp(8, 0, 10));
        check(r == 40, "mul(clamp,clamp) == 40");

        r = add(max3(1, 9, 5), max3(2, 3, 10));
        check(r == 19, "add(max3,max3) == 19");
    }

    /* ════════════════════════════════════════════════
       测试组 15：大量连续计数 — 循环中的 inline 调用
       ════════════════════════════════════════════════ */

    {
        int sum = 0;
        int j;
        for (j = 0; j < 10; j++)
            sum = add(sum, j);
        check(sum == 45, "loop add sum == 45");
    }

    /* ════════════════════════════════════════════════
       测试组 16：连续的 static inline 调用模拟
       jcc 回填模式（0F 8x xx xx xx xx）
       ════════════════════════════════════════════════ */

    emit_reset();
    e1(0x0F); e1(0x8F); e1(0x00); e1(0x00); e1(0x00); e1(0x00);
    check(code_size == 6, "jcc6: code_size == 6");
    check(code_buf[0] == 0x0F, "jcc6: byte[0] == 0x0F");
    check(code_buf[1] == 0x8F, "jcc6: byte[1] == 0x8F");
    check(code_buf[2] == 0x00, "jcc6: byte[2] == 0x00");
    check(code_buf[3] == 0x00, "jcc6: byte[3] == 0x00");
    check(code_buf[4] == 0x00, "jcc6: byte[4] == 0x00");
    check(code_buf[5] == 0x00, "jcc6: byte[5] == 0x00");

    /* ════════════════════════════════════════════════
       测试组 17：重复调用后同时依赖副作用 + 返回值
       ════════════════════════════════════════════════ */

    {
        int x = 0;
        x = add(x, 1);
        check(x == 1, "x = add(x,1) == 1");
        x = add(x, 1);
        check(x == 2, "x = add(x,1) == 2 (2nd)");
        x = add(x, 1);
        check(x == 3, "x = add(x,1) == 3 (3rd)");
        x = add(x, 1);
        check(x == 4, "x = add(x,1) == 4 (4th)");
        /* 连续四联 add */
        i = 0;
        i = add(i, 1); i = add(i, 1); i = add(i, 1); i = add(i, 1);
        check(i == 4, "i repeated add x4 == 4");
    }

    /* ════════════════════════════════════════════════
       测试组 18：连续调用后状态隔离 — 三组 prologue，验证互不干扰
       ════════════════════════════════════════════════ */

    emit_reset();
    e1(0x55); e1(0x48); e1(0x89); e1(0xE5);  /* 标准 prologue */
    e1(0x55); e1(0x48); e1(0x89); e1(0xE5);  /* 再次 */
    e1(0x55); e1(0x48); e1(0x89); e1(0xE5);  /* 第三次 */
    check(code_size == 12, "3x prologue: code_size == 12");
    check(code_buf[0] == 0x55 && code_buf[4] == 0x55 && code_buf[8] == 0x55,
          "3x prologue: all 3 0x55 positions correct");
    check(code_buf[1] == 0x48 && code_buf[5] == 0x48 && code_buf[9] == 0x48,
          "3x prologue: all 3 0x48 positions correct");
    check(code_buf[2] == 0x89 && code_buf[6] == 0x89 && code_buf[10] == 0x89,
          "3x prologue: all 3 0x89 positions correct");
    check(code_buf[3] == 0xE5 && code_buf[7] == 0xE5 && code_buf[11] == 0xE5,
          "3x prologue: all 3 0xE5 positions correct");

    /* ════════════════════════════════════════════════
       测试组 19：连续多次 emit(0xFF) — 全 1 字节写入（高 bit 7）
       ════════════════════════════════════════════════ */

    emit_reset();
    e1(0xFF); e1(0xFF); e1(0xFF); e1(0xFF);
    check(code_size == 4, "emit4(0xFF): code_size == 4");
    check(code_buf[0] == 0xFF, "emit4(0xFF): byte[0]==0xFF");
    check(code_buf[1] == 0xFF, "emit4(0xFF): byte[1]==0xFF");
    check(code_buf[2] == 0xFF, "emit4(0xFF): byte[2]==0xFF");
    check(code_buf[3] == 0xFF, "emit4(0xFF): byte[3]==0xFF");

    /* ════════════════════════════════════════════════
       测试组 20：重复 inline 后接独立的真实函数调用
       ════════════════════════════════════════════════ */

    {
        int r1, r2, r3, r4;
        r1 = add(10, 20);   /* inline */
        r2 = sub(100, 30);  /* inline */
        r3 = mul(6, 7);     /* inline */
        r4 = add(r1, r2);   /* inline */
        check(r1 == 30, "r1 == add(10,20) == 30");
        check(r2 == 70, "r2 == sub(100,30) == 70");
        check(r3 == 42, "r3 == mul(6,7) == 42");
        check(r4 == 100, "r4 == add(r1,r2) == 100");
    }

    /* ════════════════════════════════════════════════
       报告结果
       ════════════════════════════════════════════════ */

    if (failures) {
        print_str("SOME TESTS FAILED\n");
        sys_exit(1);
    }
    print_str("OK\n");
    sys_exit(0);
}
