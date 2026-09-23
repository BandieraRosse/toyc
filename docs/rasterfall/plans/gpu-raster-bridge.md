# GPU Raster / Bridge 收敛计划

> 状态：当前唯一活动计划
> 所有者：Rasterfall GPU 性能主线
> 最近核对：2026-09-23
> 当前切片：M2 动态资源复用；前置为 RTX 3050 低扰动五轮 baseline

本文只记录尚未完成的执行顺序和决策门。稳定数据流见
[GPU 渲染架构](../gpu-rendering-architecture.md)，验收工作流见
[GPU 验收与诊断](../guides/gpu-validation.md)，目标与冻结基线见
[GPU 性能标准](../reference/gpu-performance-standards.md)，审计基线见[阶段记录](../archive/gpu-2026-09-22/gpu-performance-baseline.md)。RB-0/RB-1、M1、失败候选和单次测量证据已经归档，
不得作为并行活动计划恢复。

## 当前目标

在不改变画面、玩法权威状态和 mixed frame 数值合同的前提下：

1. 建立同一 package、固定 workload 的低扰动性能基线。
2. 收敛 frame-slot 动态资源创建、上传、descriptor 与同步固定成本。
3. 根据重新归因结果选择 depth bridge 或有限 opaque producer 迁移。
4. GPU 固定成本收敛后，再削减 CPU producer 的 P95/P99 长尾。

不包含新材质体系、透明排序或粒子框架、SDL-free 平台重构、Linux native presenter 补齐，以及用减少
可见内容或改变 gameplay workload 换取性能数字。

## 唯一执行链

前一项没有满足退出条件时，不并行实施后一项。

### 1. M2 baseline 前置（当前）

用 RTX 3050 冻结审计基线对应的 baseline executable，完成 near 0、near 30、near 60、Campaign 的
`-NoAudit` 五轮低扰动采样。要求交流电、固定电源方案、固定 tick、相同 package hash，且每个场景的
workload sequence hash 跨轮一致。

退出条件：五轮全部有效，保存单轮范围和中位轮；没有 audit 墙钟混入低扰动 FPS 结论。

### 2. M2 动态资源复用

按 frame slot 持久复用动态 buffer/resource/descriptor，容量只增长，稳态不销毁或重建；本切片保留现有
skinning submit/wait。baseline 与 candidate 必须是独立 executable，并通过五轮 AB/BA 比较。

退出条件：

- frame audit 证明动态资源 generation、slot recycle、world retirement 和 resize 生命周期正确；
- RTX 3050 的 whole-loop 与相关 CPU/GPU 细项有可重复改善，P95/P99 无反向异常；
- Intel 通过适用 Full、普通性能下限和零 fallback/readback/copy 门禁；
- validation/sync、fault 和 soak 按资源生命周期变更风险补齐。

### 3. M2 后重新归因

若 skinning fence 仍是最大固定项，进入 M3：把上传与 skinning 纳入帧命令依赖链，并证明等待没有转移到
slot recycle、present 或其他 fence。否则比较 depth bridge 与 gear/weapon 边界预检，选择证据收益更高者。

退出条件：以 RTX 3050 低扰动五轮数据确定下一切片，并在本计划顶部更新“当前切片”；不能根据单轮、
audit 墙钟或 RasterCmd 数量直接选择。

### 4. Mixed 固定成本后续

按重新归因结果一次只做一个独立 ablation：

1. skinning 同步；
2. depth bridge；
3. gear/weapon 或确实能减少真实 run/bridge 的相邻 opaque 内容。

opaque 迁移必须保持 lighting、alpha/depth-write、near clipping、逆深度、fog-free runtime 和资源 pin
合同。透明、粒子、overlay、复杂 VFX、舌头/死亡表现以及未冻结逐面光照合同的内容继续留在 Raster。

### 5. CPU producer 长尾

GPU 固定成本收敛后，依次评估 visibility/LOD、presentation snapshot、静态模板、pose/socket/gear cache
与重复扫描/分配。只有 CPU producer 仍持续超过约 4 ms，才评估多线程。

退出条件：固定 workload 下 CPU producer/pack 的 P95/P99 可重复改善，画面、命令序列和玩法状态不变。

## 每个候选的通用门禁

- 先通过正确性和结构预检，再运行五轮性能采样；失败切片立即撤销并归档。
- 报告 RasterCmd、Draw、实际 run、bridge 次数/字节、upload、GPU Raster/Draw/bridge 和 whole-loop，
  但不把不能相加的分位数相加。
- RTX 3050 负责性能选择和 60 FPS 阶段签收；Intel 负责 required-native 正确性、生命周期、普通 30 FPS
  下限与相对基线退化复核。
- presenter、同步或资源生命周期改动必须增加 resize、validation/sync、fault injection 与 soak。
- producer 迁移必须增加 CPU/reference differential、near/mid/thin-far、30/60 敌人、Campaign 和受影响
  fixed capture。

## 完成条件

达到 [GPU 性能标准](../reference/gpu-performance-standards.md) 的目标，或者可靠归因证明剩余瓶颈属于新的独立问题域。
若需开启透明/VFX、厂商驱动专项或新的 Graphics 类型，先归档本计划，再由 `plans/README.md` 指向新的
唯一活动计划。
