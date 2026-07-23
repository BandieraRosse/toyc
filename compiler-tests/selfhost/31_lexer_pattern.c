// EXPECT: 0
// t04_lexer_pattern.c — Bug A: exact lex.c pattern reproduction
//
// Uses the SAME Token struct, Lexer struct, and function signature
// as the real lex.c test. If this fails with toyc, we've reproduced Bug A.

static void sys_exit(int code) {
    __asm__ __volatile__ ("syscall"
        : : "a"((long)60), "D"((long)code)
        : "rcx", "r11", "memory");
    for (;;) ;
}

static void print_str(const char *s) {
    int n = 0; while (s[n]) n++;
    __asm__ __volatile__ ("syscall"
        : : "a"(1), "D"(1), "S"(s), "d"((long)n)
        : "rcx", "r11", "memory");
}

static void print_dec(long n) {
    char buf[32]; int i = 0;
    if (n < 0) { print_str("-"); n = -n; }
    if (n == 0) { buf[i++] = '0'; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) { long ch = buf[--i];
        __asm__ __volatile__ ("syscall"
            : : "a"(1), "D"(1), "S"(&ch), "d"(1L) : "rcx", "r11", "memory");
    }
}

/* Exact same Token struct as lex.c */
typedef enum {
    TOK_SEMI = 297,
    TOK_LBRACE = 298,
    TOK_RBRACE = 299,
} TokenKind;

typedef struct {
    TokenKind kind;
    const char *start;
    int len;
    long ival;
    double dval;
    int is_float;
    const char *sval;
} Token;

typedef struct {
    const char *start;
    const char *pos;
    const char *end;
    int line;
    int col;
    Token cur;
} Lexer;

static int slen(const char *s) {
    int n = 0; while (s[n]) n++;
    return n;
}

/* Minimal lexer_next that returns correct tokens */
Token lexer_next(Lexer *lx) {
    Token t;
    t.start = lx->pos;
    t.kind = TOK_SEMI;  /* always returns SEMI for simplicity */
    t.len = 1;
    t.ival = 0;
    t.sval = 0;
    t.is_float = 0;
    lx->cur = t;
    lx->pos++;
    return t;
}

void __tlibc_start(void) {
    int passed = 0, failed = 0;

    /* Pattern A: direct member access (like lex.c operators test) */
    {
        const char *src = ";;";
        Lexer lx; lx.start = src; lx.pos = src; lx.end = src + slen(src);
        lx.line = 1; lx.col = 1;

        if (lexer_next(&lx).kind != TOK_SEMI) {
            print_str("FAIL direct: got "); print_dec(lexer_next(&lx).kind);
            print_str(" expected "); print_dec(TOK_SEMI); print_str("\n");
            failed++;
        } else {
            passed++;
        }
    }

    /* Pattern B: store to local first */
    {
        const char *src = ";;";
        Lexer lx; lx.start = src; lx.pos = src; lx.end = src + slen(src);
        lx.line = 1; lx.col = 1;

        Token t = lexer_next(&lx);
        if (t.kind != TOK_SEMI) {
            print_str("FAIL local var: got "); print_dec(t.kind);
            print_str(" expected "); print_dec(TOK_SEMI); print_str("\n");
            failed++;
        } else {
            passed++;
        }
    }

    /* Pattern C: macro-style CHECK_KIND direct (same as lex.c) */
    {
        const char *src = ";;";
        Lexer lx; lx.start = src; lx.pos = src; lx.end = src + slen(src);
        lx.line = 1; lx.col = 1;

        /* This is the same pattern as CHECK_KIND(lexer_next(&lx).kind, TOK_SEMI, ";") */
        if ((lexer_next(&lx).kind) != TOK_SEMI) {
            print_str("FAIL macro pattern: got "); print_dec(lexer_next(&lx).kind);
            print_str(" expected "); print_dec(TOK_SEMI); print_str("\n");
            failed++;
        } else {
            passed++;
        }
    }

    print_str("-> "); print_dec(passed); print_str(" passed, ");
    print_dec(failed); print_str(" failed\n");

    if (failed) sys_exit(1);
    sys_exit(0);
}
