# Toyc 时期

## 研究范围

从 2026-07-11 的 “new beginning, two parents” 开始，研究 Toyc 如何在 ToyCCompiler 的 Git
主线上重新整合 Tinylibc，并扩展工具链、库、应用和后来的 Rasterfall。该时期跨度较大，后续
可能拆成“整合期”“工具链扩展期”“Rasterfall 主线期”。

## 已确认锚点

- `643287b`：首次以 Toyc 名义描述来自 ToyCCompiler 与 Tinylibc 的双重来源。
- `a26e7a6`：将 `app/` 改为 `compiler/`，为后续应用目录腾出边界。
- `5bffca4`：工具链统一采用 `toy*` 命名，避免与 TinyCC 混淆。
- `af6bc30`：引入 Tinylibc 的完整库、应用、架构头和 GCC 构建规则。
- `2698c81`、`75a10cd`：2026-09-03 前后，仓库协作说明和文档主线转向 Rasterfall。

## 待写章节

- “two parents”的项目定位
- Tinylibc 回归时的文件来源和差异
- tcc/tas/tld 到 toyc/toyas/toyld 的命名统一
- `toyar`、浮点、库兼容与应用生态扩展
- Toyc 编译器维护与 Tinylibc 上游的后续关系
- Rasterfall 的诞生及仓库重心迁移
- 从编译器实验到长期工程化的工作流变化

## 待核问题

- `af6bc30` 引入的 Tinylibc 对应哪个上游提交或工作树状态？
- 2026-07-23 Tinylibc 自身又引入 ToyCCompiler 时，两仓是否形成双向移植？
- 何时可以认为项目名称、二进制名称和仓库身份都完成了 Toyc 化？
- Rasterfall 与工具链之间是能力验证、应用生态，还是逐渐独立的新项目主线？

