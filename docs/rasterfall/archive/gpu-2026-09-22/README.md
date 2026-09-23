# GPU 2026-09-22 阶段记录

> 状态：历史
> 归档：2026-09-23
> 当前架构：[GPU 渲染架构](../../gpu-rendering-architecture.md)
> 当前验收：[GPU 验收与诊断](../../guides/gpu-validation.md)
> 当前计划：[GPU Raster / Bridge 收敛计划](../../plans/gpu-raster-bridge.md)

本目录保留 2026-09-22 前后的单次设备现场、调查、修复、候选否决和阶段执行证据。它们不再拥有当前
状态、验收流程或实施顺序；文件中的“当前”“下一步”和顶层相对链接均按历史现场理解。

- `gpu-current-state.md`：原综合状态页，稳定边界已分别并入当前架构和验收指南。
- `gpu-mixed-optimization-20260922.md`：M1/M2 阶段执行与测量记录。
- `gpu-performance-baseline.md`：RTX 3050 M2 三轮逐帧审计基线。
- `gpu-rb0-investigation-20260922.md`、`gpu-rb0-repair-20260922.md`、
  `gpu-rb0-special-20260922.md`：RB-0 调查、修复和签收链。
- `gpu-rb2-candidate-review-20260922.md`：候选盘点与已撤销 ablation。
- `gpu-nvidia-swapchain-compat.md`：RTX 3050 首帧兼容故障现场。

需要恢复其中方案时，先按当前架构核对实现，再把仍有效的工作重新写入唯一活动计划；不得直接把归档
页面重新作为入口。
