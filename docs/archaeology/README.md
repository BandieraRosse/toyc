# Toyc 考古计划

本文档集研究 Toyc 的项目谱系、技术演化和开发实践。当前先建立可扩展框架，后续按主题逐步
补充提交级证据、代码对照、开发者回忆和外部材料。考古范围以 Tinylibc、ToyCCompiler 与
Toyc 为主体，同时追溯 Tinylibc 从 SC7 团队操作系统项目分离的前史。

## 叙事主轴

作者在考古计划启动时将这条主线概括为：

> 这条 C 语言项目的演进，是我逐渐深入理解计算机底层世界，并且提高 C 语言项目能力
> （已经不是 C 编程能力了），并且逐渐使用 coding agent 的过程。

这段口述是后续组织材料的核心线索，而不是已经由 Git 单独证明的结论。文档将沿三条相互关联
但不能互相替代的轴展开：

- **底层理解**：从操作系统内核、用户态接口和 libc，进入编译器、自举工具链及更完整的运行时。
- **项目能力**：从写 C 代码，发展到处理架构边界、构建系统、测试体系、跨平台和大型项目演进。
- **协作工具**：从个人调试与网页对话辅助，逐渐转向 Claude Code、DeepSeek API 等 coding agent
  参与的开发方式。

## 核心问题

1. Tinylibc 如何从手写 libc 与用户程序，发展出最初的 C 编译器实验？
2. 为什么编译器从 Tinylibc 中独立为 ToyCCompiler，它怎样完成自举工具链？
3. Toyc 如何继承 ToyCCompiler 的 Git 历史，并重新吸收 Tinylibc 的库与应用？
4. 网页对话、Claude Code、DeepSeek API 等工具分别怎样影响了三个时期的工作方式？
5. 哪些设计是连续演化，哪些是迁移、重写、改名或事后重新解释？

## 已知主线

```text
SC7（2025 年三人团队操作系统竞赛项目）
    │  参考 XV6、XN6 等项目，支持 RISC-V 与 LoongArch
    │  2025-10-17 Tinylibc 根提交说明从 SC7 分离
    ▼
Tinylibc（项目与代码源头）
    │  2026-06-30 起在仓库内发展 tcc 编译器
    │  2026-07-04 提取为独立仓库；未保留 Tinylibc 的 Git 祖先链
    ▼
ToyCCompiler（独立、自举工具链时期）
    │  2026-07-11 原仓库以 README 重定向宣告迁往 toyc
    │  toyc 直接延续其完整提交链
    ▼
Toyc（“two parents”整合时期）
    │  2026-07-23 至 24 日重新引入 Tinylibc 的库与应用树
    ▼
当前仓库（工具链、Tinylibc、应用与 Rasterfall 并存）
```

这里必须区分两种“祖先”：

- **Git 祖先**：Toyc 与 ToyCCompiler 共享根提交 `22ffcc8`；Toyc 的历史直接延续 ToyCCompiler。
- **项目与代码祖先**：该根提交明确写明编译器从 Tinylibc 提取；Tinylibc 更早的提交没有被接到
  Toyc 的 Git DAG 中，因此需要跨仓库考证。

## 时期入口

- [SC7 前史](periods/sc7.md)：团队操作系统竞赛、内核经验与 Tinylibc 的分离背景。
- [SC7 提交级证据表](evidence/sc7.md)：SC7 的提交锚点、协作边界、冲突记录和未纳入材料。
- [Tinylibc 时期](periods/tinylibc.md)：手写阶段、libc/应用生态、仓库内编译器的诞生。
- [Tinylibc 提交级证据表](evidence/tinylibc.md)：阶段锚点、pthread、网络、文章、AI 工作流与来源边界。
- [ToyCCompiler 时期](periods/toy-c-compiler.md)：独立仓库、自举、汇编器与链接器闭环。
- [ToyCCompiler 提交级证据表](evidence/toy-c-compiler.md)：提取来源、stage 语义、种子、工具依赖与复查结果。
- [Toyc 时期](periods/toyc.md)：双重来源、命名统一、Tinylibc 回归及后续扩展。
- [Toyc 整合期提交级证据表](evidence/toyc.md)：双向移植、准确来源快照、渐进兼容与项目身份。
- [初步时间线](timeline.md)：跨仓库的关键边界提交。
- [证据与方法](sources.md)：资料优先级、引用格式、证据边界和复查命令。
- [关键问题台账](questions.md)：跨会话逐题回答、核查和写入专题文档的工作入口。

## 专题解读

- [SC7 与系统工程的边界](essays/sc7-engineering-boundaries.md)：不重复编年和提交表，而是用
  架构、接口、内存模型、运行环境和项目范围五类边界，解释这段经历可以支持哪些
  工程结论，又不能支持哪些自我评价。
- [从逐行掌握到风险驱动理解](essays/agent-and-project-scale.md)：解释个人开发中 agent 如何扩大
  可达规模，以及按需理解、验证边界、少量失控和边际成本之间的取舍。

## 当前结论的范围

当前已完成 SC7、Tinylibc、ToyCCompiler、Toyc 初始整合及 Rasterfall 转向的阶段划分、连续叙事和提交级证据表；SC7 另有工程边界专题。
Tinylibc 已区分课程与兴趣起点、手写终端应用、pthread/论文实验、网络与构建工具、agent 转型和
编译器冲刺，并纳入作者提供的同期文章。作者确认末期以 Claude Code 为客户端、全部接入 DeepSeek
API；`Co-Authored-By: Claude` 因而不能证明使用过 Claude 模型，也不能量化贡献或证明未署名提交
没有使用 AI。作者进一步确认为节省费用主要使用 DeepSeek-V4-Flash，仅两天多使用 Pro。ToyCCompiler 已固定
为从 Tinylibc `87d61e0` 的编译器子树有选择提取，并区分自身编译、可运行下一阶段、多阶段收敛、
tas/tld 接管以及种子驱动默认构建；独立建仓是为集中上下文先完成自举，并已有之后重新整合
Tinylibc 的计划。`687ed29` 中“没有 AI”指作者平静后亲手删去自举成功时写下的过度抒情文字。
Toyc 的 “two parents” 定位先于实际目录整合：自举工具链被视为生态核心，Tinylibc 提供库与应用；
两者统一是编译器最初目标的回归，而非无关项目的事后拼接。初次直接融合遇到现实兼容距离后，开发
转为在 Toyc 中按 Tinylibc 库模块逐项建立编译和功能测试，再进行双向选择性移植。Toyc 的 standalone
是理论上的生态闭环目标；后续为工程效率仍经常使用 GCC，不能把该目标理解成永久排斥宿主工具。
独立 Tinylibc 在首次整合后只再维护约 13.5 小时，最后共有修复由 Toyc 选择性吸收；`a5b959c` 首次用 toyc
编译完整库的 C 源，但仍用系统 `as/ar/ld`，随后默认工具链构建回到 GCC。Rasterfall 从
GCC/Toyc 双路径图形验证中长出，8 月形成独立游戏工程，8 月 31 日退出 Toyc 兼容范围，9 月 3 日
成为仓库治理主线；它是继承 Tinylibc/Toyc 设施的新项目，而非编译器第四阶段。

## 收尾状态与证据边界

本轮考古已经收尾：Q00—Q10 均有稳定落点，SC7、Tinylibc、ToyCCompiler、Toyc 整合和
Rasterfall 转向已经形成连续叙事、时间线与提交级证据表。当前没有必须继续访谈或核查的问题。

尚未取得的赛事原始材料、课程与论文附件、musl 精确参考快照、agent 会话，以及 `fca878f` 前
未提交种子和 stage 日志，只限制个别细节能够达到的证据强度。各时期文档已在相应位置明确这些
边界；除非未来自然出现新材料，不把它们保留为主动待办，也不因这些缺失推翻现有提交级结论。
