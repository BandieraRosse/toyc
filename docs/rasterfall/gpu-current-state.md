# GPU 当前状态

> 文档更新：2026-09-22
> 源码核对基线：Mixed M1、metrics schema 6、RTX 3050 M2 preflight 三轮审计与双档性能标准

本文只记录当前支持范围、回滚边界、已知限制和可执行验证入口。下一轮实施顺序见
[GPU Raster / Bridge 收敛计划](plans/gpu-raster-bridge.md)，阶段过程与历史性能数字见
[Hardware Graphics 归档](archive/hardware-graphics-2026-09/README.md)。

## 当前开发阶段

当前优化执行以 [Mixed 路径优化计划与证据](gpu-mixed-optimization-20260922.md) 为准。
M1 已按 Intel 单设备范围签收：利用有序 tile indices 跳过 segment 外命令，保留独立 full-scan 对照，不改变 Draw run、bridge、
producer 或光照语义；后续资源与同步改动各自单独验证。阶段测量只保存在该专题报告中。

自 M2 起，RTX 3050 是高性能开发、正式 A/B 与初期稳定 60 FPS 签收主设备；Intel Iris Xe 是普通正确性、
完整功能与 30 FPS 下限设备。当前只设置这两档，不要求两个设备达到相同帧率。目标、冻结数字和采样口径
见 [GPU 性能标准与冻结基线](gpu-performance-standards.md)。

最新专项状态以 [RB-0 专项续接](gpu-rb0-special-20260922.md) 为准；其中记录 raw 输入覆盖和
resize 附件生命周期修复，以及真实启用 validation/sync 的证据。下方旧结果不能替代新 package 验收。

同 fixed tick 的 near/mid 复核已补齐 mixed SKY 与世界血条 overlay coverage。RB-0 签收后，Rasterfall
runtime 已统一为无 fog：CPU/GPU normal producer 只提交中性 fog，GPU Post 不再由 CLI/Core 接入。
RasterCmd fog 字段、CPU/GPU consumer 和底层 Post Fog V0 仍保留 ABI 与专项测试语义，但不参与正常游戏画面。

负载/测量及专项生命周期修复已完成 Windows Quick/Full、validation/sync、fault 与 soak；
采样结果和限制见 [RB-0 专项续接](gpu-rb0-special-20260922.md)。下文性能数字为修复前历史现场，
不能替代新基线。低扰动 whole_us 已统一到整轮起点，兜底分类为 unclassified。schema 6 离线归因已完成：
正确口径五轮没有复现历史 near60 数百毫秒双态；55 个低扰动未分类慢帧均由互斥顶层 phase 覆盖，
但缺少 phase 内因果计时，按根因未知的已知风险冻结。此前 fog/远墙差异已通过统一禁用 runtime fog 消除。
RB-0 最终签收 package `F407FD19BFC1FBC049ADE78EA21EB9E71D7CD2B627C0CAD388CB1DC8E363D762` 已通过
Full 31/31、validation/sync、五类 fault 和 10,000 帧 soak；证据与 SKY 轴向朝向 validator 修复见专项续接。
RB-0 是历史 Intel 单设备签收；M2 起已改用 RTX 3050 性能主线与 Intel 普通标准，不重写该历史证据范围。

RB-1 已合并连续跨 producer Draw spans，并将正常 world 中模块化队友改为 body Draw 集中冻结、随后按原
actor 顺序提交 opaque gear/weapon RasterCmd。Intel native near 审计的实际 Draw run 从 5 降到 2，bridge
transfer 从 10 降到 4、bytes 从 73,728,000 降到 29,491,200；Windows `gpu-test`、Quick 8/8 与 Full
均通过。随后同一 package 的 audit/低扰动组各五轮固定 workload 采样通过：near 0/30/60 均稳定为
4 次、29,491,200 bytes，Campaign 稳定为 10 次、73,728,000 bytes，各场景 workload hash 跨轮一致；
低扰动 whole-loop 中位轮依次为 16.283/22.813/33.197/21.240 ms。该 package 同时包含此前修复，不能把
相对旧文档的全部耗时下降只归因于 RB-1；该历史 Intel 单设备 bridge 结构下降已可重复。
Campaign 剩余 10 次 transfer 已逐边界复核为 2 个 world/map 与 3 个 enemy-body Draw run，彼此均由真实
Raster span 隔开；尝试只重排 Campaign fixture 不会减少 run 或 bridge，故未保留。进一步下降需要迁移
special rigid/body 等真实 opaque Raster 内容，属于 RB-2，而不是继续在 RB-1 跨越资源边界。

RB-2 的首个 `enemy-rigid-special` 诊断 ablation 已证明“只把 rigid body RasterCmd 改成 Draw”不是有效
切片。正常 GPU skinning 帧的现有 dynamic stream 要求所有 dynamic 顶点都有 skin bind/palette，故先在
baseline/ablation 都关闭 GPU skinning 的条件下受控比较。Campaign 中该 producer 从稳定 963 RasterCmd
降到 0，但 blob shadow、tongue/特殊组件及 actor 顺序仍形成真实 Raster 边界，bridge 反而从
10 次、73,728,000 bytes 增到 12 次、88,473,600 bytes；单轮 whole-loop median 为
17.532→18.298 ms，GPU bridge median 为 2.644→3.166 ms。该诊断代码已撤销，不能据此立项 typed dynamic
stream；后续 rigid 候选必须先把相邻 opaque Raster 与提交编排纳入同一 ablation，同时继续排除透明死亡表现。

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
  长帧稳定性；RTX 3050 已覆盖 native swapchain、四场景三轮 audit 与 M2 preflight 归因。Linux hosted Vulkan 路径用于 correctness，
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

当前正常 GPU skinning 的四场景成本/bridge 盘点见 [RB-2 候选评估](gpu-rb2-candidate-review-20260922.md)。
普通 infected body 与相邻 shadow 编排已短测并撤销：结构与 Raster 耗时改善，但最终画面的逐面光照
不等价，且 bridge 由 4 次增到 6 次。下一 producer 候选回到 gear/weapon 边界预检，但它不是当前立即
执行项，须等待 M2 后重新归因；producer GPU 毫秒尚不可分摊。
`tools/gpu_rb2_candidate_report.ps1` 从同 package 的 audit/no-audit 结果重建盘点表。

以下历史数字不能直接作为下一批迁移的收益基线。[RB-0 排查](gpu-rb0-investigation-20260922.md)
记录的命令范围与 timestamp 覆盖缺陷已修复；可重复 workload 不等于正确 workload，
历史 `external_scheduler` 标签也不是已证实的外部调度原因。

HG-5B 最终 Intel Iris Xe 基线中，near 30/60 敌人的稳态 whole-loop 中位数约为
29.337/54.782 ms，GPU Raster 约为 11.449/27.363 ms，GPU Draw 约为 1.816/1.814 ms。
这些数字只用于确定当前优化优先级，不构成跨机器或跨厂商性能承诺。

RB-1 前的两轮 Full 确认，60 敌人场景中 Draw 已不是主要成本；一轮预热后 whole-loop 中位数/P95 为
50.12/143.92 ms，GPU Raster 为 22.08/62.64 ms，GPU Draw 为 1.43/2.08 ms。典型重帧仍有
10 次、73,728,000 bytes bridge transfer；RB-1 当前 normal near 结构计数已降为 4 次、29,491,200 bytes，
且五轮固定 workload 已确认 near 0/30/60 均保持该值，Campaign 保持 10 次、73,728,000 bytes。
同组低扰动 whole-loop 中位轮依次为 16.283/22.813/33.197/21.240 ms；由于 package 同时包含此前修复，
这些耗时不能作为单项 ablation 收益。`native_present_ms` 中位数约 0.02 ms，因此较大的
`present_wall_ms` 不能归因为 present API 本身。

M1 segment 遍历已收敛；M2 preflight 子阶段已经在 RTX 3050 实机细分。当前唯一未完成前置是补同一
package 的低扰动 `-NoAudit` 五轮 baseline；完成后才按
[Mixed 优化执行计划](gpu-mixed-optimization-20260922.md) 实现 frame-slot 动态资源复用并进行 A/B。
producer 迁移等待 M2 后重新归因；届时仍按 [GPU Raster / Bridge 收敛计划](plans/gpu-raster-bridge.md)
执行 gear/weapon RB-2 候选门禁。透明、粒子、overlay、复杂 VFX 和新材质体系不顺带进入。

RTX 3050 当前三轮 audit whole-loop 中位轮为 near 0/30/60/Campaign 的
`34.151/52.130/73.555/39.594 ms`；这是开启逐帧日志的归因基线，不是低扰动 FPS 成绩。对应 package、
GPU Raster、P95、bridge 和 preflight 冻结数据见性能标准文档。正式 60 FPS 基线尚需补同 package
`-NoAudit` 五轮采样，后续候选一律与该低扰动 baseline 做 AB/BA。

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
layer、前后 producer 和 target generation；schema 6 metrics JSON 提供逐类范围、whole-loop 相关系数、
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

该脚本严格串行运行 near 0/30/60 与 Campaign。默认组生成逐轮 schema 6 metrics 并用于 producer/bridge
归因；`-NoAudit` 组记录不受逐帧日志扰动的最终 stats，作为真实性能对照。两组都保存原始输出、manifest
和跨轮摘要；开始前会拒绝已有 `rasterfall.exe` 进程。输出位于 `tmp/`，不提交仓库。

脚本未显式指定 `-Rounds` 时只运行一轮，用于 workload、producer、Draw run、bridge 和失败边界诊断；
确定性合同、画面或结构失败后不应机械重复。只有候选通过正确性与结构预检、准备形成性能收益结论时，
才显式使用 `-Rounds 5`。正式 baseline/candidate 比较优先使用 `gpu_mixed_ablation.ps1`，由它保持五轮
AB/BA 交替、AC 供电和 executable hash 合同。

签收前后的阶段采样与历史阻塞条件见
[测量阶段归档](archive/gpu-current-measurement-stage-20260922.md)。当前四场景盘点与下一候选见
[RB-2 候选评估](gpu-rb2-candidate-review-20260922.md)。

底层独立目标继续保留：`rf-gpu-graphics-test`、`rf-gpu-raster-test`、
`rf-gpu-raster-diff-test`、resource-cache test 和 mixed-executor test。它们验证 ABI、资源与执行器
合同，不由窗口程序替代。

Windows resource-cache test 位于 package 外；Full 会显式传入 package 内的 `rf_arch_beam.rmesh`
绝对路径，避免 Windows 的 exe-relative 文件解析把缺失的 `build-windows/rasterfall/assets` 旧目录
误当成测试前置条件。该资产只验证 invalidate 后的真实重新加载与新 generation，不是性能输入。

`-Full` 是当前日常完整回归，不等于重新执行全部历史 HG 签收矩阵。mixed executor hosted 与 native
四 extent resize 已纳入 Quick/Full，并继续由独立目标提供合同覆盖；Vulkan validation/sync validation、
fault injection、10,000 帧 soak、跨厂商完整 Full 和完整角色 CPU/native 人工组图属于
专项签收；涉及 presenter、同步、资源生命周期、驱动兼容或发布判断时必须按风险单独补跑并报告。

## 当前计划入口

当前阶段不新增 HG-6 编号，使用 [GPU Raster / Bridge 收敛计划](plans/gpu-raster-bridge.md) 的 RB-0 至
RB-3 checkpoint。RTX 3050 承担性能候选与 60 FPS 主签收，Intel 承担普通标准复核：

- RTX 3050 使用固定 workload 做低扰动五轮 A/B；Intel 使用同一 package 跑适用 Full 与普通性能门。
- presenter/synchronization 改动恢复 resize、validation、fault injection 和长时 soak。
- 新增按 producer 分类的 RasterCmd、bridge、CPU encode 与 GPU timestamp 审计。
- 使用同一 package、固定电源/冷启动条件做多轮 median、P95/P99 和最慢帧分类。

若审计确认主要成本来自可迁移 opaque 内容，下一阶段可以限定为 gear、weapon、rigid enemy part 或
正式支持的 textured opaque Draw；透明、粒子、overlay、复杂 VFX、动态光照和新材质体系不应顺带混入。
若主要成本来自 compute Raster 或同步边界，则应优先做调度和吞吐优化，而不是扩大 Draw 类型。
