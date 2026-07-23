// EXPECT: 0
// elf_write.c standalone test (simulated preprocessed state)
//
// Compile (gcc):
//   gcc -nostdlib -ffreestanding -Wall -Wextra -Wl,-e,__tlibc_start
//       compiler-tests/source/test_elf_write.c -o /tmp/test_elf_write
// Run:  /tmp/test_elf_write
//
// Self-host (future):
//   build/toyc compiler-tests/source/test_elf_write.c -o /tmp/tew.o
//   ld -nostdlib -static -T ld.script /tmp/tew.o -o /tmp/tew
//   /tmp/tew

// ============================================================
// Inlined from toyc_need.h — 基础类型 / syscall 宏 / 常量
// ============================================================

typedef unsigned long           size_t;
typedef long                    ptrdiff_t;
typedef long                    off_t;
typedef unsigned int            mode_t;

typedef signed char             int8_t;
typedef unsigned char           uint8_t;
typedef signed short int        int16_t;
typedef unsigned short int      uint16_t;
typedef signed int              int32_t;
typedef unsigned int            uint32_t;
typedef signed long int         int64_t;
typedef unsigned long int       uint64_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

/* x86_64 syscall 多参数派发 */
static inline long __syscall0(long n) {
    unsigned long ret;
    __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n) : "rcx", "r11", "memory");
    return (long)ret;
}
static inline long __syscall1(long n, long a1) {
    unsigned long ret;
    __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
    return (long)ret;
}
static inline long __syscall2(long n, long a1, long a2) {
    unsigned long ret;
    __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2)
                          : "rcx", "r11", "memory");
    return (long)ret;
}
static inline long __syscall3(long n, long a1, long a2, long a3) {
    unsigned long ret;
    __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2),
                          "d"(a3) : "rcx", "r11", "memory");
    return (long)ret;
}
static inline long __syscall4(long n, long a1, long a2, long a3, long a4) {
    unsigned long ret;
    __asm__ __volatile__ (
        "mov %5, %%r10\n\tsyscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(a4)
        : "rcx", "r11", "r10", "memory");
    return (long)ret;
}

#define __scc(X) ((long)(X))

/* 文件操作常量 */
#define AT_FDCWD    (-100)
#define O_RDONLY    0
#define O_WRONLY    1
#define O_CREAT     0100
#define O_TRUNC     01000
#define SEEK_SET    0

#define syscall0(n)       __syscall0(n)
#define syscall1(n, a)    __syscall1(n, __scc(a))
#define syscall2(n, a, b) __syscall2(n, __scc(a), __scc(b))
#define syscall3(n,a,b,c) __syscall3(n, __scc(a), __scc(b), __scc(c))
#define syscall4(n,a,b,c,d) __syscall4(n, __scc(a), __scc(b), __scc(c), __scc(d))

// ============================================================
// Inlined from elf.h — ELF64 类型、结构体、宏
// ============================================================

typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef  int32_t Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef  int64_t Elf64_Sxword;
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;

#define ELF64_R_SYM(i)       ((i) >> 32)
#define ELF64_R_TYPE(i)      ((Elf64_Word)(i))
#define ELF64_R_INFO(s, t)   ((((Elf64_Xword)(s)) << 32) | ((t) & 0xffffffffUL))
#define ELF64_ST_INFO(b, t)  (((b) << 4) | ((t) & 0xf))
#define ELF64_ST_BIND(i)     ((i) >> 4)
#define ELF64_ST_TYPE(i)     ((i) & 0xf)

typedef struct {
    Elf64_Addr   r_offset;
    Elf64_Xword  r_info;
    Elf64_Sxword r_addend;
} Elf64_Rela;

// ============================================================
// Inlined from elf_write.h — ElfWriteSym + extern 声明
// ============================================================

#define ELF_MAX_SYMS 8192
#define ELF_MAX_RELS 16384
#define ELF_CODE_BUF_SIZE 262144

typedef struct {
    const char *name;
    int offset;
    int size;
    int is_global;
    int is_func;
    int shndx;
    int sym_idx;
} ElfWriteSym;

unsigned char elf_code_buf[ELF_CODE_BUF_SIZE];
int elf_code_size;
ElfWriteSym elf_syms[ELF_MAX_SYMS];
int elf_sym_count;
Elf64_Rela elf_rels[ELF_MAX_RELS];
int elf_rel_count;
int elf_bss_size;

// ============================================================
// 运行时 stub — 替代 toyc_rt.c 的功能
// ============================================================

static void __sys_exit(int code) {
    __asm__ __volatile__ ("syscall"
        : : "a"((long)60), "D"((long)code)
        : "rcx", "r11", "memory");
    for (;;) ;
}

/* ─── 文件 I/O 实现（使用 raw syscall） ─── */

int __openat(int dirfd, const char *path, int flags, int mode) {
    return (int)syscall4(257, dirfd, path, flags, mode);
}

long __write(int fd, const void *buf, size_t len) {
    return syscall3(1, fd, buf, len);
}

int __close(int fd) {
    return (int)syscall1(3, fd);
}

long __read(int fd, void *buf, size_t count) {
    return syscall3(0, fd, buf, count);
}

/* ─── 简易输出工具 ─── */

static void print_str(const char *s) {
    int n = 0;
    while (s[n]) n++;
    __write(1, s, n);
}

static void print_dec(long n) {
    char buf[32];
    int i = 0;
    if (n < 0) { print_str("-"); n = -n; }
    if (n == 0) { buf[i++] = '0'; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) { __write(1, &buf[--i], 1); }
}

// ============================================================
// elf_write.c 源文件 — 完全来自 compiler/elf_write.c
// ============================================================

static int build_shstrtab(unsigned char *buf, const char *names[], int n) {
    int len = 0;
    buf[len++] = '\0';
    int i;
    for (i = 0; i < n; i++) {
        const char *s = names[i];
        while (*s)
            buf[len++] = *s++;
        buf[len++] = '\0';
    }
    return len;
}

int elf_write_object(const char *path) {
    int num_sections = 7;

    /* ── 构建 .shstrtab ── */
    unsigned char shstrtab_buf[256];
    const char *sec_names[] = {".text", ".rela.text", ".bss", ".symtab", ".strtab", ".shstrtab"};
    int shstrtab_sz = build_shstrtab(shstrtab_buf, sec_names, 6);

    /* ── 构建 .strtab ── */
    #define ELF_STRTAB_SIZE 16384
    #define ELF_MAX_SYM_STR 4096
    static unsigned char strtab_buf[ELF_STRTAB_SIZE];
    int strtab_sz = 0;
    strtab_buf[strtab_sz++] = '\0';
    int sym_str_idx[ELF_MAX_SYM_STR];
    if (elf_sym_count > ELF_MAX_SYM_STR) {
        __write(2, "elf_write: too many symbols\n", 28);
        return -1;
    }
    int i;
    for (i = 0; i < elf_sym_count; i++) {
        sym_str_idx[i] = strtab_sz;
        const char *s = elf_syms[i].name;
        if (!s) s = "";
        int nlen = 0; while (s[nlen]) nlen++;
        if (strtab_sz + nlen + 1 > ELF_STRTAB_SIZE) {
            __write(2, "elf_write: strtab overflow\n", 27);
            return -1;
        }
        while (*s)
            strtab_buf[strtab_sz++] = *s++;
        strtab_buf[strtab_sz++] = '\0';
    }

    /* ── 计算文件偏移 ── */
    int shdr_ofs = 64;
    int text_ofs = (shdr_ofs + num_sections * 64 + 15) & -16;
    int text_sz  = elf_code_size;
    int rela_ofs = (text_ofs + text_sz + 7) & -8;
    int rela_sz  = elf_rel_count * 24;
    int sym_ofs  = (rela_ofs + rela_sz + 7) & -8;
    int sym_sz   = (elf_sym_count + 1) * 24;
    int str_ofs  = sym_ofs + sym_sz;
    int shstr_ofs = str_ofs + strtab_sz;

    /* ── 用静态缓冲区一次性构建完整文件 ── */
    static unsigned char buf[262144];
    int p = 0;
    unsigned char *b = buf;

    /* ELF header */
    b[p++] = 0x7f; b[p++] = 'E'; b[p++] = 'L'; b[p++] = 'F';
    b[p++] = 2;     /* ELFCLASS64 */
    b[p++] = 1;     /* ELFDATA2LSB */
    b[p++] = 1;     /* EV_CURRENT */
    b[p++] = 0;     /* ELFOSABI_NONE */
    b[p++] = 0;     /* padding */
    int ei;
    for (ei = 9; ei < 16; ei++) b[p++] = 0;

    /* e_type, e_machine, e_version */
    b[p++] = 1; b[p++] = 0;       /* ET_REL */
    b[p++] = 62; b[p++] = 0;      /* EM_X86_64 */
    b[p++] = 1; b[p++] = 0; b[p++] = 0; b[p++] = 0;

    /* e_entry (8), e_phoff (8) */
    for (ei = 0; ei < 16; ei++) b[p++] = 0;

    /* e_shoff (8) */
    b[p++] = (shdr_ofs) & 0xFF; b[p++] = (shdr_ofs >> 8) & 0xFF;
    b[p++] = (shdr_ofs >> 16) & 0xFF; b[p++] = (shdr_ofs >> 24) & 0xFF;
    for (ei = 0; ei < 4; ei++) b[p++] = 0;

    /* e_flags (4) */
    for (ei = 0; ei < 4; ei++) b[p++] = 0;

    /* e_ehsize (2), e_phentsize (2) */
    b[p++] = 64; b[p++] = 0;
    b[p++] = 0; b[p++] = 0;

    /* e_phnum (2) */
    b[p++] = 0; b[p++] = 0;

    /* e_shentsize (2) */
    b[p++] = 64; b[p++] = 0;

    /* e_shnum (2) */
    b[p++] = num_sections; b[p++] = 0;

    /* e_shstrndx (2) */
    b[p++] = 6; b[p++] = 0;

    /* ── Section header table (占位) ── */
    int shdr_start = p;
    for (ei = 0; ei < num_sections * 64; ei++) b[p++] = 0;

    /* ── .text data ── */
    while (p < text_ofs) b[p++] = 0;
    for (ei = 0; ei < elf_code_size; ei++) b[p++] = elf_code_buf[ei];

    /* ── 符号索引重映射（局部在前，全局在后）── */
    {
        int elf_idx = 1;
        for (i = 0; i < elf_sym_count; i++)
            if (!elf_syms[i].is_global) elf_syms[i].sym_idx = elf_idx++;
        for (i = 0; i < elf_sym_count; i++)
            if (elf_syms[i].is_global) elf_syms[i].sym_idx = elf_idx++;
        for (int ri = 0; ri < elf_rel_count; ri++) {
            unsigned long r_sym = ELF64_R_SYM(elf_rels[ri].r_info);
            if (r_sym > 0) {
                int cgen_idx = (int)(r_sym - 1);
                if (cgen_idx >= 0 && cgen_idx < elf_sym_count) {
                    unsigned int r_type = ELF64_R_TYPE(elf_rels[ri].r_info);
                    elf_rels[ri].r_info = ELF64_R_INFO(elf_syms[cgen_idx].sym_idx, r_type);
                }
            }
        }
    }

    /* ── .rela.text data ── */
    while (p < rela_ofs) b[p++] = 0;
    for (ei = 0; ei < elf_rel_count; ei++) {
        int ro = elf_rels[ei].r_offset;
        b[p++] = (ro)&0xFF; b[p++] = (ro>>8)&0xFF;
        b[p++] = (ro>>16)&0xFF; b[p++] = (ro>>24)&0xFF;
        for (int z=0;z<4;z++) b[p++] = 0;
        long ri = elf_rels[ei].r_info;
        b[p++] = (ri)&0xFF; b[p++] = (ri>>8)&0xFF;
        b[p++] = (ri>>16)&0xFF; b[p++] = (ri>>24)&0xFF;
        b[p++] = (ri>>32)&0xFF; b[p++] = (ri>>40)&0xFF;
        b[p++] = (ri>>48)&0xFF; b[p++] = (ri>>56)&0xFF;
        long ra = elf_rels[ei].r_addend;
        b[p++] = (ra)&0xFF; b[p++] = (ra>>8)&0xFF;
        b[p++] = (ra>>16)&0xFF; b[p++] = (ra>>24)&0xFF;
        b[p++] = (ra>>32)&0xFF; b[p++] = (ra>>40)&0xFF;
        b[p++] = (ra>>48)&0xFF; b[p++] = (ra>>56)&0xFF;
    }

    /* ── .symtab data ── */
    while (p < sym_ofs) b[p++] = 0;
    int first_global = 1;

    for (ei = 0; ei < 24; ei++) b[p++] = 0;

    #define WRITE_SYM(idx, info, shndx, value, size) \
        do { \
            b[p++] = (idx) & 0xFF; b[p++] = ((idx) >> 8) & 0xFF; \
            b[p++] = ((idx) >> 16) & 0xFF; b[p++] = ((idx) >> 24) & 0xFF; \
            b[p++] = (info); b[p++] = 0; \
            b[p++] = (shndx) & 0xFF; b[p++] = ((shndx) >> 8) & 0xFF; \
            int _v = (value); \
            b[p++] = (_v) & 0xFF; b[p++] = ((_v) >> 8) & 0xFF; \
            b[p++] = ((_v) >> 16) & 0xFF; b[p++] = ((_v) >> 24) & 0xFF; \
            b[p++] = 0; b[p++] = 0; b[p++] = 0; b[p++] = 0; \
            int _sz = (size); \
            b[p++] = (_sz) & 0xFF; b[p++] = ((_sz) >> 8) & 0xFF; \
            b[p++] = ((_sz) >> 16) & 0xFF; b[p++] = ((_sz) >> 24) & 0xFF; \
            b[p++] = 0; b[p++] = 0; b[p++] = 0; b[p++] = 0; \
        } while(0)

    for (i = 0; i < elf_sym_count; i++) {
        if (elf_syms[i].is_global) continue;
        first_global++;
        int bind = 0;
        int type = elf_syms[i].is_func ? 2 : 0;
        int sym_shndx = elf_syms[i].shndx;
        if (sym_shndx == 0) sym_shndx = 0;
        WRITE_SYM(sym_str_idx[i], ELF64_ST_INFO(bind, type),
                  sym_shndx, elf_syms[i].offset, elf_syms[i].size);
    }

    for (i = 0; i < elf_sym_count; i++) {
        if (!elf_syms[i].is_global) continue;
        int bind = 1;
        int type = elf_syms[i].is_func ? 2 : 0;
        int sym_shndx = elf_syms[i].shndx;
        if (sym_shndx == 0) sym_shndx = 0;
        WRITE_SYM(sym_str_idx[i], ELF64_ST_INFO(bind, type),
                  sym_shndx, elf_syms[i].offset, elf_syms[i].size);
    }
    #undef WRITE_SYM

    /* ── .strtab data ── */
    for (ei = 0; ei < strtab_sz; ei++) b[p++] = strtab_buf[ei];

    /* ── .shstrtab data ── */
    for (ei = 0; ei < shstrtab_sz; ei++) b[p++] = shstrtab_buf[ei];

    /* ── 回填节区头表 ── */
    #define SHDR_W4(off, val) \
        do { int _v = (val); b[(off)]=_v&0xFF; b[(off)+1]=(_v>>8)&0xFF; \
             b[(off)+2]=(_v>>16)&0xFF; b[(off)+3]=(_v>>24)&0xFF; } while(0)

    /* 节区 1: .text */
    b[shdr_start+64]=1; b[shdr_start+68]=1; b[shdr_start+72]=6;
    { int off = shdr_start + 64;
      SHDR_W4(off+24, text_ofs); SHDR_W4(off+32, text_sz);
      b[off+48]=16; }

    /* 节区 2: .rela.text */
    b[shdr_start+128]=7; b[shdr_start+132]=4;
    { int off = shdr_start + 128;
      SHDR_W4(off+24, rela_ofs); SHDR_W4(off+32, rela_sz);
      b[off+40]=4;    b[off+44]=1;
      b[off+48]=8; b[off+56]=24; }

    /* 节区 3: .bss */
    b[shdr_start+192]=18; b[shdr_start+196]=8;
    b[shdr_start+200]=3;
    { int off = shdr_start + 192;
      SHDR_W4(off+24, 0);
      SHDR_W4(off+32, elf_bss_size);
      b[off+48]=32; }

    /* 节区 4: .symtab */
    b[shdr_start+256]=23; b[shdr_start+260]=2;
    { int off = shdr_start + 256;
      SHDR_W4(off+24, sym_ofs); SHDR_W4(off+32, sym_sz);
      b[off+40]=5;
      b[off+44]=first_global;
      b[off+48]=8; b[off+56]=24; }

    /* 节区 5: .strtab */
    b[shdr_start+320]=31; b[shdr_start+324]=3;
    { int off = shdr_start + 320;
      SHDR_W4(off+24, str_ofs); SHDR_W4(off+32, strtab_sz);
      b[off+48]=1; }

    /* 节区 6: .shstrtab */
    b[shdr_start+384]=39; b[shdr_start+388]=3;
    { int off = shdr_start + 384;
      SHDR_W4(off+24, shstr_ofs); SHDR_W4(off+32, shstrtab_sz);
      b[off+48]=1; }

    #undef SHDR_W4

    /* ── 写入文件 ── */
    int fd = __openat(AT_FDCWD, path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    int written = __write(fd, buf, p);
    __close(fd);
    return (written == p) ? 0 : -1;
}

// ============================================================
// 测试框架
// ============================================================

static int test_passed = 0;
static int test_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        print_str("  FAIL "); print_str(msg); print_str("\n"); \
        test_failed++; \
    } else { \
        test_passed++; \
    } \
} while (0)

static void run_section(const char *name) {
    print_str("\n--- "); print_str(name); print_str(" ---\n");
    test_passed = 0;
    test_failed = 0;
}

static void print_section_result(void) {
    print_str("  -> ");
    print_dec(test_passed); print_str(" passed, ");
    print_dec(test_failed); print_str(" failed\n");
}

/* 重置全局状态 */
static void reset_globals(void) {
    elf_code_size = 0;
    elf_sym_count = 0;
    elf_rel_count = 0;
    elf_bss_size = 0;
}

/* 从文件读取到 buf，返回字节数 */
static int read_file(const char *path, unsigned char *buf, int buf_size) {
    int fd = __openat(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) return -1;
    int total = 0;
    while (total < buf_size) {
        long n = __read(fd, buf + total, buf_size - total);
        if (n <= 0) break;
        total += (int)n;
    }
    __close(fd);
    return total;
}

/* 从文件中读取 1 字节 */
#define RB(buf, pos) ((unsigned char)(buf)[(pos)])

/* 从文件中读取 LE32 */
#define RL32(buf, pos) ( \
    (unsigned int)(buf)[(pos)] | ((unsigned int)(buf)[(pos)+1] << 8) | \
    ((unsigned int)(buf)[(pos)+2] << 16) | ((unsigned int)(buf)[(pos)+3] << 24) )

/* 从文件中读取 LE16 */
#define RL16(buf, pos) ( (unsigned int)(buf)[(pos)] | ((unsigned int)(buf)[(pos)+1] << 8) )

/* 从文件中读取 LE64 */
#define RL64(buf, pos) ( \
    (unsigned long)(buf)[(pos)] | ((unsigned long)(buf)[(pos)+1] << 8) | \
    ((unsigned long)(buf)[(pos)+2] << 16) | ((unsigned long)(buf)[(pos)+3] << 24) | \
    ((unsigned long)(buf)[(pos)+4] << 32) | ((unsigned long)(buf)[(pos)+5] << 40) | \
    ((unsigned long)(buf)[(pos)+6] << 48) | ((unsigned long)(buf)[(pos)+7] << 56) )

// ============================================================
// ELF 写入器测试用例
// ============================================================

/* 1. 最小 ELF：只写入几个字节的 .text */
static void test_minimal_elf(void) {
    run_section("Minimal ELF");

    reset_globals();
    elf_code_buf[0] = 0xB8;  /* mov eax, 42 */
    elf_code_buf[1] = 42;
    elf_code_buf[2] = 0x00;
    elf_code_buf[3] = 0x00;
    elf_code_size = 4;

    int ret = elf_write_object("/tmp/elf_test_min.o");
    CHECK(ret == 0, "elf_write_object returns 0");

    /* 读回验证 */
    unsigned char elf[1024];
    int len = read_file("/tmp/elf_test_min.o", elf, sizeof(elf));
    CHECK(len > 64, "file is larger than ELF header");

    /* ELF magic */
    CHECK(RB(elf, 0) == 0x7F, "magic[0] = 0x7F");
    CHECK(RB(elf, 1) == 'E',  "magic[1] = 'E'");
    CHECK(RB(elf, 2) == 'L',  "magic[2] = 'L'");
    CHECK(RB(elf, 3) == 'F',  "magic[3] = 'F'");

    /* ELF header fields */
    CHECK(RB(elf, 4) == 2,    "ELFCLASS64");
    CHECK(RB(elf, 5) == 1,    "ELFDATA2LSB");
    CHECK(RL16(elf, 16) == 1, "e_type = ET_REL");
    CHECK(RL16(elf, 18) == 62,"e_machine = EM_X86_64");
    CHECK(RL16(elf, 58) == 64,"e_shentsize = 64");
    CHECK(RL16(elf, 60) == 7, "e_shnum = 7 sections");
    CHECK(RL16(elf, 62) == 6, "e_shstrndx = 6");

    /* .text section (section 1, offset shdr_start+64) */
    int shdr_ofs = (int)RL64(elf, 40);
    CHECK(shdr_ofs == 64, "shoff at 64 (no phdr for ET_REL)");

    int text_shdr = shdr_ofs + 64;
    CHECK(RL32(elf, text_shdr) == 1,    ".text sh_name = 1");
    CHECK(RL32(elf, text_shdr+4) == 1,  ".text sh_type = SHT_PROGBITS");
    CHECK(RL32(elf, text_shdr+24) > 0,  ".text sh_offset > 0");
    CHECK(RL32(elf, text_shdr+32) == 4, ".text sh_size = 4 (our code)");

    /* 验证 .text 内容 */
    int text_off = (int)RL32(elf, text_shdr+24);
    CHECK(RB(elf, text_off) == 0xB8, ".text[0] = 0xB8 (mov eax)");
    CHECK(RB(elf, text_off+1) == 42,  ".text[1] = 42");
}

/* 2. 含符号表 */
static void test_with_symbols(void) {
    run_section("With Symbols");

    reset_globals();
    elf_code_buf[0] = 0xC3;  /* ret */
    elf_code_size = 1;
    elf_syms[0].name = "my_func";
    elf_syms[0].offset = 0;
    elf_syms[0].size = 1;
    elf_syms[0].is_global = 1;
    elf_syms[0].is_func = 1;
    elf_syms[0].shndx = 1;  /* .text */
    elf_sym_count = 1;

    int ret = elf_write_object("/tmp/elf_test_sym.o");
    CHECK(ret == 0, "write with symbol OK");

    unsigned char elf[2048];
    int len = read_file("/tmp/elf_test_sym.o", elf, sizeof(elf));
    CHECK(len > 0, "read back OK");

    /* 检查 symtab section */
    int shdr_ofs = (int)RL64(elf, 40);
    int sym_shdr = shdr_ofs + 64*4;  /* section 4 */
    CHECK(RL32(elf, sym_shdr) == 23,   ".symtab sh_name=23");
    CHECK(RL32(elf, sym_shdr+4) == 2,  ".symtab sh_type=SHT_SYMTAB");
    int sym_off = (int)RL32(elf, sym_shdr+24);
    int sym_sz  = (int)RL32(elf, sym_shdr+32);
    CHECK(sym_off > 0, ".symtab offset > 0");
    CHECK(sym_sz >= 24, ".symtab size >= one entry (24 bytes)");

    /* null symbol (first 24 bytes, all zeros) */
    /* second symbol (our function) */
    int sym2 = sym_off + 24;
    int str_idx = (int)RL32(elf, sym2);
    CHECK(str_idx > 0, "symbol name index > 0");
    int sym_info = RB(elf, sym2+4);
    CHECK(ELF64_ST_BIND(sym_info) == 1, "STB_GLOBAL");
    CHECK(ELF64_ST_TYPE(sym_info) == 2, "STT_FUNC");
    CHECK(RL16(elf, sym2+6) == 1, "sym st_shndx = .text");
    CHECK(RB(elf, sym2+8) == 0, "sym st_value low byte = 0");

    /* 检查 strtab: 应包含 "my_func" */
    int str_shdr = shdr_ofs + 64*5;  /* section 5 */
    int str_off = (int)RL32(elf, str_shdr+24);
    int ok = 1;
    { const char *exp = "my_func"; int si;
      for (si = 0; exp[si]; si++)
        if (RB(elf, str_off + str_idx + si) != (unsigned char)exp[si]) ok = 0; }
    CHECK(ok, "strtab contains 'my_func' at expected index");
}

/* 3. 含重定位 */
static void test_with_relocations(void) {
    run_section("With Relocations");

    reset_globals();
    /* 函数：call my_func 的占位 */
    elf_code_buf[0] = 0xE8;  /* call rel32 */
    elf_code_buf[1] = 0x00;
    elf_code_buf[2] = 0x00;
    elf_code_buf[3] = 0x00;
    elf_code_buf[4] = 0x00;
    elf_code_size = 5;

    /* 被调用的函数符号 */
    elf_syms[0].name = "target_func";
    elf_syms[0].offset = 0;
    elf_syms[0].size = 1;
    elf_syms[0].is_global = 1;
    elf_syms[0].is_func = 1;
    elf_syms[0].shndx = 0;  /* SHN_UNDEF (extern) */
    elf_sym_count = 1;

    /* 重定位：R_X86_64_PC32 调用 target_func */
    elf_rels[0].r_offset = 1;  /* 在 call 指令的操作数字段 */
    elf_rels[0].r_info = ELF64_R_INFO(1, 2);  /* sym=1, R_X86_64_PC32=2 */
    elf_rels[0].r_addend = -4;  /* PC32 惯例 */
    elf_rel_count = 1;

    int ret = elf_write_object("/tmp/elf_test_rel.o");
    CHECK(ret == 0, "write with reloc OK");

    unsigned char elf[2048];
    int len = read_file("/tmp/elf_test_rel.o", elf, sizeof(elf));
    CHECK(len > 0, "read back OK");

    int shdr_ofs = (int)RL64(elf, 40);

    /* .rela.text section (section 2) */
    int rela_shdr = shdr_ofs + 64*2;
    CHECK(RL32(elf, rela_shdr) == 7,    ".rela.text sh_name=7");
    CHECK(RL32(elf, rela_shdr+4) == 4,  ".rela.text sh_type=SHT_RELA");
    CHECK(RL32(elf, rela_shdr+32) == 24, ".rela.text size = 24 (one entry)");

    /* readelf 验证（仅在 host 存在时）*/
    /* 直接检查 rela entry */
    int rela_off = (int)RL32(elf, rela_shdr+24);
    unsigned long r_info = RL64(elf, rela_off + 8);
    CHECK(ELF64_R_TYPE(r_info) == 2,   "rela type = R_X86_64_PC32");
    CHECK(ELF64_R_SYM(r_info) == 1,    "rela sym index = 1 (adjusted)");
}

/* 4. BSS section */
static void test_with_bss(void) {
    run_section("With BSS");

    reset_globals();
    elf_code_buf[0] = 0xC3;  /* ret */
    elf_code_size = 1;
    elf_bss_size = 256;

    int ret = elf_write_object("/tmp/elf_test_bss.o");
    CHECK(ret == 0, "write with BSS OK");

    unsigned char elf[2048];
    read_file("/tmp/elf_test_bss.o", elf, sizeof(elf));

    int shdr_ofs = (int)RL64(elf, 40);
    int bss_shdr = shdr_ofs + 64*3;  /* section 3 */
    CHECK(RL32(elf, bss_shdr) == 18,    ".bss sh_name=18");
    CHECK(RL32(elf, bss_shdr+4) == 8,   ".bss sh_type=SHT_NOBITS");
    CHECK(RL32(elf, bss_shdr+32) == 256, ".bss sh_size=256");
}

/* 5. 多符号：局部 + 全局 */
static void test_local_and_global(void) {
    run_section("Local + Global Symbols");

    reset_globals();
    elf_code_buf[0] = 0xC3;
    elf_code_size = 1;

    /* 局部符号 */
    elf_syms[0].name = "local_helper";
    elf_syms[0].offset = 0;
    elf_syms[0].size = 1;
    elf_syms[0].is_global = 0;
    elf_syms[0].is_func = 1;
    elf_syms[0].shndx = 1;
    /* 全局符号 */
    elf_syms[1].name = "global_entry";
    elf_syms[1].offset = 0;
    elf_syms[1].size = 1;
    elf_syms[1].is_global = 1;
    elf_syms[1].is_func = 1;
    elf_syms[1].shndx = 1;
    elf_sym_count = 2;

    int ret = elf_write_object("/tmp/elf_test_lg.o");
    CHECK(ret == 0, "write with local+global OK");

    unsigned char elf[2048];
    read_file("/tmp/elf_test_lg.o", elf, sizeof(elf));

    int shdr_ofs = (int)RL64(elf, 40);
    int sym_shdr = shdr_ofs + 64*4;
    int sym_off = (int)RL32(elf, sym_shdr+24);
    int first_global_field = (int)RL32(elf, sym_shdr+44);

    /* 局部符号先出现，然后是全局符号 */
    /* null(0), local(1), global(2) → first_global = 2 */
    CHECK(first_global_field == 2, "first_global = 2 (null + local)");

    /* 验证局部符号是 STB_LOCAL */
    int sym_local = sym_off + 24;  /* 第 1 个符号（跳过 null） */
    CHECK(ELF64_ST_BIND(RB(elf, sym_local+4)) == 0, "local sym bind = STB_LOCAL");

    /* 验证全局符号是 STB_GLOBAL */
    int sym_global = sym_off + 48;  /* 第 2 个符号 */
    CHECK(ELF64_ST_BIND(RB(elf, sym_global+4)) == 1, "global sym bind = STB_GLOBAL");
}

/* 6. 空 ELF（无代码，无符号） */
static void test_empty_elf(void) {
    run_section("Empty ELF");

    reset_globals();
    int ret = elf_write_object("/tmp/elf_test_empty.o");
    CHECK(ret == 0, "write empty ELF OK");

    unsigned char elf[1024];
    int len = read_file("/tmp/elf_test_empty.o", elf, sizeof(elf));
    CHECK(len > 64, "file has header");

    /* 验证 .text size = 0 */
    int shdr_ofs = (int)RL64(elf, 40);
    int text_shdr = shdr_ofs + 64;
    CHECK(RL32(elf, text_shdr+32) == 0, ".text size = 0");
}

// ============================================================
// 入口
// ============================================================

void __tlibc_start(void) {
    print_str("=== elf_write.c standalone tests ===\n");

    test_minimal_elf();
    print_section_result();

    test_with_symbols();
    print_section_result();

    test_with_relocations();
    print_section_result();

    test_with_bss();
    print_section_result();

    test_local_and_global();
    print_section_result();

    test_empty_elf();
    print_section_result();

    /* 汇总 */
    print_str("\n=== ");
    if (test_failed == 0) {
        print_str("ALL PASSED");
    } else {
        print_str("SOME FAILED");
    }
    print_str(" ===\n");

    __sys_exit(test_failed != 0 ? 1 : 0);
}
