# toyc — ToyCCompiler

从 [Tinylibc](https://github.com/WHU-SC7/Tinylibc) 项目提取、独立发展的 C 编译器。
约一万行 C，零 libc 依赖，已通过完整自举验证。

## 项目结构

```
├── compiler/           # 编译器源码
│   ├── toyc.c           # 主入口：编译 C → ELF .o
│   └── toyld.c           # x86_64 静态链接器（新增）
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
│   └── toyas.c           # x86_64 汇编器
├── include/
│   ├── toyc_need.h      # 最小化类型/常量/系统调用宏/函数声明
│   └── elf.h           # ELF64 结构体定义
├── compiler-tests/     # 测试文件
│   ├── basic/          # 常规测试（toyc 编译 + toyc_rt 链接，36 个）
│   ├── selfhost/       # 自包含测试（toyc 独立编译，无 toyc_rt 依赖，40 个）
│   ├── source/         # 源文件独立测试（验证单个 .c 文件的逻辑，8 个）
│   ├── toyld/            # toyld 多文件链接测试
│   ├── pending/        # 待修复 bug 的复现用例
│   └── lib/            # Tinylibc 库编译兼容性测试
│       ├── libs.mk     #   声明式元数据（源文件、依赖、测试驱动）
│       ├── override/   #   toyc 不兼容头文件遮蔽（目前为空，builtin 已填补）
│       ├── test_*.c    #   各 lib 的功能测试驱动
│       └── ...
├── bootstrap/          # 自举种子（toyc + toyas + toyld 二进制，git 追踪）
│   └── README.md
├── Makefile            # 构建系统（默认用 bootstrap/toyc + bootstrap/toyas + bootstrap/toyld）
├── bootstrap-selfhost.sh  # 自举自托管测试
└── bootstrap-to-10.sh     # 全链收敛验证
```

## 构建

```sh
make                              # 自举构建（bootstrap/toyc + bootstrap/toyas + bootstrap/toyld）
make test                         # 常规测试（36 个）
make test-selfhost                # 自包含测试（40 个）
make test-source                  # 源文件独立测试（8 个）
make test-toyld                     # toyld 链接测试（40 个）
make test-error                   # 错误报告测试（16 个）
make test-lib-compile             # Tinylibc 库编译检查（28/28 源文件，含 clone.S 汇编）
make test-lib                     # Tinylibc 库完整测试（编译 + 功能）
make test-toyld-multifile           # toyld 多文件链接测试
make test-toyld-self                # toyld 自举验证（stage-1 → stage-2 → 收敛）
make update-bootstrap             # 用最新 build/ 产物更新 bootstrap/ 种子
./bootstrap-selfhost.sh           # 自举测试：bootstrap/toyc → stage-2 → 40 测试
./bootstrap-to-10.sh              # 全链收敛验证（stage-1 → stage-10）
make clean
```

全链零外部依赖（仅 make）。`bootstrap/toyc` + `bootstrap/toyas` + `bootstrap/toyld` 是 git 追踪的种子二进制。

## 测试状态（2026-07-23）

| 测试套件 | 通过/总数 | 说明 |
|----------|-----------|------|
| `make test` | **36/36 ✅** | 含 float return test |
| `make test-selfhost` | **40/40 ✅** | toyc 独立编译，无 toyc_rt 依赖 |
| `make test-source` | 8/8 ✅ | toyc 编译源文件独立测试 |
| `make test-toyld` | **40/40 ✅** | selfhost 测试 × toyld 链接 |
| `make test-toyld-multifile` | ✅ | 多 .o 文件交叉引用链接 |
| `make test-toyld-self` | **自举收敛 ✅** | toyld 自链接 stage-1→stage-2 字节级一致 |
| `make test-error` | **16/16 ✅** | 错误报告测试 |
| `make test-lib-compile` | **28/28 ✅** | Tinylibc 全部 16 个模块编译通过（含 thread + clone.S 汇编） |
| `make test-lib` | 编译 28/28 ✅ 功能 **12/12** | math ✅, ctype ✅, string ✅, core ✅, stdio ✅, time ✅, misc ✅, net ✅, poll ✅, tty ✅, procfs ✅, **thread ✅** |
| `bootstrap-selfhost.sh` | **40/40 ✅** | 种子自举 → stage-2 全部测试通过 |
| `bootstrap-to-10.sh` | stage-2→10 字节级一致 ✅ | 全链收敛验证（头尾完整测试） |

### Tinylibc 库测试详情（`make test-lib`）

| 模块          | 源文件              | 编译       | 功能测试                                      |
|---------------|---------------------|------------|-----------------------------------------------|
| math          | `math/math.c`       | ✅         | ✅ 全部通过                                   |
| ctype         | `ctype.c`           | ✅         | ✅ 全部通过                                    |
| string        | `string.c`          | ✅         | ✅ 全部通过（跳过 toyc codegen 限制项）       |
| core          | 6 个源文件          | ✅         | ✅ 全部通过（跳过 3 项）                      |
| stdio         | 3 个源文件          | ✅         | ✅ 全部通过                                    |
| time          | `time.c`            | ✅         | ✅ 41 项通过（避开 2D 数组 bug，仅测 mon=0 日期和数字 strftime） |
| misc          | 5 个源文件          | ✅         | ✅ 16 项通过（path.c, file.c, 避开 mkdirat 依赖） |
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
- **零外部依赖**：自举种子 `bootstrap/{toyc,toyas,toyld}` 全链自编译，仅需 `make`
- **简化优先**：源码写法向 toyc 自身能编译的方向靠拢
- **自举导向**：所有决策围绕"让 toyc 能编译自己"展开

## Tinylibc 库测试架构

`compiler-tests/lib/` 测试 toyc 编译真实 Tinylibc 库源文件的能力。

### 工作原理

1. **源文件直接来自 `../Tinylibc/lib/`** — 无物理拷贝，始终与上游一致
2. **include 路径使用真实的 Tinylibc 头文件** — `-I../Tinylibc/include/posix` 等
3. **`compiler-tests/lib/override/`** — 目前为空（`__builtin_huge_val` 已由 toyc 直接支持），保留作为遮蔽备用
4. **`compiler-tests/lib/libs.mk`** — 声明式元数据，被 Makefile include

### 添加新 lib

在 `compiler-tests/lib/libs.mk` 中追加：

```makefile
LIBS := ... xxx

_SRCS_xxx    := path/to/source.c       # 相对于 ../Tinylibc/lib/
_DEPS_xxx    := core string            # 链接时依赖的其他 lib（可选）
_TEST_xxx    := test_xxx               # 测试驱动 basename（可选）
```

框架自动：
- 发现 `$(LIBS)` 中所有模块
- 编译每个模块的所有源文件
- 若定义了 `_TEST_xxx`，编译测试驱动 → 链接依赖 → 运行

### 测试层级

| 层级 | 命令 | 内容 |
|------|------|------|
| **编译检查** | `make test-lib-compile` | toyc 编译每个 lib 的每个 .c 文件，验证无语法/语义错误 |
| **功能测试** | `make test-lib` | 编译 + 链接依赖 + 运行 test driver，检查 EXPECT 退出码 |

### 已知限制

- `stdio/printf.c` 和 `stdio/snprintf.c` 使用 `__builtin_va_arg`，toyc 编译后运行时 segfault（已知 toyc cgen va_arg bug）
- `string.c` 的 `strerror` 内部调用 `snprintf`，创建了 stdio ↔ string 的循环依赖
- `init/` 含汇编 `start.S`，`thread/` 需要 TLS，暂不纳入
- 部分 math 函数的精度不达标（约 24/49 通过）

## 已知限制

| 特性 | 状态 |
|------|------|
| 浮点运算 | ✅ 完整支持 float(32-bit) 和 double(64-bit)，SSE 指令无条件启用 |
| 全局 float/double 花括号初始化 | ✅ `float arr[] = {1.0f, 2.0f}` 等 |
| `%f` 格式化 | ✅ toyc_rt.c 运行时支持 |
| VLA（变长数组） | 未实现 |
| 位域（bitfield） | 未实现 |
| 复合字面量 `(int[]){1,2}` | 未实现 |
| 指定初始化器 `.field=val` | 未实现 |
| `_Generic` | 未实现 |
| `long double` | 不支持 |
| `goto` 跨函数 | 未检查 |
| 宽字符/宽字符串 | 未实现 |
| `-I` include 路径、`-MD` 依赖追踪 | 静默忽略（toyc 参数解析极简） |


## 验证

```sh
make test             # 36/36 ✅
make test-selfhost    # 40/40 ✅
make test-source      # 8/8 ✅
make test-toyld         # 40/40 ✅
make test-error       # 16/16 ✅
make test-toyld-self    # 自举收敛 ✅
make test-lib-compile # 28/28 ✅
./bootstrap-to-10.sh  # stage-2→10 字节级完全一致 ✅
make test-all         # 全部通过 ✅
```

全链零外部依赖（仅 make）。自举收敛证明 toyc 是自洽的编译器。
