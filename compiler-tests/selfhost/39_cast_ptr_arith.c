// EXPECT: 0
// SELF_CONTAINED
// 39_cast_ptr_arith.c — 验证 (cast_type*)ptr + N 指针算术的字节偏移正确性
//
// 背景：toyc 在 (char *)int_ptr + N 中错误地按 N * sizeof(int) 缩放偏移，
// 因为 locals 表中 int* 的 element_size=4 覆盖了解析器在 cast 中设置的
// elem_size=1。此 bug 影响所有目录遍历代码（如 dirent_get_name 中的
// (char*)data + d->reclen 模式）。
//
// 修复：指针算术检测优先使用 AST 节点上 elem_size（cast 修改后的值），
// 而非局部/全局变量表中的 element_size。

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

void __tlibc_start(void)
{
    int arr[16];
    int i;
    for (i = 0; i < 16; i++) arr[i] = i;

    int *p = arr;
    unsigned long base = (unsigned long)p;

    /* ═══ 测试 1: (char *)p + N 常量偏移 — 核心 bug ═══ */
    /* 若未修复：(char *)p + 3 按 N * sizeof(int) = 12 字节偏移 */
    if ((unsigned long)((char *)p + 3) != base + 3) sys_exit(1);
    if ((unsigned long)((char *)p + 7) != base + 7) sys_exit(2);
    if ((unsigned long)((char *)p + 0) != base + 0) sys_exit(3);

    /* ═══ 测试 2: (char *)p + N 变量偏移 ═══ */
    {
        int offset = 5;
        if ((unsigned long)((char *)p + offset) != base + 5) sys_exit(4);
    }

    /* ═══ 测试 3: (char *)p - N 指针减法（后退） ═══ */
    {
        int *q = arr + 10;
        unsigned long qbase = (unsigned long)q;
        if ((unsigned long)((char *)q - 4) != qbase - 4) sys_exit(5);
        if ((unsigned long)((char *)q - 10) != qbase - 10) sys_exit(6);
    }

    /* ═══ 测试 4: (char *)p + a + b 嵌套表达式 ═══ */
    {
        int a = 2, b = 3;
        if ((unsigned long)((char *)p + a + b) != base + 5) sys_exit(7);
    }

    /* ═══ 测试 5: (short *)p + N 非 char 的 cast 也应正确 ═══ */
    {
        if ((unsigned long)((short *)p + 3) != base + 6) sys_exit(8);
        if ((unsigned long)((short *)p + 0) != base + 0) sys_exit(9);
    }

    /* ═══ 测试 6: (long *)p + N — 8 字节元素 ═══ */
    {
        if ((unsigned long)((long *)p + 2) != base + 16) sys_exit(10);
    }

    /* ═══ 测试 7: (char *)data + reclen 模式（目录遍历常见写法） ═══ */
    {
        char buf[64];
        char *data = buf;
        int reclen = 12;
        unsigned long db = (unsigned long)data;
        char *next = (char *)data + reclen;
        if ((unsigned long)next != db + 12) sys_exit(11);

        reclen = 24;
        next = (char *)data + reclen;
        if ((unsigned long)next != db + 24) sys_exit(12);
    }

    /* ═══ 测试 8: 验证 cast+add 后的字节级别访问正确 ═══ */
    {
        /* 在 arr 中设置已知字节模式 */
        char *raw = (char *)arr;
        raw[0] = 0x10; raw[1] = 0x11; raw[2] = 0x12; raw[3] = 0x13;
        raw[4] = 0x14; raw[5] = 0x15; raw[6] = 0x16; raw[7] = 0x17;

        /* 通过 (char *)int_ptr + offset 访问特定字节 */
        if ((unsigned char)((char *)p + 0)[0] != 0x10) sys_exit(13);
        if ((unsigned char)((char *)p + 1)[0] != 0x11) sys_exit(14);
        if ((unsigned char)((char *)p + 4)[0] != 0x14) sys_exit(15);
        if ((unsigned char)((char *)p + 7)[0] != 0x17) sys_exit(16);
    }

    /* ═══ 测试 9: 验证未 cast 的普通指针算术不受影响 ═══ */
    {
        int *q = arr + 5;
        if ((unsigned long)(q + 3) != base + 5 * 4 + 3 * 4) sys_exit(17);
        if ((unsigned long)(q + 0) != base + 5 * 4) sys_exit(18);
        if (*(q + 2) != arr[7]) sys_exit(19);
    }

    /* ═══ 测试 10: unsigned char* cast ═══ */
    {
        unsigned char *ucp = (unsigned char *)p + 2;
        unsigned long expected = base + 2;
        if ((unsigned long)ucp != expected) sys_exit(20);
    }

    /* ═══ 测试 11: volatile 变量不应影响 cast 指针算术 ═══ */
    {
        /* 用其他类型绕路验证：void * → char * */
        void *vp = (void *)arr;
        char *cp = (char *)vp + 6;
        if ((unsigned long)cp != base + 6) sys_exit(21);
    }

    sys_exit(0);
}
