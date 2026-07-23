// EXPECT: 0
// SELF_CONTAINED
// 40_64bit_ptr_store.c — 验证两次堆分配后指针索引存储不被截断为 32 位
//
// 当同一个函数内出现两次堆分配，第二次分配的指针用于数组索引写入时，
// toyc 曾把 64 位地址截断为 32 位（mov [eax] 而非 mov [rax]）。
//
// 复现条件（来自 toyar.c add_member）：
//   1. 第一次分配的结果通过 struct 成员（指针解引用）存活
//   2. 第二次分配存为局部变量
//   3. 对第二次分配的指针进行数组索引写入
//   4. 中间有 struct 成员访问（-> 操作符）

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

// 基于 mmap 的分配器
static void *my_malloc(unsigned long sz)
{
    void *addr = (void*)0;
    unsigned long flags = 0x22;  // MAP_PRIVATE | MAP_ANONYMOUS
    long fd = -1;
    unsigned long ret;
    __asm__ __volatile__ ("syscall"
        : "=a"(ret)
        : "a"((long)9), "D"((long)addr), "S"((long)sz), "d"((long)3),
          "r10"((long)flags), "r8"((long)fd), "r9"((long)0)
        : "rcx", "r11", "memory");
    return (void*)ret;
}

// ============================================================
// 数据结构
// ============================================================

typedef struct {
    char name[16];
    int long_name_off;
    int size;
    char *data;
} Member;

// ============================================================
// 测试 1：精确模拟 toyar.c add_member 模式
// ============================================================

static int test_tar_pattern(void)
{
    Member m;
    const char *long_name = "very_long_name_for_test";

    // 第一次分配 + struct 成员存储（值域 0-127 避免 sign-ext 问题）
    {
        char *buf = (char *)my_malloc(100);
        int i;
        for (i = 0; i < 100; i++) buf[i] = (char)(0x41 + (i & 0x2F));
        m.data = buf;
        m.size = 100;
        m.long_name_off = -1;
    }

    // 验证 struct 成员
    if ((unsigned char)m.data[0] != (unsigned char)(0x41)) { puts("FAIL 1a\n"); return 1; }
    if (m.size != 100) { puts("FAIL 1b\n"); return 2; }

    // 第二次分配（关键）
    int bl = 0;
    while (long_name[bl]) bl++;
    int newsz = m.size + bl + 1;

    char *newbuf = (char *)my_malloc(newsz);
    if (!newbuf) { puts("FAIL 1c: alloc\n"); return 3; }

    // 复制原始数据
    {
        int i;
        for (i = 0; i < m.size; i++)
            newbuf[i] = m.data[i];
    }

    // ★ 关键模式：新指针 + struct 成员 size 作偏移写入
    {
        int i;
        for (i = 0; i < bl; i++)
            newbuf[m.size + i] = long_name[i];
    }
    newbuf[m.size + bl] = '\0';

    // 验证
    {
        int i;
        for (i = 0; i < m.size; i++) {
            if ((unsigned char)newbuf[i] != (unsigned char)m.data[i]) {
                puts("FAIL 1d: data "); putlong(i); putchar('\n');
                return 4;
            }
        }
        for (i = 0; i < bl; i++) {
            if (newbuf[m.size + i] != long_name[i]) {
                puts("FAIL 1e: name "); putlong(i); putchar('\n');
                return 5;
            }
        }
    }

    m.data = newbuf;
    m.size = newsz;

    // 再次通过 struct 成员验证
    {
        int i;
        for (i = 0; i < 100; i++) {
            if ((unsigned char)m.data[i] != (unsigned char)(0x41 + (i & 0x2F))) {
                puts("FAIL 1f: struct "); putlong(i); putchar('\n');
                return 6;
            }
        }
    }

    return 0;
}

// ============================================================
// 测试 2：多轮分配 + struct 成员
// ============================================================

static int test_multi_round(void)
{
    Member members[2];
    int i, j;

    for (i = 0; i < 2; i++) {
        // 第一轮：struct 成员分配
        char *buf = (char *)my_malloc(20);
        for (j = 0; j < 20; j++) buf[j] = (char)(0x41 + j);
        members[i].data = buf;
        members[i].size = 20;

        // 第二轮分配（local ptr）
        char *buf2 = (char *)my_malloc(30);
        for (j = 0; j < 30; j++) buf2[j] = (char)(0x61 + j);

        // 通过 local ptr + struct member index 写入
        for (j = 0; j < 5; j++)
            buf2[members[i].size + j] = (char)('A' + j);

        // 验证第一轮
        for (j = 0; j < 20; j++) {
            if ((unsigned char)members[i].data[j] != (unsigned char)(0x41 + j)) {
                puts("FAIL 2a: m["); putlong(i); puts("].data["); putlong(j); puts("]\n");
                return 10 + i;
            }
        }
        // 验证第二轮
        for (j = 0; j < 5; j++) {
            if ((unsigned char)buf2[20 + j] != (unsigned char)('A' + j)) {
                puts("FAIL 2b: "); putlong(i); puts(" buf2[20+"); putlong(j); puts("]\n");
                return 20 + i;
            }
        }
    }

    return 0;
}

// ============================================================
// 测试 3：嵌套 struct 访问 + 多分配
// ============================================================

typedef struct {
    int count;
    char *items;
} Container;

static int test_nested(void)
{
    Container c;

    // 第一次分配
    c.items = (char *)my_malloc(64);
    c.count = 64;
    int i;
    for (i = 0; i < 64; i++)
        c.items[i] = (char)(0x10 + (i & 0x3F));

    // 第二次分配
    char *buf2 = (char *)my_malloc(128);
    if (!buf2) { puts("FAIL 3a\n"); return 30; }

    // 通过 struct member 作索引写入 local ptr
    for (i = 0; i < c.count; i++)
        buf2[i] = c.items[i];

    for (i = 0; i < 32; i++)
        buf2[c.count + i] = (char)(0x50 + (i & 0x2F));

    // 验证
    for (i = 0; i < 64; i++) {
        if ((unsigned char)buf2[i] != (unsigned char)(0x10 + (i & 0x3F))) {
            puts("FAIL 3b: "); putlong(i); putchar('\n');
            return 31;
        }
    }
    for (i = 0; i < 32; i++) {
        if ((unsigned char)buf2[64 + i] != (unsigned char)(0x50 + (i & 0x2F))) {
            puts("FAIL 3c: "); putlong(i); putchar('\n');
            return 32;
        }
    }

    return 0;
}

// ============================================================
// 测试 4：大块数据，多次分配验证
// ============================================================

#define BIG_SIZE 4096

static int test_big(void)
{
    Member m;
    m.data = (char *)my_malloc(BIG_SIZE);
    m.size = BIG_SIZE;
    if (!m.data) { puts("FAIL 4a\n"); return 40; }

    // 初始化 — 使用 0-127 范围避免 sign-ext 问题
    long i;
    for (i = 0; i < BIG_SIZE; i++)
        m.data[i] = (char)(i & 0x7F);

    // 第二次分配
    char *big2 = (char *)my_malloc(BIG_SIZE);
    if (!big2) { puts("FAIL 4b\n"); return 41; }

    // 写入 big2
    for (i = 0; i < BIG_SIZE / 4; i++)
        big2[m.size / 4 + i] = (char)(0x10 + (i & 0x0F));

    // 验证第一次分配未损坏
    for (i = 0; i < BIG_SIZE; i++) {
        if ((unsigned char)m.data[i] != (unsigned char)(i & 0x7F)) {
            puts("FAIL 4c: m.data["); putlong(i); puts("]\n");
            return 42;
        }
    }

    // 验证第二次分配
    for (i = 0; i < BIG_SIZE / 4; i++) {
        if ((unsigned char)big2[BIG_SIZE / 4 + i] != (unsigned char)(0x10 + (i & 0x0F))) {
            puts("FAIL 4d: big2["); putlong(BIG_SIZE/4 + i); puts("]\n");
            return 43;
        }
    }

    return 0;
}

// ============================================================
// main
// ============================================================

int main(void)
{
    int r;

    r = test_tar_pattern();
    if (r) { puts("FAIL test_tar_pattern\n"); return r; }

    r = test_multi_round();
    if (r) { puts("FAIL test_multi_round\n"); return r; }

    r = test_nested();
    if (r) { puts("FAIL test_nested\n"); return r; }

    r = test_big();
    if (r) { puts("FAIL test_big\n"); return r; }

    puts("PASS: all 64-bit pointer store tests passed\n");
    return 0;
}

void __tlibc_start(void) { sys_exit(main()); }
