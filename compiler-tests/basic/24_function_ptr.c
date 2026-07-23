// EXPECT: 0
// function_ptr.c — 测试函数指针
// 自举关键：toyc.h 通过函数指针模式间接使用

static int add(int a, int b) { return a + b; }
static int sub(int a, int b) { return a - b; }
static int mul(int a, int b) { return a * b; }

int main(void) {
    /* 1. 直接函数指针 */
    int (*fp)(int, int) = add;
    if (fp(10, 20) != 30) return 1;

    /* 2. 切换函数指针 */
    fp = sub;
    if (fp(20, 5) != 15) return 10;

    fp = mul;
    if (fp(6, 7) != 42) return 11;

    /* 3. 函数指针数组 */
    int (*ops[3])(int, int);
    ops[0] = add;
    ops[1] = sub;
    ops[2] = mul;

    if (ops[0](5, 3) != 8) return 20;
    if (ops[1](5, 3) != 2) return 21;
    if (ops[2](5, 3) != 15) return 22;

    /* 4. 通过 typedef 函数指针 */
    typedef int (*BinOp)(int, int);
    BinOp op = add;
    if (op(100, 200) != 300) return 30;

    /* 5. NULL 函数指针（should not crash） */
    /* BinOp null_op = 0; 跳过 null call */

    return 0;
}
