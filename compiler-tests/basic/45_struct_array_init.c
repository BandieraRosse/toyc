/*
 * struct_array_init — 结构体数组初始化器布局正确性测试
 *
 * 验证全局结构体数组中各元素字段正确，
 * 即使成员初始化项数不同（元素间 init items 数不同）。
 *
 * 测试场景：
 *   1. 纯 int 结构体，顶层项数不同 — 验证 element padding
 *   2. 指针全初始化结构体 — 验证嵌套 {} 展开 + STR/INT 混合
 *
 * 已知限制：嵌套数组成员部分初始化（如 {4,5} 用于 int nums[3]）
 * 时 padding 位置不对（has_widths=0 路径无成员感知）。
 *
 * EXPEXT: 0
 */

/* ── 测试 1：纯 int，项数不同 ── */
static struct { int a; int b; int c; } arr_a[] = {
    {10, 20, 30},
    {40, 50},
    {60},
};

/* ── 测试 2：指针全初始化 ── */
static struct { const char *name; const char *extra[2]; int value; } arr_b[] = {
    {"first",  {"x", "y"}, 100},
    {"second", {"z", "w"}, 200},
};

int main(void)
{
    /* ── 测试 1 ── */
    if (arr_a[0].a != 10) return 1;
    if (arr_a[0].c != 30) return 2;
    if (arr_a[1].a != 40) return 3;
    if (arr_a[1].b != 50) return 4;
    if (arr_a[1].c != 0)  return 5;
    if (arr_a[2].a != 60) return 6;
    if (arr_a[2].b != 0)  return 7;
    if (arr_a[2].c != 0)  return 8;

    /* ── 测试 2 ── */
    if (arr_b[0].name[0] != 'f')      return 20;
    if (arr_b[0].extra[0][0] != 'x')  return 21;
    if (arr_b[0].extra[1][0] != 'y')  return 22;
    if (arr_b[0].value != 100)        return 23;
    if (arr_b[1].name[0] != 's')      return 24;
    if (arr_b[1].extra[0][0] != 'z')  return 25;
    if (arr_b[1].extra[1][0] != 'w')  return 26;
    if (arr_b[1].value != 200)        return 27;

    return 0;
}
