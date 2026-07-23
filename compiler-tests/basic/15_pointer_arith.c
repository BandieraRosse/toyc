// EXPECT: 0
// pointer_arith.c — 测试指针运算各类模式
// 自举关键：toyc 源码大量使用 ptr + offset、ptr[i] 等

int main(void) {
    int arr[5];
    int i;

    /* 初始化数组 */
    for (i = 0; i < 5; i = i + 1)
        arr[i] = i * 10;

    /* 1. 指针 + 整数（默认缩放 = 元素大小） */
    int *p = arr;
    if (*p != 0) return 1;
    if (*(p + 1) != 10) return 2;
    if (*(p + 3) != 30) return 3;

    /* 2. arr[i] 下标语法 */
    if (arr[2] != 20) return 10;
    if (arr[4] != 40) return 11;

    /* 3. 指针运算偏移量检查 */
    int *q = &arr[4];

    /* 5. char* 指针运算（elem_size=1） */
    char *cp = "abcdef";
    if (*cp != 'a') return 40;
    if (*(cp + 1) != 'b') return 41;
    if (*(cp + 5) != 'f') return 42;

    /* 6. 通过指针写数组 */
    int arr2[3];
    int *p2 = arr2;
    *p2 = 100;
    *(p2 + 1) = 200;
    *(p2 + 2) = 300;
    if (arr2[0] != 100) return 50;
    if (arr2[1] != 200) return 51;
    if (arr2[2] != 300) return 52;

    /* 7. &arr[idx] → 指针 */
    int *p3 = &arr[2];
    if (*p3 != 20) return 60;

    return 0;
}
