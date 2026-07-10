/*
 * 测试：变参函数内 case 块中使用局部变量
 * 验证 __builtin_va_start/__builtin_va_arg
 * 在局部变量声明位于 case 块内时仍正确工作。
 * 这是之前 __eprintf 在 case 内用 char buf[32] 导致 crash 的复现。
 * 根因：cgen_expr.c 中 va_start 的 reg_save_area 偏移
 *      只用了 disp8 编码导致大帧 (< -128) 截断。
 * EXPECT: 0
 */

/* 自包含入口：__tlibc_start → main → sys_exit */
__attribute__((noreturn)) static void __exit(int code) {
    __asm__ __volatile__ ("mov %0, %%rdi; mov $60, %%rax; syscall" : : "r"((long)code) : "rdi", "rax", "rcx", "r11");
    for (;;);
}
__attribute__((noreturn)) void __tlibc_start(void) {
    __exit(main());
}

static int sum_va(int n, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    int total = 0;
    int i;
    for (i = 0; i < n; i++) {
        switch (i % 3) {
        case 0: {
            long v = __builtin_va_arg(ap, long);
            total += (int)v;
            break;
        }
        case 1: {
            long v = __builtin_va_arg(ap, long);
            total += (int)v * 2;
            break;
        }
        default: {
            long v = __builtin_va_arg(ap, long);
            total += (int)v * 3;
            break;
        }
        }
    }
    __builtin_va_end(ap);
    return total;
}

int main(void) {
    /* 1+2*2+3*3 + 4+5*2+6*3 = 1+4+9 + 4+10+18 = 46 */
    int r = sum_va(6, 1L, 2L, 3L, 4L, 5L, 6L);
    return r == 46 ? 0 : 1;
}
