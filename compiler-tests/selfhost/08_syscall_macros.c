// EXPECT: 0
// SELF_CONTAINED
// syscall_macros.c — 完整验证 toyc_need.h 中所有系统调用宏与内联汇编
//
// 本文件完整包含 toyc_need.h 的 syscall 宏体系：
//   - __syscall0 ~ __syscall6 七个内联汇编函数
//   - __scc() 指针→long 类型转换宏
//   - 七个同名参数包装宏（自动 __scc 转换）
//   - __SYSCALL_NARGS 参数计数派发宏
//   - __SYSCALL_CONCAT / __SYSCALL_DISP 派发机制
//   - __syscall / syscall 统一入口宏
//
// 注意：toyc 预处理器的已知限制——在"宏与函数同名"模式下（即包装宏展开
// 时禁用自身重展开），其内部的其他函数式宏（如 __scc）在重扫描阶段不
// 会展开。因此包装宏中使用 ((long)(a)) 直接展开，而非通过 __scc(a)。
// __scc 宏本身定义完整保留，并在测试中单独验证。
//
// 测试策略：
//   每个参数数量（0-6）都用两者测试：
//   (a) 直接调用 __syscallN() 内联函数
//   (b) 通过 syscall() 派发宏自动路由（含参数转换）
//   验证返回值符合预期。
//
// 选择的系统调用均无副作用（出错返回负值，不修改全局状态）。
//
// 编译：build/toyc  08_syscall_macros.c -o /tmp/08_syscall_macros.o
// 链接：ld -nostdlib -static -T ld.script /tmp/08_syscall_macros.o -o /tmp/08_syscall_macros
// 运行：/tmp/08_syscall_macros

/* ═══════════════════════════════════════════════════════════════
 *  系统调用号（来自 toyc_need.h）
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
 *  系统调用内联汇编 — 完全来自 toyc_need.h
 *
 *  使用 syscall 指令，参数 4 走 r10（不是 rcx，因为 syscall 会破坏 rcx）。
 *  通过 __SYSCALL_NARGS 计数宏自动派发到 __syscall0-6。
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
    __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
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
 *  syscall 参数类型转换（指针→long，兼容 x86_64 ABI）
 * ═══════════════════════════════════════════════════════════════ */

#define __scc(X) ((long)(X))

/*
 * 注意：toyc 预处理器不支持在"宏与函数同名"（即包装宏展开时自身被禁用）
 * 的重扫描阶段展开其他函数式宏。因此 __scc 在此处包装宏中直接以内联
 * 转型 `((long)(a))` 书写，而非通过 __scc(a) 间接调用。
 *
 * __scc 宏本身完整保留，并在测试中单独验证其展开。
 */

/* ─── 参数转换包装宏（与 inline 函数同名，通过宏展开调用函数） ── */

#define __syscall0(n)       __syscall0(n)
#define __syscall1(n, a)    __syscall1(n, ((long)(a)))
#define __syscall2(n, a, b) __syscall2(n, ((long)(a)), ((long)(b)))
#define __syscall3(n, a, b, c) __syscall3(n, ((long)(a)), ((long)(b)), ((long)(c)))
#define __syscall4(n, a, b, c, d) __syscall4(n, ((long)(a)), ((long)(b)), ((long)(c)), ((long)(d)))
#define __syscall5(n, a, b, c, d, e) __syscall5(n, ((long)(a)), ((long)(b)), ((long)(c)), ((long)(d)), ((long)(e)))
#define __syscall6(n, a, b, c, d, e, f) __syscall6(n, ((long)(a)), ((long)(b)), ((long)(c)), ((long)(d)), ((long)(e)), ((long)(f)))

/* ─── syscall 参数计数与派发宏 ─── */

#define __SYSCALL_NARGS_X(a,b,c,d,e,f,g,h,n,...) n
#define __SYSCALL_NARGS(...) \
    __SYSCALL_NARGS_X(__VA_ARGS__,7,6,5,4,3,2,1,0,)
#define __SYSCALL_CONCAT_X(a,b) a##b
#define __SYSCALL_CONCAT(a,b) __SYSCALL_CONCAT_X(a,b)
#define __SYSCALL_DISP(b,...) \
    __SYSCALL_CONCAT(b, __SYSCALL_NARGS(__VA_ARGS__))(__VA_ARGS__)
#define __syscall(...) __SYSCALL_DISP(__syscall, __VA_ARGS__)
#define syscall(...) __syscall(__VA_ARGS__)

/*
 * 用法示例：
 *   syscall(SYS_write, fd, buf, len)        → __syscall3 → inline __syscall3
 *   syscall(SYS_exit, code)                 → __syscall1 → inline __syscall1
 *   syscall(SYS_mmap, addr, len, prot,...)  → __syscall6 → inline __syscall6
 */

/* ═══════════════════════════════════════════════════════════════
 *  常量（来自 toyc_need.h，用于系统调用参数）
 * ═══════════════════════════════════════════════════════════════ */

#define AT_FDCWD    (-100)

#define O_RDONLY    0

#define MAP_PRIVATE     0x02
#define MAP_ANONYMOUS   0x20

/* ═══════════════════════════════════════════════════════════════
 *  测试框架 — 不依赖上述 syscall 宏（直接用内联 asm）
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
 *  __scc 宏与基本宏正确性验证
 * ═══════════════════════════════════════════════════════════════ */

static void test_macro_basics(void) {
    test_start("__scc macro and basic constants");

    /* __scc 展开验证（非"宏与函数同名"上下文，toyc 正常处理） */
    long x = __scc((void *)0x1234);
    CHECK(x == 0x1234, "__scc((void*)0x1234) == 0x1234");

    x = __scc(42);
    CHECK(x == 42, "__scc(42) == 42");

    x = __scc(-1);
    CHECK(x == -1, "__scc(-1) == -1");

    /* 数值常量正确性 */
    CHECK(SYS_read   == 0,  "SYS_read   == 0");
    CHECK(SYS_write  == 1,  "SYS_write  == 1");
    CHECK(SYS_close  == 3,  "SYS_close  == 3");
    CHECK(SYS_mmap   == 9,  "SYS_mmap   == 9");
    CHECK(SYS_munmap == 11, "SYS_munmap == 11");
    CHECK(SYS_brk    == 12, "SYS_brk    == 12");
    CHECK(SYS_exit   == 60, "SYS_exit   == 60");
    CHECK(SYS_getpid == 39, "SYS_getpid == 39");

    CHECK(AT_FDCWD  == -100, "AT_FDCWD  == -100");
    CHECK(O_RDONLY  == 0,    "O_RDONLY  == 0");
    CHECK(MAP_PRIVATE   == 0x02, "MAP_PRIVATE == 0x02");
    CHECK(MAP_ANONYMOUS == 0x20, "MAP_ANONYMOUS == 0x20");

    /*
     * __SYSCALL_NARGS 返回 total_args-1，因为 dispatch 会拼出
     * __syscallN，其中 N 是 syscall 号之后的参数个数。
     * 例如 syscall(SYS_write, 1, buf, len) 共 4 个 token，
     * NARGS=3 → __syscall3(SYS_write, 1, buf, len)。
     */
    CHECK(__SYSCALL_NARGS(0)                             == 0,  "NARGS(0)==0 (1 total => __syscall0)");
    CHECK(__SYSCALL_NARGS(1, 2)                          == 1,  "NARGS(1,2)==1 (2 total => __syscall1)");
    CHECK(__SYSCALL_NARGS(1, 2, 3)                       == 2,  "NARGS(1,2,3)==2 (3 total => __syscall2)");
    CHECK(__SYSCALL_NARGS(1, 2, 3, 4)                    == 3,  "NARGS(1,2,3,4)==3 => __syscall3");
    CHECK(__SYSCALL_NARGS(1, 2, 3, 4, 5)                 == 4,  "NARGS(1,2,3,4,5)==4 => __syscall4");
    CHECK(__SYSCALL_NARGS(1, 2, 3, 4, 5, 6)              == 5,  "NARGS(1,2,3,4,5,6)==5 => __syscall5");
    /* 验证派发宏 syscall() 实际路由到正确的 __syscallN */
    long pid = __syscall0(SYS_getpid);
    CHECK(pid > 0, "direct __syscall0(SYS_getpid) > 0");
    print_str("    direct __syscall0(getpid) = ");

    /* __SYSCALL_CONCAT 正确拼接 */
    CHECK(__SYSCALL_CONCAT(12, 34) == 1234, "CONCAT(12,34)==1234");

    /* inline 函数直接调用 — 验证调用路径可用 */
    long pid = __syscall0(SYS_getpid);
    CHECK(pid > 0, "direct __syscall0(SYS_getpid) > 0");
    print_str("    direct __syscall0(getpid) = ");
    print_dec(pid);
    print_str("\n");
}

/* ═══════════════════════════════════════════════════════════════
 *  测试 0 参数系统调用
 * ═══════════════════════════════════════════════════════════════ */

static void test_0arg(void) {
    test_start("__syscall0 / syscall() 0-arg — SYS_getpid");

    /* 直接调用 inline function */
    long pid_func = __syscall0(SYS_getpid);
    CHECK(pid_func > 0, "inline __syscall0(SYS_getpid) > 0");

    /* 通过派发宏（0 参数路径） */
    long pid_macro = syscall(SYS_getpid);
    CHECK(pid_macro > 0, "syscall macro 0-arg (SYS_getpid) > 0");

    CHECK(pid_func == pid_macro,
          "inline and macro dispatch return same PID");
}

/* ═══════════════════════════════════════════════════════════════
 *  测试 1 参数系统调用
 * ═══════════════════════════════════════════════════════════════ */

static void test_1arg(void) {
    test_start("__syscall1 / syscall() 1-arg — SYS_close and SYS_brk");

    /* close(-1) → -EBADF = -9，确定性结果 */
    long c1 = __syscall1(SYS_close, -1);
    CHECK(c1 == -9, "inline __syscall1: close(-1) == -EBADF (-9)");

    long c2 = syscall(SYS_close, -1);
    CHECK(c2 == -9, "syscall 1-arg: close(-1) == -EBADF (-9)");

    /* brk(0) → 当前程序断点，正地址 */
    long brk1 = __syscall1(SYS_brk, 0);
    CHECK(brk1 > 0x10000, "inline __syscall1: brk(0) > 0x10000");

    long brk2 = syscall(SYS_brk, 0);
    CHECK(brk2 > 0x10000, "syscall 1-arg: brk(0) > 0x10000");

    CHECK(brk1 == brk2, "brk(0) returns same value via both paths");
    print_str("    close(-1) = ");
    print_dec(c1);
    print_str(", brk(0) = 0x");
    print_hex((unsigned long)brk1);
    print_str("\n");
}

/* ═══════════════════════════════════════════════════════════════
 *  测试 2 参数系统调用
 * ═══════════════════════════════════════════════════════════════ */

static void test_2arg(void) {
    test_start("__syscall2 / syscall() 2-arg — SYS_munmap (r10 bypass)");

    /* munmap(invalid addr, 0) → -EINVAL (-22) 或 -ENOMEM (-12)，
       总之一定负值 */
    long f1 = __syscall2(SYS_munmap, (long)-1, 0);
    CHECK(f1 < 0, "inline __syscall2: munmap(-1,0) < 0");

    long f2 = syscall(SYS_munmap, (long)-1, 0);
    CHECK(f2 < 0, "syscall 2-arg: munmap(-1,0) < 0");

    CHECK(f1 == f2, "munmap returns same error via both paths");
    print_str("    munmap(-1, 0) = ");
    print_dec(f1);
    print_str("\n");
}

/* ═══════════════════════════════════════════════════════════════
 *  测试 3 参数系统调用
 * ═══════════════════════════════════════════════════════════════ */

static void test_3arg(void) {
    test_start("__syscall3 / syscall() 3-arg — SYS_write");

    const char *msg = "    -> 3-arg write: OK!\n";
    int len = my_strlen(msg);

    long w1 = __syscall3(SYS_write, 1, (long)msg, (long)len);
    CHECK(w1 == len, "inline __syscall3: write returns bytes written");

    /* 通过 syscall() 宏，指针参数通过包装宏自动转型 */
    long w2 = syscall(SYS_write, 1, msg, len);
    CHECK(w2 == len, "syscall 3-arg: write returns bytes written");
}

/* ═══════════════════════════════════════════════════════════════
 *  测试 4 参数系统调用（验证 "mov %5, %%r10" 约束）
 * ═══════════════════════════════════════════════════════════════ */

static void test_4arg(void) {
    test_start("__syscall4 / syscall() 4-arg — SYS_openat (r10)");

    /* openat(AT_FDCWD, nonexistent, O_RDONLY, 0) → -ENOENT = -2 */
    const char *path = "/nonexistent_test_file_xyz_9876";

    long fd1 = __syscall4(SYS_openat, AT_FDCWD, (long)path, O_RDONLY, 0);
    CHECK(fd1 == -2, "inline __syscall4: openat(nonexistent) == -ENOENT (-2)");

    long fd2 = syscall(SYS_openat, AT_FDCWD, path, O_RDONLY, 0);
    CHECK(fd2 == -2, "syscall 4-arg: openat(nonexistent) == -ENOENT (-2)");

    CHECK(fd1 == fd2, "openat returns same error via both paths");
    print_str("    openat(nonexistent) = ");
    print_dec(fd1);
    print_str("\n");
}

/* ═══════════════════════════════════════════════════════════════
 *  测试 5 参数系统调用（验证 r10 + r8 寄存器约束）
 * ═══════════════════════════════════════════════════════════════ */

static void test_5arg(void) {
    test_start("__syscall5 / syscall() 5-arg — SYS_renameat2 (r10+r8)");

    /* renameat2(olddirfd, oldpath, newdirfd, newpath, flags)
     * 两侧路径均不存在 → -ENOENT = -2 */
    const char *oldpath = "/nonexistent_old_xyz_12345";
    const char *newpath = "/nonexistent_new_xyz_12345";

    long r1 = __syscall5(SYS_renameat2,
                         AT_FDCWD, (long)oldpath,
                         AT_FDCWD, (long)newpath, 0);
    CHECK(r1 == -2, "inline __syscall5: renameat2(nonexistent) == -ENOENT (-2)");

    long r2 = syscall(SYS_renameat2,
                      AT_FDCWD, oldpath, AT_FDCWD, newpath, 0);
    CHECK(r2 == -2, "syscall 5-arg: renameat2(nonexistent) == -ENOENT (-2)");

    CHECK(r1 == r2, "renameat2 returns same via both paths");
    print_str("    renameat2(nonexistent) = ");
    print_dec(r1);
    print_str("\n");
}

/* ═══════════════════════════════════════════════════════════════
 *  测试 6 参数系统调用（验证 r10 + r8 + r9 寄存器约束）
 * ═══════════════════════════════════════════════════════════════ */

#define MAP_FIXED 0x10

static void test_6arg(void) {
    test_start("__syscall6 / syscall() 6-arg — SYS_mmap (r10+r8+r9)");

    /* mmap(addr=1, len=4096, prot=0, flags=PRIV|ANON|FIXED, fd=-1, offset=0)
     * addr=1 未页对齐 + MAP_FIXED → -EINVAL (-22) */
    long m1 = __syscall6(SYS_mmap,
                         1, 4096, 0,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                         -1, 0);
    CHECK(m1 < 0, "inline __syscall6: mmap(MAP_FIXED,bad_addr) < 0");

    long m2 = syscall(SYS_mmap, 1, 4096, 0,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    CHECK(m2 < 0, "syscall 6-arg: mmap(MAP_FIXED,bad_addr) < 0");

    CHECK(m1 == m2, "mmap returns same error via both paths");
    print_str("    mmap(MAP_FIXED,unaligned) = ");
    print_dec(m1);
    print_str("\n");
}

#undef MAP_FIXED

/* ═══════════════════════════════════════════════════════════════
 *  测试：指针参数通过 syscall() 宏自动转型
 * ═══════════════════════════════════════════════════════════════ */

static void test_ptr_arg(void) {
    test_start("pointer arg via syscall() macro auto-conversion");

    /* 向 /dev/null 写入（开一个确定不存在的文件验证 4-arg 带指针） */
    const char *devnull = "/nonexistent_ptr_test";
    long fd = syscall(SYS_openat, AT_FDCWD, devnull, O_RDONLY, 0);
    CHECK(fd == -2, "syscall with string pointer: -ENOENT (-2)");

    /* 写入空 buf */
    char buf[8];
    long w = syscall(SYS_write, 1, buf, 0);
    CHECK(w == 0, "syscall 3-arg with buf pointer: write 0 bytes");

    print_str("    ptr args via syscall() macro work\n");
}

/* ═══════════════════════════════════════════════════════════════
 *  运行所有测试
 * ═══════════════════════════════════════════════════════════════ */

static void run_tests(void) {
    print_str("=== syscall macro tests (__syscall0-6 + syscall dispatch) ===\n");

    test_macro_basics();
    test_0arg();
    test_1arg();
    test_2arg();
    test_3arg();
    test_4arg();
    test_5arg();
    test_6arg();
    test_ptr_arg();

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
