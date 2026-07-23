// EXPECT: 0
// SELF_CONTAINED
// cast_deref_assign.c — 验证 *(cast_type *)ptr = val 的代码生成
//
// 背景：toyc 曾将 *(long *)ptr = val 生成为 8 位存储（mov %al, (%rcx)），
// 因为解析器抛弃了强转类型信息，代码生成器用 void* 变量的 element_size(1)
// 决定存储宽度。
//
// 修复：解析器在 (type *) 强转时将 target_type_size 传播到 inner 节点的
// elem_size 字段；代码生成器在 *ptr = expr 路径中检查该注解。

static long sys_write(int fd, const char *buf, unsigned long len)
{
    unsigned long ret;
    __asm__ __volatile__ ("syscall"
        : "=a"(ret)
        : "a"(1), "D"((long)fd), "S"((long)buf), "d"((long)len)
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

/* 测试缓冲区 */
static unsigned long test_buf[16];

void __tlibc_start(void)
{
    /* 清零 */
    test_buf[0] = 0;
    test_buf[1] = 0;

    /* === 测试 1: *(long *)void_ptr = val — 8 字节存储 === */
    *(long *)(void *)test_buf = 0x1234567890ABCDEFL;
    if (*(unsigned long *)test_buf != 0x1234567890ABCDEFUL) {
        sys_write(1, "FAIL: *(long*)void_p\n", 21);
        sys_exit(1);
    }

    /* === 测试 2: *(unsigned long *)void_ptr = -1 — 8 字节全部 FF === */
    *(unsigned long *)(void *)test_buf = -1UL;
    if (*(unsigned long *)test_buf != 0xFFFFFFFFFFFFFFFFUL) {
        sys_write(1, "FAIL: *(ulong*)void_p = -1\n", 27);
        sys_exit(1);
    }

    /* === 测试 3: *(int *)void_ptr = val — 4 字节存储 === */
    test_buf[0] = 0;
    *(int *)(void *)test_buf = 0xDEADBEEF;
    if (*(unsigned int *)test_buf != 0xDEADBEEFU) {
        sys_write(1, "FAIL: *(int*)void_p\n", 20);
        sys_exit(1);
    }

    /* === 测试 4: *(unsigned short *)void_ptr = val — 2 字节 === */
    test_buf[0] = 0;
    *(unsigned short *)(void *)test_buf = 0xABCDU;
    if ((*(unsigned int *)test_buf & 0xFFFF) != 0xABCDU) {
        sys_write(1, "FAIL: *(ushort*)void_p\n", 22);
        sys_exit(1);
    }

    /* 高 16 位未动 */
    if ((*(unsigned int *)test_buf >> 16) != 0) {
        sys_write(1, "FAIL: *(ushort*) overflow\n", 26);
        sys_exit(1);
    }

    /* === 测试 5: *(short *)void_ptr = -1 — 2 字节 FF === */
    test_buf[0] = 0;
    *(short *)(void *)test_buf = -1;
    if ((*(unsigned int *)test_buf & 0xFFFF) != 0xFFFFU) {
        sys_write(1, "FAIL: *(short*) = -1\n", 20);
        sys_exit(1);
    }

    /* === 测试 6: *(unsigned char *)void_ptr = val — 1 字节 === */
    test_buf[0] = 0;
    *(unsigned char *)(void *)test_buf = 0x42U;
    if ((*(unsigned int *)test_buf & 0xFF) != 0x42U) {
        sys_write(1, "FAIL: *(uchar*)void_p\n", 21);
        sys_exit(1);
    }

    /* === 测试 7: *(signed char *)void_ptr = -1 — 1 字节 FF === */
    test_buf[0] = 0;
    *(signed char *)(void *)test_buf = -1;
    if ((*(unsigned int *)test_buf & 0xFF) != 0xFFU) {
        sys_write(1, "FAIL: *(schar*) = -1\n", 20);
        sys_exit(1);
    }

    /* === 测试 8: 已声明类型的指针不变 === */
    long *lp = (long *)test_buf;
    *lp = 0xABCDABCDABCDABCDL;
    if (*lp != 0xABCDABCDABCDABCDL) {
        sys_write(1, "FAIL: typed long ptr\n", 21);
        sys_exit(1);
    }

    sys_write(1, "OK\n", 3);
    sys_exit(0);
}
