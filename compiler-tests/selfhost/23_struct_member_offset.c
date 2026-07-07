// EXPECT: 0
// SELF_CONTAINED
// Test struct member access at offset 0 and non-zero offsets

static long sys_write(int fd, const char *buf, unsigned long len) {
    unsigned long ret;
    __asm__ __volatile__ ("syscall"
        : "=a"(ret) : "a"(1), "D"((long)fd), "S"((long)buf), "d"((long)len)
        : "rcx", "r11", "memory");
    return (long)ret;
}
static void sys_exit(int code) {
    __asm__ __volatile__ ("syscall"
        : : "a"((long)60), "D"((long)code)
        : "rcx", "r11", "memory");
    for (;;) ;
}

struct S1 { long a; int b; int c; };
struct S2 { char x; int y; long z; };
struct Inner { int x; int y; };
struct Outer { struct Inner i; long z; };

void __tlibc_start(void) {
    /* First member (offset 0) */
    {
        struct S1 s = {100, 200, 300};
        if (s.a != 100) sys_exit(1);
    }

    /* Non-zero offset members */
    {
        struct S1 s = {100, 200, 300};
        if (s.b != 200) sys_exit(2);
        if (s.c != 300) sys_exit(3);
    }

    /* Pointer access */
    {
        struct S1 s = {100, 200, 300};
        struct S1 *p = &s;
        if (p->a != 100) sys_exit(4);
        if (p->b != 200) sys_exit(5);
        if (p->c != 300) sys_exit(6);
    }

    /* Different element types */
    {
        struct S2 s = {'A', 42, 999};
        if (s.x != 'A') sys_exit(7);
        if (s.y != 42) sys_exit(8);
        if (s.z != 999) sys_exit(9);
    }

    /* Assign then read back */
    {
        struct S1 s;
        s.a = 111;
        s.b = 222;
        s.c = 333;
        if (s.a != 111) sys_exit(10);
        if (s.b != 222) sys_exit(11);
        if (s.c != 333) sys_exit(12);
    }

    /* Nested struct (chained member offset) */
    {
        struct Outer o;
        o.i.x = 10;
        o.i.y = 20;
        o.z = 30;
        if (o.i.x != 10) sys_exit(13);
        if (o.i.y != 20) sys_exit(14);
        if (o.z != 30) sys_exit(15);
    }

    sys_exit(0);
}
