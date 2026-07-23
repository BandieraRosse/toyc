/* test_misc.c — misc 子系统功能测试
 * 测试 file.c（文件操作）、path.c（路径解析）
 *
 * 注意：toyc 有 2D 数组 bug + string array relocation bug，
 * 且部分 misc 函数依赖 mkdirat/openat 宏（在 tlibc_compat.h 中），
 * 测试驱动使用 core.h 的 __openat/__mkdirat 等 __ 版本。
 *
 * EXPECT: 0
 */

#include "core.h"
#include "string.h"

extern void __printf(const char *fmt, ...);
extern int snprintf(char *str, unsigned long size, const char *format, ...);

/* 手动声明 misc 函数（tlibc_everything.h 有但测试不包含它） */
int tlibc_get_file_len(char *path);
int tlibc_is_path_dir(const char *path);
int tlibc_is_path_file(const char *path);
int tlibc_rm_file(const char *path);
int tlibc_get_file_count(const char *dir_path);
int tlibc_get_dir_count(const char *dir_path);
void tlibc_cal_absolute_path(const char *path, const char *cwd,
                             char *absolute_path, size_t max_len);

static int total, passed;

static void check(const char *name, int cond) {
    total++;
    if (cond) { passed++; __printf("  %s: PASS\n", name); }
    else      { __printf("  %s: FAIL\n", name); }
}

int main(void)
{
    char path[256];
    char abs[256];
    int fd, n, ret;

    __printf("misc 功能测试\n");
    __printf("-------------\n");

    /* ── tlibc_cal_absolute_path（纯字符串逻辑，无需 I/O） ── */
    tlibc_cal_absolute_path("/usr/bin/ls", "/home/user", abs, sizeof(abs));
    check("abs: already absolute", strcmp(abs, "/usr/bin/ls") == 0);

    tlibc_cal_absolute_path("file.txt", "/home/user", abs, sizeof(abs));
    check("abs: relative", strcmp(abs, "/home/user/file.txt") == 0);

    tlibc_cal_absolute_path("../../etc/passwd", "/home/user/sub",
                            abs, sizeof(abs));
    check("abs: ../../etc/passwd",
          strcmp(abs, "/home/etc/passwd") == 0);

    tlibc_cal_absolute_path(".", "/tmp", abs, sizeof(abs));
    check("abs: .", strcmp(abs, "/tmp") == 0);

    tlibc_cal_absolute_path("foo/../bar", "/home", abs, sizeof(abs));
    check("abs: foo/../bar", strcmp(abs, "/home/bar") == 0);

    /* ── tlibc_is_path_dir / tlibc_is_path_file（已知路径） ── */
    check("is_path_dir(/tmp) == 1", tlibc_is_path_dir("/tmp") == 1);
    /* /dev/null is a char device, not a regular file; test that it exists but is not regular */
    check("is_path_file(/dev/null) == 0 (not reg)", tlibc_is_path_file("/dev/null") == 0);
    /* /etc/passwd should be a regular file */
    check("is_path_file(/etc/passwd) == 1", tlibc_is_path_file("/etc/passwd") == 1);
    check("is_path_dir(/nonexistent_xxxx) == -1",
          tlibc_is_path_dir("/nonexistent_xxxx") == -1);
    check("is_path_file(/nonexistent_xxxx) == -1",
          tlibc_is_path_file("/nonexistent_xxxx") == -1);

    /* ── 创建临时测试目录和文件 ── */
    /* 使用 core.h 的 __ 版本 syscall 包装 */
    __mkdirat(AT_FDCWD, "tmp", 0755);
    __mkdirat(AT_FDCWD, "tmp/libt_misc_test", 0755);

    /* ── tlibc_get_file_len ── */
    snprintf(path, sizeof(path), "tmp/libt_misc_test/testfile.txt");
    fd = __openat(AT_FDCWD, path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    check("open testfile", fd >= 0);
    if (fd >= 0) {
        n = __write(fd, "Hello World!\n", 13);
        check("write 13 bytes", n == 13);
        __close(fd);

        n = tlibc_get_file_len(path);
        check("get_file_len == 13", n == 13);
    }

    /* ── is_path_file on created file ── */
    check("is_path_file on testfile", tlibc_is_path_file(path) == 1);

    /* ── rm_file ── */
    check("rm_file success", tlibc_rm_file(path) == 0);
    check("file gone", tlibc_is_path_file(path) == -1);

    /* ── get_file_len on nonexistent ── */
    n = tlibc_get_file_len("/nonexistent_path_xxxxx");
    check("get_file_len nonexistent == -1", n == -1);

    /* ── tlibc_get_file_count / tlibc_get_dir_count（getdents64 模式） ── */
    n = tlibc_get_file_count(".");
    check("get_file_count(.) > 0", n > 0);

    n = tlibc_get_dir_count(".");
    check("get_dir_count(.) >= 0", n >= 0);

    /* ── 清理 ── */
    __unlinkat(AT_FDCWD, "tmp/libt_misc_test", AT_REMOVEDIR);

    __printf("-------------\n");
    __printf("结果: %d/%d 通过\n", passed, total);
    return total - passed;
}
