/*
 * tar 归档器测试 — 定义全局符号，验证 tar 的符号表提取
 * 编译: tcc test_tar_add.c -o test_tar_add.o
 * 链接: tld test_tar_add.o tcc_rt.o tcc_rt_start.o -o test_tar_add
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
