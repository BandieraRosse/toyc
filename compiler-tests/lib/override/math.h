/* override/math.h — tcc 兼容的 math.h
 *
 * Tinylibc 的 include/posix/math.h 使用 __builtin_huge_val()
 * 定义 HUGE_VAL，tcc 不支持。本文件提供等价的常数定义。
 *
 * 通过 -I 路径优先级覆盖真实 math.h：
 *   -Icompiler-tests/lib/override -I../Tinylibc/include/posix ...
 */

#ifndef _MATH_H
#define _MATH_H

#define HUGE_VAL    1.7976931348623157e+308

#define M_E         2.71828182845904523536
#define M_LOG2E     1.44269504088896340736
#define M_LOG10E    0.43429448190325182765
#define M_LN2       0.69314718055994530942
#define M_LN10      2.30258509299404568402
#define M_PI        3.14159265358979323846
#define M_PI_2      1.57079632679489661923
#define M_PI_4      0.78539816339744830962
#define M_1_PI      0.31830988618379067154
#define M_2_PI      0.63661977236758134308
#define M_2_SQRTPI  1.12837916709551257390
#define M_SQRT2     1.41421356237309504880
#define M_SQRT1_2   0.70710678118654752440

double sqrt(double x);
double fabs(double x);
double ceil(double x);
double floor(double x);
double round(double x);
double trunc(double x);
double fmod(double x, double y);
double sin(double x);
double cos(double x);
double tan(double x);
double atan(double x);
double atan2(double y, double x);
double exp(double x);
double log(double x);
double log10(double x);
double pow(double x, double y);
long long isqrt(long long n);

#endif /* _MATH_H */
