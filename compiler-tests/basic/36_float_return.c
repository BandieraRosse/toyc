/* Test: float/double function return values
 * Tests that float and double return types produce correct results
 * across various calling patterns (identity, arithmetic, chain calls,
 * mixed parameters, conditional returns).
 * EXPECT: 0
 */

/* ─── float return ─── */

static float id_f(float x) {
    return x;
}

static float add_f(float a, float b) {
    return a + b;
}

static float mul_f(float a, float b) {
    return a * b;
}

static float div_f(float a, float b) {
    return a / b;
}

/* ─── double return ─── */

static double id_d(double x) {
    return x;
}

static double add_d(double a, double b) {
    return a + b;
}

static double mul_d(double a, double b) {
    return a * b;
}

static double div_d(double a, double b) {
    return a / b;
}

/* ─── mixed parameter types with float/double return ─── */

static float mixed_ret_f(int n, float x) {
    return x + (float)n;
}

static double mixed_ret_d(int n, double x) {
    return x + (double)n;
}

static double triple_param(int a, double b, int c) {
    return b + (double)(a + c);
}

/* ─── chain calls ─── */

static float chain_f(float x) {
    return x * 2.0f;
}

static float chain_g(float x) {
    return x + 3.0f;
}

/* ─── conditional return ─── */

static float cond_ret_f(int sel, float a, float b) {
    if (sel) return a;
    return b;
}

static double cond_ret_d(int sel, double a, double b) {
    if (sel) return a;
    return b;
}

/* ─── complex expression return ─── */

static float expr_f(float a, float b, float c) {
    return a * b + c;
}

static double expr_d(double a, double b, double c) {
    return a * b + c;
}

int main(void) {
    /* ─── float identity ─── */
    {
        float r = id_f(3.14f);
        if ((int)(r * 100) != 314) return 1;
    }

    /* ─── float arithmetic return ─── */
    {
        float r = add_f(1.5f, 2.5f);
        if ((int)(r) != 4) return 2;
    }
    {
        float r = mul_f(3.0f, 4.0f);
        if ((int)(r) != 12) return 3;
    }
    {
        float r = div_f(10.0f, 4.0f);
        if ((int)(r) != 2) return 4;
    }

    /* ─── double identity ─── */
    {
        double r = id_d(2.71);
        if ((int)(r * 100) != 271) return 5;
    }

    /* ─── double arithmetic return ─── */
    {
        double r = add_d(1.5, 2.5);
        if ((int)(r) != 4) return 6;
    }
    {
        double r = mul_d(3.0, 4.0);
        if ((int)(r) != 12) return 7;
    }
    {
        double r = div_d(10.0, 4.0);
        if ((int)(r) != 2) return 8;
    }

    /* ─── float chain calls ─── */
    {
        /* chain_f(chain_g(1.0f)) = (1.0f + 3.0f) * 2.0f = 8.0f */
        float r = chain_f(chain_g(1.0f));
        if ((int)(r) != 8) return 9;
    }

    /* ─── double chain calls ─── */
    {
        /* chain_f-like: mul after add */
        double r = mul_d(add_d(1.0, 2.0), 2.0);
        if ((int)(r) != 6) return 10;
    }

    /* ─── mixed parameters, float return ─── */
    {
        float r = mixed_ret_f(10, 5.5f);
        if ((int)(r) != 15) return 11;
    }

    /* ─── mixed parameters, double return ─── */
    {
        double r = mixed_ret_d(10, 5.5);
        if ((int)(r) != 15) return 12;
    }

    /* ─── triple_param (int, double, int) → double ─── */
    {
        double r = triple_param(3, 10.0, 4);
        if ((int)(r) != 17) return 13;
    }

    /* ─── conditional return float ─── */
    {
        float r = cond_ret_f(1, 1.5f, 9.9f);
        if ((int)(r * 10) != 15) return 14;
    }
    {
        float r = cond_ret_f(0, 1.5f, 9.9f);
        if ((int)(r * 10) != 99) return 15;
    }

    /* ─── conditional return double ─── */
    {
        double r = cond_ret_d(1, 2.5, 8.8);
        if ((int)(r * 10) != 25) return 16;
    }
    {
        double r = cond_ret_d(0, 2.5, 8.8);
        if ((int)(r * 10) != 88) return 17;
    }

    /* ─── complex expression return ─── */
    {
        float r = expr_f(2.0f, 3.0f, 1.0f);  /* 2*3+1 = 7 */
        if ((int)(r) != 7) return 18;
    }
    {
        double r = expr_d(2.0, 3.0, 1.0);    /* 2*3+1 = 7 */
        if ((int)(r) != 7) return 19;
    }

    /* ─── return value used as array index (int cast) ─── */
    {
        int vals[10] = {0};
        int idx = (int)id_f(5.0f);
        vals[idx] = 42;
        if (vals[5] != 42) return 20;
    }

    /* ─── nested: return of a mixed arithmetic result ─── */
    {
        double r = add_d(mul_d(2.0, 3.0), div_d(10.0, 2.0));  /* 6 + 5 = 11 */
        if ((int)(r) != 11) return 21;
    }

    return 0;
}
