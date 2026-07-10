# tcc — ToyCCompiler

从 [Tinylibc](https://github.com/WHU-SC7/Tinylibc) 项目提取、独立发展的 C 编译器。
约一万行 C，零 libc 依赖，已通过完整自举验证。

## 项目结构

```
├── app/                # 编译器源码
│   ├── tcc.c           # 主入口：编译 C → ELF .o
│   └── tld.c           # x86_64 静态链接器（新增）
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
│   ├── basic/          # 常规测试（tcc 编译 + tcc_rt 链接，29 个）
│   ├── selfhost/       # 自包含测试（tcc 独立编译，无 tcc_rt 依赖，35 个）
│   ├── source/         # 源文件独立测试（验证单个 .c 文件的逻辑，8 个）
│   ├── tld/            # tld 多文件链接测试
│   └── pending/        # 待修复 bug 的复现用例
├── bootstrap/          # 自举种子（tcc + tas + tld 二进制，git 追踪）
│   └── README.md
├── Makefile            # 构建系统（默认用 bootstrap/tcc + bootstrap/tas + bootstrap/tld）
├── bootstrap-selfhost.sh  # 自举自托管测试
└── bootstrap-to-10.sh     # 全链收敛验证
```

## 构建

```sh
make                              # 自举构建（bootstrap/tcc + bootstrap/tas + bootstrap/tld）
make test                         # 常规测试（29 个）
make test-selfhost                # 自包含测试（35 个）
make test-source                  # 源文件独立测试（8 个）
make test-tld                     # tld 链接测试（35 个）
make test-tld-multifile           # tld 多文件链接测试
make test-tld-self                # tld 自举验证（stage-1 → stage-2 → 收敛）
make update-bootstrap             # 用最新 build/ 产物更新 bootstrap/ 种子
./bootstrap-selfhost.sh           # 自举测试：bootstrap/tcc → stage-2 → 35 测试
./bootstrap-to-10.sh              # 全链收敛验证（stage-1 → stage-10）
make clean
```

全链零外部依赖（仅 make）。`bootstrap/tcc` + `bootstrap/tas` + `bootstrap/tld` 是 git 追踪的种子二进制。

## 测试状态（2026-07-10）

| 测试套件 | 通过/总数 | 说明 |
|----------|-----------|------|
| `make test` | 29/29 ✅ | tcc 编译 + tld 链接 tcc_rt 运行时 |
| `make test-selfhost` | **36/36 ✅** | tcc 独立编译，无 tcc_rt 依赖 |
| `make test-source` | 8/8 ✅ | tcc 编译源文件独立测试 |
| `make test-tld` | **36/36 ✅** | selfhost 测试 × tld 链接 |
| `make test-tld-multifile` | ✅ | 多 .o 文件交叉引用链接 |
| `make test-tld-self` | **自举收敛 ✅** | tld 自链接 stage-1→stage-2 字节级一致 |
| `bootstrap-selfhost.sh` | 36/36 ✅ | 种子自举 → stage-2 全部测试通过 |
| `bootstrap-to-10.sh` | stage-5→10 字节级一致 ✅ | 全链收敛验证（头尾完整测试） |

## 设计原则

- **无 libc 依赖**：运行时通过 `syscall` 宏直接调用 Linux 内核
- **零外部依赖**：自举种子 `bootstrap/{tcc,tas,tld}` 全链自编译，仅需 `make`
- **简化优先**：源码写法向 tcc 自身能编译的方向靠拢
- **自举导向**：所有决策围绕"让 tcc 能编译自己"展开

## 已知限制

| 特性 | 状态 |
|------|------|
| 浮点运算 | 默认未启用（`make CFLAGS+=-DTCC_FLOAT` 开启） |
| VLA（变长数组） | 未实现 |
| 位域（bitfield） | 未实现 |
| 复合字面量 `(int[]){1,2}` | 未实现 |
| 指定初始化器 `.field=val` | 未实现 |
| `_Generic` | 未实现 |
| `long double` | 不支持 |
| `goto` 跨函数 | 未检查 |
| 宽字符/宽字符串 | 未实现 |
| `-I` include 路径、`-MD` 依赖追踪 | 静默忽略（tcc 参数解析极简） |

### 已修复的历史限制

以下问题曾阻断自举，现已修复，持续回归验证：

| 问题 | 修复 |
|------|------|
| `*main()` 的 `return` 代码生成缺陷 | 基本测试全过 |
| 变参超过 5 个传参错误 | `selfhost/11_printf_manyargs.c` |
| `*(ptr + N)` 指针嵌套解引用按 1 字节加载 | `selfhost/20_ptr_arith.c` |
| `*(cast_type *)ptr = val` 存储宽度错误 | `selfhost/16_cast_deref_assign.c` |
| 结构体返回值数组元素只复制 8 字节 | `selfhost/29_struct_return_expr.c` |
| `Lexer` 结构体复制只复制 8 字节（自举阻断 bug） | 收敛验证 |
| `add_rel()` int→Elf64_Sxword 符号扩展缺失（2026-07-09） | `app/tas.c` 参数改为 64 位 + `-4LL` |
| int→long 符号扩展缺失（2026-07-10） | `app/cgen.c` + `app/cgen_expr.c` AST_VAR/AST_CONSTANT/BINOP 条件 + cgen_return + 全局/指针/数组成员/结构体成员赋值路径 |

## 验证

```sh
make test             # 29/29 ✅
make test-selfhost    # 36/36 ✅
make test-source      # 8/8 ✅
make test-tld         # 36/36 ✅
make test-tld-self    # 自举收敛 ✅
./bootstrap-to-10.sh  # stage-2→10 字节级完全一致 ✅
```

全链零外部依赖（仅 make）。自举收敛证明 tcc 是自洽的编译器。
