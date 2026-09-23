# GPU 验收与诊断

> 状态：当前
> 所有者：Rasterfall GPU 验收工作流
> 最近核对：2026-09-23
> 事实入口：`windows/NativeCodex.ps1`、`tools/gpu_acceptance.ps1`、`tools/gpu_metrics.ps1`

本文拥有 GPU 构建后验收、诊断和性能采样流程。GPU 数据流与失败语义见
[GPU 渲染架构](../architecture/gpu-rendering-architecture.md)，性能判定见
[GPU 性能标准](../reference/gpu-performance-standards.md)。物理 GPU 结论必须来自 Windows native package；
WSL、llvmpipe 或 hosted Vulkan 不能替代 native present、驱动和窗口生命周期证据。

## 前置条件

先从仓库根完成 Windows 原生闭环：

```powershell
.\windows\NativeCodex.ps1 doctor
.\windows\NativeCodex.ps1 build
.\windows\NativeCodex.ps1 test
.\windows\NativeCodex.ps1 gpu-test
.\windows\NativeCodex.ps1 acceptance
```

GPU 工具从 `build-windows/rasterfall-windows/` 运行 package 内的 `rasterfall.exe`。不要用裸 exe 运行结果
判断资源或 GPU 故障。开始串行 GPU suite 前关闭残留的 `rasterfall`/`rf-gpu` 进程；性能采样还必须使用
交流电和固定电源方案。

## 日常门禁

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_acceptance.ps1 -Quick
```

Quick 覆盖逻辑回归、graphics/raster differential、resource cache、hosted mixed executor、native mixed
四 extent resize、normal native smoke、角色 vertex diff、presenter audit，以及零 fallback/readback/
CPU framebuffer copy/hot queue-idle。

## 完整验收

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_acceptance.ps1 -Full
```

Full 包含 Quick，并增加 near/mid CPU/GPU 对照、thin-far、300 帧 presenter、world-cycle、Campaign、
30/60 敌人统计、ground/map/character 固定 capture 和角色回滚。结果写入新的
`tmp/gpu-acceptance-<timestamp>/`，以 `manifest.json`、`summary.json`、原始日志和 capture 为证据；这些
生成物不提交仓库。

`-Full` 是日常完整回归，不等于所有专项。涉及 presenter、同步、资源生命周期、驱动兼容或发布判断时，
按风险追加下一节的验证。

## 专项生命周期验收

`tools/gpu_rb0_special.ps1` 串行执行 validation/sync、五类 presenter fault 和 10,000 帧 soak：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_rb0_special.ps1
```

脚本要求可用的 `VK_LAYER_KHRONOS_validation` manifest，默认查找
`tmp/hg2a-tools/mingw64/bin`，也可通过 `-ValidationLayerDirectory` 指定。可用 `-Stage Validation`、
`-Stage Faults` 或 `-Stage Soak` 只跑相关阶段；`-SkipSoak` 只适合明确不需要长时门禁的中间诊断。

必须从输出同时证明 layer 实际加载和 Synchronization Validation 启用；只设置环境变量不算通过。
fault 的预期失败退出码、poisoned generation 退休、recreate 边界和 soak 完整帧数由脚本断言。

## 审计与离线指标

对包含 `FRAME-AUDIT` 的 runtime log 运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_metrics.ps1 `
  -LogPath <runtime.log> -WarmupFrames 16 -ExpectedPath gpu-native
```

审计用于核对 fixed tick、workload、producer、Draw/Raster run、bridge、frame slot 和失败边界；逐帧 audit
会扰动墙钟，不能充当最终 FPS。GPU timestamp 必须按返回的 frame ID 归属；requested/recorded 不等或
dropped 非零的样本无效。若修改 metrics 解析，用
`tools/gpu_metrics_test.ps1 -LogPath <audit.log>` 回放真实日志及受控损坏输入。

## 性能 baseline 与 A/B

单个 package 的固定场景采样使用：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_rb0_sampling.ps1 -Rounds 5
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_rb0_sampling.ps1 -Rounds 5 -NoAudit
```

默认组用于 workload 与边界归因；`-NoAudit` 组用于低扰动性能。未显式指定 `-Rounds` 时只跑一轮，
适合结构诊断，不能支持正式收益结论。

候选比较统一使用 AB/BA runner：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_mixed_ablation.ps1 `
  -BaselineExe <baseline.exe> -CandidateExe <candidate.exe> `
  -OutputDirectory <new-output-directory> -Rounds 5
```

脚本核对交流电、独立 executable hash、每轮结果和场景数量，并交替 AB/BA 顺序。正式报告应给出五轮
单轮范围与中位轮，保持相同 package 资源、分辨率、fixed workload 和电源方案；不得把 audit 与
`-NoAudit` 数字直接比较。

## 结果判定

一次 GPU 验收至少同时报告：

- 使用的设备、驱动、package/executable hash、命令和输出目录；
- suite 退出码，以及 manifest/summary 是否完整；
- required native 帧数和 fallback、readback、CPU copy、invalid transition、hot queue-idle 是否为零；
- 受风险影响的 resize、world retirement、validation/sync、fault 或 soak 结果；
- 性能结论对应的场景、轮数、workload hash、median/P95/P99 与单轮范围。

系统 GPU 枚举可能因 CIM/PnP 权限失败；这只是枚举缺口。adapter、native path 和通过与否以 required
运行的日志、退出码和 frame/presenter audit 为准。
