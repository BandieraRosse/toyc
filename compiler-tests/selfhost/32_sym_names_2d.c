/*
 * 32_sym_names_2d.c — 2D char 数组读写验证
 *
 * 本测试来自调试 toyas.c 自举编译的过程。toyas.c 的 add_sym() 使用
 *   sym_names[elf_sym_count][ni] = name[ni];
 * 模式（static char sym_names[256][128]），toyc 曾在此处生成错误代码。
 *
 * 根因：toyc 对全局 2D 数组的第一维步长计算为 sizeof(char)=1 而非
 *   sizeof(char[128])=128，导致 sym_names[i] 访问到错误行。
 * 该 bug 在 parse.c 中有部分修复（gv_elem_at 计算），但旧版自举种子
 * bootstrap/toyc-boot3 不包含该修复，因此 toyas.c 最终将 sym_names 改为
 * 1D 数组 + 手动偏移来彻底规避。
 *
 * 本测试验证 toyc 对 char[256][16] 的读写是否正确。
 * 参见：pending/12_struct_array_sizeof.c
 *
 * EXP: 0
 */

static char sym_names[256][16];
static int elf_sym_count;

int add_sym(const char *name) {
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
    char nbuf[16];
    int ni;
    const char *id_start = "main";

    for (ni = 0; ni < 4 && ni < 15; ni++)
        nbuf[ni] = id_start[ni];
    nbuf[ni] = '\0';

    add_sym(nbuf);

    if (sym_names[0][0] != 'm') return 1;
    if (sym_names[0][1] != 'a') return 2;
    if (sym_names[0][2] != 'i') return 3;
    if (sym_names[0][3] != 'n') return 4;

    /* Test with variable index */
    id_start = "test";
    for (ni = 0; ni < 4 && ni < 15; ni++)
        nbuf[ni] = id_start[ni];
    nbuf[ni] = '\0';

    add_sym(nbuf);

    if (sym_names[1][0] != 't') return 10;
    if (sym_names[1][1] != 'e') return 11;
    if (sym_names[1][2] != 's') return 12;
    if (sym_names[1][3] != 't') return 13;

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
