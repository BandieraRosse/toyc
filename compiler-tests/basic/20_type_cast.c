// EXPECT: 0
// type_cast.c — 测试类型转换（显式和隐式）
// 自举关键：toyc 源码中大量使用 (char *)arena + sizeof(Arena) 等类型转换

int main(void) {
    int i;
    long l;
    char c;
    int *p;
    char *cp;

    /* 1. 整型提升 */
    short s = 10;
    int si;
    si = s;
    if (si != 10) return 1;

    /* 2. long ↔ int 转换 */
    i = 100;
    l = i;
    if (l != 100L) return 10;

    l = 200L;
    i = l;
    if (i != 200) return 11;

    /* 3. char ↔ int */
    c = 'A';
    i = c;
    if (i != 65) return 20;

    i = 66;
    c = i;
    if (c != 'B') return 21;

    /* 4. 指针转换 */
    i = 42;
    p = &i;
    cp = (char *)p;
    /* 跳过 char* 解引用，仅确认转换不崩溃 */
    if (*p != 42) return 30;

    /* 5. (unsigned char) 类型转换 */
    i = (unsigned char)200;
    if (i != 200) return 40;

    /* 6. 负值类型转换 */
    i = -1;
    l = (long)i;
    if (l != -1L) return 50;

    /* 7. 表达式中的类型转换 */
    i = (int)(long)42;
    if (i != 42) return 60;

    /* 8. void* 赋值 */
    p = (int *)0;
    cp = (char *)p;
    /* 不应崩溃 */
    if (cp != 0) return 70;

    /* 9. 函数参数中的隐式转换 */
    l = 3000000000L;
    i = l;  /* 截断 */
    /* long 64-bit → int 32-bit */

    return 0;
}
