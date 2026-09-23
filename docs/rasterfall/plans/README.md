# Rasterfall 活动计划

> 状态：当前
> 所有者：Rasterfall 项目优先级
> 最近核对：2026-09-23

本目录是 Rasterfall 当前工作顺序的唯一入口。架构文档不得声明另一条“当前唯一执行链”。

## Active plan（当前唯一活动计划）

[下一代统一 GPU 渲染器计划](gpu-scene-renderer.md)

冻结三件套已接入独立 Scene native 提交、pin/退休和生命周期专项；RTX 3050 validation/sync 已通过。下一步复用 CPU pose/upload backing、补 Scene GPU 时间戳，随后扩 WORLD opaque。主线开发与签收以 RTX 3050 为准。正常帧仍走旧路径，完整帧性能 A/B 从阶段 3 开始。

计划中的当前切片、前置条件、完成门槛和下一决策点以该文档顶部为准。已完成 checkpoint、撤销实验和
单次设备测量进入 [`../archive/`](../archive/)，不得继续充当优先级来源。

## 更新规则

- 开始或切换主线时，先更新本页，再更新活动计划。
- 同一时间只列一个“当前唯一活动计划”。
- 稳定后的所有权和数据流移入架构文档；复现步骤移入 guide；固定格式移入 reference。
- 活动计划完成或被替代后整体归档，不在入口保留并行的旧执行链。
