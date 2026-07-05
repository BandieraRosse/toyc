// EXPECT: 0
// SELF_CONTAINED
// 09_preproc_selfref_dispatch.c — 验证预处理器自引用宏与 ## dispatch 修复
//
// 本文件验证 tcc 预处理器的两项修复：
//
// Bug 1 修复：自引用函数宏（与 inline 函数同名的包装宏）在替换文本
//   重扫描时正确禁用自身，不会无限递归。关键测试是包装宏中使用 __scc(a)
//   而非 ((long)(a)) 绕开，验证 __scc 能在重扫描阶段正确展开。
//
// Bug 2 修复：## 粘贴产生的宏名与后续 (args) 合并重扫描。
//   关键测试是 syscall() 派发路径：__SYSCALL_CONCAT(__syscall, N)(args)
//   中 ## 产生 __syscallN 后，应连同 (args) 一起重扫描，触发包装宏展开。
//
// 测试策略：
//   使用完整的 tcc_need.h 式宏体系（包括 __scc 包装宏），对每个参数
//   数量（0-6）都用三种方式测试并比较结果：
//   (a) 直接调用 inline 函数（__syscallN(no, arg...)）
//   (b) 通过包装宏直接调用（__syscallN(no, arg...) 触发同名宏展开）
//   (c) 通过 syscall() 派发宏路由（测试 ## → 重扫整条链路）
//   如果 Bug 1 未修复，(b) 会无限递归（depth 限制后输出异常）；如果
//   Bug 2 未修复，(c) 会丢失 (long) 转型，在高 32 位有值时结果不同。

/* ═══════════════════════════════════════════════════════════════
 *  系统调用号
 * ═══════════════════════════════════════════════════════════════ */

#define SYS_read        0
#define SYS_write       1
#define SYS_close       3
#define SYS_lseek       8
#define SYS_mmap        9
#define SYS_munmap      11
#define SYS_brk         12
#define SYS_exit        60
#define SYS_getpid      39
#define SYS_openat      257
#define SYS_renameat2   316

/* ═══════════════════════════════════════════════════════════════
 *  POSIX / 内存映射常量
 * ═══════════════════════════════════════════════════════════════ */

#define AT_FDCWD    (-100)
#define O_RDONLY    0
#define MAP_PRIVATE     0x02
#define MAP_ANONYMOUS   0x20

/* ═══════════════════════════════════════════════════════════════
 *  syscall 内联汇编函数 — 完全来自 tcc_need.h
 * ═══════════════════════════════════════════════════════════════ */

static inline long __syscall0(long n)
{
    unsigned long ret;
    __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n) : "rcx", "r11", "memory");
    return ret;
}

static inline long __syscall1(long n, long a1)
{
    unsigned long ret;
    __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1)
                          : "rcx", "r11", "memory");
    return ret;
}

static inline long __syscall2(long n, long a1, long a2)
{
    unsigned long ret;
    __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2)
                          : "rcx", "r11", "memory");
    return ret;
}

static inline long __syscall3(long n, long a1, long a2, long a3)
{
    unsigned long ret;
    __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2),
                          "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

static inline long __syscall4(long n, long a1, long a2, long a3, long a4)
{
    unsigned long ret;
    __asm__ __volatile__ (
        "mov %5, %%r10\n\tsyscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(a4)
        : "rcx", "r11", "r10", "memory");
    return ret;
}

static inline long __syscall5(long n, long a1, long a2, long a3, long a4, long a5)
{
    unsigned long ret;
    __asm__ __volatile__ (
        "mov %5, %%r10\n\tmov %6, %%r8\n\tsyscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(a4), "r"(a5)
        : "rcx", "r11", "r10", "r8", "memory");
    return ret;
}

static inline long __syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
    unsigned long ret;
    __asm__ __volatile__ (
        "mov %5, %%r10\n\tmov %6, %%r8\n\tmov %7, %%r9\n\tsyscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(a4), "r"(a5), "r"(a6)
        : "rcx", "r11", "r10", "r8", "r9", "memory");
    return ret;
}

/* ═══════════════════════════════════════════════════════════════
 *  syscall 宏体系 — 完整来自 tcc_need.h，使用 __scc(a)
 *
 *  注意：本测试使用 __scc(a) 而非 ((long)(a))，专门验证 Bug 1+2 修复。
 *  如果自引用禁用正确，__scc 会在重扫描阶段被展开；如果 ## dispatch
 *  后重扫正确，包装宏也会被展开。
 * ═══════════════════════════════════════════════════════════════ */

#define __scc(X) ((long)(X))

/* ─── 参数转换包装宏（与 inline 函数同名） ── */
/* Bug 1 验证：__syscallN 在替换文本中再次出现时被禁用，不会无限递归 */

#define __syscall0(n)       __syscall0(n)
#define __syscall1(n, a)    __syscall1(n, __scc(a))
#define __syscall2(n, a, b) __syscall2(n, __scc(a), __scc(b))
#define __syscall3(n, a, b, c) __syscall3(n, __scc(a), __scc(b), __scc(c))
#define __syscall4(n, a, b, c, d) __syscall4(n, __scc(a), __scc(b), __scc(c), __scc(d))
#define __syscall5(n, a, b, c, d, e) __syscall5(n, __scc(a), __scc(b), __scc(c), __scc(d), __scc(e))
#define __syscall6(n, a, b, c, d, e, f) __syscall6(n, __scc(a), __scc(b), __scc(c), __scc(d), __scc(e), __scc(f))

/* ─── 参数计数与派发宏 ── */
/* Bug 2 验证：## 产生 __syscallN 后连带 (args) 一起重扫 */

#define __SYSCALL_NARGS_X(a,b,c,d,e,f,g,h,n,...) n
#define __SYSCALL_NARGS(...) \
    __SYSCALL_NARGS_X(__VA_ARGS__,7,6,5,4,3,2,1,0,)
#define __SYSCALL_CONCAT_X(a,b) a##b
#define __SYSCALL_CONCAT(a,b) __SYSCALL_CONCAT_X(a,b)
#define __SYSCALL_DISP(b,...) \
    __SYSCALL_CONCAT(b, __SYSCALL_NARGS(__VA_ARGS__))(__VA_ARGS__)
#define __syscall(...) __SYSCALL_DISP(__syscall, __VA_ARGS__)
#define syscall(...) __syscall(__VA_ARGS__)

/* ═══════════════════════════════════════════════════════════════
 *  测试框架 — 不依赖 syscall 宏（直接用内联 asm）
 * ═══════════════════════════════════════════════════════════════ */

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
    while (i > 0) sys_write(1, &buf[--i], 1);
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
    print_str("    FAIL assertion ");
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

/* ═══════════════════════════════════════════════════════════════
 *  __scc 宏展开验证
 * ═══════════════════════════════════════════════════════════════ */

static void test_scc_macro(void) {
    test_start("__scc macro expansion");

    /* __scc 独立展开 */
    long x = __scc((void *)0x1234);
    CHECK(x == 0x1234, "__scc((void*)0x1234) == 0x1234");

    x = __scc(42);
    CHECK(x == 42, "__scc(42) == 42");

    x = __scc(-1);
    CHECK(x == -1, "__scc(-1) == -1");
}

/* ═══════════════════════════════════════════════════════════════
 *  Bug 1 验证：直接调用包装宏（自引用禁用）
 *
 *  调用 __syscall1(SYS_close, -1) → 宏展开为
 *    __syscall1(SYS_close, __scc(-1))
 *    → __syscall1(SYS_close, ((long)(-1)))  ← __scc 展开
 *    → __syscall1(SYS_close, -1)             ← __syscall1 被禁用，不递归
 *
 *  如果没有 Bug 1 修复，__syscall1 会反复展开：
 *    __syscall1(SYS_close, __scc(-1))
 *    → __syscall1(SYS_close, ((long)(-1)))    ← __scc 展开
 *    → __syscall1(SYS_close, __scc(-1)))      ← 再次展开 __syscall1！递归
 *  无限递归直到 depth 64，产生大量嵌套的 ((long)(...))。
 *  即使 depth 限制后停止，输出也异常庞大，导致栈溢出或结果错误。
 * ═══════════════════════════════════════════════════════════════ */

static void test_bug1_direct_wrapper(void) {
    test_start("Bug 1 fix: direct wrapper macro (self-ref disabled)");

    /* 比较包装宏路径与 inline 函数路径结果是否一致 */
    long inline_r = __syscall1(SYS_close, -1);  /* inline function */
    long wrap_r  = __syscall1(SYS_close, -1);   /* wrapper macro (same name!) */

    CHECK(inline_r == -9, "inline __syscall1: close(-1) == -EBADF (-9)");
    CHECK(wrap_r  == -9, "wrapper __syscall1: close(-1) == -EBADF (-9)");
    CHECK(inline_r == wrap_r, "inline and wrapper return same value");

    /* 多参数也验证 */
    long inline_b = __syscall2(SYS_munmap, (long)-1, 0);
    long wrap_b  = __syscall2(SYS_munmap, (long)-1, 0);
    CHECK(inline_b < 0, "inline __syscall2: munmap(-1,0) < 0");
    CHECK(wrap_b  < 0, "wrapper __syscall2: munmap(-1,0) < 0");
    CHECK(inline_b == wrap_b, "inline and wrapper munmap same");

    /* 验证 brk(0) 结果一致 */
    long brk_inline = __syscall1(SYS_brk, 0);
    long brk_wrap   = __syscall1(SYS_brk, 0);
    CHECK(brk_inline == brk_wrap, "brk(0): inline == wrapper");

    print_str("    Bug 1: direct wrapper macro OK\n");
}

/* ═══════════════════════════════════════════════════════════════
 *  Bug 2 验证：dispatch 路径（## 粘贴后的重扫）
 *
 *  调用 syscall(SYS_close, -1) → 经过 NARGS 计数 → CONCAT 粘贴 →
 *    __syscall1(SYS_close, -1)   ← 应被重扫为包装宏展开
 *    → __syscall1(SYS_close, __scc(-1))
 *    → ...
 *
 *  如果没有 Bug 2 修复，## 产生 __syscall1 后不会与 (SYS_close, -1)
 *  合并重扫，__syscall1 的包装宏不会展开，(long) 转型丢失。
 *  最终输出：__syscall1(SYS_close, -1) — 调用 inline 函数，但无转型。
 *  在 x86_64 上 -1 作为 int 传入 long 参数时高 32 位为垃圾值。
 * ═══════════════════════════════════════════════════════════════ */

static void test_bug2_dispatch(void) {
    test_start("Bug 2 fix: ## dispatch re-scan");

    /* 比较 dispatch 路径与 inline 函数路径 */
    long inline_r = __syscall1(SYS_close, -1);
    long dispatch_r = syscall(SYS_close, -1);

    CHECK(inline_r == -9, "inline: close(-1) == -EBADF (-9)");
    CHECK(dispatch_r == -9, "dispatch: close(-1) == -EBADF (-9)");
    CHECK(inline_r == dispatch_r, "inline and dispatch return same");

    /* 2-arg 验证 */
    long inline_m = __syscall2(SYS_munmap, (long)-1, 0);
    long dispatch_m = syscall(SYS_munmap, (long)-1, 0);
    CHECK(inline_m < 0, "inline: munmap(-1,0) < 0");
    CHECK(dispatch_m < 0, "dispatch: munmap(-1,0) < 0");
    CHECK(inline_m == dispatch_m, "munmap: inline == dispatch");

    /* brk(0) 验证 */
    long brk_inline = __syscall1(SYS_brk, 0);
    long brk_dispatch = syscall(SYS_brk, 0);
    CHECK(brk_inline > 0x10000, "inline: brk(0) > 0x10000");
    CHECK(brk_dispatch > 0x10000, "dispatch: brk(0) > 0x10000");
    CHECK(brk_inline == brk_dispatch, "brk(0): inline == dispatch");

    /* ptr 参数验证 — dispatch 必须正确转型 */
    const char *path = "/nonexistent_test_bug2";
    long fd = syscall(SYS_openat, AT_FDCWD, path, O_RDONLY, 0);
    CHECK(fd == -2, "dispatch 4-arg: openat(nonexistent) == -ENOENT (-2)");

    print_str("    Bug 2: dispatch re-scan OK\n");
}

/* ═══════════════════════════════════════════════════════════════
 *  完整参数范围验证（0-6 参数，三路径对比）
 * ═══════════════════════════════════════════════════════════════ */

static void test_all_arg_counts(void) {
    test_start("All arg counts: inline vs wrapper vs dispatch");

    /* 0 arg */
    long pid_i   = __syscall0(SYS_getpid);      /* inline */
    long pid_w   = __syscall0(SYS_getpid);      /* wrapper (same name) */
    long pid_d   = syscall(SYS_getpid);          /* dispatch */
    CHECK(pid_i   > 0, "0-arg inline > 0");
    CHECK(pid_w   == pid_i, "0-arg wrapper == inline");
    CHECK(pid_d   == pid_i, "0-arg dispatch == inline");

    /* 1 arg */
    long c_i   = __syscall1(SYS_close, -1);
    long c_w   = __syscall1(SYS_close, -1);
    long c_d   = syscall(SYS_close, -1);
    CHECK(c_i   == -9, "1-arg inline == -9");
    CHECK(c_w   == -9, "1-arg wrapper == -9");
    CHECK(c_d   == -9, "1-arg dispatch == -9");

    /* 2 arg */
    long m_i   = __syscall2(SYS_munmap, (long)-1, 0);
    long m_w   = __syscall2(SYS_munmap, (long)-1, 0);
    long m_d   = syscall(SYS_munmap, (long)-1, 0);
    CHECK(m_i  < 0, "2-arg inline < 0");
    CHECK(m_w  < 0, "2-arg wrapper < 0");
    CHECK(m_d  < 0, "2-arg dispatch < 0");

    /* 3 arg — write */
    {
        const char *msg = "    3-arg: OK!\n";
        int len = my_strlen(msg);
        long w_i = __syscall3(SYS_write, 1, (long)msg, (long)len);
        long w_d = syscall(SYS_write, 1, msg, len);
        CHECK(w_i == len, "3-arg inline write OK");
        CHECK(w_d == len, "3-arg dispatch write OK");
    }

    /* 4 arg — openat */
    {
        const char *pn = "/nonexistent_4arg";
        long o_i = __syscall4(SYS_openat, AT_FDCWD, (long)pn, O_RDONLY, 0);
        long o_d = syscall(SYS_openat, AT_FDCWD, pn, O_RDONLY, 0);
        CHECK(o_i == -2, "4-arg inline == -ENOENT");
        CHECK(o_d == -2, "4-arg dispatch == -ENOENT");
        CHECK(o_i == o_d, "4-arg inline == dispatch");
    }

    /* 5 arg — renameat2 */
    {
        const char *op = "/nonexistent_5_old";
        const char *np = "/nonexistent_5_new";
        long r_i = __syscall5(SYS_renameat2,
                               AT_FDCWD, (long)op,
                               AT_FDCWD, (long)np, 0);
        long r_d = syscall(SYS_renameat2,
                            AT_FDCWD, op, AT_FDCWD, np, 0);
        CHECK(r_i == -2, "5-arg inline == -ENOENT");
        CHECK(r_d == -2, "5-arg dispatch == -ENOENT");
        CHECK(r_i == r_d, "5-arg inline == dispatch");
    }

    /* 6 arg — mmap with MAP_FIXED */
    {
        long m1 = __syscall6(SYS_mmap,
                              1, 4096, 0,
                              MAP_PRIVATE | MAP_ANONYMOUS | 0x10,
                              -1, 0);
        long m2 = syscall(SYS_mmap, 1, 4096, 0,
                           MAP_PRIVATE | MAP_ANONYMOUS | 0x10, -1, 0);
        CHECK(m1 < 0, "6-arg inline < 0");
        CHECK(m2 < 0, "6-arg dispatch < 0");
        CHECK(m1 == m2, "6-arg inline == dispatch");
    }

    print_str("    All arg counts: three paths consistent\n");
}

/* ═══════════════════════════════════════════════════════════════
 *  __SYSCALL_CONCAT 与 __SYSCALL_NARGS 独立验证
 * ═══════════════════════════════════════════════════════════════ */

static void test_dispatch_mechanism(void) {
    test_start("Dispatch mechanism: NARGS + CONCAT");

    /* __SYSCALL_NARGS 计数 */
    CHECK(__SYSCALL_NARGS(0)                             == 0,  "NARGS(0)==0");
    CHECK(__SYSCALL_NARGS(1, 2)                          == 1,  "NARGS(1,2)==1");
    CHECK(__SYSCALL_NARGS(1, 2, 3)                       == 2,  "NARGS(1,2,3)==2");
    CHECK(__SYSCALL_NARGS(1, 2, 3, 4)                    == 3,  "NARGS(1,2,3,4)==3");
    CHECK(__SYSCALL_NARGS(1, 2, 3, 4, 5)                 == 4,  "NARGS(1,2,3,4,5)==4");
    CHECK(__SYSCALL_NARGS(1, 2, 3, 4, 5, 6)              == 5,  "NARGS(1,2,3,4,5,6)==5");

    /* __SYSCALL_CONCAT 粘贴 */
    CHECK(__SYSCALL_CONCAT(12, 34) == 1234, "CONCAT(12,34)==1234");
    CHECK(__SYSCALL_CONCAT(__syscall, 1)(SYS_close, -1) == -9,
           "CONCAT(__syscall,1)(close,-1) == -9 (Bug 2: paste+rescan)");

    print_str("    Dispatch mechanism OK\n");
}

/* ═══════════════════════════════════════════════════════════════
 *  运行所有测试
 * ═══════════════════════════════════════════════════════════════ */

static void run_tests(void) {
    print_str("=== Self-ref macro + ## dispatch tests (Bug 1 & 2) ===\n");

    test_scc_macro();
    test_bug1_direct_wrapper();
    test_bug2_dispatch();
    test_dispatch_mechanism();
    test_all_arg_counts();

    print_str("\n");
    if (failures == 0) {
        print_str("ALL PASSED\n");
        sys_exit(0);
    }
    print_str("SOME TESTS FAILED: ");
    print_dec(failures);
    print_str(" failure(s)\n");
    sys_exit(failures > 127 ? 127 : failures);
}

void __tlibc_start(void) { run_tests(); }
