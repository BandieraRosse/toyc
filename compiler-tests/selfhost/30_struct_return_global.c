// EXPECT: 0
// t02_struct_return_global_corrupt.c — Bug B: global variable corruption
//   after struct-return function call
//
// toyc bug: code_size (or other globals) gets wrong value after
//          assigning struct return value to a local variable.
//
// This test uses a global variable `counter` to simulate code_size.

static void sys_exit(int code) {
    __asm__ __volatile__ ("syscall"
        : : "a"((long)60), "D"((long)code)
        : "rcx", "r11", "memory");
    for (;;) ;
}

static void print_str(const char *s) {
    int n = 0; while (s[n]) n++;
    __asm__ __volatile__ ("syscall"
        : : "a"(1), "D"(1), "S"(s), "d"((long)n)
        : "rcx", "r11", "memory");
}

static void print_dec(long n) {
    char buf[32]; int i = 0;
    if (n < 0) { print_str("-"); n = -n; }
    if (n == 0) { buf[i++] = '0'; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) {
        long ch = buf[--i];
        __asm__ __volatile__ ("syscall"
            : : "a"(1), "D"(1), "S"(&ch), "d"(1L)
            : "rcx", "r11", "memory");
    }
}

/* Global variable — simulates code_size/code_buf pattern */
static int counter = 0;

/* Large struct to force hidden pointer return */
struct Large {
    char data[64];
};

static struct Large make_large(int v) {
    struct Large l;
    int i;
    for (i = 0; i < 64; i++) l.data[i] = (char)v;
    return l;
}

/* Small struct that fits in register (no hidden pointer) */
struct Small {
    int val;
};

static struct Small make_small(int v) {
    struct Small s;
    s.val = v;
    return s;
}

void __tlibc_start(void) {
    int test_failed = 0;

    /* Test A: large struct return + global counter check */
    counter = 1234;
    {
        struct Large l = make_large(42);
        if (counter != 1234) {
            print_str("FAIL counter after large struct: "); print_dec(counter);
            print_str(" (expected 1234)\n");
            test_failed = 1;
        }
    }

    /* Test B: small struct return + global counter check */
    counter = 5678;
    {
        struct Small s = make_small(7);
        if (counter != 5678) {
            print_str("FAIL counter after small struct: "); print_dec(counter);
            print_str(" (expected 5678)\n");
            test_failed = 1;
        }
    }

    /* Test C: multiple struct returns in block scope */
    counter = 9999;
    {
        struct Large a = make_large(1);
        struct Large b = make_large(2);
        struct Large c = make_large(3);
        if (counter != 9999) {
            print_str("FAIL counter after 3 large structs: "); print_dec(counter);
            print_str(" (expected 9999)\n");
            test_failed = 1;
        }
    }

    if (test_failed) sys_exit(1);
    sys_exit(0);
}
