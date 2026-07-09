/*
 * 32_tas_emulate.c — tas_assemble 栈布局模拟测试
 *
 * 本测试来自调试 tas.c 自举编译的过程。重现 tas_assemble 函数中
 * 标签定义路径的完整模式：多个局部 int + const char* 变量，
 * 栈上 char nbuf[256] 大数组，然后调用 6 参数的 add_sym()。
 *
 * 在调试早期怀疑 tcc 对复杂栈布局或 6 参数调用的代码生成有 bug，
 * 此测试确认这些模式本身没有问题。实际 bug 是 struct 数组的 sizeof
 * 计算错误和 2D 数组 stride 计算错误（见 pending/12_struct_array_sizeof.c）。
 *
 * 保留此测试作为回归验证：确保 tcc 对混合类型局部变量 + 大栈数组
 * + 多参数调用的代码生成保持正确。
 *
 * EXP: 0
 */
static char sym_names[256][16];
static int elf_sym_count;

int add_sym(const char *name, int offset, int size,
            int is_global, int is_func, int shndx) {
    if (elf_sym_count >= 256) return -1;
    int ni = 0;
    while (name[ni] && ni < 15) {
        sym_names[elf_sym_count][ni] = name[ni];
        ni++;
    }
    sym_names[elf_sym_count][ni] = '\0';
    elf_sym_count++;
    return elf_sym_count - 1;
}

int main(void) {
    int last_label_sym = -1;
    int next_is_global = 0;
    int next_is_func = 0;
    const char *line = "main";
    const char *end = line + 4;
    int nlen = 4;
    const char *id_start = line;

    /* Replicating label definition */
    char nbuf[256];
    int ni;
    for (ni = 0; ni < nlen && ni < 255; ni++)
        nbuf[ni] = id_start[ni];
    nbuf[ni] = '\0';

    last_label_sym = add_sym(nbuf, 0, 0, next_is_global, next_is_func, 1);

    /* Verify */
    if (sym_names[0][0] != 'm') return 1;
    if (sym_names[0][1] != 'a') return 2;
    if (sym_names[0][2] != 'i') return 3;
    if (sym_names[0][3] != 'n') return 4;

    return 0;
}

void __tlibc_start(void) {
    long rc = main();
    __asm__ __volatile__ (
        "syscall"
        : : "a"((long)60), "D"(rc)
        : "rcx", "r11"
    );
}
