// EXPECT: 0
// static_inline.c — 测试 static inline 函数模式
// 自举关键：toyc.h 大量使用 static inline 辅助函数

static inline int add(int a, int b) {
    return a + b;
}

static inline long add_long(long a, long b) {
    return a + b;
}

static inline int max3(int a, int b, int c) {
    int m = a;
    if (b > m) m = b;
    if (c > m) m = c;
    return m;
}

static inline int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static inline int align_up(int offset, int align) {
    return (offset + align - 1) & ~(align - 1);
}

int main(void) {
    /* 1. 简单 inline */
    if (add(3, 4) != 7) return 1;
    if (add(-1, 1) != 0) return 2;

    /* 2. long 运算 */
    if (add_long(100L, 200L) != 300L) return 10;

    /* 3. 多参数 + 分支 */
    if (max3(1, 5, 3) != 5) return 20;
    if (max3(9, 2, 7) != 9) return 21;
    if (max3(1, 2, 8) != 8) return 22;

    /* 4. 按位运算 */
    if (align_up(5, 8) != 8) return 30;
    if (align_up(8, 8) != 8) return 31;
    if (align_up(0, 4) != 0) return 32;
    if (align_up(3, 4) != 4) return 33;

    /* 5. 比较表达式 */
    if (!is_digit('5')) return 40;
    if (is_digit('x')) return 41;
    if (!is_digit('0')) return 42;

    /* 6. 嵌套调用 */
    if (add(max3(1, 10, 5), max3(20, 30, 5)) != 40) return 50;

    return 0;
}
