# tcc — Tinylibc C 编译器（独立版）

从 [Tinylibc](https://github.com/WHU-SC7/Tinylibc) 项目中提取的 C 编译器，目标是用自身编译自己（自举）。

## 项目结构

```
├── app/                # 编译器源码
│   ├── tcc.c           # 主入口：编译 C → ELF .o
│   ├── tcc_rt.c        # 独立运行时（syscall 包装、malloc、printf）
│   ├── tcc_rt_start.S  # 启动汇编 __tlibc_start → main → exit
│   ├── lex.c           # 词法分析
│   ├── parse.c         # 递归下降解析
│   ├── preproc.c       # 预处理器（宏展开、#include、条件编译）
│   ├── cgen.c          # 代码生成（函数、流程控制）
│   ├── cgen_expr.c     # 表达式代码生成
│   ├── cgen_asm.c      # __asm__ 内联汇编
│   ├── elf_write.c     # ELF64 .o 文件写入
│   ├── elf_write.h     # ELF 写入器接口
│   ├── tcc.h           # 编译器核心类型定义
│   ├── tpp.c           # 独立预处理器
│   └── tas.c           # x86_64 汇编器
├── include/
│   ├── tcc_need.h      # 最小化类型/常量/系统调用宏/函数声明
│   └── elf.h           # ELF64 结构体定义
├── compiler-tests/     # 测试文件
├── ld.script           # 链接脚本
└── Makefile            # 构建系统
```

## 构建

```sh
make               # 构建 tcc、tpp、tas → build/
make test          # 运行常规测试
make test 03       # 运行指定编号测试
make test-selfhost # 自举测试（tcc 独自编译，不依赖运行时对象）
make clean
```

输出在 `build/`：
- `build/tcc` — C 编译器
- `build/tpp` — 预处理器
- `build/tas` — x86_64 汇编器
- `build/*.o` — 运行时对象（测试链接用）

## 设计原则

- **无 libc 依赖**：运行时（tcc_rt.c）通过 `syscall` 宏直接调用 Linux 内核
- **静态链接**：所有输出 ELF64 .o 文件，通过 `ld -nostdlib -static` 链接
- **简化优先**：源码写法向 tcc 自身能编译的方向妥协，不自找麻烦
- **自举导向**：所有决策围绕"让 tcc 能编译自己"展开

## 目标

让 tcc 能完整编译自身。

## 已知限制（编写 selfhost 测试必读）

tcc 尚未完备实现 C 标准。编写自举测试（`compiler-tests/selfhost/`）时
必须避开的陷阱：

### 🔥 会导致崩溃的硬限制

| 限制 | 说明 | 绕过方式 |
|------|------|----------|
| **函数 epilogue 不可靠** | `main()` 的 return 语句代码生成有 bug，从 main 返回会崩溃 | 用 `sys_exit(N)` 替代 `return N`；入口函数设为 `void __tlibc_start(void)` 直接调 `sys_exit` |
| **变参参数 ≤ 5 个** | 超过 5 个变参（fmt + 5 个参数以上）会读出错误值 | 限制每次调用 ≤ 6 参数（含 fmt） |
| **无浮点（默认构建）** | `make` 默认不启用浮点支持（`-DTCC_FLOAT`） | 用整数运算代替；double 变量声明能用但运算符未实现 |
| **无 libc** | 不能链接标准库，`printf`/`malloc` 等不可用 | 用自包含的 `sys_write` + `print_dec`/`print_hex` |
| **内联汇编约束有限** | `__asm__` 支持基本约束但不支持复杂 reload | 用简单 `"=a"(var)` / `"a"(val)` 约束，避免复杂拼接 |

### ⚠️ 类型系统注意点

| 问题 | 说明 |
|------|------|
| **指针嵌套解引用** | `*(p+1)` 中的 `p+1` 不是直接变量引用，代码生成器查不到 `elem_size` 和 `elem_is_unsigned`，解引用会按默认 1 字节处理。**始终用 `p[i]` 下标语法** |
| **struct 标签解析顺序** | 前向引用的 struct 可能在布局计算时尚未注册，导致成员偏移计算失败 |

### 📝 语法/语义不支持

| 特性 | 原因 |
|------|------|
| VLA（变长数组） | 未实现 |
| 位域（bitfield） | 未实现 |
| 复合字面量 `(int[]){1,2}` | 未实现 |
| 指定初始化器 `.field=val` | 未实现 |
| `_Generic` | 未实现 |
| `inline` 关键字 | 解析但语义忽略 |
| 宽字符/宽字符串 | 未实现 |
| `long double` | 降级为 double |
| 函数返回 struct 或 union | 未实现，寄存器溢出处理不完整 |
| `goto` 跨函数 | 未检查 |

### ✅ 安全的 selfhost 测试模板

参照现有测试 `01_totally_selfcontained.c` 的结构：

```c
// EXPECT: 0       ← make test 期望的退出码
// SELF_CONTAINED  ← 标记无运行时依赖

static long sys_write(int fd, const char *buf, unsigned long len) {
    unsigned long ret;
    __asm__ __volatile__ ("syscall"
        : "=a"(ret) : "a"(1), "D"((long)fd), "S"((long)buf), "d"((long)len)
        : "rcx", "r11", "memory");
    return (long)ret;
}
static void sys_exit(int code) {
    __asm__ __volatile__ ("syscall"
        : : "a"((long)60), "D"((long)code)
        : "rcx", "r11", "memory");
    for (;;) ;
}
// 用 sys_exit(0/非0) 结束，绝不 return
// 变参调用保持在 5 个值以内
void __tlibc_start(void) {
    /* 测试逻辑 ... */
    sys_exit(0);  /* pass */
    sys_exit(1);  /* fail */
}
```

### 测试顺序建议

新增 selfhost 测试时，从最简单的开始验证，逐步增加复杂度：

1. 常量加载与算术（01）
2. 负数与符号扩展（02）
3. 变参调用（03）
4. 无符号类型语义（04）
5. 复杂表达式和指针运算
