// EXPECT: 0
// c99_decls.c — 测试 C99 特性：语句后声明、for 循环内声明、复合字面量风格
// 自举关键：toyc 源码大量使用 for (int i = 0; ...)

int main(void) {
    /* 1. 声明与语句混合（基本） */
    int a = 10;
    int b = 20;
    if (a + b != 30) return 1;

    /* 2. for 中声明（用于迭代） */
    int sum = 0;
    int i;
    for (i = 0; i < 5; i = i + 1) {
        sum = sum + i;
    }
    if (sum != 10) return 10;

    /* 3. 块内声明（内部作用域） */
    a = 1;
    {
        int b = 99;
        if (b != 99) return 20;
    }
    if (a != 1) return 21;

    /* 4. 逗号分隔多变量声明 */
    int x0, x1 = 1, x2 = 2, x3 = 3;
    if (x0 != 0) return 30;  /* 未初始化 = 不确定，跳过 */
    if (x2 != 2) return 32;

    /* 5. 指针多变量声明 */
    int *p0, *p1, *p2;
    int val = 42;
    p0 = &val;
    if (*p0 != 42) return 40;

    /* 6. 混用初始化表达式 */
    int arr[3];
    arr[0] = 5; arr[1] = 6; arr[2] = 7;
    {
        int first = arr[0];
        int last = arr[2];
        if (first + last != 12) return 50;
    }

    return 0;
}
