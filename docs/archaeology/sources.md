# 证据与方法

## 证据等级

按下列层次保存材料，不把不同性质的证据混写成确定事实：

1. **对象级证据**：提交对象、父提交、树、blob、tag、构建产物及可复现命令输出。
2. **仓库内陈述**：提交说明、README、CLAUDE.md、脚本注释和历史文档。
3. **外部同期材料**：GitHub 页面、issue、release、聊天记录、API 日志、账单和本地工作日志。
4. **作者回忆**：作者对动机、工具、过程和感受的口述。
5. **研究者推断**：由若干证据拼出的解释，必须注明推断过程和不确定性。

提交说明属于同期陈述，不等于其宣称的功能已经被独立复现。`Co-Authored-By` 只说明提交元数据
显式署名，不应用来计算某个工具生成了多少代码。

## 本地资料

| 项目 | 本地路径 | 远端 | 本轮观察到的提交范围 |
|---|---|---|---|
| SC7 | `../SC7` | `WHU-SC7/SC7` | `ec37618` 至主分支 `1a668a3`，620 个提交 |
| Tinylibc | `../Tinylibc` | `WHU-SC7/Tinylibc` | `fce216c` 至 `a566206`，211 个提交 |
| ToyCCompiler | `../ToyCCompiler` | `BandieraRosse/ToyCCompiler` | `22ffcc8` 至 `58ac389`，96 个提交 |
| Toyc | `.` | `BandieraRosse/toyc` | `22ffcc8` 至本轮 HEAD `cf8c5cd`，773 个提交 |

提交数量和 HEAD 只记录本轮调查现场，后续仓库推进后不作为永久统计结论。

## 常用复查命令

```sh
git -C ../SC7 log --all --reverse --date=iso-strict
git -C ../Tinylibc log --reverse --date=iso-strict
git -C ../ToyCCompiler log --reverse --date=iso-strict
git log --reverse --date=iso-strict
git show --stat --summary <commit>
git diff --no-index <old-tree-path> <new-tree-path>
git log --follow -- <path>
git blame <commit> -- <path>
```

跨仓库比较不能只看文件名。应先固定两端提交，再对目录树做 hash、相似度和语义对照，并记录
重命名、格式化、生成文件与二进制种子等干扰因素。

## 作者口述基线

以下内容来自本计划启动时的作者说明，尚未全部得到外部材料佐证：

- 项目主线为 Tinylibc → ToyCCompiler → Toyc，三个项目存在传承关系。
- SC7 是作者在 2025 年参加操作系统内核比赛时的团队作品。
- Tinylibc 主要属于手写时期；学习依赖网页端对话，也复制过部分代码原型，调试和主要开发由
  作者自己进行。
- ToyCCompiler 时期高强度使用 Claude Code 和 DeepSeek API，并完成自己的 C 编译器。
- Toyc 保存了大部分提交信息；两个历史项目的公开仓库已克隆在当前仓库上级目录。
- 整条 C 项目主线体现了作者逐渐深入计算机底层、从 C 编程提升到 C 项目能力，并逐渐使用
  coding agent 的过程。

后续引用作者口述时应标注访谈日期、问题原文、回答原文或摘要，以及作者是否在事后修订。

## 待收集材料

- 四个 GitHub 仓库的创建时间、默认分支变化、release、tag、issue 和仓库描述历史。
- SC7 各比赛阶段的分支、团队分工，以及 `tlibc` 分支与 Tinylibc 的关系。
- Tinylibc 从 SC7 分离前的代码来源和授权边界。
- 网页端对话的服务、时间范围、保留形式和可公开范围。
- Claude Code 会话、本地配置、命令历史和显式署名策略。
- DeepSeek API 调用脚本、模型版本、提示词、输出与费用记录。
- 未进入 Git 的失败实现、临时文件、截图、构建日志和二进制产物。
