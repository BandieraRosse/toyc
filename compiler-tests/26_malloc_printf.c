// EXPECT: 0
// malloc_printf.c — 测试 tlibc_malloc / __printf（自举关键：运行时功能验证）

typedef unsigned long size_t;

extern void  __printf(const char *fmt, ...);
extern void *tlibc_malloc(size_t size);
extern void  tlibc_free(void *ptr);
extern void *__memset(void *dst, int val, size_t n);

int main(void) {
    /* 1. 分配内存并检查非 NULL */
    char *buf = (char *)tlibc_malloc(64);
    if (buf == (void *)0) return 1;

    /* 2. 写入数据并读取验证 */
    buf[0] = 'H';
    buf[1] = 'i';
    buf[2] = '!';
    buf[3] = '\n';
    buf[4] = 0;
    if (buf[0] != 'H') return 2;
    if (buf[1] != 'i') return 3;
    if (buf[2] != '!') return 4;

    /* 3. 使用 __printf 输出（测试 __builtin_va_* 路径） */
    __printf("%s", buf);

    /* 4. 测试格式化输出：整数 */
    __printf("malloc test: alloc=%d bytes\n", 64);

    /* 5. 测试格式化输出：十六进制地址 */
    __printf("ptr=%p val=%s\n", buf, buf);
    /* 注意：tcc 的 __printf 可能不支持 %p；这里用 %x 演示 */
    __printf("ptr_low=%x val=%s\n", (unsigned int)(unsigned long)buf, buf);

    /* 6. 多次分配 */
    {
        int i;
        for (i = 0; i < 5; i = i + 1) {
            char *block = (char *)tlibc_malloc(16);
            if (block == (void *)0) return 10 + i;
            block[0] = 'A' + i;
            block[1] = 0;
            __printf("block[%d] = %s\n", i, block);
            tlibc_free(block);
        }
    }

    /* 7. 使用 memset 填充 */
    __memset(buf, 'A', 4);
    buf[4] = 0;
    if (buf[0] != 'A') return 20;
    if (buf[1] != 'A') return 21;
    __printf("memset ok: %s\n", buf);

    /* 8. 释放内存 */
    tlibc_free(buf);

    __printf("All malloc/printf tests passed!\n");
    return 0;
}
