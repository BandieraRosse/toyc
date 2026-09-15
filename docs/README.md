# Toyc 文档总览

本目录保存 Toyc 工具链的维护资料与项目历史。Rasterfall 有独立的维护者文档入口，
不要将两套文档的职责混在一起。

## 文档入口

- [Toyc 考古计划](archaeology/README.md)：追溯 Tinylibc、ToyCCompiler 与 Toyc 的传承，
  整理时间线、技术演化、开发方式和可复核证据。
- [编译器时期协作说明](AGENTS-toyc-history.md)：仓库开发重心转向 Rasterfall 前的构建、
  自举和测试约定，仅作为历史参考。
- [Rasterfall 维护者导航](../rasterfall/docs/README.md)：Rasterfall 当前架构、模块边界和验证入口。

## 维护原则

- 当前功能说明仍以根目录 `README.md`、`README_en.md`、`toyc-c-features.md` 和实际构建行为为准。
- 考古文档区分 Git 可验证事实、仓库文本陈述、作者回忆和后续推断。
- 历史提交中的测试数字、能力声明和因果解释只代表当时记录，不自动视为当前事实。

