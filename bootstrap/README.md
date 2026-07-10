# 自举种子（Bootstrap Seeds）

本目录包含 tcc 项目的自举种子二进制：

- `tcc` — C 编译器（350 KB，git 追踪）
- `tas` — x86_64 汇编器（74 KB，git 追踪）
- `tld` — x86_64 静态链接器（33 KB，git 追踪）

## 用途

`bootstrap/tcc` + `bootstrap/tas` + `bootstrap/tld` 是 Makefile 的默认工具链，
`make` 即用它们全链自编译，唯一外部依赖是 `make` 本身。

## 更新种子

```sh
make && make update-bootstrap
```

这会用当前源码构建 tcc+tas+tld，然后覆盖 `bootstrap/` 中的种子。
通常在源码重大变更后执行。

## 自举验证

```sh
make test-tld-self          # tld 自链接 stage-1→stage-2 字节级收敛
./bootstrap-selfhost.sh     # seed → stage-2 → 全部 selfhost 测试
./bootstrap-to-10.sh        # stage-3→10 字节级收敛验证
```

## 历史

种子最初由宿主机 C 编译器 + ld 编译生成。自 tld 加入后，
工具链从"零 gcc"演进到"零外部依赖"：`make` 即自举。
字节级收敛证明种子与自编译版本完全等价，项目已彻底自举。
