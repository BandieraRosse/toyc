// EXPECT: 0
// 29_struct_return_expr.c — struct 按值返回 + 链式成员访问
//
// 覆盖场景：
//   A) func().member          — 在函数返回值上直接访问成员（仅首个成员可用）
//   B) local = func();        — 临时量中转后访问（所有成员都可用）
//   C) func().first_member    — 多次调用 + 首位成员直接访问
//
// Bug 历史：
//   - 2026-07-08: func().kind 读垃圾值（hidden pointer ABI 问题）。
//     Token t = func(); t.kind 可正常工作。
//   - 2026-07-09: 9e17a3e 修复 struct 返回类型追踪，
//     func() 节点的 struct_type 被正确记录，func().kind 可正常解析。
//     ca5bb66 补充测试注释。
//
// 以下限制已修复（2026-07-10）：
//   func().non_first_member — 改用寄存器传偏移，避免 push 覆盖隐藏缓冲区
//   func().array_member[idx] — 同上，cgen_addr 路径也同步修复
//
// 以下模式被测试验证为可用：
//   make_tok(42).kind              ✅ func().first_member
//   struct T t = func(); t.member  ✅ 临时量中转
//   make_med(1,2,3).b              ✅ func().first_member (首个为 long)

static void sys_exit(int code) {
    __asm__ __volatile__ ("syscall"
        : : "a"((long)60), "D"((long)code)
        : "rcx", "r11", "memory");
    for (;;) ;
}

/* 大结构体（>8字节，触发 hidden pointer ABI） */
struct BigToken {
    int kind;
    long pad[5];  /* 40 bytes → total 48 bytes */
};

static struct BigToken make_tok(int k) {
    struct BigToken t;
    t.kind = k;
    t.pad[0] = 1; t.pad[1] = 2; t.pad[2] = 3;
    t.pad[3] = 4; t.pad[4] = 5;
    return t;
}

/* 不同布局 */
struct Medium {
    long b;
    int a;
    short c;
};

static struct Medium make_med(long vb, int va, short vc) {
    struct Medium r;
    r.b = vb; r.a = va; r.c = vc;
    return r;
}

void __tlibc_start(void) {
    /* Test A: func().first_member */
    if (make_tok(42).kind != 42) sys_exit(1);

    /* Test B: local = func(); local.member（所有成员） */
    struct BigToken t = make_tok(99);
    if (t.kind != 99) sys_exit(2);
    if (t.pad[4] != 5) sys_exit(3);

    /* Test C: func().first_member（多次） */
    if (make_tok(77).kind != 77) sys_exit(4);

    /* Test D: 负数成员（临时量中转） */
    struct BigToken tn = make_tok(-1);
    if (tn.kind != -1) sys_exit(5);

    /* Test E: 另一种 struct 的首位成员 */
    if (make_med(100L, 200, (short)300).b != 100L) sys_exit(6);
    if (make_med(-10L, -20, (short)-30).b != -10L) sys_exit(7);
    if (make_med(1L, 2, (short)3).b != 1L) sys_exit(8);

    /* Test F: 非首成员经由 local 中转 */
    struct Medium m = make_med(10L, 20, (short)30);
    if (m.a != 20) sys_exit(9);
    if (m.c != 30) sys_exit(10);

    /* Test G: 大结构体数组元素（local 中转） */
    struct BigToken tx = make_tok(55);
    if (tx.pad[0] != 1) sys_exit(11);
    if (tx.pad[1] != 2) sys_exit(12);
    if (tx.pad[3] != 4) sys_exit(13);
    if (tx.pad[4] != 5) sys_exit(14);

    /* Test H: func().first_member 连续调用 */
    if (make_tok(111).kind != 111) sys_exit(15);

    /* Test I: func().non_first_member（原来阻塞的 bug） */
    if (make_med(10L, 20, (short)30).a != 20) sys_exit(16);
    if (make_med(10L, 20, (short)30).c != 30) sys_exit(17);
    if (make_med(-10L, -20, (short)-30).a != -20) sys_exit(18);
    if (make_med(1L, 2, (short)3).c != 3) sys_exit(19);

    /* Test J: func().array_member[idx]（原来阻塞的 bug） */
    if (make_tok(55).pad[4] != 5) sys_exit(20);
    if (make_tok(55).pad[0] != 1) sys_exit(21);
    if (make_tok(55).pad[2] != 3) sys_exit(22);

    sys_exit(0);
}
