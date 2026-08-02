# Toyc 项目协作说明

本文件为使用 AI 编码代理参与仓库开发时提供稳定的项目上下文。面向用户的构建、测试和
项目介绍以 `README.md` 为准；语言特性详情以 `toyc-c-features.md` 为准。不要在本
文件复制测试通过数、日期或修复流水账，以免多个文档再次失去同步。

## 项目概况

Toyc 是面向 Linux x86_64 的小型、自托管 C 工具链：

- `compiler/toyc.c` 及相关文件：C → ELF64 目标文件
- `compiler/toyas.c`：x86_64 汇编器
- `compiler/toyld.c`：静态链接器
- `compiler/toyar.c`：ar 归档器
- `compiler/toypp.c`：独立预处理器（非默认构建目标）
- `compiler/toyc_rt.c`：不依赖 libc、直接使用系统调用的运行时
- `lib/`、`include/`：仓库内的 Tinylibc
- `app/`：示例和自托管应用
- `llm/`：小型 GPT-2 实现与测试

## 构建事实

默认 `make` 使用 GCC、GNU `as` 和 GNU `ld` 构建
`build/{toyc,toyas,toyld,toyar}`，并不使用 `bootstrap/` 种子。`self-*` 目标才使用
生成的 `build/toyc` 验证自托管构建。

```sh
make                    # 默认工具链
make build/toypp        # 可选预处理器
make self-lib           # Toyc 构建 Tinylibc
make self-app           # Toyc 构建全部应用
make clean              # 删除 build/ 和 tmp/
```

`bootstrap/` 内是版本控制跟踪的种子二进制，仅用于阶段性收敛检查。不要在普通修改中
运行 `make update-bootstrap`；只有明确需要更新种子时才运行，并随后验证：

```sh
./bootstrap-selfhost.sh
./bootstrap-to-10.sh
```

种子可能暂时落后于源码，这不等同于默认 GCC 构建失败。

## 测试策略

修改后先运行与改动最接近的测试，再根据风险扩大范围：

如果没有修改 `compiler/toyc.c`、`compiler/lex.c`、`compiler/parse.c`、
`compiler/preproc.c`、`compiler/cgen*.c`、`compiler/elf_write.c`、`compiler/toyc.h` 等
toyc 编译器源文件，则不要求执行完整测试；运行与本次改动直接相关的构建和测试即可。
只有修改编译器实现、公共代码生成路径或影响范围无法可靠限定时，才需要扩大到完整测试。

```sh
make test               # 常规编译/运行测试
make test-selfhost      # 无 toyc_rt 的自包含测试
make test-source        # 编译器源码级测试
make test-lib           # Tinylibc 编译和功能测试
make test-error         # 诊断测试
make test-toyld         # toyld 链接/运行测试
make test-toyar
make test-toyld-archive
make test-toyld-self
make test-llm
make test-all           # 核心聚合目标，不包含 test-toyld 和 test-llm
```

测试注意事项：

- `make test-all` 遇到首个失败会停止，后续目标需要单独补跑。
- `make test-self-app` 不负责构建应用；应先运行 `make self-app`，否则缺失程序会被
  跳过，出现“零失败”但没有实际覆盖的结果。
- syscall 和 procfs 测试可能受容器或沙箱影响。例如只读根文件系统会让
  `renameat2` 返回 `EROFS`，容器 PID 1 的 `/proc` 字段也可能不同。报告结果时应区分
  编译器回归与环境相关断言，不能简单宣称全部通过。
- 测试数量会随用例变化。更新文档前应从实际输出或测试文件重新统计。

## 修改原则

- 保持工具链 freestanding，不要无意引入宿主 libc 依赖。
- 优先添加最小回归用例，再修改编译器实现。
- 不要把 `compiler-tests/pending/` 中的复现用例当成已支持特性。
- 保留用户已有的工作区修改；不要顺手格式化或改写无关文件。
- 不要提交 `build/`、`tmp/` 或 `/tmp` 中的生成物。
- 更新构建目标或测试集合时，同步检查 `README.md`、`README_en.md` 和 Makefile 顶部
  注释，避免三处说法分叉。

## 文档职责

- `README.md` / `README_en.md`：用户入口、构建命令、测试语义和最近一次实测结果
- `toyc-c-features.md`：C 特性支持范围和限制
- `bootstrap/README.md`：种子二进制及自举流程
- `AGENTS.md`：AI 编码代理共享的稳定项目上下文和仓库操作约束
- `CLAUDE.md`：通过 `@AGENTS.md` 向 Claude Code 导入本文件

遇到文档与代码不一致时，以 Makefile、脚本和实际测试行为为准，并修正文档。
