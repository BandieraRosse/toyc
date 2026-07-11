// EXPECT: 0
// lex.c standalone test (simulated preprocessed state)
//
// Compile (gcc):
//   gcc -nostdlib -ffreestanding -Wall -Wextra -Wl,-e,__tlibc_start
//       compiler-tests/source/lex.c -o /tmp/test_lex
// Run:  /tmp/test_lex
//
// Self-host (future):
//   build/tcc tcc/source/lex.c -o /tmp/tlex.o
//   ld -nostdlib -static -T ld.script /tmp/tlex.o -o /tmp/tlex
//   /tmp/tlex
//

// ============================================================
// Inlined from tcc_need.h — 基础类型、NULL
// ============================================================

typedef unsigned long           size_t;
typedef long                    ptrdiff_t;
typedef long                    off_t;
typedef unsigned int            mode_t;

typedef signed char             int8_t;
typedef unsigned char           uint8_t;
typedef signed short int        int16_t;
typedef unsigned short int      uint16_t;
typedef signed int              int32_t;
typedef unsigned int            uint32_t;
typedef signed long int         int64_t;
typedef unsigned long int       uint64_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

/* x86_64 syscall 内联汇编 — 仅测试框架自用 */
static inline long __syscall3(long n, long a1, long a2, long a3)
{
    unsigned long ret;
    __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2),
                          "d"(a3) : "rcx", "r11", "memory");
    return (long)ret;
}
#define syscall(N, A1, A2, A3) __syscall3((long)(N), (long)(A1), (long)(A2), (long)(A3))

// ============================================================
// Inlined from elf.h / elf_write.h — 只有 lex.c 需要的 Token 类型
//
// lex.c 实际不依赖 ELF 相关定义，仅使用 TokenKind / Token / Lexer
// ============================================================

// ============================================================
// Inlined from tcc.h — Token / Lexer / 函数声明
// ============================================================

typedef enum {
    TOK_INT = 256,
    TOK_VOID,
    TOK_CHAR,
    TOK_SHORT,
    TOK_LONG,
    TOK_UNSIGNED,
    TOK_SIGNED,
    TOK_RETURN,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_DO,
    TOK_DOUBLE,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_SWITCH,
    TOK_CASE,
    TOK_DEFAULT,
    TOK_GOTO,
    TOK_SIZEOF,
    TOK_STRUCT,
    TOK_UNION,
    TOK_ENUM,
    TOK_TYPEDEF,
    TOK_CONST,
    TOK_VOLATILE,
    TOK_RESTRICT,
    TOK_REGISTER,
    TOK_STATIC,
    TOK_EXTERN,
    TOK_INLINE,
    TOK__ATTRIBUTE__,
    TOK__ASM__,
    TOK__BUILTIN_VA_LIST,
    TOK__BUILTIN_VA_START,
    TOK__BUILTIN_VA_ARG,
    TOK__BUILTIN_VA_END,

    TOK_IDENT,
    TOK_NUMBER,
    TOK_STRING,

    TOK_SEMI, TOK_LBRACE, TOK_RBRACE,
    TOK_LPAREN, TOK_RPAREN,
    TOK_LBRACKET, TOK_RBRACKET,
    TOK_COMMA, TOK_DOT, TOK_ARROW,
    TOK_AMPERSAND, TOK_STAR, TOK_PLUS, TOK_MINUS,
    TOK_TILDE, TOK_EXCLAM,
    TOK_SLASH, TOK_PERCENT,
    TOK_LESS, TOK_GREATER,
    TOK_LESS_EQ, TOK_GREATER_EQ,
    TOK_EQ_EQ, TOK_NOT_EQ,
    TOK_AND_AND, TOK_OR_OR,
    TOK_PIPE, TOK_CARET,
    TOK_LESS_LESS, TOK_GREATER_GREATER,
    TOK_EQ, TOK_PLUS_EQ, TOK_MINUS_EQ,
    TOK_STAR_EQ, TOK_SLASH_EQ, TOK_PERCENT_EQ,
    TOK_AND_EQ, TOK_OR_EQ, TOK_CARET_EQ,
    TOK_LESS_LESS_EQ, TOK_GREATER_GREATER_EQ,
    TOK_PLUS_PLUS, TOK_MINUS_MINUS,
    TOK_QUESTION, TOK_COLON,
    TOK_ELLIPSIS,

    TOK_EOF = 0,
    TOK_ERROR = -1,
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

void lexer_init(Lexer *lx, const char *src, int len);
Token lexer_next(Lexer *lx);
Token lexer_peek(Lexer *lx);

// ============================================================
// lex.c 源文件 — 完全来自 compiler/lex.c，不作修改
// ============================================================

/* 文件结束标志（非 EOF 宏，避免与 TOK_EOF 冲突）。
 * 直接使用字面值 (-1)，不依赖宏展开
 * （tcc 自编译时预处理器有展开 bug）。 */

typedef struct {
    const char *word;
    TokenKind kind;
} Keyword;

static Keyword keywords[] = {
    {"__asm__",                   TOK__ASM__},
    {"__attribute__",             TOK__ATTRIBUTE__},
    {"__builtin_va_arg",          TOK__BUILTIN_VA_ARG},
    {"__builtin_va_end",          TOK__BUILTIN_VA_END},
    {"__builtin_va_list",         TOK__BUILTIN_VA_LIST},
    {"__builtin_va_start",        TOK__BUILTIN_VA_START},
    {"__const__",                 TOK_CONST},
    {"__inline",                  TOK_INLINE},
    {"__inline__",                TOK_INLINE},
    {"__restrict__",              TOK_RESTRICT},
    {"__signed__",                TOK_SIGNED},
    {"__volatile__",              TOK_VOLATILE},
    {"auto",                      TOK_IDENT},
    {"break",                     TOK_BREAK},
    {"case",                      TOK_CASE},
    {"char",                      TOK_CHAR},
    {"const",                     TOK_CONST},
    {"continue",                  TOK_CONTINUE},
    {"default",                   TOK_DEFAULT},
    {"do",                        TOK_DO},
    {"double",                    TOK_DOUBLE},
    {"else",                      TOK_ELSE},
    {"enum",                      TOK_ENUM},
    {"extern",                    TOK_EXTERN},
    {"float",                     TOK_IDENT},
    {"for",                       TOK_FOR},
    {"goto",                      TOK_GOTO},
    {"if",                        TOK_IF},
    {"inline",                    TOK_INLINE},
    {"int",                       TOK_INT},
    {"long",                      TOK_LONG},
    {"register",                  TOK_REGISTER},
    {"restrict",                  TOK_RESTRICT},
    {"return",                    TOK_RETURN},
    {"short",                     TOK_SHORT},
    {"signed",                    TOK_SIGNED},
    {"sizeof",                    TOK_SIZEOF},
    {"static",                    TOK_STATIC},
    {"struct",                    TOK_STRUCT},
    {"switch",                    TOK_SWITCH},
    {"typedef",                   TOK_TYPEDEF},
    {"union",                     TOK_UNION},
    {"unsigned",                  TOK_UNSIGNED},
    {"void",                      TOK_VOID},
    {"volatile",                  TOK_VOLATILE},
    {"while",                     TOK_WHILE},
};

#define KEYWORD_COUNT (sizeof(keywords) / sizeof(keywords[0]))

static TokenKind keyword_lookup(const char *start, int len) {
    int i;
    for (i = 0; i < (int)KEYWORD_COUNT; i++) {
        const char *k = keywords[i].word;
        int j;
        for (j = 0; j < len; j++) {
            if (k[j] == '\0' || k[j] != start[j])
                goto next;
        }
        if (k[j] == '\0')
            return keywords[i].kind;
next:;
    }
    return TOK_IDENT;
}

/* ─── 初始化和字符操作 ─── */

void lexer_init(Lexer *lx, const char *src, int len) {
    lx->start = src;
    lx->pos = src;
    lx->end = src + len;
    lx->line = 1;
    lx->col = 1;
    lx->cur.kind = TOK_EOF;
}

static int is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_alnum(char c) {
    return is_alpha(c) || is_digit(c);
}

static void advance(Lexer *lx) {
    if (*lx->pos == '\n') {
        lx->line++;
        lx->col = 1;
    } else {
        lx->col++;
    }
    lx->pos++;
}

static int input_peek(Lexer *lx) {
    return (lx->pos < lx->end) ? (unsigned char)*lx->pos : (-1);
}

/* ─── 跳过注释 ─── */

static void skip_comment(Lexer *lx, int style) {
    if (style == '/') {
        /* // 行注释 */
        while (input_peek(lx) != '\n' && input_peek(lx) != (-1))
            advance(lx);
    } else {
        /* 块注释：跳过直到遇见星号斜线 */
        advance(lx); /* 跳过 *，当前在星号后 */
        while (1) {
            int c = input_peek(lx);
            if (c == (-1))
                return;
            if (c == '*' && lx->pos + 1 < lx->end && *(lx->pos + 1) == '/') {
                advance(lx);
                advance(lx);
                return;
            }
            advance(lx);
        }
    }
}

/* ─── 简易字符串转 double ─── */

static double simple_atof(const char *s, int len) {
    double result = 0.0;
    int i = 0;
    int exp_sign = 1;
    int exponent = 0;
    int has_exponent = 0;

    /* 整数部分 */
    while (i < len && is_digit(s[i])) {
        result = result * 10.0 + (double)(s[i] - '0');
        i++;
    }

    /* 小数部分 */
    if (i < len && s[i] == '.') {
        i++;
        double frac = 0.0;
        double divisor = 1.0;
        while (i < len && is_digit(s[i])) {
            frac = frac * 10.0 + (double)(s[i] - '0');
            divisor *= 10.0;
            i++;
        }
        result += frac / divisor;
    }

    /* 指数部分 */
    if (i < len && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < len && s[i] == '-') {
            exp_sign = -1; i++;
        } else if (i < len && s[i] == '+') {
            i++;
        }
        while (i < len && is_digit(s[i])) {
            exponent = exponent * 10 + (s[i] - '0');
            i++;
        }
        has_exponent = 1;
    }

    if (has_exponent) {
        int exp = exponent * exp_sign;
        double pow10 = 1.0;
        if (exp > 0) {
            while (exp-- > 0) pow10 *= 10.0;
        } else {
            while (exp++ < 0) pow10 /= 10.0;
        }
        result *= pow10;
    }

    return result;
}

/* ─── 读数字 ─── */

static Token read_number(Lexer *lx) {
    Token t;
    t.start = lx->pos - 1;  /* pos 已在 lexer_next 中前移过首位 */
    t.kind = TOK_NUMBER;
    t.ival = 0;
    t.dval = 0.0;
    t.is_float = 0;
    t.sval = 0;

    int base = 10;
    int maybe_float = 0;

    if (t.start[0] == '0' && (input_peek(lx) == 'x' || input_peek(lx) == 'X')) {
        base = 16;
        advance(lx);
    } else if (t.start[0] == '0' && is_digit(input_peek(lx))) {
        /* 以 0 开头且后跟数字：八进制字面量（如 0777 = 511） */
        base = 8;
        t.ival = 0;
    } else if (t.start[0] == '.') {
        /* 以 . 开头的浮点数：.5 .25 等 */
        maybe_float = 1;
    } else if (is_digit(t.start[0])) {
        t.ival = t.start[0] - '0';  /* 把已消费的首位算回来 */
    }

    /* 读取整数位（十进制/十六进制/八进制） */
    while (1) {
        int c = input_peek(lx);
        if (base == 8) {
            /* 八进制：只接受 0-7 */
            if (c >= '0' && c <= '7') {
                t.ival = t.ival * 8 + (c - '0');
                advance(lx);
            } else {
                break;
            }
        } else if (is_digit(c)) {
            t.ival = t.ival * base + (c - '0');
            advance(lx);
        } else if (base == 16 && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            t.ival = t.ival * 16 + ((c >= 'a') ? (c - 'a' + 10) : (c - 'A' + 10));
            advance(lx);
        } else {
            break;
        }
    }

    /* 小数部分（仅十进制，且下一位必须是数字：3.f → 3 . f，不是 float） */
    if (base == 10 && input_peek(lx) == '.') {
        int next = (lx->pos + 1 < lx->end) ? (unsigned char)*(lx->pos + 1) : -1;
        if (is_digit(next)) {
            maybe_float = 1;
            advance(lx);  /* 跳过 . */
            while (is_digit(input_peek(lx)))
                advance(lx);
        }
    }

    /* 指数部分（仅十进制，下一个必须是数字或符号） */
    if (base == 10 && (input_peek(lx) == 'e' || input_peek(lx) == 'E')) {
        int next = (lx->pos + 1 < lx->end) ? (unsigned char)*(lx->pos + 1) : -1;
        if (is_digit(next) || next == '+' || next == '-') {
            maybe_float = 1;
            advance(lx);  /* 跳过 e/E */
            if (input_peek(lx) == '+' || input_peek(lx) == '-')
                advance(lx);
            while (is_digit(input_peek(lx)))
                advance(lx);
        }
    }

    /* 计算浮点值 */
    if (maybe_float) {
        t.is_float = 1;
        t.dval = simple_atof(t.start, lx->pos - t.start);
    }

    /* 跳过后缀：u/l/ll（整数）和 f/F（浮点） */
    while (1) {
        int c = input_peek(lx);
        if (c == 'u' || c == 'U' || c == 'l' || c == 'L' ||
            c == 'f' || c == 'F')
            advance(lx);
        else
            break;
    }

    t.len = lx->pos - t.start;
    return t;
}

/* ─── 读标识符/关键字 ─── */

static Token read_ident(Lexer *lx) {
    Token t;
    t.start = lx->pos - 1;
    t.kind = TOK_IDENT;
    t.ival = 0;
    t.sval = 0;

    while (is_alnum(input_peek(lx)))
        advance(lx);

    t.len = lx->pos - t.start;
    t.kind = keyword_lookup(t.start, t.len);
    return t;
}

/* ─── 读字符串 ─── */

static Token read_string(Lexer *lx) {
    Token t;
    t.start = lx->pos - 1;
    t.kind = TOK_STRING;
    t.ival = 0;
    t.sval = 0;

    while (1) {
        int c = input_peek(lx);
        if (c == '"') {
            advance(lx);
            break;
        }
        if (c == '\n' || c == (-1)) {
            t.kind = TOK_ERROR;
            break;
        }
        if (c == '\\') {
            advance(lx);
            if (input_peek(lx) != (-1)) advance(lx);
        } else {
            advance(lx);
        }
    }
    t.len = lx->pos - t.start;
    return t;
}

/* ─── 主力函数：取下一个 Token ─── */

Token lexer_next(Lexer *lx) {
    /* 跳过空白和注释 */
    while (1) {
        int c = input_peek(lx);
        if (c == (-1)) {
            Token t;
            t.kind = TOK_EOF;
            t.start = lx->pos;
            t.len = 0;
            t.ival = 0;
            t.sval = 0;
            lx->cur = t;
            return t;
        }
        if (c == '/' && lx->pos + 1 < lx->end) {
            int n = *(lx->pos + 1);
            if (n == '/' || n == '*') {
                advance(lx);
                skip_comment(lx, n);
                continue;
            }
        }
        /* 跳过预处理器行（# 行号 "file"） */
        if (c == '#' && lx->col <= 2) {
            while (input_peek(lx) != '\n' && input_peek(lx) != (-1))
                advance(lx);
            continue;
        }
        if (is_ws(c)) {
            advance(lx);
            continue;
        }
        break;
    }

    lx->start = lx->pos;
    int c = input_peek(lx);
    advance(lx);

    Token t;
    t.start = lx->start;
    t.ival = 0;
    t.sval = 0;
    t.is_float = 0;

    switch (c) {
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        t = read_number(lx);
        break;

    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
    case 'h': case 'i': case 'j': case 'k': case 'l': case 'm': case 'n':
    case 'o': case 'p': case 'q': case 'r': case 's': case 't': case 'u':
    case 'v': case 'w': case 'x': case 'y': case 'z':
    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G':
    case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': case 'N':
    case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U':
    case 'V': case 'W': case 'X': case 'Y': case 'Z':
    case '_':
        t = read_ident(lx);
        break;

    case '"':
        t = read_string(lx);
        break;

    case '\'': {
        t.kind = TOK_NUMBER;
        if (input_peek(lx) == '\\') {
            advance(lx);
            if (input_peek(lx) == 'x') {
                /* \xAB 十六进制转义 */
                advance(lx);
                int hex_val = 0;
                int hex_digits = 0;
                while (hex_digits < 2) {
                    int c = input_peek(lx);
                    if (c >= '0' && c <= '9') { hex_val = hex_val * 16 + (c - '0'); advance(lx); hex_digits++; }
                    else if (c >= 'a' && c <= 'f') { hex_val = hex_val * 16 + (c - 'a' + 10); advance(lx); hex_digits++; }
                    else if (c >= 'A' && c <= 'F') { hex_val = hex_val * 16 + (c - 'A' + 10); advance(lx); hex_digits++; }
                    else break;
                }
                t.ival = hex_val;
            } else if (input_peek(lx) >= '0' && input_peek(lx) <= '7') {
                /* \NNN 八进制转义：读取最多 3 位 */
                int oct_val = 0;
                int oct_digits = 0;
                while (oct_digits < 3) {
                    int c = input_peek(lx);
                    if (c >= '0' && c <= '7') { oct_val = oct_val * 8 + (c - '0'); advance(lx); oct_digits++; }
                    else break;
                }
                t.ival = oct_val;
            } else {
                switch (input_peek(lx)) {
                case 'n': t.ival = '\n'; break;
                case 't': t.ival = '\t'; break;
                case 'r': t.ival = '\r'; break;
                case '0': t.ival = '\0'; break;
                case '\\': t.ival = '\\'; break;
                case '\'': t.ival = '\''; break;
                case '"': t.ival = '"'; break;
                default:  t.ival = input_peek(lx); break;
                }
                advance(lx);
            }
        } else {
            t.ival = input_peek(lx);
            advance(lx);
        }
        if (input_peek(lx) == '\'')
            advance(lx);
        else
            t.kind = TOK_ERROR;
        break;
    }

    case ';': t.kind = TOK_SEMI; break;
    case '{': t.kind = TOK_LBRACE; break;
    case '}': t.kind = TOK_RBRACE; break;
    case '(': t.kind = TOK_LPAREN; break;
    case ')': t.kind = TOK_RPAREN; break;
    case '[': t.kind = TOK_LBRACKET; break;
    case ']': t.kind = TOK_RBRACKET; break;
    case ',': t.kind = TOK_COMMA; break;
    case '?': t.kind = TOK_QUESTION; break;
    case '~': t.kind = TOK_TILDE; break;
    case '^':
        if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_CARET_EQ; }
        else t.kind = TOK_CARET;
        break;

    case '.':
        if (is_digit(input_peek(lx))) {
            t = read_number(lx);
        } else if (input_peek(lx) == '.') { advance(lx);
            if (input_peek(lx) == '.') { advance(lx); t.kind = TOK_ELLIPSIS; }
            else t.kind = TOK_ERROR;
        } else t.kind = TOK_DOT;
        break;

    case '+':
        if (input_peek(lx) == '+') { advance(lx); t.kind = TOK_PLUS_PLUS; }
        else if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_PLUS_EQ; }
        else t.kind = TOK_PLUS;
        break;

    case '-':
        if (input_peek(lx) == '-') { advance(lx); t.kind = TOK_MINUS_MINUS; }
        else if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_MINUS_EQ; }
        else if (input_peek(lx) == '>') { advance(lx); t.kind = TOK_ARROW; }
        else t.kind = TOK_MINUS;
        break;

    case '*':
        if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_STAR_EQ; }
        else t.kind = TOK_STAR;
        break;

    case '/':
        if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_SLASH_EQ; }
        else t.kind = TOK_SLASH;
        break;

    case '%':
        if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_PERCENT_EQ; }
        else t.kind = TOK_PERCENT;
        break;

    case '&':
        if (input_peek(lx) == '&') { advance(lx); t.kind = TOK_AND_AND; }
        else if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_AND_EQ; }
        else t.kind = TOK_AMPERSAND;
        break;

    case '|':
        if (input_peek(lx) == '|') { advance(lx); t.kind = TOK_OR_OR; }
        else if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_OR_EQ; }
        else t.kind = TOK_PIPE;
        break;

    case '<':
        if (input_peek(lx) == '<') { advance(lx);
            if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_LESS_LESS_EQ; }
            else t.kind = TOK_LESS_LESS;
        } else if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_LESS_EQ; }
        else t.kind = TOK_LESS;
        break;

    case '>':
        if (input_peek(lx) == '>') { advance(lx);
            if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_GREATER_GREATER_EQ; }
            else t.kind = TOK_GREATER_GREATER;
        } else if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_GREATER_EQ; }
        else t.kind = TOK_GREATER;
        break;

    case '=':
        if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_EQ_EQ; }
        else t.kind = TOK_EQ;
        break;

    case '!':
        if (input_peek(lx) == '=') { advance(lx); t.kind = TOK_NOT_EQ; }
        else t.kind = TOK_EXCLAM;
        break;

    case ':': t.kind = TOK_COLON; break;

    default:
        t.kind = TOK_ERROR;
        break;
    }

    t.len = lx->pos - t.start;
    lx->cur = t;
    return t;
}

Token lexer_peek(Lexer *lx) {
    return lx->cur;
}

// ============================================================
// 测试框架 — syscall 包装 + print 工具
// ============================================================

static long __sys_write(int fd, const char *buf, unsigned long len)
{
    return syscall(1, fd, buf, len);
}

static void __sys_exit(int code)
{
    __asm__ __volatile__ ("syscall"
        : : "a"((long)60), "D"((long)code)
        : "rcx", "r11", "memory");
    for (;;) ;
}

/* 简易整数输出 */
static void print_str(const char *s) {
    int n = 0;
    while (s[n]) n++;
    __sys_write(1, s, n);
}

static void print_dec(long n) {
    char buf[32];
    int i = 0;
    if (n < 0) { print_str("-"); n = -n; }
    if (n == 0) { buf[i++] = '0'; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) { __sys_write(1, &buf[--i], 1); }
}

/* 测试计数 */
static int test_passed = 0;
static int test_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        print_str("  FAIL "); print_str(msg); print_str("\n"); \
        test_failed++; \
    } else { \
        test_passed++; \
    } \
} while (0)

#define CHECK_KIND(got, expected, msg) do { \
    if ((got) != (expected)) { \
        print_str("  FAIL "); print_str(msg); \
        print_str(": got "); print_dec(got); \
        print_str(" expected "); print_dec(expected); \
        print_str("\n"); \
        test_failed++; \
    } else { \
        test_passed++; \
    } \
} while (0)

#define CHECK_IVAL(got, expected, msg) do { \
    if ((got) != (expected)) { \
        print_str("  FAIL "); print_str(msg); \
        print_str(": got "); print_dec(got); \
        print_str(" expected "); print_dec(expected); \
        print_str("\n"); \
        test_failed++; \
    } else { \
        test_passed++; \
    } \
} while (0)

static void run_section(const char *name) {
    print_str("\n--- "); print_str(name); print_str(" ---\n");
    test_passed = 0;
    test_failed = 0;
}

static void print_section_result(void) {
    print_str("  -> ");
    print_dec(test_passed); print_str(" passed, ");
    print_dec(test_failed); print_str(" failed\n");
}

// ============================================================
// 词法分析器测试用例
// ============================================================

/* 字符串长度辅助（替代 strlen，避免依赖 tcc_rt.c） */
static int slen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void test_numbers(void) {
    run_section("Numbers");

    {
        const char *src = "42";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_KIND(t.kind, TOK_NUMBER, "42 kind");
        CHECK_IVAL(t.ival, 42, "42 value");
        CHECK_IVAL(t.len, 2, "42 len");
    }

    {
        const char *src = "0";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_KIND(t.kind, TOK_NUMBER, "0 kind");
        CHECK_IVAL(t.ival, 0, "0 value");
    }

    {
        const char *src = "0xFF";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_KIND(t.kind, TOK_NUMBER, "0xFF kind");
        CHECK_IVAL(t.ival, 255, "0xFF value");
        CHECK_IVAL(t.len, 4, "0xFF len");
    }

    {
        const char *src = "0777";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_KIND(t.kind, TOK_NUMBER, "0777 kind");
        CHECK_IVAL(t.ival, 511, "0777 value (8*8*8+8*8+8+7)");
    }

    {
        const char *src = "123u";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_KIND(t.kind, TOK_NUMBER, "123u kind");
        CHECK_IVAL(t.ival, 123, "123u value");
    }

    {
        const char *src = "42L";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_KIND(t.kind, TOK_NUMBER, "42L kind");
        CHECK_IVAL(t.ival, 42, "42L value");
    }

    {
        const char *src = "0xABCD";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_KIND(t.kind, TOK_NUMBER, "0xABCD kind");
        CHECK_IVAL(t.ival, 0xABCD, "0xABCD value");
    }

    /* 浮点数：含小数点 */
    {
        const char *src = "3.14";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_KIND(t.kind, TOK_NUMBER, "3.14 kind");
        CHECK(t.is_float, "3.14 is_float");
    }

    /* 浮点数：以 . 开头 */
    {
        const char *src = ".5";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_KIND(t.kind, TOK_NUMBER, ".5 kind");
        CHECK(t.is_float, ".5 is_float");
    }

    /* 3.f 不是浮点数（. 后不是数字，是运算符 . 和标识符 f） */
    {
        const char *src = "3.f";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t1 = lexer_next(&lx);
        CHECK_KIND(t1.kind, TOK_NUMBER, "3.f t1 kind");
        CHECK_IVAL(t1.ival, 3, "3.f t1=3");
        Token t2 = lexer_next(&lx);
        CHECK_KIND(t2.kind, TOK_DOT, "3.f t2 dot");
        Token t3 = lexer_next(&lx);
        CHECK_KIND(t3.kind, TOK_IDENT, "3.f t3 ident");
    }
}

static void test_char_constants(void) {
    run_section("Character Constants");

    {
        const char *src = "'a'";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_KIND(t.kind, TOK_NUMBER, "'a' kind");
        CHECK_IVAL(t.ival, 'a', "'a' value");
    }

    {
        const char *src = "'\\n'";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_KIND(t.kind, TOK_NUMBER, "'\\n' kind");
        CHECK_IVAL(t.ival, '\n', "'\\n' value");
    }

    {
        const char *src = "'\\t'";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_IVAL(t.ival, '\t', "'\\t' value");
    }

    {
        const char *src = "'\\\\'";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_IVAL(t.ival, '\\', "'\\\\' value");
    }

    {
        const char *src = "'\\''";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_IVAL(t.ival, '\'', "'\\'' value");
    }

    {
        const char *src = "'\\x41'";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_IVAL(t.ival, 0x41, "'\\x41' value (65)");
    }

    {
        const char *src = "'\\0'";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_IVAL(t.ival, 0, "'\\0' value");
    }

    /* 未闭合字符常量 → TOK_ERROR */
    {
        const char *src = "'a";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_KIND(t.kind, TOK_ERROR, "'a (unclosed) error");
    }
}

static void test_strings(void) {
    run_section("String Literals");

    {
        const char *src = "\"hello\"";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_KIND(t.kind, TOK_STRING, "\"hello\" kind");
    }

    /* 含转义字符的字符串 */
    {
        const char *src = "\"hello\\nworld\"";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_KIND(t.kind, TOK_STRING, "\"hello\\nworld\" kind");
    }

    /* 未闭合字符串 → TOK_ERROR */
    {
        const char *src = "\"unclosed";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_KIND(t.kind, TOK_ERROR, "unclosed string error");
    }
}

static void test_keywords(void) {
    run_section("Keywords");

    /* 每个关键字独立测试 */
    const char src[] = "int void char short long unsigned signed "
                       "return if else while for do "
                       "break continue switch case default goto "
                       "struct union enum typedef const volatile "
                       "static extern inline sizeof "
                       "__asm__ __attribute__ __builtin_va_list";
    Lexer lx; lexer_init(&lx, src, slen(src));
    int expected[] = {
        TOK_INT, TOK_VOID, TOK_CHAR, TOK_SHORT, TOK_LONG, TOK_UNSIGNED, TOK_SIGNED,
        TOK_RETURN, TOK_IF, TOK_ELSE, TOK_WHILE, TOK_FOR, TOK_DO,
        TOK_BREAK, TOK_CONTINUE, TOK_SWITCH, TOK_CASE, TOK_DEFAULT, TOK_GOTO,
        TOK_STRUCT, TOK_UNION, TOK_ENUM, TOK_TYPEDEF, TOK_CONST, TOK_VOLATILE,
        TOK_STATIC, TOK_EXTERN, TOK_INLINE, TOK_SIZEOF,
        TOK__ASM__, TOK__ATTRIBUTE__, TOK__BUILTIN_VA_LIST,
    };
    int i;
    for (i = 0; i < (int)(sizeof(expected)/sizeof(expected[0])); i++) {
        Token t = lexer_next(&lx);
        if (t.kind != expected[i]) {
            print_str("  FAIL keyword["); print_dec(i);
            print_str("]: got "); print_dec(t.kind);
            print_str(" expected "); print_dec(expected[i]);
            print_str("\n");
            test_failed++;
        } else {
            test_passed++;
        }
    }
}

static void test_identifiers(void) {
    run_section("Identifiers");

    {
        const char *src = "foo bar _baz my_var123 FOO";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t1 = lexer_next(&lx);
        CHECK_KIND(t1.kind, TOK_IDENT, "foo ident");
        CHECK_IVAL(t1.len, 3, "foo len");
        Token t2 = lexer_next(&lx);
        CHECK_KIND(t2.kind, TOK_IDENT, "bar ident");
        Token t3 = lexer_next(&lx);
        CHECK_KIND(t3.kind, TOK_IDENT, "_baz ident");
        Token t4 = lexer_next(&lx);
        CHECK_KIND(t4.kind, TOK_IDENT, "my_var123 ident");
        Token t5 = lexer_next(&lx);
        CHECK_KIND(t5.kind, TOK_IDENT, "FOO ident");
    }

    /* auto 和 float 被视为标识符 */
    {
        const char *src = "auto float";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t1 = lexer_next(&lx);
        CHECK_KIND(t1.kind, TOK_IDENT, "auto is ident");
        Token t2 = lexer_next(&lx);
        CHECK_KIND(t2.kind, TOK_IDENT, "float is ident");
    }
}

static void test_operators(void) {
    run_section("Operators");

    {
        const char *src = "; { } ( ) [ ] , ? ~";
        Lexer lx; lexer_init(&lx, src, slen(src));
        CHECK_KIND(lexer_next(&lx).kind, TOK_SEMI, ";");
        CHECK_KIND(lexer_next(&lx).kind, TOK_LBRACE, "{");
        CHECK_KIND(lexer_next(&lx).kind, TOK_RBRACE, "}");
        CHECK_KIND(lexer_next(&lx).kind, TOK_LPAREN, "(");
        CHECK_KIND(lexer_next(&lx).kind, TOK_RPAREN, ")");
        CHECK_KIND(lexer_next(&lx).kind, TOK_LBRACKET, "[");
        CHECK_KIND(lexer_next(&lx).kind, TOK_RBRACKET, "]");
        CHECK_KIND(lexer_next(&lx).kind, TOK_COMMA, ",");
        CHECK_KIND(lexer_next(&lx).kind, TOK_QUESTION, "?");
        CHECK_KIND(lexer_next(&lx).kind, TOK_TILDE, "~");
    }

    /* 比较/逻辑 */
    {
        const char *src = "< > <= >= == !=";
        Lexer lx; lexer_init(&lx, src, slen(src));
        CHECK_KIND(lexer_next(&lx).kind, TOK_LESS, "<");
        CHECK_KIND(lexer_next(&lx).kind, TOK_GREATER, ">");
        CHECK_KIND(lexer_next(&lx).kind, TOK_LESS_EQ, "<=");
        CHECK_KIND(lexer_next(&lx).kind, TOK_GREATER_EQ, ">=");
        CHECK_KIND(lexer_next(&lx).kind, TOK_EQ_EQ, "==");
        CHECK_KIND(lexer_next(&lx).kind, TOK_NOT_EQ, "!=");
    }

    /* 算术 */
    {
        const char *src = "+ - * / % ! & | ^ =";
        Lexer lx; lexer_init(&lx, src, slen(src));
        CHECK_KIND(lexer_next(&lx).kind, TOK_PLUS, "+");
        CHECK_KIND(lexer_next(&lx).kind, TOK_MINUS, "-");
        CHECK_KIND(lexer_next(&lx).kind, TOK_STAR, "*");
        CHECK_KIND(lexer_next(&lx).kind, TOK_SLASH, "/");
        CHECK_KIND(lexer_next(&lx).kind, TOK_PERCENT, "%");
        CHECK_KIND(lexer_next(&lx).kind, TOK_EXCLAM, "!");
        CHECK_KIND(lexer_next(&lx).kind, TOK_AMPERSAND, "&");
        CHECK_KIND(lexer_next(&lx).kind, TOK_PIPE, "|");
        CHECK_KIND(lexer_next(&lx).kind, TOK_CARET, "^");
        CHECK_KIND(lexer_next(&lx).kind, TOK_EQ, "=");
    }

    /* 复合赋值 */
    {
        const char *src = "+= -= *= /= %= <<= >>= &= |= ^=";
        Lexer lx; lexer_init(&lx, src, slen(src));
        CHECK_KIND(lexer_next(&lx).kind, TOK_PLUS_EQ, "+=");
        CHECK_KIND(lexer_next(&lx).kind, TOK_MINUS_EQ, "-=");
        CHECK_KIND(lexer_next(&lx).kind, TOK_STAR_EQ, "*=");
        CHECK_KIND(lexer_next(&lx).kind, TOK_SLASH_EQ, "/=");
        CHECK_KIND(lexer_next(&lx).kind, TOK_PERCENT_EQ, "%=");
        CHECK_KIND(lexer_next(&lx).kind, TOK_LESS_LESS_EQ, "<<=");
        CHECK_KIND(lexer_next(&lx).kind, TOK_GREATER_GREATER_EQ, ">>=");
        CHECK_KIND(lexer_next(&lx).kind, TOK_AND_EQ, "&=");
        CHECK_KIND(lexer_next(&lx).kind, TOK_OR_EQ, "|=");
        CHECK_KIND(lexer_next(&lx).kind, TOK_CARET_EQ, "^=");
    }

    /* 自增自减、箭头 */
    {
        const char *src = "++ -- ->";
        Lexer lx; lexer_init(&lx, src, slen(src));
        CHECK_KIND(lexer_next(&lx).kind, TOK_PLUS_PLUS, "++");
        CHECK_KIND(lexer_next(&lx).kind, TOK_MINUS_MINUS, "--");
        CHECK_KIND(lexer_next(&lx).kind, TOK_ARROW, "->");
    }

    /* 逻辑/移位 */
    {
        const char *src = "&& || << >>";
        Lexer lx; lexer_init(&lx, src, slen(src));
        CHECK_KIND(lexer_next(&lx).kind, TOK_AND_AND, "&&");
        CHECK_KIND(lexer_next(&lx).kind, TOK_OR_OR, "||");
        CHECK_KIND(lexer_next(&lx).kind, TOK_LESS_LESS, "<<");
        CHECK_KIND(lexer_next(&lx).kind, TOK_GREATER_GREATER, ">>");
    }

    /* 特殊 */
    {
        const char *src = ": . ...";
        Lexer lx; lexer_init(&lx, src, slen(src));
        CHECK_KIND(lexer_next(&lx).kind, TOK_COLON, ":");
        CHECK_KIND(lexer_next(&lx).kind, TOK_DOT, ".");
        CHECK_KIND(lexer_next(&lx).kind, TOK_ELLIPSIS, "...");
    }
}

static void test_comments(void) {
    run_section("Comments");

    /* 行注释 */
    {
        const char *src = "int // comment\nx;";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t1 = lexer_next(&lx);
        CHECK_KIND(t1.kind, TOK_INT, "line comment: int");
        Token t2 = lexer_next(&lx);
        CHECK_KIND(t2.kind, TOK_IDENT, "line comment: x");
        CHECK_IVAL(t2.len, 1, "line comment: x len");
        Token t3 = lexer_next(&lx);
        CHECK_KIND(t3.kind, TOK_SEMI, "line comment: ;");
    }

    /* 块注释 */
    {
        const char *src = "a /* block\ncomment */ b";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t1 = lexer_next(&lx);
        CHECK_KIND(t1.kind, TOK_IDENT, "block comment: a");
        Token t2 = lexer_next(&lx);
        CHECK_KIND(t2.kind, TOK_IDENT, "block comment: b");
    }

    /* 行中块注释 */
    {
        const char *src = "x/**/y";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t1 = lexer_next(&lx);
        CHECK_KIND(t1.kind, TOK_IDENT, "inline block: x");
        Token t2 = lexer_next(&lx);
        CHECK_KIND(t2.kind, TOK_IDENT, "inline block: y");
    }
}

static void test_preprocessor_skip(void) {
    run_section("Preprocessor Line Skipping");

    {
        const char *src = "# 1 \"test.c\"\nint x;\n";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t1 = lexer_next(&lx);
        CHECK_KIND(t1.kind, TOK_INT, "preproc skip: int");
        Token t2 = lexer_next(&lx);
        CHECK_KIND(t2.kind, TOK_IDENT, "preproc skip: x");
        Token t3 = lexer_next(&lx);
        CHECK_KIND(t3.kind, TOK_SEMI, "preproc skip: ;");
    }

    /* # 开头的非预处理行（不在行首）应正常解析 */
    {
        const char *src = "a\n # b\nc";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t1 = lexer_next(&lx);
        CHECK_KIND(t1.kind, TOK_IDENT, "col>2 #: a");
        (void)lexer_next(&lx);
        /* '#' 在 col=2 不算行首模式，具体行为取决于实现 */
        /* 这里只验证不崩溃，token 留到 EOF */
    }
}

static void test_boundary(void) {
    run_section("Boundary Conditions");

    /* 空输入 */
    {
        const char *src = "";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_KIND(t.kind, TOK_EOF, "empty input");
    }

    /* 仅空白 */
    {
        const char *src = "   \t\n  ";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_KIND(t.kind, TOK_EOF, "whitespace only");
    }

    /* 仅注释 */
    {
        const char *src = "// comment\n/* block */";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_KIND(t.kind, TOK_EOF, "comments only");
    }

    /* 非法字符 */
    {
        const char *src = "@`$";
        Lexer lx; lexer_init(&lx, src, slen(src));
        int i;
        for (i = 0; i < 3; i++) {
            Token t = lexer_next(&lx);
            CHECK_KIND(t.kind, TOK_ERROR, "illegal char");
        }
    }

    /* 连续多个非法字符 */
    {
        const char *src = "@@@@";
        Lexer lx; lexer_init(&lx, src, slen(src));
        int i;
        for (i = 0; i < 4; i++) {
            Token t = lexer_next(&lx);
            CHECK_KIND(t.kind, TOK_ERROR, "multiple illegal chars");
        }
        Token t = lexer_next(&lx);
        CHECK_KIND(t.kind, TOK_EOF, "EOF after illegal");
    }

    /* . 单独（非 ... 开头）后跟非数字 */
    {
        const char *src = ".x";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_KIND(t.kind, TOK_DOT, ". (dot) before ident");
        Token t2 = lexer_next(&lx);
        CHECK_KIND(t2.kind, TOK_IDENT, "ident after dot");
    }

    /* .. 是错误（... 需要三个点） */
    {
        const char *src = "..";
        Lexer lx; lexer_init(&lx, src, slen(src));
        Token t = lexer_next(&lx);
        CHECK_KIND(t.kind, TOK_ERROR, ".. is error");
    }
}

static void test_tokenize_fragment(void) {
    run_section("C Fragment Tokenization");

    {
        const char *src = "int main(void) { return 42; }";
        Lexer lx; lexer_init(&lx, src, slen(src));
        CHECK_KIND(lexer_next(&lx).kind, TOK_INT, "frag: int");
        CHECK_KIND(lexer_next(&lx).kind, TOK_IDENT, "frag: main");
        CHECK_KIND(lexer_next(&lx).kind, TOK_LPAREN, "frag: (");
        CHECK_KIND(lexer_next(&lx).kind, TOK_VOID, "frag: void");
        CHECK_KIND(lexer_next(&lx).kind, TOK_RPAREN, "frag: )");
        CHECK_KIND(lexer_next(&lx).kind, TOK_LBRACE, "frag: {");
        CHECK_KIND(lexer_next(&lx).kind, TOK_RETURN, "frag: return");
        CHECK_KIND(lexer_next(&lx).kind, TOK_NUMBER, "frag: 42");
        CHECK_KIND(lexer_next(&lx).kind, TOK_SEMI, "frag: ;");
        CHECK_KIND(lexer_next(&lx).kind, TOK_RBRACE, "frag: }");
        CHECK_KIND(lexer_next(&lx).kind, TOK_EOF, "frag: EOF");
    }
}

// ============================================================
// 入口
// ============================================================

void __tlibc_start(void) {
    print_str("=== lex.c standalone tests ===\n");

    test_numbers();
    print_section_result();

    test_char_constants();
    print_section_result();

    test_strings();
    print_section_result();

    test_keywords();
    print_section_result();

    test_identifiers();
    print_section_result();

    test_operators();
    print_section_result();

    test_comments();
    print_section_result();

    test_preprocessor_skip();
    print_section_result();

    test_boundary();
    print_section_result();

    test_tokenize_fragment();
    print_section_result();

    /* 汇总 */
    print_str("\n=== ");
    if (test_failed == 0) {
        print_str("ALL PASSED");
    } else {
        print_str("SOME FAILED");
    }
    print_str(" ===\n");

    __sys_exit(test_failed != 0 ? 1 : 0);
}
