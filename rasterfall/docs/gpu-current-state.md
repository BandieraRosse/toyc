# GPU 当前状态

> 文档更新：2026-09-21
> 源码核对基线：`fc75009`；HG-5B 签收、统一 GPU 验收脚本及当前开发方向

本文只记录当前支持范围、回滚边界、已知限制和可执行验证入口。阶段过程与历史性能数字见
[Hardware Graphics 归档](archive/hardware-graphics-2026-09/README.md)。

## 当前开发阶段

Rasterfall 当前处于 GPU 渲染持续开发阶段。HG-0 至 HG-5B 已完成，现阶段不是继续按历史计划机械增加
HG 编号，而是先补强 Windows 物理 GPU 覆盖、恢复专项稳定性矩阵，并量化剩余 RasterCmd 和整帧耗时。
主要开发、构建编排、GPU 实机运行和签收环境为 Windows 原生 PowerShell，入口是
`windows/NativeCodex.ps1` 与本页列出的统一验收脚本。

共享 C 源码和 freestanding Linux 路径继续保留。WSL、llvmpipe 和 Linux hosted Vulkan 只适合辅助编译
或 correctness 诊断；WSL 路径不保证随 GPU 主线同步更新、可构建或运行结果正确，也不能替代 Windows
native present、物理驱动、窗口生命周期和性能验收。

## 当前支持范围

- Windows normal runtime 支持 Core-owned mixed Draw/Raster frame、Vulkan native present、Post/fog、
  overlay、双帧资源槽和 swapchain resize。
- 普通 opaque static RMESH、持久 ground/map/boundary geometry 和角色 body Draw 使用 hardware
  graphics；动态、透明和未迁移 producer 继续使用 compute RasterCmd。
- 角色 normal path 使用 GPU skinning；CPU 仍拥有 pose、IK、socket、gear 与 weapon placement。
- world generation retirement、frame pin、GPU cache 和 presenter completion 均有逐帧审计。
- Intel Iris Xe 已覆盖 strict native、Fog、Campaign enemy、resize、world cycle、角色 vertex diff 和
  长帧稳定性；RTX 3050 已覆盖 native swapchain smoke。Linux hosted Vulkan 路径用于 correctness，
  Linux normal window/native presentation尚未按同一矩阵验收。

## 回滚与失败边界

- `--gpu-character-skinning-off` 只回滚角色 vertex backing 为 CPU-skinned upload。
- 省略 `--gpu-native-present` 可用于软件呈现 A/B；这不是 required 模式的运行时降级。
- `--gpu-required` 要求 native present，任何 unsupported、preflight、submit、present、readback 或
  CPU copy 都应非零退出，不允许部分 GPU 帧后整帧 CPU replay。
- CPU renderer 仍是独立完整路径，用于对照和不启用 GPU renderer 的正常运行。

## 已知限制

- 当前实机覆盖集中在 Windows Intel；其他厂商、Linux native presentation、设备丢失恢复和
  validation layer 专项不包含在日常 Quick 门禁中。
- 10,000 帧 soak 是专项验证，不属于 Full 的固定耗时范围。
- fixed scene 和 capture 参数属于诊断接口；玩家参数完整清单以 `rasterfall.exe --help` 为准。
- frame-dynamic 角色资源不使用跨实例聚合 position bound 代替单 Draw bound；`thin-far` 是 Full
  门禁中的固定回归场景。

## 当前性能判断

HG-5B 最终 Intel Iris Xe 基线中，near 30/60 敌人的稳态 whole-loop 中位数约为
29.337/54.782 ms，GPU Raster 约为 11.449/27.363 ms，GPU Draw 约为 1.816/1.814 ms。
这些数字只用于确定当前优化优先级，不构成跨机器或跨厂商性能承诺。

60 敌人场景中 Draw 已不是主要成本；继续微调 Draw 或 skinning shader 预计不是最高收益方向。
下一轮工作应先按 producer 分类统计剩余 RasterCmd、bridge、透明/特效、高级材质、gear/weapon 和
特殊敌人 rigid 内容，并分解 CPU worker、slot wait、acquire、submit、present 与 GPU 阶段时间。
在获得这份成本清单前，不预设新的 HG-6 迁移范围。

## 验证命令

先完成 Windows package 与 hosted GPU test targets，再从仓库根运行：

```powershell
powershell -ExecutionPolicy Bypass -File tools/gpu_acceptance.ps1 -Quick
powershell -ExecutionPolicy Bypass -File tools/gpu_acceptance.ps1 -Full
```

Quick 覆盖 logic test、hosted graphics/raster differential、resource cache、native
mixed near smoke、角色 vertex diff，以及零 fallback/readback/CPU framebuffer copy/hot queue-idle。

Full 包含 Quick，并增加 near/mid CPU/GPU 对照、thin-far、300 帧 presenter、world-cycle、Campaign 320 帧、
30/60 敌人统计、ground/map/character 固定采集和角色回滚。结果写入
`tmp/gpu-acceptance-<timestamp>/manifest.json` 与 `summary.json`；原始日志、capture 和 metrics 同目录
保存但不提交。性能统计可独立重算：

```powershell
powershell -ExecutionPolicy Bypass -File tools/gpu_metrics.ps1 `
  -LogPath <runtime.log> -WarmupFrames 16 -ExpectedPath gpu-native
```

底层独立目标继续保留：`rf-gpu-graphics-test`、`rf-gpu-raster-test`、
`rf-gpu-raster-diff-test`、resource-cache test 和 mixed-executor test。它们验证 ABI、资源与执行器
合同，不由窗口程序替代。

`-Full` 是当前日常完整回归，不等于重新执行全部历史 HG 签收矩阵。四 extent resize、Vulkan validation/
sync validation、fault injection、10,000 帧 soak、跨厂商完整 Full 和完整角色 CPU/native 人工组图属于
专项签收；涉及 presenter、同步、资源生命周期、驱动兼容或发布判断时必须按风险单独补跑并报告。

## 下一阶段立项条件

新 GPU 阶段应先满足以下前置条件：

- 至少在 Intel 与另一种物理 GPU 上运行当前 Full，明确厂商差异；条件允许时补 AMD。
- presenter/synchronization 改动恢复 resize、validation、fault injection 和长时 soak。
- 新增按 producer 分类的 RasterCmd、bridge、CPU encode 与 GPU timestamp 审计。
- 使用同一 package、固定电源/冷启动条件做多轮 median、P95/P99 和最慢帧分类。

若审计确认主要成本来自可迁移 opaque 内容，下一阶段可以限定为 gear、weapon、rigid enemy part 或
正式支持的 textured opaque Draw；透明、粒子、overlay、复杂 VFX、动态光照和新材质体系不应顺带混入。
若主要成本来自 compute Raster 或同步边界，则应优先做调度和吞吐优化，而不是扩大 Draw 类型。
