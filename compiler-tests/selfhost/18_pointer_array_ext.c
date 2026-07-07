// EXPECT: 0
// SELF_CONTAINED
// pointer_array_ext.c — 扩展指针数组测试：
//   1. 全局 char * 双重下标（常量+变量下标，拼接验证）
//   2. 运行时初始化的指针数组 + 双重下标

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
    /* ═══ 测试 1: 全局 char * 双重下标（跨边界索引） ═══ */
    {
        static const char *strs[4] = { "AB", "CDE", "FGHI", "JK" };
        if (strs[0][0] != 'A') { sys_write(1,"F1a\n",4); sys_exit(1); }
        if (strs[0][1] != 'B') { sys_write(1,"F1b\n",4); sys_exit(1); }
        if (strs[1][2] != 'E') { sys_write(1,"F1c\n",4); sys_exit(1); }
        if (strs[2][3] != 'I') { sys_write(1,"F1d\n",4); sys_exit(1); }
        if (strs[3][0] != 'J') { sys_write(1,"F1e\n",4); sys_exit(1); }
        if (strs[3][1] != 'K') { sys_write(1,"F1f\n",4); sys_exit(1); }
    }

    /* ═══ 测试 2: 运行时初始化 char * 双重下标 ═══ */
    {
        static const char *rstrs[3];
        rstrs[0] = "XYZ";
        rstrs[1] = "UVW";
        rstrs[2] = "RST";
        if (rstrs[0][0] != 'X') { sys_write(1,"F2a\n",4); sys_exit(1); }
        if (rstrs[0][2] != 'Z') { sys_write(1,"F2b\n",4); sys_exit(1); }
        if (rstrs[1][1] != 'V') { sys_write(1,"F2c\n",4); sys_exit(1); }
        if (rstrs[2][2] != 'T') { sys_write(1,"F2d\n",4); sys_exit(1); }
        /* 变量下标 */
        {
            int i;
            const char *all = "XYZUVWRST";
            for (i = 0; i < 9; i++) {
                int ri = i / 3, ci = i % 3;
                if (rstrs[ri][ci] != all[i]) { sys_write(1,"F2e\n",4); sys_exit(1); }
            }
        }
    }

    sys_write(1, "OK\n", 3);
    sys_exit(0);
}
