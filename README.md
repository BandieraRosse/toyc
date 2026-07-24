# toyc

核心是 C 编译器，最初在 [Tinylibc](https://github.com/WHU-SC7/Tinylibc) 项目中发展，
后来独立为 ToyCCompiler 开发至能自举。现在转变为新项目 Toyc，
同时引入了原 Tinylibc 的库和程序，可以编译它们（虽有妥协）。
约一万行 C，零 libc 依赖，已通过完整自举收敛验证。

## 工具链

| 组件 | 源码 | 功能 |
|------|------|------|
| **toyc** | `compiler/toyc.c` + lex/parse/cgen/… | C 源码 → ELF64 .o |
| **toyas** | `compiler/toyas.c` | x86_64 汇编 → ELF64 .o |
| **toyld** | `compiler/toyld.c` | 多个 .o → ET_EXEC 静态可执行文件 |
| **toypp** | `compiler/toypp.c` | 独立预处理器 |
| **toyar** | `compiler/toyar.c` | ar 归档器 |

## 自举链演进

```
gcc                 最初：gcc 编译出能自举的 toyc
 │
 ├──→ stage-1 toyc    toyc 诞生：可以编译自身源码
 │
 ├──→ toyc + gcc ──→ toyas     汇编器加入
 │
 ├──→ toyc + toyas ──→ toyld    链接器加入
 │                         此前 ld 负责所有链接
 │
 └──→ toyld ──→ ld 被替代      toyld 接手所有链接任务
       │                     从此全链零外部依赖，仅需 make
       │
       ↓
  stage-2 toyc + toyas + toyld + toyar  ← 自举闭环
       │
       ↓
  stage-3…10 字节级收敛
```

## 构建

### 编译器工具链

```sh
make                              # 自举构建：bootstrap/{toyc,toyas,toyld,toyar} → build/
make update-bootstrap             # 用 build/ 产物更新 bootstrap/ 种子
make clean                        # 清除 build/
```

### App 构建

项目附带一批自托管应用（`app/` 目录）和一个来自 Tinylibc 的库（`lib/`），可通过名字指定单个 app 构建：

```sh
make lib                          # 构建 libtlibc.a（gcc 编译的 Tinylibc 库）
make app                          # 用 gcc 编译所有 app（shell, tmake）
make app-<name>                   # 用 gcc 编译单个 app
make self-lib                     # 构建自托管 libtlibc.a（toyc 编译）
make self-app                     # 用 toyc 编译所有 app
make self-app-<name>              # 用 toyc 编译单个 app
```

### 测试

```sh
make test                         # 常规测试（43 个）
make test-selfhost                # 自包含测试，无 toyc_rt 依赖（41 个）
make test-source                  # 源文件独立测试（8 个）
make test-toyld                   # toyld 链接测试（41 个）
make test-error                   # 错误报告测试（16 个）
make test-lib-compile             # Tinylibc 库编译检查（28/28 源文件，含 clone.S）
make test-lib                     # Tinylibc 库完整测试（编译 + 功能）
make test-toyld-multifile         # toyld 多 .o 交叉引用链接
make test-toyld-self              # toyld 自举验证（stage-1 → stage-2 字节级一致）
make test-toyar                   # 归档器功能测试（5 个）
make test-toyld-archive           # toyld 从归档链接（2 个）
make test-self-app                # 自托管 App 冒烟测试
make test-all                     # 全部测试套件
```

### 自举收敛验证

```sh
./bootstrap-selfhost.sh           # bootstrap/toyc → stage-2 → 41 测试全部通过
./bootstrap-to-10.sh              # 全链收敛验证（stage-1 → stage-10 字节级一致）
```

## 测试状态（2026-07-24）

| 测试套件 | 通过/总数 | 说明 |
|----------|-----------|------|
| `make test` | **43/43 ✅** | 含 float return test |
| `make test-selfhost` | **41/41 ✅** | toyc 独立编译，无 toyc_rt 依赖 |
| `make test-source` | 8/8 ✅ | toyc 编译源文件独立测试 |
| `make test-toyld` | **41/41 ✅** | selfhost 测试 × toyld 链接 |
| `make test-toyld-self` | **自举收敛 ✅** | toyld 自链接 stage-1→stage-2 字节级一致 |
| `make test-toyar` | **5/5 ✅** | 归档器功能测试 |
| `make test-error` | **16/16 ✅** | 错误报告测试 |
| `make test-lib` | 编译 **28/28** + 功能 **12/12** | math, ctype, string, core, stdio, time, misc, net, poll, tty, procfs, thread 全部通过 |
| `bootstrap-selfhost.sh` | **41/41 ✅** | 种子自举 → stage-2 全部测试通过 |
| `bootstrap-to-10.sh` | stage-2→10 字节级一致 ✅ | 全链收敛验证 |

## 为什么做这个

C 编译器并不稀缺。GCC 有一千五百万行代码。LLVM 是一个庞大的生态系统。

做这个的原因有两个：

1. **为了看看能不能做到**——用最少的代码行，零依赖、零运行时、零外部帮助，写一个自举的 C 编译器。只有源码、CPU 和 System V ABI。

2. **为了彻底地理解**——你自己写的编译器，你自己就能完全理解。每一个 Bug 都是你的。每一个取舍都是你的。

## 验证

```sh
make test             # 43/43 ✅
make test-selfhost    # 41/41 ✅
make test-source      # 8/8 ✅
make test-toyld         # 41/41 ✅
make test-error       # 16/16 ✅
make test-toyld-self    # 自举收敛 ✅
make test-lib-compile # 28/28 ✅
make test-lib         # 编译 28/28 + 功能 12/12 ✅
./bootstrap-to-10.sh  # stage-2→10 字节级完全一致 ✅
make test-all         # 全部通过 ✅
```

全链零外部依赖（仅 make）。自举收敛证明 toyc 是自洽的编译器。
