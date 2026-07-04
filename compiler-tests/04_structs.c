// EXPECT: 0
// structs.c — struct 成员读写、-> 指针访问、static struct 数组（如 lex.c keywords、preproc.c）
typedef struct { int x; int y; } Point;

static int sum(Point *p) {
    return p->x + p->y;
}

int main(void) {
    Point p;
    p.x = 10; p.y = 20;
    if (p.x != 10) return 1;

    Point *q = &p;
    q->x = 30;
    if (p.x != 30) return 2;

    if (sum(&p) != 50) return 3;

    return 0;
}
