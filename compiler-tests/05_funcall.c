// EXPECT: 0
// funcall.c — 函数调用（多参数、递归、函数指针）（如 parse.c、cgen.c）
static int add(int a, int b) { return a + b; }
static int sum4(int a, int b, int c, int d) { return a + b + c + d; }
static int sum7(int a,int b,int c,int d,int e,int f,int g) {
    return a+b+c+d+e+f+g;
}
static int fact(int n) { if (n <= 1) return 1; return n * fact(n - 1); }
static int inc(int x) { return x + 1; }
static int twice(int x) { return x * 2; }

int main(void) {
    if (add(3, 4) != 7) return 1;
    if (sum4(1, 2, 3, 4) != 10) return 2;
    if (sum7(1, 2, 3, 4, 5, 6, 7) != 28) return 3;
    if (fact(5) != 120) return 4;
    if (inc(twice(10)) != 21) return 5;

    int (*fp)(int, int) = add;
    if (fp(10, 3) != 13) return 6;

    return 0;
}
