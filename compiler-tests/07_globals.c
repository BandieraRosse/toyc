// EXPECT: 0
// globals.c — 全局数组/变量、static、extern（如 cgen.c 全局缓冲区）
static int label_ids[64];
static int label_count;

static int new_label(void) {
    int id = label_count;
    label_ids[label_count++] = id;
    return id;
}

static void set_label(int id) {
    int i;
    for (i = 0; i < label_count; i = i + 1)
        if (label_ids[i] == id) return;
    label_ids[label_count++] = id;
}

static int code_buf[256];
static int code_size;

static void emit(int b) {
    code_buf[code_size++] = b & 0xFF;
}

int global_val;

int main(void) {
    if (new_label() != 0) return 1;
    if (new_label() != 1) return 2;
    set_label(5);
    if (label_count != 3) return 3;

    emit(0x90); emit(0xC3);
    if (code_size != 2) return 4;
    if (code_buf[1] != 0xC3) return 5;

    global_val = 42;
    if (global_val != 42) return 6;

    return 0;
}
