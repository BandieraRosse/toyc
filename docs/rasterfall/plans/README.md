# Rasterfall 计划入口

> 状态：当前
> 所有者：Rasterfall 项目优先级
> 决策日期：2026-09-26

## 当前优先级

当前没有活动计划。GPU Compute Raster 退役记录已移入[归档](../archive/gpu-compute-retirement/README.md)。硬件 Scene 是 GPU 渲染主线；CPU 软件渲染保留独立入口。

用户确认独立 Scene 阶段收尾：旧地图实机游玩已流畅，性能达到本阶段预期。此结论是本次实机使用判断；历史诊断与正式五轮性能标准的测量范围保持原样，不补写未执行的签收数据。

原[独立 GPU Scene 渲染架构计划](../archive/gpu-scene-renderer.md)及其阶段草案已归档。Scene 的当前运行边界见[GPU 渲染架构](../architecture/gpu-rendering-architecture.md)，复现入口见[Scene 工作流](../guides/gpu-scene-fixture.md)。

## 延期事项

- Outpost V1 扩建：渲染路线退役已完成，后续按新计划继续；阶段记录见 `outpost-v1.md`。
- 敌人槽位复用：降低大批敌人生成时的冷启动资源分配与准备负载；暂不作为本阶段收尾条件。
- 其余小幅性能优化按后续实测需要再立项，不继续沿用归档计划中的阶段 4/5 执行链。

归档中的未完成清单只代表当时的计划，不自动成为当前待办。
