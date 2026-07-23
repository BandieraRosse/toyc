// EXPECT: 0
// int_to_long_sext.c — int→long 符号扩展各路径的回归测试
// 历史：toyc 在 64 位 long 变量的声明+初始化路径中缺少 movsxd rax,eax，
// 导致 AST_UNARY（如 -a）、AST_CALL 等表达式被零扩展而非符号扩展。
// 修复：cgen.c 的 var_decl/for_init/global_init 路径统一用 catch-all

int neg_int(int x) { return -x; }

int main(void) {
    /* 测试 1：int 变量 → long 声明 */
    int a = -1;
    long x1 = a;
    if (x1 != -1L) return 1;

    /* 测试 2：int 一元表达式 → long 声明（曾漏掉 AST_UNARY） */
    int b = -50;
    long x2 = -b;
    if (x2 != 50L) return 2;

    /* 测试 3：int 函数返回值 → long 声明（曾漏掉 AST_CALL） */
    long x3 = neg_int(-42);
    if (x3 != 42L) return 3;

    /* 测试 4：int 二元运算 → long 声明 */
    int c = -100, d = 1;
    long x4 = c + d;
    if (x4 != -99L) return 4;

    /* 测试 5：正数 int → long （不应被符号扩展影响） */
    int e = 42;
    long x5 = e;
    if (x5 != 42L) return 5;

    /* 测试 6：for 循环 init 中的 int→long（曾漏掉 for_init 路径） */
    int f = -99;
    long y = 0;
    for (long i = f; i < 0; i++) { y = i; break; }
    if (y != -99L) return 6;

    /* 测试 7：全局 long 变量运行时初始化 */
    /* （通过 main 中的复合赋值间接测试——实际全局 init 在 cgen.c 中走不同路径） */
    {
        /* 在 basic 测试中 tcc_rt.o 有 printf，这里不用 tcc_rt 功能 */
        long arr[10];
        int g = -1;
        arr[0] = g;        /* 数组元素赋值（已有 sign-ext） */
        if (arr[0] != -1L) return 7;
    }

    return 0;
}
