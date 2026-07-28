/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 BandieraRosse
 *
 * test: struct 成员对齐 — unsigned long 等 8 字节类型的对齐
 *
 * toyc 曾将 struct snd_pcm_sw_params 的 sizeof 算成 132 而非 136，
 * 原因是非 struct 类型成员（unsigned long）的 member_tag_for_chain 回退到
 * 外层 struct 自身的 tag，而该 struct 尚未注册，mst=NULL → alignment=1。
 *
 * 本测试验证 signed/unsigned long、指针（char *）、嵌套 struct 的对齐。
 * EXPECT: 0
 */

/* 测试结构体 1：unsigned long 与 int 混合 */
struct test_align1 {
    int            a;       /* 4 */
    int            b;       /* 4 */
    unsigned long  c;       /* 8, pad from 8→16 */
};

/* 测试结构体 2：指针与 int */
struct test_align2 {
    int   a;               /* 4 */
    char *p;               /* 8, pad from 4→8 */
};

/* 测试结构体 3：snd_pcm_sw_params 的简化版 */
struct test_align3 {
    int            tstamp_mode;
    unsigned int   period_step;
    unsigned int   sleep_min;
    unsigned long  avail_min;
    unsigned long  xfer_align;
    unsigned long  start_threshold;
    unsigned long  stop_threshold;
    unsigned long  silence_threshold;
    unsigned long  silence_size;
    unsigned long  boundary;
    unsigned int   proto;
    unsigned int   tstamp_type;
};

/* 测试结构体 4：嵌套 struct */
struct test_inner {
    int x;
    unsigned long y;
};

struct test_align4 {
    int             a;
    struct test_inner inner;
};

int main(void) {
    /* struct test_align1: int(4) + int(4) + pad(4) + unsigned long(8) = 16 */
    if ((unsigned long)&((struct test_align1*)0)->c != 8)
        return 10;
    if (sizeof(struct test_align1) != 16)
        return 11;

    /* struct test_align2: int(4) + pad(4) + ptr(8) = 16 */
    if ((unsigned long)&((struct test_align2*)0)->p != 8)
        return 20;
    if (sizeof(struct test_align2) != 16)
        return 21;

    /* struct test_align3: int(4)+int(4)+int(4)+pad(4)+ 8×unsigned long(8)
     *                    + uint(4)+uint(4) = 80, 8-aligned */
    if ((unsigned long)&((struct test_align3*)0)->avail_min != 16)
        return 30;
    if ((unsigned long)&((struct test_align3*)0)->boundary != 64)
        return 31;
    if (sizeof(struct test_align3) != 80)
        return 32;

    /* struct test_align4: int(4) + pad(4) + test_inner(16) = 24 */
    if ((unsigned long)&((struct test_inner*)0)->y != 8)
        return 40;
    if ((unsigned long)&((struct test_align4*)0)->inner != 8)
        return 41;
    if (sizeof(struct test_align4) != 24)
        return 42;

    return 0;
}
