// EXPECT: 0
// SELF_CONTAINED
// 20_small_array.c — 小数组退化 + 小类型 brace-init 数据排放
//   1. unsigned char[4] 数组（≤8 字节）退化→指针，下标访问正确
//   2. unsigned short[5] brace-init 数据按 2 字节/slot 排放
//   3. int[2] 数组（恰 8 字节）退化→指针
//   4. char[3] 数组（<8 字节）退化→指针
//   5. 小数组传递到局部指针后再下标

static long sys_write(int fd, const char *buf, unsigned long len) {
    unsigned long ret;
    __asm__ __volatile__ ("syscall"
        : "=a"(ret) : "a"(1), "D"((long)fd), "S"((long)buf), "d"((long)len)
        : "rcx", "r11", "memory");
    return (long)ret;
}
static void sys_exit(int code) {
    __asm__ __volatile__ ("syscall"
        : : "a"((long)60), "D"((long)code)
        : "rcx", "r11", "memory");
    for (;;) ;
}

/* ─── 测试 1: unsigned char[4] ≤8 字节数组 ─── */
static unsigned char g_uc[4] = { 10, 20, 30, 40 };

/* ─── 测试 2: unsigned short[5] brace-init ─── */
static unsigned short g_us[5] = { 100, 200, 300, 400, 500 };

/* ─── 测试 3: int[2] 恰 8 字节数组 ─── */
static int g_i2[2] = { 1000, 2000 };

/* ─── 测试 4: char[3] <8 字节数组 ─── */
static char g_c3[3] = { 'X', 'Y', 'Z' };

/* ─── 测试 5: unsigned char[6] 传局部指针 ─── */
static unsigned char g_uc6[6] = { 5, 10, 15, 20, 25, 30 };

void __tlibc_start(void)
{
    /* ═══ 测试 1: unsigned char[4] 下标 ═══ */
    {
        if (g_uc[0] != 10) { sys_write(1,"F1a\n",4); sys_exit(11); }
        if (g_uc[1] != 20) { sys_write(1,"F1b\n",4); sys_exit(12); }
        if (g_uc[2] != 30) { sys_write(1,"F1c\n",4); sys_exit(13); }
        if (g_uc[3] != 40) { sys_write(1,"F1d\n",4); sys_exit(14); }
    }

    /* ═══ 测试 2: unsigned short[5] brace-init ═══ */
    {
        if (g_us[0] != 100) { sys_write(1,"F2a\n",4); sys_exit(21); }
        if (g_us[1] != 200) { sys_write(1,"F2b\n",4); sys_exit(22); }
        if (g_us[2] != 300) { sys_write(1,"F2c\n",4); sys_exit(23); }
        if (g_us[3] != 400) { sys_write(1,"F2d\n",4); sys_exit(24); }
        if (g_us[4] != 500) { sys_write(1,"F2e\n",4); sys_exit(25); }
    }

    /* ═══ 测试 3: int[2] 恰 8 字节 ═══ */
    {
        if (g_i2[0] != 1000) { sys_write(1,"F3a\n",4); sys_exit(31); }
        if (g_i2[1] != 2000) { sys_write(1,"F3b\n",4); sys_exit(32); }
    }

    /* ═══ 测试 4: char[3] <8 字节 ═══ */
    {
        if (g_c3[0] != 'X') { sys_write(1,"F4a\n",4); sys_exit(41); }
        if (g_c3[1] != 'Y') { sys_write(1,"F4b\n",4); sys_exit(42); }
        if (g_c3[2] != 'Z') { sys_write(1,"F4c\n",4); sys_exit(43); }
    }

    /* ═══ 测试 5: 小数组传局部指针 ═══ */
    {
        unsigned char *p = g_uc6;
        if (p[0] != 5)  { sys_write(1,"F5a\n",4); sys_exit(51); }
        if (p[2] != 15) { sys_write(1,"F5b\n",4); sys_exit(52); }
        if (p[5] != 30) { sys_write(1,"F5c\n",4); sys_exit(53); }
    }

    sys_write(1, "OK\n", 3);
    sys_exit(0);
}
