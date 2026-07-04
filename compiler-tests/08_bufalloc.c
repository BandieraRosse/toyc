// EXPECT: 0
// bufalloc.c — 结构体缓冲区、追加写模式（如 preproc.c OutBuf、cgen.c 代码发射）
typedef struct { char data[16]; int len; } Buf;

static void putc(Buf *b, char c) {
    if (b->len < 16) { b->data[b->len] = c; b->len = b->len + 1; }
}

int main(void) {
    Buf buf;
    buf.len = 0;
    putc(&buf, 'a'); putc(&buf, 'b'); putc(&buf, 'c');
    if (buf.len != 3) return 1;
    if (buf.data[2] != 'c') return 2;

    return 0;
}
