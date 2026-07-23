/* Test: __builtin_huge_val / __builtin_huge_valf
 *
 * 验证 tcc 对 __builtin_huge_val() 和 __builtin_huge_valf() 的支持。
 * 两者分别产生 IEEE 754 双精度和单精度正无穷常量。
 *
 * 手动解析位模式验证（不使用 %f，因为运行时的 printf
 * 在无穷大格式上有已知限制）。
 *
 * IEEE 754 格式：
 *   double (64-bit): [1 sign][11 exponent][52 mantissa]
 *     +inf: sign=0, exponent=0x7FF, mantissa=0
 *   float (32-bit):  [1 sign][8 exponent][23 mantissa]
 *     +inff: sign=0, exponent=0xFF, mantissa=0
 *
 * 注意：所有测试写在同一函数 main 中，以验证 tcc 在大量
 * float/double 局部变量（超过 128 字节栈帧）时的内存访问正确性。
 *
 * EXPECT: 0
 */

static int exp_bits_double(unsigned long bits) {
    return (int)((bits >> 52) & 0x7FFUL);
}

static int exp_bits_float(unsigned int bits) {
    return (int)((bits >> 23) & 0xFFU);
}

int main(void) {
    /* ─── Test 1: double +inf 位模式 ─── */
    {
        double inf = __builtin_huge_val();
        unsigned long *p = (unsigned long *)&inf;

        if (inf < 0.0) return 1;
        if (inf > 0.0) ; else return 2;

        if (exp_bits_double(*p) != 0x7FF) return 3;
        if ((*p & ((1UL << 52) - 1)) != 0) return 4;
    }

    /* ─── Test 2: float +inff 位模式 ─── */
    {
        float valf = __builtin_huge_valf();
        unsigned int *p = (unsigned int *)&valf;

        if (valf < 0.0f) return 5;
        if (exp_bits_float(*p) != 0xFF) return 6;
        if ((*p & ((1U << 23) - 1)) != 0) return 7;
    }

    /* ─── Test 3: -inf 保留无穷大位模式，仅翻转符号位 ─── */
    {
        double inf = __builtin_huge_val();
        double neg = -inf;
        unsigned long *p = (unsigned long *)&neg;

        if (*p >> 63) ; else return 8;
        if (exp_bits_double(*p) != 0x7FF) return 9;
        if (neg < 0.0) ; else return 10;
    }

    /* ─── Test 4: double 自等性 ─── */
    {
        double a = __builtin_huge_val();
        double b = __builtin_huge_val();

        if (a != b) return 11;
        if (a == a) ; else return 12;
    }

    /* ─── Test 5: float 自等性（多个 float 局部变量）─── */
    {
        float a = __builtin_huge_valf();
        float b = __builtin_huge_valf();
        if (a != b) return 13;
    }

    /* ─── Test 6: inf > DBL_MAX ─── */
    {
        double inf = __builtin_huge_val();
        double huge = 1.7976931348623157e+308;
        if (!(inf > huge)) return 14;
    }

    /* ─── Test 7: double 赋值传递 ─── */
    {
        double rv7 = __builtin_huge_val();
        {
            double x = rv7;
            unsigned long *q = (unsigned long *)&x;
            if (exp_bits_double(*q) != 0x7FF) return 15;
            if ((*q & ((1UL << 52) - 1)) != 0) return 16;
        }
    }

    /* ─── Test 8: float 赋值传递（和上面大量 float/double
     * 变量共存，验证栈偏移超过 disp8 范围时的正确性）─── */
    {
        float rv8 = __builtin_huge_valf();
        {
            float x = rv8;
            unsigned int *q = (unsigned int *)&x;
            if (((*q >> 23) & 0xFFU) != 0xFFU) return 17;
            if ((*q & ((1U << 23) - 1)) != 0) return 18;
        }
    }

    return 0;
}
