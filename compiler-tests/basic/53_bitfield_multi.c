// EXPECT: 0
// bitfield_multi — 多个相邻位域打包到同一存储单元

struct packed {
    unsigned int a:3;
    unsigned int b:3;
    unsigned int c:3;
    unsigned int d:3;
    unsigned int e:3;  /* 15 bits used, fits in one uint32_t */
};

int main(void) {
    struct packed v;
    v.a = 0; v.b = 0; v.c = 0; v.d = 0; v.e = 0;

    v.a = 7;   /* 3-bit max */
    v.b = 5;
    v.c = 3;
    v.d = 1;
    v.e = 0;

    if (v.a != 7) return 1;
    if (v.b != 5) return 2;
    if (v.c != 3) return 3;
    if (v.d != 1) return 4;
    if (v.e != 0) return 5;

    /* 验证不会互相干扰 */
    v.a = 1;
    if (v.b != 5) return 6;  /* b unchanged */
    v.e = 6;
    if (v.d != 1) return 7;  /* d unchanged */
    if (v.a != 1) return 8;  /* a unchanged */

    /* 超出位宽截断 */
    v.a = 10;    /* 10 & 0x7 = 2 */
    if (v.a != 2) return 9;

    return 0;
}
