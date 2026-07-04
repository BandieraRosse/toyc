// EXPECT: 0
// arrays.c — 局部定长数组、char 下标、sizeof 求数组长度（如 tcc.c out_path、lex.c sizeof/sizeof）
int main(void) {
    char buf[8];
    buf[0] = 'H'; buf[1] = 'i'; buf[2] = '\0';
    if (buf[0] != 'H') return 1;
    if (buf[1] != 'i') return 2;

    int arr[5];
    int i;
    for (i = 0; i < 5; i = i + 1) arr[i] = i * 10;
    if (arr[3] != 30) return 3;

    /* sizeof/sizeof 模式（lex.c: KEYWORD_COUNT） */
    int nums[8];
    if (sizeof(nums) / sizeof(nums[0]) != 8) return 4;

    /* 字符串字面量 */
    const char *s = "hello";
    if (s[0] != 'h') return 5;
    if (s[4] != 'o') return 6;

    return 0;
}
