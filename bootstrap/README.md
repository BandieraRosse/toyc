# 自举种子（Bootstrap Seeds）

本目录包含 toyc 项目的自举种子二进制：

- `toyc` — C 编译器（git 追踪）
- `toyas` — x86_64 汇编器（git 追踪）
- `toyld` — x86_64 静态链接器（git 追踪）
- `toyar` — ar 归档器（git 追踪）

## 用途

`bootstrap/{toyc,toyas,toyld,toyar}` 是 Makefile 的默认工具链，
`make` 即用它们全链自编译，唯一外部依赖是 `make` 本身。

## 构建

```sh
make                              # 自举构建
make update-bootstrap             # 用 build/ 产物更新种子
make clean                        # 清除 build/
```

## 测试

```sh
make test                         # 常规测试
make test-selfhost                # 自包含测试
make test-toyld                   # toyld 链接测试
make test-error                   # 错误报告测试
make test-lib                     # Tinylibc 库完整测试
make test-toyld-self              # toyld 自举验证（字节级收敛）
make test-all                     # 全部测试套件
```

## 自举收敛验证

```sh
make test-toyld-self              # toyld 自链接 stage-1→stage-2 字节级收敛
./bootstrap-selfhost.sh           # seed → stage-2 → 全部 selfhost 测试
./bootstrap-to-10.sh              # stage-2→10 字节级收敛验证
```

## 历史

种子最初由宿主机 C 编译器 + ld 编译生成。自 toyld 加入后，
工具链从"零 gcc"演进到"零外部依赖"：`make` 即自举。
字节级收敛证明种子与自编译版本完全等价，项目已彻底自举。
