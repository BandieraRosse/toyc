# Toyc 编译器时期的 AGENTS.md（历史保留）

> 本文件保存仓库开发重心转向 Rasterfall 前，根 `AGENTS.md` 中关于 Toyc 编译器、工具链、
> 自举和测试的协作约定。它不作为当前根级代理指令；维护编译器时可作为参考，最终以当前
> Makefile、脚本、README 和实际测试行为为准。

## 项目概况

Toyc 是面向 Linux x86_64 的小型、自托管 C 工具链：

- `compiler/toyc.c` 及相关文件：C 到 ELF64 目标文件。
- `compiler/toyas.c`：x86_64 汇编器；`compiler/toyld.c`：静态链接器。
- `compiler/toyar.c`：ar 归档器；`compiler/toypp.c`：可选独立预处理器。
- `compiler/toyc_rt.c`：不依赖 libc、直接使用系统调用的运行时。
- `lib/`、`include/`：Tinylibc；`app/`：示例和自托管应用。
- `llm/`：GPT-2、Qwen2 及共享张量/检查点代码，独立于核心编译器测试。

## 构建与自举

默认 `make` 使用 GCC、GNU `as` 和 GNU `ld` 构建 `build/{toyc,toyas,toyld,toyar}`，不使用
`bootstrap/` 种子；`self-*` 目标才使用生成的 `build/toyc` 验证自托管。

```sh
make
make build/toypp
make self-lib
make self-app
make clean
```

`bootstrap/` 种子只用于阶段性收敛检查。普通修改不要运行 `make update-bootstrap`；明确更新后运行
`./bootstrap-selfhost.sh` 和 `./bootstrap-to-10.sh`。种子落后于源码不等同于默认构建失败。

## 编译器测试策略

修改 `compiler/toyc.c`、`lex.c`、`parse.c`、`preproc.c`、`cgen*.c`、`elf_write.c`、`toyc.h`
等实现或公共代码生成路径时，先跑最近测试，再按风险扩大：

```sh
make test
make test-selfhost
make test-source
make test-error
make test-toyld
make test-toyar
make test-toyld-archive
make test-toyld-self
make test-llm
make test-all
```

- `make test-lib` 是弃用的历史阶段目标，不作为常规验证入口。
- `make test-all` 遇到首个失败会停止，未执行目标需单独补跑。
- `make test-self-app` 不负责构建应用；应先运行 `make self-app`。
- syscall/procfs 测试可能受沙箱影响，应区分编译器回归与 `EROFS`、容器 PID 1 等环境断言。
- 测试数量随用例变化，文档不复制易失效的通过数。

## 修改与文档原则

- 保持工具链 freestanding，不无意引入宿主 libc 依赖。
- Toyc 源码兼容范围以 `app/` 为界；Rasterfall 不要求由 Toyc 编译。
- 优先添加最小回归用例，再修改编译器实现。
- 不把 `compiler-tests/pending/` 的复现用例当作已支持特性。
- 更新构建目标或测试集合时同步检查 `README.md`、`README_en.md` 和 Makefile 顶部注释。
- `toyc-c-features.md` 负责 C 特性；`bootstrap/README.md` 负责种子和自举流程。

