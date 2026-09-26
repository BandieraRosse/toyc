# GPU Compute Raster 退役归档

> 状态：历史资料
> 归档日期：2026-09-26

这里保存 GPU Compute Raster、mixed executor、Draw/Raster bridge 退役前的设计、验证指南和执行计划快照。这些文档描述当时的实现与诊断命令；对应源码、shader、生成器和脚本已从当前工作树移除。源码细节可从 Git 历史查看。

| 历史记录 | 内容 |
| --- | --- |
| [退役计划快照](gpu-compute-retirement-plan-20260926.md) | 退役目标、顺序与原完成门槛 |
| [旧 GPU 架构](gpu-rendering-architecture-20260926.md) | mixed、Raster、bridge 与 Scene 迁移期数据流 |
| [旧 GPU 验证指南](gpu-validation-20260926.md) | Quick/Full、RB0 与 mixed 诊断流程 |
| [旧构建指南](build-platforms-20260926.md) | 旧 Raster 构建和诊断入口 |

当前 GPU 渲染入口是独立 Scene，CPU 软件渲染独立保留。维护时从[当前 GPU 架构](../../architecture/gpu-rendering-architecture.md)、[Scene 工作流](../../guides/gpu-scene-fixture.md)和[Windows Native](../../guides/windows-native.md)进入。

## 完成记录

2026-09-27：Core 正常帧只保留 CPU 与 Scene mode；Vulkan backend/graphics 的 compute raster、load pass 和 bridge pipeline、GPU Raster service API、ABI、pack/bin、shader 与内嵌 SPIR-V、旧诊断测试及脚本均从当前源码和构建目标移除。CPU `toy_raster_cmd` 保留。Windows 玩家构建、CPU 逻辑回归、4 帧 CPU 呈现、120 帧硬件 Scene、Scene graphics 测试（93 项）、GPU service 测试、Scene 互动/resize/world cycle/combat/160 帧连续运行、五类 present 故障注入和 `acceptance` 通过。`tools/check_docs.ps1` 与 `git diff --check` 通过。

归档中的计划快照停留在退役中途状态，仅供复盘；上述记录是最终实施与验证结果。旧源码可从本次删除前的 Git 历史查看。
