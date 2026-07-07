// EXPECT: 0
// SELF_CONTAINED
// macro_const_direct.c — 直接使用宏常量比较（不经过变量）
// 验证 read_number 溢出时 is_unsigned 是否正确

static long sys_write(int fd, const char *buf, unsigned long len)
{
    unsigned long ret;
    __asm__ __volatile__ ("syscall"
        : "=a"(ret) : "a"(1), "D"((long)fd), "S"((long)buf), "d"((long)len)
        : "rcx", "r11", "memory");
    return (long)ret;
}
static void sys_exit(int code)
{
    __asm__ __volatile__ ("syscall"
        : : "a"((long)60), "D"((long)code)
        : "rcx", "r11", "memory");
    for (;;) ;
}

static int my_strlen(const char *s) {
    int n = 0; while (*s) { s++; n++; } return n;
}
static void print_str(const char *s) {
    sys_write(1, s, (unsigned long)my_strlen(s));
}
static void print_dec(long n) {
    char buf[32]; int i = 0;
    if (n < 0) { sys_write(1, "-", 1); n = -n; }
    if (n == 0) { sys_write(1, "0", 1); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) sys_write(1, &buf[--i], 1);
}
static void print_hex(unsigned long n) {
    const char *hex = "0123456789abcdef";
    char buf[19]; int i = 18;
    buf[18] = 0;
    if (n == 0) { sys_write(1, "0", 1); return; }
    while (n > 0) { buf[--i] = hex[n & 0xf]; n >>= 4; }
    sys_write(1, &buf[i], (unsigned long)(18 - i));
}

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { print_str("FAIL: "); print_str(msg); print_str("\n"); failures++; } } while (0)

void __tlibc_start(void)
{
    /* 测试 0: 基线 — 小值 U 后缀 */
    /* 0x7FFFFFFFU > 0U: is_unsigned 启发式失败，但值 < 2^31, signed=unsigned */
    CHECK(0x7FFFFFFFU > 0U, "0x7FFFFFFFU > 0U");
    CHECK((0x7FFFFFFFU >> 1) == 0x3FFFFFFFU, "0x7FFFFFFFU >> 1");
    CHECK(0xFFFFFFFFU > 0U, "0xFFFFFFFFU > 0U");
    CHECK((0xFFFFFFFFU >> 1) == 0x7FFFFFFFU, "0xFFFFFFFFU >> 1");

    /* 测试 1: 大值 UL 常量直接比较 — 可能触发 read_number 溢出 */
    /* 0x8000000000000000UL = 2^63，在 signed long 中溢出为 LONG_MIN */
    /* 如果 is_unsigned_binop = 0，会使用 signed 比较: -2^63 > 0 → false! */
    print_str("Test A: ");
    if (0x8000000000000000UL > 0UL) {
        print_str("OK (unsigned cmp)\n");
    } else {
        print_str("BUG! signed cmp\n");
        failures++;
    }

    /* 同上，通过变量包裹 — 应工作 */
    {
        unsigned long a = 0x8000000000000000UL;
        unsigned long z = 0UL;
        if (a > z) {
            print_str("Test B: OK (via var)\n");
        } else {
            print_str("BUG! via var failed\n");
            failures++;
        }
    }

    /* 测试 2: 大值 >> 1 — 逻辑右移 vs 算术右移 */
    /* 0x8000000000000000UL >> 1 应是 0x4000000000000000 (逻辑) */
    {
        unsigned long r1 = 0x8000000000000000UL >> 1;
        /* 位模式: 期望 0x4000000000000000 */
        /* 若算术右移: 0xC000000000000000 */
        if (r1 == 0x4000000000000000UL) {
            print_str("Test C: OK (logical shift via var)\n");
        } else {
            print_str("BUG! arithmetic shift via var\n");
            failures++;
        }
    }

    /* 直接常量 >> (不经过变量) */
    {
        /* 这里直接计算常量 >> 1，tcc 不会做编译期常量折叠 */
        unsigned long sr = 0x8000000000000000UL >> 1;
        if (sr == 0x4000000000000000UL) {
            print_str("Test D: OK (direct shift)\n");
        } else {
            print_hex(sr);
            print_str(": arithmetic shift (signed >>) on unsigned constant\n");
            failures++;
        }
    }

    /* 测试 3: UL 常量除法 */
    {
        unsigned long dq = 0x8000000000000000UL / 2UL;
        if (dq == 0x4000000000000000UL) {
            print_str("Test E: OK (direct div)\n");
        } else {
            print_hex(dq);
            print_str(": signed division on unsigned constant\n");
            failures++;
        }
    }

    /* 测试 4: UL 常量取模 */
    {
        unsigned long rm = 0x8000000000000003UL % 2UL;
        if (rm == 1UL) {
            print_str("Test F: OK (direct mod)\n");
        } else {
            print_dec(rm);
            print_str(": signed modulo on unsigned constant\n");
            failures++;
        }
    }

    print_str("\n");
    if (failures == 0) {
        print_str("ALL PASSED\n");
        sys_exit(0);
    }
    print_str("FAILURES: ");
    print_dec(failures);
    print_str("\n");
    sys_exit(failures);
}
