# 自举种子

本目录保存版本控制内的工具链种子二进制：`toyc`、`toyas`、`toyld` 和
`toyar`。它们用于自举收敛检查，不参与默认构建。

## 默认构建

根 `Makefile` 默认使用 GCC 和 GNU binutils 构建工具链；`self-*` 目标再使用
`build/toyc` 编译 Tinylibc 或应用。日常构建和测试不需要更新本目录中的种子。

## 自举检查

```sh
make test-toyld-self       # 检查 toyld 自链接的两阶段字节一致性
./bootstrap-selfhost.sh   # 用种子构建 stage 2，并运行自包含测试
./bootstrap-to-10.sh      # 检查 stage 2 到 stage 10 的字节级收敛
```

这些检查用于验证种子和自举链，不代替常规编译器测试。聚合测试入口及其覆盖范围见根
[README 的测试章节](../README.md#测试)。

## 更新种子

只有在有意更新已跟踪的种子时才运行：

```sh
make update-bootstrap
```

该目标先由 GCC 构建工具链，再覆盖本目录中的四个二进制。更新后应检查二进制差异，并运行
自举检查；普通源码修改不需要更新种子。
