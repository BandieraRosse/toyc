// EXPECT: 0
// basic.c — 变量、算术、for/if/while（如 tcc.c 主循环）
int main(void) {
    int i, sum = 0;
    for (i = 0; i < 10; i = i + 1)
        sum = sum + i;
    if (sum != 45) return 1;

    int x = 42, y = 0;
    if (x > 0) y = 1; else y = -1;
    if (y != 1) return 2;

    if (2 + 3 * 4 != 14) return 3;
    if (13 / 5 != 2) return 4;
    if (13 % 5 != 3) return 5;

    int n = 0;
    while (n < 5) n = n + 1;
    if (n != 5) return 6;
    return 0;
}
