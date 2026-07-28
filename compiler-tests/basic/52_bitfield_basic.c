// EXPECT: 0
// bitfield_basic — 基础位域读写（unsigned，单字段）

struct bf {
    unsigned int x:4;
    unsigned int y:8;
};

int main(void) {
    struct bf v;
    v.x = 0;
    v.y = 0;

    v.x = 5;
    if (v.x != 5) return 1;
    if (v.x != 5) return 2;

    v.y = 200;
    if (v.y != 200) return 3;

    /* 截断：4 位只能存 0-15 */
    v.x = 20;
    if (v.x != 4) return 4;  /* 20 & 0xF = 4 */

    /* 连续两个字段 */
    v.x = 3;
    v.y = 100;
    if (v.x != 3) return 5;
    if (v.y != 100) return 6;

    return 0;
}
