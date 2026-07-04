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
