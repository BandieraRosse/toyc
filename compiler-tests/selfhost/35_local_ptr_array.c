// EXPECT: 0
// t03_local_ptr_array.c — Bug C: local pointer array crash
//
// toyc bug: declaring char *arr[] = {"a", "b", 0} inside a function
//          and accessing arr[i] causes segfault.

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

/* Minimal strlen (no libc) */
static int slen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

void __tlibc_start(void) {
    /* Test A: basic local pointer array */
    {
        char *arr[] = {"abc", "def", "ghi", 0};
        if (arr[0][0] != 'a') sys_exit(1);
        if (arr[1][2] != 'f') sys_exit(2);
        if (arr[3] != 0) sys_exit(3);
        if (slen(arr[1]) != 3) sys_exit(4);
    }

    /* Test B: char* array with 2 elements */
    {
        char *names[] = {"hello", "world"};
        if (names[0][0] != 'h') sys_exit(10);
        if (names[1][4] != 'd') sys_exit(11);
        if (slen(names[1]) != 5) sys_exit(12);
    }

    /* Test C: array of char* as toyc args */
    {
        char *a[] = {"program", "input.c", 0};
        if (a[0][0] != 'p') sys_exit(20);
        if (a[1][6] != 'c') sys_exit(21);  /* 'input.c'[6] = 'c' */
        if (a[2] != 0) sys_exit(22);
    }

    sys_exit(0);
}
