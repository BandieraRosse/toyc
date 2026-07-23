// EXPECT: 0
// cgen.c standalone test (simulated preprocessed state)
//
// Compile (gcc):
//   gcc -nostdlib -ffreestanding -Wall -Wextra -Wl,-e,__tlibc_start
//       compiler-tests/source/test_cgen.c -o /tmp/test_cgen
// Run:  /tmp/test_cgen

// ============================================================
// Inlined from toyc_need.h — 最小类型
// ============================================================

typedef unsigned long size_t;
typedef long ptrdiff_t; typedef long off_t; typedef unsigned int mode_t;

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long int64_t;

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
// Inlined from elf.h — 最小 ELF 类型和宏
// ============================================================

typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;

#define ELF64_R_INFO(s, t)  ((((Elf64_Xword)(s)) << 32) | ((t) & 0xffffffffUL))
#define R_X86_64_PC32 2

typedef struct {
    Elf64_Addr   r_offset;
    Elf64_Xword  r_info;
    Elf64_Sxword r_addend;
} Elf64_Rela;

// ============================================================
// Inlined from elf_write.h — CgenSym / ElfWriteSym
// ============================================================

#define ELF_MAX_SYMS 8192
#define ELF_MAX_RELS 16384
#define ELF_CODE_BUF_SIZE 262144

typedef struct {
    const char *name;
    int offset, size, is_global, is_func, shndx, sym_idx;
} CgenSym;

// ============================================================
// Inlined from toyc.h — AstKind / AstNode / LocalVar / StrInfo / 常量
// ============================================================

#define CODE_BUF_SIZE  262144
#define STRTAB_SIZE    262144
#define STRPOOL_SIZE   262144

#define MAX_SYMS  8192
#define MAX_RELS  16384
#define MAX_STRINGS 1024

typedef struct {
    int pool_offset, len, sym_index;
    char name[16];
} StrInfo;

typedef enum {
    AST_PROGRAM=0, AST_FUNC_DEF, AST_RETURN, AST_CONSTANT,
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
        AsmOperand *outputs, *inputs;
        int output_count, input_count;
        const char **clobbers; int clobber_count;
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

// ============================================================
// 全局缓冲区（cgen.c 通常定义这些）— 必须在 inline 函数之前
// ============================================================

unsigned char code_buf[CODE_BUF_SIZE];
int code_size;
CgenSym syms[MAX_SYMS];
int sym_count;
Elf64_Rela rels[MAX_RELS];
int rel_count;
char strtab[STRTAB_SIZE];
int strtab_len;
unsigned char strpool_buf[STRPOOL_SIZE];
int strpool_size;
StrInfo str_infos[MAX_STRINGS];
int str_info_count;
LocalVar locals[MAX_LOCALS];
int local_count, frame_size, reg_save_offset, func_nparams, scope_depth;
int global_elem_size[MAX_SYMS];
int global_base_elem_size[MAX_SYMS];
int elf_bss_size;

// ============================================================
// 内联辅助函数（来自 toyc.h）
// ============================================================

static int disp8_fits(int offset) { return offset >= -128 && offset <= 127; }

static void e1(int b) {
    if (code_size >= CODE_BUF_SIZE) return;
    code_buf[code_size++] = b & 0xFF;
}
static void e4(int v) { e1(v); e1(v>>8); e1(v>>16); e1(v>>24); }

static void emit_store_rbp64(int reg, int off) {
    e1(0x48); e1(0x89);
    if (disp8_fits(off)) { e1(0x45 | (reg << 3)); e1(off & 0xFF); }
    else { e1(0x85 | (reg << 3)); e4(off); }
}
static void emit_load_rbp64(int reg, int off) {
    e1(0x48); e1(0x8B);
    if (disp8_fits(off)) { e1(0x45 | (reg << 3)); e1(off & 0xFF); }
    else { e1(0x85 | (reg << 3)); e4(off); }
}
static void emit_store_rbp32(int reg, int off) {
    e1(0x89);
    if (disp8_fits(off)) { e1(0x45 | (reg << 3)); e1(off & 0xFF); }
    else { e1(0x85 | (reg << 3)); e4(off); }
}
static void load_rax_from_rbp(int off) { emit_load_rbp64(0, off); }
static void store_rax_to_rbp(int off) { emit_store_rbp64(0, off); }
static void store_eax_to_rbp(int off) { emit_store_rbp32(0, off); }
static void emit_store_rbp16(int off) { e1(0x66); emit_store_rbp32(0, off); }
static void emit_store_rbp8(int off) {
    e1(0x88);
    if (disp8_fits(off)) { e1(0x45); e1(off & 0xFF); }
    else { e1(0x85); e4(off); }
}

// ============================================================
// 运行时 stub
// ============================================================

void cgen_expr(AstNode *node) { (void)node; }
void cgen_asm(AstNode *node) { (void)node; }

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

void __write(int fd, const void *buf, size_t len) {
    __asm__ __volatile__ ("syscall"
        : : "a"(1), "D"((long)fd), "S"(buf), "d"((long)len)
        : "rcx", "r11", "memory");
}

void __exit(int code) { sys_exit(code); }

// ============================================================
// cgen.c 源文件 — 完全来自 compiler/cgen.c
// ============================================================

#define MAX_LABELS 1024
#define MAX_FIXUPS 2048

static int label_ids[MAX_LABELS];
static int label_offsets[MAX_LABELS];
static int label_count;

static int fixup_label[MAX_FIXUPS];
static int fixup_offset[MAX_FIXUPS];
static int fixup_count;

static int break_target_label;
static int continue_target_label;

static int new_label(void);

#define MAX_GOTO_LABELS 256
static const char *goto_label_names[MAX_GOTO_LABELS];
static int goto_label_ids[MAX_GOTO_LABELS];
static int goto_label_count;

static int get_or_create_goto_label(const char *name) {
    int i;
    for (i = 0; i < goto_label_count; i++)
        if (strcmp(goto_label_names[i], name) == 0)
            return goto_label_ids[i];
    if (goto_label_count < MAX_GOTO_LABELS) {
        int id = new_label();
        goto_label_names[goto_label_count] = name;
        goto_label_ids[goto_label_count] = id;
        goto_label_count++;
        return id;
    }
    return -1;
}

static void reset_labels(void) {
    label_count = 0;
    fixup_count = 0;
    goto_label_count = 0;
}

static int new_label(void) {
    static int counter = 0;
    return counter++;
}

static void set_label(int id) {
    if (label_count >= MAX_LABELS) {
        __write(2, "toyc: label overflow\n", 20);
        __exit(1);
    }
    label_ids[label_count] = id;
    label_offsets[label_count] = code_size;
    label_count++;
}

static int find_label_offset(int id) {
    int i;
    for (i = 0; i < label_count; i++)
        if (label_ids[i] == id) return label_offsets[i];
    return -1;
}

static void emit1(int b) { code_buf[code_size++] = b & 0xFF; }
static void emit4(int v) { emit1(v); emit1(v>>8); emit1(v>>16); emit1(v>>24); }

static void emit_jmp(int label_id) {
    int off = find_label_offset(label_id);
    if (off >= 0) {
        int disp = off - (code_size + 5);
        emit1(0xE9); emit4(disp);
    } else {
        if (fixup_count >= MAX_FIXUPS) {
            __write(2, "toyc: fixup overflow\n", 20);
            __exit(1);
        }
        fixup_label[fixup_count] = label_id;
        fixup_offset[fixup_count] = code_size;
        fixup_count++;
        emit1(0xE9); emit4(0);
    }
}

static void emit_jcc(int cc, int label_id) {
    int off = find_label_offset(label_id);
    if (off >= 0) {
        int disp = off - (code_size + 6);
        emit1(0x0F); emit1(cc); emit4(disp);
    } else {
        if (fixup_count >= MAX_FIXUPS) {
            __write(2, "toyc: fixup overflow\n", 20);
            __exit(1);
        }
        fixup_label[fixup_count] = label_id;
        fixup_offset[fixup_count] = code_size;
        fixup_count++;
        emit1(0x0F); emit1(cc); emit4(0);
    }
}

static void apply_fixups(void) {
    int i;
    for (i = 0; i < fixup_count; i++) {
        int label_off = find_label_offset(fixup_label[i]);
        if (label_off < 0) continue;
        int jump_off = fixup_offset[i];
        int instr_len;
        if ((code_buf[jump_off] == 0xE9)) {
            instr_len = 5;
        } else {
            instr_len = 6;
        }
        int disp = label_off - (jump_off + instr_len);
        code_buf[jump_off + instr_len - 4]     = disp & 0xFF;
        code_buf[jump_off + instr_len - 3]     = (disp >> 8) & 0xFF;
        code_buf[jump_off + instr_len - 2]     = (disp >> 16) & 0xFF;
        code_buf[jump_off + instr_len - 1]     = (disp >> 24) & 0xFF;
    }
}

static void emit_prologue(void) {
    emit1(0x55);
    emit1(0x48); emit1(0x89); emit1(0xE5);
    if (frame_size > 0) {
        int aligned = (frame_size + 15) & -16;
        if (aligned <= 127) {
            emit1(0x48); emit1(0x83); emit1(0xEC); emit1(aligned);
        } else {
            emit1(0x48); emit1(0x81); emit1(0xEC); emit4(aligned);
        }
    }
}

static void emit_epilogue(void) {
    emit1(0x48); emit1(0x89); emit1(0xEC);
    emit1(0x5D);
    emit1(0xC3);
}

static int add_sym(const char *name, int offset, int size,
                   int is_global, int is_func) {
    if (name) {
        int i;
        for (i = 0; i < sym_count; i++) {
            if (syms[i].name && syms[i].shndx == 0 &&
                strcmp(syms[i].name, name) == 0) {
                syms[i].offset = offset;
                syms[i].size   = size;
                syms[i].is_func = is_func;
                syms[i].shndx  = 1;
                return i;
            }
        }
    }
    if (sym_count >= MAX_SYMS) return -1;
    CgenSym *s = &syms[sym_count++];
    s->name = name;
    s->offset = offset;
    s->size = size;
    s->is_global = is_global;
    s->is_func = is_func;
    s->shndx = 1;
    s->sym_idx = -1;
    return sym_count - 1;
}

static void cgen_stmt(AstNode *stmt);
static AstNode *global_init_prog;

static void collect_locals(AstNode *node) {
    if (!node) return;
    switch (node->kind) {
    case AST_FUNC_DEF:
        local_count = 0; frame_size = 0; scope_depth = 0;
        collect_locals(node->body); break;
    case AST_BLOCK:
        scope_depth++;
        for (AstNode *s = node->stmts; s; s = s->next) collect_locals(s);
        scope_depth--; break;
    case AST_VAR_DECL:
        if (local_count < MAX_LOCALS && node->name) {
            int sz = node->ival > 0 ? node->ival : 4;
            frame_size += sz;
            locals[local_count].name = node->name;
            locals[local_count].offset = -frame_size;
            locals[local_count].size = sz;
            locals[local_count].struct_tag = NULL;
            locals[local_count].is_float = node->is_float;
            locals[local_count].is_unsigned = node->is_unsigned;
            locals[local_count].element_size = node->elem_size;
            locals[local_count].base_elem_size = node->base_elem_size;
            locals[local_count].scope_depth = scope_depth;
            locals[local_count].elem_is_unsigned = node->elem_is_unsigned;
            locals[local_count].is_array =
                (node->type_size != 8 || node->ival > 8) && node->ival > 0 &&
                node->elem_size > 0;
            local_count++;
        } break;
    case AST_IF:
        collect_locals(node->then_stmt); collect_locals(node->else_stmt); break;
    case AST_WHILE: case AST_DO_WHILE:
        collect_locals(node->loop_body); break;
    case AST_FOR:
        collect_locals(node->loop_init); collect_locals(node->loop_body); break;
    case AST_SWITCH:
        for (AstNode *s = node->stmts; s; s = s->next) collect_locals(s); break;
    default: break;
    }
}

static void cgen_return(AstNode *stmt) {
    if (stmt->expr) cgen_expr(stmt->expr);
    emit_epilogue();
}

static void cgen_if(AstNode *stmt) {
    cgen_expr(stmt->cond);
    emit1(0x85); emit1(0xC0);
    int else_label = new_label();
    int end_label = new_label();
    emit_jcc(0x84, else_label);
    cgen_stmt(stmt->then_stmt);
    if (stmt->else_stmt) emit_jmp(end_label);
    set_label(else_label);
    if (stmt->else_stmt) { cgen_stmt(stmt->else_stmt); set_label(end_label); }
}

static void cgen_while(AstNode *stmt) {
    int start_label = new_label();
    int end_label = new_label();
    int saved_break = break_target_label;
    int saved_continue = continue_target_label;
    break_target_label = end_label;
    continue_target_label = start_label;
    set_label(start_label);
    cgen_expr(stmt->loop_cond);
    if (stmt->loop_cond && stmt->loop_cond->type_size == 8)
        { emit1(0x48); emit1(0x85); emit1(0xC0); }
    else
        { emit1(0x85); emit1(0xC0); }
    emit_jcc(0x84, end_label);
    cgen_stmt(stmt->loop_body);
    emit_jmp(start_label);
    set_label(end_label);
    break_target_label = saved_break;
    continue_target_label = saved_continue;
}

static void cgen_for(AstNode *stmt) {
    int start_label = new_label();
    int end_label = new_label();
    int step_label = new_label();
    int saved_break = break_target_label;
    int saved_continue = continue_target_label;
    break_target_label = end_label;
    continue_target_label = step_label;
    if (stmt->loop_init) {
        if (stmt->loop_init->kind == AST_VAR_DECL) {
            if (stmt->loop_init->expr) {
                cgen_expr(stmt->loop_init->expr);
                int i;
                for (i = local_count - 1; i >= 0; i--) {
                    if (strcmp(locals[i].name, stmt->loop_init->name) == 0 &&
                        locals[i].scope_depth <= scope_depth) {
                        if (locals[i].is_float) {
                            emit1(0xF2); emit1(0x0F); emit1(0x11);
                            emit1(0x45); emit1(locals[i].offset & 0xFF);
                        } else if (locals[i].size == 8) {
                            if (stmt->loop_init->expr && !stmt->loop_init->expr->is_float && stmt->loop_init->expr->type_size < 8) {
                                int do_sext = 0;
                                if (stmt->loop_init->expr->kind == AST_VAR) do_sext = 1;
                                else if (stmt->loop_init->expr->kind == AST_CONSTANT &&
                                         stmt->loop_init->expr->ival >= -2147483648L &&
                                         stmt->loop_init->expr->ival <= 2147483647L) do_sext = 1;
                                if (do_sext) { e1(0x48); e1(0x63); e1(0xC0); }
                            }
                            store_rax_to_rbp(locals[i].offset);
                        } else if (locals[i].size == 1) {
                            if (disp8_fits(locals[i].offset)) { e1(0x88); e1(0x45); e1(locals[i].offset & 0xFF); }
                            else { e1(0x88); e1(0x85); e4(locals[i].offset); }
                        } else if (locals[i].size == 2) {
                            e1(0x66);
                            if (disp8_fits(locals[i].offset)) { e1(0x89); e1(0x45); e1(locals[i].offset & 0xFF); }
                            else { e1(0x89); e1(0x85); e4(locals[i].offset); }
                        } else {
                            store_eax_to_rbp(locals[i].offset);
                        }
                        break;
                    }
                }
            }
        } else {
            cgen_expr(stmt->loop_init->expr);
        }
    }
    set_label(start_label);
    if (stmt->loop_cond) {
        cgen_expr(stmt->loop_cond);
        if (stmt->loop_cond->type_size == 8)
            { emit1(0x48); emit1(0x85); emit1(0xC0); }
        else
            { emit1(0x85); emit1(0xC0); }
        emit_jcc(0x84, end_label);
    }
    cgen_stmt(stmt->loop_body);
    set_label(step_label);
    if (stmt->loop_step) cgen_expr(stmt->loop_step);
    emit_jmp(start_label);
    set_label(end_label);
    break_target_label = saved_break;
    continue_target_label = saved_continue;
}

static void cgen_block(AstNode *block) {
    scope_depth++;
    for (AstNode *s = block->stmts; s; s = s->next) cgen_stmt(s);
    scope_depth--;
}

#define MAX_CASES 256
typedef struct { int value; int label; } CaseEntry;

static void cgen_switch(AstNode *stmt) {
    CaseEntry cases[MAX_CASES];
    int case_count = 0;
    int default_label = -1;
    for (AstNode *s = stmt->stmts; s; s = s->next) {
        if (s->kind == AST_CASE) {
            if (case_count >= MAX_CASES) break;
            cases[case_count].value = s->ival;
            cases[case_count].label = new_label();
            s->op = cases[case_count].label;
            case_count++;
        } else if (s->kind == AST_DEFAULT) {
            default_label = new_label();
            s->op = default_label;
        }
    }
    cgen_expr(stmt->cond);
    emit1(0x50);
    int dispatch_label = new_label();
    emit_jmp(dispatch_label);
    int saved_break = break_target_label;
    break_target_label = new_label();
    for (AstNode *s = stmt->stmts; s; s = s->next) {
        if (s->kind == AST_CASE) set_label(s->op);
        else if (s->kind == AST_DEFAULT) set_label(s->op);
        else cgen_stmt(s);
    }
    set_label(dispatch_label);
    emit1(0x58);
    int i;
    for (i = 0; i < case_count; i++) {
        emit1(0x3D); emit4(cases[i].value);
        emit_jcc(0x84, cases[i].label);
    }
    if (default_label >= 0) emit_jmp(default_label);
    set_label(break_target_label);
    break_target_label = saved_break;
}

static void cgen_stmt(AstNode *stmt) {
    if (!stmt) return;
    switch (stmt->kind) {
    case AST_RETURN: cgen_return(stmt); break;
    case AST_IF: cgen_if(stmt); break;
    case AST_WHILE: cgen_while(stmt); break;
    case AST_FOR: cgen_for(stmt); break;
    case AST_DO_WHILE: {
        int start_label = new_label();
        int end_label = new_label();
        int saved_break = break_target_label;
        int saved_continue = continue_target_label;
        break_target_label = end_label;
        continue_target_label = start_label;
        set_label(start_label);
        cgen_stmt(stmt->loop_body);
        if (stmt->loop_cond) {
            cgen_expr(stmt->loop_cond);
            if (stmt->loop_cond->type_size == 8)
                { emit1(0x48); emit1(0x85); emit1(0xC0); }
            else
                { emit1(0x85); emit1(0xC0); }
            emit_jcc(0x85, start_label);
        }
        set_label(end_label);
        break_target_label = saved_break;
        continue_target_label = saved_continue;
        break;
    }
    case AST_BLOCK: cgen_block(stmt); break;
    case AST_EXPR_STMT: if (stmt->expr) cgen_expr(stmt->expr); break;
    case AST_NULL_STMT: break;
    case AST_ASM: if (stmt->asm_.asm_template) cgen_asm(stmt); break;
    case AST_SWITCH: cgen_switch(stmt); break;
    case AST_CASE: case AST_DEFAULT: break;
    case AST_BREAK: if (break_target_label >= 0) emit_jmp(break_target_label); break;
    case AST_CONTINUE: if (continue_target_label >= 0) emit_jmp(continue_target_label); break;
    case AST_GOTO:
        if (stmt->name) { int lid = get_or_create_goto_label(stmt->name);
            if (lid >= 0) emit_jmp(lid); } break;
    case AST_LABEL:
        if (stmt->name) { int lid = get_or_create_goto_label(stmt->name);
            if (lid >= 0) set_label(lid); } break;
    case AST_VAR_DECL:
        if (stmt->expr) {
            if (stmt->expr->kind == AST_ASSIGN) {
                for (AstNode *e = stmt->expr; e; e = e->next) cgen_expr(e);
            } else {
                cgen_expr(stmt->expr);
                int i;
                for (i = local_count - 1; i >= 0; i--) {
                    if (strcmp(locals[i].name, stmt->name) == 0 &&
                        locals[i].scope_depth <= scope_depth) {
                        if (locals[i].is_float) {
                            emit1(0xF2); emit1(0x0F); emit1(0x11);
                            emit1(0x45); emit1(locals[i].offset & 0xFF);
                        } else if (locals[i].size == 8) {
                            if (stmt->expr && !stmt->expr->is_float && stmt->expr->type_size < 8) {
                                int do_sext = 0;
                                if (stmt->expr->kind == AST_VAR) do_sext = 1;
                                else if (stmt->expr->kind == AST_CONSTANT &&
                                         stmt->expr->ival >= -2147483648L &&
                                         stmt->expr->ival <= 2147483647L) do_sext = 1;
                                if (do_sext) { e1(0x48); e1(0x63); e1(0xC0); }
                            }
                            store_rax_to_rbp(locals[i].offset);
                        } else if (locals[i].size == 1) {
                            if (disp8_fits(locals[i].offset)) { e1(0x88); e1(0x45); e1(locals[i].offset & 0xFF); }
                            else { e1(0x88); e1(0x85); e4(locals[i].offset); }
                        } else if (locals[i].size == 2) {
                            e1(0x66);
                            if (disp8_fits(locals[i].offset)) { e1(0x89); e1(0x45); e1(locals[i].offset & 0xFF); }
                            else { e1(0x89); e1(0x85); e4(locals[i].offset); }
                        } else {
                            store_eax_to_rbp(locals[i].offset);
                        }
                        break;
                    }
                }
            }
        } break;
    default: break;
    }
}

static void cgen_func_def(AstNode *func) {
    int is_variadic = func->is_variadic;
    reset_labels();
    scope_depth = 0;
    collect_locals(func);
    scope_depth = 0;
    if (is_variadic) frame_size += 48;
    int func_start = code_size;
    if (is_variadic) reg_save_offset = -(frame_size);
    else reg_save_offset = 0;
    func_nparams = func->ival;
    emit_prologue();
    if (func->name && strcmp(func->name, "main") == 0 && global_init_prog) {
        AstNode *gn;
        for (gn = global_init_prog->body; gn; gn = gn->next) {
            if (gn->kind == AST_VAR_DECL && gn->expr) {
                cgen_expr(gn->expr);
                int si = -1;
                int i;
                for (i = 0; i < sym_count; i++) {
                    if (syms[i].name && gn->name &&
                        strcmp(syms[i].name, gn->name) == 0) { si = i; break; }
                }
                if (si >= 0) {
                    int vsize = gn->ival > 0 ? gn->ival : 4;
                    if (vsize == 8) { e1(0x48); e1(0x89); e1(0x05); }
                    else { e1(0x89); e1(0x05); }
                    int ro = code_size;
                    e4(0);
                    if (rel_count < MAX_RELS) {
                        Elf64_Rela *r = &rels[rel_count++];
                        r->r_offset = ro;
                        r->r_info = ELF64_R_INFO(si + 1, R_X86_64_PC32);
                        r->r_addend = -4;
                    }
                }
            }
        }
    }
    {
        int int_reg = 0;
        int float_reg = 0;
        AstNode *p; int pi;
        for (p = func->params, pi = 0; p && pi < func_nparams; p = p->next, pi++) {
            if (p->kind == AST_VAR_DECL && p->name) {
                int i;
                for (i = 0; i < local_count; i++) {
                    if (strcmp(locals[i].name, p->name) == 0) {
                        if (locals[i].is_float) {
                            if (float_reg < 8) {
                                e1(0xF2); e1(0x0F); e1(0x11);
                                e1(0x45 | ((float_reg & 7) << 3));
                                e1(locals[i].offset & 0xFF);
                            } float_reg++;
                        } else {
                            int param_size = locals[i].size;
                            int use64 = (param_size == 8);
                            if (int_reg < 6) {
                                if (param_size == 1) {
                                    switch (int_reg) {
                                    case 0: e1(0x40); e1(0x88); e1(0x7D); break;
                                    case 1: e1(0x40); e1(0x88); e1(0x75); break;
                                    case 2: e1(0x88); e1(0x55); break;
                                    case 3: e1(0x88); e1(0x4D); break;
                                    case 4: e1(0x41); e1(0x88); e1(0x45); break;
                                    case 5: e1(0x41); e1(0x88); e1(0x4D); break;
                                    } e1(locals[i].offset & 0xFF);
                                } else {
                                    if (param_size == 2) e1(0x66);
                                    switch (int_reg) {
                                    case 0: if (use64) e1(0x48); e1(0x89); e1(0x7D); break;
                                    case 1: if (use64) e1(0x48); e1(0x89); e1(0x75); break;
                                    case 2: if (use64) e1(0x48); e1(0x89); e1(0x55); break;
                                    case 3: if (use64) e1(0x48); e1(0x89); e1(0x4D); break;
                                    case 4: if (use64) e1(0x4C); else e1(0x44); e1(0x89); e1(0x45); break;
                                    case 5: if (use64) e1(0x4C); else e1(0x44); e1(0x89); e1(0x4D); break;
                                    } e1(locals[i].offset & 0xFF);
                                }
                            } else {
                                int sd = 0x10 + (int_reg - 6) * 8;
                                if (param_size == 1) {
                                    e1(0x0F); e1(0xB6); e1(0x45); e1(sd & 0xFF);
                                    emit_store_rbp8(locals[i].offset);
                                } else if (param_size == 2) {
                                    e1(0x0F); e1(0xB7); e1(0x45); e1(sd & 0xFF);
                                    emit_store_rbp16(locals[i].offset);
                                } else if (use64) {
                                    load_rax_from_rbp(sd);
                                    store_rax_to_rbp(locals[i].offset);
                                } else {
                                    e1(0x8B); e1(0x45); e1(sd & 0xFF);
                                    store_eax_to_rbp(locals[i].offset);
                                }
                            }
                            int_reg++;
                        } break;
                    }
                }
            }
        }
    }
    if (is_variadic) {
        int sb = reg_save_offset;
        emit_store_rbp64(7, sb);
        emit_store_rbp64(6, sb + 8);
        emit_store_rbp64(2, sb + 16);
        emit_store_rbp64(1, sb + 24);
        e1(0x4C); e1(0x89);
        if (disp8_fits(sb+32)) { e1(0x45); e1((sb+32)&0xFF); }
        else { e1(0x85); e4(sb+32); }
        e1(0x4C); e1(0x89);
        if (disp8_fits(sb+40)) { e1(0x4D); e1((sb+40)&0xFF); }
        else { e1(0x8D); e4(sb+40); }
    }
    cgen_block(func->body);
    if (code_size <= 0 || code_buf[code_size - 1] != 0xC3) emit_epilogue();
    int func_end = code_size;
    apply_fixups();
    add_sym(func->name ? func->name : "", func_start,
            func_end - func_start, !func->is_static, 1);
}

void cgen_init(void) {
    code_size = 0; sym_count = 0; rel_count = 0;
    local_count = 0; frame_size = 0;
    reg_save_offset = 0; func_nparams = 0; scope_depth = 0;
    strtab_len = 0; strtab[strtab_len++] = '\0';
    strpool_size = 0; str_info_count = 0; elf_bss_size = 0;
    for (int _i = 0; _i < MAX_SYMS; _i++) {
        global_elem_size[_i] = 0; global_base_elem_size[_i] = 0;
    }
}

void cgen_program(AstNode *prog) {
    if (!prog || prog->kind != AST_PROGRAM) return;
    global_init_prog = prog;
    int bss_offset = 0;
    for (AstNode *node = prog->body; node; node = node->next) {
        if (node->kind == AST_VAR_DECL) {
            int vsize = node->ival > 0 ? node->ival : 4;
            bss_offset = (bss_offset + 7) & -8;
            if (sym_count < MAX_SYMS) {
                int si = sym_count;
                CgenSym *s = &syms[si];
                s->name = node->name;
                s->offset = bss_offset;
                s->size = vsize;
                s->is_global = !node->is_static;
                s->is_func = 0;
                s->shndx = 3;
                s->sym_idx = -1;
                global_elem_size[si] = (vsize > 8) ? node->elem_size : 0;
                global_base_elem_size[si] = node->base_elem_size;
                sym_count++;
            }
            bss_offset += vsize;
        }
    }
    elf_bss_size = bss_offset;
    for (AstNode *node = prog->body; node; node = node->next) {
        if (node->kind == AST_FUNC_DEF) cgen_func_def(node);
        else if (node->kind == AST_ASM) cgen_asm(node);
    }
    if (strpool_size > 0) {
        if (code_size + strpool_size > CODE_BUF_SIZE) {
            __write(2, "toyc: code buffer overflow\n", 26);
            __exit(1);
        }
        int string_start = code_size;
        int i;
        for (i = 0; i < strpool_size; i++)
            code_buf[code_size++] = strpool_buf[i];
        for (i = 0; i < str_info_count; i++) {
            int si = str_infos[i].sym_index;
            if (si >= 0 && si < sym_count)
                syms[si].offset = string_start + str_infos[i].pool_offset;
        }
    }
}

// ============================================================
// 测试框架
// ============================================================

static int test_passed = 0, test_failed = 0;

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
            : : "a"(1), "D"(1), "S"(&ch), "d"(1L)
            : "rcx", "r11", "memory"); }
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
static void psr(void) {
    print_str("  -> "); print_dec(test_passed); print_str(" passed, ");
    print_dec(test_failed); print_str(" failed\n");
}

static void reset(void) {
    code_size = 0; sym_count = 0; rel_count = 0;
    local_count = 0; frame_size = 0; strtab_len = 0; strpool_size = 0;
    str_info_count = 0; reg_save_offset = 0; func_nparams = 0;
    scope_depth = 0; elf_bss_size = 0;
    global_init_prog = 0;
    break_target_label = -1; continue_target_label = -1;
    label_count = 0; fixup_count = 0; goto_label_count = 0;
    /* 重置 label 计数器（static int 在函数内，无法重置，只能从 0 开始） */
    /* 测试中每个分组用 reset 保证 state 干净 */
}

// ============================================================
// cgen.c 测试用例
// ============================================================

/* 1. cgen_init */
static void test_cgen_init(void) {
    run_section("cgen_init");
    code_size = 123; sym_count = 99; rel_count = 88;
    local_count = 7; frame_size = 256;
    strtab_len = 42; strpool_size = 99;
    cgen_init();
    CHECK(code_size == 0, "code_size = 0");
    CHECK(sym_count == 0, "sym_count = 0");
    CHECK(rel_count == 0, "rel_count = 0");
    CHECK(local_count == 0, "local_count = 0");
    CHECK(frame_size == 0, "frame_size = 0");
    CHECK(strtab_len == 1, "strtab_len = 1 (null byte)");
    CHECK(strtab[0] == '\0', "strtab[0] = '\\0'");
    CHECK(strpool_size == 0, "strpool_size = 0");
    CHECK(elf_bss_size == 0, "elf_bss_size = 0");
}

/* 2. 标签系统 */
static void test_labels(void) {
    run_section("Label System");

    reset();
    /* new_label → 从 0 开始递增 */
    CHECK(new_label() == 0, "first label = 0");
    CHECK(new_label() == 1, "second label = 1");
    CHECK(new_label() == 2, "third label = 2");

    reset();
    /* set_label + find_label_offset */
    int l1 = new_label();
    int l2 = new_label();
    code_size = 100;
    set_label(l1);
    CHECK(code_size == 100, "set_label doesn't change code_size");
    CHECK(find_label_offset(l1) == 100, "find_label_offset after set_label");
    code_size = 200;
    set_label(l2);
    CHECK(find_label_offset(l2) == 200, "second label offset");
    CHECK(find_label_offset(l1) == 100, "first label still valid");

    /* 未设置的标签 → -1 */
    CHECK(find_label_offset(999) == -1, "unset label → -1");

    /* reset_labels */
    reset();
    int l3 = new_label(); set_label(l3);
    reset_labels();
    CHECK(find_label_offset(l3) == -1, "after reset_labels, label gone");
}

/* 3. 跳转指令：向后跳 */
static void test_jmp_backward(void) {
    run_section("JMP Backward");

    reset();
    int l = new_label();
    code_size = 10;
    set_label(l);  /* label at offset 10 */
    code_size = 20;

    /* emit_jmp to label at 10, from code_size=20:
     * jmp rel32 emitted at offset 20: 0xE9 + disp4
     * disp = label_off - (jump_off + 5) = 10 - (20 + 5) = -15 = 0xFFFFFFF1 */
    emit_jmp(l);
    /* jmp starts at code_buf[20] */
    CHECK_CODE(20, 0xE9, 0xF1, 0xFF, 0xFF, 0xFF);
    CHECK(code_size == 25, "jmp backward: 5 bytes, code_size=25");
}

/* 4. 跳转指令：向前跳 + 回填 */
static void test_jmp_forward(void) {
    run_section("JMP Forward + Fixup");

    reset();
    int l = new_label();

    /* emit_jmp to future label — placeholder emitted */
    emit_jmp(l);
    CHECK(code_size == 5, "forward jmp: placeholder at 5 bytes");
    CHECK_CODE(0, 0xE9, 0x00, 0x00, 0x00, 0x00);

    /* emit more code */
    emit1(0x90); emit1(0x90); /* 2 NOPs */
    code_size = 7;

    /* set_label, then apply_fixups */
    set_label(l);
    apply_fixups();

    /* After fixup: jmp rel32 displacement = label_off - (jump_off + 5) = 7 - (0 + 5) = 2 */
    CHECK_CODE(0, 0xE9, 0x02, 0x00, 0x00, 0x00);
}

/* 5. jcc forward + fixup */
static void test_jcc_forward(void) {
    run_section("JCC Forward + Fixup");

    reset();
    int l = new_label();

    /* emit_jcc(0x84, future_label) — je rel32: 0F 84 + placeholder */
    emit_jcc(0x84, l);
    CHECK(code_size == 6, "forward jcc: placeholder at 6 bytes");
    CHECK_CODE(0, 0x0F, 0x84, 0x00, 0x00, 0x00, 0x00);

    emit1(0x90); emit1(0x90); emit1(0x90); /* 3 NOPs */
    set_label(l);
    apply_fixups();

    /* displ = 9 - (0 + 6) = 3 */
    CHECK_CODE(0, 0x0F, 0x84, 0x03, 0x00, 0x00, 0x00);
}

/* 6. emit_prologue + emit_epilogue */
static void test_prologue_epilogue(void) {
    run_section("Prologue + Epilogue");

    reset();
    frame_size = 0;
    emit_prologue();
    CHECK_CODE(0, 0x55, 0x48, 0x89, 0xE5);
    CHECK(code_size == 4, "prologue frame=0: 4 bytes");

    reset(); code_size = 0;
    frame_size = 32;
    emit_prologue();
    /* push rbp; mov rbp,rsp; sub rsp, 32(0x20) — as disp8 */
    CHECK_CODE(0, 0x55, 0x48, 0x89, 0xE5, 0x48, 0x83, 0xEC, 0x20);
    CHECK(code_size == 8, "prologue frame=32: 8 bytes");

    reset(); code_size = 0;
    frame_size = 200;  /* >127, needs disp32 */
    emit_prologue();
    /* push rbp; mov rbp,rsp; sub rsp, 208(aligned) — as disp32 */
    CHECK_CODE(0, 0x55, 0x48, 0x89, 0xE5, 0x48, 0x81, 0xEC);
    /* aligned = (200+15)&-16 = 208 = 0xD0 */
    CHECK(code_size == 11, "prologue frame=200: 11 bytes (disp32)");

    reset(); code_size = 0;
    emit_epilogue();
    CHECK_CODE(0, 0x48, 0x89, 0xEC, 0x5D, 0xC3);
    CHECK(code_size == 5, "epilogue: 5 bytes");
}

/* 7. add_sym */
static void test_add_sym(void) {
    run_section("add_sym");

    reset();
    int idx = add_sym("foo", 0, 4, 1, 1);
    CHECK(idx == 0, "first sym index = 0");
    CHECK(sym_count == 1, "sym_count = 1");
    CHECK(syms[0].name && strcmp(syms[0].name, "foo") == 0, "sym name = 'foo'");
    CHECK(syms[0].offset == 0, "sym offset = 0");
    CHECK(syms[0].size == 4, "sym size = 4");
    CHECK(syms[0].is_global == 1, "sym is_global = 1");
    CHECK(syms[0].is_func == 1, "sym is_func = 1");
    CHECK(syms[0].shndx == 1, "sym shndx = 1 (.text)");

    idx = add_sym("bar", 64, 8, 1, 0);
    CHECK(idx == 1, "second sym index = 1");

    /* UNDEF → DEFINED: 先创建 UNDEF */
    int undef_idx = add_sym("later", 0, 0, 0, 0);
    syms[undef_idx].shndx = 0;  /* mark UNDEF */
    int redef = add_sym("later", 128, 16, 0, 0);
    CHECK(redef == undef_idx, "redef returns UNDEF index");
    CHECK(syms[undef_idx].shndx == 1, "UNDEF → shndx=1 after redef");
}

/* 8. cgen_return */
static void test_cgen_return(void) {
    run_section("cgen_return");

    reset();
    AstNode ret_noexpr;
    ret_noexpr.kind = AST_RETURN; ret_noexpr.expr = 0; ret_noexpr.next = 0;
    cgen_return(&ret_noexpr);
    CHECK_CODE(0, 0x48, 0x89, 0xEC, 0x5D, 0xC3);
    CHECK(code_size == 5, "return without expr: epilogue");

    reset(); code_size = 0;
    AstNode val; val.kind = AST_CONSTANT; val.ival = 42; val.next = 0;
    AstNode ret_expr;
    ret_expr.kind = AST_RETURN; ret_expr.expr = &val; ret_expr.next = 0;
    cgen_return(&ret_expr);
    /* cgen_expr is no-op, so just epilogue */
    CHECK_CODE(0, 0x48, 0x89, 0xEC, 0x5D, 0xC3);
}

/* 9. cgen_if */
static void test_cgen_if(void) {
    run_section("cgen_if");

    reset();
    /* if(cond) { then } — cond is const(0), then is return */
    AstNode cond; cond.kind = AST_CONSTANT; cond.ival = 1; cond.type_size = 4; cond.next = 0;
    AstNode ret; ret.kind = AST_RETURN; ret.expr = 0; ret.next = 0;
    AstNode if_node; if_node.kind = AST_IF; if_node.next = 0;
    if_node.cond = &cond; if_node.then_stmt = &ret; if_node.else_stmt = 0;

    cgen_if(&if_node);
    /* cgen_expr(cond) [no-op], test eax,eax (85 C0),
     * je to else_label (0F 84 xx xx xx xx),
     * cgen_return (48 89 EC 5D C3),
     * else_label: */
    CHECK(code_size >= 11, "if-then emits at least 11 bytes");
    CHECK_CODE(0, 0x85, 0xC0);  /* test eax,eax */
    CHECK_CODE(2, 0x0F, 0x84); /* je rel32 */
}

/* 10. cgen_while */
static void test_cgen_while(void) {
    run_section("cgen_while");

    reset();
    AstNode cond; cond.kind = AST_CONSTANT; cond.ival = 1; cond.type_size = 4; cond.next = 0;
    AstNode body_ret; body_ret.kind = AST_RETURN; body_ret.expr = 0; body_ret.next = 0;
    AstNode while_node; while_node.kind = AST_WHILE; while_node.next = 0;
    while_node.loop_cond = &cond; while_node.loop_body = &body_ret;

    cgen_while(&while_node);
    /* start_label, test/je, return, jmp back, end_label */
    CHECK(code_size >= 11, "while loop emits code");
    /* test eax,eax at some point */
    int found_test = 0, found_jmp = 0, found_ret = 0;
    int i;
    for (i = 0; i < code_size - 1; i++) {
        if (code_buf[i] == 0x85 && code_buf[i+1] == 0xC0) found_test = 1;
        if (code_buf[i] == 0xE9) found_jmp = 1;
        if (code_buf[i] == 0xC3) found_ret = 1;
    }
    CHECK(found_test, "while has test eax,eax");
    CHECK(found_jmp, "while has jmp (loop back)");
    CHECK(found_ret, "while has ret (body)");
}

/* 11. collect_locals */
static void test_collect_locals(void) {
    run_section("collect_locals");

    reset();
    /* func { int a; int b; } */
    AstNode block_stmts[2];
    block_stmts[0].kind = AST_VAR_DECL; block_stmts[0].name = "a"; block_stmts[0].ival = 4;
    block_stmts[0].type_size = 4; block_stmts[0].elem_size = 0; block_stmts[0].base_elem_size = 0;
    block_stmts[0].is_float = 0; block_stmts[0].is_unsigned = 0; block_stmts[0].elem_is_unsigned = 0;
    block_stmts[0].expr = 0; block_stmts[0].next = &block_stmts[1];
    block_stmts[1].kind = AST_VAR_DECL; block_stmts[1].name = "b"; block_stmts[1].ival = 8;
    block_stmts[1].type_size = 8; block_stmts[1].elem_size = 0; block_stmts[1].base_elem_size = 0;
    block_stmts[1].is_float = 0; block_stmts[1].is_unsigned = 0; block_stmts[1].expr = 0;
    block_stmts[1].next = 0;

    AstNode block; block.kind = AST_BLOCK; block.stmts = &block_stmts[0]; block.next = 0;

    AstNode func; func.kind = AST_FUNC_DEF; func.body = &block; func.name = "test"; func.next = 0;

    collect_locals(&func);
    CHECK(local_count == 2, "2 locals collected");
    CHECK(strcmp(locals[0].name, "a") == 0, "local[0] = 'a'");
    CHECK(locals[0].offset == -4, "local[0] offset = -4");
    CHECK(locals[0].size == 4, "local[0] size = 4");
    CHECK(strcmp(locals[1].name, "b") == 0, "local[1] = 'b'");
    CHECK(locals[1].offset == -12, "local[1] offset = -12 (4+8)");
    CHECK(locals[1].size == 8, "local[1] size = 8");
    CHECK(frame_size == 12, "frame_size = 12");
}

/* 12. cgen_func_def — simple function */
static void test_cgen_func_def_simple(void) {
    run_section("cgen_func_def (simple)");

    reset();
    /* int test(void) { return 42; } */
    AstNode val; val.kind = AST_CONSTANT; val.ival = 42; val.next = 0;
    AstNode ret; ret.kind = AST_RETURN; ret.expr = &val; ret.next = 0;
    AstNode body; body.kind = AST_BLOCK; body.stmts = &ret; body.next = 0;

    AstNode func; func.kind = AST_FUNC_DEF;
    func.name = "test_func";
    func.body = &body;
    func.params = 0;
    func.ival = 0;  /* func_nparams */
    func.is_variadic = 0;
    func.is_static = 0;

    cgen_func_def(&func);
    CHECK(code_size > 0, "func def emits code");
    /* prologue + body + epilogue */
    CHECK(code_buf[code_size - 1] == 0xC3, "func def ends with ret");

    /* verify symbol created */
    int found = 0;
    int i; for (i = 0; i < sym_count; i++) {
        if (syms[i].name && strcmp(syms[i].name, "test_func") == 0) found = 1;
    }
    CHECK(found, "test_func symbol created");
}

/* 13. cgen_program — BSS variables only */
static void test_cgen_program_bss(void) {
    run_section("cgen_program (BSS)");

    reset();
    AstNode var1; var1.kind = AST_VAR_DECL; var1.name = "g1"; var1.ival = 4;
    var1.is_static = 0; var1.elem_size = 0; var1.base_elem_size = 0; var1.next = &var1 + 1;
    AstNode var2; var2.kind = AST_VAR_DECL; var2.name = "g2"; var2.ival = 8;
    var2.is_static = 0; var2.elem_size = 0; var2.base_elem_size = 0; var2.next = 0;

    var1.next = &var2;

    AstNode prog; prog.kind = AST_PROGRAM; prog.body = &var1; prog.next = 0;

    cgen_program(&prog);
    CHECK(sym_count == 2, "2 BSS symbols");
    CHECK(elf_bss_size == 16, "elf_bss_size = 4(aligned) + 8 = 16");
    /* g1 at bss offset 0, g2 at bss offset 8 (aligned from 4) */
    int ig1 = -1, ig2 = -1;
    int i; for (i = 0; i < sym_count; i++) {
        if (syms[i].name && strcmp(syms[i].name, "g1") == 0) ig1 = i;
        if (syms[i].name && strcmp(syms[i].name, "g2") == 0) ig2 = i;
    }
    CHECK(ig1 >= 0, "g1 in syms");
    CHECK(ig2 >= 0, "g2 in syms");
    CHECK(syms[ig1].offset == 0, "g1 bss offset = 0");
    CHECK(syms[ig2].offset == 8, "g2 bss offset = 8");
    CHECK(syms[ig1].shndx == 3, "g1 shndx = 3 (.bss)");
    CHECK(syms[ig2].shndx == 3, "g2 shndx = 3 (.bss)");
}

/* 14. cgen_switch */
static void test_cgen_switch(void) {
    run_section("cgen_switch");

    reset();
    /* switch(cond) { case 1: return; case 2: return; } */
    AstNode cond; cond.kind = AST_CONSTANT; cond.ival = 0;
    AstNode ret1; ret1.kind = AST_RETURN; ret1.expr = 0; ret1.next = 0;
    AstNode case1; case1.kind = AST_CASE; case1.ival = 1; case1.next = &ret1;

    AstNode ret2; ret2.kind = AST_RETURN; ret2.expr = 0; ret2.next = 0;
    AstNode case2; case2.kind = AST_CASE; case2.ival = 2; case2.next = &ret2;

    case1.next = &ret1; ret1.next = &case2; case2.next = &ret2; ret2.next = 0;

    AstNode switch_node; switch_node.kind = AST_SWITCH; switch_node.next = 0;
    switch_node.cond = &cond; switch_node.stmts = &case1;

    cgen_switch(&switch_node);
    CHECK(code_size > 0, "switch emits code");

    /* should have two je + dispatch table */
    int je_count = 0;
    int i; for (i = 0; i < code_size - 1; i++) {
        if (code_buf[i] == 0x0F && code_buf[i+1] == 0x84) je_count++;
    }
    CHECK(je_count == 2, "switch dispatch has 2 je instructions");
}

/* 15. break/continue */
static void test_break_continue(void) {
    run_section("break/continue");

    reset();
    int saved_break = break_target_label;

    /* Set break target */
    int end_lbl = new_label();
    break_target_label = end_lbl;

    /* Emit break */
    AstNode brk; brk.kind = AST_BREAK;
    cgen_stmt(&brk);
    CHECK(code_size == 5, "break: jmp (placeholder forward)");
    CHECK_CODE(0, 0xE9, 0x00, 0x00, 0x00, 0x00);

    set_label(end_lbl);
    apply_fixups();
    CHECK_CODE(0, 0xE9, 0x00, 0x00, 0x00, 0x00); /* displ = 5-(0+5) = 0, points to next instr */

    break_target_label = saved_break;

    /* continue out of loop context — no target, no code */
    reset();
    AstNode cont; cont.kind = AST_CONTINUE;
    continue_target_label = -1;
    cgen_stmt(&cont);
    CHECK(code_size == 0, "continue with no target = no code");
}

/* 16. get_or_create_goto_label */
static void test_goto_label(void) {
    run_section("goto/label");

    reset();
    /* Create goto target (backward jump) */
    AstNode label_node; label_node.kind = AST_LABEL; label_node.name = "my_label";
    cgen_stmt(&label_node);
    int label_off = code_size;  /* label here */

    /* Compute expected displacement BEFORE emitting jmp (which changes code_size) */
    int expected_disp = label_off - (label_off + 5);  /* = -5, backward jmp */

    AstNode goto_node; goto_node.kind = AST_GOTO; goto_node.name = "my_label";
    cgen_stmt(&goto_node);
    CHECK_CODE(0, 0xE9);
    int disp_val = (int)(code_buf[1] | (code_buf[2]<<8) | (code_buf[3]<<16) | (code_buf[4]<<24));
    CHECK(disp_val == expected_disp, "goto displacement matches label offset");
}

// ============================================================
// 入口
// ============================================================

void __tlibc_start(void) {
    print_str("=== cgen.c standalone tests ===\n");

    test_cgen_init();              psr();
    test_labels();                 psr();
    test_jmp_backward();           psr();
    test_jmp_forward();            psr();
    test_jcc_forward();            psr();
    test_prologue_epilogue();      psr();
    test_add_sym();                psr();
    test_cgen_return();            psr();
    test_cgen_if();                psr();
    test_cgen_while();             psr();
    test_collect_locals();         psr();
    test_cgen_func_def_simple();   psr();
    test_cgen_program_bss();       psr();
    test_cgen_switch();            psr();
    test_break_continue();         psr();
    test_goto_label();             psr();

    print_str("\n=== ");
    print_str(test_failed == 0 ? "ALL PASSED" : "SOME FAILED");
    print_str(" ===\n");

    sys_exit(test_failed != 0 ? 1 : 0);
}
