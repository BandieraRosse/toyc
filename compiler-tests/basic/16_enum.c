// EXPECT: 0
// enum.c — 测试 enum 定义和使用
// 自举关键：toyc.h 定义 TOK_INT = 256 等枚举

enum Color { RED, GREEN, BLUE };
enum Status { OK = 0, ERROR = -1, BUSY = 5 };
enum Flags {
    FLAG_A = 1,
    FLAG_B = 2,
    FLAG_C = 4,
};

/* 匿名 enum + typedef */
typedef enum { MONDAY, TUESDAY, WEDNESDAY } Weekday;

int main(void) {
    /* 1. 基本枚举值 */
    if (RED != 0) return 1;
    if (GREEN != 1) return 2;
    if (BLUE != 2) return 3;

    /* 2. 指定值的枚举 */
    if (OK != 0) return 10;
    if (ERROR != -1) return 11;
    if (BUSY != 5) return 12;

    /* 3. 位掩码枚举 */
    if (FLAG_A != 1) return 20;
    if (FLAG_B != 2) return 21;
    if (FLAG_C != 4) return 22;

    /* 4. 枚举变量 */
    enum Color c = GREEN;
    if (c != 1) return 30;

    /* 5. 枚举在 switch 中 */
    int r;
    c = RED;
    switch (c) {
    case RED:    r = 100; break;
    case GREEN:  r = 200; break;
    case BLUE:   r = 300; break;
    default:     r = -1; break;
    }
    if (r != 100) return 40;

    /* 6. typedef 枚举 */
    Weekday w = TUESDAY;
    if (w != 1) return 50;

    /* 7. 枚举在表达式中的使用 */
    if ((FLAG_A | FLAG_C) != 5) return 60;

    return 0;
}
