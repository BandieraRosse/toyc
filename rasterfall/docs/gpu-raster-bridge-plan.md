# GPU Raster / Bridge 收敛计划

> 文档更新：2026-09-21
> 源码核对基线：`486e7e5`；Windows Intel Iris Xe 两轮 Full PASS 与逐帧 mixed CPU/GPU audit

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
3. 减少全屏 bridge 次数/字节，并保持 color、depth、层顺序与 Fog 数值合同。
4. 将高成本、语义稳定的 opaque RasterCmd producer 迁移到现有 Draw 路径。
5. 在 Intel 与至少另一种物理 GPU 上验证收益和正确性，再决定透明/VFX 后续路线。

本计划不包括：

- 新材质体系、动态光照重构、透明排序体系或通用粒子框架。
- SDL-free 平台重构、Linux native presenter 补齐或跨版本兼容。
- 仅为降低微小 Draw shader/encode 时间而进行的大规模 pipeline 重写。
- 用可见几何替代玩法碰撞，或改变 Game/session 的权威状态所有权。

## Checkpoint RB-0：可信测量与归因

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

退出条件：

- 固定负载的命令计数和 simulation tick 可重复；不同轮次的差异能由审计字段解释。
- 最慢 5% 帧可分类为 Raster workload、CPU producer、slot/fence wait、present/acquire 或外部审计/调度，
  不再只得到一个笼统的 `present_wall_ms`。
- Intel 与 RTX 3050 使用同一命令完成至少 near 0/30/60、Campaign 和 presenter-300；条件允许时补 AMD。
- Quick/Full、零 fallback/readback/CPU framebuffer copy/hot queue-idle 合同继续通过。

## Checkpoint RB-1：Bridge 与同步收敛

RB-1 先处理 mixed frame 的结构性往返，不新增 Draw 语义。主要手段应是 producer/layer 编排和已有
Raster/Draw segment 的合并，而不是绕过深度或层顺序合同。

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
- CPU/reference、hosted differential、Fog、thin-far、四 extent resize、world-cycle 和固定 capture 保持
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
- 保留 CPU/reference 或可复核 differential；近裁剪、逆深度、Fog、lighting、alpha/depth-write 语义不得
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
- 若成本集中在透明和粒子，单独立项 transparent/VFX GPU 路径，并先冻结排序、blend、depth 和 Fog
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
near/mid/thin-far、30/60 敌人、Campaign、Fog 和受影响资产的固定 capture。发布或跨设备结论必须在 Intel
与至少另一种物理 GPU 上使用同一 package 重复验证。
