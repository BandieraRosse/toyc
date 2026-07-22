// EXPECT: 0
// float_extended.c — float/double 进阶功能
// 覆盖：局部 float/double 数组、数组赋值、指针解引用与赋值、
//       指针算术、struct float/double 成员（含 -> 访问）、
//       for(float/double) 循环、复合赋值（+= -= *= /=）、嵌套三元

struct F { int tag; float f; double d; short extra; };

int main(void) {
    /* 1. float 指针解引用 + 赋值 */
    { float x = 5.5f; float *fp = &x;
      int t = *fp * 10; if (t != 55) return 1;
      *fp = 42; int u = *fp; if (u != 42) return 2; }

    /* 2. float 指针算术 */
    { float fa[3] = {100.0f, 200.0f, 300.0f}; float *fp = fa;
      if (*(fp + 1) != 200) return 3;
      if (*(fp + 2) != 300) return 4; }

    /* 3. 局部 float 数组 */
    { float la[3] = {1.5f, 2.5f, 3.5f};
      if ((int)(la[0] * 10) != 15) return 5;
      la[0] = 10.0f; if ((int)la[0] != 10) return 6;
      la[1] = 20;    if ((int)la[1] != 20) return 7; }

    /* 4. 局部 double 数组 */
    { double ld[2] = {3.14, 2.71};
      if ((int)(ld[0] * 100) != 314) return 8;
      ld[1] = 99; if ((int)ld[1] != 99) return 9; }

    /* 5. struct float/double 成员 */
    { struct F s; s.f = 1.5f; s.d = 2.5;
      if ((int)(s.f * 10) != 15) return 10;
      s.f = 99; if ((int)s.f != 99) return 11;
      s.d = 4.0; if ((int)s.d != 4) return 12; }

    /* 6. struct 指针 -> 成员 */
    { struct F s; s.f = 6.28f; struct F *ps = &s;
      if ((int)(ps->f * 100) != 628) return 13;
      ps->d = 9.99; if ((int)(ps->d * 100) != 999) return 14;
      ps->f = 7; if ((int)ps->f != 7) return 15; }

    /* 7. for(float) 循环 */
    { int c = 0; for (float f = 0.0f; f < 3.0f; f += 1.0f) c++;
      if (c != 3) return 16; }

    /* 8. for(double) 循环 */
    { int c = 0; for (double d = 0.0; d < 3.0; d += 1.0) c++;
      if (c != 3) return 17; }

    /* 9. float 复合赋值 */
    { float ca = 5.0f; ca += 3.0f; if ((int)ca != 8) return 18;
      ca -= 2.0f; if ((int)ca != 6) return 19;
      ca *= 3.0f; if ((int)ca != 18) return 20;
      ca /= 4.0f; if ((int)ca != 4) return 21; }

    /* 10. double 复合赋值 */
    { double da = 10.0; da += 5.0; if ((int)da != 15) return 22;
      da -= 3.0; if ((int)da != 12) return 23;
      da *= 2.0; if ((int)da != 24) return 24;
      da /= 5.0; if ((int)da != 4) return 25; }

    /* 11. 嵌套三元含 float */
    { float r = (1 ? (2 ? 4.0f : 8.0f) : 0.0f); if ((int)r != 4) return 26;
      r = (0 ? 3.0f : (1 ? 5.0f : 7.0f)); if ((int)r != 5) return 27; }

    /* 12. int → float 转换 */
    { float x = 42; if ((int)x != 42) return 28; }

    return 0;
}
