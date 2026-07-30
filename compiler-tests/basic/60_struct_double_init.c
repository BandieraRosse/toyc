/*
 * struct_double_init — 结构体含 double 字段的数组初始化器正确性测试
 *
 * 验证全局结构体数组中的 double 字段被正确初始化（完整 8 字节），
 * 不因 cgen_emit_data_init 的 has_widths=0 bug 而只发射低 4 字节。
 *
 * 测试场景：
 *   1. 纯 double 结构体：验证 8 字节 double 值完整
 *   2. double + int 混合：验证不同大小成员的布局正确
 *   3. double 嵌套数组：验证数组成员的展开
 *
 * EXPEXT: 0
 */

/* ── 测试 1：纯 double 结构体 ── */
struct pair {
    double x;
    double y;
};

static const struct pair dpairs[] = {
    {1.0, 2.0},
    {3.0, 4.0},
};

/* ── 测试 2：double + int 混合 ── */
struct note {
    double freq;
    int    beats;
};

static const struct note melody[] = {
    {261.63, 1},
    {293.66, 2},
    {329.63, 1},
};

/* ── 测试 3：char + double + int（对齐敏感） ── */
struct mixed {
    char   tag;
    double val;
    int    count;
};

static const struct mixed mix[] = {
    {'A', 3.14, 100},
    {'B', 2.718, 200},
};

int main(void)
{
    /* ── 测试 1 ── */
    if (dpairs[0].x != 1.0)   return 1;
    if (dpairs[0].y != 2.0)   return 2;
    if (dpairs[1].x != 3.0)   return 3;
    if (dpairs[1].y != 4.0)   return 4;

    /* ── 测试 2 ── */
    if (melody[0].freq != 261.63) return 10;
    if (melody[0].beats != 1)     return 11;
    if (melody[1].freq != 293.66) return 12;
    if (melody[1].beats != 2)     return 13;
    if (melody[2].freq != 329.63) return 14;
    if (melody[2].beats != 1)     return 15;

    /* ── 测试 3 ── */
    if (mix[0].tag   != 'A')      return 20;
    if (mix[0].val   != 3.14)     return 21;
    if (mix[0].count != 100)      return 22;
    if (mix[1].tag   != 'B')      return 23;
    if (mix[1].val   != 2.718)    return 24;
    if (mix[1].count != 200)      return 25;

    return 0;
}
