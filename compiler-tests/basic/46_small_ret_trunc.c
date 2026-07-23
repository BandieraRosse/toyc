/*
 * 46_small_ret_trunc.c — 验证 toyc 对小整数类型返回值的截断修复
 *
 * 1. unsigned short 返回值截断（如 htons/ntohs）
 * 2. unsigned char 返回值截断
 * 3. signed char/short 返回值符号扩展
 * 4. 无符号常量 0xFFFFFFFFU 比较
 * 5. 无符号常量 > INT_MAX 的赋值使用
 * 6. uint8_t 数组初始化值 >127
 *
 * EXPECT: 0
 */

/* ================================================================ */
/* 测试 1: unsigned short 返回值截断                                  */
/* ================================================================ */

static unsigned short test_htons(unsigned short host_val) {
    return (host_val << 8) | (host_val >> 8);
}

static int test_ushort_returns(void) {
    if (test_htons(0x1234) != 0x3412) return 1;
    if (test_htons(0x0001) != 0x0100) return 2;
    if (test_htons(0xFFFF) != 0xFFFF) return 3;
    return 0;
}

/* ================================================================ */
/* 测试 2: unsigned char 返回值截断                                    */
/* ================================================================ */

static unsigned char ret_0xC0_uchar(void) {
    int x = 192;
    return x;
}

static int test_uchar_returns(void) {
    if (ret_0xC0_uchar() != 192) return 1;
    if (ret_0xC0_uchar() != (unsigned char)0xC0) return 2;
    return 0;
}

/* ================================================================ */
/* 测试 3: signed char/short 返回值符号扩展                            */
/* ================================================================ */

static signed char ret_neg_schar(void) {
    int x = -42;
    return x;
}

static signed short ret_neg_sshort(void) {
    int x = -1000;
    return x;
}

static int test_signed_small_returns(void) {
    if (ret_neg_schar() != (signed char)-42) return 1;
    if (ret_neg_sshort() != (signed short)-1000) return 2;
    return 0;
}

/* ================================================================ */
/* 测试 4: 无符号常量 0xFFFFFFFFU 比较                                */
/* ================================================================ */

static unsigned int ret_allones(void) {
    unsigned int x = -1;
    return x;
}

static int test_ffff_comparison(void) {
    if (ret_allones() != 0xFFFFFFFFU) return 1;
    /* 直接常量比较 */
    unsigned int a = 0xFFFFFFFFU;
    if (a != 0xFFFFFFFFU) return 2;
    if (a == 0) return 3;
    if (~a != 0) return 4;
    return 0;
}

/* ================================================================ */
/* 测试 5: 无符号常量 > INT_MAX 的赋值和使用                           */
/* ================================================================ */

static int test_large_unsigned(void) {
    unsigned int val = 0xC0A80101U;
    if (val != 0xC0A80101U) return 1;
    if (val != 3232235777U) return 2;
    if (val <= 0x7FFFFFFF) return 3;
    /* 结构体成员赋值 */
    {
        struct { unsigned int s_addr; } in;
        in.s_addr = 0xC0A80101U;
        if (in.s_addr != 0xC0A80101U) return 4;
    }
    return 0;
}

/* ================================================================ */
/* 测试 6: uint8_t 数组初始化值 >127                                  */
/* ================================================================ */

static int test_uint8_brace_init(void) {
    unsigned char arr[] = {7, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
                           3, 'c', 'o', 'm', 0,
                           0xC0, 0x00};
    if (arr[13] != 192) return 1;
    if (arr[13] != (unsigned char)0xC0) return 2;
    if (arr[0] != 7) return 3;
    if (arr[1] != 'e') return 4;
    if (arr[12] != 0) return 5;
    if (arr[14] != 0) return 6;
    return 0;
}

/* 全局 uint8_t 数组初始化 */
unsigned char global_arr[] = {0xC0, 0x00, 0x7F, 0x80, 0xFF, 'A'};

static int test_global_uint8_brace_init(void) {
    if (global_arr[0] != 0xC0) return 1;
    if (global_arr[0] != 192) return 2;
    if (global_arr[1] != 0) return 3;
    if (global_arr[2] != 0x7F) return 4;
    if (global_arr[3] != 0x80) return 5;
    if (global_arr[3] != 128) return 6;
    if (global_arr[4] != 0xFF) return 7;
    if (global_arr[4] != 255) return 8;
    if (global_arr[5] != 'A') return 9;
    return 0;
}

/* ================================================================ */
/* 主函数                                                             */
/* ================================================================ */

int main(void)
{
    int r;

    r = test_ushort_returns();
    if (r) return r;

    r = test_uchar_returns();
    if (r) return 10 + r;

    r = test_signed_small_returns();
    if (r) return 20 + r;

    r = test_ffff_comparison();
    if (r) return 30 + r;

    r = test_large_unsigned();
    if (r) return 40 + r;

    r = test_uint8_brace_init();
    if (r) return 50 + r;

    r = test_global_uint8_brace_init();
    if (r) return 60 + r;

    return 0;
}
