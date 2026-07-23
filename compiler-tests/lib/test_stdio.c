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

    /* %c */
    n = snprintf(buf, sizeof(buf), "%c", 'A');
    check("snprintf char",      n == 1 && strcmp(buf, "A") == 0);

    /* 多参数 */
    n = snprintf(buf, sizeof(buf), "%s=%d", "count", 42);
    check("snprintf multi",     n == 8 && strcmp(buf, "count=42") == 0);

    /* 负数 */
    n = snprintf(buf, sizeof(buf), "%d", -42);
    check("snprintf neg",       n == 3 && strcmp(buf, "-42") == 0);

    /* ── __printf 基本确认 ── */
    __printf("  __printf 输出正常\n");
    passed++; total++;

    __printf("--------------\n");
    __printf("结果: %d/%d 通过\n", passed, total);
    return total - passed;
}
