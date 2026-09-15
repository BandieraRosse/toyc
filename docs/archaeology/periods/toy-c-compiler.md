# ToyCCompiler 时期

## 本章定位

ToyCCompiler 的独立历史只有一周：从 2026-07-04 的根提交 `22ffcc8` 到 7 月 11 日以 README
重定向迁往 Toyc 的 `58ac389`。编译器 Phase 1、自编译源码、`tas` 和测试套件都已在 Tinylibc 内
发生；本章从“怎样提取”开始，研究独立后怎样把“能编译自身源码”推进为可运行的下一阶段、多阶段
收敛，以及由 `tcc`、`tas`、`tld` 和预置种子组成的默认构建闭环。

逐提交和脚本依据见 [ToyCCompiler 证据表](../evidence/toy-c-compiler.md)。本章将 Git 可确认事实、
同期 README/提交说明、AI 生成文章和作者回忆分开处理。

## `22ffcc8`：从 `87d61e0` 有选择地提取

ToyCCompiler 没有 Tinylibc 的 Git 祖先链，但根提交的来源快照现在可以精确到 Tinylibc
`87d61e0`。将根提交的 `app/` 对应到 `87d61e0:app/compiler/` 后，14 个编译器与运行时源文件中，
11 个 blob 完全相同：`cgen.c`、`cgen_asm.c`、`cgen_expr.c`、`elf_write.c`、`lex.c`、`parse.c`、
`preproc.c`、`tas.c`、`tcc.c`、`tcc_rt_start.S`、`tpp.c`。另外三个不是另一时期的快照：
`elf_write.h` 与 `tcc.h` 只把 Tinylibc 总头替换成独立头，`tcc_rt.c` 则改用该头并把固定参数
`__printf` 改为变参实现。`include/elf.h` 也只替换 include，`ld.script` blob 完全相同。

根提交不是整树复制。它删去 libc、应用和大部分工程设施，新写独立 Makefile 与
`include/tcc_need.h`；后者从 Tinylibc 类型、常量、系统调用宏和声明中摘出编译器所需子集。
Tinylibc 当时约百个细粒度测试也没有原样搬入，而被改写为 13 个聚合的自举导向测试。因此准确
表述是：**以 `87d61e0` 的编译器目录为代码基底，移动路径并做最小独立化，另建测试与构建入口**。

作者于 2026-09-15 补充：当时编译器功能较弱，编译测例的表现已不理想，编译 Tinylibc 更差；
因此将编译器迁入新仓库，用更集中的上下文专注自举。当时的判断是，只有先完成自举，才有希望
继续到“编译 Tinylibc”这一更大目标。所以独立建仓首先是开发与目标收窄决策，而非为公开展示单独
制作一份作品。当时已计划日后重新整合 Tinylibc；`Toyc` 这个名称则是之后经过几次讨论才确定。

## 独立后继续暴露的自举阻塞

`22ffcc8` 已包含能编译自身各源码文件的编译器，却不等于完整自举。根提交的 Makefile 仍以 GCC
编译 C 和汇编启动文件、以系统 `ld` 链接。7 月 4 日当天，聚合测试被再次改写为直接针对自身源码
模式，随后连续修复栈寻址、符号扩展、signed/unsigned、短类型存储、函数参数和内联汇编约束。
7 月 5—6 日又按 `lex.c`、`preproc.c`、`elf_write.c`、代码生成器和 `parse.c` 建立源码级测试，
struct 返回、全局初始化、作用域链等问题继续被单独暴露。

这延续了 Tinylibc 末期的测试策略：不再只运行一次“编译自身然后追段错误”，而是将自举所需模式、
模块源码和跨翻译单元行为拆成回归入口。提交顺序能确认测试怎样落地；“先建测试框架”由作者提出，
则仍是作者回忆，不能从 Git trailer 倒推。

## “自举成功”不是一个瞬间

| 层级 | 本时期的证据边界 |
|---|---|
| 编译自身源码 | Tinylibc `9921705` 已声明 `tcc.c` 自编译；这不是 ToyCCompiler 才获得的能力 |
| 生成可运行下一阶段 | `0252321`：GCC 生成 stage 1，再由它编译九个 C 文件；启动汇编仍由 GCC 处理，系统 `ld` 生成 stage 2 |
| 连续多 stage | `961fbf6` 加入 stage 1—10 脚本；当时 stage 1 来自 GCC，每一阶段仍用 GCC 汇编、系统 `ld` 链接 |
| 编译器输出收敛 | `8b2ea1f` README 记录 stage 3—10 的 tcc 可执行文件字节相同；它验证固定点，不证明完整 C 正确性或可信性 |
| 消除 GCC 编译 C | `7adcf8f` 提交预置 `bootstrap/tcc`，默认 C 编译不再调用 GCC |
| 消除 GNU `as` | 同一提交加入 `bootstrap/tas`，默认启动汇编改由它处理 |
| 消除 GNU `ld` | `9948ea0` 加入 `bootstrap/tld` 并让默认 Makefile 的链接规则使用它 |
| 三工具种子更新 | `46119c1` 更新 `tcc/tas/tld`，提交说明称种子来自收敛链并记录 stage 9/10 一致 |

`961fbf6` 与 `8b2ea1f` 的“自举成功”因而是**编译器本体在宿主汇编器、链接器辅助下生成可运行的
后续代并收敛**，不是三件套闭环。`7adcf8f` 才使默认构建离开 GCC/GNU as，`9948ea0` 才使默认
Makefile 离开 GNU ld。这些成果是递进关系，不能用最终状态重写早期宣言。

最初 stage 1 是 GCC 构建的 tcc；`8b2ea1f` 记录 stage 3—10 收敛。加入预置种子后，脚本把种子
复制为 stage 1，`7adcf8f` 称收敛点提前到 stage 2。最终 `46119c1` 的种子本身就是收敛链产物；
隔离复查时 stage 1—10 的 tcc 都得到同一 MD5。因此 stage 编号不是脱离脚本版本即可比较的概念。

脚本比较的是各 stage 的 **tcc 可执行文件** MD5，而不是每一阶段所有 `.o`、`tas`、`tld` 和测试
产物的全树比较。最终脚本只对 stage 1、2、10 跑完整 selfhost 测试，中间阶段只比较 MD5。相邻
输出一致说明确定性构建映射达到固定点；它不能证明实现符合完整 C 标准，也不能排除不同输入上的
共同错误，更不能抵御会稳定复制自身的 Trusting Trust 后门。

draft/004 的初稿曾把收敛写成能发现隐藏恶意逻辑，draft/005 已改为“fixed point，不是正确性”，
draft/006 又明确说明稳定后门同样可以收敛。这组三篇均由 Claude Code 客户端接入 DeepSeek 模型
生成，应作为叙述修订过程保留，技术结论仍以脚本和通行的信任边界判断。

## tas、tld 与种子

`tas` 早在 Tinylibc `608f4a3` 出现。独立后，`fca878f` 为让 `tas.c` 被 tcc 编译而改写二维数组
和表驱动代码，同时提交说明称“自举种子退役”、Makefile 回归 GCC。仓库中没有在该提交之前受 Git
跟踪的 `bootstrap/` 文件，所以这里的“退役”只证明构建策略曾反复，不能重建未提交种子的内容。
数小时后的 `7adcf8f` 又首次提交 `bootstrap/tcc` 与 `bootstrap/tas`，恢复种子驱动的默认构建。

`c951c79` 新增 `tld.c`，但 Makefile 暂以 GCC 编译它；`b3c5145` 修复阻塞后，改由 tcc 编译 tld，
并让 tld 两次自链接结果字节一致。`9948ea0` 再把 `bootstrap/tld` 纳入仓库，令默认 Makefile 的
所有链接规则使用它。这里要区分“tld 能链接自己”和“项目默认所有链接均由 tld 接管”。

最终 `bootstrap-to-10.sh` 仍硬编码 `LD="ld"`，所以它验证的是 tcc 多阶段收敛，不是最终默认
Makefile 的三工具无 GNU 链路；`make test-tld-self` 才单独验证 tld 自链接收敛。两条门禁应并列
阅读，不能用其中一条替代另一条。

## “零外部依赖（仅 make）”的准确边界

`9948ea0` 后，从一份正常检出的仓库运行默认 `make`，C 编译、启动汇编和链接分别由预置的
`bootstrap/tcc`、`bootstrap/tas`、`bootstrap/tld` 完成；生成物通过 Linux x86_64 syscall 运行，
不链接宿主 libc。这个意义下，GCC、GNU as、GNU ld 和宿主 libc 已退出默认工具链路径。

但它不是从纯源码或裸机开始：仍需要 Linux x86_64 内核、shell、make、文件系统、CPU，并信任
Git 中预置的三个可执行种子。脚本还调用多种宿主用户态工具，`bootstrap-to-10.sh` 最终仍调用
GNU `ld`。更准确的短语是：**默认 Makefile 的编译、汇编、链接阶段不调用 GCC/binutils 或宿主
libc，但构建环境和二进制种子并非零依赖。** `tmake` 当时也未接管默认入口。

隔离复查还发现三个种子在 Git 树中模式均为 `100644`；Linux 新检出后须先补执行位，否则默认
Makefile 报 `Permission denied`。补执行位后，最终快照默认构建、29 个 basic 测试与 tld 自链接
均可运行；38 个 selfhost 中两个以 `renameat2` 必须返回 `ENOENT` 为假设的用例，在本次受限环境中
因返回 `EROFS` 而失败，属于环境敏感断言。stage 1—10 的 tcc 仍全部生成且 MD5 一致。

## Claude Code、DeepSeek 与提交署名

作者确认 Claude Code 是客户端和工具执行环境，模型请求全部发送到 DeepSeek API，没有同时使用
Anthropic Claude 模型。为节省费用，当时全部使用 DeepSeek-V4-Flash；只有两天多使用 Pro，其余时间均以
Flash 开发。后期作者主要写 prompt、设定目标与限制、判断异常，让 agent 阅读、修改、
运行命令、调试并总结；作者偶尔亲自测试。Git 能看到提交密度、文件改动和大量
`Co-Authored-By: Claude Opus 4.8`，但看不到客户端怎样解析模型输出、谁发起每次工具调用、或
trailer 由哪一层生成。

这些 trailer 因而只能作为 Claude Code 工作流留下的文本痕迹，不能归因为 Claude 模型。作者当时
没有留意 trailer，事后认为它使观众误以为项目使用了昂贵的 Claude 模型；而实际选择 Flash 正是出于
成本考虑。`687ed29` 没有共同作者 trailer，
提交说明称“这一次提交没有 AI”，diff 确实只是删去 README 中引语、拟人化段落、代码风貌和若干
限制文字。作者于 2026-09-15 确认：这里的“没有 AI”指该次删改由作者亲手完成，没有让 AI/agent
修改文件。被删段落写于完成自举后；作者回想过程中的困难，情绪激动，因而写得较为抒情。平静后
认为这种文字不适合放在 README，遂主动删去。

作者后来关于 GLM-5.2 的回忆，只能说明当时曾用它讨论项目迁移的想法；最终留下了一份刻意幽默的
迁移提交。该细节不用于建立准确模型切换时间线，也不把 `58ac389` 的文字归因给特定模型。

## README 宣言与项目身份

`8b2ea1f` 首次加入 README，把 stage 3—10 收敛写成“自举成功”。数小时后的 `687ed29` 主动删去
较夸张和拟人化部分。随后种子、tas 与 tld 改变了“闭环”的技术边界，README 又随实现更新。
7 月 11 日 `58ac389` 把原仓库 README 改成 HTTP 301 风格的迁移说明；Toyc 直接继承 `22ffcc8`
起的全部 Git 历史。因此 ToyCCompiler 不是被导入 Toyc 的外部快照，而是同一提交链继续发展。

## 未纳入的材料与证据边界

`fca878f` 前未入 Git 的初版种子、生成命令和原始日志没有进入当前材料集。因此文档只把
`fca878f` 之后的种子、脚本和提交说明作为可复核边界，不把更早未提交状态列为后续任务。
