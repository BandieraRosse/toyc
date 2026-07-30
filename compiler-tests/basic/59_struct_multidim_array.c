// EXPECT: 0
// struct_multidim_array.c — 结构体多维数组成员
// 覆盖：minimp3 的 h->mdct_overlap[ch] 模式
typedef struct {
    int arr[4][3];
} M;

/* 通过单下标 + 元素访问检验偏移正确 */
static int fill_and_check(M *m) {
    int n = 0, i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 3; j++)
            m->arr[i][j] = n++;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 3; j++)
            if (m->arr[i][j] != i * 3 + j)
                return 100 + i * 10 + j;
    return 0;
}

int main(void) {
    M m;
    int i, r;
    r = fill_and_check(&m);
    if (r) return r;

    /* 双下标一致性 */
    for (i = 0; i < 4; i++) {
        int j;
        for (j = 0; j < 3; j++)
            m.arr[i][j] = i * 10 + j;
    }
    for (i = 0; i < 4; i++) {
        int j;
        for (j = 0; j < 3; j++)
            if (m.arr[i][j] != i * 10 + j)
                return 200 + i * 10 + j;
    }

    /* 单元素读写 */
    m.arr[2][1] = 99;
    if (m.arr[2][1] != 99) return 300;

    /* 指针访问 */
    M *p = &m;
    if (p->arr[3][2] != 32) return 400;

    return 0;
}
