// EXPECT: 0
// bitfield_sizeof — sizeof(struct) 含位域的正确性

struct bf {
    unsigned int a:4;
    unsigned int b:4;
};

struct bf2 {
    unsigned int a:1;
    unsigned int b:1;
    unsigned int c:1;
    unsigned int d:1;
};

struct bf3 {
    unsigned int a:4;
    unsigned int :4;    /* 匿名填充 */
    unsigned int b:4;
    unsigned int c:4;
};

struct bf4 {
    unsigned int a:4;
    unsigned int b:12;
    unsigned int c:16;   /* 正好填满 32 位 */
};

struct overflow {
    unsigned int a:20;
    unsigned int b:20;  /* 40 bits > 32 → second unit */
};

int main(void) {
    /* bf: a(4)+b(4)=8 bits, fits in 4-byte unit */
    if (sizeof(struct bf) != 4) return 1;

    /* bf2: 4 bits, fits in 4-byte unit */
    if (sizeof(struct bf2) != 4) return 2;

    /* bf3: a(4)+:4(4)+b(4)+c(4)=16 bits, fits in 4 bytes */
    if (sizeof(struct bf3) != 4) return 3;

    /* bf4: a(4)+b(12)+c(16)=32 bits, exactly one unit */
    if (sizeof(struct bf4) != 4) return 4;

    /* 溢出到下一单元 */
    if (sizeof(struct overflow) != 8) return 5;

    return 0;
}
