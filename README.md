# toyc

`toyc` 是 **toyc** 生态的核心组件——一个面向 x86-64 Linux 的自举 C 编译器。
约一万行 C 代码，零外部依赖，零 libc。

源自 [ToyCCompiler](https://github.com/BandieraRosse/ToyCCompiler) 与
[Tinylibc](https://github.com/WHU-SC7/Tinylibc) 两个项目的合并。
toyc 将在此之上发展为一个独立的系统软件生态。

## 这是什么

ToyCCompiler 是一个小型的、独立的 C 编译器套件，包含编译器（toyc）、汇编器（toyas）和链接器（toyld），三者均能自举。

它不使用 LLVM、GCC 或任何已有的编译器基础设施。通过内联 `syscall` 指令直接与 Linux 内核通信。输出为静态链接的 ELF64 可执行文件。全链仅需 `make`，无任何外部依赖。

## 自举链演进

```
gcc                 最初：gcc 编译出能自举的 toyc
 │
 ├──→ stage-1 toyc   toyc 诞生：可以编译自身源码
 │
 ├──→ toyc + gcc ──→ toyas      汇编器加入：toyc 编译 toyas.c，gcc 链接
 │
 ├──→ toyc + toyas ──→ toyld      链接器加入：toyc 编译 toyld.c，toyas 汇编启动代码
 │                           此前 ld 负责所有链接
 │
 └──→ toyld ──→ ld 被替代      toyld 接手所有链接任务
       │                     从此全链零外部依赖，仅需 make
       │
       ↓
  stage-2 toyc + toyas + toyld  ← 自举闭环
       │
       ↓
  stage-3…10 字节级收敛
```

| 阶段 | 编译/汇编/链接 | 说明 |
|------|----------------|------|
| 种子 | gcc 编译 toyc | 初始种子二进制 `bootstrap/toyc` |
| toyas 加入 | toyc 编译 toyas，gcc 链接 | 汇编器 `bootstrap/toyas` 加入 |
| toyld 加入 | toyc + toyas 编译 toyld，ld 链接 | 链接器 `bootstrap/toyld` 加入 |
| toyld 替代 ld | toyc + toyas + toyld 全链自举 | ld 不再需要 |
| stage-2 | toyc + toyas + toyld 编译自身 | 全链自举验证 |
| stage-3…10 | 重复 stage-2 过程 | 字节级收敛 |

## 工具链三件套

| 组件 | 源码 | 功能 |
|------|------|------|
| **toyc** | `compiler/toyc.c` + lex/parse/cgen/… | C 源码 → ELF64 .o |
| **toyas** | `compiler/toyas.c` | x86_64 汇编 → ELF64 .o |
| **toyld** | `compiler/toyld.c` | 多个 .o → ET_EXEC 静态可执行文件 |

## 为什么做这个

C 编译器并不稀缺。GCC 有一千五百万行代码。LLVM 是一个庞大的生态系统。即使是 Fabrice Bellard 的 `toyc` 也比这个庞大十倍。

做这个的原因有两个：

1. **为了看看能不能做到**——用最少的代码行，零依赖、零运行时、零外部帮助，写一个自举的 C 编译器。只有源码、CPU 和 System V ABI。

2. **为了彻底地理解**——你自己写的编译器，你自己就能完全理解。每一个 Bug 都是你的。每一个取舍都是你的。

## 验证

```sh
make test             # 29/29 ✅
make test-selfhost    # 38/38 ✅
make test-source      # 8/8   ✅
make test-toyld         # 38/38 ✅
make test-error       # 16/16 ✅
make test-toyld-self    # 自举收敛 ✅
./bootstrap-to-10.sh  # stage-2→10 字节级一致 ✅
```

全链零外部依赖（仅 make）。自举收敛证明该工具链是自洽的。
