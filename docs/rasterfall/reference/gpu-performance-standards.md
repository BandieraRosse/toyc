# GPU 性能标准

> 状态：当前 reference
> 所有者：Rasterfall GPU 性能标准
> 最近核对：2026-09-23
> 测量证据：[2026-09-22 冻结审计](../archive/gpu-2026-09-22/gpu-performance-baseline.md)

本文定义 Rasterfall 当前使用的两档 Windows 物理 GPU 标准。目标不是当前成绩；当前执行顺序只见[活动计划](../plans/gpu-scene-renderer.md)，采样工作流见[GPU 验收指南](../guides/gpu-validation.md)。

## 两档设备与职责

| 标准 | 当前设备 | 职责 |
| --- | --- | --- |
| 高性能标准 | AMD 5600H + NVIDIA GeForce RTX 3050 Laptop GPU | 性能立项、实现取舍、正式 A/B 与 60 FPS 阶段签收的主设备 |
| 普通标准 | Intel Iris Xe | 正确性、完整功能、生命周期回归与普通性能下限；不再决定优化实施顺序 |

当前不设置第三档设备，不把 WSL、llvmpipe、软件呈现或未列出的 GPU 当成任一物理设备标准。两档均使用
Windows native、required GPU、native present 和相同 package/固定 workload；禁止用 fallback、readback、
CPU framebuffer copy、降低 workload 或删除应见内容达成绩效数字。

## 高性能目标

初期产品目标是在 1280×720 下让绝大多数正常游戏情况稳定 60 FPS。固定验证场景为 near 0、near 30、
near 60、Campaign，并保留 resize、world-cycle 与 thin-far 正确性门禁。

| 场景级别 | whole-loop median | whole-loop P95 | whole-loop P99 | 解释 |
| --- | ---: | ---: | ---: | --- |
| near 0、near 30、Campaign 等常规场景 | ≤ 14.5 ms | ≤ 16.67 ms | ≤ 20 ms | 14.5 ms 为正常预算，给系统和驱动抖动保留余量 |
| near 60 压力场景最终目标 | ≤ 16.67 ms | ≤ 16.67 ms | ≤ 20 ms | 作为 60 FPS 压力门；阶段中可先达到 median ≤ 16.67 ms、P95 ≤ 20 ms |

这些值是目标，不是当前成绩。正式结论取同 package、交流电、固定电源方案、固定 workload 的低扰动
五轮 AB/BA 采样；报告五个单轮指标的中位数，同时保留各轮范围，不把 audit 墙钟与低扰动帧率混用。

## 普通标准

Intel Iris Xe 初期以完整功能和稳定 30 FPS 为下限：常规场景 whole-loop median ≤ 28 ms、P95 ≤ 33.3 ms；
near 60 压力场景暂不设 60 FPS 硬门。候选不得相对其冻结的同 workload 低扰动基线产生可重复的 10% 以上
退化。Intel 继续执行 required native、Quick/Full、resize、world-cycle、thin-far、零 fallback/readback/
CPU copy 与受影响专项门禁，但默认不承担每个中间切片的五轮性能选择。
