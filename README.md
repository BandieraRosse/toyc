# Toyc

[English](README_en.md)

Toyc 是一个面向 Linux x86_64 的小型、自托管 C 工具链，源自
[Tinylibc](https://github.com/WHU-SC7/Tinylibc) 和 ToyCCompiler。工具链自身不链接
libc，运行时直接使用 Linux 系统调用；仓库同时包含 Tinylibc、示例程序和一个小型
GPT-2 实现。

## 工具链

| 程序 | 用途 |
|---|---|
| `toyc` | C 源码 → ELF64 目标文件 |
| `toyas` | x86_64 汇编 → ELF64 目标文件 |
| `toyld` | 目标文件/归档 → 静态可执行文件 |
| `toyar` | ar 归档创建、查看与提取 |
| `toypp` | 独立预处理器（可选目标） |

## 构建

需要 Linux x86_64、GNU make、GCC 和 GNU binutils（`as`、`ld`、`ar`）。默认构建
由 GCC/binutils 生成工具链；`self-*` 目标才使用生成的 Toyc。

```sh
make                    # build/{toyc,toyas,toyld,toyar}
make build/toypp        # 可选：构建独立预处理器
make self-lib           # 用 build/toyc 构建 Tinylibc
make self-app           # 用 build/toyc 构建全部应用
make clean              # 删除 build/ 和 tmp/
```

也可使用 `make lib`、`make app` 或 `make app-<name>` 由 GCC 构建库、全部应用或单个
应用；`make self-app-<name>` 是对应的自托管构建。

`bootstrap/` 保存版本控制内的种子二进制。它们用于阶段性的自举检查，不参与默认
`make`，而且可能落后于源码：

```sh
make update-bootstrap       # 有意更新种子；会修改跟踪的二进制
./bootstrap-selfhost.sh     # 种子 → stage 2，并运行自包含测试
./bootstrap-to-10.sh        # 验证 stage 2…10 字节级收敛
```

## 测试

```sh
make test               # 58 个常规编译/运行测试
make test-selfhost      # 42 个无 toyc_rt 的自包含测试
make test-source        # 8 个编译器源码级测试
make test-lib           # 29 个库编译单元 + 12 个功能套件
make test-error         # 16 个诊断测试
make test-toyld         # 42 个使用 toyld 的链接/运行测试
make test-toyar         # 5 个归档器测试
make test-toyld-archive # 2 个归档链接测试
make test-toyld-self    # toyld 两阶段字节一致性
make test-llm           # 29 个 GPT-2 数值/前向传播测试
make test-all           # 核心聚合目标；不包含 test-toyld 和 test-llm
```

`make test-self-app` 只测试已经存在的 `build/*_self`，因此应先运行
`make self-app`；否则缺少的程序会被跳过。

### 2026-07-31 实测

在受限容器中，本次运行结果为：

- `make test`：58/58；`make test-source`：8/8；`make test-error`：16/16。
- `make test-toyar`：5/5；`make test-toyld-archive`：2/2；
  `make test-toyld-self`：两阶段字节一致。
- `make test-llm`：29/29。
- `make test-selfhost` 和 `make test-toyld`：均为 40/42。两个用例调用根目录路径上的
  `renameat2`，容器返回 `EROFS`，而测试固定期待 `ENOENT`；这是环境相关的断言差异。
- `make test-lib`：编译 29/29，功能 11/12。`procfs` 套件读取容器 PID 1 时得到
  `starttime=0`，其余 19/20 个断言通过。

因此本环境的 `make test-all` 会在 `test-selfhost` 处停止，不能标记为全绿。

## 目录

```text
compiler/        编译器、汇编器、链接器、归档器与运行时
compiler-tests/  编译器、链接器和 Tinylibc 测试
include/         Toyc/Tinylibc 头文件
lib/             Tinylibc 源码
app/             示例与自托管应用
llm/             小型 GPT-2 实现和测试
bootstrap/       版本控制内的自举种子
```

更细的语言特性记录见 [toyc-c-features.md](toyc-c-features.md)，种子说明见
[bootstrap/README.md](bootstrap/README.md)。
