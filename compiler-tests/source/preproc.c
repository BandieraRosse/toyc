// EXPECT: 0
// preproc.c standalone test (simulated preprocessed state)
//
// Compile (gcc):
//   gcc -nostdlib -ffreestanding -Wall -Wextra -Wl,-e,__tlibc_start
//       compiler-tests/source/preproc.c -o /tmp/test_preproc
// Run:  /tmp/test_preproc
//
// Self-host (future):
//   build/tcc compiler-tests/source/preproc.c -o /tmp/tpre.o
//   ld -nostdlib -static -T ld.script /tmp/tpre.o -o /tmp/tpre
//   /tmp/tpre

// ============================================================
// Inlined from tcc_need.h — 基础类型 / syscall 宏
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

/* x86_64 syscall — 仅测试框架自用 */
static inline long __syscall3(long n, long a1, long a2, long a3)
{
    unsigned long ret;
    __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2),
                          "d"(a3) : "rcx", "r11", "memory");
    return (long)ret;
}
#define syscall(N, A1, A2, A3) __syscall3((long)(N), (long)(A1), (long)(A2), (long)(A3))

/* 文件 I/O 常量 */
#define AT_FDCWD    (-100)
#define O_RDONLY    0
#define SEEK_SET    0
#define SEEK_END    2

// ============================================================
// Inlined from tcc.h — 仅 preproc.c 实际使用的部分
// ============================================================

#define MAX_INCLUDE_PATHS 16

// ============================================================
// 运行时 stub — 替代 tcc_rt.c 的功能
// ============================================================

/* ─── syscall 输出 ─── */

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

/* ─── 简易堆（bump allocator）替代真正的 malloc ─── */

#define HEAP_SIZE (4 * 1024 * 1024)  /* 4MB — 测试够用 */
static char heap_buf[HEAP_SIZE];
static unsigned long heap_used;

static void heap_init(void) {
    heap_used = 0;
}

void *tlibc_malloc(unsigned long size) {
    /* 对齐到 8 字节 */
    unsigned long aligned = (size + 7) & ~7UL;
    if (heap_used + aligned > HEAP_SIZE) {
        __sys_write(2, "tlibc_malloc: OOM\n", 18);
        __sys_exit(1);
    }
    void *p = heap_buf + heap_used;
    heap_used += aligned;
    /* 清零 */
    { unsigned long i; for (i = 0; i < size; i++) ((char *)p)[i] = 0; }
    return p;
}

void tlibc_free(void *ptr) {
    (void)ptr;  /* bump allocator 不回收 */
}

/* ─── strlen ─── */

int strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

/* ─── strcmp ─── */

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/* ─── __printf 最小实现（仅支持 %s %c %% \n） ─── */

void __printf(const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    const char *p = fmt;
    while (*p) {
        if (*p == '%') {
            p++;
            if (*p == 's') {
                p++;
                const char *s = __builtin_va_arg(ap, const char *);
                if (s) { int sl; for (sl = 0; s[sl]; sl++) __sys_write(2, &s[sl], 1); }
                else __sys_write(2, "(null)", 6);
            } else if (*p == 'c') {
                p++;
                char c = (char)__builtin_va_arg(ap, int);
                __sys_write(2, &c, 1);
            } else if (*p == '%') {
                p++;
                __sys_write(2, "%", 1);
            } else if (*p == 'd') {
                /* 简易版：只处理 single digit */
                p++;
                int v = __builtin_va_arg(ap, int);
                if (v < 0) { __sys_write(2, "-", 1); v = -v; }
                char buf[32]; int bi = 0;
                if (v == 0) buf[bi++] = '0';
                while (v > 0) { buf[bi++] = '0' + (v % 10); v /= 10; }
                { int j; for (j = bi - 1; j >= 0; j--) __sys_write(2, &buf[j], 1); }
            } else if (*p == 'l') {
                p++;  /* 跳过 l（假设 long 是 %ld，走 %d 分支） */
                if (*p == 'd') {
                    p++;
                    long v = __builtin_va_arg(ap, long);
                    if (v < 0) { __sys_write(2, "-", 1); v = -v; }
                    char buf[32]; int bi = 0;
                    if (v == 0) buf[bi++] = '0';
                    while (v > 0) { buf[bi++] = '0' + (v % 10); v /= 10; }
                    { int j; for (j = bi - 1; j >= 0; j--) __sys_write(2, &buf[j], 1); }
                }
            } else {
                __sys_write(2, "%", 1);
                __sys_write(2, p, 1);
                p++;
            }
        } else {
            __sys_write(2, p, 1);
            p++;
        }
    }
    __builtin_va_end(ap);
}

/* ─── 文件 I/O stub（#include 测试用：全部返回 失败） ─── */

int __openat(int dirfd, const char *path, int flags, int mode) {
    (void)dirfd; (void)path; (void)flags; (void)mode;
    return -1;  /* 文件不存在 */
}

long __read(int fd, void *buf, size_t count) {
    (void)fd; (void)buf; (void)count;
    return 0;
}

int __close(int fd) {
    (void)fd;
    return 0;
}

off_t __lseek(int fd, off_t offset, int whence) {
    (void)fd; (void)offset; (void)whence;
    return 0;
}

// ============================================================
// preproc.c 源文件 — 完全来自 app/preproc.c
// ============================================================

#define MAX_MACROS 4096
#define MAX_FUNC_MACROS 1024
#define MAX_MACRO_PARAMS 64

typedef struct { const char *name; const char *value; int value_len; } Macro;
static Macro macros[MAX_MACROS];
static int macro_count;

typedef struct {
    const char *name;
    const char *params[MAX_MACRO_PARAMS];
    int param_count;
    int is_variadic;
    const char *replacement;
    int repl_len;
} FuncMacro;
static FuncMacro func_macros[MAX_FUNC_MACROS];
static int func_macro_count;

/* 宏展开栈：防止无限递归 */
#define MAX_EXPAND_STACK 64
static const char *expand_stack[MAX_EXPAND_STACK];
static int expand_stack_depth;

static int is_expanding(const char *name) {
    int i;
    for (i = 0; i < expand_stack_depth; i++)
        if (expand_stack[i] == name) return 1;
    return 0;
}

static const char *inc_paths[MAX_INCLUDE_PATHS];
static int inc_path_count;

void add_include_path(const char *path) {
    if (inc_path_count < MAX_INCLUDE_PATHS) inc_paths[inc_path_count++] = path; }

static void add_macro(const char *name, const char *val, int vlen) {
    if (macro_count < MAX_MACROS) {
        macros[macro_count].name = name; macros[macro_count].value = val;
        macros[macro_count].value_len = vlen; macro_count++; } }

static void undef_macro(const char *name) {
    int i; for (i = 0; i < macro_count; i++) {
        int j; for (j = 0; name[j]; j++) if (macros[i].name[j] != name[j]) goto nx;
        if (macros[i].name[j] == '\0') { macros[i] = macros[--macro_count]; return; }
        nx:; } }

static int macro_defined(const char *name) {
    int i; for (i = 0; i < macro_count; i++) {
        int j; for (j = 0; name[j]; j++) if (macros[i].name[j] != name[j]) goto nx2;
        if (macros[i].name[j] == '\0') return 1;
        nx2:; } return 0; }

typedef struct { char *data; int len; int cap; } OutBuf;

static void out_putc(OutBuf *b, char c) {
    if (b->data == 0) { b->cap = 65536; b->data = (char *)tlibc_malloc(b->cap); } else if (b->len >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 65536;
        char *nd = (char *)tlibc_malloc(b->cap);
        int i; for (i = 0; i < b->len; i++) nd[i] = b->data[i];
        tlibc_free(b->data); b->data = nd; }
    b->data[b->len++] = c; }

static int pp_ws(char c) { return c==' '||c=='\t'||c=='\n'||c=='\r'; }
static int pp_id(char c) { return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||(c>='0'&&c<='9'); }

static void skip_cond(const char *s, int *pos, int len) {
    int d = 0;
    while (*pos < len) {
        int ls = *pos; while (*pos < len && s[*pos] != '\n') (*pos)++;
        if (*pos < len) (*pos)++;
        int j; int hh = 0;
        for (j = ls; j < *pos && j < len; j++) { if (s[j] == '#') { hh = 1; break; } if (!pp_ws(s[j])) break; }
        if (!hh) continue;
        int p = j + 1; while (p < len && pp_ws(s[p])) p++;
        if (s[p]=='e'&&s[p+1]=='n'&&s[p+2]=='d'&&s[p+3]=='i'&&s[p+4]=='f') { if (d==0) return; d--; }
        else if (s[p]=='e'&&s[p+1]=='l'&&s[p+2]=='s'&&s[p+3]=='e') { if (d==0) return; }
        else if (s[p]=='i'&&s[p+1]=='f') d++;
    }
}

static char *pp_read(const char *path, int *out_len) {
    int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) return 0;
    int sz = __lseek(fd, 0, SEEK_END); __lseek(fd, 0, SEEK_SET);
    if (sz <= 0) { __close(fd); return 0; }
    char *b = (char *)tlibc_malloc(sz + 2);
    int n = __read(fd, b, sz); __close(fd);
    if (n != sz) { tlibc_free(b); return 0; }
    b[sz] = '\0'; *out_len = sz; return b;
}

static void pp_buf_impl(const char *s, int len, OutBuf *out, int depth, int *had_nl);
static void pp_buf(const char *s, int len, OutBuf *out, int depth);

/* 从路径中提取目录部分（不包括文件名和末尾斜杠） */
static void dirname_of(const char *full, int full_len, char *buf, int bufsz) {
    int last_slash = -1;
    int i;
    for (i = 0; i < full_len && full[i]; i++) {
        if (full[i] == '/') last_slash = i;
    }
    if (last_slash < 0) {
        buf[0] = '.';
        buf[1] = '/';
        buf[2] = '\0';
    } else {
        int j;
        for (j = 0; j < last_slash && j < bufsz - 1; j++)
            buf[j] = full[j];
        buf[j] = '\0';
    }
}

static int inc_path_added_source_dir = 0;
static char current_source_dir[1024];

static void do_include(const char *s, int *pos, int len, OutBuf *out, int depth) {
    while (*pos < len && s[*pos] != '"' && s[*pos] != '<') (*pos)++;
    if (*pos >= len) return;
    int delim = s[*pos]; (*pos)++;
    int fs = *pos; while (*pos < len && s[*pos] != delim) (*pos)++;
    int flen = *pos - fs; if (*pos < len) (*pos)++;
    if (flen <= 0) return;
    char fn[512]; int fi;
    for (fi = 0; fi < flen && fi < 500; fi++) { fn[fi] = s[fs + fi]; } fn[fi] = '\0';

    /* 对 #include "..."，先搜索源文件所在目录 */
    if (delim == '"' && current_source_dir[0] && !inc_path_added_source_dir) {
        /* 把源文件目录添加到 inc_paths 开头 */
        int pi;
        for (pi = inc_path_count; pi > 0; pi--)
            inc_paths[pi] = inc_paths[pi - 1];
        const char *dp = (const char *)tlibc_malloc(strlen(current_source_dir) + 1);
        { int ci; for (ci = 0; current_source_dir[ci]; ci++) ((char*)dp)[ci] = current_source_dir[ci]; ((char*)dp)[ci] = '\0'; }
        inc_paths[0] = dp;
        inc_path_count++;
        inc_path_added_source_dir = 1;
    }

    int fnd = 0;
    int pi; for (pi = 0; pi < inc_path_count; pi++) {
        char pth[1024]; int pj;
        for (pj = 0; inc_paths[pi][pj] && pj < 1000; pj++) pth[pj] = inc_paths[pi][pj];
        if (pj > 0 && pth[pj-1] != '/') pth[pj++] = '/';
        for (fi = 0; fi < flen && pj < 1000; fi++, pj++) pth[pj] = s[fs + fi];
        pth[pj] = '\0';
        int l2; char *fc = pp_read(pth, &l2);
        if (fc) { pp_buf(fc, l2, out, depth + 1); tlibc_free(fc); fnd = 1; break; }
    }
    if (!fnd) { __printf("tcc: cannot find '%s'\n", fn); }
}

static void get_name(const char *s, int start, int end, char *buf, int bufsz) {
    int i; int n = end - start;
    if (n > bufsz - 1) n = bufsz - 1;
    for (i = 0; i < n; i++) buf[i] = s[start + i];
    buf[n] = '\0';
}

/* 从替换文本中移除 \ 续行符（含后续空白） */
static char *strip_continuations(const char *src, int len, int *out_len) {
    OutBuf b = { 0, 0, 0 };
    int i = 0;
    while (i < len) {
        if (src[i] == '\\' && i + 1 < len) {
            int ni = i + 1;
            /* 跳过 \n 或 \r\n */
            if (src[ni] == '\r') ni++;
            if (ni < len && src[ni] == '\n') {
                i = ni + 1;
                /* 跳过续行开头的空白 */
                while (i < len && (src[i] == ' ' || src[i] == '\t')) i++;
                continue;
            }
        }
        out_putc(&b, src[i]); i++;
    }
    out_putc(&b, '\0');
    *out_len = b.len - 1;
    return b.data;
}

static void do_directive(const char *s, int ls, int le, OutBuf *out, int depth) {
    int p = ls; while (p < le && pp_ws(s[p])) p++;
    if (p >= le || s[p] != '#') return;
    p++; while (p < le && pp_ws(s[p])) p++;
    int dw = p; while (p < le && !pp_ws(s[p]) && s[p] != '\n') p++;
    int dl = p - dw;

    if (dl == 7 && s[dw]=='i'&&s[dw+1]=='n'&&s[dw+2]=='c'&&s[dw+3]=='l'&&s[dw+4]=='u'&&s[dw+5]=='d'&&s[dw+6]=='e')
        { do_include(s, &p, le, out, depth); return; }

    if (dl == 6 && s[dw]=='d'&&s[dw+1]=='e'&&s[dw+2]=='f') {
        while (p < le && pp_ws(s[p])) p++;
        int ms = p; while (p < le && pp_id(s[p])) p++;
        if (p == ms) return;
        int mnl = p - ms;
        int cp = p; while (cp < le && pp_ws(s[cp])) cp++;
        if (cp == p && cp < le && s[cp] == '(') {
            /* 函数式宏（必须 ( 紧跟宏名，无空白）— 存储定义 */
            if (func_macro_count >= MAX_FUNC_MACROS) return;
            FuncMacro *fm = &func_macros[func_macro_count];
            char *mn2 = (char *)tlibc_malloc(mnl + 1);
            { int ci; for (ci = 0; ci < mnl; ci++) mn2[ci] = s[ms + ci]; mn2[mnl] = '\0'; }
            fm->name = mn2;
            fm->param_count = 0; fm->is_variadic = 0;
            cp++; /* 跳过 ( */
            while (cp < le && s[cp] != ')') {
                while (cp < le && pp_ws(s[cp])) cp++;
                if (cp >= le || s[cp] == ')') break;
                if (cp + 2 <= le && s[cp] == '.' && s[cp+1] == '.' && s[cp+2] == '.') {
                    fm->is_variadic = 1; cp += 3; break;
                }
                int ps = cp;
                while (cp < le && pp_id(s[cp])) cp++;
                if (cp > ps && fm->param_count < MAX_MACRO_PARAMS) {
                    char *pn = (char *)tlibc_malloc(cp - ps + 1);
                    { int ci; for (ci = 0; ci < cp - ps; ci++) pn[ci] = s[ps + ci]; pn[cp - ps] = '\0'; }
                    fm->params[fm->param_count++] = pn;
                }
                while (cp < le && pp_ws(s[cp])) cp++;
                if (cp < le && s[cp] == ',') { cp++; continue; }
            }
            if (cp < le && s[cp] == ')') cp++;
            while (cp < le && pp_ws(s[cp])) cp++;
            {
                int vs = cp; int vl = le - cp;
                while (vl > 0 && pp_ws(s[vs+vl-1])) vl--;
                if (vl > 0) {
                    int cl; char *rp = strip_continuations(s + vs, vl, &cl);
                    fm->replacement = rp; fm->repl_len = cl;
                } else { fm->replacement = 0; fm->repl_len = 0; }
            }
            func_macro_count++;
            return;
        }
        while (p < le && pp_ws(s[p])) p++;
        {
            int vs = p; int vl = le - p;
            while (vl > 0 && pp_ws(s[vs+vl-1])) vl--;
            { int ci; for (ci = 0; ci < vl - 1; ci++) {
                if (s[vs+ci] == '/' && s[vs+ci+1] == '*') { vl = ci; break; }
                if (s[vs+ci] == '/' && s[vs+ci+1] == '/') { vl = ci; break; }
            } }
            char *n = (char *)tlibc_malloc(mnl+1); get_name(s, ms, ms+mnl, n, mnl+1);
            char *v = 0; int rvl = 0;
            if (vl > 0) { v = strip_continuations(s + vs, vl, &rvl); }
            add_macro(n, v, rvl); return;
        }
    }

    if (dl == 6 && s[dw]=='i'&&s[dw+1]=='f'&&s[dw+2]=='d') {
        while (p < le && pp_ws(s[p])) p++;
        int ms = p; while (p < le && pp_id(s[p])) p++;
        if (p > ms) { char mn[256]; get_name(s, ms, p, mn, 256); if (!macro_defined(mn)) skip_cond(s, &p, le); }
        return;
    }

    if (dl == 7 && s[dw]=='i'&&s[dw+1]=='f'&&s[dw+2]=='n') {
        while (p < le && pp_ws(s[p])) p++;
        int ms = p; while (p < le && pp_id(s[p])) p++;
        if (p > ms) { char mn[256]; get_name(s, ms, p, mn, 256); if (macro_defined(mn)) skip_cond(s, &p, le); }
        return;
    }

    if ((dl == 4 && s[dw]=='e'&&s[dw+1]=='l'&&s[dw+2]=='s'&&s[dw+3]=='e') ||
        (dl == 5 && s[dw]=='e'&&s[dw+1]=='n'&&s[dw+2]=='d')) return;

    if (dl == 5 && s[dw]=='u'&&s[dw+1]=='n') {
        while (p < le && pp_ws(s[p])) p++;
        int ms = p; while (p < le && pp_id(s[p])) p++;
        if (p > ms) { char mn[256]; get_name(s, ms, p, mn, 256); undef_macro(mn); }
        return;
    }

    if (dl == 5 && s[dw]=='e'&&s[dw+1]=='r'&&s[dw+2]=='r') {
        while (p < le && pp_ws(s[p])) { p++; } __printf("tcc: #error ");
        while (p < le) { __printf("%c", s[p++]); } __printf("\n"); return;
    }
}

static char *strip_all_comments(const char *src, int len, int *out_len);

static void pp_buf_impl(const char *s, int len, OutBuf *out, int depth, int *had_nl);

static void pp_buf(const char *s, int len, OutBuf *out, int depth) {
    if (depth > 32) return;
    if (depth == 0) {
        add_macro("__x86_64__", 0, 0); add_macro("X86_64_TLIBC", "1", 1);
    }
    int nlen; char *n = strip_all_comments(s, len, &nlen);
    pp_buf_impl(n, nlen, out, depth, NULL);
    tlibc_free(n);
}

static void pp_buf_impl(const char *s, int len, OutBuf *out, int depth, int *had_nl) {
    (void)had_nl;
    int i = 0;
    while (i < len) {
        /* conditional compilation state */
        static int cond_skip = 0;
        static int cond_depth = 0;
        static int cond_emit[32];

        /* skip mode: if inside a #ifdef block that should be skipped, consume line */
        if (cond_skip > 0 && s[i] != '#') {
            while (i < len && s[i] != '\n') i++;
            if (i < len && s[i] == '\n') i++;
            continue;
        }
        if (s[i] == '#') {
            int is_directive = 0;
            if (i == 0) is_directive = 1;
            else { int bi = i;
                while (bi > 0 && (s[bi-1] == ' ' || s[bi-1] == '\t')) bi--;
                if (bi == 0 || s[bi-1] == '\n' || s[bi-1] == '\r') is_directive = 1; }
            if (is_directive) {
                int ls = i; int le = i;
                while (le < len && s[le] != '\n') {
                    if (s[le] == '\\') {
                        int nl = le + 1;
                        if (nl < len && s[nl] == '\r') nl++;
                        if (nl < len && s[nl] == '\n') le = nl + 1;
                        else le++;
                    } else { le++; }
                }
                /* handle #ifdef/#ifndef/#else/#endif inline */
                int cpos = ls;
                while (cpos < le && (s[cpos] == ' ' || s[cpos] == '\t')) cpos++;
                if (cpos < le && s[cpos] == '#') {
                    int cw = cpos + 1; while (cw < le && (s[cw] == ' ' || s[cw] == '\t')) cw++;
                    int wlen = le - cw;
                    if (wlen >= 4 && s[cw]=='e'&&s[cw+1]=='l'&&s[cw+2]=='s'&&s[cw+3]=='e') {
                        if (cond_depth > 0) {
                            if (cond_emit[cond_depth-1]) {
                                cond_skip++;
                            } else {
                                if (cond_skip > 0) cond_skip--;
                            }
                            cond_emit[cond_depth-1] = !cond_emit[cond_depth-1];
                        }
                        i = le; if (i < len && s[i] == '\n') i++;
                        continue;
                    }
                    if (wlen >= 5 && s[cw]=='e'&&s[cw+1]=='n'&&s[cw+2]=='d'&&s[cw+3]=='i'&&s[cw+4]=='f') {
                        if (cond_depth > 0) {
                            if (!cond_emit[cond_depth-1] && cond_skip > 0) cond_skip--;
                            cond_depth--;
                        }
                        i = le; if (i < len && s[i] == '\n') i++;
                        continue;
                    }
                    if (wlen >= 5 && s[cw]=='i'&&s[cw+1]=='f') {
                        int is_ifndef = (wlen >= 6 && s[cw+2]=='n');
                        int is_ifdef = !is_ifndef && (wlen < 6 || s[cw+2]!='n');
                        if (is_ifdef || is_ifndef) {
                            int mp = cw + (is_ifndef ? 6 : 5);
                            while (mp < le && (s[mp] == ' ' || s[mp] == '\t')) mp++;
                            int ms = mp;
                            while (mp < le && ((s[mp] >= 'a' && s[mp] <= 'z') || (s[mp] >= 'A' && s[mp] <= 'Z') || s[mp] == '_' || (s[mp] >= '0' && s[mp] <= '9'))) mp++;
                            int is_def = 0;
                            if (mp > ms) {
                                char mn[256];
                                int ci;
                                for (ci = 0; ci < mp-ms && ci < 255; ci++) mn[ci] = s[ms+ci];
                                mn[mp-ms] = '\0';
                                is_def = macro_defined(mn);
                            }
                            int emit_block;
                            if (is_ifdef) emit_block = is_def;
                            else emit_block = !is_def;
                            if (cond_depth < 32) {
                                if (emit_block) {
                                    cond_emit[cond_depth] = 1;
                                } else {
                                    cond_emit[cond_depth] = 0;
                                    cond_skip++;
                                }
                                cond_depth++;
                            }
                            i = le; if (i < len && s[i] == '\n') i++;
                            continue;
                        }
                    }
                }

                /* if in skip mode, directives that we handle (#ifdef etc) are already consumed above */
                if (cond_skip > 0) {
                    i = le; if (i < len && s[i] == '\n') i++;
                    continue;
                }

                do_directive(s, ls, le, out, depth);
                i = le; if (i < len && s[i] == '\n') i++;
                continue;
            }
        }

        /* ─── 跳过字符串字面量（宏不展开其中的标识符） ─── */
        if (s[i] == '"') {
            out_putc(out, '"'); i++;
            while (i < len && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < len) {
                    out_putc(out, s[i]); i++;
                }
                out_putc(out, s[i]); i++;
            }
            if (i < len) { out_putc(out, '"'); i++; }
            continue;
        }

        /* ─── 对象宏展开 ─── */
        if ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || s[i] == '_') {
            int start = i;
            while (i < len && ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
                   s[i] == '_' || (s[i] >= '0' && s[i] <= '9'))) i++;
            int id_len = i - start;
            int mi;
            int expanded = 0;
            for (mi = 0; mi < macro_count; mi++) {
                const char *mn = macros[mi].name;
                int j;
                for (j = 0; j < id_len; j++) if (mn[j] != s[start + j]) goto nomatch;
                if (mn[j] != '\0') goto nomatch;
                /* 找到了！输出宏值 */
                if (macros[mi].value && macros[mi].value_len > 0) {
                    /* 递归展开宏值中的宏（防止无限循环：depth > 64 时停止） */
                    if (depth < 64) {
                        pp_buf_impl(macros[mi].value, macros[mi].value_len, out, depth + 1, NULL);
                    } else {
                        int vi; for (vi = 0; vi < macros[mi].value_len; vi++) out_putc(out, macros[mi].value[vi]);
                    }
                }
                expanded = 1;
                break;
                nomatch:;
            }
            if (expanded) continue;
            /* 检查函数式宏：标识符后跟 ( */
            if (i < len && s[i] == '(' && func_macro_count > 0) {
                int id_match = 0;
                int fmi;
                for (fmi = 0; fmi < func_macro_count; fmi++) {
                    const char *fn = func_macros[fmi].name;
                    int j;
                    for (j = 0; j < id_len; j++) if (fn[j] != s[start + j]) goto fnm;
                    if (fn[j] != '\0') goto fnm;
                    id_match = 1;
                    /* 防无限递归：正在展开的宏不再展开，原样输出标识符 */
                    if (is_expanding(fn)) {
                        int idx; for (idx = start; idx < i; idx++) out_putc(out, s[idx]);
                        id_match = 1;
                        break;
                    }
                    /* 解析参数：从 i+1 开始，匹配 ) */
                    int ap = i + 1;
                    int adepth = 1;
                    int aprev = -1;
                    const char *arg_starts[MAX_MACRO_PARAMS];
                    int arg_lens[MAX_MACRO_PARAMS];
                    int arg_count = 0;
                    arg_starts[0] = s + ap;
                    while (adepth > 0 && ap < len) {
                        if (ap == aprev) break;
                        aprev = ap;
                        if (s[ap] == '"') {
                            ap++; while (ap < len && s[ap] != '"') {
                                if (s[ap] == '\\' && ap+1 < len) ap++;
                                ap++;
                            }
                            if (ap < len) ap++;
                            continue;
                        }
                        if (s[ap] == '\'') {
                            ap++; if (ap < len && s[ap] == '\\') ap++;
                            if (ap < len) ap++;
                            if (ap < len && s[ap] == '\'') ap++;
                            continue;
                        }
                        if (s[ap] == '(') adepth++;
                        if (s[ap] == ')') { adepth--; if (adepth == 0) break; }
                        if (adepth == 1 && s[ap] == ',' && arg_count < MAX_MACRO_PARAMS-1) {
                            arg_lens[arg_count] = (s + ap) - arg_starts[arg_count];
                            arg_count++;
                            arg_starts[arg_count] = s + ap + 1;
                        }
                        ap++;
                        if (ap >= len) break;
                    }
                    if (adepth == 0) {
                        arg_lens[arg_count] = (s + ap) - arg_starts[arg_count];
                        arg_count++;
                        /* 去除每个参数的收尾空白（标准 C 语义） */
                        {
                            int ai;
                            for (ai = 0; ai < arg_count; ai++) {
                                while (arg_lens[ai] > 0 && pp_ws(arg_starts[ai][0]))
                                    { arg_starts[ai]++; arg_lens[ai]--; }
                                while (arg_lens[ai] > 0 && pp_ws(arg_starts[ai][arg_lens[ai]-1]))
                                    arg_lens[ai]--;
                            }
                        }
                        /* 预展开所有参数（标准 C 语义：参数先展开再替换） */
                        char *expanded_args[MAX_MACRO_PARAMS];
                        int expanded_lens[MAX_MACRO_PARAMS];
                        {
                            int ai;
                            for (ai = 0; ai < arg_count; ai++) {
                                OutBuf ab = { 0, 0, 0 };
                                /* 递归展开参数中的宏（depth+1，且当前宏已入栈保护） */
                                expand_stack[expand_stack_depth++] = fn;
                                pp_buf_impl(arg_starts[ai], arg_lens[ai], &ab, depth + 1, NULL);
                                expand_stack_depth--;
                                if (ab.data && ab.len > 0) {
                                    expanded_args[ai] = ab.data;
                                    expanded_lens[ai] = ab.len;
                                } else {
                                    expanded_args[ai] = 0;
                                    expanded_lens[ai] = 0;
                                }
                            }
                        }
                        /* 构建临时展开缓冲区 */
                        OutBuf temp = { 0, 0, 0 };
                        if (func_macros[fmi].replacement) {
                            const char *rp = func_macros[fmi].replacement;
                            int rl = func_macros[fmi].repl_len;
                            int ri = 0;
                            while (ri < rl) {
                                /* 跳过字符串字面量（参数名不在字符串内替换） */
                                if (rp[ri] == '"') {
                                    out_putc(&temp, rp[ri]); ri++;
                                    while (ri < rl && rp[ri] != '"') {
                                        if (rp[ri] == '\\' && ri + 1 < rl) {
                                            out_putc(&temp, rp[ri]); ri++;
                                        }
                                        out_putc(&temp, rp[ri]); ri++;
                                    }
                                    if (ri < rl) { out_putc(&temp, rp[ri]); ri++; }
                                    continue;
                                }
                                /* 跳过字符字面量 */
                                if (rp[ri] == '\'') {
                                    out_putc(&temp, rp[ri]); ri++;
                                    if (ri < rl && rp[ri] == '\\') { out_putc(&temp, rp[ri]); ri++; }
                                    if (ri < rl) { out_putc(&temp, rp[ri]); ri++; }
                                    if (ri < rl && rp[ri] == '\'') { out_putc(&temp, rp[ri]); ri++; }
                                    continue;
                                }
                                /* , ## __VA_ARGS__：GCC 扩展，VA_ARGS 为空时删除逗号 */
                                if (func_macros[fmi].is_variadic && rp[ri] == ',') {
                                    int nri = ri + 1;
                                    while (nri < rl && (rp[nri] == ' ' || rp[nri] == '\t')) nri++;
                                    if (nri + 1 < rl && rp[nri] == '#' && rp[nri+1] == '#') {
                                        nri += 2;
                                        while (nri < rl && (rp[nri] == ' ' || rp[nri] == '\t')) nri++;
                                        if (nri + 10 < rl &&
                                            rp[nri]=='_' && rp[nri+1]=='_' && rp[nri+2]=='V' &&
                                            rp[nri+3]=='A' && rp[nri+4]=='_' && rp[nri+5]=='A' &&
                                            rp[nri+6]=='R' && rp[nri+7]=='G' && rp[nri+8]=='S' &&
                                            rp[nri+9]=='_' && rp[nri+10]=='_') {
                                            int va_empty = (arg_count == func_macros[fmi].param_count);
                                            if (va_empty) {
                                                ri = nri + 11; continue;
                                            } else {
                                                out_putc(&temp, ',');
                                                int vi;
                                                for (vi = func_macros[fmi].param_count; vi < arg_count; vi++) {
                                                    if (vi > func_macros[fmi].param_count) out_putc(&temp, ',');
                                                    if (expanded_args[vi]) {
                                                        int vj; for (vj = 0; vj < expanded_lens[vi]; vj++)
                                                            out_putc(&temp, expanded_args[vi][vj]);
                                                    } else {
                                                        int vj; for (vj = 0; vj < arg_lens[vi]; vj++)
                                                            out_putc(&temp, (arg_starts[vi])[vj]);
                                                    }
                                                }
                                                ri = nri + 11; continue;
                                            }
                                        }
                                    }
                                    out_putc(&temp, rp[ri]); ri++; continue;
                                }
                                /* __VA_ARGS__（独立出现，非 ,## 后） */
                                if (func_macros[fmi].is_variadic && ri + 10 < rl &&
                                    rp[ri]=='_' && rp[ri+1]=='_' && rp[ri+2]=='V' &&
                                    rp[ri+3]=='A' && rp[ri+4]=='_' && rp[ri+5]=='A' &&
                                    rp[ri+6]=='R' && rp[ri+7]=='G' && rp[ri+8]=='S' &&
                                    rp[ri+9]=='_' && rp[ri+10]=='_') {
                                    ri += 11;
                                    int vi;
                                    for (vi = func_macros[fmi].param_count; vi < arg_count; vi++) {
                                        if (vi > func_macros[fmi].param_count) out_putc(&temp, ',');
                                        if (expanded_args[vi]) {
                                            int vj; for (vj = 0; vj < expanded_lens[vi]; vj++)
                                                out_putc(&temp, expanded_args[vi][vj]);
                                        } else {
                                            int vj; for (vj = 0; vj < arg_lens[vi]; vj++)
                                                out_putc(&temp, (arg_starts[vi])[vj]);
                                        }
                                    }
                                    continue;
                                }
                                /* 跳过 #（stringify 运算符—TODO：支持真正字符串化） */
                                if (rp[ri]=='#' && ri+1 < rl && rp[ri+1] != '#') { ri++; continue; }
                                /* ## 通用粘贴：移除两侧空白，连接 token */
                                if (ri+1 < rl && rp[ri]=='#' && rp[ri+1]=='#') {
                                    ri += 2;  /* 跳过 ## */
                                    /* 跳过 ## 后的空白 */
                                    while (ri < rl && (rp[ri] == ' ' || rp[ri] == '\t')) ri++;
                                    /* 去掉 ## 前已写入 temp 的尾部空白 */
                                    while (temp.len > 0 &&
                                           (temp.data[temp.len-1] == ' ' || temp.data[temp.len-1] == '\t'))
                                        temp.len--;
                                    continue;
                                }
                                /* 检查参数名 */
                                if (pp_id(rp[ri])) {
                                    int rs = ri;
                                    while (ri < rl && pp_id(rp[ri])) ri++;
                                    int matched = 0;
                                    int pi;
                                    for (pi = 0; pi < func_macros[fmi].param_count && pi < arg_count; pi++) {
                                        const char *pn = func_macros[fmi].params[pi];
                                        int jj;
                                        for (jj = 0; jj < ri - rs; jj++) if (pn[jj] != rp[rs+jj]) goto pnm;
                                        if (pn[jj] != '\0') goto pnm;
                                        if (expanded_args[pi]) {
                                            int vj; for (vj = 0; vj < expanded_lens[pi]; vj++)
                                                out_putc(&temp, expanded_args[pi][vj]);
                                        } else {
                                            int vj; for (vj = 0; vj < arg_lens[pi]; vj++)
                                                out_putc(&temp, (arg_starts[pi])[vj]);
                                        }
                                        matched = 1;
                                        break;
                                        pnm:;
                                    }
                                    if (!matched) {
                                        int idx; for (idx = rs; idx < ri; idx++) out_putc(&temp, rp[idx]);
                                    }
                                    continue;
                                }
                                out_putc(&temp, rp[ri]); ri++;
                            }
                        }
                        /* 递归展开临时缓冲区中的宏 */
                        int used_follow_on = 0;
                        if (temp.data && temp.len > 0) {
                            if (depth < 64) {
                                OutBuf pp_result = { 0, 0, 0 };
                                expand_stack[expand_stack_depth++] = fn;
                                pp_buf_impl(temp.data, temp.len, &pp_result, depth + 1, NULL);
                                expand_stack_depth--;
                                {
                                    int ri = 0;
                                    while (ri < pp_result.len && pp_ws(pp_result.data[ri])) ri++;
                                    if (ri < pp_result.len && pp_id(pp_result.data[ri])) {
                                        int rs = ri;
                                        while (ri < pp_result.len && pp_id(pp_result.data[ri])) ri++;
                                        int fmi2;
                                        for (fmi2 = 0; fmi2 < func_macro_count; fmi2++) {
                                            const char *fn2 = func_macros[fmi2].name;
                                            int jn;
                                            for (jn = 0; jn < ri - rs; jn++)
                                                if (fn2[jn] != pp_result.data[rs + jn]) goto fnm2;
                                            if (fn2[jn] != '\0') goto fnm2;
                                            if (ap + 1 < len && s[ap + 1] == '(' && !is_expanding(fn2)) {
                                                int paren_start = ap + 1;
                                                int paren_depth = 1;
                                                int scan_pos = paren_start + 1;
                                                while (paren_depth > 0 && scan_pos < len) {
                                                    if (s[scan_pos] == '"') {
                                                        scan_pos++;
                                                        while (scan_pos < len && s[scan_pos] != '"') {
                                                            if (s[scan_pos] == '\\' && scan_pos + 1 < len)
                                                                scan_pos++;
                                                            scan_pos++;
                                                        }
                                                        if (scan_pos < len) scan_pos++;
                                                        continue;
                                                    }
                                                    if (s[scan_pos] == '(') paren_depth++;
                                                    if (s[scan_pos] == ')') {
                                                        paren_depth--;
                                                        if (paren_depth == 0) break;
                                                    }
                                                    scan_pos++;
                                                }
                                                if (paren_depth == 0) {
                                                    OutBuf combined = { 0, 0, 0 };
                                                    int ci;
                                                    for (ci = 0; ci < pp_result.len; ci++)
                                                        out_putc(&combined, pp_result.data[ci]);
                                                    for (ci = paren_start; ci <= scan_pos; ci++)
                                                        out_putc(&combined, s[ci]);
                                                    pp_buf_impl(combined.data, combined.len,
                                                                out, depth + 1, NULL);
                                                    tlibc_free(combined.data);
                                                    i = scan_pos + 1;
                                                    used_follow_on = 1;
                                                    break;
                                                }
                                            }
                                            fnm2:;
                                        }
                                    }
                                }
                                if (!used_follow_on) {
                                    int wi;
                                    for (wi = 0; wi < pp_result.len; wi++)
                                        out_putc(out, pp_result.data[wi]);
                                }
                                tlibc_free(pp_result.data);
                            } else {
                                int ti; for (ti = 0; ti < temp.len; ti++) out_putc(out, temp.data[ti]);
                            }
                            tlibc_free(temp.data);
                        }
                        /* 清理预展开的参数缓冲区 */
                        {
                            int ai;
                            for (ai = 0; ai < arg_count; ai++) {
                                if (expanded_args[ai]) tlibc_free(expanded_args[ai]);
                            }
                        }
                        if (!used_follow_on) {
                            i = ap + 1; /* 跳过 ) */
                        }
                    }
                    break;
                    fnm:;
                }
                if (id_match) continue;
            }
            /* 不是宏，原样输出 */
            { int idx; for (idx = start; idx < i; idx++) out_putc(out, s[idx]); }
            continue;
        }
        if (s[i] != 13) { out_putc(out, s[i]); } i++;
    }
}

/* 预处理前清除所有注释（替换为空格） */
static char *strip_all_comments(const char *src, int len, int *out_len) {
    OutBuf out = { 0, 0, 0 };
    int i = 0;
    while (i < len) {
        if (src[i] == '"') {
            out_putc(&out, src[i]); i++;
            while (i < len && src[i] != '"') {
                if (src[i] == '\\' && i+1 < len) { out_putc(&out, src[i]); i++; }
                out_putc(&out, src[i]); i++;
            }
            if (i < len) { out_putc(&out, src[i]); i++; }
            continue;
        }
        if (src[i] == '\'') {
            out_putc(&out, src[i]); i++;
            if (i < len && src[i] == '\\') { out_putc(&out, src[i]); i++; }
            if (i < len) { out_putc(&out, src[i]); i++; }
            if (i < len && src[i] == '\'') { out_putc(&out, src[i]); i++; }
            continue;
        }
        if (src[i] == '/' && i+1 < len) {
            if (src[i+1] == '/') {
                i += 2; while (i < len && src[i] != '\n') { out_putc(&out, ' '); i++; }
                continue;
            }
            if (src[i+1] == '*') {
                i += 2;
                while (i < len) {
                    if (src[i]=='*' && i+1<len && src[i+1]=='/') { i+=2; break; }
                    if (src[i] == '\n') out_putc(&out, '\n');
                    else out_putc(&out, ' ');
                    i++;
                }
                continue;
            }
        }
        out_putc(&out, src[i]);
        i++;
    }
    out_putc(&out, '\0');
    *out_len = out.len - 1;
    return out.data;
}

char *preprocess(const char *src, int len, const char *fname, int *out_len) {
    current_source_dir[0] = '\0';
    inc_path_added_source_dir = 0;
    if (fname) {
        { int fnl = 0; while (fname[fnl]) fnl++; dirname_of(fname, fnl, current_source_dir, 1024); }
        int fnl = 0; while (fname[fnl]) fnl++;
        char *fv = (char *)tlibc_malloc(fnl + 3);
        fv[0] = '"'; { int fi; for (fi = 0; fi < fnl; fi++) fv[fi+1] = fname[fi]; }
        fv[fnl+1] = '"'; fv[fnl+2] = '\0';
        add_macro("__FILE__", fv, fnl + 2);
    } else {
        add_macro("__FILE__", "\"<unknown>\"", 11);
    }
    add_macro("__LINE__", "0", 1);
    int clean_len;
    char *clean = strip_all_comments(src, len, &clean_len);
    OutBuf out = { 0, 0, 0 };
    pp_buf(clean, clean_len, &out, 0);
    tlibc_free(clean);
    out_putc(&out, '\0');
    *out_len = out.len > 0 ? out.len - 1 : 0;
    return out.data;
}

// ============================================================
// 测试框架
// ============================================================

static int test_passed = 0;
static int test_failed = 0;

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

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        print_str("  FAIL "); print_str(msg); print_str("\n"); \
        test_failed++; \
    } else { \
        test_passed++; \
    } \
} while (0)

/* 预处理辅助：直接调用 preprocess，返回输出 buffer */
typedef struct {
    char *data;
    int len;
} PPResult;

static PPResult pp(const char *input) {
    int len = 0;
    while (input[len]) len++;
    int out_len;
    char *out = preprocess(input, len, "test.c", &out_len);
    PPResult r = { out, out_len };
    return r;
}


/* 字符串比较（忽略尾部空白） */
static int eq_trim(const char *a, int alen, const char *b) {
    int i = alen;
    while (i > 0 && (a[i-1] == ' ' || a[i-1] == '\t' || a[i-1] == '\n' || a[i-1] == '\r'))
        i--;
    int blen = 0; while (b[blen]) blen++;
    while (blen > 0 && (b[blen-1] == ' ' || b[blen-1] == '\t' || b[blen-1] == '\n' || b[blen-1] == '\r'))
        blen--;
    if (i != blen) return 0;
    int j;
    for (j = 0; j < i; j++) if (a[j] != b[j]) return 0;
    return 1;
}

/* 检查预处理输出是否等于预期字符串（忽略尾部空白） */
#define CHECK_PP(input, expected, msg) do { \
    PPResult r = pp(input); \
    if (!eq_trim(r.data, r.len, expected)) { \
        print_str("  FAIL "); print_str(msg); print_str(":\n"); \
        print_str("    got:      \""); \
        { int _i; for (_i = 0; _i < r.len; _i++) __sys_write(1, &r.data[_i], 1); } \
        print_str("\"\n"); \
        print_str("    expected: \""); print_str(expected); print_str("\"\n"); \
        test_failed++; \
    } else { \
        test_passed++; \
    } \
    heap_init(); \
} while (0)

/* 精确比较（含尾部空白） */
#define CHECK_PP_EXACT(input, expected, msg) do { \
    PPResult r = pp(input); \
    if (!eq(r.data, r.len, expected)) { \
        print_str("  FAIL "); print_str(msg); print_str(":\n"); \
        print_str("    got:      \""); \
        { int _i; for (_i = 0; _i < r.len; _i++) __sys_write(1, &r.data[_i], 1); } \
        print_str("\"\n"); \
        print_str("    expected: \""); print_str(expected); print_str("\"\n"); \
        test_failed++; \
    } else { \
        test_passed++; \
    } \
    heap_init(); \
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
// 预处理器测试用例
// ============================================================

/* 每次调用前重置堆 */
static void reset(void) {
    heap_init();
    macro_count = 0;
    func_macro_count = 0;
    expand_stack_depth = 0;
    inc_path_count = 0;
    /* pp_buf_impl 的 static 状态只能在 pp_buf 内部重置 */
    /* 我们用 heap_init() 清掉所有已分配内存 */
}

/* 1. 基础对象宏 */
static void test_object_macros(void) {
    run_section("Object-like Macros");

    reset();
    /* 简单常量替换 */
    CHECK_PP("#define FOO 42\nFOO\n", "42", "basic #define FOO 42");

    reset();
    /* 多行文本 */
    CHECK_PP("#define MSG hello world\nMSG\n", "hello world", "#define MSG multi-word");

    reset();
    /* 空定义（仅定义标记） */
    CHECK_PP("#define EMPTY\nEMPTY\n", "", "#define EMPTY expands to nothing");

    reset();
    /* 宏值中包含空格 */
    CHECK_PP("#define VAL a + b\nVAL\n", "a + b", "#define VAL with spaces");

    reset();
    /* 宏中出现其他宏名（预展开） */
    CHECK_PP("#define X 1\n#define Y X\nY\n", "1", "nested macro X in Y");
}

/* 2. 函数式宏 */
static void test_func_macros(void) {
    run_section("Function-like Macros");

    reset();
    CHECK_PP("#define ADD(a,b) ((a)+(b))\nADD(1,2)\n", "((1)+(2))", "basic function macro");

    reset();
    CHECK_PP("#define MUL(a,b) a*b\nMUL(3,4)\n", "3*4", "func macro no parens");

    reset();
    CHECK_PP("#define ID(x) x\nID(42)\n", "42", "identity macro");

    reset();
    /* 多层嵌套 — 参数先展开再替换：ADD(1,2) → ((1)+(2)) 代入 MUL */
    CHECK_PP("#define ADD(a,b) ((a)+(b))\n#define MUL(a,b) ((a)*(b))\nMUL(ADD(1,2),ADD(3,4))\n",
             "((((1)+(2)))*(((3)+(4))))", "nested func macros");

    reset();
    /* 空参数 */
    CHECK_PP("#define EMPTY() \nEMPTY()\n", "", "empty func macro");

    reset();
    /* 逗号分隔参数 */
    CHECK_PP("#define F(a,b) a b\nF(hello,world)\n", "hello world", "two params");
}

/* 3. #undef */
static void test_undef(void) {
    run_section("#undef");

    reset();
    CHECK_PP("#define FOO 42\nFOO\n#undef FOO\nFOO\n", "42\nFOO", "#define then #undef then raw ident");

    reset();
    /* undef 不存在的宏不应报错 */
    CHECK_PP("#undef NOTDEFINED\nint x;\n", "int x;", "#undef non-existent macro");
}

/* 4. #ifdef / #ifndef / #else / #endif */
static void test_cond_compile(void) {
    run_section("Conditional Compilation");

    reset();
    CHECK_PP("#define FOO\n#ifdef FOO\nkeep\n#endif\n", "keep", "#ifdef defined");

    reset();
    CHECK_PP("#define FOO\n#ifndef FOO\nskip\n#endif\n", "", "#ifndef defined → skip");

    reset();
    CHECK_PP("#ifdef UNDEF\nskip\n#endif\nkeep\n", "keep", "#ifdef undefined → skip");

    reset();
    CHECK_PP("#ifndef UNDEF\nkeep\n#endif\n", "keep", "#ifndef undefined → keep");

    reset();
    CHECK_PP("#define FOO\n#ifdef FOO\nkeep\n#else\nskip\n#endif\n", "keep", "#ifdef + #else keep");

    reset();
    CHECK_PP("#ifdef UNDEF\nskip\n#else\nkeep\n#endif\n", "keep", "#ifdef undef + #else keep");

    reset();
    /* 嵌套条件 */
    CHECK_PP("#define A\n#define B\n#ifdef A\n#ifdef B\nboth\n#endif\n#endif\n",
             "both", "nested #ifdef A + #ifdef B");
}

/* 5. 字符串字面量中的宏名不展开 */
static void test_string_preserve(void) {
    run_section("String Literal Preservation");

    reset();
    CHECK_PP("#define FOO 42\n\"FOO\"\n", "\"FOO\"", "macro not expanded in string");

    reset();
    CHECK_PP("#define BAR hello\n\"BAR world\"\n", "\"BAR world\"", "macro not expanded in string 2");
}

/* 6. 注释剥离 */
static void test_comment_strip(void) {
    run_section("Comment Stripping");

    reset();
    /* 块注释中的每个字符变成空格（保留行号对应）——9 inside chars = 9 spaces */
    CHECK_PP("int /* comment */ x;\n", "int           x;", "block comment stripped");

    reset();
    reset();
    /* 行注释：int(空格) + "// line comment"(13 chars→13spaces) + newline(保留) + x; */
    /* 精确验证：int(3) + 1space + 13spaces + \n + x; + \n = 21 字节 */
    { PPResult r = pp("int // line comment\nx;\n");
    int ok = (r.len == 21) && (r.data[0]=='i') && (r.data[1]=='n') && (r.data[2]=='t');
    int si;
    for (si = 4; si < 17; si++) { if (r.data[si] != ' ') ok = 0; }
    ok = ok && (r.data[17] == '\n') && (r.data[18]=='x') && (r.data[19]==';') && (r.data[20]=='\n');
    CHECK(ok, "line comment stripped (exact byte check)");
    heap_init(); }

    reset();
    /* 宏定义中的注释被剥离 */
    CHECK_PP("#define FOO 42 /* comment */\nFOO\n", "42", "comment in #define stripped");
}

/* 7. ## token 粘贴 */
static void test_token_paste(void) {
    run_section("## Token Pasting");

    reset();
    CHECK_PP("#define CAT(a,b) a##b\nCAT(foo,bar)\n", "foobar", "basic ## paste");

    reset();
    CHECK_PP("#define CONCAT3(a,b,c) a##b##c\nCONCAT3(x,y,z)\n", "xyz", "triple ## paste");

    /* 注意：空参数 + ## 不会出错（GCC 扩展） */
    reset();
    CHECK_PP("#define CAT2(a,b) a##b\nCAT2(x,)\n", "x", "## with empty second param");
}

/* 8. 变参宏 */
static void test_variadic(void) {
    run_section("Variadic Macros");

    reset();
    /* 基本变参：__VA_ARGS__ 替换为额外参数 */
    CHECK_PP("#define PRINT(fmt,...) fmt __VA_ARGS__\nPRINT(\"hi\",42)\n",
             "\"hi\" 42", "basic __VA_ARGS__");

    reset();
    /* 只有一个 fmt 参数时，__VA_ARGS__ 为空 */
    CHECK_PP("#define PRINT(fmt,...) fmt __VA_ARGS__\nPRINT(\"hi\")\n",
             "\"hi\" ", "empty __VA_ARGS__");
}

/* 9. #error 指令 */
static void test_error_directive(void) {
    run_section("#error Directive");

    reset();
    /* #error 行在输出中消失，错误消息打印到 stderr */
    PPResult r = pp("#error this is wrong\nstill here\n");
    CHECK(eq_trim(r.data, r.len, "still here"), "#error removed from output, rest passes through");
    heap_init();
}

/* 10. 预定义宏 */
static void test_predefined(void) {
    run_section("Predefined Macros");

    reset();
    { PPResult r = pp("__FILE__\n");
    CHECK(eq_trim(r.data, r.len, "\"test.c\""), "__FILE__ expands to \"test.c\"");
    heap_init(); }

    reset();
    { PPResult r2 = pp("__LINE__\n");
    CHECK(eq_trim(r2.data, r2.len, "0"), "__LINE__ expands to 0");
    heap_init(); }

    reset();
    /* 不传 fname 时 __FILE__ 为 "<unknown>" */
    { int len; char *out = preprocess("__FILE__\n", 8, 0, &len);
    CHECK(eq_trim(out, len, "\"<unknown>\""), "__FILE__ without fname");
    heap_init(); }

    reset();
    CHECK_PP("__x86_64__\n", "", "__x86_64__ expands to nothing");

    reset();
    CHECK_PP("X86_64_TLIBC\n", "1", "X86_64_TLIBC expands to 1");
}

/* 11. 普通文本保留 */
static void test_plain_text(void) {
    run_section("Plain Text Passthrough");

    reset();
    CHECK_PP("int main(void) { return 0; }\n",
             "int main(void) { return 0; }", "plain C code preserved");

    reset();
    CHECK_PP("", "", "empty input");

    reset();
    CHECK_PP("\n\n\n", "", "whitespace-only input");
}

/* 12. 续行符处理 */
static void test_line_continuation(void) {
    run_section("Line Continuation");

    reset();
    /* \ 续行拼接宏定义 */
    CHECK_PP("#define FOO \\\n42\nFOO\n", "42", "line continuation in #define");

    reset();
    /* 连续多行续行 */
    CHECK_PP("#define SUM a+\\\nb+\\\nc\nSUM\n", "a+b+c", "multiple line continuations");
}

/* 13. 空参数预处理 */
static void test_token_paste_followon(void) {
    run_section("Token Paste Follow-on");

    reset();
    /* ## 后的宏名与外部 (args) 结合 */
    CHECK_PP("#define ID(x) x\n#define CAT(a,b) a##b\nCAT(ID,(42))\n",
             "42", "## paste + follow-on function call");
}

// ============================================================
// 入口
// ============================================================

void __tlibc_start(void) {
    print_str("=== preproc.c standalone tests ===\n");

    test_plain_text();
    print_section_result();

    test_object_macros();
    print_section_result();

    test_func_macros();
    print_section_result();

    test_undef();
    print_section_result();

    test_cond_compile();
    print_section_result();

    test_string_preserve();
    print_section_result();

    test_comment_strip();
    print_section_result();

    test_token_paste();
    print_section_result();

    test_variadic();
    print_section_result();

    test_error_directive();
    print_section_result();

    test_predefined();
    print_section_result();

    test_line_continuation();
    print_section_result();

    test_token_paste_followon();
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
