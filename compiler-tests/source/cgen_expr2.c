// EXPECT: 0
//
// cgen_expr2.c — 测试结构体返回值各种赋值路径
//
// 覆盖场景：
//   1. 直接赋值：Pair p = make(1, 2)
//   2. 花括号初始化数组：Pair arr[] = { make(3, 4) }
//   3. 多元素花括号初始化：Pair arr2[] = { make(5, 6), make(7, 8) }
//   4. 大结构体（>16 字节）直接赋值
//   5. 大结构体数组花括号初始化
//   6. 小结构体（≤8 字节，RAX 返回）赋值

// ============================================================
// Inlined from tcc_need.h — 最小类型和系统调用
// ============================================================

typedef unsigned long size_t;
typedef long ptrdiff_t;
typedef long off_t;
typedef unsigned int mode_t;
#ifndef NULL
#define NULL ((void *)0)
#endif

static void sys_exit(int code) {
    __asm__ __volatile__ ("syscall"
        : : "a"((long)60), "D"((long)code)
        : "rcx", "r11", "memory");
    for (;;) ;
}

static long sys_write(int fd, const char *buf, unsigned long len) {
    unsigned long ret;
    __asm__ __volatile__ ("syscall"
        : "=a"(ret) : "a"(1), "D"((long)fd), "S"((long)buf), "d"((long)len)
        : "rcx", "r11", "memory");
    return (long)ret;
}

void print_str(const char *s) {
    int n = 0; while (s[n]) n++;
    sys_write(1, s, n);
}

void print_dec(long n) {
    char buf[32]; int i = 0;
    if (n < 0) { print_str("-"); n = -n; }
    if (n == 0) { buf[i++] = '0'; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) {
        long ch = buf[--i];
        sys_write(1, (const char*)&ch, 1);
    }
}

// ============================================================
// 测试用结构体类型
// ============================================================

/* 16 字节结构体 */
typedef struct {
    long a, b;
} Pair;

/* 32 字节结构体 */
typedef struct {
    long a, b, c, d;
} Quad;

/* 4 字节小结构体 */
typedef struct {
    int val;
} Small;

// ============================================================
// 工厂函数（按值返回结构体）
// ============================================================

static Pair make_pair(long x, long y) {
    Pair p; p.a = x; p.b = y; return p;
}

static Quad make_quad(long a, long b, long c, long d) {
    Quad q; q.a = a; q.b = b; q.c = c; q.d = d; return q;
}

static Small make_small(int v) {
    Small s; s.val = v; return s;
}

// ============================================================
// 测试用例
// ============================================================

static int failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        print_str("  FAIL: "); print_str(msg); print_str("\n"); \
        failed = 1; \
    } \
} while (0)

void test_direct_assign(void) {
    print_str("--- Direct Assign ---\n");

    Pair p = make_pair(1, 2);
    CHECK(p.a == 1, "p.a == 1");
    CHECK(p.b == 2, "p.b == 2");

    Quad q = make_quad(10, 20, 30, 40);
    CHECK(q.a == 10, "q.a == 10");
    CHECK(q.b == 20, "q.b == 20");
    CHECK(q.c == 30, "q.c == 30");
    CHECK(q.d == 40, "q.d == 40");

    Small s = make_small(99);
    CHECK(s.val == 99, "s.val == 99");
}

void test_brace_array(void) {
    print_str("--- Brace-init Array ---\n");

    Pair arr[] = { make_pair(3, 4) };
    CHECK(arr[0].a == 3, "arr[0].a == 3");
    CHECK(arr[0].b == 4, "arr[0].b == 4");

    Pair arr2[] = { make_pair(5, 6), make_pair(7, 8) };
    CHECK(arr2[0].a == 5, "arr2[0].a == 5");
    CHECK(arr2[0].b == 6, "arr2[0].b == 6");
    CHECK(arr2[1].a == 7, "arr2[1].a == 7");
    CHECK(arr2[1].b == 8, "arr2[1].b == 8");
}

void test_large_brace(void) {
    print_str("--- Large Struct Brace-init ---\n");

    /* 32 字节结构体数组 */
    Quad quads[] = { make_quad(1, 2, 3, 4), make_quad(5, 6, 7, 8) };
    CHECK(quads[0].a == 1, "quads[0].a == 1");
    CHECK(quads[0].b == 2, "quads[0].b == 2");
    CHECK(quads[0].c == 3, "quads[0].c == 3");
    CHECK(quads[0].d == 4, "quads[0].d == 4");
    CHECK(quads[1].a == 5, "quads[1].a == 5");
    CHECK(quads[1].d == 8, "quads[1].d == 8");
}

void test_mixed_assign(void) {
    print_str("--- Mixed / Chained ---\n");

    /* 数组元素赋值 */
    Pair arr[2];
    arr[0] = make_pair(11, 22);
    arr[1] = make_pair(33, 44);
    CHECK(arr[0].a == 11, "arr[0].a == 11");
    CHECK(arr[0].b == 22, "arr[0].b == 22");
    CHECK(arr[1].a == 33, "arr[1].a == 33");
    CHECK(arr[1].b == 44, "arr[1].b == 44");
}

void test_small_ret(void) {
    print_str("--- Small Struct (≤8 bytes) ---\n");

    Small s = make_small(42);
    CHECK(s.val == 42, "s.val == 42");

    /* 通过函数返回再赋值 */
    Small arr[] = { make_small(1), make_small(2) };
    CHECK(arr[0].val == 1, "arr[0].val == 1");
    CHECK(arr[1].val == 2, "arr[1].val == 2");
}

// ============================================================
// 入口
// ============================================================

void __tlibc_start(void) {
    test_direct_assign();
    test_brace_array();
    test_large_brace();
    test_mixed_assign();
    test_small_ret();

    print_str("\n");
    if (failed) {
        print_str("SOME TESTS FAILED\n");
        sys_exit(1);
    } else {
        print_str("ALL PASSED\n");
        sys_exit(0);
    }
}
