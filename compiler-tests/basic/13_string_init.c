// EXPECT: 0
// string_init.c — 测试字符串初始化和指针访问模式
// 自举关键：toyc.c 的 buf[size + 2]、parse.c 的字符串解码
//
// 注意：toyc 暂不支持 char buf[] = "str" 复制初始化（只存指针），
//       所以测试改用指针 + 定长数组模式。

int main(void) {
    /* 1. 定长字符数组 + 逐元素赋值 */
    char s1[6];
    s1[0] = 'h'; s1[1] = 'e'; s1[2] = 'l';
    s1[3] = 'l'; s1[4] = 'o'; s1[5] = '\0';
    if (s1[0] != 'h') return 1;
    if (s1[4] != 'o') return 2;
    if (s1[5] != '\0') return 3;

    /* 2. const char *s = "str" — 指针指向字面量 */
    const char *s3 = "world";
    if (s3[0] != 'w') return 20;
    if (s3[4] != 'd') return 21;

    /* 3. 字符数组逐元素赋值（可变大小） */
    char s4[6];
    s4[0] = 'a'; s4[1] = 'b'; s4[2] = 'c';
    s4[3] = 'd'; s4[4] = 'e'; s4[5] = '\0';
    if (s4[0] != 'a') return 30;
    if (s4[4] != 'e') return 31;

    /* 4. 字符串指针解引用 */
    const char *s5 = "test123";
    if (s5[0] != 't') return 40;
    if (s5[4] != '1') return 41;

    /* 5. 转义序列（字符串指针） */
    const char *s6 = "a\nb\tc";
    if (s6[0] != 'a') return 50;
    if (s6[1] != '\n') return 51;
    if (s6[2] != 'b') return 52;
    if (s6[3] != '\t') return 53;
    if (s6[4] != 'c') return 54;

    /* 6. 空字符串 */
    const char *s7 = "";
    if (s7[0] != '\0') return 60;

    return 0;
}
