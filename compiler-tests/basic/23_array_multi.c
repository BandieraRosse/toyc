// EXPECT: 0
// array_multi.c — 测试多维数组
// 自举关键：toyc 可能使用数组嵌套

int main(void) {
    /* 1. 二维 int 数组 */
    int mat[3][4];
    int i, j;
    int v = 0;

    for (i = 0; i < 3; i = i + 1)
        for (j = 0; j < 4; j = j + 1) {
            mat[i][j] = v;
            v = v + 1;
        }

    if (mat[0][0] != 0) return 1;
    if (mat[0][3] != 3) return 2;
    if (mat[2][3] != 11) return 3;

    /* 2. 多维数组初始化（逐元素） */
    int ident[2][2];
    ident[0][0] = 1; ident[0][1] = 0;
    ident[1][0] = 0; ident[1][1] = 1;
    if (ident[0][0] != 1) return 10;
    if (ident[1][1] != 1) return 11;
    if (ident[0][1] != 0) return 12;

    /* 3. 结构体中的数组 */
    typedef struct { int data[4]; int n; } Vec;
    Vec vec;
    vec.n = 4;
    vec.data[0] = 10;
    vec.data[1] = 20;
    vec.data[2] = 30;
    vec.data[3] = 40;
    if (vec.data[2] != 30) return 20;

    /* 4. 指针数组 */
    int a0 = 0, a1 = 1, a2 = 2;
    int *ptrs[3];
    ptrs[0] = &a0;
    ptrs[1] = &a1;
    ptrs[2] = &a2;
    if (*ptrs[0] != 0) return 30;
    if (*ptrs[2] != 2) return 31;

    return 0;
}
