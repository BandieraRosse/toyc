// EXPECT: 0
// float_basic.c — 验证 float 和 double 基本运算
int main(void) {
    /* ── 基本 float 声明和赋值 ── */
    float a;
    a = 3.14f;
    /* int 转换验证整数值 */
    if ((int)(a * 100) != 314) return 1;

    /* ── float 算术 ── */
    float b = 1.5f;
    float c = 2.5f;
    if ((int)(b + c) != 4) return 2;   /* 1.5 + 2.5 = 4.0 */
    if ((int)(c - b) != 1) return 3;   /* 2.5 - 1.5 = 1.0 */
    if ((int)(b * 4.0f) != 6) return 4; /* 1.5 * 4 = 6.0 */
    if ((int)(10.0f / 4.0f) != 2) return 5; /* truncation */
    if ((int)(10.0f / 2.5f) != 4) return 6;

    /* ── 基本 double 声明和赋值 ── */
    double d = 3.14;
    if ((int)(d * 100) != 314) return 7;

    /* ── double 算术 ── */
    double e = 1.5, f = 2.5;
    if ((int)(e + f) != 4) return 8;
    if ((int)(f - e) != 1) return 9;
    if ((int)(e * 4.0) != 6) return 10;
    if ((int)(10.0 / 4.0) != 2) return 11;

    /* ── float 比较 ── */
    if (!(3.0f > 2.0f)) return 12;
    if (!(2.0f < 3.0f)) return 13;
    if (!(3.0f >= 3.0f)) return 14;
    if (!(3.0f <= 3.0f)) return 15;
    if (!(4.0f == 4.0f)) return 16;
    if (!(4.0f != 5.0f)) return 17;
    if (3.0f > 5.0f) return 18;
    if (5.0f < 3.0f) return 19;

    /* ── double 比较 ── */
    if (!(3.0 > 2.0)) return 20;
    if (!(3.0 == 3.0)) return 21;
    if (!(3.0 != 4.0)) return 22;
    if (2.0 > 5.0) return 23;

    /* ── int→float 转型 ── */
    float fi = (float)42;
    if ((int)fi != 42) return 24;
    fi = (float)-7;
    if ((int)fi != -7) return 25;

    /* ── float→int 转型 ── */
    int x = (int)(3.99f);
    if (x != 3) return 26;
    x = (int)(-3.99f);
    if (x != -3) return 27;

    /* ── double→int 转型 ── */
    x = (int)(3.99);
    if (x != 3) return 28;
    x = (int)(-3.99);
    if (x != -3) return 29;

    /* ── int→double 转型 ── */
    double di = (double)42;
    if ((int)di != 42) return 30;
    di = (double)-7;
    if ((int)di != -7) return 31;

    /* ── 求反 ── */
    float neg = -5.0f;
    if ((int)neg != -5) return 32;
    double negd = -5.0;
    if ((int)negd != -5) return 33;
    neg = -(-10.0f);
    if ((int)neg != 10) return 34;

    /* ── 逻辑非 ── */
    if (!(!0.0f)) return 35;
    if (!!1.0f != 1) return 36;
    if (!(!0.0)) return 37;
    if (!!1.0 != 1) return 38;

    /* ── 混合 int/float 表达式 ── */
    float mixed = 3.0f + 2;
    if ((int)mixed != 5) return 39;
    mixed = 3 + 2.0f;
    if ((int)mixed != 5) return 40;
    mixed = 10.0f / 3;
    if ((int)mixed != 3) return 41;  /* truncation */
    mixed = 10 / 3.0f;
    if ((int)mixed != 3) return 42;

    /* ── 混合 int/double 表达式 ── */
    double mixd = 3.0 + 2;
    if ((int)mixd != 5) return 43;
    mixd = 10.0 / 3;
    if ((int)mixd != 3) return 44;

    /* ── 三元表达式 ── */
    float tern = 1 ? 3.14f : 2.0f;
    if ((int)(tern * 100) != 314) return 45;
    tern = 0 ? 5.0f : 7.0f;
    if ((int)tern != 7) return 46;

    double ternd = 1 ? 3.14 : 2.0;
    if ((int)(ternd * 100) != 314) return 47;
    ternd = 0 ? 5.0 : 7.0;
    if ((int)ternd != 7) return 48;

    /* ── 复杂的 float 表达式 ── */
    float complex = (1.5f + 2.5f) * 4.0f - 3.0f;
    if ((int)complex != 13) return 49;  /* (4.0 * 4.0) - 3.0 = 13.0 */
    complex = ((10.0f / 3.0f) > 3.0f) ? 1.0f : 2.0f;
    if ((int)complex != 1) return 50;

    /* ── float 局部变量 ← float 表达式 ── */
    float v1 = 6.0f;
    float v2 = 7.0f;
    v1 = v1 + v2;
    if ((int)v1 != 13) return 51;
    v2 = v2 * 2.0f;
    if ((int)v2 != 14) return 52;

    /* ── float 赋值 int → float ── */
    float to_float;
    to_float = 99;
    if ((int)to_float != 99) return 53;

    /* ── double 赋值 int → double ── */
    double to_double;
    to_double = 99;
    if ((int)to_double != 99) return 54;

    /* ── 比较含负值 ── */
    if (!(-3.0f < 1.0f)) return 55;
    if (!(-3.0f > -5.0f)) return 56;
    if (!(-3.0 < 1.0)) return 57;

    return 0;
}
