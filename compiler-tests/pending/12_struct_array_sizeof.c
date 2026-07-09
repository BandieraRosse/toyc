/*
 * test: struct 数组 sizeof/traversal bug
 *
 * tcc 对 struct 数组的 sizeof 计算错误（如 sizeof(InstrDef) 返回 320 而非 24），
 * 导致 `arr[i].field` 格式的遍历访问错误内存。使用数组名后直接 `.field` 也受影响。
 *
 * 同时，tcc 对 2D 数组（char[256][128]、int[256][32]）的第一维步长计算为 sizeof(元素类型)
 * 而非 sizeof(内层数组)，导致 arr[i][j] 访问错误。
 *
 * 已知：tas.c 中 instrs[] 和 reg_table[] 受影响，sym_names[256][128] 也受影响。
 * tas.c 的修复方式：将 struct 数组遍历改为 if/else 链，将 2D 数组改为一维手动索引。
 *
 * 这两个 bug 的根因在 tcc 的代码生成器（cgen_expr.c）中：
 * - struct 数组的 elem_size 未从 AST 正确传播
 * - 2D 数组的内层 sizeof 计算为 1（退化到 base type size）
 *
 * EXP: 0
 */

/* ─── struct 数组 sizeof bug ─── */

typedef struct {
    const char *name;
    int fmt;
    unsigned char opcode[2];
    int oplen;
    int op_ext;
} InstrDef;

static const InstrDef instrs[] = {
    {"mov", 1, {0x89},1,0},
    {"mov", 2, {0xC7},1,0},
    {"mov", 3, {0xB0},1,0},
    {"lea", 4, {0x8D},1,0},
    {"ret", 5, {0xC3},1,0},
    {0, 0, {0},0,0}
};

static int find_instr_by_fmt(const char *name, int want_fmt) {
    int i;
    for (i = 0; instrs[i].name; i++) {
        if (instrs[i].name[0] == name[0] && instrs[i].fmt == want_fmt)
            return 1;
    }
    return 0;
}

/* ─── 2D 数组 stride bug ─── */

static char sym_names[4][16];
static int  int_matrix[4][8];

static void fill_names(void) {
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 8; j++) {
            sym_names[i][j] = (char)('a' + i * 8 + j);
            int_matrix[i][j] = i * 8 + j;
        }
}

static int check_names(void) {
    int i, j;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 8; j++) {
            if (sym_names[i][j] != (char)('a' + i * 8 + j))
                return i * 100 + j + 1;
            if (int_matrix[i][j] != i * 8 + j)
                return 200 + i * 100 + j + 1;
        }
    return 0;
}

int main(void) {
    /* 测试 struct 数组遍历 */
    if (!find_instr_by_fmt("mov", 2)) return 10;

    /* 测试 2D 数组 */
    fill_names();
    {
        int r = check_names();
        if (r != 0) return r;
    }

    return 0;
}

void __tlibc_start(void) {
    __asm__ __volatile__ ("syscall" : : "a"((long)60), "D"((long)main()) : "rcx", "r11");
}
