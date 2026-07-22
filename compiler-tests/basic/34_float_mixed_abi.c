/*
 * Test: Mixed float/int ABI — non-variadic functions (P2 GP counter fix)
 *
 * Tests that non-variadic functions with mixed (float, int) parameter
 * ordering work correctly after the GP counter fix in caller pop loop.
 *
 * EXPECT: 0
 */

/* (double, int) — FP first, GP second */
static int test_fi(double x, int y) {
    if ((int)(x + 0.5) != y) return 1;
    return 0;
}

/* (int, double, int) — GP, FP, GP */
static int test_if1(int a, double x, int b) {
    if ((int)x != b) return 2;
    if (a + b != 10) return 3;
    return 0;
}

/* (double, int, double) — FP, GP, FP */
static int test_fif(double x, int y, double z) {
    if ((int)(x * 10) != 15) return 4;
    if (y != 42) return 5;
    if ((int)(z * 100) != 314) return 6;
    return 0;
}

/* (int, double, int, double) — GP, FP, GP, FP */
static int test_ifif(int a, double x, int b, double y) {
    if (a + b != 3) return 7;
    if ((int)(x * 10) != 25) return 8;
    if ((int)(y * 10) != 35) return 9;
    return 0;
}

/* (double, double, int) — FP, FP, GP */
static double mult;
static int test_ffi(double x, double y, int z) {
    mult = x * y;
    if ((int)mult != 3) return 10;
    if (z != 99) return 11;
    return 0;
}

/* (float, int) — float (not double) */
static int test_ffi_f(float x, int y) {
    if ((int)(x * 10) != 15) return 12;
    if (y != 7) return 13;
    return 0;
}

int main(void) {
    if (test_fi(3.5, 4) != 0) return 1;
    if (test_if1(7, 3.0, 3) != 0) return 2;
    if (test_fif(1.5, 42, 3.14) != 0) return 3;
    if (test_ifif(1, 2.5, 2, 3.5) != 0) return 4;
    if (test_ffi(1.5, 2.0, 99) != 0) return 5;
    if (test_ffi_f(1.5f, 7) != 0) return 6;
    return 0;
}
