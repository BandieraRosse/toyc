# RTX 3050 M2 冻结审计基线

> 状态：历史
> 归档原因：2026-09-22 单次设备与 package 的逐帧审计快照
> 当前入口：[GPU 性能标准](../../reference/gpu-performance-standards.md)、[活动计划](../../plans/gpu-raster-bridge.md)
> 证据基线：`2c318a1` metrics schema 6 与 RTX 3050 M2 preflight 三轮审计

可复核证据位于当时的 `tmp/amd3050-m2-breakdown-20260922/`，package SHA-256 为 `E070D6AF4AAE73D87083EDE9DF7981041B65A9481688369BA8DE85E2FA2A9291`。采样基于提交 `f803f70` 加 metrics schema 6 工作区改动，Windows native、交流电、OEM 均衡方案；三轮四场景 workload sequence hash 分别保持一致，结果为 PASS。

这是开启逐帧 audit 的归因基线，记录当时的实现、工作负载和成本组成。它不是低扰动 FPS 基线，不能与未来 `-NoAudit` whole-loop 数字直接比较。

| 场景 | 三轮 whole median 的中位轮 | whole P95 三轮范围 | GPU Raster median 三轮范围 | bridge |
| --- | ---: | ---: | ---: | ---: |
| near 0 | 34.151 ms | 48.522–65.615 ms | 7.050–7.232 ms | 4 / 29,491,200 bytes |
| near 30 | 52.130 ms | 88.065–127.238 ms | 12.022–15.097 ms | 4 / 29,491,200 bytes |
| near 60 | 73.555 ms | 101.032–155.222 ms | 16.155–17.963 ms | 4 / 29,491,200 bytes |
| Campaign | 39.594 ms | 57.344–83.401 ms | 9.470–9.678 ms | 10 / 73,728,000 bytes |

同组 preflight 子阶段的三轮中位结果见[Mixed 路径优化阶段归档](gpu-mixed-optimization-20260922.md)：preflight 20.482–24.516 ms，动态资源销毁 5.150–5.834 ms，动态资源创建/上传/descriptor/skinning 10.256–10.881 ms，其中 skinning fence 约 5.236–5.488 ms。CPU dynamic pack 为 0.801–0.960 ms。

归档时尚缺同一 package 的正式 `-NoAudit` 五轮基线；后续步骤由[活动计划](../../plans/gpu-raster-bridge.md)维护。
