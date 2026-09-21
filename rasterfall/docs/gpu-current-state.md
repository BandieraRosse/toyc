# GPU 当前状态

> 文档更新：2026-09-21
> 源码核对基线：`208532c`

本文只记录当前支持范围、回滚边界、已知限制和可执行验证入口。阶段过程与历史性能数字见
[Hardware Graphics 归档](archive/hardware-graphics-2026-09/README.md)。

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
