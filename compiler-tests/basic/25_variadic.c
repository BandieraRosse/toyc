// EXPECT: 0
// variadic.c — 测试变参函数和 __builtin_va_* 支持
// 自举关键：toyc_rt.c 的 __printf 使用 __builtin_va_*

static int sum_count(int count, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, count);
    int sum = 0;
    int i;
    for (i = 0; i < count; i = i + 1) {
        sum = sum + __builtin_va_arg(ap, int);
    }
    __builtin_va_end(ap);
    return sum;
}

int main(void) {
    /* 1. 变参函数调用 */
    int s = sum_count(3, 10, 20, 30);
    if (s != 60) return 1;

    s = sum_count(5, 1, 2, 3, 4, 5);
    if (s != 15) return 2;

    s = sum_count(1, 99);
    if (s != 99) return 3;

    s = sum_count(0);
    if (s != 0) return 4;

    /* 2. 多个变参调用 */
    int s1 = sum_count(2, 5, 10);
    int s2 = sum_count(3, 100, 200, 300);
    if (s1 != 15) return 10;
    if (s2 != 600) return 11;

    return 0;
}
