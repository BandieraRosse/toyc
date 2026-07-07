// EXPECT: 0
// SELF_CONTAINED
// double_subscript.c — 验证 ptr_array[i][j] 双重下标代码生成正确

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

static const char *strs[4] = { "ABC", "DEFG", "HIJKL", "MNO" };

void __tlibc_start(void)
{
    if (strs[0][0] != 'A') { sys_write(1, "FAIL:a\n",7); sys_exit(1); }
    if (strs[0][1] != 'B') { sys_write(1, "FAIL:b\n",7); sys_exit(1); }
    if (strs[0][2] != 'C') { sys_write(1, "FAIL:c\n",7); sys_exit(1); }
    if (strs[1][0] != 'D') { sys_write(1, "FAIL:d\n",7); sys_exit(1); }
    if (strs[1][3] != 'G') { sys_write(1, "FAIL:e\n",7); sys_exit(1); }
    if (strs[2][0] != 'H') { sys_write(1, "FAIL:f\n",7); sys_exit(1); }
    if (strs[2][4] != 'L') { sys_write(1, "FAIL:g\n",7); sys_exit(1); }
    if (strs[3][2] != 'O') { sys_write(1, "FAIL:h\n",7); sys_exit(1); }

    /* 变量下标 */
    {
        int i, j;
        for (i = 0; i < 4; i++)
            for (j = 0; j < 4; j++) {
                int exp;
                if (i==0) exp = (j<3) ? "ABC"[j] : 0;
                else if (i==1) exp = (j<4) ? "DEFG"[j] : 0;
                else if (i==2) exp = (j<5) ? "HIJKL"[j] : 0;
                else exp = (j<3) ? "MNO"[j] : 0;
                if (strs[i][j] != exp) { sys_write(1,"FAIL:v\n",7); sys_exit(1); }
            }
    }

    /* 拼接所有字符串 */
    {
        char r[20]; int ri=0, i;
        for (i=0; i<4; i++) { int j; for (j=0; strs[i][j]; j++) r[ri++]=strs[i][j]; }
        r[ri]=0;
        const char *want = "ABCDEFGHIJKLMNO";
        for (i=0; i<15; i++) if (r[i]!=want[i]) { sys_write(1,"FAIL:r\n",7); sys_exit(1); }
    }

    sys_write(1, "OK\n", 3);
    sys_exit(0);
}
