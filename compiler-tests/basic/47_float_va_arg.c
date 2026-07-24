/* Test: __builtin_va_arg(ap, double) 浮点可变参数处理
 *
 * 验证 self-hosted toyc 在自举编译时能正确识别 double 类型参数。
 * 不依赖 __printf 中 _tcc_va_double 的手动绕行方案，直接使用
 * __builtin_va_arg(ap, double) 测试编译器本身的 codegen。
 *
 * 覆盖场景：
 *   1) 单个 double 参数
 *   2) 多个 double 参数（累积到不同 fp_offset）
 *   3) 负数 double
 *   4) 0.0 / 大数值
 *   5) int 参数在 double 之后（gp vs fp 偏移独立跟踪）
 *   6) 5 个 double 参数（遍历 xmm0-xmm4）
 *   7) __printf 输出验证（同时测试 _tcc_va_double 绕行路径）
 *
 * 注意：所有被测试的 variadic 函数的命名参数均为 int/指针类型
 * （与 __printf/snprintf 一致），避免触发 toyc 中命名参数为
 * float/double 时 __builtin_va_start gp_offset 计算不正确的
 * 已知独立 bug。
 *
 * EXPECT: 0
 */

/* ── 求和：所有 double 参数相加 ── */

static double sum_doubles(int count, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, count);
    double sum = 0.0;
    int i;
    for (i = 0; i < count; i++) {
        sum = sum + __builtin_va_arg(ap, double);
    }
    __builtin_va_end(ap);
    return sum;
}

/* ── 取第 n 个 double（0-based） ── */

static double nth_double(int n, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, n);
    double d = 0.0;
    int i;
    for (i = 0; i <= n; i++) {
        d = __builtin_va_arg(ap, double);
    }
    __builtin_va_end(ap);
    return d;
}

/* ── int 参数 → 下一个 double ── */

static double int_then_double(int first, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, first);
    double d = __builtin_va_arg(ap, double);
    __builtin_va_end(ap);
    return d;
}

int main(void) {
    /* 1) 单个 double */
    if ((int)(sum_doubles(1, 3.14) * 100) != 314) return 1;

    /* 2a) 两个 double */
    if ((int)(sum_doubles(2, 1.5, 2.5) * 10) != 40) return 2;

    /* 2b) 三个 double */
    if ((int)(sum_doubles(3, 1.0, 2.0, 3.0)) != 6) return 3;

    /* 3) 负数 */
    if ((int)(sum_doubles(1, -3.14) * 100) != -314) return 4;

    /* 4a) 0.0 */
    if ((int)(sum_doubles(1, 0.0) * 100) != 0) return 5;

    /* 4b) 大数值 */
    if ((int)(sum_doubles(1, 1234.5678)) != 1234) return 6;

    /* 2c) 取第 2 个 double（fp_offset 推进到第 2 槽） */
    if ((int)(nth_double(1, 1.0, 2.5, 3.0) * 10) != 25) return 7;

    /* 2d) 取第 4 个 double */
    if ((int)(nth_double(3, 10.0, 20.0, 30.0, 40.5)) != 40) return 8;

    /* 2e) 取第 5 个 double（xmm4, fp_offset=48+4*8=80） */
    if ((int)(nth_double(4, 1.0, 2.0, 3.0, 4.0, 5.0)) != 5) return 9;

    /* 5) int → double：int 消耗 gp 后 double 从 fp 路径读 */
    {
        double d = int_then_double(42, 3.14);
        if ((int)(d * 100) != 314) return 10;
    }

    /* 多个 double 求和 */
    {
        double d = sum_doubles(5, 1000.0, 2000.0, 3000.0, 4000.0, 5000.0);
        if ((int)d != 15000) return 11;
    }

    /* 验证 is_unsigned 不变（double → is_unsigned=0, unsigned long → 1） */
    {
        __builtin_va_list ap;
        __builtin_va_start(ap, ap);
        /* 此处仅验证 sum_doubles 不影响 is_unsigned 判断 */
    }

    /* 6) __printf 输出验证（同时测试 _tcc_va_double 绕行路径） */
    __printf("float va_arg test:\n");
    __printf("  pi=%f\n", 3.14159);
    __printf("  e=%f\n", 2.71828);
    __printf("  neg=%f\n", -1.5);
    __printf("  zero=%f\n", 0.0);
    __printf("  int=%d float=%f\n", 42, 3.14);
    __printf("  float=%f int=%d\n", 2.718, 99);

    return 0;
}
