// EXPECT: 0
// bitfield_align_zero — :0 零宽度位域对齐

struct with_zero {
    unsigned int a:4;
    unsigned int :0;    /* 强制对齐到下一 uint32_t */
    unsigned int b:4;
};

struct without_zero {
    unsigned int a:4;
    unsigned int b:4;
};

int main(void) {
    /* a 在单元 0，b 在单元 4（:0 强制跳到下一单元） */
    if (sizeof(struct with_zero) != 8) return 1;

    struct with_zero v;
    v.a = 0; v.b = 0;

    v.a = 12;
    v.b = 3;

    if (v.a != 12) return 2;
    if (v.b != 3) return 3;

    /* 验证没有 :0 时能连续打包 */
    if (sizeof(struct without_zero) != 4) return 4;

    struct without_zero v2;
    v2.a = 7;
    v2.b = 7;
    if (v2.a != 7) return 5;
    if (v2.b != 7) return 6;

    return 0;
}
