// EXPECT: 0
// SELF_CONTAINED
//
// 13_global_keyword_init.c — 验证全局结构体数组初始化器
//
// 测试场景：
//   1. static Keyword keywords[] = { {"str", TOK_VAL}, ... }
//      中字符串指针和整数值都被正确发射到 .data 段
//   2. sizeof(keywords) 在 [] + 初始化器推断后返回正确值
//   3. 线性查找（keyword_lookup 模式）能正确定位关键字
//
// 历史：此测试覆盖了 toyc 自举中的关键 bug：
//   - pvar_add_ex 在初始器计数前已调用（size 太小）
//   - 尾随逗号导致 init_count 偏大 1
//   - 修正后在 Phase 1 中符号 slot 的分配顺序

typedef struct { char *name; int tok; } Keyword;

static Keyword keywords[] = {
    {"__asm__", 289}, {"__attribute__", 288}, {"__const__", 281},
    {"__inline", 287}, {"__inline__", 287}, {"__restrict__", 283},
    {"__signed__", 262}, {"__volatile__", 282},
    {"auto", 294}, {"break", 270}, {"case", 273}, {"char", 258},
    {"const", 281}, {"continue", 271}, {"default", 274},
    {"do", 268}, {"double", 269}, {"else", 265},
    {"enum", 279}, {"extern", 286}, {"for", 267},
    {"goto", 275}, {"if", 264}, {"inline", 287},
    {"int", 256}, {"long", 260}, {"register", 284},
    {"return", 263}, {"short", 259}, {"signed", 262},
    {"sizeof", 276}, {"static", 285}, {"struct", 277},
    {"switch", 272}, {"typedef", 280}, {"union", 278},
    {"unsigned", 261}, {"void", 257}, {"volatile", 282},
    {"while", 266},
};

#define KEYWORD_COUNT (sizeof(keywords) / sizeof(keywords[0]))

static int keyword_lookup(const char *start, int len) {
    int i;
    for (i = 0; i < (int)KEYWORD_COUNT; i++) {
        const char *k = keywords[i].name;
        int j;
        for (j = 0; j < len; j++) {
            if (k[j] == '\0' || k[j] != start[j])
                goto next;
        }
        if (k[j] == '\0')
            return keywords[i].tok;
next:;
    }
    return 294;  /* TOK_IDENT */
}

static long sys_write(int fd, const char *buf, unsigned long len) {
    unsigned long ret;
    __asm__ __volatile__ ("syscall"
        : "=a"(ret) : "a"(1), "D"((long)fd), "S"((long)buf), "d"((long)len)
        : "rcx", "r11", "memory");
    return (long)ret;
}
static void sys_exit(int code) {
    __asm__ __volatile__ ("syscall"
        : : "a"((long)60), "D"((long)code)
        : "rcx", "r11", "memory");
    for (;;) ;
}

void __tlibc_start(void) {
    /* 验证 sizeof 正确 */
    int n = (int)(sizeof(keywords) / sizeof(keywords[0]));
    if (n != 40) sys_exit(10);

    /* 验证线性查找 */
    if (keyword_lookup("int", 3) != 256) sys_exit(1);
    if (keyword_lookup("void", 4) != 257) sys_exit(2);
    if (keyword_lookup("char", 4) != 258) sys_exit(3);
    if (keyword_lookup("return", 6) != 263) sys_exit(4);
    if (keyword_lookup("struct", 6) != 277) sys_exit(5);
    if (keyword_lookup("static", 6) != 285) sys_exit(6);
    if (keyword_lookup("unsigned", 8) != 261) sys_exit(7);
    if (keyword_lookup("xxx", 3) != 294) sys_exit(8);

    /* 验证字符串指针正确性（直接访问） */
    if (keywords[24].name[0] != 'i') sys_exit(20);
    if (keywords[24].name[1] != 'n') sys_exit(21);
    if (keywords[24].name[2] != 't') sys_exit(22);
    if (keywords[24].name[3] != '\0') sys_exit(23);

    /* 验证整数值 */
    if (keywords[24].tok != 256) sys_exit(30);

    sys_exit(0);
}
