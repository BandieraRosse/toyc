// EXPECT: 0
// switch.c — 多 case、default、fall-through（如 parse.c 的 AST 派发）
static int classify(int tok) {
    switch (tok) {
    case 256: return 1;  /* TOK_INT */
    case 257: return 2;  /* TOK_VOID */
    case 258: return 3;  /* TOK_CHAR */
    case 259: return 4;  /* TOK_SHORT */
    case 260: return 5;  /* TOK_LONG */
    case 261: return 6;  /* TOK_UNSIGNED */
    case 262: return 7;  /* TOK_SIGNED */
    case 263: return 8;  /* TOK_RETURN */
    case 264: return 9;  /* TOK_IF */
    case 265: return 10; /* TOK_ELSE */
    case 300: return 20;
    default:  return 99;
    }
}

int main(void) {
    if (classify(256) != 1)  return 1;
    if (classify(265) != 10) return 2;
    if (classify(300) != 20) return 3;
    if (classify(999) != 99) return 4;
    if (classify(258) != 3)  return 5;

    /* fall-through */
    int x = 1, r = 0;
    switch (x) {
    case 1: r = 10;
    case 2: r = r + 5; break;
    default: r = 0;
    }
    if (r != 15) return 6;

    return 0;
}
