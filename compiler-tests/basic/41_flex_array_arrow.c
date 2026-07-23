/* Test: struct flexible array member access via ->
 * Regression test for toyc bug: -> member access path in parse.c
 * didn't propagate memb_is_array to the AST_MEMBER node, causing
 * d_name[] to be loaded as char (movsbl) instead of decaying to
 * pointer. Also tests small fixed-size array members via ->.
 *
 * Bug: TOK_ARROW path at parse.c:1024-1031 was missing
 *   n->is_array = st->members[fi].memb_is_array;
 * (same fix also applied to the fallback 2 path at ~1049)
 *
 * EXPECT: 0
 */

/* ─── Struct with flexible array member ─── */
struct flex {
    unsigned long a;
    unsigned long b;
    unsigned short c;
    unsigned char  d;
    char           name[];
};

/* ─── Struct with small fixed-size array member ─── */
struct small_arr {
    int   tag;
    short vals[2];
    char  buf[4];
};

/* ─── Struct with char array member ─── */
struct with_buf {
    int  len;
    char data[8];
};

int main(void) {
    /* ─── Test 1: flexible array member via -> pointer access ─── */
    {
        char buf[128];
        struct flex *p = (struct flex *)buf;

        /* Set up a fake entry */
        p->a = 42;
        p->b = 123;
        p->c = 24;
        p->d = 4;
        p->name[0] = 'H'; p->name[1] = 'e'; p->name[2] = 'l';
        p->name[3] = 'l'; p->name[4] = 'o'; p->name[5] = '\0';

        /* Read back through -> */
        if (p->a != 42) return 1;
        if (p->b != 123) return 2;
        if (p->c != 24) return 3;
        if (p->d != 4) return 4;

        /* Test d_name[0] subscript */
        if (p->name[0] != 'H') return 5;
        if (p->name[4] != 'o') return 6;

        /* Test *d_name dereference (array-to-pointer decay) */
        if (*p->name != 'H') return 7;

        /* Test pointer arithmetic */
        if (*(p->name + 3) != 'l') return 8;

        /* Test iteration pattern (like tlibc_list_pids) */
        int len = 0;
        for (char *cp = p->name; *cp; cp++) len++;
        if (len != 5) return 9;

        /* Test numeric name conversion pattern */
        buf[0] = 0; p = (struct flex *)buf;
        p->a = 100; p->b = 0; p->c = 24; p->d = 4;
        p->name[0] = '4'; p->name[1] = '2'; p->name[2] = '\0';
        long val = 0;
        for (char *cp = p->name; *cp; cp++)
            val = val * 10 + (*cp - '0');
        if (val != 42) return 10;

        /* Test through variable pointer */
        char *n = p->name;
        if (n[0] != '4') return 11;
    }

    /* ─── Test 2: small fixed-size array via -> ─── */
    {
        char buf[64];
        struct small_arr *p = (struct small_arr *)buf;
        p->tag = 999;
        p->vals[0] = 10;
        p->vals[1] = 20;
        p->buf[0] = 'x'; p->buf[1] = 'y'; p->buf[2] = 'z'; p->buf[3] = '\0';

        if (p->tag != 999) return 20;
        if (p->vals[0] != 10) return 21;
        if (p->vals[1] != 20) return 22;
        if (p->buf[0] != 'x') return 23;
        if (p->buf[2] != 'z') return 24;
    }

    /* ─── Test 3: char array member via -> ─── */
    {
        char buf[64];
        struct with_buf *p = (struct with_buf *)buf;
        p->len = 3;
        p->data[0] = 'a'; p->data[1] = 'b'; p->data[2] = 'c';

        if (p->len != 3) return 30;
        if (*p->data != 'a') return 31;
        if (*(p->data + 2) != 'c') return 32;

        /* Iteration pattern */
        char *dp = p->data;
        dp[0] = 'A';
        if (p->data[0] != 'A') return 33;
    }

    /* ─── Test 4: through . (dot) access with flexible array member ─── */
    {
        struct flex s;
        /* Access via . (dot) — tests that TOK_DOT path still works */
        s.a = 55;
        s.name[0] = 'd'; s.name[1] = 'o'; s.name[2] = 't'; s.name[3] = '\0';
        if (s.a != 55) return 40;
        if (s.name[0] != 'd') return 41;
        if (*(s.name + 2) != 't') return 42;

        /* Also check that -> works on a pointer to this local struct */
        struct flex *p = &s;
        if (p->name[1] != 'o') return 43;
        if (*p->name != 'd') return 44;
    }

    return 0;
}
