# GPU 当前状态

> 文档更新：2026-09-22
> 源码核对基线：RB-1 模块化队友 opaque 提交编排工作区

本文只记录当前支持范围、回滚边界、已知限制和可执行验证入口。下一轮实施顺序见
[GPU Raster / Bridge 收敛计划](gpu-raster-bridge-plan.md)，阶段过程与历史性能数字见
[Hardware Graphics 归档](archive/hardware-graphics-2026-09/README.md)。

## 当前开发阶段

最新专项状态以 [RB-0 专项续接](gpu-rb0-special-20260922.md) 为准；其中记录 raw 输入覆盖和
resize 附件生命周期修复，以及真实启用 validation/sync 的证据。下方旧结果不能替代新 package 验收。

同 fixed tick 的 near/mid 复核已补齐 mixed SKY 与世界血条 overlay coverage。RB-0 签收后，Rasterfall
runtime 已统一为无 fog：CPU/GPU normal producer 只提交中性 fog，GPU Post 不再由 CLI/Core 接入。
RasterCmd fog 字段、CPU/GPU consumer 和底层 Post Fog V0 仍保留 ABI 与专项测试语义，但不参与正常游戏画面。

负载/测量及专项生命周期修复已完成 Windows Quick/Full、validation/sync、fault 与 soak；
采样结果和限制见 [RB-0 专项续接](gpu-rb0-special-20260922.md)。下文性能数字为修复前历史现场，
不能替代新基线。低扰动 whole_us 已统一到整轮起点，兜底分类为 unclassified。schema 5 离线归因已完成：
正确口径五轮没有复现历史 near60 数百毫秒双态；55 个低扰动未分类慢帧均由互斥顶层 phase 覆盖，
但缺少 phase 内因果计时，按根因未知的已知风险冻结。此前 fog/远墙差异已通过统一禁用 runtime fog 消除。
RB-0 最终签收 package `F407FD19BFC1FBC049ADE78EA21EB9E71D7CD2B627C0CAD388CB1DC8E363D762` 已通过
Full 31/31、validation/sync、五类 fault 和 10,000 帧 soak；证据与 SKY 轴向朝向 validator 修复见专项续接。
Intel 单设备 RB-0 已签收，第二物理 GPU 仍暂缓，当前结论不外推为跨设备签收。

RB-1 已合并连续跨 producer Draw spans，并将正常 world 中模块化队友改为 body Draw 集中冻结、随后按原
actor 顺序提交 opaque gear/weapon RasterCmd。Intel native near 审计的实际 Draw run 从 5 降到 2，bridge
transfer 从 10 降到 4、bytes 从 73,728,000 降到 29,491,200；Windows `gpu-test`、Quick 8/8 与 Full
均通过。该结果证明结构计数和固定回归正确性，尚未完成固定 workload 多轮性能签收或第二物理 GPU 复核。

Rasterfall 当前处于 GPU 渲染持续开发阶段。HG-0 至 HG-5B 已完成，现阶段不是继续按历史计划机械增加
HG 编号，而是先补强 Windows 物理 GPU 覆盖、恢复专项稳定性矩阵，并量化剩余 RasterCmd 和整帧耗时。
主要开发、构建编排、GPU 实机运行和签收环境为 Windows 原生 PowerShell，入口是
`windows/NativeCodex.ps1` 与本页列出的统一验收脚本。

共享 C 源码和 freestanding Linux 路径继续保留。WSL、llvmpipe 和 Linux hosted Vulkan 只适合辅助编译
或 correctness 诊断；WSL 路径不保证随 GPU 主线同步更新、可构建或运行结果正确，也不能替代 Windows
native present、物理驱动、窗口生命周期和性能验收。

## 当前支持范围

- Windows normal runtime 支持 Core-owned mixed Draw/Raster frame、Vulkan native present、Post bypass、
  overlay、双帧资源槽和 swapchain resize。
- 普通 opaque static RMESH、持久 ground/map/boundary geometry 和角色 body Draw 使用 hardware
  graphics；动态、透明和未迁移 producer 继续使用 compute RasterCmd。
- 角色 normal path 使用 GPU skinning；CPU 仍拥有 pose、IK、socket、gear 与 weapon placement。
- world generation retirement、frame pin、GPU cache 和 presenter completion 均有逐帧审计。
- Intel Iris Xe 已覆盖 strict native、Campaign enemy、resize、world cycle、角色 vertex diff 和
  长帧稳定性；RTX 3050 已覆盖 native swapchain smoke。Linux hosted Vulkan 路径用于 correctness，
  Linux normal window/native presentation尚未按同一矩阵验收。
- `rasterfall-gpu-mixed-test.exe --native-window` 独立覆盖四个连续 extent；extent target/swapchain
  重建期间稳定 mesh/texture upload counter 保持不变。该底层合同仍独立于 Full 的 normal-runtime 覆盖。

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

以下历史数字不能直接作为下一批迁移的收益基线。最新 [RB-0 排查](gpu-rb0-investigation-20260922.md)
确认 actor 局部裁剪会用 flush 前的起点恢复已消费的命令数量，GPU timestamp 只覆盖有限前缀，
graphics fence 计数包含正常蒙皮资源提交等待。先修复命令范围与测量覆盖，再重建基线；
可重复 workload 不等于正确 workload，`external_scheduler` 也不是已证实的外部调度原因。

HG-5B 最终 Intel Iris Xe 基线中，near 30/60 敌人的稳态 whole-loop 中位数约为
29.337/54.782 ms，GPU Raster 约为 11.449/27.363 ms，GPU Draw 约为 1.816/1.814 ms。
这些数字只用于确定当前优化优先级，不构成跨机器或跨厂商性能承诺。

RB-1 前的两轮 Full 确认，60 敌人场景中 Draw 已不是主要成本；一轮预热后 whole-loop 中位数/P95 为
50.12/143.92 ms，GPU Raster 为 22.08/62.64 ms，GPU Draw 为 1.43/2.08 ms。典型重帧仍有
10 次、73,728,000 bytes bridge transfer；RB-1 当前 normal near 结构计数已降为 4 次、29,491,200 bytes，
但尚未完成多轮性能重采样。`native_present_ms` 中位数约 0.02 ms，因此较大的
`present_wall_ms` 不能归因为 present API 本身。

下一轮按 [GPU Raster / Bridge 收敛计划](gpu-raster-bridge-plan.md) 实施：先固定 workload 并补齐
producer、bridge、slot/fence wait 与 P95/P99 归因，再收敛 bridge/同步，随后只迁移数据证明高成本的
opaque enemy、gear、weapon 或 rigid 内容。透明、粒子、overlay、复杂 VFX 和新材质体系不顺带进入。

## 验证命令

先完成 Windows package 与 hosted GPU test targets，再从仓库根运行：

```powershell
powershell -ExecutionPolicy Bypass -File tools/gpu_acceptance.ps1 -Quick
powershell -ExecutionPolicy Bypass -File tools/gpu_acceptance.ps1 -Full
```

Quick 覆盖 logic test、hosted graphics/raster differential、resource cache、mixed executor hosted、
mixed native 四 extent resize、native mixed near smoke、角色 vertex diff，以及逐帧完整 presenter audit
与零 fallback/readback/CPU framebuffer copy/hot queue-idle。

Full 包含 Quick，并增加 near/mid CPU/GPU 对照、thin-far、300 帧 presenter、world-cycle、Campaign 320 帧、
30/60 敌人统计、ground/map/character 固定采集和角色回滚。结果写入
`tmp/gpu-acceptance-<timestamp>/manifest.json` 与 `summary.json`；原始日志、capture 和 metrics 同目录
保存但不提交。性能统计可独立重算：

其中 presenter-300、Campaign 320 和 near 30/60 性能统计均启用固定 16 ms simulation tick；指标 JSON
包含 P99、tick/accumulator 集合、关键命令与 bridge 计数范围，以及最慢 5% 帧的初步分类。normal frame
日志还逐 producer 输出 RasterCmd/span/opaque/transparent，并逐 bridge direction 输出 color/depth traffic、
layer、前后 producer 和 target generation；schema 4 metrics JSON 提供逐类范围、whole-loop 相关系数、
慢帧明细，以及排除计时和 target generation 后的逐帧规范化 workload 与 SHA-256。

```powershell
powershell -ExecutionPolicy Bypass -File tools/gpu_metrics.ps1 `
  -LogPath <runtime.log> -WarmupFrames 16 -ExpectedPath gpu-native
```

RB-0 的同 package 五轮固定 workload 采样入口为：

```powershell
powershell -ExecutionPolicy Bypass -File tools/gpu_rb0_sampling.ps1 -Rounds 5
powershell -ExecutionPolicy Bypass -File tools/gpu_rb0_sampling.ps1 -Rounds 5 -NoAudit
```

该脚本严格串行运行 near 0/30/60 与 Campaign。默认组生成逐轮 schema 4 metrics 并用于 producer/bridge
归因；`-NoAudit` 组记录不受逐帧日志扰动的最终 stats，作为真实性能对照。两组都保存原始输出、manifest
和跨轮摘要；开始前会拒绝已有 `rasterfall.exe` 进程。输出位于 `tmp/`，不提交仓库。

2026-09-22 的当前 Intel package 已完成两组各五轮。审计组确认 near 0/30/60 的 bridge 固定为 12 次、
88,473,600 bytes，Campaign 为 14/16 次、103,219,200/117,964,800 bytes；无审计组单独保存最终 stats。
用 schema 4 重算审计组后，near 0/30/60 与 Campaign 的五轮逐帧规范化 workload 均逐场景完全一致；
Campaign 的 14/16 bridge 状态是确定帧序列的一部分，不是跨轮调度漂移。
逐帧 producer/bridge 输出会明显扰动重负载调度，因此两组数据不得直接拼接或用审计组 whole-loop 作为
优化收益基线。`--gpu-rb0-stats` 在前 16 帧预热后只向内存记录 whole-loop、Raster/GPU Raster、CPU
producer、slot/fence wait、acquire/present、producer 与 bridge 数字，退出时一次输出分位数和最慢 5%；
`gpu_rb0_sampling.ps1 -NoAudit` 使用该结果作为低扰动基线。本机 CIM 电源、adapter/driver 枚举因权限拒绝，
脚本另记录低权限 `powercfg /getactivescheme` 结果；另一物理 GPU 五轮复核仍未覆盖。

最新 package 的 `--gpu-rb0-stats` 格式门禁和正式五轮低扰动采样均通过。预热后 whole-loop median
的五轮范围/中位轮为：near 0 `24.660--28.735/25.262 ms`，near 30
`100.004--110.014/106.324 ms`，near 60 `146.750--203.240/157.118 ms`，Campaign
`27.869--31.301/30.804 ms`。near 60 的 P95/P99 范围扩大到
`174.307--583.242/178.815--641.747 ms`；其中 18/30 个最慢 5% 帧归为 `slot_or_fence_wait`，其余
12 帧归为 `external_scheduler`。对应慢帧的 bridge 仍固定为 12 次、88,473,600 bytes，因此该双态
不是 workload 或 bridge 计数漂移。near 30 的慢帧为 13 帧 `slot_or_fence_wait`、17 帧
`external_scheduler`；near 0 全部为 `external_scheduler`；Campaign 主要为 `external_scheduler`，仅
4 帧为 `cpu_producer`，没有慢帧归为 `raster_workload` 或 `present_or_acquire`。本轮活动电源方案为
“节能”，driver 版本仍因低权限枚举失败而未覆盖；这些结果只补强 RB-0 的长尾归因，不能据此进入
RB-1。

同一旧 package 在“平衡”方案下的后续五轮仍复现 near 60 双态：whole-loop median 范围/中位轮为
`133.752--156.078/148.916 ms`，P95/P99 范围为
`171.969--442.528/192.907--450.468 ms`。near 60 最慢 5% 中原分类为 27 帧
`slot_or_fence_wait`、3 帧 `external_scheduler`，bridge 继续固定为 12 次、88,473,600 bytes；因此不能把
节能方案视为双态的充分原因。一次运行中途由“高性能”切换到“平衡”的目录已判为无效，不参与对照。

当前低扰动统计进一步区分 frame-slot recycle、graphics submit fence、presenter reacquire completion
和 acquire/present API，并输出 GPU Raster/Draw/bridge 分位数、未覆盖时间及 CPU/GPU frame ID 对应。
双帧延迟返回的 GPU timestamp 按 frame ID 回填到对应 CPU 样本，末尾未回收样本不进入 GPU 分位数；
reason 可比较同帧 GPU Raster。未覆盖时间不是互斥时间分解。该统计合同已通过 Windows build、logic、
Quick、Full 与四场景一轮格式门禁；随后完成的正式五轮归因和最终专项签收已结束 Intel 单设备 RB-0。

底层独立目标继续保留：`rf-gpu-graphics-test`、`rf-gpu-raster-test`、
`rf-gpu-raster-diff-test`、resource-cache test 和 mixed-executor test。它们验证 ABI、资源与执行器
合同，不由窗口程序替代。

`-Full` 是当前日常完整回归，不等于重新执行全部历史 HG 签收矩阵。mixed executor hosted 与 native
四 extent resize 已纳入 Quick/Full，并继续由独立目标提供合同覆盖；Vulkan validation/sync validation、
fault injection、10,000 帧 soak、跨厂商完整 Full 和完整角色 CPU/native 人工组图属于
专项签收；涉及 presenter、同步、资源生命周期、驱动兼容或发布判断时必须按风险单独补跑并报告。

## 当前计划入口

当前阶段不新增 HG-6 编号，使用 [GPU Raster / Bridge 收敛计划](gpu-raster-bridge-plan.md) 的 RB-0 至
RB-3 checkpoint。立项前置条件为：

- 至少在 Intel 与另一种物理 GPU 上运行当前 Full，明确厂商差异；条件允许时补 AMD。
- presenter/synchronization 改动恢复 resize、validation、fault injection 和长时 soak。
- 新增按 producer 分类的 RasterCmd、bridge、CPU encode 与 GPU timestamp 审计。
- 使用同一 package、固定电源/冷启动条件做多轮 median、P95/P99 和最慢帧分类。

若审计确认主要成本来自可迁移 opaque 内容，下一阶段可以限定为 gear、weapon、rigid enemy part 或
正式支持的 textured opaque Draw；透明、粒子、overlay、复杂 VFX、动态光照和新材质体系不应顺带混入。
若主要成本来自 compute Raster 或同步边界，则应优先做调度和吞吐优化，而不是扩大 Draw 类型。
