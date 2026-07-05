// EXPECT: 0
// cgen_asm.c standalone test (simulated preprocessed state)
//
// Compile (gcc):
//   gcc -nostdlib -ffreestanding -Wall -Wextra -Wl,-e,__tlibc_start
//       compiler-tests/source/cgen_asm.c -o /tmp/test_cgen_asm
// Run:  /tmp/test_cgen_asm

// ============================================================
// Inlined from tcc_need.h — 最小类型
// ============================================================

typedef unsigned long size_t;
typedef long ptrdiff_t;
typedef long off_t;
typedef unsigned int mode_t;
#ifndef NULL
#define NULL ((void *)0)
#endif

static void sys_exit(int code) {
    __asm__ __volatile__ ("syscall"
        : : "a"((long)60), "D"((long)code)
        : "rcx", "r11", "memory");
    for (;;) ;
}

// ============================================================
// Inlined from tcc.h — AstKind / AstNode / AsmOperand / LocalVar
// ============================================================

#define CODE_BUF_SIZE  262144

typedef enum {
    AST_PROGRAM = 0, AST_FUNC_DEF, AST_RETURN, AST_CONSTANT,
    AST_BINOP, AST_UNARY, AST_ASSIGN, AST_VAR, AST_VAR_DECL,
    AST_IF, AST_WHILE, AST_FOR, AST_DO_WHILE, AST_SWITCH,
    AST_CASE, AST_DEFAULT, AST_BREAK, AST_CONTINUE, AST_BLOCK,
    AST_CALL, AST_EXPR_STMT, AST_NULL_STMT, AST_MEMBER, AST_STRING,
    AST_STRUCT_DEF, AST_ASM, AST_GOTO, AST_LABEL,
} AstKind;

typedef struct {
    const char *constraint;
    struct AstNode *expr;
} AsmOperand;

typedef struct AstNode {
    AstKind kind;
    struct AstNode *next;
    const char *name;
    struct AstNode *body, *expr;
    long ival; double dval; int is_float;
    struct AstNode *left, *right;
    struct AstNode *cond, *then_stmt, *else_stmt;
    struct AstNode *loop_cond, *loop_body, *loop_init, *loop_step;
    struct AstNode *stmts, *args, *call_target;
    const char *member_name, *str_val;
    struct {
        const char *asm_template;
        int is_volatile;
        AsmOperand *outputs;
        int output_count;
        AsmOperand *inputs;
        int input_count;
        const char **clobbers;
        int clobber_count;
    } asm_;
    struct AstNode *params;
    int op, is_static, is_variadic, is_postfix;
    int type_size, elem_size, base_elem_size;
    int is_unsigned, elem_is_unsigned;
} AstNode;

#define MAX_LOCALS 256

typedef struct {
    const char *name;
    int offset, size;
    const char *struct_tag;
    int is_float, element_size, base_elem_size;
    int scope_depth, is_array, is_unsigned, elem_is_unsigned;
} LocalVar;

LocalVar locals[MAX_LOCALS];
int local_count, frame_size, reg_save_offset, func_nparams, scope_depth;

// ============================================================
// 代码生成全局缓冲区
// ============================================================

unsigned char code_buf[CODE_BUF_SIZE];
int code_size;

// ============================================================
// 内联辅助函数（来自 tcc.h）
// ============================================================

static void e1(int b) {
    if (code_size >= CODE_BUF_SIZE) return;
    code_buf[code_size++] = b & 0xFF;
}
static void e4(int v) { e1(v); e1(v>>8); e1(v>>16); e1(v>>24); }

// ============================================================
// 运行时 stub
// ============================================================

void cgen_expr(AstNode *node) { (void)node; }

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

void __printf(const char *fmt, ...) { (void)fmt; }

// ============================================================
// cgen_asm.c 源文件 — 完全来自 app/cgen_asm.c
// ============================================================

static int str_contains(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    while (*haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return 1;
        haystack++;
    }
    return 0;
}

static void emit_mov_from_rax(char constraint_letter) {
    switch (constraint_letter) {
    case 'a': break;
    case 'D': e1(0x48); e1(0x89); e1(0xC7); break;
    case 'S': e1(0x48); e1(0x89); e1(0xC6); break;
    case 'd': e1(0x48); e1(0x89); e1(0xC2); break;
    case 'b': e1(0x48); e1(0x89); e1(0xC3); break;
    case 'c': e1(0x48); e1(0x89); e1(0xC1); break;
    default: break;
    }
}

static void emit_mov_ext_from_rax(int reg_num) {
    int modrm = 0xC0 | (reg_num & 7);
    e1(0x49); e1(0x89); e1(modrm);
}

void cgen_asm(AstNode *node) {
    if (!node || !node->asm_.asm_template) return;
    const char *t = node->asm_.asm_template;
    int r_counter = 0;
    int a_idx = -1;
    int i;
    for (i = 0; i < node->asm_.input_count; i++) {
        const char *c = node->asm_.inputs[i].constraint;
        if (c && c[0] == 'a' && c[0] != '=') { a_idx = i; break; }
    }
    for (i = 0; i < node->asm_.input_count; i++) {
        if (i == a_idx) continue;
        const char *c = node->asm_.inputs[i].constraint;
        AstNode *expr = node->asm_.inputs[i].expr;
        if (!c || !expr) continue;
        if (c[0] == '=') continue;
        cgen_expr(expr);
        if (c[0] == 'r') {
            int ext_regs[] = {10, 8, 9};
            if (r_counter < 3) emit_mov_ext_from_rax(ext_regs[r_counter]);
            r_counter++;
        } else {
            emit_mov_from_rax(c[0]);
        }
    }
    if (a_idx >= 0) {
        AstNode *expr = node->asm_.inputs[a_idx].expr;
        if (expr) cgen_expr(expr);
    }
    if (str_contains(t, "mov $") && str_contains(t, "%%rax") && str_contains(t, "syscall")) {
        const char *p = t;
        while (*p && *p != '$') p++;
        int imm = 0;
        if (*p == '$') {
            p++;
            while (*p >= '0' && *p <= '9') { imm = imm * 10 + (*p - '0'); p++; }
        }
        e1(0x48); e1(0xC7); e1(0xC0); e4(imm);
        e1(0x0F); e1(0x05);
    } else if (str_contains(t, "syscall")) {
        e1(0x0F); e1(0x05);
    } else if (str_contains(t, "lock xaddq")) {
        e1(0xF0); e1(0x48); e1(0x0F); e1(0xC1); e1(0x02);
    } else if (str_contains(t, "lock xaddl")) {
        e1(0xF0); e1(0x0F); e1(0xC1); e1(0x02);
    } else if (str_contains(t, "lock cmpxchgq")) {
        e1(0xF0); e1(0x48); e1(0x0F); e1(0xB1); e1(0x02);
    } else if (str_contains(t, "lock cmpxchgl")) {
        e1(0xF0); e1(0x0F); e1(0xB1); e1(0x02);
    } else if (str_contains(t, "%%fs:0")) {
        e1(0x64); e1(0x48); e1(0x8B); e1(0x04); e1(0x25);
        e4(0);
    } else if (str_contains(t, "ecall")) {
    } else {
        __printf("tcc: unknown inline asm template: \"%s\"\n", t);
        return;
    }
    for (i = 0; i < node->asm_.output_count; i++) {
        const char *c = node->asm_.outputs[i].constraint;
        AstNode *expr = node->asm_.outputs[i].expr;
        if (!c || !expr) continue;
        if (expr->kind != AST_VAR) continue;
        int j;
        for (j = 0; j < local_count; j++) {
            if (strcmp(locals[j].name, expr->name) != 0) continue;
            int off = locals[j].offset & 0xFF;
            if (locals[j].size == 8) {
                e1(0x48); e1(0x89); e1(0x45); e1(off);
            } else if (locals[j].size == 4) {
                e1(0x89); e1(0x45); e1(off);
            } else if (locals[j].size == 2) {
                e1(0x66); e1(0x89); e1(0x45); e1(off);
            } else if (locals[j].size == 1) {
                e1(0x88); e1(0x45); e1(off);
            }
            break;
        }
    }
}

// ============================================================
// 测试框架
// ============================================================

static int test_passed = 0;
static int test_failed = 0;

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
    while (i > 0) {
        long ch = buf[--i];
        __asm__ __volatile__ ("syscall"
            : : "a"(1), "D"(1), "S"(&ch), "d"(1L)
            : "rcx", "r11", "memory");
    }
}

#define CHECK(cond, msg) do { \
    if (!(cond)) { print_str("  FAIL "); print_str(msg); print_str("\n"); test_failed++; } \
    else { test_passed++; } \
} while (0)

static int code_match(int pos, const unsigned char *bytes, int n) {
    if (pos + n > code_size) return 0;
    int i; for (i = 0; i < n; i++) if (code_buf[pos + i] != bytes[i]) return 0;
    return 1;
}

#define CHECK_CODE(pos, ...) do { \
    unsigned char _exp[] = { __VA_ARGS__ }; \
    int _n = (int)(sizeof(_exp)/sizeof(_exp[0])); \
    if (!code_match(pos, _exp, _n)) { \
        print_str("  FAIL code mismatch at byte "); print_dec(pos); print_str("\n"); \
        test_failed++; \
    } else { test_passed++; } \
} while (0)

static void run_section(const char *name) {
    print_str("\n--- "); print_str(name); print_str(" ---\n");
    test_passed = 0; test_failed = 0;
}

static void print_section_result(void) {
    print_str("  -> "); print_dec(test_passed); print_str(" passed, ");
    print_dec(test_failed); print_str(" failed\n");
}

static void reset(void) { code_size = 0; local_count = 0; frame_size = 0; }

static AstNode mk_const(long val) {
    AstNode n; n.kind = AST_CONSTANT; n.ival = val; n.dval = 0.0; n.is_float = 0;
    n.expr = 0; n.left = 0; n.right = 0; n.name = 0; n.body = 0; return n;
}

static AsmOperand mk_in(const char *c, AstNode *e) {
    AsmOperand o; o.constraint = c; o.expr = e; return o;
}

static AsmOperand mk_out(const char *c, AstNode *e) {
    AsmOperand o; o.constraint = c; o.expr = e; return o;
}

static AstNode mk_asm(const char *tmpl, int nin, AsmOperand *in,
                      int nout, AsmOperand *out) {
    AstNode n;
    n.kind = AST_ASM;
    n.asm_.asm_template = tmpl;
    n.asm_.is_volatile = 0;
    n.asm_.inputs = in; n.asm_.input_count = nin;
    n.asm_.outputs = out; n.asm_.output_count = nout;
    n.asm_.clobbers = 0; n.asm_.clobber_count = 0;
    n.expr = 0; n.left = 0; n.right = 0; n.name = 0; n.body = 0;
    return n;
}

// ============================================================
// cgen_asm 测试用例
// ============================================================

static void test_basic_constraints(void) {
    run_section("Basic Constraint Loading");

    reset();
    { AstNode v = mk_const(42);
      AsmOperand in[] = { mk_in("a", &v) };
      AstNode n = mk_asm("syscall", 1, in, 0, 0);
      cgen_asm(&n);
      CHECK_CODE(0, 0x0F, 0x05);
      CHECK(code_size == 2, "a+syscall → 2 bytes"); }

    reset();
    { AstNode v = mk_const(99);
      AsmOperand in[] = { mk_in("D", &v), mk_in("a", &v) };
      AstNode n = mk_asm("syscall", 2, in, 0, 0);
      cgen_asm(&n);
      CHECK_CODE(0, 0x48, 0x89, 0xC7, 0x0F, 0x05);
      CHECK(code_size == 5, "D+a+syscall → 5 bytes"); }

    reset();
    { AstNode v = mk_const(0);
      AsmOperand in[] = { mk_in("D", &v), mk_in("S", &v), mk_in("d", &v),
                          mk_in("b", &v), mk_in("c", &v), mk_in("a", &v) };
      AstNode n = mk_asm("syscall", 6, in, 0, 0);
      cgen_asm(&n);
      CHECK_CODE(0, 0x48, 0x89, 0xC7, 0x48, 0x89, 0xC6, 0x48, 0x89, 0xC2,
                    0x48, 0x89, 0xC3, 0x48, 0x89, 0xC1, 0x0F, 0x05);
      CHECK(code_size == 17, "D S d b c a + syscall → 17 bytes"); }
}

static void test_r_constraint(void) {
    run_section("'r' Constraint");

    reset();
    { AstNode v = mk_const(0);
      AsmOperand in[] = { mk_in("r", &v), mk_in("r", &v), mk_in("r", &v), mk_in("a", &v) };
      AstNode n = mk_asm("syscall", 4, in, 0, 0);
      cgen_asm(&n);
      CHECK_CODE(0, 0x49, 0x89, 0xC2, 0x49, 0x89, 0xC0, 0x49, 0x89, 0xC1, 0x0F, 0x05);
      CHECK(code_size == 11, "r×3 + a + syscall → 11 bytes"); }
}

static void test_templates(void) {
    run_section("Known Templates");

    reset();
    { AstNode v = mk_const(0);
      AsmOperand in[] = { mk_in("a", &v) };
      AstNode n = mk_asm("syscall", 1, in, 0, 0);
      code_size = 0; cgen_asm(&n);
      CHECK_CODE(0, 0x0F, 0x05); }

    reset();
    { AstNode n = mk_asm("lock xaddq (%0)", 0, 0, 0, 0);
      code_size = 0; cgen_asm(&n);
      CHECK_CODE(0, 0xF0, 0x48, 0x0F, 0xC1, 0x02); }

    reset();
    { AstNode n = mk_asm("lock xaddl (%0)", 0, 0, 0, 0);
      code_size = 0; cgen_asm(&n);
      CHECK_CODE(0, 0xF0, 0x0F, 0xC1, 0x02); }

    reset();
    { AstNode n = mk_asm("lock cmpxchgq (%0)", 0, 0, 0, 0);
      code_size = 0; cgen_asm(&n);
      CHECK_CODE(0, 0xF0, 0x48, 0x0F, 0xB1, 0x02); }

    reset();
    { AstNode n = mk_asm("lock cmpxchgl (%0)", 0, 0, 0, 0);
      code_size = 0; cgen_asm(&n);
      CHECK_CODE(0, 0xF0, 0x0F, 0xB1, 0x02); }

    reset();
    { AstNode n = mk_asm("%%fs:0", 0, 0, 0, 0);
      code_size = 0; cgen_asm(&n);
      CHECK_CODE(0, 0x64, 0x48, 0x8B, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00); }

    reset();
    { AstNode n = mk_asm("mov $60, %%rax\n\tsyscall", 0, 0, 0, 0);
      code_size = 0; cgen_asm(&n);
      CHECK_CODE(0, 0x48, 0xC7, 0xC0, 0x3C, 0x00, 0x00, 0x00, 0x0F, 0x05); }
}

static void test_output_constraints(void) {
    run_section("Output Constraints");

    reset();
    { locals[0].name = "ret_val"; locals[0].offset = -8; locals[0].size = 8; local_count = 1;
      AstNode v; v.kind = AST_VAR; v.name = "ret_val";
      AstNode vi = mk_const(0);
      AsmOperand in[] = { mk_in("a", &vi) };
      AsmOperand out[] = { mk_out("=a", &v) };
      AstNode n = mk_asm("syscall", 1, in, 1, out);
      code_size = 0; cgen_asm(&n);
      CHECK_CODE(0, 0x0F, 0x05, 0x48, 0x89, 0x45, 0xF8); }

    reset();
    { locals[0].name = "v32"; locals[0].offset = -16; locals[0].size = 4; local_count = 1;
      AstNode v; v.kind = AST_VAR; v.name = "v32";
      AstNode vi = mk_const(0);
      AsmOperand in[] = { mk_in("a", &vi) };
      AsmOperand out[] = { mk_out("=a", &v) };
      AstNode n = mk_asm("syscall", 1, in, 1, out);
      code_size = 0; cgen_asm(&n);
      CHECK_CODE(0, 0x0F, 0x05, 0x89, 0x45, 0xF0); }

    reset();
    { locals[0].name = "v16"; locals[0].offset = -2; locals[0].size = 2; local_count = 1;
      AstNode v; v.kind = AST_VAR; v.name = "v16";
      AstNode vi = mk_const(0);
      AsmOperand in[] = { mk_in("a", &vi) };
      AsmOperand out[] = { mk_out("=a", &v) };
      AstNode n = mk_asm("syscall", 1, in, 1, out);
      code_size = 0; cgen_asm(&n);
      CHECK_CODE(0, 0x0F, 0x05, 0x66, 0x89, 0x45, 0xFE); }

    reset();
    { locals[0].name = "v8"; locals[0].offset = -1; locals[0].size = 1; local_count = 1;
      AstNode v; v.kind = AST_VAR; v.name = "v8";
      AstNode vi = mk_const(0);
      AsmOperand in[] = { mk_in("a", &vi) };
      AsmOperand out[] = { mk_out("=a", &v) };
      AstNode n = mk_asm("syscall", 1, in, 1, out);
      code_size = 0; cgen_asm(&n);
      CHECK_CODE(0, 0x0F, 0x05, 0x88, 0x45, 0xFF); }
}

static void test_edge_cases(void) {
    run_section("Edge Cases");

    reset(); cgen_asm(0); CHECK(code_size == 0, "NULL node");

    reset();
    { AstNode n; n.kind = AST_ASM; n.asm_.asm_template = 0;
      n.asm_.input_count = 0; n.asm_.output_count = 0;
      cgen_asm(&n); CHECK(code_size == 0, "null template"); }

    reset();
    { AstNode n = mk_asm("some weird template", 0, 0, 0, 0);
      cgen_asm(&n); CHECK(code_size == 0, "unknown template"); }

    reset();
    { AstNode n = mk_asm("ecall", 0, 0, 0, 0);
      cgen_asm(&n); CHECK(code_size == 0, "ecall placeholder"); }

    reset();
    { AsmOperand in[] = { mk_in("D", 0) };
      AstNode n = mk_asm("syscall", 1, in, 0, 0);
      cgen_asm(&n); CHECK(code_size == 2, "NULL input expr"); }

    reset();
    { AsmOperand out[] = { mk_out("=a", 0) };
      AstNode n = mk_asm("syscall", 0, 0, 1, out);
      cgen_asm(&n); CHECK(code_size == 2, "NULL output expr"); }

    reset();
    { AstNode v = mk_const(0);
      AsmOperand in[] = { mk_in("D", &v) };
      AstNode n = mk_asm("syscall", 1, in, 0, 0);
      code_size = 0; cgen_asm(&n);
      CHECK_CODE(0, 0x48, 0x89, 0xC7, 0x0F, 0x05); }
}

static void test_multi_r_syscall6(void) {
    run_section("syscall6 Pattern (a+D+S+d+r+r+r)");

    reset();
    AstNode v = mk_const(0);
    AsmOperand in[] = {
        mk_in("a", &v), mk_in("D", &v), mk_in("S", &v), mk_in("d", &v),
        mk_in("r", &v), mk_in("r", &v), mk_in("r", &v),
    };
    AstNode n = mk_asm("syscall", 7, in, 0, 0);
    cgen_asm(&n);
    CHECK_CODE(0, 0x48, 0x89, 0xC7, 0x48, 0x89, 0xC6, 0x48, 0x89, 0xC2,
                   0x49, 0x89, 0xC2, 0x49, 0x89, 0xC0, 0x49, 0x89, 0xC1,
                   0x0F, 0x05);
    CHECK(code_size == 20, "syscall6: 6 regs + syscall → 20 bytes");
}

// ============================================================
// 入口
// ============================================================

void __tlibc_start(void) {
    print_str("=== cgen_asm.c standalone tests ===\n");

    test_basic_constraints(); print_section_result();
    test_r_constraint();      print_section_result();
    test_templates();         print_section_result();
    test_output_constraints(); print_section_result();
    test_edge_cases();        print_section_result();
    test_multi_r_syscall6();  print_section_result();

    print_str("\n=== ");
    print_str(test_failed == 0 ? "ALL PASSED" : "SOME FAILED");
    print_str(" ===\n");

    sys_exit(test_failed != 0 ? 1 : 0);
}
