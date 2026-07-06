// EXPECT: 0
// struct_type_prop.c — 测试 AST 类型传播：链式成员、函数参数、数组元素访问

typedef struct { int x; int y; } Point;
typedef struct { Point start; Point end; int len; } Line;

int main(void) {
    /* Test 1: 链式成员访问 a.b.c */
    {
        Line line;
        line.start.x = 1;
        line.start.y = 2;
        line.end.x = 3;
        line.end.y = 4;
        line.len = 5;
        if (line.start.x != 1) return 1;
        if (line.end.y != 4) return 2;
        if (line.len != 5) return 3;
    }

    /* Test 2: 指针链式访问 ptr->member.submember */
    {
        Line line;
        line.start.x = 10;
        line.start.y = 20;
        line.end.x = 30;
        line.end.y = 40;
        Line *lp = &line;
        if (lp->start.x != 10) return 10;
        if (lp->end.y != 40) return 11;
        /* 修改后验证 */
        lp->start.x = 100;
        if (line.start.x != 100) return 12;
    }

    /* Test 3: 函数参数 struct 指针访问 */
    {
        Point p;
        p.x = 42;
        p.y = 84;
        Point *pp = &p;
        if (pp->x != 42) return 20;
        if (pp->y != 84) return 21;
    }

    /* Test 4: 数组元素的成员访问 a[i].member */
    {
        Point arr[3];
        arr[0].x = 10; arr[0].y = 20;
        arr[1].x = 30; arr[1].y = 40;
        arr[2].x = 50; arr[2].y = 60;
        if (arr[0].x != 10) return 40;
        if (arr[1].y != 40) return 41;
        if (arr[2].x != 50) return 42;
    }

    /* Test 5: struct 嵌套在数组中 */
    {
        Line lines[2];
        lines[0].start.x = 1; lines[0].start.y = 2;
        lines[0].end.x = 3;   lines[0].end.y = 4;
        lines[1].start.x = 5; lines[1].start.y = 6;
        lines[1].end.x = 7;   lines[1].end.y = 8;
        if (lines[0].end.y != 4) return 50;
        if (lines[1].start.x != 5) return 51;
    }

    /* Test 6: 简单指针重新赋值 */
    {
        Point p1, p2;
        p1.x = 1; p1.y = 2;
        p2.x = 3; p2.y = 4;
        Point *pp = &p1;
        if (pp->x != 1) return 60;
        if (pp->y != 2) return 61;
    }

    /* Test 7: 简单字段赋值 */
    {
        Point p;
        p.x = 99;
        if (p.x != 99) return 70;
        p.y = 88;
        if (p.y != 88) return 71;
    }

    return 0;
}
