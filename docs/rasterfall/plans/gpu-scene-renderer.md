# 独立 GPU Scene 渲染架构计划

> 状态：当前唯一活动计划；目标尚未全部实现
> 所有者：Rasterfall GPU 渲染与性能主线
> 方向调整：2026-09-24
> 当前切片：阶段 4，敌人单次提取缓存与连续 draw 绑定复用已落地；继续评估剩余提取和上传成本

## 决策

2026-09-24 按核心功能调整门槛：单人 GPU Scene 游玩已基本成立，阶段 3B 以现有核心功能基线结束，
现在进入阶段 4。旧功能未来会调整，不再要求全面补齐旧功能、展示站/编辑器表现、全部交互组合
或展示细节才允许推进。保留核心游玩、内容遮挡、只读来源、零旧路径补画及原生稳定性约束；
影响这些约束的缺陷随阶段 4 修复。此决策不追加未经执行的验收，也不代表架构或性能已全面签收。

新 GPU 渲染器与旧 CPU/mixed 帧组织分开。新路径只消费玩法和展示状态的只读快照，
独立提取 Scene、准备资源、执行分层 pass 并 native present；禁止先运行旧渲染入口，
再丢弃 RasterCmd/mixed recording 来获得 Scene 输入。

复用已经验证的 Vulkan 绘制、GPU skinning、资源 registry/cache、资产解码、动画求值、
swapchain 与同步能力。重建帧编排、各层目标、透明顺序和资源生命周期的组织方式，
不重写玩法、session 或网络权威状态，不推倒底层 GPU 后端和资产系统。

旧路径保留为整帧独立启动选项，只修必要问题，停止围绕它扩展新功能。
默认路径在完整新帧验收前仍为 mixed。3B 提供显式的单人可玩新渲染入口；
当前独立预览只用于开发，不代表完整画面或产品性能。

## 目标链路与合同

```text
玩法/session + 展示缓存
  → 单次冻结的只读帧快照
  → 独立 Scene 提取（可见性、pose/socket、实例、材质、有序透明）
  → 资源准备、整帧 preflight、generation pin
  → SKY / WORLD opaque / transparent / EFFECTS / VIEWMODEL / POST / OVERLAY
  → native present / slot retirement
```

- renderer 不修改 gameplay；GPU executor 不回读玩法数组。世界、碰撞、可见几何保持分层。
- WORLD 共用 color/depth；透明保留显式稳定顺序、source-over、depth-test/no-depth-write；
  VIEWMODEL 独立 depth/coverage。天空、标签、HUD、菜单和特效都必须进入新帧。
- preflight 在目标写入前完成；失败整帧退出，禁止局部 CPU replay、depth bridge、
  readback 或 framebuffer copy 补画。资源 pin 保留到对应 GPU slot 退休。
- 第一版单 graphics queue、显式有序 pass；完整帧成立后按测量优化复用与流水。
- 画面验收以内容完整、遮挡正确、光照和材质表现合理为准。旧整数光栅舍入和整图像素匹配
  不再是开发前置或永久 ABI；已知差异记录后重构收敛。固定新画面规范在完整帧阶段建立。
- 性能以 RTX 3050、固定 workload、1280×720 的完整帧 whole-loop 与分段计时证明；
  缺层预览、减少敌人或 draw 数都不能作为 FPS 收益。

## 当前实施顺序

| 阶段 | 交付与完成条件 | 验证 |
| --- | --- | --- |
| 3A 独立动态 WORLD | 不调用旧整帧 producer，不创建 RasterCmd/mixed recording；在现有地图、正式队员、旗帜、投射物和交互物之上，补齐敌人身体/特感姿态/死亡与附属表现、程序和补充模块化角色、倒地角色及网络展示输入，全部从只读接口冻结并在独立预览可见 | 原生构建、来源逻辑回归、动态战斗 capture、逐帧零旧录制检查、定向 Scene native/sync |
| 3B 单人核心可玩帧（按调整后门槛结束） | 已提供独立分层及显式单人可玩入口，以现有核心游玩能力作为后续基线；旧功能全面补齐与展示细节不作为完成前置 | 已有定向内容、窗口输入、resize/world-cycle/fault/连续运行及 validation/sync 证据；不冒称所有交互或最终包完整矩阵通过 |
| 4 资源复用与流水（当前） | 在核心游玩帧上归因并消除重复扫描、动态网格重建、pose 分配和重复上传；按退休状态复用 slot，依据测量决定并行方案 | 相关回归和 CPU extraction/pack、GPU upload/skinning/draw、whole-loop median/P95/P99；固定内容与 workload 前后对比 |
| 5 性能与生命周期签收 | RTX 3050 达到既定性能标准，并完成完整原生生命周期验收；实机联机专项在单人可玩入口之后安排 | 同候选 package、固定 tick/workload 的低扰动五轮、完整 fault/resize/world-cycle/长时 soak；保留设备、哈希与原始日志 |

阶段 0–2 的资源与诊断成果继续复用，不回到旧像素一致性审批链。网络先验证展示输入与逻辑，
实机联机专项安排在单人可玩入口之后；网络可见内容仍属于最终完整帧范围。

## 已有功能基线

`--gpu-scene-independent-preview` 已开始落地独立来源：跳过 `rf_game_render_profiled`，
使用只获取窗口 surface/extent 的 Core begin，直接冻结地图、正式模块化队员、旗帜、投射物和交互物，
消费已有 Scene 资源准备、GPU skinning 与 native present。逐帧检查旧 command/mixed draw 数为零。
旧 `--gpu-scene-world-preview` 保留为 producer 驱动的诊断对照，不再作为新功能接入入口。

独立预览已从只读展示接口接入普通/特殊/LEGACY 敌人、死亡变换与阴影/舌头、程序角色、
补充模块化角色、downed 和网络角色输入。日志报告 `dynamic_sources_pending=0` 及各类来源数量；
该字段只表示动态来源已接通，不表示整帧完成。天空、地图透明与死亡渐隐、特效、
VIEWMODEL、HUD/交互提示/地图标签和基础暂停/结算面板已进入独立分层批次。
特效已补世界空间近裁剪射线、盒体碎片和受击方向箭头。暂停设置、结算、动态准星、
计分板共享 canvas 布局，AI 名字/血量/倒地反馈由 Scene 提取。Console/GUI 在 normal runtime
本就受关闭 gate 限制，不作为单人入口的新增前置。展示站/编辑器专用表现仍未纳入完整验收。
目前仍共享 runtime 中的 Scene 审计编排、串行资源 owner 和底层 renderer 几何/光照 helper，
独立 begin 仍使用已有 GPU 初始化设施；这不是最终 Core/Scene executor 拆分或多帧流水。

动态来源由独立 adapter 冻结，使用各自 presentation history；补充模块化项仍是帧内身份，
不是跨帧异步资源方案。定向捕获可在显式帧读回同一 Scene 批次，常规 native 帧保持零读回。
本轮来源、原生捕获与验证现场见[动态来源记录](../archive/gpu-scene-dynamic-sources-20260924.md)。
首版分层与定向验证见[分层接线记录](../archive/gpu-scene-layers-20260924.md)。
`--gpu-scene-play` 已开放显式实验性单人入口，沿用真实输入和时钟；基本窗口输入、resize、
world-cycle、fault 和真实波次连续运行已通过，现场见[单人入口记录](../archive/gpu-scene-play-20260924.md)。
购买/重启等未完成专项、完整内容规范与展示细节不再作为阶段 4 前置；后续按核心功能缺陷及实际需求处理。
现有预览日志 `world_only=0` 只表示含非 WORLD 层，不是完整帧签收。资源重建成本明显，尚无性能达标结论。
禁止为补齐内容重新调用旧 producer 或给新路径加入兼容 Raster 通道。

## 阶段 4 进度与下一决策点

首轮已补 Scene 准备分段、提交/退休及从循环开始到退休后的墙钟计时，配套同包固定 workload 的交替 A/B。
测量定位到敌人/程序角色的逐帧资源重建为最大开销，现已在同步退休后按容量复用 GPU 三角形资源。
顶点仍逐帧完整更新，索引、纹理、descriptor 与 buffer 保留；容量不足才重建，世界关闭时释放。
固定 workload 的 draw 与来源数量不变。测量与验证现场见[首轮成本优化记录](../archive/gpu-scene-resource-reuse-20260924.md)。

1. 已拆分正式模块化队员的 `scene_load`、`scene_pack` 和 upload/skin/wait 墙钟，并消除逐次临时 staging 分配：
   目标可直接写入时 map/flush，否则保留 staging 容量。现场见[角色上传优化记录](../archive/gpu-scene-actor-upload-20260924.md)。
   冻结/CPU pose 与准备分别统计，GPU skinning 执行和驱动/等待尚未独立归因。
2. 敌人 CPU 几何已加入单次蒙皮顶点和精确坐标光照缓存；连续 draw 复用绑定，提交/呈现与退休分别计时。
   同包 A/B 与验证见[敌人几何及提交优化记录](../archive/gpu-scene-enemy-submit-20260924.md)。
   下一切片评估剩余敌人提取/上传成本；角色后续评估不变 bind 缓存及蒙皮提交合并，分层资源仍待复用。
   保持 generation pin 到对应 slot 退休，禁止复用仍在 GPU 使用的资源；保留当前动态资源复用回归。
3. 每个优化在同内容、同 workload 下比较耗时和资源计数，运行最近的逻辑、Scene native/sync 及适用的
   resize/world-cycle/fault 回归。保留核心画面与游玩行为，不能通过减少敌人、draw 或可见内容获得收益。
4. 资源复用收益确认后，再依据等待与 CPU/GPU 占比决定多帧流水或并行工作；不预先开展大范围架构重写。

下一决策点是剩余敌人提取/上传与正式队员准备成本，再决定是否推进流水。
阶段 4 尚未完成；正式五轮性能门槛和
完整生命周期仍在阶段 5，不能用“已进入阶段 4”代替签收。

## 验证预算与完成定义

日常只跑最近的构建、逻辑、离屏或原生定向验证；阶段 4 保持核心内容与遮挡、
零旧路径补画及必要的原生稳定性检查，不扩展为旧功能全面验收。完整生命周期候选再执行 Full、10,000 帧 soak
及正式五轮性能采样。开发预览每帧报告独立来源、缺口、draw/upload 和 GPU 时间，
不混入旧 mixed 的性能统计。实际工作流见 [Scene fixture](../guides/gpu-scene-fixture.md)。

单人可玩、架构完成和性能达标分别记录：3B 达到可玩门槛后即可提供显式启动入口；
完整独立帧、内容/遮挡合同、零 bridge/fallback/copy、Windows native 完整生命周期和 validation/sync 通过，才算架构完成；
[GPU 性能标准](../reference/gpu-performance-standards.md)的最终 median/P95/P99 通过，才算性能完成。
任一未达不得宣布整个计划完成。无需以推倒 Vulkan、动画或资产系统换取所谓独立性。

调整前计划和阶段证据索引保存在 [历史快照](../archive/gpu-scene-before-independent-20260924.md)。
稳定所有权由 [GPU 架构](../architecture/gpu-rendering-architecture.md)拥有，
接口增量参考 [Scene 接口](gpu-scene-interface.md)，与本次方向冲突的旧迁移前置不再适用。
