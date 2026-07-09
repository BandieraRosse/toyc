// EXPECT: 0
// sizeof_pending.c — sizeof(struct) 尾部填充计算错误
//
// BUG: tcc 对包含数组成员 + 后续 int 成员的 struct 计算 sizeof 时，
//      得到错误结果。例如 {char[16]; int;} 正确的 sizeof 是 20，
//      但 tcc 返回 48。
//
// 影响：
//   - malloc(sizeof(struct)) 多分配（浪费但不崩溃）
//   - 数组步长错误：arr[i+1] 可能落在错误位置
//   - x86_64 ABI 不兼容
//
// 自举影响：低。tcc 自己的 struct 布局内部一致，
//           自身源码未触发此 bug 路径。

typedef struct { char data[16]; int len; } Buf;

int main(void) {
    /* 正确的 sizeof 应为 20（int 按 4 对齐，无需尾填充） */
    if (sizeof(Buf) != 20) return 31;

    /* 数组步长检查 */
    Buf arr[2];
    char *p0 = (char *)&arr[0];
    char *p1 = (char *)&arr[1];
    if (p1 - p0 != sizeof(Buf)) return 32;

    /* 成员偏移检查 */
    Buf b;
    char *base = (char *)&b;
    int *lp = &b.len;
    int offset = (char *)lp - base;
    if (offset != 16) return 33;

    return 0;
}
