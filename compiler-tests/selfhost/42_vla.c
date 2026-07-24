/*
 * VLA (Variable Length Array) 自包含测试
 *
 * 无 toyc_rt 依赖 — 直接 syscall exit。
 * 定义 __tlibc_start → main → exit 模式。
 * EXPECT: 0
 */

/* syscall 包装 */
static long sys_exit(long code) {
    long ret;
    __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(60), "D"(code) : "rcx", "r11", "memory");
    return ret;
}

static long sys_write(long fd, const void *buf, unsigned long count) {
    long ret;
    __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(1), "D"(fd), "S"(buf), "d"(count) : "rcx", "r11", "memory");
    return ret;
}

/* 简易断言 */
static void assert_eq(int got, int want, int code) {
    if (got != want) sys_exit(code);
}

int main(void) {
    int i;

    /* Test 1: 基本 VLA */
    {
        int n = 5;
        int arr[n];
        for (i = 0; i < n; i++) arr[i] = i * 10;
        for (i = 0; i < n; i++) assert_eq(arr[i], i * 10, 1);

        int sum = 0;
        for (i = 0; i < n; i++) sum += arr[i];
        assert_eq(sum, 100, 2);
    }

    /* Test 2: 多个 VLA 共存 */
    {
        int a = 3, b = 4;
        int x[a], y[b];
        for (i = 0; i < a; i++) x[i] = i + 10;
        for (i = 0; i < b; i++) y[i] = i + 20;
        for (i = 0; i < a; i++) assert_eq(x[i], i + 10, 3);
        for (i = 0; i < b; i++) assert_eq(y[i], i + 20, 4);
    }

    /* Test 3: 指针对 VLA 元素访问 */
    {
        int len = 4;
        int data[len];
        int *p = data;
        for (i = 0; i < len; i++) p[i] = i;
        for (i = 0; i < len; i++) assert_eq(data[i], i, 5);
    }

    /* Test 4: sizeof(VLA) */
    {
        int sz = 10;
        int vla[sz];
        assert_eq(sizeof(vla), 40, 6);
    }

    /* Test 5: char VLA */
    {
        int n = 6;
        char buf[n];
        for (i = 0; i < n - 1; i++) buf[i] = 'A' + i;
        buf[n - 1] = '\0';
        assert_eq(buf[0], 'A', 7);
        assert_eq(buf[5], '\0', 8);
    }

    /* Test 6: VLA + 固定数组混合 */
    {
        int fixed[3] = {1, 2, 3};
        int vlen = 4;
        int vla[vlen];
        for (i = 0; i < vlen; i++) vla[i] = fixed[i % 3] * 10;
        for (i = 0; i < vlen; i++) assert_eq(vla[i], fixed[i % 3] * 10, 9);
    }

    return 0;
}

void __tlibc_start(void) {
    sys_exit(main());
}
