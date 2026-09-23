# Hardware Graphics 2026-09 归档

> 状态：历史
> 归档原因：阶段完成或已由当前文档取代
> 当前入口：[GPU 渲染架构](../../architecture/gpu-rendering-architecture.md)

> 文档更新：2026-09-21
> 源码核对基线：`208532c`

本目录保存 2026 年 9 月 Hardware Graphics 工作的原始阶段记录。它们保留设计动机、实机证据、
故障背景和当时的性能数字，但不再更新，也不是当前接口或验证命令的事实入口。

## 阶段与最终结果

| 阶段 | 目标与最终结果 | 主要提交 |
| --- | --- | --- |
| HG-0 | 冻结事实、架构和可复核基线 | `37b47c0` |
| HG-1 | 建立 Draw/reference、资源 identity、generation 与 frame pin | `9cecfc1` |
| HG-2 | 建立 indexed graphics、mixed executor、统一 target/command 与 Windows presenter | `0d72158`、`18f8c48` |
| HG-3 | 扩围 opaque static RMESH 并消除连续 Draw 的空 flush | `21d0a89` |
| AI frontend | 收敛 pose、skinned vertex、gear/weapon placement 与 bounds | `21d0a89` 后续工作区 |
| HG-4 | 将 ground 与 map/boundary geometry 固化为 persistent world mesh | `8b9b84f`、`77b3f5c` |
| HG-5 | 接入动态角色 Draw、GPU skinning、vertex diff 与独立回滚 | `aa06226`、`208532c` |

## 为什么归档

阶段文件包含已经完成的下一步、临时 checkpoint 名称、特定机器性能数值和已撤销的中间结论。
继续把它们放在维护者主线会掩盖当前所有权和验证边界。归档保留考证价值，同时让主线文档只描述
现行架构与可执行事实。

## 当前事实入口

- [GPU 渲染架构](../../architecture/gpu-rendering-architecture.md)
- [GPU 验收与诊断](../../guides/gpu-validation.md)
- [渲染架构](../../architecture/rendering-architecture.md)
- [构建与平台](../../build-platforms.md)
- `rasterfall.exe --help`
- `tools/gpu_acceptance.ps1 -Quick` / `-Full`
- `tools/gpu_metrics.ps1`

归档中的链接按原文保留；相对链接可能仍指向归档前位置，阅读时以本 README 和当前事实入口为准。
