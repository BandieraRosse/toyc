// EXPECT: 0
// bitfield_mixed — 位域与普通成员混合

struct mixed {
    unsigned int a:4;
    int b;
    unsigned int c:8;
    int d;
};

int main(void) {
    struct mixed v;
    v.a = 0; v.b = 0; v.c = 0; v.d = 0;

    v.a = 10;
    v.b = -1000;
    v.c = 200;
    v.d = 12345;

    if (v.a != 10) return 1;
    if (v.b != -1000) return 2;
    if (v.c != 200) return 3;
    if (v.d != 12345) return 4;

    /* 验证非位域不会干扰 */
    v.b = 42;
    if (v.a != 10) return 5;

    /* 数组之后的位域 */
    struct {
        int arr[3];
        unsigned int flag:1;
    } s2;

    s2.arr[0] = 100;
    s2.arr[1] = 200;
    s2.arr[2] = 300;
    s2.flag = 1;

    if (s2.arr[0] != 100) return 6;
    if (s2.arr[1] != 200) return 7;
    if (s2.arr[2] != 300) return 8;
    if (s2.flag != 1) return 9;

    s2.flag = 0;
    if (s2.flag != 0) return 10;

    return 0;
}
