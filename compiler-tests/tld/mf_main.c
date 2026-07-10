/* mf_main.c — 多文件链接测试：主程序 */
/* EXPECT: 0 */

extern int add(int a, int b);
extern int mul(int a, int b);
extern int get_secret(void);
extern int data_arr[4];

static long __syscall1(long n, long a1) {
    unsigned long ret;
    __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}
static void __exit(int code) { __syscall1(60, code); }
static long __write(int fd, const void *buf, long len) {
    unsigned long ret;
    __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(1), "D"((long)fd), "S"((long)buf), "d"(len) : "rcx", "r11", "memory");
    return ret;
}

/* 最小化输出：成功只写 OK\n，失败写具体消息 */
static void pstr(const char *s) {
    int n = 0; while (s[n]) n++;
    __write(1, s, n);
}

void __tlibc_start(void) {
    /* 跨文件函数调用 */
    if (add(3, 7) != 10) { pstr("add FAIL\n"); __exit(1); }
    if (mul(5, 6) != 30) { pstr("mul FAIL\n"); __exit(1); }
    if (get_secret() != 42) { pstr("get_secret FAIL\n"); __exit(1); }

    /* 跨文件全局变量 */
    if (data_arr[0] != 10) { pstr("arr[0] FAIL\n"); __exit(1); }
    if (data_arr[2] != 30) { pstr("arr[2] FAIL\n"); __exit(1); }

    pstr("ALL multi-file tests PASSED\n");
    __exit(0);
}
