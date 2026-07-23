/* test_ctype.c — ctype.c 功能测试
 *
 * EXPECT: 0
 */

#include "core.h"
#include "ctype.h"

extern void __printf(const char *fmt, ...);

static int total, passed;

static void check(const char *name, int cond) {
    total++;
    if (cond) { passed++; __printf("  %s: PASS\n", name); }
    else      { __printf("  %s: FAIL\n", name); }
}

int main(void) {
    __printf("ctype.c 功能测试\n");
    __printf("---------------\n");

    /* ── isalpha ── */
    check("isalpha('a')",     isalpha('a'));
    check("isalpha('Z')",     isalpha('Z'));
    check("isalpha('m')",     isalpha('m'));
    check("!isalpha('0')",   !isalpha('0'));
    check("!isalpha('@')",   !isalpha('@'));
    check("!isalpha(' ')",   !isalpha(' '));

    /* ── isdigit ── */
    check("isdigit('0')",     isdigit('0'));
    check("isdigit('9')",     isdigit('9'));
    check("isdigit('5')",     isdigit('5'));
    check("!isdigit('a')",   !isdigit('a'));

    /* ── isalnum ── */
    check("isalnum('a')",     isalnum('a'));
    check("isalnum('Z')",     isalnum('Z'));
    check("isalnum('5')",     isalnum('5'));
    check("!isalnum(' ')",   !isalnum(' '));
    check("!isalnum('.')",   !isalnum('.'));

    /* ── isspace ── */
    check("isspace(' ')",     isspace(' '));
    check("isspace('\\t')",   isspace('\t'));
    check("isspace('\\n')",   isspace('\n'));
    check("!isspace('a')",   !isspace('a'));
    check("!isspace('5')",   !isspace('5'));

    /* ── isprint ── */
    check("isprint(' ')",     isprint(' '));
    check("isprint('~')",     isprint('~'));
    check("!isprint(0)",     !isprint(0));
    check("!isprint(0x1F)",  !isprint(0x1F));

    /* ── isgraph ── */
    check("isgraph('!')",     isgraph('!'));
    check("isgraph('~')",     isgraph('~'));
    check("!isgraph(' ')",   !isgraph(' '));

    /* ── isxdigit ── */
    check("isxdigit('0')",    isxdigit('0'));
    check("isxdigit('f')",    isxdigit('f'));
    check("isxdigit('F')",    isxdigit('F'));
    check("!isxdigit('g')",  !isxdigit('g'));
    check("!isxdigit(' ')",  !isxdigit(' '));

    /* ── toupper ── */
    check("toupper('a')==A",  toupper('a') == 'A');
    check("toupper('z')==Z",  toupper('z') == 'Z');
    check("toupper('A')==A",  toupper('A') == 'A');
    check("toupper('5')==5",  toupper('5') == '5');

    /* ── tolower ── */
    check("tolower('A')==a",  tolower('A') == 'a');
    check("tolower('Z')==z",  tolower('Z') == 'z');
    check("tolower('a')==a",  tolower('a') == 'a');
    check("tolower('5')==5",  tolower('5') == '5');

    __printf("---------------\n");
    __printf("结果: %d/%d 通过\n", passed, total);
    return total - passed;
}
