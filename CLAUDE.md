# toyc — ToyCCompiler

核心是 C 编译器，最初在 [Tinylibc](https://github.com/WHU-SC7/Tinylibc) 项目中发展，
后来独立为 [ToyCCompiler](https://github.com/BandieraRosse/ToyCCompiler.git) 开发至能自举。现在转变为新项目 Toyc，
同时引入了原 Tinylibc 的库和程序，可以编译它们（虽有妥协）。
约一万行 C，零 libc 依赖。

构建策略：**gcc 编译 toyc 工具链，保证功能正确。**
- `make` — gcc 编译 toyc/toyas/toyld/toyar → `build/`
- 运行时 toyc_rt.c 由 gcc 编译的 toyc 再编译（确保与 toyld 兼容）
- `self-*` 目标用 `build/toyc` 编译 app/，验证 toyc 代码生成能力
- 自举收敛验证（`bootstrap-selfhost.sh` / `bootstrap-to-10.sh`）为可选，不纳入默认流程

## 项目结构

```
├── compiler/           # 编译器源码
│   ├── toyc.c           # 主入口：编译 C → ELF .o
│   ├── toyld.c           # x86_64 静态链接器
│   ├── toyc_rt.c        # 独立运行时（syscall 包装、malloc、printf）
│   ├── toyc_rt_start.S  # 启动汇编 __tlibc_start → main → exit
│   ├── lex.c           # 词法分析
│   ├── parse.c         # 递归下降解析
│   ├── preproc.c       # 预处理器（宏展开、#include、条件编译）
│   ├── cgen.c          # 代码生成（函数、流程控制）
│   ├── cgen_expr.c     # 表达式代码生成
│   ├── cgen_asm.c      # __asm__ 内联汇编
│   ├── elf_write.c     # ELF64 .o 文件写入
│   ├── elf_write.h     # ELF 写入器接口
│   ├── toyc.h           # 编译器核心类型定义
│   ├── toypp.c           # 独立预处理器
│   ├── toyas.c           # x86_64 汇编器
│   └── toyar.c           # ar 归档器
├── llm/                 # GPT-2 模型学习项目（基于 Karpathy llm.c）
│   ├── llm.h             # 公共头文件：float 数学包装、tanh/GELU、softmax、RNG
│   ├── gpt2.h            # GPT-2 模型类型（config、params）和 API
│   ├── gpt2.c            # 各层 forward 实现 + 完整模型前向传播
│   └── main.c            # 测试驱动（合成数据，无需 checkpoint）
├── include/
│   ├── toyc_need.h      # 最小化类型/常量/系统调用宏/函数声明
│   ├── core.h           # Tinylibc 核心头文件
│   ├── elf.h           # ELF64 结构体定义
│   ├── posix/           # POSIX 兼容头文件
│   └── tlibc/           # Tinylibc 专用头文件
├── lib/                 # Tinylibc 库源码（math, stdio, string, core 等 16 个模块）
├── arch/                # 架构相关头文件
├── compiler-tests/     # 测试文件
│   ├── basic/          # 常规测试（toyc 编译 + toyc_rt 链接，43 个）
│   ├── selfhost/       # 自包含测试（toyc 独立编译，无 toyc_rt 依赖，41 个）
│   ├── source/         # 源文件独立测试（验证单个 .c 文件的逻辑，8 个）
│   ├── toyld/            # toyld 多文件链接测试
│   ├── pending/        # 待修复 bug 的复现用例
│   └── lib/            # Tinylibc 库编译兼容性测试
│       ├── libs.mk     #   声明式元数据（源文件、依赖、测试驱动）
│       ├── override/   #   toyc 不兼容头文件遮蔽（目前为空，builtin 已填补）
│       ├── test_*.c    #   各 lib 的功能测试驱动
│       └── ...
├── bootstrap/          # 自举种子（toyc + toyas + toyld + toyar 二进制，git 追踪）
│   └── README.md
├── Makefile            # 构建系统（默认用 bootstrap/toyc + bootstrap/toyas + bootstrap/toyld + bootstrap/toyar）
├── bootstrap-selfhost.sh  # 自举自托管测试
└── bootstrap-to-10.sh     # 全链收敛验证
```

## 构建

### 编译器工具链

```sh
make                              # gcc 编译 toyc 套件 → build/{toyc,toyas,toyld,toyar}
make clean                        # 清除 build/
```

### App 构建

项目附带一批自托管应用（`app/` 目录）和一个来自 Tinylibc 的库（`lib/`），可通过名字指定单个 app 构建：

```sh
make lib                          # 构建 libtlibc.a（gcc 编译的 Tinylibc 库）
make app                          # 用 gcc 编译所有 app（shell, tmake）
make app-shell                    # 用 gcc 编译单个 app，按名字指定
make app-tmake
make self-lib                     # 自托管 libtlibc.a（build/toyc 编译）
make self-app                     # 用 build/toyc 编译所有 app
make self-app-shell               # 用 build/toyc 编译单个 app，按名字指定
make self-app-tmake
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
make llm                          # 编译 llm/（GPT-2 模型，gcc + Tinylibc）
make test-llm                     # llm 功能测试（29/29 ✅，合成数据）
make test-all                     # 全部测试套件（含 llm）
```

### 自举收敛验证

```sh
./bootstrap-selfhost.sh           # bootstrap/toyc → stage-2 → 41 测试全部通过
./bootstrap-to-10.sh              # 全链收敛验证（stage-1 → stage-10 字节级一致）
```

全链零外部依赖（仅 make）。`bootstrap/{toyc,toyas,toyld,toyar}` 是 git 追踪的种子二进制。

## 测试状态（2026-07-24）

| 测试套件 | 通过/总数 | 说明 |
|----------|-----------|------|
| `make test` | **44/44 ✅** | 含 VLA 测试 |
| `make test-selfhost` | **42/42 ✅** | 含 VLA 自包含测试 |
| `make test-source` | 8/8 ✅ | toyc 编译源文件独立测试 |
| `make test-toyld` | **41/41 ✅** | selfhost 测试 × toyld 链接 |
| `make test-toyld-multifile` | ✅ | 多 .o 文件交叉引用链接 |
| `make test-toyld-self` | **自举收敛 ✅** | toyld 自链接 stage-1→stage-2 字节级一致 |
| `make test-toyar` | **5/5 ✅** | 归档器功能测试 |
| `make test-toyld-archive` | **2/2 ✅** | toyld 从归档链接 |
| `make test-error` | **16/16 ✅** | 错误报告测试 |
| `make test-lib-compile` | **28/28 ✅** | Tinylibc 全部 16 个模块编译通过（含 thread + clone.S 汇编） |
| `make test-lib` | 编译 28/28 ✅ 功能 **12/12** | math ✅, ctype ✅, string ✅, core ✅, stdio ✅, time ✅, misc ✅, net ✅, poll ✅, tty ✅, procfs ✅, **thread ✅** |
| `bootstrap-selfhost.sh` | **42/42 ✅** | 种子自举 → stage-2 全部测试通过（含 VLA） |
| `bootstrap-to-10.sh` | stage-2→10 字节级一致 ✅ | 全链收敛验证（头尾完整测试） |
| `make llm` | **编译 ✅** | GPT-2 模型（gcc + Tinylibc，见下方说明） |
| `make test-llm` | **29/29 ✅** | Softmax, GELU, LayerNorm, MatMul, Encoder, 完整 GPT-2 前向 |

### Tinylibc 库测试详情（`make test-lib`）

| 模块          | 源文件              | 编译       | 功能测试                                      |
|---------------|---------------------|------------|-----------------------------------------------|
| math          | `math/math.c`       | ✅         | ✅ 49/49 全部通过                             |
| ctype         | `ctype.c`           | ✅         | ✅ 全部通过                                    |
| string        | `string.c`          | ✅         | ✅ 61/61 全部通过                              |
| core          | 6 个源文件          | ✅         | ✅ 9/9 全部通过                               |
| stdio         | 3 个源文件          | ✅         | ✅ 33/33 全部通过（va_arg %f bug 已修复）     |
| time          | `time.c`            | ✅         | ✅ 51/51 全部通过                             |
| misc          | 5 个源文件          | ✅         | ✅ 19 项通过（含 getdents64 文件计数） |
| net           | 2 个源文件          | ✅         | ✅ 全部通过（已修复 uint16_t typedef is_unsigned、返回值截断和 0xFFFFFFFFU 比较） |
| poll          | `poll.c`            | ✅         | ✅ 11 项通过（pipe + epoll）                  |
| tty           | `tty.c`             | ✅         | ✅ 全部通过（错误路径 + cursor 输出）          |
| procfs        | `procfs.c`          | ✅         | ✅ 20 项全部通过 |
| evdev_kbd     | `evdev_kbd.c`       | ✅         | —                                             |
| evdev_mouse   | `evdev_mouse.c`     | ✅         | —                                             |
| audio         | `audio/alsa.c`      | ✅         | —                                             |
| **thread**    | `thread/pthread.c` + `thread/clone.S` | ✅ (toyc + toyas) | ✅ 16 项全部通过（create, join, self, equal） |
| **总计**      | **28 个源文件**     | **28/28 ✅** | **12/12 ✅**                                  |

## 设计原则

- **无 libc 依赖**：运行时通过 `syscall` 宏直接调用 Linux 内核
- **零外部依赖**：自举种子 `bootstrap/{toyc,toyas,toyld,toyar}` 全链自编译，仅需 `make`
- **简化优先**：源码写法向 toyc 自身能编译的方向靠拢
- **自举导向**：所有决策围绕"让 toyc 能编译自己"展开

## Tinylibc 库测试架构

`compiler-tests/lib/` 测试 toyc 编译内部 `lib/` 中 Tinylibc 库源文件的能力。

### 工作原理

1. **源文件来自项目 `lib/` 目录** — Tinylibc 的完整库移植到项目内部，与编译器同仓库
2. **include 路径使用项目自身 `include/`** — 基于 `TINYLIBC_DIR := .`（项目根），自动使用 `include/`, `include/posix/`, `include/tlibc/`, `arch/`, `arch/x86_64/`
3. **`compiler-tests/lib/override/`** — 目前为空（`__builtin_huge_val` 已由 toyc 直接支持），保留作为遮蔽备用
4. **`compiler-tests/lib/libs.mk`** — 声明式元数据，被 Makefile include

### 添加新 lib

在 `compiler-tests/lib/libs.mk` 中追加：

```makefile
LIBS := ... xxx

_SRCS_xxx    := path/to/source.c       # 相对于 lib/
_DEPS_xxx    := core string            # 链接时依赖的其他 lib（可选）
_TEST_xxx    := test_xxx               # 测试驱动 basename（可选）
_ASM_xxx     := path/to/file.S         # 汇编源文件（可选，由 toyas 编译）
```

框架自动：
- 发现 `$(LIBS)` 中所有模块
- 编译每个模块的所有 .c 源文件（toyc）和 .S 源文件（toyas）
- 若定义了 `_TEST_xxx`，编译测试驱动 → 链接依赖 → 运行

### 测试层级

| 层级 | 命令 | 内容 |
|------|------|------|
| **编译检查** | `make test-lib-compile` | toyc 编译每个 lib 的每个 .c 文件，toyas 汇编 .S 文件，验证无语法/语义错误 |
| **功能测试** | `make test-lib` | 编译 + 链接依赖 + 运行 test driver，检查 EXPECT 退出码 |

### 已修复的已知限制

- ~~`stdio/printf.c` 和 `stdio/snprintf.c` 使用 `__builtin_va_arg` 导致 segfault~~ → **已修复**，stdio 功能测试 33/33 通过
- ~~`time` 测试因 2D 数组代码生成 bug 只能测 mon=0 日期和数字 strftime~~ → **已修复**，全部 51/51 通过
- ~~`math` 函数精度约 24/49 通过~~ → **已修复**，全部 49/49 通过（`__builtin_huge_val` 已由 toyc 直接支持）
- ~~`string` 编译在 toyc 下有限制项~~ → **已修复**，61/61 全部通过

### 剩余已知限制

- `string.c` 的 `strerror` 内部调用 `snprintf`，创建了 stdio ↔ string 的循环依赖（但功能测试中的 strerror(0-40) 返回静态字符串，无需 snprintf，测试独立通过）
- `init/` 含汇编 `start.S`，暂不纳入自托管编译
- `graphics/` 含 SDL/OpenGL 绑定，暂不纳入

### toyc 已支持

| 特性 | 说明 |
|------|------|
| 浮点运算 | ✅ 完整支持 float(32-bit) 和 double(64-bit)，SSE 指令无条件启用 |
| 全局 float/double 花括号初始化 | ✅ `float arr[] = {1.0f, 2.0f}` 等 |
| `%f` 格式化 | ✅ toyc_rt.c 运行时支持 |
| `__func__` 预定义标识符 | ✅ C99 标准支持 |
| `__builtin_va_arg` | ✅ 已修复，stdio 功能测试 33/33 通过 |

### 暂不支持

| 特性 | 说明 |
|------|------|
| VLA（变长数组） | ✅ `int arr[n]` 运行时栈分配、元素访问、`sizeof` 运行时求值<br/>⚠ 限制：仅支持一维 VLA（完整）；多维 `int arr[n][m]` 仅支持首维为运行时表达式<br/>❌ goto 跨 VLA 声明未检测（C99 约束） |
| `char (*)[N]` 指针转数组访问 | ❌ `files[i]` 被当作 `char**`（取指针）而非地址偏移（取元素），需用平坦指针+手动偏移 |
| 位域（bitfield） | ❌ 未实现 |
| 复合字面量 `(int[]){1,2}` | ❌ 未实现 |
| — | (已实现 ✅) |
| `_Generic` | ❌ 未实现 |
| `long double` | ❌ 不支持 |
| `goto` 跨函数 | ❌ 未检查 |
| 宽字符/宽字符串 | ❌ 未实现 |
| `-I` include 路径、`-MD` 依赖追踪 | ❌ 静默忽略（toyc 参数解析极简） |


## 验证

```sh
make test             # 44/44 ✅
make test-selfhost    # 42/42 ✅
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
