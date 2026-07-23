/* Bug 1 & 2 测试：静态 2D 数组初始化 + 访问 */
/* 文件作用域 + 函数内 static */

static const int month_days[2][12] = {
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
    {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
};

static const int flat[24] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/* 2D + 1D 共存：用于检测地址重叠 */
static const int extra[4] = {100, 200, 300, 400};

static int check_month_days(void) {
    int i, j;
    /* 检查每个月的天数 */
    for (j = 0; j < 12; j++) {
        int expected_noleap = 31;
        if (j == 1) expected_noleap = 28;       /* Feb */
        else if (j == 3 || j == 5 || j == 8 || j == 10) expected_noleap = 30;
        if (month_days[0][j] != expected_noleap) {
            __write(2, "FAIL: month_days[0][", 20);
            { char _bb[8]; int _nv = j; int _ii = 0; do { _bb[_ii++] = '0' + _nv % 10; _nv /= 10; } while (_nv > 0); int _jj; for (_jj = 0; _jj < _ii / 2; _jj++) { char _t = _bb[_jj]; _bb[_jj] = _bb[_ii - 1 - _jj]; _bb[_ii - 1 - _jj] = _t; } _bb[_ii] = ']'; __write(2, _bb, _ii + 1); }
            __write(2, " = ", 3);
            { char _bb[16]; int _nv = month_days[0][j]; int _ii = 0; int _neg = 0; if (_nv < 0) { _neg = 1; _nv = -_nv; } do { _bb[_ii++] = '0' + _nv % 10; _nv /= 10; } while (_nv > 0); if (_neg) _bb[_ii++] = '-'; int _jj; for (_jj = 0; _jj < _ii / 2; _jj++) { char _t = _bb[_jj]; _bb[_jj] = _bb[_ii - 1 - _jj]; _bb[_ii - 1 - _jj] = _t; } _bb[_ii] = '\n'; __write(2, _bb, _ii + 1); }
            return 0;
        }
    }
    for (j = 0; j < 12; j++) {
        int expected_leap = 31;
        if (j == 1) expected_leap = 29;
        else if (j == 3 || j == 5 || j == 8 || j == 10) expected_leap = 30;
        if (month_days[1][j] != expected_leap) {
            __write(2, "FAIL: month_days[1][", 20);
            { char _bb[8]; int _nv = j; int _ii = 0; do { _bb[_ii++] = '0' + _nv % 10; _nv /= 10; } while (_nv > 0); int _jj; for (_jj = 0; _jj < _ii / 2; _jj++) { char _t = _bb[_jj]; _bb[_jj] = _bb[_ii - 1 - _jj]; _bb[_ii - 1 - _jj] = _t; } _bb[_ii] = ']'; __write(2, _bb, _ii + 1); }
            __write(2, " = ", 3);
            { char _bb[16]; int _nv = month_days[1][j]; int _ii = 0; int _neg = 0; if (_nv < 0) { _neg = 1; _nv = -_nv; } do { _bb[_ii++] = '0' + _nv % 10; _nv /= 10; } while (_nv > 0); if (_neg) _bb[_ii++] = '-'; int _jj; for (_jj = 0; _jj < _ii / 2; _jj++) { char _t = _bb[_jj]; _bb[_jj] = _bb[_ii - 1 - _jj]; _bb[_ii - 1 - _jj] = _t; } _bb[_ii] = '\n'; __write(2, _bb, _ii + 1); }
            return 0;
        }
    }
    return 1;
}

static int check_flat(void) {
    int j;
    for (j = 0; j < 24; j++) {
        int expected = 31;
        if (j >= 12) {
            /* second year: leap */
            if (j == 12+1) expected = 29;
            else if (j == 12+3 || j == 12+5 || j == 12+8 || j == 12+10) expected = 30;
            else if (j != 12+0 && j != 12+2 && j != 12+4 && j != 12+6 && j != 12+7 && j != 12+9 && j != 12+11) expected = 30;
        } else {
            /* first year: no leap */
            if (j == 1) expected = 28;
            else if (j == 3 || j == 5 || j == 8 || j == 10) expected = 30;
        }
        if (flat[j] != expected) {
            __write(2, "FAIL: flat[", 11);
            { char _bb[8]; int _nv = j; int _ii = 0; do { _bb[_ii++] = '0' + _nv % 10; _nv /= 10; } while (_nv > 0); int _jj; for (_jj = 0; _jj < _ii / 2; _jj++) { char _t = _bb[_jj]; _bb[_jj] = _bb[_ii - 1 - _jj]; _bb[_ii - 1 - _jj] = _t; } _bb[_ii] = ']'; __write(2, _bb, _ii + 1); }
            __write(2, " = ", 3);
            { char _bb[16]; int _nv = flat[j]; int _ii = 0; int _neg = 0; if (_nv < 0) { _neg = 1; _nv = -_nv; } do { _bb[_ii++] = '0' + _nv % 10; _nv /= 10; } while (_nv > 0); if (_neg) _bb[_ii++] = '-'; int _jj; for (_jj = 0; _jj < _ii / 2; _jj++) { char _t = _bb[_jj]; _bb[_jj] = _bb[_ii - 1 - _jj]; _bb[_ii - 1 - _jj] = _t; } _bb[_ii] = '\n'; __write(2, _bb, _ii + 1); }
            return 0;
        }
    }
    return 1;
}

static int check_extra(void) {
    int j;
    int vals[4] = {100, 200, 300, 400};
    for (j = 0; j < 4; j++) {
        if (extra[j] != vals[j]) {
            __write(2, "FAIL: extra[", 12);
            { char _bb[8]; int _nv = j; int _ii = 0; do { _bb[_ii++] = '0' + _nv % 10; _nv /= 10; } while (_nv > 0); int _jj; for (_jj = 0; _jj < _ii / 2; _jj++) { char _t = _bb[_jj]; _bb[_jj] = _bb[_ii - 1 - _jj]; _bb[_ii - 1 - _jj] = _t; } _bb[_ii] = ']'; __write(2, _bb, _ii + 1); }
            __write(2, " = ", 3);
            { char _bb[16]; int _nv = extra[j]; int _ii = 0; int _neg = 0; if (_nv < 0) { _neg = 1; _nv = -_nv; } do { _bb[_ii++] = '0' + _nv % 10; _nv /= 10; } while (_nv > 0); if (_neg) _bb[_ii++] = '-'; int _jj; for (_jj = 0; _jj < _ii / 2; _jj++) { char _t = _bb[_jj]; _bb[_jj] = _bb[_ii - 1 - _jj]; _bb[_ii - 1 - _jj] = _t; } _bb[_ii] = '\n'; __write(2, _bb, _ii + 1); }
            return 0;
        }
    }
    return 1;
}

/* 本地 static 2D 数组测试（Bug 2） */
static int check_local_static(void) {
    static const int local_months[2][12] = {
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
        {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
    };
    int j;
    for (j = 0; j < 12; j++) {
        int expected = 31;
        if (j == 1) expected = 28;
        else if (j == 3 || j == 5 || j == 8 || j == 10) expected = 30;
        if (local_months[0][j] != expected) {
            __write(2, "FAIL: local_months[0][...]\n", 27);
            return 0;
        }
    }
    return 1;
}

int main(void) {
    int ok = 1;
    ok = ok && check_month_days();
    ok = ok && check_flat();
    ok = ok && check_extra();
    ok = ok && check_local_static();
    if (ok) {
        __write(1, "OK: all 2D array tests passed\n", 31);
        return 0;
    }
    __write(2, "FAIL\n", 5);
    return 1;
}
