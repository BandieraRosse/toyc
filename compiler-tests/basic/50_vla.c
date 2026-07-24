/*
 * VLA (Variable Length Array) 测试
 *
 * 验证 toyc 编译变长数组 `int arr[n]` 的正确性。
 * EXPECT: 0
 */

void __assert(int ok, int code) {
    if (!ok) __exit(code);
}

int main() {
    int i;

    /* ───── Test 1: 基本 VLA int arr[n] ───── */
    {
        int n = 10;
        int arr[n];
        for (i = 0; i < n; i++)
            arr[i] = i * 2;
        for (i = 0; i < n; i++)
            __assert(arr[i] == i * 2, 1);
    }

    /* ───── Test 2: VLA 求和 ───── */
    {
        int n = 10;
        int arr[n];
        for (i = 0; i < n; i++) arr[i] = i * 2;
        int sum = 0;
        for (i = 0; i < n; i++) sum += arr[i];
        __assert(sum == 90, 2);
    }

    /* ───── Test 3: char VLA ───── */
    {
        int m = 5;
        char buf[m];
        for (i = 0; i < m; i++)
            buf[i] = 'a' + i;
        for (i = 0; i < m; i++)
            __assert(buf[i] == 'a' + i, 3);
    }

    /* ───── Test 4: short VLA ───── */
    {
        int s = 6;
        short sarr[s];
        for (i = 0; i < s; i++)
            sarr[i] = (short)(i * 3);
        for (i = 0; i < s; i++)
            __assert(sarr[i] == i * 3, 4);
    }

    /* ───── Test 5: 多个 VLA 共存（逗号分隔） ───── */
    {
        int a = 3, b = 4;
        int x123[a], y456[b];
        for (i = 0; i < a; i++) x123[i] = i + 10;
        for (i = 0; i < b; i++) y456[i] = i + 20;
        for (i = 0; i < a; i++) __assert(x123[i] == i + 10, 5);
        for (i = 0; i < b; i++) __assert(y456[i] == i + 20, 6);
    }

    /* ───── Test 6: VLA + 固定数组混合 ───── */
    {
        int fixed[3] = {1, 2, 3};
        int len6 = 4;
        int arr6[len6];
        for (i = 0; i < len6; i++) arr6[i] = fixed[i % 3] * 10;
        for (i = 0; i < len6; i++)
            __assert(arr6[i] == fixed[i % 3] * 10, 7);
    }

    /* ───── Test 7: 指针对 VLA 元素访问 ───── */
    {
        int len7 = 6;
        int data7[len7];
        for (i = 0; i < len7; i++) data7[i] = i;
        int *p7 = data7;
        for (i = 0; i < len7; i++)
            __assert(p7[i] == i, 8);
    }

    /* ───── Test 8: sizeof(VLA) 运行时求值 ───── */
    {
        int sz8 = 7;
        int vla8[sz8];
        __assert(sizeof(vla8) == 28, 9);
    }

    /* ───── Test 9: 块作用域 VLA ───── */
    for (i = 0; i < 3; i++) {
        int block_n = i + 1;
        int block_arr[block_n];
        int j;
        for (j = 0; j < block_n; j++)
            block_arr[j] = j * 100;
        for (j = 0; j < block_n; j++)
            __assert(block_arr[j] == j * 100, 10);
    }

    /* ───── Test 10: 通过指针写 VLA ───── */
    {
        int len10 = 8;
        int data10[len10];
        int *p10 = data10;
        for (i = 0; i < len10; i++) p10[i] = i;
        for (i = 0; i < len10; i++) __assert(data10[i] == i, 11);
    }

    return 0;
}
