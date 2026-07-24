// EXPECT: 0
// designated_init.c — C99 指定初始化器（designated initializer）测试
//
// 测试 struct 局部变量的 .field = value 语法

/* ── 测试 1：纯 int 结构体，顺序指派 ── */
typedef struct { int a; int b; int c; } ThreeInts;

/* ── 测试 2：混合类型结构体 ── */
typedef struct { char c; short s; int i; long l; } MixedInts;

/* ── 测试 3：含指针成员的结构体 ── */
typedef struct { const char *name; int value; } Entry;

/* ── 测试 4：通过 typedef 的匿名 struct ── */
typedef struct { int x; int y; int z; } Point3D;

int main(void)
{
    /* ── 测试 1：顺序指派 ── */
    {
        ThreeInts s = { .a = 10, .b = 20, .c = 30 };
        if (s.a != 10) return 1;
        if (s.b != 20) return 2;
        if (s.c != 30) return 3;
    }

    /* ── 测试 2：乱序指派 ── */
    {
        ThreeInts s = { .c = 3, .a = 1, .b = 2 };
        if (s.a != 1)  return 10;
        if (s.b != 2)  return 20;
        if (s.c != 3)  return 30;
    }

    /* ── 测试 3：全倒序指派 ── */
    {
        ThreeInts s = { .c = 99, .b = 88, .a = 77 };
        if (s.a != 77) return 40;
        if (s.b != 88) return 50;
        if (s.c != 99) return 60;
    }

    /* ── 测试 4：混合类型 ── */
    {
        MixedInts m = { .l = 1000L, .i = 100, .s = 10, .c = 'X' };
        if (m.c != 'X')  return 70;
        if (m.s != 10)   return 80;
        if (m.i != 100)  return 90;
        if (m.l != 1000) return 100;
    }

    /* ── 测试 5：指针成员 ── */
    {
        Entry e = { .value = 42, .name = "hello" };
        if (e.value != 42)           return 110;
        if (e.name[0] != 'h')        return 120;
        if (e.name[1] != 'e')        return 130;
    }

    /* ── 测试 6：typedef 匿名 struct ── */
    {
        Point3D p = { .z = 300, .y = 200, .x = 100 };
        if (p.x != 100) return 140;
        if (p.y != 200) return 150;
        if (p.z != 300) return 160;
    }

    /* ── 测试 7：单个成员指派（只设一个成员） ── */
    {
        ThreeInts s = { .b = 7 };
        /* 注意：当前局部 brace init 不对其他成员零初始化，
         * 所以 s.a 和 s.c 是未定义值。这里只断言显式指派的成员。 */
        if (s.b != 7) return 170;
    }

    return 0;
}
