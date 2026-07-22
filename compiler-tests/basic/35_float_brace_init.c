/* Test: Global float/double brace initialization (P3 fix)
 * Tests that float and double literals in global brace initializers
 * correctly produce IEEE 754 bit patterns in the .data section.
 * EXPECT: 0
 */

float farr[] = {1.0f, 2.0f, 3.0f, 10.5f};
double darr[] = {1.0, 2.0, 3.14, -2.5, 0.5};
static float sf = 42.5f;
static double sd = 99.99;
int fsize = sizeof(farr) / sizeof(farr[0]);
int dsize = sizeof(darr) / sizeof(darr[0]);

int main() {
    /* ─── float array ─── */
    if (fsize != 4) return 1;
    if ((int)(farr[0] * 10) != 10) return 2;
    if ((int)(farr[1] * 10) != 20) return 3;
    if ((int)(farr[2] * 10) != 30) return 4;
    if ((int)(farr[3] * 10) != 105) return 5;

    /* ─── double array ─── */
    if (dsize != 5) return 6;
    if ((int)(darr[0] * 10) != 10) return 7;
    if ((int)(darr[1] * 10) != 20) return 8;
    if ((int)(darr[2] * 100) != 314) return 9;
    if ((int)(darr[3] * 10) != -25) return 10;
    if ((int)(darr[4] * 10) != 5) return 11;

    /* ─── Static scalar float/double ─── */
    if ((int)(sf * 10) != 425) return 12;
    if ((int)(sd * 100) != 9999) return 13;

    /* ─── Sequential reads (test array data integrity) ─── */
    {
        float f;
        f = farr[0]; if ((int)(f * 10) != 10) return 14;
        f = farr[1]; if ((int)(f * 10) != 20) return 15;
        f = farr[2]; if ((int)(f * 10) != 30) return 16;
        f = farr[3]; if ((int)(f * 10) != 105) return 17;
    }

    /* ─── Int casts from indexed reads ─── */
    {
        if ((int)farr[0] != 1) return 18;
        if ((int)farr[3] != 10) return 19;
        if ((int)darr[2] != 3) return 20;
        if ((int)darr[3] != -2) return 21;
    }

    /* ─── Single-element scalar initialization (non-brace) ─── */
    {
        float x = -3.5f;
        double y = 2.718;
        if ((int)(x * 10) != -35) return 22;
        if ((int)(y * 1000) != 2718) return 23;
    }

    return 0;
}
