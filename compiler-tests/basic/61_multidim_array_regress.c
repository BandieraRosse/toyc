// EXPECT: 0
// multidim_array_regress.c — 多维数组/浮点代码生成回归测试
//
// 覆盖 2026-07-31 修复的 toyc bug：
//   1. 3D 数组（static const 初始化 + 读取，minimp3 halfrate 模式）—
//      子数组退化失败导致把加载值当指针（原崩溃点）
//   2. 2D 小行数组（uint8_t[2][3]，行大小 < 4 未退化）
//   3. 全局 3D 数组变量索引（中间层 stride 错误 + 二次加载）
//   4. sizeof(结构体成员数组)（原恒为 8 → 路径截断）
//   5. sizeof 多维下标链（sizeof(a[i][j]) 原恒为 8）
//   6. float 指针参数 p[i]（elem_is_float 缺失 → int 加载）
//   7. *w++ 解引用（++ 不传播 elem_size → char 加载）
//   8. 负索引（4*(i-16)+2 32 位零扩展污染指针高 32 位）
//   9. 结构体 float 数组成员（is_float 语义）
//
// 暴露未修复 bug：
//   10. 结构体成员数组维度为常量表达式（float syn[18+15][2*32]）—
//       parse_struct_body 只认 TOK_NUMBER，18+15 被当作 18，
//       syn 只分配 18*64*4=4608 字节（应 8448）→ 访问越界写穿栈帧
//   11. float 返回值传给 double 参数缺少提升（应 cvtss2sd，
//       toyc 直接按 double 位模式传递 float 位模式）

typedef unsigned char uint8_t;

/* 1. 3D static const 数组：初始化 + 3 层索引读取（minimp3 hdr_bitrate 模式） */
static int hdr_bitrate(int mpeg1, int layer, int br)
{
    static const uint8_t halfrate[2][3][15] = {
        { { 0,4,8,12,16,20,24,28,32,40,48,56,64,72,80 },
          { 0,4,8,12,16,20,24,28,32,40,48,56,64,72,80 },
          { 0,16,24,28,32,40,48,56,64,72,80,88,96,112,128 } },
        { { 0,16,20,24,28,32,40,48,56,64,80,96,112,128,160 },
          { 0,16,24,28,32,40,48,56,64,80,96,112,128,160,192 },
          { 0,16,32,48,64,80,96,112,128,144,160,176,192,208,224 } },
    };
    return 2*halfrate[mpeg1][layer][br];
}

/* 2. 2D 小行数组：uint8_t[2][3]（行大小 < 4，旧 is_subarray 门失效） */
static int small_rows(void)
{
    static const uint8_t a[2][3] = { {1,2,3}, {4,5,6} };
    int i, v = 0;
    for (i = 0; i < 2; i++) {
        int j;
        for (j = 0; j < 3; j++) {
            if (a[i][j] != v + 1) return 100 + i*10 + j;
            v++;
        }
    }
    return 0;
}

/* 3. 全局 3D 数组 + 变量索引 */
static const uint8_t g3[2][3][2] = {
    { {1,2}, {3,4}, {5,6} },
    { {7,8}, {9,10}, {11,12} },
};
static int global_3d(void)
{
    if (g3[1][2][1] != 12) return 200;
    if (g3[0][0][0] != 1)  return 201;
    if (g3[1][0][1] != 8)  return 202;
    return 0;
}

/* 4. sizeof(结构体成员数组) — 原恒为 8 */
struct dev { char path[48]; int card; };
static int sizeof_member(void)
{
    struct dev d;
    if ((int)sizeof(d.path) != 48) return 300;
    if ((int)sizeof(d.card) != 4)  return 301;
    return 0;
}

/* 5. sizeof 多维下标链 */
static int sizeof_chain(void)
{
    float t[4][8];
    if ((int)sizeof(t)     != 128) return 400;
    if ((int)sizeof(t[0])  != 32)  return 401;
    if ((int)sizeof(t[0][0]) != 4) return 402;
    return 0;
}

/* 6. float 指针参数：p[i] 应浮点加载（原 int 加载 + 转换） */
static float sum3(const float *p)
{
    return p[0] + p[1] + p[2];
}

/* 7. *w++ 解引用：++ 传播元素大小和浮点性 */
static const float g_win[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
static int deref_postfix(void)
{
    const float *w = g_win;
    float w0 = *w++;
    float w1 = *w++;
    float w2 = *w++;
    return (int)(w0 + w1 + w2);   /* 1+2+3 = 6（转换在函数内部，局部 float） */
}

/* 8. 负索引：指针 + 负的复合索引需符号扩展 */
static int neg_index(float *zlin)
{
    int i = 14;
    zlin[4*(i - 16) + 2] = 1.5f;   /* 索引 -6，旧代码 32 位零扩展污染高 32 位 */
    return (int)zlin[4*(i - 16) + 2];
}

/* 9. 结构体 float 数组成员：值退化为指针，元素浮点访问正常 */
struct scratch {
    char head[16];
    float grbuf[2][8];
};
static int member_float_arr(struct scratch *s)
{
    s->grbuf[1][3] = 6.25f;
    return (int)(s->grbuf[1][3] * 4.0f);   /* 6.25*4 = 25 */
}

/* 10. 未修复：成员数组维度为常量表达式（18+15 被当作 18） */
struct exprdim {
    char head[16];
    float syn[18 + 15][2*32];   /* 33 行 × 64 列，应 8448 字节 */
    char tail[16];
};

/* 11. 未修复：float 返回值传给 double 参数（缺少 cvtss2sd 提升） */
static float getf(void) { return 7.5f; }
static int is_75(double d) { return (d > 7.0 && d < 8.0) ? 0 : 1; }

/* 12. 未修复：无参 float 返回函数（f()）的调用节点 is_float 不设置 —
 *     parse 期启发式要求"任一实参为 float"，无参函数永不满足 → (int)f() 转换丢失 */
static float get7(void) { return 7.0f; }

int main(void)
{
    float buf[8];
    float v;
    struct exprdim es;

    /* 1. 3D static const */
    if (hdr_bitrate(1, 2, 14) != 448) return 1;
    if (hdr_bitrate(1, 2, 9)  != 288) return 2;
    if (hdr_bitrate(0, 0, 1)  != 8)   return 3;

    /* 2. 2D 小行数组 */
    if (small_rows()) return 4;

    /* 3. 全局 3D */
    if (global_3d()) return 5;

    /* 4. sizeof 成员 */
    if (sizeof_member()) return 6;

    /* 5. sizeof 链 */
    if (sizeof_chain()) return 7;

    /* 6. float 指针参数 p[i]：sum3 应得 7.5（带 float 实参触发返回类型推断） */
    buf[0] = 1.5f; buf[1] = 2.5f; buf[2] = 3.5f;
    if ((int)sum3(buf) != 7) return 8;

    /* 7. *w++：1+2+3 = 6 */
    if (deref_postfix() != 6) return 9;

    /* 8. 负索引：4*(i-16)+2 = -6，值 1.5 → (int)1 */
    if (neg_index(buf) != 1) return 10;

    /* 9. float 数组成员：6.25*4 = 25 */
    {
        struct scratch sc;
        if (member_float_arr(&sc) != 25) return 11;
    }

    /* 10. 未修复 bug 暴露：成员数组维度表达式
     * 修复前 sizeof(syn)=4608（18 行），期望 8448（33 行）→ 返回 200 暴露 */
    if ((int)sizeof(es.syn) != 33*64*4) return 200;
    if ((int)sizeof(es)     != 16 + 33*64*4 + 16) return 201;

    /* 11. 未修复 bug 暴露：float 返回值 → double 参数
     * 修复前 7.5f 的位模式被当作 double（极小值）→ is_75 失败 → 返回 210 暴露 */
    if (is_75(getf())) return 210;

    /* 12. 未修复 bug 暴露：无参 float 返回函数的 (int) 转换
     * 修复前 (int)get7() 直接按 EAX 读垃圾 → 返回 220 暴露 */
    if ((int)get7() != 7) return 220;

    return 0;
}
