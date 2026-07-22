/* arch_mid.c — 中间函数，调用 leaf_func */
extern int leaf_func(void);

int mid_func(void) {
    return leaf_func() * 6;
}
