/**
 * test 38: (long)signed_int 作为函数参数传递时符号扩展
 *
 * Bug 2: AST_VAR 中 node->type_size 被 locals[i].size 覆盖，
 * 导致 (long)n 在传参时不补 movsxd，32-bit 零扩展值被送入 64-bit 寄存器。
 *
 * 本测试验证：int n = -42; func((long)n) 传递正确的 0xFFFFFFFFFFFFFFD6 (-42L)
 */

#include "toyc_need.h"

static int results[8];
static int idx;

static void check_eq(long got, long expected) {
    if (got != expected) {
        __write(1, "FAIL\n", 5);
        __exit(1);
    }
    results[idx++] = 1;
}

int main(void) {
    int si = -42;
    unsigned int ui = 3000000000U;

    /* 测试 1: signed int 作为 (long) 传参 */
    check_eq((long)si, -42L);

    /* 测试 2: unsigned int 作为 (long) 传参（不应符号扩展高位） */
    check_eq((long)ui, 3000000000L);

    /* 测试 3: signed int 作为 (unsigned long) 传参
     * C 规则：负 int → unsigned long 时先符号扩展为 long 再解读为 unsigned */
    check_eq((unsigned long)(int)-42, 0xFFFFFFFFFFFFFFD6UL);

    /* 测试 4: unsigned int 作为 (unsigned long) 传参（零扩展） */
    check_eq((unsigned long)ui, 3000000000UL);

    /* 测试 5: int → long → int 赋值（原赋值路径应仍正确） */
    long ln = (long)si;
    int back = (int)ln;
    check_eq((long)back, -42L);

    /* 测试 6: 表达式上下文中的 cast */
    long expr = (long)si + 5;
    check_eq(expr, -37L);

    /* 测试 7: 多个参数中的 cast */
    check_eq((long)si, (long)(int)-42);

    /* 测试 8: signed char → long */
    signed char sc = -123;
    check_eq((long)sc, -123L);

    if (idx == 8)
        __write(1, "PASS\n", 5);
    return 0;
}
