/* mf_helper.c — 多文件链接测试：辅助函数 */
int add(int a, int b) {
    return a + b;
}

int mul(int a, int b) {
    int r = 0;
    for (int i = 0; i < b; i++) r += a;
    return r;
}

int data_arr[4] = { 10, 20, 30, 40 };
int s_secret_box[1] = { 42 };
int get_secret(void) { return s_secret_box[0]; }
