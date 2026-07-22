/*
 * Test: %f formatting in __printf (use tcc_rt)
 *
 * Tests that __printf supports %f format specifier.
 * Single-float calls test the basic va_arg double path.
 *
 * EXPECT: 0
 */

int main() {
    /* Basic integer formatting (should still work) */
    __printf("int=%d\n", 42);
    __printf("hex=%x\n", 255);

    /* Float formatting with common values */
    __printf("f0=%f\n", 0.0);
    __printf("f1=%f\n", 1.0);
    __printf("f2=%f\n", 3.14159);
    __printf("f3=%f\n", -2.5);
    __printf("f4=%f\n", 0.5);
    __printf("f5=%f\n", 123.456);
    __printf("f6=%f\n", -0.0);

    /* Int + float mixed */
    __printf("mix=%d+%f\n", 1, 2.5);

    return 0;
}
