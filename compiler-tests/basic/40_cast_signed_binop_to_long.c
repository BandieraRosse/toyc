/* 有符号 int 表达式转 long 时必须符号扩展，unsigned 则必须零扩展。 */
static long interpolate(int ax, int bx, long numerator, long denominator)
{
    long delta = (long)(bx - ax);
    if (delta != -999) return 1000000000L;
    return ax + delta * numerator / denominator;
}

int main(void)
{
    unsigned int u = 0xffffffffU;
    long x = interpolate(18, -981, -8, -1023);
    if (x != 11) return 1;
    if ((unsigned long)u != 4294967295UL) return 2;
    return 0;
}
