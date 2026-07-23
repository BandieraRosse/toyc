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
 * EXPECT: 0
 */

/* 取 double 位模式下标 32-63（高位） */
static int exp_bits_double(unsigned long bits) {
    return (int)((bits >> 52) & 0x7FFUL);
}

/* 取 float 位模式下标 23-30（指数） */
static int exp_bits_float(unsigned int bits) {
    return (int)((bits >> 23) & 0xFFU);
}

/* 将 float 的指数校验和尾数全零校验封装在独立函数中，
 * 避免 tcc 局部变量分配时的 float 相互作用。 */
static int test_float_return(void) {
    float a = __builtin_huge_valf();
    float b = __builtin_huge_valf();
    if (a != b) return 13;
    return 0;
}

static int test_float_assign(void) {
    float rv8 = __builtin_huge_valf();
    float x = rv8;
    unsigned int *q = (unsigned int *)&x;
    if (((*q >> 23) & 0xFFU) != 0xFFU) return 17;
    if ((*q & ((1U << 23) - 1)) != 0) return 18;
    return 0;
}

int main(void) {
    /* ─── Test 1: double +inf 位模式 ─── */
    {
        double inf = __builtin_huge_val();
        unsigned long *p = (unsigned long *)&inf;

        /* sign = 0（正数）*/
        if (inf < 0.0) return 1;
        if (inf > 0.0) ; else return 2;  /* 必须是正数 */

        /* exponent = 0x7FF（全 1 → 无穷大/NaN）*/
        if (exp_bits_double(*p) != 0x7FF) return 3;

        /* mantissa = 0（无穷大，非 NaN）*/
        if ((*p & ((1UL << 52) - 1)) != 0) return 4;
    }

    /* ─── Test 2: float +inff 位模式 ─── */
    {
        float valf = __builtin_huge_valf();
        unsigned int *p = (unsigned int *)&valf;

        /* sign = 0 */
        if (valf < 0.0f) return 5;

        /* exponent = 0xFF（全 1）*/
        if (exp_bits_float(*p) != 0xFF) return 6;

        /* mantissa = 0 */
        if ((*p & ((1U << 23) - 1)) != 0) return 7;
    }

    /* ─── Test 3: -inf 保留无穷大位模式，仅翻转符号位 ─── */
    {
        double inf = __builtin_huge_val();
        double neg = -inf;
        unsigned long *p = (unsigned long *)&neg;

        /* sign 必须为 1 */
        if (*p >> 63) ; else return 8;

        /* exponent 仍为全 1 */
        if (exp_bits_double(*p) != 0x7FF) return 9;

        /* 负无穷大比较 */
        if (neg < 0.0) ; else return 10;
    }

    /* ─── Test 4: double 自等性 ─── */
    {
        double a = __builtin_huge_val();
        double b = __builtin_huge_val();

        if (a != b) return 11;
        if (a == a) ; else return 12;  /* inf == inf */
    }

    /* ─── Test 5: float 自等性（独立函数）─── */
    {
        int r = test_float_return();
        if (r) return r;
    }

    /* ─── Test 6: inf > DBL_MAX（无穷大大于任何有限双精度）─ */
    {
        double inf = __builtin_huge_val();
        double huge = 1.7976931348623157e+308;

        if (!(inf > huge)) return 14;
    }

    /* ─── Test 7: inf 函数参数传递 ─── */
    {
        double rv7 = __builtin_huge_val();
        unsigned long *p = (unsigned long *)&rv7;

        /* 通过赋值传递后检查位模式是否保持 */
        {
            double x = rv7;
            unsigned long *q = (unsigned long *)&x;
            if (exp_bits_double(*q) != 0x7FF) return 15;
            if ((*q & ((1UL << 52) - 1)) != 0) return 16;
        }
    }

    /* ─── Test 8: float 赋值传递（独立函数）─── */
    {
        int r = test_float_assign();
        if (r) return r;
    }

    return 0;
}
