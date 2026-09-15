# ToyCCompiler 提交级证据表

## 使用说明

本表为 [ToyCCompiler 时期](../periods/toy-c-compiler.md) 提供可复核依据。提交号属于
`BandieraRosse/ToyCCompiler`；本地缓存为 `../ToyCCompiler`，可见历史从 `22ffcc8` 到
`58ac389`。提交说明、README、`CLAUDE.md` 和 draft/004—007 都是同期陈述，不自动证明功能。

## 提取来源表

比较端点为 Tinylibc `87d61e0` 与 ToyCCompiler `22ffcc8`。

| ToyCCompiler 根路径 | Tinylibc 来源 | 对照结果 |
|---|---|---|
| `app/{cgen,cgen_asm,cgen_expr,elf_write,lex,parse,preproc,tas,tcc,tpp}.c` | `app/compiler/` 同名路径 | 10 个 blob 完全相同，只移动目录 |
| `app/tcc_rt_start.S` | `app/compiler/tcc_rt_start.S` | blob 完全相同 |
| `app/elf_write.h`、`app/tcc.h` | `app/compiler/` 同名路径 | 只把 Tinylibc 总头替换为独立头 |
| `app/tcc_rt.c` | `app/compiler/tcc_rt.c` | 改用 `tcc_need.h`，删除重复定义，并将固定参数 `__printf` 改为变参实现 |
| `include/elf.h` | Tinylibc 同路径 | 只把 `tlibc_types.h` 换成 `tcc_need.h` |
| `ld.script` | Tinylibc 同路径 | blob 完全相同 |
| `include/tcc_need.h` | 多个 Tinylibc 头的所需子集 | 根提交新文件；组合类型、常量、syscall 与运行时声明 |
| `compiler-tests/01`—`13` | Tinylibc 细粒度测试 | 不是改名或相同 blob；重写成 13 个聚合测试 |
| `Makefile`、`.gitignore` | 独立工程入口 | 根提交新文件 |

根提交晚于 `87d61e0` 约十小时，11 个核心 blob 与该末端完全相同，差异文件又是明确的独立化修改，
故可把 `87d61e0` 固定为提取基底，而不是只称“附近快照”。

## 自举层级与工具依赖

| 对象 | 对象可确认行为 | 当时仍依赖或限制 |
|---|---|---|
| `22ffcc8` | 独立 Makefile 构建 tcc/tpp/tas | C 与 `.S` 均由 GCC 处理，系统 `ld` 链接 |
| `0252321` | GCC→stage 1；stage 1 编译九个 C 文件并生成 stage 2 | `.S` 用 GCC，链接和测试用系统 `ld` |
| `961fbf6` | 新增 stage 1—10 脚本 | stage 1=GCC；每级 `.S`=GCC；每级链接=`ld` |
| `8b2ea1f` | README 记录 stage 3—10 tcc 二进制一致 | 同期宣言；不是 tas/tld 闭环，不证明 C 正确性 |
| `fca878f` | 让 tas 源码适合 tcc；说明称种子退役、Makefile 回 GCC | 此前无受跟踪 `bootstrap/`，旧种子不可复查 |
| `7adcf8f` | 首次提交种子 tcc/tas；默认 CC/AS 改为种子 | 默认链接仍用系统 `ld` |
| `c951c79` | 新增 tld 与测试 | `TLD_CC=gcc` 暂绕 tcc bug |
| `b3c5145` | tcc 编译 tld，tld 两次自链接输出相同 | 默认项目链接尚未全部切到 tld |
| `9948ea0` | 首次提交 tld 种子，默认 Makefile 改用它 | shell/make/宿主命令、Linux 与三种子仍在 TCB |
| `bf518d5` | 修复负成员偏移符号扩展；清理脚本 workaround | 最终 stage 脚本仍硬编码 `LD="ld"` |
| `46119c1` | 更新三个种子；说明记录 stage 9→10 一致 | 最初种子的可信来源仍无独立证明 |

## stage、种子与链接器边界

- `0252321` 的“完整自身”指九个 C 编译单元加运行时；tpp、tas 不链接进 tcc。
- `961fbf6` 的 stage 1 是 GCC 产物；`7adcf8f` 后 stage 1 改为复制预置种子。
- 最终脚本只测试 stage 1、2、10；中间阶段只记录 tcc 可执行文件 MD5。
- `bootstrap-to-10.sh` 的 `SEED_TAS` 消除了 GCC 对 `.S` 的处理，但 `LD="ld"` 保留到末端。

受 Git 跟踪的 `bootstrap/` 历史只有三个节点：`7adcf8f` 加入 tcc（357736 bytes）与 tas
（75192 bytes）；`9948ea0` 加入 tld（32832 bytes）；`46119c1` 更新三个种子，并记录 tcc 为
403592 bytes、MD5 前缀 `abdc0b42`。`fca878f` 所说的更早种子未进入 Git，当前不能恢复。

`b3c5145` 的 `test-tld-self` 以同一组对象连续链接两代 tld 并 `cmp`；`9948ea0` 则改变正常构建
依赖图。二者分别回答“链接器能否复制自身”和“默认构建是否还调用 GNU ld”。

作者于 2026-09-15 回忆，随项目附带种子有一定“为了证明不需要 GCC”的意气成分。初版种子
已能自举，但仍有一些可修复的小问题；用有问题的种子继续修复比借助 GCC 更麻烦。这解释了构建策略
为何会在“证明可以离开 GCC”与“借助 GCC 更方便地修正工具链”之间反复；作者已记不清旧种子的
具体生成与恢复过程。

三个种子的 Git mode 均为 `100644`。Linux 普通检出后不能直接执行，需先补执行位；这与“新检出
后只运行 make”的字面声明冲突；现有材料无法确认这是否源于当时的 Windows 文件系统环境。

## README、署名与文章

| 对象 | 同期陈述 | 使用限制 |
|---|---|---|
| `8b2ea1f` | 自举成功、stage 3—10 收敛；署名 Claude Opus 4.8 | 作者确认实际模型为 DeepSeek，trailer 不能作为模型证据 |
| `687ed29` | “这一次提交没有 AI”，删去引语、拟人化文字与若干段落 | 作者确认是在自举后激动写下抒情文字，平静后未调用 AI、亲手删除 |
| `58ac389` | HTTP 301 风格迁往 Toyc；仍署名 Claude | 只能确认作者想留下幽默的迁移声明，不向特定模型归因 |
| draft/004 | 将收敛描述成可发现隐藏后门 | AI 生成初稿；技术结论错误，后文已修订 |
| draft/005 | fixed point 不等于正确性或可信性 | AI 生成；bug 提交号可与 Git 对照 |
| draft/006 | 稳定 Trusting Trust 后门也会收敛 | AI 生成；外部安全史与数量断言未在本轮核查 |
| draft/007 | “零依赖自举 C 编译器”提纲 | 未完成，不当作实现说明 |

## 2026-09-15 隔离复查

从 `e61c83f` 以 `git archive` 解出临时树：

- 未改权限直接 `make`：`bootstrap/tcc: Permission denied`。
- 补三个种子的执行位后：默认 `make` 成功，命令显示 tcc、tas、tld 分别承担编译、汇编、链接。
- `make test` 为 29/29；`make test-tld-self` 的两次自链接文件字节一致。
- `bootstrap-to-10.sh` 产出 stage 1—10，均为 403592 bytes，MD5 均为
  `abdc0b42b2c36677b514e951a58179ef`；脚本同时确认链接仍由系统 `ld` 执行。
- selfhost 为 36/38；两个失败假设不存在路径的 `renameat2` 必须返回 `ENOENT`，受限环境实际返回
  `EROFS`。其余 syscall 参数路径通过，故记录为环境敏感测试，不改写为编译器回归。

## 复查命令

```sh
git -C ../Tinylibc ls-tree -r 87d61e0 app/compiler compiler-tests include/elf.h ld.script
git -C ../ToyCCompiler ls-tree -r 22ffcc8
git -C ../ToyCCompiler diff 8b2ea1f 687ed29 -- README.md
git -C ../ToyCCompiler show 0252321:bootstrap-selfhost.sh
git -C ../ToyCCompiler show 961fbf6:bootstrap-to-10.sh
git -C ../ToyCCompiler show bf518d5:bootstrap-to-10.sh
git -C ../ToyCCompiler log --reverse -- bootstrap/
git -C ../ToyCCompiler show 9948ea0:Makefile
```

## 未纳入的材料

- `fca878f` 前未提交种子的二进制、生成命令或日志。
- 2026-07-08 至 10 日原始 stage 输出。

这些缺失限制最早种子来源和历史运行现场的复原，不影响脚本调用链、后续种子对象和提交边界，
不再作为本轮后续任务。
