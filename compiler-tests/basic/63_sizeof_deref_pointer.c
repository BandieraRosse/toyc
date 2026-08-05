// EXPECT: 0
/* sizeof 解引用表达式只检查指针指向的类型，不应执行实际的空指针解引用。 */
struct large_value {
    int words[64];
};

static int check_parameters(struct large_value *sp, int *ip, char *cp, int **ipp)
{
    if (sizeof(*sp) != 256) return 6;
    if (sizeof(*ip) != 4) return 7;
    if (sizeof(*cp) != 1) return 8;
    if (sizeof(*ipp) != 8) return 9;
    return 0;
}

int main(void)
{
    struct large_value *sp = (struct large_value *)0;
    int *ip = (int *)0;
    char *cp = (char *)0;
    int **ipp = (int **)0;
    int rc;

    if (sizeof(*sp) != sizeof(struct large_value)) return 1;
    if (sizeof(*sp) != 256) return 2;
    if (sizeof(*ip) != 4) return 3;
    if (sizeof(*cp) != 1) return 4;
    if (sizeof(*ipp) != 8) return 5;
    rc = check_parameters(sp, ip, cp, ipp);
    if (rc) return rc;
    return 0;
}
