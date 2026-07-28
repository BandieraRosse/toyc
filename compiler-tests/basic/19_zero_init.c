// EXPECT: 0
// zero_init.c — 局部数组 = {0} 初始化（C99 要求全零化）
int main(void) {
    /* int 数组：所有元素应被零化 */
    {
        int sentinel[16];
        int i;
        for (i = 0; i < 16; i++) sentinel[i] = 0xDEADBEEF;

        int arr[8] = {0};
        if (arr[0] != 0) return 1;
        if (arr[3] != 0) return 2;
        if (arr[7] != 0) return 3;
    }

    /* char 数组：所有字节应被零化 */
    {
        volatile int garbage;
        garbage = 0xDEAD;
        { char buf[12] = {0};  /* 内层块确保栈被写上一步的环境 */
          if (buf[0] != 0) return 4;
          if (buf[5] != 0) return 5;
          if (buf[11] != 0) return 6; }
    }

    /* 部分初始化：int arr[8] = {1, 2} — arr[2..7] 应为 0 */
    {
        int sentinel[16];
        int i;
        for (i = 0; i < 16; i++) sentinel[i] = 0xDEADBEEF;

        int arr[8] = {1, 2};
        if (arr[0] != 1) return 10;
        if (arr[1] != 2) return 11;
        if (arr[2] != 0) return 12;
        if (arr[7] != 0) return 13;
    }

    /* 全部显式初始化不受影响 */
    {
        int arr[4] = {10, 20, 30, 40};
        if (arr[0] != 10) return 20;
        if (arr[1] != 20) return 21;
        if (arr[2] != 30) return 22;
        if (arr[3] != 40) return 23;
    }

    return 0;
}
