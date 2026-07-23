/*
 * toyar 归档器测试 — 定义全局符号，验证 toyar 的符号表提取
 * 编译: toyc test_toyar_add.c -o test_toyar_add.o
 * 链接: toyld test_toyar_add.o toyc_rt.o toyc_rt_start.o -o test_toyar_add
 * EXPECT: 0
 */

int add(int a, int b) {
    return a + b;
}

int data_val = 42;

int main(void) {
    if (add(1, 2) != 3) return 1;
    if (data_val != 42) return 1;
    return 0;
}
