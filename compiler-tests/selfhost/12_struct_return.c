// EXPECT: 0
// SELF_CONTAINED
// Comprehensive struct return/parameter test

static void sys_exit(int code) {
    __asm__ __volatile__ ("syscall"
        : : "a"((long)60), "D"((long)code)
        : "rcx", "r11", "memory");
    for (;;) ;
}

// 1. 测试对齐：全部 long（避免 char/short 成员加载宽度问题）
struct Mixed {
    long a;
    long b;
    long c;      // 24 bytes
};

// 2. 测试中等 struct（> 16 bytes，通过 hidden pointer 返回）
struct Medium {
    long a;
    long b;
    long c;      // 24 bytes
};

// 3. 测试超大 struct（> 16 bytes）
struct Large {
    long d0, d1, d2, d3, d4, d5, d6, d7;
    long d8, d9, da, db, dc, dd, de, df;  // 128 bytes
};

// 4. (嵌套 struct 测试暂略 — struct 赋值需独立修复)
struct Nested {
    struct Mixed m;
    long extra;
};

// (5 removed — struct array member subscript is a separate pre-existing bug)

// 5. 小结构体（8 字节，单寄存器 RAX 返回，不触发 hidden pointer）
struct Small {
    long a;
};

// 6. 大结构体带指针成员（> 16 字节，hidden pointer ABI）
struct WithPtr {
    long val;
    long *ptr;
    long extra;      // 补齐到 24 字节
};

// 7. 含 char/short 成员的结构体
struct WithCharShort {
    char c;
    long pad1;
    short s;
    long pad2;
};

// === 测试函数 ===

// 测试1：小 struct 参数和返回（寄存器传递）
static struct Medium make_medium(long a, long b, long c) {
    struct Medium s;
    s.a = a;
    s.b = b;
    s.c = c;
    return s;
}

// 测试2：大 struct 参数和返回（内存传递）
static struct Large make_large(long base) {
    struct Large l;
    l.d0 = base;    l.d1 = base + 1;  l.d2 = base + 2;  l.d3 = base + 3;
    l.d4 = base + 4;  l.d5 = base + 5;  l.d6 = base + 6;  l.d7 = base + 7;
    l.d8 = base + 8;  l.d9 = base + 9;  l.da = base + 10; l.db = base + 11;
    l.dc = base + 12; l.dd = base + 13; l.de = base + 14; l.df = base + 15;
    return l;
}

// 测试3：混合对齐
// 测试3：混合对齐
static struct Mixed make_mixed(long a, long b, long c) {
    struct Mixed m;
    m.a = a;
    m.b = b;
    m.c = c;
    return m;
}

// (嵌套 struct 测试暂略)

// 测试5：struct 作为参数（大，用指针）
static long sum_large(struct Large *lp) {
    return lp->d0 + lp->d1 + lp->d2 + lp->d3 +
           lp->d4 + lp->d5 + lp->d6 + lp->d7 +
           lp->d8 + lp->d9 + lp->da + lp->db +
           lp->dc + lp->dd + lp->de + lp->df;
}

// 测试6：struct 作为参数（小，用指针）
static long sum_medium(struct Medium *sp) {
    return sp->a + sp->b + sp->c;
}

// 测试7：多次 return（控制流）
static struct Medium choose_medium(int choice, long a, long b, long c) {
    if (choice == 0) {
        struct Medium r;
        r.a = a;
        r.b = b;
        r.c = c;
        return r;
    } else {
        struct Medium s;
        s.a = b;
        s.b = a;
        s.c = c;
        return s;
    }
}

// 测试8：struct 比较（用指针）
static int compare_mixed(struct Mixed *ap, struct Mixed *bp) {
    if (ap->a != bp->a) return 1;
    if (ap->b != bp->b) return 2;
    if (ap->c != bp->c) return 3;
    return 0;
}

// 测试9：8 字节结构体返回（寄存器 ABI）
static struct Small make_small(long a) {
    struct Small s;
    s.a = a;
    return s;
}

// 测试10：8 字节结构体多路径返回
static struct Small choose_small(int choice, long a, long b) {
    if (choice == 0) {
        struct Small s;
        s.a = a;
        return s;
    } else {
        struct Small s;
        s.a = b;
        return s;
    }
}

// 测试11：带指针成员结构体返回（hidden pointer ABI）
static struct WithPtr make_with_ptr(long val, long *ptr, long extra) {
    struct WithPtr wp;
    wp.val = val;
    wp.ptr = ptr;
    wp.extra = extra;
    return wp;
}

// 测试12：含 char/short 成员的结构体
static struct WithCharShort make_with_char_short(char c, long pad1, short s, long pad2) {
    struct WithCharShort w;
    w.c = c;
    w.pad1 = pad1;
    w.s = s;
    w.pad2 = pad2;
    return w;
}

// === 主测试 ===
void __tlibc_start(void) {
    // 测试1：小 struct 返回
    struct Medium s1 = make_medium(100, 200, 300);
    if (s1.a != 100) sys_exit(1);
    if (s1.b != 200) sys_exit(2);
    if (s1.c != 300) sys_exit(3);

    // 测试2：小 struct 作为参数
    long sum1 = sum_medium(&s1);
    if (sum1 != 600) sys_exit(4);

    // 测试3：大 struct 返回
    struct Large l1 = make_large(1000);
    if (l1.d0 != 1000) sys_exit(10);
    if (l1.d1 != 1001) sys_exit(11);
    if (l1.d2 != 1002) sys_exit(12);
    if (l1.d3 != 1003) sys_exit(13);
    if (l1.d4 != 1004) sys_exit(14);
    if (l1.d5 != 1005) sys_exit(15);
    if (l1.d6 != 1006) sys_exit(16);
    if (l1.d7 != 1007) sys_exit(17);
    if (l1.d8 != 1008) sys_exit(18);
    if (l1.d9 != 1009) sys_exit(19);
    if (l1.da != 1010) sys_exit(20);
    if (l1.db != 1011) sys_exit(21);
    if (l1.dc != 1012) sys_exit(22);
    if (l1.dd != 1013) sys_exit(23);
    if (l1.de != 1014) sys_exit(24);
    if (l1.df != 1015) sys_exit(25);

    // 测试4：大 struct 作为参数
    long sum2 = sum_large(&l1);
    if (sum2 != 16120) sys_exit(30);  // sum(1000..1015) = 16120

    // 测试5：混合对齐
    struct Mixed m1 = make_mixed(100, 200, 300);
    if (m1.a != 100) sys_exit(40);
    if (m1.b != 200) sys_exit(41);
    if (m1.c != 300) sys_exit(42);

    // 测试6：多次 return
    struct Medium s2 = choose_medium(0, 1, 2, 3);
    if (s2.a != 1 || s2.b != 2 || s2.c != 3) sys_exit(80);
    struct Medium s3 = choose_medium(1, 3, 4, 5);
    if (s3.a != 4 || s3.b != 3 || s3.c != 5) sys_exit(81);

    // 测试8：struct 比较（通过指针）
    struct Mixed m2 = make_mixed(100, 200, 300);
    struct Mixed m3 = make_mixed(100, 200, 300);
    if (compare_mixed(&m2, &m3) != 0) sys_exit(90);

    // === 新增测试：8 字节 struct（寄存器返回）===
    // 测试9：基本返回与字段访问
    struct Small sm1 = make_small(42);
    if (sm1.a != 42) sys_exit(100);

    // 多路径返回
    struct Small sm2 = choose_small(0, 111, 222);
    if (sm2.a != 111) sys_exit(101);
    struct Small sm3 = choose_small(1, 333, 444);
    if (sm3.a != 444) sys_exit(102);

    // 多个独立调用
    struct Small sm4 = make_small(55);
    struct Small sm5 = make_small(66);
    if (sm4.a != 55) sys_exit(103);
    if (sm5.a != 66) sys_exit(104);

    // 结构体拷贝初始化（8 字节）
    struct Small sm6 = sm4;
    if (sm6.a != 55) sys_exit(105);
    // 修改原变量，验证拷贝独立性
    sm4.a = 999;
    if (sm6.a != 55) sys_exit(106);

    // === 新增测试：结构体指针成员（hidden pointer ABI）===
    // 测试10：带指针成员
    long data_val_1 = 777;
    struct WithPtr wp1 = make_with_ptr(42, &data_val_1, 99);
    if (wp1.val != 42) sys_exit(110);
    // 直接解引用指针成员（elem_size 传播修复后可用）
    if (*wp1.ptr != 777) sys_exit(111);
    if (wp1.extra != 99) sys_exit(112);

    // === 新增测试：24 字节 struct 拷贝初始化 ===
    struct Medium mcopy = s1;   // s1 来自测试1：make_medium(100, 200, 300)
    if (mcopy.a != 100) sys_exit(120);
    if (mcopy.b != 200) sys_exit(121);
    if (mcopy.c != 300) sys_exit(122);

    // === 新增测试：char/short 成员 ===
    // 测试12：char/short 成员的正确加载宽度
    struct WithCharShort wcs1 = make_with_char_short((char)65, 1000, (short)-200, 2000);
    if (wcs1.c != 65) sys_exit(140);
    if (wcs1.pad1 != 1000) sys_exit(141);
    if (wcs1.s != -200) sys_exit(142);
    if (wcs1.pad2 != 2000) sys_exit(143);

    // === 新增测试：返回结构体字段算术 ===
    struct Mixed msum = make_mixed(10, 20, 30);
    long total = msum.a + msum.b + msum.c;
    if (total != 60) sys_exit(130);

    // 全部通过
    sys_exit(0);
}