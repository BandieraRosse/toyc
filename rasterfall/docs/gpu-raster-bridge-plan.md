# GPU Raster / Bridge 收敛计划

> 文档更新：2026-09-22
> 源码核对基线：Mixed M1、metrics schema 6、RTX 3050 M2 preflight 三轮审计与双档性能标准

本文定义 HG-0 至 HG-5B 完成后的下一轮 GPU 性能工作。它不是新的通用 Graphics 功能阶段，也不继续
沿用历史 HG 编号；目标是先建立可信、可复现的帧耗时归因，再收敛剩余 RasterCmd、Draw/Raster bridge
和同步长尾。当前实现边界见 [GPU 渲染架构](gpu-rendering-architecture.md)，硬件覆盖与日常门禁见
[GPU 当前状态](gpu-current-state.md)。历史 HG 计划只保存在 `archive/`，不作为本计划的实施顺序。

## 立项结论

当前状态以 [RB-2 候选评估](gpu-rb2-candidate-review-20260922.md) 为准：Intel RB-0 已签收、
RB-1 已有结构收敛证据；RB-2 的 rigid-only 与普通 infected body/shadow 两个切片均已否决并撤销，
迁移候选保留 gear/weapon 边界预检。当前优先执行不改变 producer 合同的
[Mixed 优化计划](gpu-mixed-optimization-20260922.md)：M1 segment 遍历已按原 Intel 单设备范围签收，
M2 起以 RTX 3050 为性能主线，依次做动态资源复用、按证据决定 skinning 同步、depth bridge 与选择性
opaque 迁移；infected 须先补光照等价合同。两档设备、60/30 FPS 目标和冻结基线见
[GPU 性能标准与冻结基线](gpu-performance-standards.md)。以下立项数字为历史背景，
不作为当前性能基线。

## 当前执行顺序

1. 冻结 RTX 3050 三轮 audit 归因基线，并补同 package 的低扰动五轮 baseline。
2. M2 按 frame slot 复用动态 resource/buffer/descriptor，保留现有 skinning submit/wait，完成 RTX 3050
   五轮 AB/BA 与生命周期专项门禁。
3. M2 后重新归因：skinning fence 若仍为最大固定项则进入 M3；否则按剩余成本在 depth bridge 与
   gear/weapon 实际边界预检之间选择。
4. GPU 固定成本收敛后，再按可见性/LOD、presentation snapshot、静态模板、pose/socket/gear 缓存、
   重复扫描/分配的顺序削减 CPU producer；暂不先引入多线程。
5. RTX 3050 负责 60 FPS 性能签收；Intel 负责 required-native 正确性、普通 30 FPS 下限和退化复核。

不以“全部软件 Raster 迁移”为目标。硬件 Graphics 只接收能保持画面合同并减少真实 Draw/Raster run、
bridge 与 whole-loop 的 opaque 内容；透明、粒子、overlay、复杂 VFX 和未冻结光照合同的内容继续留在
软件 Raster。

2026-09-21 的 Windows Intel Iris Xe Full 验收中，1280x720 required native present 的预热后结果为：

| 场景 | whole-loop median / P95 | GPU Raster median / P95 | GPU Draw median / P95 |
| --- | ---: | ---: | ---: |
| near 0，300 帧 | 26.08 / 31.97 ms | 6.53 / 10.15 ms | 1.52 / 2.27 ms |
| Campaign 320 | 33.93 / 44.44 ms | 6.76 / 8.63 ms | 1.51 / 1.90 ms |
| near 30 敌人 | 29.56 / 93.57 ms | 10.18 / 43.09 ms | 1.55 / 2.11 ms |
| near 60 敌人 | 50.12 / 143.92 ms | 22.08 / 62.64 ms | 1.43 / 2.08 ms |

这些数字只用于确定当前机器上的优化方向，不是跨设备性能承诺。原始证据位于本地生成目录
`tmp/gpu-acceptance-20260921-233022/`，不提交仓库。

当时立项判断如下（当前状态与候选排序见本节开头链接）：

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
5. 在 RTX 3050 上验证主要收益并在 Intel 上验证正确性与普通性能下限，再决定透明/VFX 后续路线。

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
RTX 3050 当时尚未进入同组复核，因此该历史 checkpoint 按 Intel 单设备范围结束。当前设备政策已经更新：
RTX 3050 为后续性能主线，Intel 为普通标准；不得把 RB-0 的历史单设备签收误写成当前设备范围。

签收前的测量缺陷、修复前五轮数据和当时的阶段阻塞条件已移到
[RB-0 测量阶段记录](archive/gpu-rb0-measurement-stage-20260922.md)。不得用这些历史数字替代当前基线。

持续保留的测量合同：固定 tick、相机、extent 与 package；audit 用于 workload/边界归因，
`--gpu-rb0-stats` 用于低扰动性能；GPU timestamp 按历史 frame ID 回填；CPU 细项与 wait 不直接相加。
正式收益签收仍要求多轮采样；M2 起按当前双档标准分别承担性能主签收与普通标准复核。

本轮局部修补处理无独立 graphics submit 时的旧 watermark，详见
[RB-2 候选评估](gpu-rb2-candidate-review-20260922.md)。这不重新开启整个 RB-0，也不宣称解决已冻结的长尾根因。

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
不能只凭 opaque 分类继续跨越。该单次门禁证明结构计数下降，性能退出结论仍需固定 workload 多轮数据；
Windows `gpu-test`、Quick 8/8 与 Full 正确性门禁已通过。mixed-executor hosted
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
但相同 workload 下的 bridge 计数/字节下降已满足该历史 Intel 单设备结构退出证据。

随后对 Campaign 剩余 10 次 transfer 做了逐边界复核：5 个实际 Draw run 分别是 2 个 world/map run 与
3 个 enemy-body run；每个 run 之间都有真实 Raster span，不是 producer 身份变化产生的伪边界。最后一组
`weapon -> enemy-body -> gear` 来自 Campaign fixture，但把该 fixture 提前到已有延迟 body 阶段后，实机仍为
5 个 Draw run、10 次 transfer、73,728,000 bytes，只改变相邻 producer，不减少 bridge，因此该无收益重排
未保留。其余 enemy-body run 由普通模型 Draw 与 special rigid/body Raster 在 actor 序列中交替形成；继续
合并必须迁移或重排真实 Raster 内容，已经超出 RB-1 的“只收敛现有 segment”边界，应作为 RB-2 候选先做
独立 ablation。恢复基线后的 Windows Full 31/31 通过。

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

以上是候选范围，不是机械执行顺序。普通 infected body 与 shadow 编排虽在 near 60 短测显著降低
Raster 时间，但通用 skinned Draw 未保持现有逐面光照画面，已撤销且未进入五轮收益签收。下一候选为
gear/weapon 的边界预检，具体证据与否决条件见 [候选评估](gpu-rb2-candidate-review-20260922.md)。

每个候选先做单独 ablation，再决定是否进入实现。迁移不得顺带引入透明材质、复杂 VFX 或新的通用材质
抽象。CPU 继续拥有 pose、IK、socket、gear/weapon placement；GPU 只接收冻结后的绘制输入。

首个 `enemy-rigid-special` ablation 已完成，结果为否决并已撤销。现有 `rf_core_mixed_dynamic_draw` 在正常
GPU skinning 开启时共享单一 dynamic resource，preflight 要求 `skin_vertex_count == dynamic_vertex_count`，
所以 procedural rigid 顶点不能直接混入正常帧。为先测方向，实验在 baseline/ablation 双方均使用
`--gpu-character-skinning-off`：每个 rigid part 以冻结后的 world-space 顶点和既有 Q8 面光照进入 Draw，
Campaign 的 `enemy-rigid-special` 从 963 RasterCmd 降为 0；但每个 actor 的 blob shadow、Smoker tongue/
特殊组件及其他 Raster 仍保留，实际 Draw run 增加，bridge 从 10 次、73,728,000 bytes 反向增至
12 次、88,473,600 bytes。单轮低扰动 whole-loop median 为 17.532→18.298 ms，GPU Raster median
12.793→12.848 ms，GPU Draw median 1.646→1.686 ms，GPU bridge median 2.644→3.166 ms。
该结果已足以拒绝“rigid body 单独迁移”和为它先扩展 typed dynamic stream；不做更多性能轮次。
若再次评估 rigid，ablation 必须把相邻 opaque shadow/组件与 actor 批次编排作为一个受限切片，透明死亡、
舌头和 VFX 仍留在 Raster，并先证明 Draw run/bridge 会下降。

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
near/mid/thin-far、30/60 敌人、Campaign、fog-free runtime 和受影响资产的固定 capture。阶段性能结论
以 RTX 3050 为主，Intel 使用同一 package 重复验证正确性、生命周期合同和普通性能下限。
