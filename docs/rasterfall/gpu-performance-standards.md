# GPU 性能标准与冻结基线

> 状态：当前 reference
> 所有者：Rasterfall GPU 性能标准
> 最近核对：2026-09-23
> 证据基线：`2c318a1` metrics schema 6 与 RTX 3050 M2 preflight 三轮审计

本文冻结 Rasterfall 当前仅使用的两档 Windows 物理 GPU 标准，并区分“已经测得的基线”与“后续优化
目标”。性能开发以高性能标准为主线；普通标准保留正确性、功能完整性和合理性能下限，不要求两个设备
达到相同帧率。

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

## RTX 3050 冻结审计基线

当前可复核证据位于 `tmp/amd3050-m2-breakdown-20260922/`，package SHA-256 为
`E070D6AF4AAE73D87083EDE9DF7981041B65A9481688369BA8DE85E2FA2A9291`。采样基于提交 `f803f70` 加
metrics schema 6 工作区改动，Windows native、交流电、OEM 均衡方案；三轮四场景 workload sequence hash
分别保持一致，结果为 PASS。

这是开启逐帧 audit 的归因基线。它冻结当前实现、工作负载和成本组成，不是低扰动 FPS 基线，也不能直接
与未来 `-NoAudit` whole-loop 数字比较。

| 场景 | 三轮 whole median 的中位轮 | whole P95 三轮范围 | GPU Raster median 三轮范围 | bridge |
| --- | ---: | ---: | ---: | ---: |
| near 0 | 34.151 ms | 48.522–65.615 ms | 7.050–7.232 ms | 4 / 29,491,200 bytes |
| near 30 | 52.130 ms | 88.065–127.238 ms | 12.022–15.097 ms | 4 / 29,491,200 bytes |
| near 60 | 73.555 ms | 101.032–155.222 ms | 16.155–17.963 ms | 4 / 29,491,200 bytes |
| Campaign | 39.594 ms | 57.344–83.401 ms | 9.470–9.678 ms | 10 / 73,728,000 bytes |

同组 preflight 子阶段的三轮中位结果已冻结在
[Mixed 路径优化阶段归档](archive/gpu-2026-09-22/gpu-mixed-optimization-20260922.md)：preflight 20.482–24.516 ms，动态资源
销毁 5.150–5.834 ms，动态资源创建/上传/descriptor/skinning 10.256–10.881 ms，其中 skinning fence
约 5.236–5.488 ms。CPU dynamic pack 仅 0.801–0.960 ms。

当前尚缺同一 package 的正式 `-NoAudit` 五轮基线。M2 候选开发前先补 baseline exe 的低扰动五轮采样；
随后使用 `gpu_mixed_ablation.ps1` 保持 baseline/candidate hash、AB/BA、交流电和场景一致。审计基线永久
保留作成本与合同对照，低扰动基线建立后另行追加，不覆盖本节。

## 优化路线与决策门

1. M2：按 frame slot 持久复用动态 buffer/resource/descriptor；容量只增长，稳态不销毁或重建，暂时保留
   skinning submit/wait。
2. M3：仅在 M2 后 skinning fence 仍是最大固定成本时，将上传与 skinning 纳入帧命令依赖链；必须证明
   等待没有转移到 slot recycle、present 或其他 fence。
3. 重新归因：根据 RTX 3050 低扰动 P95/P99 决定先做 depth bridge，还是迁移高成本 opaque Raster。
4. 软件 Raster 只做选择性迁移。优先 gear/weapon 及能实际减少 Draw/Raster run 的相邻 opaque 内容；透明、
   粒子、overlay、复杂 VFX 与未冻结光照合同的内容继续留在 Raster。
5. GPU 固定成本收敛后再削减 CPU producer：先做可见性/LOD、actor presentation snapshot、静态模板与
   pose/socket/gear 缓存，再处理重复扫描/分配；只有 CPU producer 仍持续超过约 4 ms 才评估多线程。

每阶段只保留一个独立 ablation。迁移不能以 RasterCmd 数量下降单独签收，必须同时证明最终画面等价、
GPU Raster 与 whole-loop 改善、Draw 增量可控、真实 run/bridge 不增加。
