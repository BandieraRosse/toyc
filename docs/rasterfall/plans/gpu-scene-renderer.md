# 下一代统一 GPU 渲染器计划

> 状态：当前唯一活动计划；设计目标，尚非现行架构
>
> 所有者：Rasterfall GPU 渲染与性能主线
>
> 制定：2026-09-23
>
> 当前切片：阶段 1A 的隔离基础设施；actor identity、actor/world/transient 值 snapshot 与有序 Scene 元数据已落地，下一步是数据源 adapter、完整资源/材质/pose 提取；正常帧仍走旧路径

本计划取代[旧 GPU Raster / Bridge 收敛计划](../archive/gpu-raster-bridge-20260923.md)。现行所有权和实现以
[渲染架构](../architecture/rendering-architecture.md)、[GPU 渲染架构](../architecture/gpu-rendering-architecture.md)
及源码为准；本文描述目标状态和迁移顺序，不能当作已实现能力。验收工作流由
[GPU 验收指南](../guides/gpu-validation.md)拥有，目标由[GPU 性能标准](../reference/gpu-performance-standards.md)拥有。

## 决策与目标

正常帧改为由一个 GPU Scene 驱动的统一渲染路径：WORLD 不透明内容共用 graphics color/depth，
之后按层执行有序透明、特效、独立深度的 VIEWMODEL 和屏幕 OVERLAY，再 native present。
Raster/Draw 交替执行及全屏深度 bridge 退出正常帧。旧 mixed renderer 保留为迁移期间的独立对照路径；
CPU renderer 保留为离线 reference 和非 GPU 模式，不承担 GPU 帧的部分回放。

RTX 3050 是高性能设计和性能取舍设备；Intel Iris Xe 在架构闭合后承担 required-native 正确性、
生命周期、普通 30 FPS 下限及相对基线退化复核。当前 M2 的 Intel 与 validation/sync 未完成状态随
本计划保留，不将旧路径的 3050 结果冒充跨设备最终签收。

本计划针对 1280×720 下常规场景 60 FPS 与 near 60 压力场景的[既定门槛](../reference/gpu-performance-standards.md)。
旧 M2 candidate 的低扰动五轮 whole-loop median 为 near 0 16.424、near 30 26.037、near 60 42.092、
Campaign 17.502 ms；near 60 GPU bridge median 4.253 ms。审计中的 CPU enemies、AI teammates 开销
也很高。不同阶段或分位数不可直接相加，消除 bridge 本身不足以保证 60 FPS。历史采样细节见
[旧计划](../archive/gpu-raster-bridge-20260923.md)。

### 不变量与允许变化

- gameplay、session 和网络权威状态不变；renderer 只消费冻结的 presentation 输入。地图可见几何不
  代替地图碰撞，资产坐标、bind pose、动画求值和末端变换继续分层。
- 保留 SKY → WORLD opaque → WORLD transparent → EFFECTS → VIEWMODEL → POST 语义边界 → OVERLAY
  的可见层序。透明保持稳定提交顺序、source-over、depth-test/no-depth-write；VIEWMODEL 使用独立
  depth/coverage；正常画面仍为 fog-free。静态世界光照来源和角色材质策略不能隐式变化。
- 不以删减可见内容、改变 fixed workload、降低分辨率、fallback、readback 或 CPU framebuffer copy
  换取性能。preflight 在首个 target 写入前完成；执行失败使整帧失败，不做半帧 CPU replay。
- 新硬件光栅路径不以旧整数 Raster 的逐像素舍入为永久 ABI。阶段 0 明确哪些结果必须精确、哪些允许
  有界视觉差异，并对固定 capture 审批一次新基线；在合同冻结前不得用“看起来接近”替代差分。

## 目标数据流与所有权

```text
Game/session truth + presentation cache
  → immutable PresentationSnapshot（camera、actor 语义、世界状态）
  → scene extraction（可见性、LOD、finalized pose/socket/attachment）
  → frozen GPU Scene（资源 handle、实例、材质覆盖、透明序列、层清单）
  → whole-frame preflight + resource pin
  → frame-slot upload / skinning / WORLD opaque
  → ordered transparent + effects / VIEWMODEL / OVERLAY
  → native present + slot retirement
```

| 边界 | 目标职责 | 禁止混入 |
| --- | --- | --- |
| PresentationSnapshot | 从玩法和展示缓存冻结本帧只读输入；固定 actor ID、camera、动作语义与变换 | GPU handle、资源生命周期、反向修改玩法真值 |
| scene extraction | 选择资源、求 pose/IK/socket、保守可见性和 LOD；保持 actor 与透明顺序 | 在材质或 GPU pass 中再次遍历玩法数组 |
| GPU Scene | 以稳定 handle/generation 存实例、材质覆盖、pass 分类和透明顺序；不持有裸 CPU 指针 | 逐三角形 RasterCmd 作为正常 WORLD 主格式 |
| frame graph / executor | 冻结 pass 依赖、target 读写、barrier、提交及诊断计时 | 根据 producer 名称隐式切 target 或热路径 queue-idle |
| resource cache / frame slot | 不可变 mesh/texture 跨帧共享；动态 staging、palette、skinning 输出、descriptor 随 slot 复用 | slot 未退休前销毁或更新其引用的资源 |

第一版只用单 graphics queue 和显式有序 pass。upload、skinning 与 draw 由同一帧依赖链排序；
只有跨队列收益得到证实时才引入异步 compute。Vulkan dynamic rendering、synchronization2、
descriptor indexing 等能力须在 RTX 3050 和 Intel 实际枚举后选定共同基线或明确兼容实现；
不把某一扩展默认为所有设备可用。GPU-driven culling、multi-draw indirect 和新材质体系不是首版前置。

## 执行链与退出门槛

前一阶段的合同和端到端退出门槛未满足时，不在正常帧中并行启用下一阶段。每阶段允许局部调试，
但正式五轮 A/B 只在阶段退出时有完整可运行候选的里程碑进行；避免为每个小改动重复整套性能采样。
阶段 0 的源码覆盖与接口风险已列在附属合同；为缩短前期盘点，先开发不接入正常帧的 1A 纯数据模块。
固定画面审批、正式基线和设备能力门禁仍未完成，在它们完成前不得把新 Scene 设为正常帧候选。

### 当前开发顺序与验证预算

| 顺序 | 交付物 | 日常验证；阶段末验证 |
| --- | --- | --- |
| 0A | 补全覆盖矩阵中 direct-pixel、特殊材质、条件地图和资源来源的归属；把实际阻断项列清 | 源码与已有审计核对；阶段 0 末一次固定场景审计 |
| 0B | 确定 actor/world/prop/transient 的身份及 generation 来源、冻结时刻和失败边界；收敛 V1 接口 | 接口审阅；实现前不跑 GPU suite |
| 0C | 固定 capture 输入、画面差异审批字段及可重跑命令 | 只为缺失的 fixture 做定向 capture；阶段 0 末统一冻结 |
| 0D | 以同一 package 完成阶段 0 基线，保存 package 内容哈希、executable 哈希、设备/驱动、workload 与电源信息 | 仅此时运行需要的结构审计和低扰动五轮；缺失设备证据如实标记，不反复探测 |
| 1A | 建立 snapshot/scene 版本结构、稳定身份、arena 和确定性 extraction | 相关逻辑用例与构建 |
| 1B | 建立资源表、整帧 preflight/pin、frame-slot 退休和独立启动选择；接静态地图与最小角色 | 定向资源/离屏用例；阶段 1 末一次生命周期验收 |
| 2 | 接齐 WORLD opaque 的地图、角色与附件，共享 color/depth | 每个 producer 用最近的离屏差分；整段接齐后跑阶段 2 画面及性能门禁 |
| 3 | 接透明、effects、独立 VIEWMODEL、OVERLAY 和 native present | 每层定向差分；完整正常帧接齐后跑阶段 3 全套生命周期与性能门禁 |
| 4–5 | 根据完整帧归因改 CPU producer，随后完成跨设备签收 | 每项优化定向回归；阶段末和最终候选才重复正式测量 |

日常改动只运行受影响模块的构建、逻辑或离屏用例；失败时只补定位该失败所需的诊断。
Quick/Full、长时 soak、validation/sync、跨设备验证按阶段退出条件运行，不因文档修改或每个小补丁重跑。
性能脚本不用于接口设计、日常调试或提前寻找收益；阶段退出候选若被修改，先完成相关正确性回归，
仅在修复影响性能结论时重测该阶段。所有测量保留原始日志与身份，不能用短 smoke 充当性能基线。

### 阶段 0：合同、基线与迁移接口

第一步的[现行画面覆盖矩阵](gpu-scene-coverage.md)记录源码可达内容及未确认的运行证据。
第二步的[迁移接口合同](gpu-scene-interface.md)定义 snapshot、scene、稳定身份、顺序与所有权；
尚待核对来源 ID 和实际设备能力。
第三步的[迁移基线状态](gpu-scene-baseline.md)记录已有 Windows native 结构审计和阶段退出时需补的冻结证据。
第四步的[画面差异合同](gpu-scene-visual-contract.md)列出精确项、容差统计和固定审阅集合；
阈值与新基线仍待首份候选前审批。

1. 枚举现有 WORLD、transparent、effects、VIEWMODEL、OVERLAY 的 producer、材质/纹理语义、
   depth-write、near clipping、静态光照、资源 pin 和当前 unsupported 情况；把会阻断统一 WORLD 的
   内容列成覆盖矩阵，不以文件名推断边界。
2. 定义 PresentationSnapshot 与 GPU Scene 的最小版本化结构、稳定 ID/generation、实例和透明顺序字段；
   确定 Core freeze/preflight、renderer producer、GPU cache 和 presenter 的接口所有者。
3. 在覆盖和接口合同收敛后，对 near 0/30/60、Campaign、mid、thin-far、0/30/60 敌人、固定角色/地图
   capture 冻结输入和画面。阶段 0 退出时再一次性采集所需性能基线。保存 build/package/executable
   hash、workload sequence hash、设备/驱动、分辨率和电源方案；区分审计归因与低扰动 whole-loop。
4. 写明画面差异政策：精确项目、容差项目、人工审阅的固定截图及其审批记录；旧路径保留为诊断对照。

退出条件：覆盖矩阵没有未归属的正常帧内容；新接口和画面合同可供实现与差分；冻结基线可复现。
未达到时，不以 shader 原型代替合同决策。

### 阶段 1：GPU Scene 与 frame-slot 基础设施

实现只读 snapshot → frozen GPU Scene，整帧资源解析、pin 和 preflight。保留旧 renderer 选择开关；
新路径建立 pass/target 资源图、frame-slot arena 和容量只增长的动态 backing。以 generation 和 slot
退休为唯一可回收依据。先接入静态地图和最小角色实例，但暂不宣称正常帧完成。

退出条件：同一 frozen 输入可重复生成等价实例/层序；world-cycle、resize、资源增长/退休和故障边界
可审计；没有正常热路径资源重建或 queue-idle。运行最近的逻辑、资源与离屏验证。

### 阶段 2：统一 WORLD 不透明路径

将地图、ground、static RMESH、普通与特殊敌人 body、队友 body、opaque gear/weapon 作为一个
graphics 深度域提交。pose/IK/socket 仍由 CPU 展示层拥有，GPU skinning 消费已冻结 palette；
rigid 附件消费相同 finalized placement。保持保守 near clipping、lighting、alpha/depth-write 和资源 pin。
允许按材质/mesh 批处理不透明实例，但必须证明不会改变可见顺序和材质结果。

退出条件：固定 near/mid/thin-far、30/60 敌人和 Campaign 中，正常 WORLD 不再触发 Raster↔Draw
depth bridge；完整不透明内容通过画面合同、CPU/reference differential、角色 vertex diff 和固定 capture。
对已覆盖场景执行 RTX 3050 首次低扰动五轮 A/B，报告 whole-loop、GPU pass、CPU extraction、
上传、实际 draw/instance、P95/P99；结果决定是否调整实例/材质组织，不用命令数代替收益。

### 阶段 3：完整 GPU 正常帧

把有序透明、effects、VIEWMODEL 独立 depth/coverage、screen OVERLAY 与 native present 接入
同一帧依赖图；POST 只保留当前正常画面所需的语义边界。不能转换的特殊效果必须有明确 GPU 表示或
在此阶段形成阻断决策，不能静默掉帧、隐藏内容或借 CPU framebuffer copy 完成 present。

退出条件：四个固定场景和受影响 capture 在新路径完整运行；required native、零 fallback/readback/
CPU framebuffer copy、零 invalid transition、零热路径 queue-idle；Quick/Full、resize、world-cycle、
fault 和 10,000 帧 soak 按新生命周期覆盖。再次执行 RTX 3050 五轮 A/B，保留旧路径独立 executable。

### 阶段 4：CPU producer 与可见性

在完整新路径上按 near 60 的独立阶段计时，依次评估 presentation snapshot 生成、保守
visibility/LOD、静态实例模板、pose/socket/gear cache、重复扫描与分配。每次只改一个所有权边界，
LOD 必须以冻结的距离/投影规则和视觉合同证明可见内容符合要求。只有 producer 仍持续高于约 4 ms，
才评估线程并行；worker 不能改变提交序列、玩法真值或资源 pin 生命周期。

退出条件：固定 workload 下 CPU extraction/pack 的 P95/P99 可重复改善，画面与 gameplay 状态不变；
RTX 3050 低扰动五轮 whole-loop 的变化和范围足以支持保留该阶段结果。

### 阶段 5：性能签收与跨设备补齐

RTX 3050 按[性能标准](../reference/gpu-performance-standards.md)完成 near 0/30/60、Campaign 的
median/P95/P99 阶段或最终门槛；保留 GPU pass 和 CPU producer 的可归因数据，不能把分位数相加。
若目标未达，先定位新架构中最大的完整帧瓶颈，再决定单独的后续计划，不退回逐个 bridge 补丁链。

之后在 Intel Iris Xe 上用同一候选 package 完成 required-native、适用 Full、普通 30 FPS 下限、
相对冻结基线退化、resize/world-cycle/thin-far 和零 fallback/readback/copy；在装有
`VK_LAYER_KHRONOS_validation` 的环境完成 validation/sync，并证明 layer 实际加载与同步验证启用。
受影响的 fault 和 soak 结果一并保留。缺少设备或 layer 时只标记该门禁未完成，不宣称最终签收。

## 阶段门禁与决策规则

- **结构门禁：** 每个阶段先证明目标层序、target 所有权、资源 generation、失败语义和实际 bridge/run
  变化；禁止用 RasterCmd、draw call 或单次 audit 墙钟推断 FPS。
- **画面门禁：** 固定 capture、像素/深度差分和可见内容清单共同判断；容差必须先在阶段 0 冻结。
  失败时修正新路径或公开修订合同，不把不符合合同的结果写成性能收益。
- **性能门禁：** 阶段 0 退出时冻结旧路径基线；此后只在阶段 2、3、4 的完整候选及最终签收跑正式低扰动五轮 A/B；同 package、
  独立 executable、交流电、固定电源方案、固定 tick/workload，保存各轮和中位轮。
- **回滚门禁：** 新路径用显式启动选择隔离，整帧选择旧或新 renderer。任何 GPU required 执行失败
  非零退出；不能在同一帧做局部 CPU replay。撤销的阶段结果归档，不长期维护两个“当前”设计。
- **文档门禁：** 所有权或数据流实际落地时，同一改动更新 architecture 与
  [`../README.md`](../README.md) 任务路由；命令变化更新 guide；旧测量和失败候选归档。

## 范围边界与完成定义

首版不引入新 PBR 材质、OIT、全局 GPU-driven pipeline、光追、跨平台 presenter 重构或新的玩法字段。
若 shader/material 覆盖矩阵证明其中一项不可避免，先在本计划记录阻断原因、成本与替代方案，再决策。

完成需同时具备：统一 GPU Scene 正常帧、零 Raster/Draw 深度 bridge、冻结画面合同通过、Windows native
生命周期与 validation/sync 通过、RTX 3050 性能门槛达到或对剩余独立瓶颈给出可复核归因，以及
Intel 普通档门槛完成。完成或被替代时整体归档，由 `plans/README.md` 指向下一份唯一活动计划。
