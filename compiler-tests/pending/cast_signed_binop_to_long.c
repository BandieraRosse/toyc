/*
 * Pending regression behind wayland_fps near-plane clipping.
 *
 * A negative signed-int binary expression must be sign-extended when cast to
 * long.  Toyc currently zero-extends (bx - ax), which turns -999 into
 * 4294966297 and sends the clipped vertex millions of pixels off screen.
 */
static long interpolate(int ax, int bx, long numerator, long denominator)
{
    long delta = (long)(bx - ax);
    if (delta != -999) return 1000000000L;
    return ax + delta * numerator / denominator;
}

int main(void)
{
    long x = interpolate(18, -981, -8, -1023);
    return x == 11 ? 0 : 1;
}
