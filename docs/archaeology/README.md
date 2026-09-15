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
    │  基于 MIT XV6，支持 RISC-V 与 LoongArch
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
- [Tinylibc 时期](periods/tinylibc.md)：手写阶段、libc/应用生态、仓库内编译器的诞生。
- [ToyCCompiler 时期](periods/toy-c-compiler.md)：独立仓库、自举、汇编器与链接器闭环。
- [Toyc 时期](periods/toyc.md)：双重来源、命名统一、Tinylibc 回归及后续扩展。
- [初步时间线](timeline.md)：跨仓库的关键边界提交。
- [证据与方法](sources.md)：资料优先级、引用格式、待核问题和复查命令。
- [关键问题台账](questions.md)：跨会话逐题回答、核查和写入专题文档的工作入口。

## 当前结论的范围

本轮只建立框架并确认仓库级传承。关于 AI 参与强度和个人开发方式，当前依据主要是作者口述；
提交中的 `Co-Authored-By` 可证明部分提交显式记录了 Claude，但不能单独量化 AI 贡献，也不能
证明未署名提交没有使用 AI。DeepSeek API 和网页对话的具体使用过程仍需聊天记录、脚本、账单、
本地日志或作者访谈等材料交叉验证。

## 下一阶段

- 为 Tinylibc 建立阶段划分，先覆盖编译器出现前的手写主线。
- 对照 SC7 的 `tlibc` 分支与 Tinylibc 根提交，确认分离时继承的代码和设计。
- 对照 Tinylibc `87d61e0` 附近源码与 ToyCCompiler 根提交，形成文件级来源表。
- 按提交重建 ToyCCompiler 从最小编译器到 stage-10 收敛的过程。
- 找出 Toyc 引入 Tinylibc 时的准确源快照，而不只依赖提交说明。
- 建立“作者回忆待访谈”清单，记录工具使用、关键决策、失败尝试与情绪背景。
