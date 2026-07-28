// EXPECT: 0
// bitfield_signed — 有符号位域符号扩展

struct sb {
    int x:4;   /* signed, range -8..7 */
    int y:8;   /* signed, range -128..127 */
};

int main(void) {
    struct sb v;
    v.x = 0; v.y = 0;

    /* 正数 */
    v.x = 5;
    if (v.x != 5) return 1;

    /* 负数：-1 */
    v.x = -1;
    if (v.x != -1) return 2;
    if (v.x >= 0) return 3;

    /* -8 (最小负值 for 4-bit) */
    v.x = -8;
    if (v.x != -8) return 4;
    if (v.x > -1) return 5;

    /* 溢出到负数域 */
    v.x = 15;  /* 15 in 4-bit signed = -1 */
    if (v.x != -1) return 6;

    v.x = 8;   /* 8 in 4-bit signed = -8 */
    if (v.x != -8) return 7;

    /* 8-bit signed */
    v.y = -100;
    if (v.y != -100) return 8;

    v.y = 127;
    if (v.y != 127) return 9;

    v.y = -128;
    if (v.y != -128) return 10;

    /* 混合读写 */
    v.x = -3;
    v.y = 42;
    if (v.x != -3) return 11;
    if (v.y != 42) return 12;

    return 0;
}
