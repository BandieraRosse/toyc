/* test_string.c — string.c 功能测试
 *
 * EXPECT: 0
 */

#include "core.h"
#include "string.h"

extern void __printf(const char *fmt, ...);

static int total, passed;

static void check(const char *name, int cond) {
    total++;
    if (cond) { passed++; __printf("  %s: PASS\n", name); }
    else      { __printf("  %s: FAIL\n", name); }
}

int main(void) {
    char buf[128];
    const char *s;
    int cmp;

    __printf("string.c 功能测试\n");
    __printf("----------------\n");

    /* ── strlen ── */
    check("strlen('')",      strlen("") == 0);
    check("strlen('a')",     strlen("a") == 1);
    check("strlen('hello')", strlen("hello") == 5);
    check("strlen('longer')", strlen("longer") == 6);

    /* ── strcpy ── */
    buf[0] = '\0';
    strcpy(buf, "hello");
    check("strcpy(len)",     strlen(buf) == 5);
    check("strcpy(content)", buf[0]=='h' && buf[1]=='e' && buf[4]=='o');
    check("strcpy(nul)",     buf[5] == '\0');

    /* ── strncpy ── */
    strncpy(buf, "hello", 3); buf[3] = '\0';
    check("strncpy(len=3)",  strlen(buf) == 3);
    check("strncpy(content)", buf[0]=='h' && buf[2]=='l');

    /* ── strcmp ── */
    check("strcmp(==)",      strcmp("abc", "abc") == 0);
    check("strcmp(<)",       strcmp("abc", "abd") < 0);
    check("strcmp(>)",       strcmp("abd", "abc") > 0);
    check("strcmp(empty)",   strcmp("", "") == 0);
    check("strcmp(to a)",    strcmp("a", "") > 0);

    /* ── strncmp ── */
    check("strncmp(==)",     strncmp("abc", "abc", 3) == 0);
    check("strncmp(<)",      strncmp("abc", "abd", 3) < 0);
    check("strncmp(n=0)",    strncmp("abc", "xyz", 0) == 0);

    /* ── strcat ── */
    strcpy(buf, "a"); strcat(buf, "b");
    check("strcat(len=2)",   strlen(buf) == 2);
    check("strcat(ab)",      buf[0]=='a' && buf[1]=='b' && buf[2]=='\0');

    /* ── strncat ── */
    strcpy(buf, "a"); strncat(buf, "bcd", 2);
    check("strncat(len=3)",  strlen(buf) == 3);
    check("strncat(abc)",    buf[0]=='a' && buf[1]=='b' && buf[2]=='c');

    /* ── strchr ── */
    s = strchr("hello", 'e');
    check("strchr(found)",   s != NULL && *s == 'e');
    s = strchr("hello", 'l');
    check("strchr(first)",   s != NULL && *s == 'l' && *(s+1) == 'l');
    check("strchr(not)",     strchr("abc", 'z') == NULL);
    check("strchr(nul)",     strchr("abc", '\0') != NULL);

    /* ── strrchr ── */
    s = strrchr("hello", 'l');
    check("strrchr(last)",   s != NULL && *s == 'l' && *(s+1) == 'o');
    s = strrchr("hello", 'e');
    check("strrchr(single)", s != NULL && *s == 'e');

    /* ── strstr ── */
    s = strstr("hello world", "world");
    check("strstr(found)",   s != NULL && *s == 'w');
    s = strstr("hello world", "lo");
    check("strstr(mid)",     s != NULL && *s == 'l');
    check("strstr(not)",     strstr("abc", "xyz") == NULL);
    check("strstr(empty)",   strstr("abc", "") != NULL);

    /* ── strspn ── */
    check("strspn(all)",     strspn("abc", "cba") == 3);
    check("strspn(partial)", strspn("aabbcc", "ab") == 4);
    check("strspn(none)",    strspn("xyz", "ab") == 0);

    /* ── strcspn ── */
    check("strcspn",         strcspn("hello", "aeiou") == 1);
    check("strcspn(none)",   strcspn("bcdfg", "aeiou") == 5);
    check("strcspn(first)",  strcspn("abc", "a") == 0);

    /* ── strpbrk ── */
    s = strpbrk("hello", "aeiou");
    check("strpbrk(vowel)",  s != NULL && *s == 'e');
    check("strpbrk(not)",    strpbrk("bcdfg", "aeiou") == NULL);

    /* ── memcpy ── */
    buf[0] = '\0';
    memcpy(buf, "test", 5);
    check("memcpy",          buf[0]=='t' && buf[3]=='t' && buf[4]=='\0');

    /* ── memcmp ── */
    check("memcmp(==)",      memcmp("abc", "abc", 3) == 0);
    check("memcmp(<)",       memcmp("abc", "abd", 3) < 0);
    check("memcmp(n=0)",     memcmp("abc", "xyz", 0) == 0);
    check("memcmp(>)",       memcmp("abd", "abc", 3) > 0);

    /* ── strtok / strtok_r ── */
    {
        char *save;
        char str[] = "a,b,c";
        s = strtok_r(str, ",", &save);
        check("strtok_r(1st)", s != NULL && strlen(s) == 1 && s[0] == 'a');
        s = strtok_r(NULL, ",", &save);
        check("strtok_r(2nd)", s != NULL && s[0] == 'b');
        s = strtok_r(NULL, ",", &save);
        check("strtok_r(3rd)", s != NULL && s[0] == 'c');
        s = strtok_r(NULL, ",", &save);
        check("strtok_r(end)", s == NULL);
    }

    /* ── strtol (在 string.c 中定义) ── */
    check("strtol(123)",     strtol("123", NULL, 10) == 123);
    check("strtol(-456)",    strtol("-456", NULL, 10) == -456);
    check("strtol(hex)",     strtol("ff", NULL, 16) == 255);
    check("strtol(0)",       strtol("0", NULL, 10) == 0);

    /* ── abs / labs ── */
    check("abs(5)",          abs(5) == 5);
    check("abs(-3)",         abs(-3) == 3);
    check("labs(-1000000)",  labs(-1000000L) == 1000000L);

    /* ── atoi / atol ── */
    check("atoi(42)",        atoi("42") == 42);
    check("atoi(-7)",        atoi("-7") == -7);
    check("atol(99999)",     atol("99999") == 99999L);

    /* ── strerror ── */
    s = strerror(0);
    check("strerror(0)",     s != NULL && strlen(s) > 0);

    /* ── __memset ── */
    {
        int i;
        for (i = 0; i < 16; i++) buf[i] = 0xFF;
        __memset(buf, 0xAB, 8);
        check("__memset(8)", buf[0]==0xAB && buf[7]==0xAB && buf[8]==0xFF);
    }

    /* ── __memmove (重叠) ── */
    {
        char mv[] = "abcdefgh";
        __memmove(mv + 2, mv, 4);
        check("__memmove", mv[0]=='a' && mv[2]=='a' && mv[3]=='b');
    }

    __printf("----------------\n");
    __printf("结果: %d/%d 通过\n", passed, total);
    return total - passed;
}
