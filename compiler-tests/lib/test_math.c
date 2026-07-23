/* test_math.c — 验证 math.c 能否被 toyc 编译并运行
 * 使用同目录下的 stub 头文件 (core.h, math.h)。
 * 编译后运行打印每个函数的 PASS/FAIL 状态。
 * EXPECT: 0
 */

#include "core.h"
#include "math.h"

/* toyc 需要链接 __printf 入 tcc_rt.o */
extern void __printf(const char *fmt, ...);

/* 允许的误差范围 */
#define EPS 1e-10

static int dbl_near(double a, double b) {
    double diff = a - b;
    if (diff < 0.0) diff = -diff;
    return diff < EPS;
}

static int total, passed;

static void check(const char *name, int cond) {
    total++;
    if (cond) { passed++; __printf("  %s: PASS\n", name); }
    else       { __printf("  %s: FAIL\n", name); }
}

int main(void)
{
    double v;

    __printf("math.c Tinylibc 兼容性测试\n");
    __printf("-------------------------\n");

    /* =============== sqrt =============== */
    v = sqrt(4.0);   check("sqrt(4)",   dbl_near(v, 2.0));
    v = sqrt(2.0);   check("sqrt(2)",   dbl_near(v*v, 2.0));
    v = sqrt(0.0);   check("sqrt(0)",   dbl_near(v, 0.0));
    v = sqrt(1.0);   check("sqrt(1)",   dbl_near(v, 1.0));

    /* =============== fabs =============== */
    check("fabs(-3.5)", dbl_near(fabs(-3.5), 3.5));
    check("fabs(3.5)",  dbl_near(fabs(3.5), 3.5));
    check("fabs(0)",    dbl_near(fabs(0.0), 0.0));

    /* =============== trunc =============== */
    check("trunc(3.7)",  dbl_near(trunc(3.7), 3.0));
    check("trunc(-3.7)", dbl_near(trunc(-3.7), -3.0));
    check("trunc(3.0)",  dbl_near(trunc(3.0), 3.0));
    check("trunc(0.5)",  dbl_near(trunc(0.5), 0.0));

    /* =============== ceil =============== */
    check("ceil(3.2)",  dbl_near(ceil(3.2), 4.0));
    check("ceil(-2.3)", dbl_near(ceil(-2.3), -2.0));
    check("ceil(3.0)",  dbl_near(ceil(3.0), 3.0));

    /* =============== floor =============== */
    check("floor(3.7)",  dbl_near(floor(3.7), 3.0));
    check("floor(-2.3)", dbl_near(floor(-2.3), -3.0));
    check("floor(3.0)",  dbl_near(floor(3.0), 3.0));

    /* =============== round =============== */
    check("round(3.0)", dbl_near(round(3.0), 3.0));
    check("round(3.5)", dbl_near(round(3.5), 4.0));
    check("round(-3.5)", dbl_near(round(-3.5), -4.0));
    check("round(0.4)", dbl_near(round(0.4), 0.0));

    /* =============== fmod =============== */
    check("fmod(10,3)",  dbl_near(fmod(10.0, 3.0), 1.0));
    check("fmod(5,2.5)", dbl_near(fmod(5.0, 2.5), 0.0));

    /* =============== sin =============== */
    check("sin(0)",     dbl_near(sin(0.0), 0.0));
    check("sin(pi/2)",  dbl_near(sin(1.57079632679), 1.0));
    check("sin(pi)",    dbl_near(sin(3.14159265359), 0.0));

    /* =============== cos =============== */
    check("cos(0)",     dbl_near(cos(0.0), 1.0));
    check("cos(pi/2)",  dbl_near(cos(1.57079632679), 0.0));
    check("cos(pi)",    dbl_near(cos(3.14159265359), -1.0));

    /* =============== tan =============== */
    check("tan(0)",    dbl_near(tan(0.0), 0.0));
    check("tan(pi/4)", dbl_near(tan(0.78539816339), 1.0));

    /* =============== exp =============== */
    check("exp(0)", dbl_near(exp(0.0), 1.0));
    check("exp(1)", dbl_near(exp(1.0), 2.71828182846));

    /* =============== log =============== */
    check("log(1)",  dbl_near(log(1.0), 0.0));
    check("log(e)",  dbl_near(log(2.71828182846), 1.0));

    /* =============== log10 =============== */
    check("log10(1)",   dbl_near(log10(1.0), 0.0));
    check("log10(100)", dbl_near(log10(100.0), 2.0));

    /* =============== pow =============== */
    check("pow(2,3)",   dbl_near(pow(2.0, 3.0), 8.0));
    check("pow(5,0)",   dbl_near(pow(5.0, 0.0), 1.0));
    check("pow(2,-1)",  dbl_near(pow(2.0, -1.0), 0.5));
    check("pow(0,0)",   dbl_near(pow(0.0, 0.0), 1.0));

    /* =============== atan =============== */
    check("atan(0)",   dbl_near(atan(0.0), 0.0));
    check("atan(1)",   dbl_near(atan(1.0), 0.78539816339));

    /* =============== atan2 =============== */
    check("atan2(1,0)", dbl_near(atan2(1.0, 0.0), 1.57079632679));
    check("atan2(0,1)", dbl_near(atan2(0.0, 1.0), 0.0));

    /* =============== isqrt =============== */
    check("isqrt(0)",   isqrt(0) == 0);
    check("isqrt(1)",   isqrt(1) == 1);
    check("isqrt(16)",  isqrt(16) == 4);
    check("isqrt(100)", isqrt(100) == 10);

    __printf("-------------------------\n");
    __printf("结果: %d/%d 通过\n", passed, total);
    return total - passed;  /* 失败数作为 exit code，0=全过 */
}
