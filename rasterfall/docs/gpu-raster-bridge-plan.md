# GPU Raster / Bridge 收敛计划

> 文档更新：2026-09-22
> 源码核对基线：RB-1 模块化队友 opaque 提交编排工作区

本文定义 HG-0 至 HG-5B 完成后的下一轮 GPU 性能工作。它不是新的通用 Graphics 功能阶段，也不继续
沿用历史 HG 编号；目标是先建立可信、可复现的帧耗时归因，再收敛剩余 RasterCmd、Draw/Raster bridge
和同步长尾。当前实现边界见 [GPU 渲染架构](gpu-rendering-architecture.md)，硬件覆盖与日常门禁见
[GPU 当前状态](gpu-current-state.md)。历史 HG 计划只保存在 `archive/`，不作为本计划的实施顺序。

## 立项结论

2026-09-21 的 Windows Intel Iris Xe Full 验收中，1280x720 required native present 的预热后结果为：

| 场景 | whole-loop median / P95 | GPU Raster median / P95 | GPU Draw median / P95 |
| --- | ---: | ---: | ---: |
| near 0，300 帧 | 26.08 / 31.97 ms | 6.53 / 10.15 ms | 1.52 / 2.27 ms |
| Campaign 320 | 33.93 / 44.44 ms | 6.76 / 8.63 ms | 1.51 / 1.90 ms |
| near 30 敌人 | 29.56 / 93.57 ms | 10.18 / 43.09 ms | 1.55 / 2.11 ms |
| near 60 敌人 | 50.12 / 143.92 ms | 22.08 / 62.64 ms | 1.43 / 2.08 ms |

这些数字只用于确定当前机器上的优化方向，不是跨设备性能承诺。原始证据位于本地生成目录
`tmp/gpu-acceptance-20260921-233022/`，不提交仓库。

当前判断如下：

- GPU Draw 与 GPU skinning 已不是主要成本；60 敌人负载增加后 Draw 基本不变，新增 GPU 时间集中在
  Raster。
- 典型重帧仍有约 24K--26K world RasterCmd、约 4.9K transparent、约 2.7K effects 和约 1.1K
  viewmodel commands；当前审计尚不能把所有 GPU Raster 时间精确归属到 producer。
- 典型 mixed 帧有 67 个 Raster span、5 个 Draw span、10 次 bridge transfer 和 73,728,000 bridge
  bytes。bridge 的 GPU 中位数约 2.2--2.5 ms，是稳定存在的结构性成本。
- `native_present_ms` 中位数约 0.02 ms，`native_acquire_ms` 约 0.005 ms；重负载下数十毫秒的
  `present_wall_ms` 主要代表 GPU completion、frame slot/fence 或 Raster segment 尾部等待，不能归因于
  Vulkan present API。
- CPU `draw_encode_ms` 约 1 ms，CPU graphics draw 编排约 0.6--0.7 ms；现阶段微调 Draw encode、
  skinning shader 或 swapchain present API 的预期收益低于 Raster/bridge 收敛。
- 现有 near 30/60 性能场景未固定 simulation tick；不同运行的 workload、P95 和 near 30 中位数存在
  明显波动。现有数字足以决定方向，但不能直接充当单项优化的最终收益证明。

## 目标和非目标

本计划目标：

1. 让同一 package、同一固定 workload 的 median、P95、P99 和最慢帧原因可以复核。
2. 将 CPU encode、frame-slot/fence wait、GPU Raster、GPU Draw、bridge 和 present API 分开归因。
3. 减少全屏 bridge 次数/字节，并保持 color、depth、层顺序与 runtime fog 中性值合同。
4. 将高成本、语义稳定的 opaque RasterCmd producer 迁移到现有 Draw 路径。
5. 在 Intel 与至少另一种物理 GPU 上验证收益和正确性，再决定透明/VFX 后续路线。

本计划不包括：

- 新材质体系、动态光照重构、透明排序体系或通用粒子框架。
- SDL-free 平台重构、Linux native presenter 补齐或跨版本兼容。
- 仅为降低微小 Draw shader/encode 时间而进行的大规模 pipeline 重写。
- 用可见几何替代玩法碰撞，或改变 Game/session 的权威状态所有权。

## Checkpoint RB-0：可信测量与归因

最新执行记录见 [RB-0 专项续接](gpu-rb0-special-20260922.md)；专项入口为
`tools/gpu_rb0_special.ps1`，审计分析使用 metrics schema 5。以下阶段数字不能替代修复后的新基线。

RB-0 签收状态：最终 package `F407FD19...63D762` 的 Full 31/31、validation/sync、五类 fault 与
10,000 帧 soak 已在 Intel Iris Xe 通过；schema 5 性能归因完成。Intel 单设备 RB-0 已签收，但计划要求的
RTX 3050/第二物理 GPU 由用户明确暂缓，因此本轮按 Intel 单设备范围结束，不称为跨设备签收，
也不自动进入 RB-1。

最新状态以 [RB-0 专项续接](gpu-rb0-special-20260922.md) 为准；
修复前失败保留在 [RB-0 修复与续接](gpu-rb0-repair-20260922.md)。应先补齐画面及设备签收缺口，
再讨论 RB-1。下方阶段数字属于修复前记录。

最新 [RB-0 排查报告](gpu-rb0-investigation-20260922.md) 修正了下文阶段记录的解释：
producer 切换触发 flush 后，actor 裁剪仍使用旧命令起点，可能恢复已消费的槽位；GPU query pool
存在静默截断；graphics fence 不是 bridge 专属计时。当前先修复这些正确性/测量问题，
再重做固定 workload 基线。不得由跨轮 hash 一致推断观察代码没有改变实际负载。
本机新统计的平衡方案五轮已完成，结果和范围限制见报告；仍不进入 RB-1。

RB-0 不改变正常画面输出。先修正测量边界，防止把 workload 漂移、日志扰动或等待嵌套误判成 renderer
成本。

实施项：

- 为 near 0/30/60 和正式 Campaign 性能运行提供固定 simulation tick；性能比较必须使用同一初始状态、
  帧数、相机、extent 和资产 package。
- 按 producer 输出 RasterCmd 数量、span 数、主要命令类型、透明/不透明属性和可用时的像素覆盖估计。
  至少区分 world/map、enemy body、enemy rigid special、gear、weapon、transparent、effects、viewmodel、
  overlay 和 bridge。
- 将 `present_wall_ms` 内的 frame-slot wait、render fence/GPU completion、acquire、submit、present API 和
  presenter completion 分开记录。继续明确哪些字段互相嵌套，不允许在汇总中直接相加。
- 对 bridge 记录发生层边界、方向、次数、color/depth 字节和对应前后 producer。
- 扩展 `tools/gpu_metrics.ps1`，报告 median、P95、P99、maximum、最慢帧分类及关键计数；保留 GPU
  timestamp 自身 frame ID 的预热窗口语义。
- 同一 package 至少运行五轮；逐帧 audit 与无 audit 各有一组，记录电源状态、adapter、driver、extent
  和 package commit。

当前进展：`--gpu-normal-fixed-tick` 已同时覆盖 normal scene 与 Campaign wave repro；Full 的 near 0/30/60
和 Campaign 性能运行均使用一帧一 tick。mixed span 已携带 world/map、enemy body、enemy rigid special、
gear、weapon、transparent、effects、viewmodel、overlay 诊断身份；producer 切换只冻结已有 RasterCmd 边界，
不执行 GPU 工作或改变提交顺序。逐帧 bridge 审计输出 import/export、color/depth traffic、layer、前后
producer 与 target generation。`gpu_metrics.ps1` 汇总每类 producer 的 RasterCmd、span、opaque/transparent、
bridge event/bytes 的 minimum/maximum/distinct、whole-loop 相关系数和最慢 5% 帧明细，并生成排除计时与
target generation 的逐帧规范化 workload 和 SHA-256。当前 Intel package
的审计/无审计跨轮五次采样已完成；受权限限制的电源/驱动清单和另一物理 GPU 复核仍属于后续 RB-0
工作，完成前不得进入 RB-1。

五轮固定 workload 采样使用 `tools/gpu_rb0_sampling.ps1`。它在同一 package 上严格串行运行 near 0/30/60
各 120 帧与 Campaign 320 帧；默认审计组逐轮调用 schema 4 metrics 门禁，`-NoAudit` 对照组从最终 stats
提取不受逐帧日志扰动的 frame/P95/P99。输出目录保存 package 时间戳、commit、工作区、可用的
电源/adapter/driver 信息、原始日志、逐轮 metrics 和跨轮摘要。采样目录属于本地证据，不提交。

2026-09-22 已在当前 Windows Intel package 上完成审计组与 `-NoAudit` 组各五轮。near 0/30/60 的
bridge traffic 在审计组内分别固定为 12 次、88,473,600 bytes；Campaign 固定落在 14/16 次与
103,219,200/117,964,800 bytes 两种状态。逐帧日志会显著扰动重负载调度，因此 producer/bridge 审计组
只用于归因，无审计 stats 组用于性能对照，不能混为同一基线。schema 4 离线重算证明四个场景各自的
五轮逐帧规范化 workload 完全一致，Campaign 的两种 bridge 状态由确定帧序列触发。新增
`--gpu-rb0-stats` 在 16 帧预热后低扰动地保留整帧与关键分类数字，退出时统一输出分位数与最慢 5%，
采样脚本的 `-NoAudit` 组读取该结果。本机 CIM 电源与 driver 查询因权限拒绝，
且另一物理 GPU 尚未完成同组复核；这些仍是 RB-0 退出条件缺口，不进入 RB-1。

最新 package 的 `--gpu-rb0-stats` 格式门禁与正式五轮低扰动采样随后通过。预热后 whole-loop 的
五轮范围如下；“中位轮”是按各轮 median 排序后的中间一轮，不使用 maximum 或启动首帧：

| 场景 | median 范围 / 中位轮 | P95 范围 | P99 范围 |
| --- | ---: | ---: | ---: |
| near 0 | 24.660--28.735 / 25.262 ms | 28.392--35.876 ms | 30.002--43.307 ms |
| near 30 | 100.004--110.014 / 106.324 ms | 119.593--153.087 ms | 122.428--160.053 ms |
| near 60 | 146.750--203.240 / 157.118 ms | 174.307--583.242 ms | 178.815--641.747 ms |
| Campaign | 27.869--31.301 / 30.804 ms | 37.041--42.257 ms | 40.302--48.995 ms |

最慢 5% 中，near 0 的 30 帧均为 `external_scheduler`；near 30 为 17 帧
`external_scheduler`、13 帧 `slot_or_fence_wait`；near 60 为 12 帧 `external_scheduler`、18 帧
`slot_or_fence_wait`；Campaign 为 76 帧 `external_scheduler`、4 帧 `cpu_producer`。没有慢帧归为
`raster_workload` 或 `present_or_acquire`。near 60 的 P95/P99 呈明显跨轮双态，但慢帧中的 bridge
仍固定为 12 次、88,473,600 bytes；这说明长尾不是 workload 或 bridge 计数漂移，仍需在 RB-0 内继续
区分 GPU completion/slot wait 与未覆盖的外部调度。慢帧 `gpu_raster_us` 与 CPU wall Raster 都随
near 0→30→60 增长，方向一致，但二者的作用域不同且 CPU wall 字段互相嵌套，不得相加或解释为互斥
时间分解。本轮 manifest 记录活动电源方案为“节能”；CIM adapter/driver 查询仍因权限拒绝，driver
版本未覆盖。

随后在同一旧 package、Windows“平衡”电源方案下完成另一组五轮 `-NoAudit`。near 0/30/60 与
Campaign 的 whole-loop median 五轮范围/中位轮分别为 `19.296--20.004/19.903 ms`、
`94.355--100.644/97.359 ms`、`133.752--156.078/148.916 ms`、
`25.728--26.021/25.902 ms`；near 60 的 P95/P99 仍横跨
`171.969--442.528/192.907--450.468 ms`，双态没有因“节能”切换为“平衡”而消失。该组
near 60 慢帧为 27 帧原 `slot_or_fence_wait`、3 帧 `external_scheduler`，bridge 仍固定为 12 次、
88,473,600 bytes。一次从“高性能”运行中途切换到“平衡”的五轮目录不满足固定电源条件，明确不作为
性能证据。driver 版本仍未覆盖，因此仍不得退出 RB-0。

为继续拆分该双态，低扰动统计已将原笼统 wait 拆为 frame-slot render-fence recycle、graphics submit
fence、presenter reacquire completion 与 acquire/present API；同时输出 GPU Raster/Draw/bridge 的
median/P95/P99、未覆盖时间和 GPU timestamp frame ID。GPU timestamp 由双帧 slot 延迟回收，退出汇总
按 frame ID 回填到对应 CPU frame，末尾尚未回收的 timestamp 不进入 GPU 分位数。reason 现在比较
同帧 GPU Raster 与各 CPU/wait 候选；未覆盖时间只表示 whole-loop 与最大单项的差，不是可相加的互斥
时间分解。

退出条件：

- 固定负载的命令计数和 simulation tick 可重复；不同轮次的差异能由审计字段解释。
- 最慢 5% 帧可分类为 Raster workload、CPU producer、slot/fence wait、present/acquire 或外部审计/调度，
  不再只得到一个笼统的 `present_wall_ms`。
- Intel 与 RTX 3050 使用同一命令完成至少 near 0/30/60、Campaign 和 presenter-300；条件允许时补 AMD。
- Quick/Full、零 fallback/readback/CPU framebuffer copy/hot queue-idle 合同继续通过。

## Checkpoint RB-1：Bridge 与同步收敛

RB-1 先处理 mixed frame 的结构性往返，不新增 Draw 语义。主要手段应是 producer/layer 编排和已有
Raster/Draw segment 的合并，而不是绕过深度或层顺序合同。

当前进展：mixed executor 已将没有 Raster span 介入的连续 WORLD Draw spans 合为一个 graphics batch。
producer 边界仍保留在 frozen frame 中用于命令归因，但不再单独触发 import/export；GPU timestamp 预留和
Core bridge event 审计均改为按实际 Draw run 计数。mixed-executor 回归显式覆盖跨 producer 的相邻 Draw，
保持原绘制顺序、像素/depth differential，并要求只产生一次 import/export。Windows native `gpu-test`
已通过。随后 frame audit 将 normal near 的 5 个 Draw run 定位为逐个模块化队友的
`body Draw -> gear/weapon Raster` 交替。正常 AI 提交现在先冻结所有可见模块化队友的 body Draw，再按原 actor
顺序提交其 opaque gear/weapon RasterCmd；pose、IK、attachment、逐 actor 光照和资源所有权不变，且不跨越
transparent/effects 层。相同 native `gpu-test` 场景的实际 Draw run 从 5 降到 2，transfer 从 10 降到 4，
bridge bytes 从 73,728,000 降到 29,491,200。剩余两段由前置 world/map Draw 与其后的真实 Raster 内容隔开，
不能只凭 opaque 分类继续跨越。该单次门禁证明结构计数下降，性能退出结论仍需固定 workload 多轮数据和
第二物理 GPU 复核；Windows `gpu-test`、Quick 8/8 与 Full 正确性门禁已通过。mixed-executor hosted
回归现按 extent 精确验证 bridge 次数与字节；native resize 回归也逐 pass 验证 Draw run、transfer、
bridge bytes 以及活动 frame-slot target rebuild，防止只保持画面却让 bridge/resize 合同回退。

同一 package 随后完成 audit 与 `-NoAudit` 各五轮固定 workload 采样。四个场景各自的规范化 workload
sequence hash 均跨五轮一致；near 0/30/60 的 bridge 固定为 4 次、29,491,200 bytes，Campaign 固定为
10 次、73,728,000 bytes，相比 RB-0 的 near 12 次、88,473,600 bytes 与 Campaign 14/16 次、
103,219,200/117,964,800 bytes 均可重复下降。低扰动组 whole-loop median 的五轮范围/中位轮为：
near 0 `15.575--16.680/16.283 ms`，near 30 `21.928--23.021/22.813 ms`，near 60
`32.336--33.518/33.197 ms`，Campaign `20.819--21.690/21.240 ms`。audit 组对应中位轮为
`13.511/20.686/31.328/16.401 ms`；audit 只用于归因，不与低扰动数据混作同一性能基线。该 package
包含 RB-0 修复与此前 RB-1 改动，因此相对更早文档数字的全部耗时下降不能只归因于本次 bridge 编排；
但相同 workload 下的 bridge 计数/字节下降已满足 Intel 单设备结构退出证据。第二物理 GPU 仍未复核。

实施项：

- 根据 RB-0 的边界记录，找出造成 10 次全屏 transfer 的具体 Draw/Raster 交替点。
- 合并同层、同语义且无需中间 target 可见性的 Raster span；允许时将相邻 opaque producer 聚合到同一
  Raster 或 Draw 区段。
- 避免重复导入/导出未改变的 color/depth attachment；任何跳过必须由 resource state 和层依赖证明，
  不能靠场景特例猜测。
- 将 graphics wait、Raster 尾部 wait 和 presenter wait 放在最晚必要点；正常热路径继续禁止
  queue-idle。
- 为 bridge 次数、字节、方向和 target generation 增加 hosted 与 native 合同测试。

退出条件：

- near 0/30/60 与 Campaign 的 bridge 次数或字节有可重复下降；目标值由 RB-0 归因后确定，不提前用
  不可靠数字强行设定百分比。
- GPU bridge timestamp 和 whole-loop/P95 在 Intel 上有一致改善，另一物理 GPU 不出现反向显著退化。
- CPU/reference、hosted differential、fog-free runtime、thin-far、四 extent resize、world-cycle 和固定 capture 保持
  正确；所有帧仍为 required `gpu-native`。

## Checkpoint RB-2：高成本 Opaque RasterCmd 迁移

RB-2 只迁移 RB-0 证明为主要成本、且适合现有 Graphics 数值合同的 opaque 内容。默认候选顺序为：

1. enemy rigid/body 中仍留在 RasterCmd 的 opaque 部分；
2. gear 与 weapon；
3. special enemy rigid parts；
4. 当前正式资产需要的 textured opaque Draw。

每个候选先做单独 ablation，再决定是否进入实现。迁移不得顺带引入透明材质、复杂 VFX 或新的通用材质
抽象。CPU 继续拥有 pose、IK、socket、gear/weapon placement；GPU 只接收冻结后的绘制输入。

每批迁移必须：

- 明确 producer、资源所有者、frame pin/generation 和回滚边界。
- 保留 CPU/reference 或可复核 differential；近裁剪、逆深度、fog-free runtime、lighting、alpha/depth-write 语义不得
  变化。
- 输出迁移前后的 RasterCmd、Draw、span、bridge、upload、GPU Raster/Draw 和 whole-loop/P95 对照。
- 覆盖 near 0/30/60、Campaign、thin-far、角色/敌人边缘入镜以及相关固定 capture。

退出条件：

- 目标 producer 的正常 opaque 路径不再生成 RasterCmd，或未迁移部分有明确的数值/透明/动态理由。
- GPU Raster 降幅与命令/覆盖变化相符，GPU Draw 增量没有抵消收益。
- 资源 cache、world retirement、resize 和角色 rollback 无生命周期回归。

## Checkpoint RB-3：CPU Producer 与长尾

完成 RB-1/RB-2 后再处理仍显著的 CPU 长尾，避免优化已经被迁移删除的提交路径。

候选包括 enemy visibility/LOD、组合 bounds、pose/gear/weapon transform cache、teammate procedural
submission，以及 mixed pack 的重复扫描/复制。优化以 P95/P99 和最慢帧分类为依据，不以单个累加 CPU
计时器为依据。

退出条件：

- 固定负载下 CPU producer/pack 的 P95/P99 有可重复改善。
- workload 命令数、画面和玩法状态保持一致；不得通过额外剔除可见内容伪造性能收益。
- 单 worker/多 worker 或 cache hit/miss 路径均有相应逻辑与画面回归。

## 后续路线的决策门

RB-0 至 RB-3 完成后再决定下一阶段：

- 若主要剩余成本仍是 opaque Raster，继续扩展已证明的 Draw 类型。
- 若成本集中在透明和粒子，单独立项 transparent/VFX GPU 路径，并先冻结排序、blend、depth 和中性 fog
  合同。
- 若成本集中在 bridge/同步，继续优化 mixed scheduling，不扩大材质范围。
- 若 Intel 与独显差异显著，先做厂商/驱动专项，不把单设备优化当作通用结论。

任何新阶段都必须引用 RB-0 固定 workload 的多轮数据；不以单次截图、首帧、WSL/llvmpipe 或无
required native present 的结果立项。

## 验证矩阵

日常最近验证仍从以下入口开始：

```powershell
.\windows\NativeCodex.ps1 build
.\windows\NativeCodex.ps1 test
powershell -ExecutionPolicy Bypass -File tools/gpu_acceptance.ps1 -Quick
```

涉及 bridge、同步、presenter 或资源生命周期时，至少增加 Full、四 extent native resize、validation/
sync validation、fault injection 和专项 soak。涉及 producer 迁移时，增加 CPU/reference differential、
near/mid/thin-far、30/60 敌人、Campaign、fog-free runtime 和受影响资产的固定 capture。发布或跨设备结论必须在 Intel
与至少另一种物理 GPU 上使用同一 package 重复验证。
