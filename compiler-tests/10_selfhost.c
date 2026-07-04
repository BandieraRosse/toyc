// EXPECT: 42
// selfhost.c — 自举综合：大量局部变量、enum、static inline 模式
enum TokenKind {
    TOK_INT = 256, TOK_VOID, TOK_CHAR, TOK_SHORT,
    TOK_LONG, TOK_UNSIGNED, TOK_RETURN, TOK_IF,
    TOK_ELSE, TOK_WHILE, TOK_FOR, TOK_DO,
    TOK_BREAK, TOK_CONTINUE, TOK_SWITCH, TOK_CASE,
    TOK_DEFAULT, TOK_GOTO, TOK_SIZEOF, TOK_STRUCT,
    TOK_IDENT = 300, TOK_NUMBER, TOK_STRING,
};

static inline int align_up(int offset, int align) {
    return (offset + align - 1) & ~(align - 1);
}

static inline int is_digit(int c) {
    return c >= '0' && c <= '9';
}

int main(void) {
    int a0,a1,a2,a3,a4,a5,a6,a7,a8,a9;
    int b0,b1,b2,b3,b4,b5,b6,b7,b8,b9;
    a0=0; a1=1; a2=2; a3=3; a4=4; a5=5; a6=6; a7=7; a8=8; a9=9;
    b0=10;b1=11;b2=12;b3=13;b4=14;b5=15;b6=16;b7=17;b8=18;b9=19;

    if (align_up(5, 8) != 8) return 1;
    if (align_up(8, 8) != 8) return 2;
    if (!is_digit('5')) return 3;
    if (is_digit('x')) return 4;
    if (TOK_INT != 256) return 5;
    if (TOK_IDENT != 300) return 6;
    if (TOK_STRING != 302) return 7;
    if (TOK_ELSE != 264) return 8;

    return 42;
}
