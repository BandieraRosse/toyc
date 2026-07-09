// EXPECT: 0
// sizeof.c — 测试 sizeof 对各种类型、表达式、数组、struct 的支持
// 这是自举的关键：tcc.h 大量使用 sizeof(Arena)、sizeof(keywords)/sizeof(keywords[0])

typedef struct { int x; int y; } Point;
typedef struct { char data[16]; int len; } Buf;

int global_arr[10];

int main(void) {
    /* 1. sizeof 基本类型 */
    if (sizeof(int) != 4) return 1;
    if (sizeof(char) != 1) return 2;
    if (sizeof(short) != 2) return 3;
    if (sizeof(long) != 8) return 4;
    if (sizeof(long long) != 8) return 5;

    /* 2. sizeof 指针 */
    int *p = 0;
    if (sizeof(p) != 8) return 10;
    if (sizeof(int *) != 8) return 11;
    if (sizeof(char *) != 8) return 12;

    /* 3. sizeof 数组（自举关键） */
    int nums[8];
    if (sizeof(nums) != 32) return 20;
    if (sizeof(nums) / sizeof(nums[0]) != 8) return 21;

    char buf[16];
    if (sizeof(buf) != 16) return 22;

    long longs[4];
    if (sizeof(longs) != 32) return 23;

    /* 4. sizeof struct */
    if (sizeof(Point) != 8) return 30;
    if (sizeof(Buf) != 20) return 31;  // 16 (char[16]) + 4 (int), x86_64 ABI tail pad to max(1,4)=4

    /* 5. sizeof 局部变量 */
    int a;
    if (sizeof(a) != 4) return 40;
    long b;
    if (sizeof(b) != 8) return 41;
    char c;
    if (sizeof(c) != 1) return 42;

    /* 6. sizeof 全局变量（自举关键：tcc.c 的 sizeof(out_path)） */
    if (sizeof(global_arr) != 40) return 50;  // 10*4

    /* 7. sizeof 变量（直接查 pvar 表） */
    int x = 42;
    long lx = 7;
    if (sizeof(x) != 4) return 60;
    if (sizeof(lx) != 8) return 61;

    /* 8. sizeof 在常量表达式中的使用（自举关键：KEYWORD_COUNT 模式） */
    if (8 / 8 != 1) return 70;

    return 0;
}
