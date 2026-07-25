/* 测试 toyc bug：((struct sockaddr_in *)sa)->sin_addr 一行内联转型后
 * 通过 -> 访问成员失败。根因：指针转型路径未传播 struct_type。 */
#include "core.h"
#include "net.h"

struct test_struct {
    int a;
    int b;
};

int main(void) {
    unsigned char buf[32] = {0};
    struct test_struct *p = (struct test_struct *)buf;

    /* bug 场景：一行内联转型 + -> 成员访问 */
    int val_a = ((struct test_struct *)buf)->a;
    int val_b = ((struct test_struct *)buf)->b;

    /* 对照：拆成两行应始终正常工作 */
    struct test_struct *q = (struct test_struct *)buf;
    int val_a2 = q->a;
    int val_b2 = q->b;

    /* 全部应为 0（buf 已清零） */
    if (val_a != 0) return 1;
    if (val_b != 0) return 2;
    if (val_a2 != 0) return 3;
    if (val_b2 != 0) return 4;

    /* 设置值后，验证内联转型读取正确 */
    p->a = 42;
    p->b = 99;
    val_a = ((struct test_struct *)buf)->a;
    val_b = ((struct test_struct *)buf)->b;
    val_a2 = q->a;
    val_b2 = q->b;

    if (val_a != 42) return 5;
    if (val_b != 99) return 6;
    if (val_a2 != 42) return 7;
    if (val_b2 != 99) return 8;

    /* 也测试通过 -> 写入 */
    ((struct test_struct *)buf)->a = 123;
    ((struct test_struct *)buf)->b = 456;
    if (p->a != 123) return 9;
    if (p->b != 456) return 10;

    return 0;
}
