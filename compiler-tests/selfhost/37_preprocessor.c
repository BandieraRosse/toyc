// EXPECT: 0
// 37_preprocessor.c — __LINE__, # stringify, and #if constant expression
//
// 测试三组预处理器功能：
//
// 1) __LINE__ (行 17-20)
//    验证行号从 1 开始递增，在块作用域内也正确跟踪。
//
// 2) # 字符串化 (行 23-52)
//    验证 #define STR(x) #x 将宏参数转换为 "..." 字面量：
//      单 token     STR(hello) → "hello"
//      多 token      STR(x y z) → "x y z"（保留内部空白）
//      字母+数字    STR(a123)  → "a123"
//
// 3) #if 常量表达式 (行 55-行尾)
//    验证预处理器常量表达式求值，每项测试用 #if expr / #else / #endif 结构：
//      基础：     #if 0 跳过， #if 1 进入
//      defined：  有/无括号，!defined
//      宏展开：   宏名→值后参与表达式
//      比较：     == != < > <= >=
//      算术：     + - * / % (含一元 + -)
//      位运算：   | & ^ ~
//      移位：     << >>
//      逻辑：     && || !
//      字符常量： 'A' '\n' ' ' '0'
//      整数基：   0xFF 0777 0x10
//      嵌套：     #if 内的 #if
//      宏展开嵌套
//      未定义/空宏 → 0
//      混合优先级

#define STR(x) #x
#define TEST_MACRO 42
#define EMPTY_MACRO

static void texit(int c) {
    __asm__ __volatile__ ("syscall"
        :: "a"((long)60), "D"((long)c)
        : "rcx", "r11", "memory");
    for (;;) ;
}

void __tlibc_start(void) {
    /* __LINE__ 行号跟踪 */
    if (__LINE__ != 45) texit(1);
    if (__LINE__ != 46) texit(2);
    {
        if (__LINE__ != 48) texit(3);
    }

    /* # 字符串化：单 token */
    {
        char *s = STR(hello);
        if (s[0] != 'h') texit(10);
        if (s[1] != 'e') texit(11);
        if (s[2] != 'l') texit(12);
        if (s[3] != 'l') texit(13);
        if (s[4] != 'o') texit(14);
        if (s[5] != '\0') texit(15);
    }

    /* # 字符串化：多 token（含内部空白） */
    {
        char *s = STR(x y z);
        if (s[0] != 'x') texit(20);
        if (s[1] != ' ') texit(21);
        if (s[2] != 'y') texit(22);
        if (s[3] != ' ') texit(23);
        if (s[4] != 'z') texit(24);
        if (s[5] != '\0') texit(25);
    }

    /* # 字符串化：数字与字母混合 */
    {
        char *s = STR(a123);
        if (s[0] != 'a') texit(30);
        if (s[1] != '1') texit(31);
        if (s[2] != '2') texit(32);
        if (s[3] != '3') texit(33);
        if (s[4] != '\0') texit(34);
    }

    /* ── #if 常量表达式 ── */

    /* 基础：#if 0 跳过 / #if 1 进入 */
#if 0
    texit(100);
#endif
#if 1
    /* enters */
#else
    texit(101);
#endif

    /* defined 运算符：有括号和无括号 */
#if defined(TEST_MACRO)
    /* enters */
#else
    texit(102);
#endif
#if !defined(NONEXISTENT)
    /* enters */
#else
    texit(103);
#endif
#if defined TEST_MACRO
    /* enters */
#else
    texit(104);
#endif
#if !defined NONEXISTENT
    /* enters */
#else
    texit(105);
#endif

    /* 宏展开 + 比较 */
#if TEST_MACRO == 42
    /* enters */
#else
    texit(106);
#endif
#if TEST_MACRO != 0
    /* enters */
#else
    texit(107);
#endif
#if TEST_MACRO > 10 && TEST_MACRO < 100
    /* enters */
#else
    texit(108);
#endif
#if TEST_MACRO >= 42 && TEST_MACRO <= 42
    /* enters */
#else
    texit(109);
#endif

    /* 算术 */
#if (1 + 2) * 3 == 9
    /* enters */
#else
    texit(110);
#endif
#if 10 / 3 == 3
    /* enters */
#else
    texit(111);
#endif
#if 10 % 3 == 1
    /* enters */
#else
    texit(112);
#endif
#if -1 < 0
    /* enters */
#else
    texit(113);
#endif
#if +5 == 5
    /* enters */
#else
    texit(114);
#endif

    /* 位运算 */
#if (1 | 4) == 5
    /* enters */
#else
    texit(115);
#endif
#if (3 & 6) == 2
    /* enters */
#else
    texit(116);
#endif
#if (1 ^ 3) == 2
    /* enters */
#else
    texit(117);
#endif
#if ~0 != 0
    /* enters (~0 是全 1，不等于 0) */
#else
    texit(118);
#endif

    /* 移位 */
#if 1 << 3 == 8
    /* enters */
#else
    texit(119);
#endif
#if 16 >> 2 == 4
    /* enters */
#else
    texit(120);
#endif
#if (1 << 10) == 1024
    /* enters */
#else
    texit(121);
#endif

    /* 逻辑 && || ! */
#if (0 || 1) && 1
    /* enters */
#else
    texit(122);
#endif
#if !!42
    /* enters (!!42 = !0 = 1) */
#else
    texit(123);
#endif
#if !0
    /* enters */
#else
    texit(124);
#endif

    /* 字符常量 */
#if 'A' == 65
    /* enters */
#else
    texit(125);
#endif
#if '\n' == 10
    /* enters */
#else
    texit(126);
#endif
#if ' ' == 32
    /* enters */
#else
    texit(127);
#endif
#if '0' == 48
    /* enters */
#else
    texit(128);
#endif

    /* 各种整数基 */
#if 0xFF == 255
    /* enters */
#else
    texit(129);
#endif
#if 0777 == 511
    /* enters */
#else
    texit(130);
#endif
#if 0x10 == 16
    /* enters */
#else
    texit(131);
#endif

    /* 嵌套 #if */
#if 1
#if 0
    texit(132);  /* 内部 #if 0 */
#endif
#if 1
    /* 内部 #if 1 */
#else
    texit(133);
#endif
#else
    texit(134);  /* 外部 else */
#endif

    /* 宏展开嵌套 */
#define INNER 10
#if INNER + 5 == 15
    /* enters */
#else
    texit(135);
#endif

    /* 未定义标识符 → 0 */
#if UNDEFINED_MACRO
    texit(136);
#endif

    /* 空宏 → 0 */
#if EMPTY_MACRO
    texit(137);
#endif

    /* 复杂表达式混合优先级 */
#if 1 + 2 * 3 == 7
    /* enters */
#else
    texit(138);
#endif
#if 1 == 1 && 2 == 2 || 0
    /* enters（&& 优先级高于 ||） */
#else
    texit(139);
#endif
#if (2 + 3) * 4 == 20
    /* enters */
#else
    texit(140);
#endif

    texit(0);
}
