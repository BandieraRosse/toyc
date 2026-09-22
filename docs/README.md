# Toyc / Rasterfall 维护文档

`docs/` 是本仓库唯一的维护文档根。先按任务选择子系统，不要从归档或日期化调查记录进入当前实现。

| 任务范围 | 入口 | 内容 |
| --- | --- | --- |
| Rasterfall | [rasterfall/README.md](rasterfall/README.md) | 当前主线；架构、构建、GPU、玩法、资产、地图和网络 |
| Toyc / Tinylibc | [toolchain/README.md](toolchain/README.md) | 编译器、公共库、自举、平台迁移和历史考证 |
| 应用与离线工具 | [applications/README.md](applications/README.md) | `app/`、`tools/` 及跨平台应用边界 |
| GPT-2 / Qwen2 | [llm/README.md](llm/README.md) | `llm/` 的实现、构建和验证入口 |
| 文档与仓库治理 | [repository/documentation.md](repository/documentation.md) | 文档类型、状态、放置和更新规则 |

## 事实优先级

1. 可执行 CLI、测试、构建脚本和实际行为。
2. 当前架构与 reference 文档。
3. 操作指南。
4. 当前活动计划。
5. `archive/` 中的历史现场。

文档和实现不一致时，应核对 Makefile、脚本、参数解析与调用入口；以实际行为修正文档，而不是用历史记录覆盖当前事实。

## 用户文档

- 仓库与工具链总览：[../README.md](../README.md)
- Rasterfall 构建和运行：[../rasterfall/README.md](../rasterfall/README.md)
- Toyc 语言特性：[../toyc-c-features.md](../toyc-c-features.md)

这些页面面向使用者，不替代维护者架构与计划。

