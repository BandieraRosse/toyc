# Toyc 初始整合期提交级证据表

## 使用说明

本表为 [Toyc 时期](../periods/toyc.md) 的初始整合及 Rasterfall 转向提供可复核依据。Toyc
直接延续 ToyCCompiler 的 Git 链；Tinylibc 是另一仓库，二者之间发生的是文件复制与适配，不是
Git merge。提交说明和 README 属于同期陈述，整合动机与 standalone 含义另由作者于 2026-09-16
回忆确认。

## 定位与准备

| 对象 | 可确认内容 | 证据边界 |
|---|---|---|
| `58ac389` | ToyCCompiler README 以 HTTP 301 形式指向新的 `toyc` 仓库 | 只确认迁移和新名称已决定 |
| `643287b` | 称 tcc 是 toyc 生态核心，项目由 ToyCCompiler 与 Tinylibc 合并而生，将成为独立系统软件生态 | “two parents” 首次明确出现；此时尚未复制 Tinylibc 树 |
| `a26e7a6` | 将 `app/` 移到 `compiler/`，提交说明明确为未来依赖库的应用腾出 `app/` | 作者日期为 7 月 11 日，提交者日期为 7 月 15 日 |
| `015b236`、`b8fb22f` | 新增 `tar`/归档链接支持，说明明确服务 Tinylibc 静态库 | 归档器后来随工具链统一改名 `toyar` |
| `6ce3490` | 不复制库源码，直接对外部 Tinylibc 各模块建立声明式编译与功能测试 | 标志渐进整合策略落地 |
| `5bffca4` | `tcc/tas/tld/tpp/tar` 统一改名为 `toyc/toyas/toyld/toypp/toyar` | 避免 TinyCC 混淆，也落实更宽的项目身份 |

## 两次选择性移植

Tinylibc `32436f1` 于 2026-07-23 20:43 将工具链放入 `app/compiler/`。把该目录映射回 Toyc 的
`compiler/` 后，连同 `include/toyc_need.h`、`include/elf.h` 共 19 个可比文件，与 Toyc
`0a7800f` 的 blob 全部相同。`0a7800f` 的时间为 19:59，因此来源是当时最新 Toyc 工具链，而非
泛指旧 ToyCCompiler 快照。Tinylibc 随后用 `5a27733`、`2c3f585`、`384a71c`、`6a86e5f` 调整
构建和 `va_list` 兼容。

Toyc `af6bc30` 于次日 00:09 新增 108 个路径；这些路径与 Tinylibc `6a86e5f` 同路径 blob
108/108 相同，故可将 `6a86e5f` 固定为准确复制快照。它晚于该 Tinylibc 提交约 23 分钟。

这不是完整仓库的双向镜像：

- Tinylibc 接收工具链源码，并在自己的 Makefile、脚本、shell 与 tmake 中做适配。
- Toyc 接收完整的 `lib/`、x86_64 架构头和公共头，但只选择部分 `app/`。
- `af6bc30` 所称 “full app tree” 不符合树对象：未引入 `app/net/` 15 个、`app/graphics/` 10 个、
  `app/term/` 4 个、`app/audio/` 2 个、`app/paper/` 4 个、`app/elf/` 3 个文件，也没有重复引入
  Tinylibc 的 `app/compiler/` 18 个文件；`arch/riscv64/` 3 个文件亦未进入。

因此准确表述是：**先把最新 Toyc 工具链移入 Tinylibc 做整体构建探索和适配，再把验证后的
Tinylibc 库及较易验证的应用子集移入 Toyc。** 文件流向是双向的，但项目权威主线最终指向 Toyc。

## 动机与 standalone 的含义

作者于 2026-09-16 确认，编译器最初目的就是编译自己的 Tinylibc 库并取代 GCC 的部分功能；
ToyCCompiler 成熟后继续分仓没有意义，统一是原目标的延续。`toyc` 是借统一机会确定的新名称，
既避免旧名问题，也表示项目不再只是编译器，而是包含较完整 C 生态的“有点功能的玩具”。

自举成功与编译 Tinylibc 之间仍有现实距离。早期曾尝试一次性融合并立即用 toyc 编译整个库，后来
转为更可行的路径：在 Toyc 中按 Tinylibc 的 lib 模块建立测试，逐个解决编译和功能问题。部分
应用难迁移，初次只选择容易验证的部分；另一些老程序被认为意义有限，留在 Tinylibc 仓库。

作者认可 `643287b` 的同期结构表达了 “two parents” 的主要含义：ToyCCompiler 提供 standalone
编译工具链，Tinylibc 提供库和应用。只有 standalone 的库并不完整；有了 standalone 编译器，
toyc 生态在理论上形成能够创造自身并继续创造其他程序的闭环。作者将这视为项目成熟和重要进步，
同时明确这只是“时间足够时”的理论能力方向，工程上未必值得彻底执行；后续维护 Toyc 时，为效率
仍经常使用 GCC。

这里必须区分三层：

1. ToyCCompiler 已证明编译器工具链可自举；
2. 初始整合期通过逐模块测试缩短了 toyc 与编译 Tinylibc 的能力距离；
3. “创造自己并创造一切”是作者对 standalone 生态潜力的概括，不等于 `af6bc30` 已证明所有库、
   应用和后续软件均由 toyc 完整构建。

## 复查命令

```sh
git show 643287b -- README.md README_en.md
git show --stat a26e7a6 6ce3490 5bffca4 af6bc30
git -C ../Tinylibc show --stat 32436f1 5a27733 2c3f585 384a71c 6a86e5f
git ls-tree -r 0a7800f compiler include/toyc_need.h include/elf.h
git -C ../Tinylibc ls-tree -r 32436f1 app/compiler include/toyc_need.h include/elf.h
git diff-tree --no-commit-id --name-only --diff-filter=A -r af6bc30
git -C ../Tinylibc ls-tree -r 6a86e5f
git -C ../Tinylibc log --all --decorate --oneline 6a86e5f..
git show a5b959c:Makefile
git diff a5b959c^ a5b959c -- Makefile
git diff 976f5e3 989b938 -- Makefile lib/stdio/printf.c lib/stdio/snprintf.c
git show --stat a37c4fb 410dac6 e6d1e37 deecddd a41c580 2d7edb7 75a10cd
```

## 整合收尾与完整库构建

| 对象 | 可确认内容 | 证据边界 |
|---|---|---|
| Tinylibc `83a32fd`、`a566206` | `af6bc30` 后仅有的两个主线提交；末次提交为 7 月 24 日 13:39 | `main`/`origin/main` 同停于 `a566206`；没有显式停更宣言 |
| Toyc `989b938` | 13:44 把库测试源从 `../Tinylibc` 切到内部 `lib/`，并吸收末次提交的 `%.0f` 修复 | 未复制临时测试、`.o` 及 `83a32fd` 的全部格式分支改法，不是完整镜像 |
| Toyc `a5b959c` | 首次加入 `self-lib`，以 `build/toyc` 编译全部 Tinylibc C 源并归档完整库 | `.S`、归档、应用链接仍用系统 `as/ar/ld` |
| Toyc `1f2a625` | 默认工具链构建改回 GCC，`self-*` 定位为代码生成验证 | 普通 `lib/app` 也一直保留 GCC 路径 |

由此可把独立 Tinylibc 的权威边界定在 `989b938`：首次复制后两仓又并行约 13.5 小时，独立仓库
最后一次共有库修复五分钟后被选择性吸收，此后不再提交，Toyc 测试也不再依赖外部源码。

## Rasterfall 转向锚点

| 对象 | 可确认内容 | 证据边界 |
|---|---|---|
| `a37c4fb`、`410dac6` | Wayland 软件 3D、公共光栅器和 FPS 灰盒同时以 GCC/Toyc 验证，并加入 Toyc pending 用例 | 起点兼具真实应用与工具链验证性质 |
| `e6d1e37` | 灰盒扩为包含独立规则、敌人、HUD、音频的僵尸潮游戏 | 此时尚未使用 Rasterfall 名称 |
| `deecddd`、`a41c580` | 8 月 6 日首次命名并模块化 Rasterfall；8 月 9 日形成顶层独立目录 | 项目边界由连续迁移建立，不是单提交诞生 |
| `2d7edb7` | 从 `self-app` 排除 Rasterfall，README/AGENTS 明确以 GCC 为准 | 复杂游戏实现不再受 Toyc 兼容范围约束；提交同时处理真实线程原子状态 |
| `75a10cd` | 根协作说明转向 Rasterfall，编译器时期说明归档 | 仓库治理层面的主线切换锚点 |

当前文件延续关系证明代码继承；“模块化测试等工程经验”属于对连续构建与验证方式的归纳，
不应写成某一提交的原话。
