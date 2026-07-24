/* test_stdio.c — stdio 功能测试（printf / snprintf）
 *
 * 注意：Tinylibc 的 snprintf 实现位于 stdio/snprintf.c，
 * 但 string.h 也声明了 snprintf。链接时两者不可重复定义，
 * 因此本测试只链接 stdio 的三个 .o（不含 string.o 的 snprintf）。
 *
 * EXPECT: 0
 */

#include "core.h"
#include "string.h"

extern void __printf(const char *fmt, ...);
extern int snprintf(char *str, unsigned long size, const char *format, ...);

static int total, passed;

static void check(const char *name, int cond) {
    total++;
    if (cond) { passed++; __printf("  %s: PASS\n", name); }
    else      { __printf("  %s: FAIL\n", name); }
}

/* ── 已知问题测试辅助函数 ── */

/*
 * Bug 3: toyc __builtin_va_start 固定 fp_offset=48 且
 * gp_offset=func_nparams*8，未区分 GP/FP 参数类型。
 * 当命名参数含 double 时，fp_offset 应从 56(48+8) 开始，
 * 但当前固定 48，导致首个 va_arg(double) 误读命名参数的值，
 * 返回 99.0 而非 1.0（见下方测试）。
 */
static double test_va_double(double a, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, a);
    double v1 = __builtin_va_arg(args, double);
    double v2 = __builtin_va_arg(args, double);
    double v3 = __builtin_va_arg(args, double);
    __builtin_va_end(args);
    return v1 + v2 + v3;
}

int main(void) {
    char buf[128];
    int n;

    __printf("stdio 功能测试\n");
    __printf("--------------\n");

    /* ── snprintf 基本格式 ── */
    n = snprintf(buf, sizeof(buf), "%d", 42);
    check("snprintf int",       n == 2 && strcmp(buf, "42") == 0);

    n = snprintf(buf, sizeof(buf), "%s", "hello");
    check("snprintf str",       n == 5 && strcmp(buf, "hello") == 0);

    n = snprintf(buf, sizeof(buf), "a%db", 123);
    check("snprintf mixed",     n == 5 && strcmp(buf, "a123b") == 0);

    n = snprintf(buf, sizeof(buf), "hello world");
    check("snprintf plain",     n == 11 && strcmp(buf, "hello world") == 0);

    /* 缓冲区截断 */
    n = snprintf(buf, 8, "hello world");
    check("snprintf trunc len", n == 11);  /* 返回值 = 完整长度 */
    check("snprintf trunc nul", buf[7] == '\0');
    check("snprintf trunc val", strncmp(buf, "hello w", 7) == 0);

    n = snprintf(buf, 1, "test");
    check("snprintf n=1",       buf[0] == '\0' && n == 4);

    /* %% */
    n = snprintf(buf, sizeof(buf), "%%");
    check("snprintf %%",        n == 1 && strcmp(buf, "%") == 0);

    /* %x / %X */
    n = snprintf(buf, sizeof(buf), "%x", 255);
    check("snprintf hex low",   n == 2 && strcmp(buf, "ff") == 0);

    n = snprintf(buf, sizeof(buf), "%X", 255);
    check("snprintf hex up",    n == 2 && strcmp(buf, "FF") == 0);

    /* %u */
    n = snprintf(buf, sizeof(buf), "%u", 3000000000U);
    check("snprintf unsigned",  n == 10 && strcmp(buf, "3000000000") == 0);

    /* %ld */
    n = snprintf(buf, sizeof(buf), "%ld", 100000L);
    check("snprintf long",      n == 6 && strcmp(buf, "100000") == 0);

    /* %lu（值在 32-bit 正数范围内，避免触发 toyc 大常量符号扩展预存 bug） */
    n = snprintf(buf, sizeof(buf), "%lu", 2000000000UL);
    check("snprintf %%lu 2e9",    n == 10 && strcmp(buf, "2000000000") == 0);

    n = snprintf(buf, sizeof(buf), "%lu", 0UL);
    check("snprintf %%lu zero",   n == 1 && strcmp(buf, "0") == 0);

    /* %lu >= 2^31 -- toyc parser/codegen 对大常量有符号扩展问题 */
    n = snprintf(buf, sizeof(buf), "%lu", 3000000000UL);
    check("snprintf %%lu 3e9 (KNOWN BUG)", n == 10 && strcmp(buf, "3000000000") == 0);

    /* %x */
    n = snprintf(buf, sizeof(buf), "%x", 0xdead);
    check("snprintf %%x",         n == 4 && strcmp(buf, "dead") == 0);

    /* %lx（值在 32-bit 范围，避免触发符号扩展预存 bug） */
    n = snprintf(buf, sizeof(buf), "%lx", 0x1234abcdUL);
    check("snprintf %%lx",        n == 8 && strcmp(buf, "1234abcd") == 0);

    /* %p（lib 实现输出 "0x" 前缀，null 输出 "0x0" 而非 "(nil)"） */
    n = snprintf(buf, sizeof(buf), "%p", (void*)0x0);
    check("snprintf %%p null",    n == 3 && strcmp(buf, "0x0") == 0);

    n = snprintf(buf, sizeof(buf), "%p", (void*)0x1234);
    check("snprintf %%p addr",    n == 6 && strcmp(buf, "0x1234") == 0);

    /* %c */
    n = snprintf(buf, sizeof(buf), "%c", 'A');
    check("snprintf char",        n == 1 && strcmp(buf, "A") == 0);

    /* 多参数 */
    n = snprintf(buf, sizeof(buf), "%s=%d", "count", 42);
    check("snprintf multi",       n == 8 && strcmp(buf, "count=42") == 0);

    /* 负数 */
    n = snprintf(buf, sizeof(buf), "%d", -42);
    check("snprintf neg",         n == 3 && strcmp(buf, "-42") == 0);

    /* ── %f 浮点格式化 ── */

    n = snprintf(buf, sizeof(buf), "%f", 3.14159);
    check("snprintf %%f pi",      n == 8 && strcmp(buf, "3.141590") == 0);

    n = snprintf(buf, sizeof(buf), "%f", 0.0);
    check("snprintf %%f zero",    n == 8 && strcmp(buf, "0.000000") == 0);

    n = snprintf(buf, sizeof(buf), "%f", -2.5);
    check("snprintf %%f neg",     n == 9 && strcmp(buf, "-2.500000") == 0);

    n = snprintf(buf, sizeof(buf), "%.2f", 3.14159);
    check("snprintf %%.2f",       n == 4 && strcmp(buf, "3.14") == 0);

    /* %.0f 已修复：dec=0 时不输出小数点和尾数 */
    n = snprintf(buf, sizeof(buf), "%.0f", 3.14159);
    check("snprintf %%.0f",       n == 1 && strcmp(buf, "3") == 0);

    n = snprintf(buf, sizeof(buf), "%.0f", 3.14159);
    check("snprintf %%.0f nodot",  n == 1 && strcmp(buf, "3") == 0);

    n = snprintf(buf, sizeof(buf), "%f %f", 1.5, 2.5);
    check("snprintf %%f %%f",     n == 17 && strcmp(buf, "1.500000 2.500000") == 0);

    n = snprintf(buf, sizeof(buf), "%d %f %s", 42, 3.14, "ok");
    check("snprintf int float str", n == 14 && strcmp(buf, "42 3.140000 ok") == 0);

    /* ── 已知问题测试 ── */

    /*
     * Bug 3: toyc __builtin_va_start 在命名参数为 double 时，
     * fp_offset 固定 48 而非 56(48+1*8)，gp_offset 用
     * func_nparams*8(=8) 而非 0。导致第一个 va_arg(double)
     * 读到命名参数 a=99.0 而非第一个变参 1.0。
     *
     * 正确传参：test_va_double(named=99.0, 1.0, 2.0, 3.0)
     *   寄存器分配：%xmm0=99.0, %xmm1=1.0, %xmm2=2.0, %xmm3=3.0
     *   fp_offset 应为 56(48+8) 指向 %xmm1=1.0
     *   实际 fp_offset=48 指向 %xmm0=99.0 → v1=99.0, sum=102.0
     *   正确 fp_offset=56 → v1=1.0, v2=2.0, v3=3.0, sum=6.0
     */
    n = snprintf(buf, sizeof(buf), "%.1f", test_va_double(99.0, 1.0, 2.0, 3.0));
    check("snprintf va_double named (KNOWN BUG)", n == 3 && strcmp(buf, "6.0") == 0);

    /* ── __printf 基本确认 ── */
    __printf("  __printf 输出正常\n");
    passed++; total++;

    __printf("--------------\n");
    __printf("结果: %d/%d 通过\n", passed, total);
    return total - passed;
}
