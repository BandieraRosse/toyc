// EXPECT: 0
// typedef_struct.c — 测试 typedef + struct 复杂模式
// 自举关键：toyc.h 和 parse.c 大量使用 typedef struct { ... } Name

/* 1. 基本 typedef struct */
typedef struct { int x; int y; } Point;

/* 2. 带 tag 的 typedef struct */
typedef struct Buffer {
    char data[16];
    int len;
} Buffer;

/* 3. 匿名 struct typedef 在函数参数中 */
typedef struct { const char *name; int value; } Entry;

/* 4. 嵌套 typedef struct */
typedef struct { Point start; Point end; } Line;

/* 5. 含有数组成员的 struct */
typedef struct { int items[8]; int count; } ArrayHolder;

/* 6. typedef 指针类型 */
typedef char *String;
typedef int *IntPtr;

/* 7. 函数指针 typedef */
typedef int (*BinaryOp)(int, int);

static int add_func(int a, int b) { return a + b; }
static int mul_func(int a, int b) { return a * b; }

int main(void) {
    /* Test 1: 基本 struct */
    Point p;
    p.x = 10;
    p.y = 20;
    if (p.x != 10) return 1;
    if (p.y != 20) return 2;

    /* Test 2: 带 tag + 数组 */
    Buffer buf;
    buf.len = 5;
    buf.data[0] = 'a';
    buf.data[1] = 'b';
    if (buf.len != 5) return 10;
    if (buf.data[1] != 'b') return 11;

    /* Test 3: typedef 作为参数类型 */
    Entry e;
    e.name = "hello";
    e.value = 42;
    if (e.value != 42) return 20;

    /* Test 4: 嵌套 struct */
    Line line;
    line.start.x = 1;
    line.start.y = 2;
    line.end.x = 3;
    line.end.y = 4;
    if (line.start.x != 1) return 30;
    if (line.end.y != 4) return 31;

    /* Test 5: 嵌套结构体赋值 */
    Point *pp = &p;
    pp->x = 100;
    if (p.x != 100) return 40;

    /* Test 6: typedef 指针 */
    int val = 42;
    IntPtr ip = &val;
    if (*ip != 42) return 50;

    /* Test 7: 函数指针 */
    BinaryOp op = add_func;
    if (op(10, 20) != 30) return 60;
    op = mul_func;
    if (op(5, 6) != 30) return 61;

    return 0;
}
