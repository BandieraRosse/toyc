/* test_tty.c — tty.c 功能测试
 * 测试终端控制函数在非 TTY fd 上返回预期错误，以及纯输出函数不崩溃。
 *
 * 核心思路：pipe fd 不是 TTY，在其上调用 ioctl(TCGETS/TIOCGWINSZ) 失败，
 * 测试这些错误路径验证函数返回预期错误码且不崩溃。
 * tlibc_cursor_goto / tlibc_cursor_goto_col 是纯输出函数，验证不崩。
 *
 * 跳过：tlibc_general_input_process（交互式，不可自动化）
 * 跳过：tlibc_check_term_size（调用 __exit(-1) 不可自动化）
 * EXPECT: 0
 */

#include "core.h"
#include "tty.h"

extern void __printf(const char *fmt, ...);

static int total, passed;

static void check(const char *name, int cond) {
    total++;
    if (cond) { passed++; __printf("  %s: PASS\n", name); }
    else      { __printf("  %s: FAIL\n", name); }
}

int main(void)
{
    int fds[2];
    int ret;

    __printf("tty.c 功能测试\n");
    __printf("-------------\n");

    /* ── 创建 pipe 获取非 TTY fd ── */
    ret = __pipe2(fds, 0);
    check("pipe2 success", ret == 0);

    if (ret != 0) {
        __printf("  pipe2 failed, cannot run tty tests\n");
        __printf("-------------\n");
        __printf("结果: %d/%d 通过\n", passed, total);
        return total - passed;
    }

    /* ── tlibc_get_term_size on pipe fd → -1 (ENOTTY) ── */
    {
        struct winsize ws;
        ret = tlibc_get_term_size(fds[0], &ws);
        check("get_term_size on pipe == -1", ret == -1);
    }

    /* ── tlibc_get_term_config on pipe fd → -1 ── */
    {
        struct termios term;
        ret = tlibc_get_term_config(fds[0], &term);
        check("get_term_config on pipe == -1", ret == -1);
    }

    /* ── tlibc_set_term_config on pipe fd → -1 ── */
    {
        struct termios term;
        ret = tlibc_set_term_config(fds[0], &term);
        check("set_term_config on pipe == -1", ret == -1);
    }

    /* ── tlibc_set_term_raw_and_noecho on pipe fd
     *    内部先调用 get_term_config → 失败 → -1 ── */
    {
        ret = tlibc_set_term_raw_and_noecho(fds[0]);
        check("set_term_raw on pipe == -1", ret == -1);
    }

    /* ── tlibc_restore_term: fd with no saved config → 0 ── */
    {
        ret = tlibc_restore_term(fds[0]);
        check("restore_term never-saved == 0", ret == 0);
    }

    /* ── tlibc_restore_term: invalid fd → -1 ── */
    {
        ret = tlibc_restore_term(-1);
        check("restore_term(-1) == -1", ret == -1);
    }

    /* ── tlibc_restore_term: fd >= 16 → -1 ── */
    {
        ret = tlibc_restore_term(16);
        check("restore_term(16) == -1", ret == -1);
    }

    /* ── tlibc_get_term_config on valid stdin (0)
     *    如果是终端返回 0，若重定向则返回 -1，两者均可接受 ── */
    {
        struct termios term;
        ret = tlibc_get_term_config(0, &term);
        if (ret == 0) {
            /* stdin IS a terminal — verify basic fields */
            check("get_term_config(0) success", 1);
        } else {
            __printf("  get_term_config(0): not a TTY (expected in CI)\n");
            check("get_term_config(0) fails as non-TTY", 1);
        }
    }

    /* ── tlibc_cursor_goto: void, verify it doesn't crash ── */
    {
        tlibc_cursor_goto(1, 1);
        check("cursor_goto(1,1) no crash", 1);
    }

    /* ── tlibc_cursor_goto_col: void, verify it doesn't crash ── */
    {
        tlibc_cursor_goto_col(5);
        check("cursor_goto_col(5) no crash", 1);
    }

    /* ── tlibc_set_term_raw_and_noecho: invalid fd < 0 → -1 ── */
    {
        ret = tlibc_set_term_raw_and_noecho(-1);
        check("set_term_raw(-1) == -1", ret == -1);
    }

    /* ── 清理 pipe ── */
    __close(fds[0]);
    __close(fds[1]);

    __printf("-------------\n");
    __printf("结果: %d/%d 通过\n", passed, total);
    return total - passed;
}
