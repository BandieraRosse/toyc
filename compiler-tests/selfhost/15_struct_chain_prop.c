// EXPECT: 0
// struct_chain.c — 验证结构体链式成员访问编译正确性
//
// 测试场景：通过 -> 连续访问 struct 成员（p->a->b），验证代码生成器
// 正确传播 struct_type 并计算成员偏移。

typedef unsigned long size_t;
typedef long ptrdiff_t;

static long sys_write(int fd, const void *buf, unsigned long len) {
    unsigned long ret;
    __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(1), "D"((long)fd), "S"(buf), "d"((long)len) : "rcx", "r11", "memory");
    return (long)ret;
}
static void sys_exit(int code) {
    __asm__ __volatile__ ("syscall" : : "a"((long)60), "D"((long)code) : "rcx", "r11", "memory");
    for (;;) ;
}

static void pstr(const char *s) { int n=0; while(s[n]) n++; sys_write(1, s, n); }
static void pdec(long v) {
    char b[32]; int i=30; b[31]='\n';
    if (v<0) { pstr("-"); v=-v; }
    if (v==0) b[--i]='0';
    while (v>0 && i>0) { b[--i]='0'+(v%10); v/=10; }
    sys_write(1, b+i, 31-i);
}

static int passed = 0, failed = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { pstr("  FAIL "); pstr(msg); pstr("\n"); failed++; } \
    else { passed++; } \
} while(0)

/* ─── 测试用的结构体定义 ─── */

/* 自引用结构体：类似 AstNode 的简化版，逗号分隔成员 */
typedef struct Node {
    int kind;
    struct Node *next;
    const char *name;
    struct Node *body, *expr;   /* 逗号分隔 */
    long ival;
    struct Node *left, *right;  /* 逗号分隔指针 */
    int is_float;
    int type_size;
} Node;

/* 另一个含有同名成员的结构体——用于验证链式传播而非全局回退 */
typedef struct Other {
    int is_float;
    const char *name;
    struct Other *peer;
} Other;

/* ─── 测试 1：简单链式访问（p->right->is_float） ─── */
static void test_chain_is_float(void) {
    Node a, b;
    a.kind = 100;
    a.left = 0;
    a.right = &b;
    b.is_float = 42;
    b.kind = 200;

    /* 链式读取 */
    int val = a.right->is_float;
    CHECK(val == 42, "p->right->is_float = 42");
}

/* ─── 测试 2：链式访问 name（p->left->name） ─── */
static void test_chain_name(void) {
    Node a, b;
    a.left = &b;
    a.right = 0;
    b.name = "hello";

    const char *n = a.left->name;
    CHECK(n != 0, "p->left->name != NULL");
    if (n) {
        CHECK(n[0] == 'h' && n[1] == 'e' && n[2] == 'l', "p->left->name = 'hello'");
    }
}

/* ─── 测试 3：多级链式访问（p->right->left->is_float） ─── */
static void test_chain_multi(void) {
    Node a, b, c;
    a.right = &b;
    b.left = &c;
    c.is_float = 77;

    int val = a.right->left->is_float;
    CHECK(val == 77, "p->right->left->is_float = 77");
}

/* ─── 测试 4：有歧义时仍正确（Other 也有 is_float） ─── */
static void test_chain_ambiguous(void) {
    Node a, b;
    Other o;
    o.is_float = 999;
    a.right = &b;
    b.is_float = 55;

    int val = a.right->is_float;
    CHECK(val == 55, "ambiguous p->right->is_float = 55 (not from Other)");
}

/* ─── 测试 5：逗号分隔成员的链式访问 ─── */
static void test_chain_comma_member(void) {
    Node a, b;
    a.expr = &b;
    b.ival = 12345;

    long val = a.expr->ival;
    CHECK(val == 12345, "p->expr->ival = 12345 (comma member chain)");
}

void __tlibc_start(void) {
    pstr("=== struct_chain tests ===\n");

    test_chain_is_float();      pstr(".");
    test_chain_name();          pstr(".");
    test_chain_multi();         pstr(".");
    test_chain_ambiguous();     pstr(".");
    test_chain_comma_member();  pstr(".");

    pstr("\n");
    pdec(passed); pstr(" passed, "); pdec(failed); pstr(" failed\n");

    sys_exit(failed ? 1 : 0);
}
