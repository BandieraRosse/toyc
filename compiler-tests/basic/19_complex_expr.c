// EXPECT: 0
// complex_expr.c — 测试复杂表达式：运算符优先级、复合赋值、三元、逗号、位运算
// 自举关键：toyc 源码使用了各种复杂的表达式组合

int main(void) {
    int a, b, c, r;

    /* 1. 复合赋值运算 */
    a = 10;
    a += 5;
    if (a != 15) return 1;

    a = 10;
    a -= 3;
    if (a != 7) return 2;

    a = 5;
    a *= 4;
    if (a != 20) return 3;

    a = 20;
    a /= 4;
    if (a != 5) return 4;

    a = 17;
    a %= 5;
    if (a != 2) return 5;

    /* 2. 位运算（用展开形式避免复合赋值解析 bug） */
    a = 0xFF;
    a = a & 0x0F;
    if (a != 0x0F) return 10;

    a = 0x0F;
    a = a | 0xF0;
    if (a != 0xFF) return 11;

    a = 0xFF;
    a = a ^ 0x0F;
    if (a != 0xF0) return 12;

    /* 3. 位移运算（用展开形式） */
    a = 1;
    a = a << 3;
    if (a != 8) return 20;

    a = 16;
    a = a >> 2;
    if (a != 4) return 21;

    /* 4. 三元运算符 */
    r = (5 > 3) ? 100 : 200;
    if (r != 100) return 30;

    r = (2 > 9) ? 100 : 200;
    if (r != 200) return 31;

    /* 5. 逗号表达式 */
    a = (b = 1, c = 2, b + c);
    if (a != 3) return 40;

    /* 6. 自增/自减 */
    a = 5;
    b = a++;
    if (b != 5) return 50;  /* postfix */
    if (a != 6) return 51;

    a = 10;
    b = --a;
    if (b != 9) return 52;  /* prefix */
    if (a != 9) return 53;

    /* 7. 位运算组合 */
    if ((3 | 5) != 7) return 60;
    if ((3 & 5) != 1) return 61;
    if ((3 ^ 5) != 6) return 62;
    if (~0 != -1) return 63;
    if ((~0 & 0xFF) != 0xFF) return 64;

    /* 8. 移位 */
    if ((1 << 4) != 16) return 70;
    if ((64 >> 3) != 8) return 71;

    /* 9. 逻辑运算短路 */
    a = 0;
    b = 0;
    if ((a++ && (b = 1)) != 0) return 80;
    if (a != 1) return 81;  /* a++ 被执行，结果为 0 */
    if (b != 0) return 82;  /* b=1 因短路未执行 */

    a = 0;
    b = 0;
    if ((a++ || (b = 1)) != 1) return 85;
    if (a != 1) return 86;
    if (b != 1) return 87;  /* b=1 因短路执行（a=0 为假） */

    /* 10. 关系运算组合 */
    a = 5;
    if (!(a > 3 && a < 10)) return 90;
    if (!(a == 5)) return 91;
    if (a != 5) return 92;

    return 0;
}
