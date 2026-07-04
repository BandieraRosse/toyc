// EXPECT: 0
// pointers.c — 取地址/解引用、指针参数遍历数组（如 tcc.c read_file、cgen.c）
static int sum_array(int *arr, int n) {
    int i, s = 0;
    for (i = 0; i < n; i = i + 1) s = s + arr[i];
    return s;
}

int main(void) {
    int x = 42;
    int *ptr = &x;
    if (*ptr != 42) return 1;
    *ptr = 10;
    if (x != 10) return 2;

    int data[4];
    data[0] = 10; data[1] = 20; data[2] = 30; data[3] = 40;
    if (sum_array(data, 4) != 100) return 3;

    return 0;
}
