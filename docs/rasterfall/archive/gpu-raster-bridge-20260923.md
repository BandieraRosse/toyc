# GPU Raster / Bridge 收敛计划

> 状态：历史；2026-09-23 被下一代统一 GPU 渲染器计划替代
> 所有者：Rasterfall GPU 性能主线
> 最近核对：2026-09-23
> 当前替代入口：[下一代统一 GPU 渲染器计划](../plans/gpu-scene-renderer.md)

以下执行顺序和“当前切片”仅代表归档时的旧方案。M2 测量仍可作迁移基线，未完成的 Intel 与
validation/sync 门禁已转入新活动计划；本页不再决定实施优先级。

本文只记录尚未完成的执行顺序和决策门。稳定数据流见
[GPU 渲染架构](../architecture/gpu-rendering-architecture.md)，验收工作流见
[GPU 验收与诊断](../guides/gpu-validation.md)，目标与冻结基线见
[GPU 性能标准](../reference/gpu-performance-standards.md)，审计基线见[阶段记录](gpu-2026-09-22/gpu-performance-baseline.md)。RB-0/RB-1、M1、失败候选和单次测量证据已经归档，
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

先在 RTX 3050 上完成高性能开发与逐切片五轮 AB/BA，再补 Intel Iris Xe 普通档及 validation/sync 门禁。
3050 阶段结论只用于选择和推进高性能切片，不宣称 M2 已跨设备最终签收。

### 1. M2 baseline 前置（已完成）

用 RTX 3050 冻结审计基线对应的 baseline executable，完成 near 0、near 30、near 60、Campaign 的
`-NoAudit` 五轮低扰动采样。要求交流电、固定电源方案、固定 tick、相同 package hash，且每个场景的
workload sequence hash 跨轮一致。

退出条件：五轮全部有效，保存单轮范围和中位轮；没有 audit 墙钟混入低扰动 FPS 结论。

2026-09-23 已用同 package 的独立 baseline/candidate exe 完成四场景五轮 AB/BA；两组低扰动运行均 PASS，
对应单轮与中位数据保存在 `tmp/amd3050-m2-abba-20260923/`。同 package audit 配对的四场景 workload
sequence hash、bridge 次数与字节数一致。此前冻结的三轮审计仍只用于成本归因，不充当 FPS 基线。

| 场景 | baseline whole median | candidate whole median | baseline P95/P99 | candidate P95/P99 |
| --- | ---: | ---: | ---: | ---: |
| near 0 | 23.323 ms | 16.424 ms | 27.227 / 31.055 ms | 18.270 / 26.810 ms |
| near 30 | 33.925 ms | 26.037 ms | 41.105 / 45.842 ms | 31.235 / 36.821 ms |
| near 60 | 51.621 ms | 42.092 ms | 61.154 / 67.785 ms | 51.101 / 57.947 ms |
| Campaign | 27.180 ms | 17.502 ms | 31.916 / 35.547 ms | 21.354 / 26.501 ms |

表中各值均为五个单轮结果的中位数，不是逐帧合并的分位数。候选已通过 Windows build/test/gpu-test、
GPU Quick/Full、五类 fault 与 10,000 帧 soak。当前机器缺少 validation layer，且只有 RTX 3050 与 AMD
Vulkan ICD，validation/sync 和 Intel 普通档复核尚未完成；M2 已满足 3050 高性能开发的继续条件，
但不能视为跨设备最终签收。异机复核须使用与本轮候选一致的 executable 和 package，核对 hash 后再采样。
当前候选复用 normal skinning 的 Draw resource 与 descriptor；CPU skinning 回滚路径仍沿用每帧创建，
上传 staging 仍为临时 buffer。后续是否继续收敛这两处成本，须先看剩余固定成本归因。

### 2. M2 动态资源复用

按 frame slot 持久复用动态 buffer/resource/descriptor，容量只增长，稳态不销毁或重建；本切片保留现有
skinning submit/wait。baseline 与 candidate 必须是独立 executable，并通过五轮 AB/BA 比较。

退出条件：

- frame audit 证明动态资源 generation、slot recycle、world retirement 和 resize 生命周期正确；
- RTX 3050 的 whole-loop 与相关 CPU/GPU 细项有可重复改善，P95/P99 无反向异常；
- Intel 通过适用 Full、普通性能下限和零 fallback/readback/copy 门禁（3050 高性能切片完成后补签）；
- validation/sync、fault 和 soak 按资源生命周期变更风险补齐；其中 validation/sync 在具备 layer 的环境补签。

### 3. M2 后重新归因

若 skinning fence 仍是最大固定项，进入 M3：把上传与 skinning 纳入帧命令依赖链，并证明等待没有转移到
slot recycle、present 或其他 fence。否则比较 depth bridge 与 gear/weapon 边界预检，选择证据收益更高者。

退出条件：以 RTX 3050 低扰动五轮数据确定下一切片，并在本计划顶部更新“当前切片”；不能根据单轮、
audit 墙钟或 RasterCmd 数量直接选择。

2026-09-23 在同一 M2 candidate executable（SHA-256 `13AD1BC57012FE6AEF4A9E345DF9E1999D3E777A91CE3FA802D91EA6003F87A6`）
上完成四场景五轮审计，结果 PASS，原始证据在 `tmp/amd3050-m2-reattribution-20260923/`。各场景的
workload sequence hash 跨轮一致；near 0/30/60 均为每帧 4 次、29,491,200 字节 bridge，Campaign
为 10 次、73,728,000 字节。near 60 五轮 graphics wait 单轮 median 范围为 1.179–1.249 ms，
动态资源阶段为 2.232–2.409 ms。四次 near bridge 均只传 depth；低扰动五轮 candidate 的 near 60
GPU bridge median 为 4.253 ms。gear 边界关联最后一次 export，weapon 未关联 bridge；只迁移
gear/weapon 尚无减少真实 bridge 的证据。因此下一切片先做 depth bridge 的结构与数值预检，
通过后才做独立实现和五轮 AB/BA；审计墙钟不作为收益预测。

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

### 6. 跨设备补签

3050 高性能切片完成后，在 Intel Iris Xe 上使用同候选 package 完成适用 Full、普通档性能下限、
相对冻结基线退化复核、零 fallback/readback/copy 与生命周期检查；在具备
`VK_LAYER_KHRONOS_validation` 的环境完成 validation/sync，并保留 layer 实际加载与 Synchronization
Validation 启用的日志。按受影响资源生命周期补齐专项，不用 3050 结果代替 Intel 结论。

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
