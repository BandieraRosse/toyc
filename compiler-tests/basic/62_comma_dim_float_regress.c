// EXPECT: 0
// comma_dim_float_regress.c — 逗号路径表达式维度 + 浮点字面量/指针回归测试
//
// 覆盖 2026-07-31 修复的 toyc bug：
//   1. 逗号分隔成员声明中的表达式维度（float grbuf[2][576], scf[40],
//      syn[18+15][2*32]）——首个成员路径已修，逗号路径仍只认 TOK_NUMBER，
//      syn 被算成 18 行 → 结构体欠分配 → 溢出覆盖调用者帧（mp3_player
//      崩溃根因之一）。这里验证 syn 大小与后续成员偏移。
//   2. 整数值 float 字面量（1.f / 4.f/3 / 2.f/9）——词法器要求小数点后
//      必须是数字，1.f 被解析为"1 . f"（成员访问）→ 基址 1 + 偏移 -1 = 0
//      → 加载 [0] 崩溃（L3_pow_43 崩溃根因）。
//   3. *p++ 无符号字节加载——后缀自增代码生成只传播 elem_is_float，
//      未传播 elem_is_unsigned → movsbl 符号扩展 0xF0 → 0xFFFFFFF0，
//      污染位运算（get_bits 侧信息解析全零 → mp3 静音根因）。
//   4. float 数组名作为实参（memcpy(tmp, grbuf, n)）——解析期 AST_VAR
//      is_float=4（元素浮点性）被误判为浮点实参 → 数组地址被 cvti2d
//      转换后丢失/按 XMM 参数分配 → 参数错位（memcpy 崩溃根因）。
//   5. float 数组名 + 整数（tmp+1）——BINOP 浮点检测把数组名当浮点值，
//      走浮点加法路径，右操作数常量求值覆盖 rax 中的数组地址。

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uintptr_t;
typedef float f4;

#define MAX_L3_FRAME_PAYLOAD_BYTES 2304
#define MAX_BITRESERVOIR_BYTES 511

typedef struct { const unsigned char *buf; int pos, limit; } bs_t;

typedef struct {
    const unsigned char *sfbtab;
    uint16_t part_23_length, big_values, scalefac_compress;
    uint8_t global_gain, block_type, mixed_block_flag, n_long_sfb, n_short_sfb;
    uint8_t table_select[3], region_count[3], subblock_gain[3];
    uint8_t preflag, scalefac_scale, count1_table, scfsi;
} L3_gr_info_t;

/* 1. 逗号路径表达式维度：syn 在逗号列表第三个，[18+15][2*32] 应得 8448 字节 */
typedef struct {
    bs_t bs;
    uint8_t maindata[MAX_BITRESERVOIR_BYTES + MAX_L3_FRAME_PAYLOAD_BYTES];
    L3_gr_info_t gr_info[4];
    float grbuf[2][576], scf[40], syn[18 + 15][2*32];
    uint8_t ist_pos[2][39];
} mp3dec_scratch_t;

/* 3. *p++ 无符号字节加载 */
static uint32_t get_bits_u8(const uint8_t *buf, int *pos, int n)
{
    uint32_t next, cache = 0, s = (*pos) & 7;
    int shl = n + s;
    const uint8_t *p = buf + ((*pos) >> 3);
    *pos += n;
    next = *p++ & (255 >> s);
    while ((shl -= 8) > 0) {
        cache |= next << shl;
        next = *p++;
    }
    return cache | (next >> -shl);
}

/* 4. float 数组名作为实参 */
extern void *memcpy(void *, const void *, unsigned long);
static float g_dst[18];

static void copy_arr(float *grbuf)
{
    float tmp[18];
    memcpy(tmp, grbuf, 72);
    g_dst[0] = tmp[0];
    g_dst[1] = tmp[17];
    /* 5. float 数组名 + 整数：tmp+1 应是指针算术（&tmp[1]）而非浮点加法 */
    g_dst[2] = *(tmp + 1);
}

int main(void)
{
    mp3dec_scratch_t s;
    static const uint8_t payload[8] = {0x00, 0x0f, 0xf0, 0x00, 0x00, 0x69, 0x00, 0x00};

    /* 1. 逗号路径表达式维度：syn 8448 字节，ist_pos 紧跟其后 */
    if ((int)((uintptr_t)&s.syn - (uintptr_t)&s) != 7728) return 1;
    if ((int)((uintptr_t)&s.ist_pos - (uintptr_t)&s) != 16176) return 2;
    if ((int)sizeof(mp3dec_scratch_t) != 16256) return 3;

    /* 2. 整数值 float 字面量 */
    {
        float a = 1.f;
        float b = 4.f/3;
        float c = 2.f/9;
        if (a != 1.0f) return 4;
        if (b < 1.3f || b > 1.4f) return 5;
        if (c < 0.2f || c > 0.25f) return 6;
        if ((int)(a * 3.0f) != 3) return 7;
    }

    /* 3. *p++ 无符号加载：位提取应与 gcc 一致
     * payload = 00 0f f0 00 00 69 —— mdb=0, scfsi=0x3F, part23=0xC00 */
    {
        int pos = 0;
        int mdb = (int)get_bits_u8(payload, &pos, 9);
        int scfsi = (int)get_bits_u8(payload, &pos, 9);
        int p23 = (int)get_bits_u8(payload, &pos, 12);
        if (mdb != 0) return 8;
        if (scfsi != 0x3F) return 9;
        if (p23 != 0xC00) return 10;
        if (pos != 30) return 11;
    }

    /* 4+5. float 数组名实参 + 指针算术 */
    {
        float src[18];
        int i;
        for (i = 0; i < 18; i++) src[i] = (float)(i + 1);
        copy_arr(src);
        if (g_dst[0] != 1.0f) return 12;
        if (g_dst[1] != 18.0f) return 13;
        if (g_dst[2] != 2.0f) return 14;
    }

    return 0;
}
