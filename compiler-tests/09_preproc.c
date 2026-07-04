// EXPECT: 0
// preproc.c — #define 常量、函数宏、条件编译（如 tcc.h 宏定义）
#define CODE_BUF_SIZE 262144
#define MAX_SYMS 8192
#define ARENA_SIZE 16777216
#define TOK_INT 256
#define TOK_VOID 257
#define TOK_RETURN 263

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define ALIGN4(n) (((n) + 3) & ~3)
#define O_RDONLY 0

#ifdef unused
static int not_used(void) { return 0; }
#endif

int main(void) {
    if (CODE_BUF_SIZE != 262144) return 1;
    if (TOK_INT != 256) return 2;
    if (TOK_RETURN != 263) return 3;

    if (MIN(10, 20) != 10) return 4;
    if (MIN(30, 5) != 5) return 5;

    if (ALIGN4(5) != 8) return 6;
    if (ALIGN4(4) != 4) return 7;

    return 0;
}
