# 独立 GPU Scene 渲染架构计划

> 状态：当前唯一活动计划；目标尚未全部实现
> 所有者：Rasterfall GPU 渲染与性能主线
> 方向调整：2026-09-24
> 当前切片：阶段 3B，实验性单人入口与独立分层内容、交互和生命周期验收

## 决策

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
| 3B 单人可玩完整帧 | 接天空、有序透明、特效、独立 VIEWMODEL、POST、HUD/菜单/OVERLAY；以完成正常帧所需的最小 Scene 协调器统一冻结、资源准备、提交和退休；提供显式单人可玩启动选项 | 固定场景内容与遮挡验收、新画面规范、基本 resize/world-cycle/fault/连续运行和 validation/sync；逐帧确认无旧路径补画 |
| 4 资源复用与流水 | 在可玩完整帧上归因并消除重复扫描、动态网格重建、pose 分配和重复上传；按退休状态复用 slot，依据测量决定并行方案 | 相关回归和 CPU extraction/pack、GPU upload/skinning/draw、whole-loop P95/P99 |
| 5 性能与生命周期签收 | RTX 3050 达到既定性能标准，并完成完整原生生命周期验收；实机联机专项在单人可玩入口之后安排 | 同候选 package、固定 tick/workload 的低扰动五轮、完整 fault/resize/world-cycle/长时 soak；保留设备、哈希与原始日志 |

阶段 0–2 的资源与诊断成果继续复用，不回到旧像素一致性审批链。网络先验证展示输入与逻辑，
实机联机专项安排在单人可玩入口之后；网络可见内容仍属于最终完整帧范围。

## 首个开发切片与下一步

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
继续完整内容规范、购买/重启等交互和展示细节验收后确认阶段 3B 门槛。现有预览日志 `world_only=0`
只表示含非 WORLD 层，不是完整帧签收。资源重建成本明显，尚不承诺可用帧率。
禁止为补齐内容重新调用旧 producer 或给新路径加入兼容 Raster 通道。

## 验证预算与完成定义

日常只跑最近的构建、逻辑、离屏或原生定向验证；3B 可玩入口前必须通过完整内容与遮挡、
零旧路径补画及必要的原生稳定性检查。完整生命周期候选再执行 Full、10,000 帧 soak
及正式五轮性能采样。开发预览每帧报告独立来源、缺口、draw/upload 和 GPU 时间，
不混入旧 mixed 的性能统计。实际工作流见 [Scene fixture](../guides/gpu-scene-fixture.md)。

单人可玩、架构完成和性能达标分别记录：3B 达到可玩门槛后即可提供显式启动入口；
完整独立帧、内容/遮挡合同、零 bridge/fallback/copy、Windows native 完整生命周期和 validation/sync 通过，才算架构完成；
[GPU 性能标准](../reference/gpu-performance-standards.md)的最终 median/P95/P99 通过，才算性能完成。
任一未达不得宣布整个计划完成。无需以推倒 Vulkan、动画或资产系统换取所谓独立性。

调整前计划和阶段证据索引保存在 [历史快照](../archive/gpu-scene-before-independent-20260924.md)。
稳定所有权由 [GPU 架构](../architecture/gpu-rendering-architecture.md)拥有，
接口增量参考 [Scene 接口](gpu-scene-interface.md)，与本次方向冲突的旧迁移前置不再适用。
