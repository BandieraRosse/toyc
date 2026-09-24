# 下一代统一 GPU 渲染器计划

> 状态：当前唯一活动计划；设计目标，尚非现行架构
>
> 所有者：Rasterfall GPU 渲染与性能主线
>
> 制定：2026-09-23
>
> 当前切片：阶段 3，开始接入真实 WORLD 的硬件 Scene native 提交，再接齐完整帧各层；正常产品默认仍为 mixed

2026-09-24 用户调整门禁：阶段 2 按迁移范围收尾；CPU/mixed 整图一致性、数值容差和新图像
基线审批不再阻塞阶段 3。已观察的光照、轮廓与光栅差异记录后延至重构完成再修复，不伪记为
旧画面合同 PASS。网络继续仅验证逻辑。下文原阶段退出门槛中的像素一致性要求据此延期；
GPU 执行失败、资源生命周期、层序、零 CPU fallback/copy 和零 depth bridge 仍是结构门禁。
首个开发入口明确为 WORLD preview，不能作为完整画面或产品性能验收。

阶段 2 补充：静态 RMESH 超出旧整数光栅屏幕范围时，已按安全的整数顶点变换选择 Scene 硬件裁剪，
`actor-standard` / `actor-rifleman` 的 8 / 4 项数值暂缓清零；近裁剪专项证明可见覆盖。
`actor-standard` 新增提交未改变固定 Scene 截图，不作为修复可见缺物或性能收益的证据。
验证记录见[静态实例裁剪现场](../archive/gpu-scene-static-prop-clipping-20260924.md)。
动态特感切片已将 Smoker、Charger、Tank 的存活身体从同帧 finalized pose 接入独立 Scene WORLD；
普通感染体六种 Block/Humanoid recipe 的存活身体也已接入，冻结家族、步态、变换与反馈颜色后独立提取。
程序角色、不透明死亡、legacy 敌人、blob shadow 和 Smoker 舌头已补入同帧离屏诊断；透明死亡留给透明层。
这条诊断依赖 mixed producer 冻结姿态，动态资源按同步审计帧重建，不能作为正常 Scene producer 或性能收益。
验证与范围见[动态特感身体现场](../archive/gpu-scene-special-enemies-20260924.md)。
普通感染体资源、RTX 3050 同步验证及局部像素记录见[普通感染体现场](../archive/gpu-scene-ordinary-enemies-20260924.md)。
WORLD 固定画面合同遗留问题按顶部用户决定延期，记录见[阶段 2 决定归档](../archive/gpu-scene-stage2-decision-20260924.md)。

后续顺序：直接推进完整 GPU 正常帧，画面差异重构后修复。
网络玩家按用户要求仅验证逻辑路径；实机连接、同步与可见性问题统一延后到 GPU 完成后的网络专项。
本轮收尾范围与后续事项见[阶段 2 收尾记录](gpu-scene-stage2-handoff.md)。
网络 adapter 冻结已解析的展示位置、朝向和 actor 状态，不把输入包交给 Scene。
网络角色只延后开发顺序，不从完整 WORLD 合同中删除。Smoker 舌头保留现有连接带和束缚圈的最小等价表现，
不在渲染迁移中删除束缚玩法或其可见提示。完成 WORLD 后集中推进独立来源和完整帧。

本计划取代[旧 GPU Raster / Bridge 收敛计划](../archive/gpu-raster-bridge-20260923.md)。现行所有权和实现以
[渲染架构](../architecture/rendering-architecture.md)、[GPU 渲染架构](../architecture/gpu-rendering-architecture.md)
及源码为准；本文描述目标状态和迁移顺序，不能当作已实现能力。验收工作流由
[GPU 验收指南](../guides/gpu-validation.md)拥有，目标由[GPU 性能标准](../reference/gpu-performance-standards.md)拥有。

## 决策与目标

正常帧改为由一个 GPU Scene 驱动的统一渲染路径：WORLD 不透明内容共用 graphics color/depth，
之后按层执行有序透明、特效、独立深度的 VIEWMODEL 和屏幕 OVERLAY，再 native present。
Raster/Draw 交替执行及全屏深度 bridge 退出正常帧。旧 mixed renderer 保留为迁移期间的独立对照路径；
CPU renderer 保留为离线 reference 和非 GPU 模式，不承担 GPU 帧的部分回放。

RTX 3050 是本计划的开发、正确性、生命周期、性能取舍与最终签收设备。其他设备适配作为未来独立工作，
不进入本计划的执行链、阶段门禁或完成定义。

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
descriptor indexing 等能力须在 RTX 3050 实际枚举后选定 Vulkan 基线；
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
| 0B | 确定来源生命周期 epoch、actor 子项顺序、world/prop/transient 身份及失败边界 | 接口审阅及相关逻辑用例 |
| 0C | 专用渲染地图、固定输入与统计方法；隔离原型提供容差证据 | 定向 capture；正式候选验收前批准容差 |
| 0D | 以同一 package 完成阶段 0 基线，保存 package 内容哈希、executable 哈希、设备/驱动、workload 与电源信息 | 仅此时运行需要的结构审计和低扰动五轮；缺失设备证据如实标记，不反复探测 |
| 1A | 收敛身份、确定性 extraction 和真实来源 adapter，停止扩展无消费者字段 | 相关逻辑用例与构建 |
| 1B | 静态地图、蒙皮角色、rigid 附件贯通 Scene、pin、draw、native present 与退休 | 定向资源/离屏、validation/sync；阶段末生命周期验收 |
| 2 | 接齐 WORLD opaque 的地图、角色与附件，共享 color/depth | 离屏差分与分段成本；不做产品 whole-loop A/B |
| 3 | 接透明、effects、独立 VIEWMODEL、OVERLAY 和 native present | 每层定向差分；完整正常帧接齐后跑阶段 3 全套生命周期与性能门禁 |
| 4–5 | 根据完整帧归因改 CPU producer，随后完成主设备签收 | 每项优化定向回归；阶段末和最终候选才重复正式测量 |

日常改动只运行受影响模块的构建、逻辑或离屏用例；失败时只补定位该失败所需的诊断。
Quick/Full、长时 soak、validation/sync 按阶段退出条件运行，不因文档修改或每个小补丁重跑。
正式五轮脚本不用于日常调试；阶段 0/1 允许定向计时建立预算，不能冒充收益。阶段退出候选若被修改，先完成相关正确性回归，
仅在修复影响性能结论时重测该阶段。所有测量保留原始日志与身份，不能用短 smoke 充当性能基线。

### 阶段 3 当前切片与下一步

`--gpu-scene-world-preview` 已用真实 normal-scene/wave workload 进入 native Scene WORLD。
Core 只保留 producer 求值，丢弃未执行的 mixed recording；Scene 独立提交并同步退休，
不执行 mixed、不读回 color/depth。near/mid/thin-far/Campaign 各四帧 RTX 3050
validation/sync 已通过，原始日志在 `tmp/stage3-preview-final/`。
接下来接入天空、有序透明和 effects，再接独立 VIEWMODEL 与 HUD/OVERLAY；
完整层序接齐后切正常候选与生命周期/性能验证。现有输入仍依赖旧 producer，
独立来源与动态资源复用继续收敛，不恢复阶段 2 像素匹配门禁。

### 阶段 1–2 实现记录（历史上下文）

本轮交付为来源生命周期身份修正、专用渲染地图和可重跑诊断入口；运行相关逻辑回归、Windows 构建、
地图解析及定向 capture。地图通过显式参数选择，不替换正式地图，也不计入 near/Campaign 性能成绩。
复现见[渲染 fixture](../guides/gpu-scene-fixture.md)。地图只提供 authored world 内容；蒙皮角色、附件、
特殊材质、effect 和 UI 由后续冻结的诊断输入提供，地图存在不等于这些内容已覆盖。

几何差异已定位到 persistent map 的世界 Y 被通用模型 foot-origin 再归一化，以及 box 一侧
颜色符号与 CPU 不同；已在 producer 修复，不改 shader 投影或放宽像素容差。LABEL 旧直接像素
被延迟 WORLD flush 覆盖，已移到正式 OVERLAY，SIGN 保留世界深度。多镜头、仅地图 policy 与
单物体缩减入口见[fixture 指南](../guides/gpu-scene-fixture.md)；本次证据见
[几何与来源记录](../archive/gpu-scene-geometry-20260923.md)。这些是可信对照输入，不是新 Scene 基线审批。

本地来源 adapter 使用 session 创建的八名非 hired 正式模块化队员，创建/reset/unload 事件分别提供 epoch，
复制真实动作、位置、角色与附件所需语义，并通过现有 snapshot → Scene 元数据链。
这些冻结值已通过隔离 pose extractor 复用现有 RFANIM、IK、socket 求值，输出 body palette 和
gear/weapon placement；每名 actor 的 lower-body 展示时钟由来源分别冻结，不借用旧 slot pose cache。定向资源回归见
[fixture 指南](../guides/gpu-scene-fixture.md)。提取仍每次创建独立 instance；fixture 和正常帧审计
已使用资源 handle/generation、退休后可复用的动态 backing。其余角色类别及正常呈现的资源生命周期仍待接入。
不得从 executor 回读 actor。
该三件套现已通过显式 fixture 接入独立 Scene target/submit/retire，资源解析、palette 与材质 preflight、
pin、GPU skinning、共享 WORLD depth、直接 swapchain blit 已贯通。Windows 生命周期专项覆盖增长、resize、
world 失效及故障退出；入口见 [fixture 指南](../guides/gpu-scene-fixture.md)。RTX 3050 已证明 Khronos layer
实际加载、同步验证开启，并完成三件套、增长、resize、world 退休和五类故障；现场见
[同步与绕序修复记录](../archive/gpu-scene-sync-culling-20260924.md)。
CPU pose 保留独立 instance 求值；Scene slot 的 CPU pack/upload 数组在退休后按容量复用，world generation 退休不释放这些数组。
独立 Scene 提交的 WORLD draw 与 swapchain blit 已使用同一 query pool，fence 退休后按 frame ID 读取 GPU 时间；
它不包括先前同步提交的 upload/skinning，也不代表完整产品帧。下一步进入阶段 2，扩 WORLD opaque，
角色接入范围继续扩大。完整透明、effects、VIEWMODEL、HUD/OVERLAY 接齐后才做正式性能 A/B。
本轮 RTX 3050 同步验证和可复核输出见[backing 与时间戳现场](../archive/gpu-scene-backing-timestamps-20260924.md)。
正常帧 `--frame-audit` 已将 Runtime Map authored render ID、投影顺序、air gate/platform 展示值
送入 V2 snapshot 与有序 Scene 元数据；定向 near 0 输入记录 83 条 world 项、1 条 local actor 项。
独立 V1 world render 值帧按同一 frame/world/map generation 冻结完整 `toy_map_draw`，并与 V2 snapshot
逐项核对 authored ID、顺序、可见性与 alpha。现有 persistent map 的 wall、box、ramp、style 2 platform
网格几何已能消费该冻结值；地面范围、出生区与 authored ground 策略另按值冻结，分区地面复用
mixed 的颜色分区与 V2 顶点采样算法。静态 object 另按 authored ID、投影顺序与完整 prop 值冻结，
其中 boundary wall 复用 mixed 几何算法，成为第六类网格。普通地图 `MODEL` 盒体沿用现有
V1 诊断光照与单四边形划分，且每个三角形单独持有中心光照，作为第七类网格。
SIGN 牌柱、牌面和双面字形按旧世界坐标及字体 run 生成第八类网格，保留 V2 逐三角形中心光照。
style 1、2、6、9、12 的 legacy 方块人/圆柱人展示模型共用形体目录，生成第九类网格；
style 3–5 特殊感染体按旧 rig 静态姿态、面光照和世界变换生成第十类网格。
style 7–8、10–11、13–14 的六种导入感染体展示模型复用旧 idle rig 姿态、CPU skinning、材质及逐三角形冻结 V1 光照，生成第十一类网格。
正常帧审计 near 0 的 83 条 world 项中，
60 条进入地图网格或地面分区，15 条 LABEL 暂缓、4 条为透明项；另有 134 条 object 投影中的 21 条
boundary wall 进入 Scene。十一类网格已按 world/map generation、V2 light bake
generation、完整冻结绘制值、地面、V1 诊断光照场与 object 输入进入独立 Scene world registry；任一内容变化触发重建，
逐帧 frame ID 变化不重建；旧代 pinned 资源在帧退休后释放。
独立 Scene 原生入口已在三件套提交之后，用真实渲染地图 fixture 的 11 条 world 项生成
6 个模型、28 个 GPU cache draw，并在离屏 Scene WORLD color/depth 中验证覆盖；同代 cache hit 与
已 pin 旧代在退休后释放也已验证。该诊断不进入正常帧，
地图 pin、材质检查、GPU cache 准备和 draw 编码已抽为可由正常 Scene 执行器调用的
`rf_gpu_scene_world_gpu_prepare`；
正常帧审计现已在 mixed present 后用同帧冻结 handle、camera 和独立 graphics/cache owner
提交离屏 Scene WORLD，map-wall 镜头验证了有效深度覆盖。Campaign near 0 的 134 条
object 投影中，21 条 boundary wall 进入生成网格；其余 113 条 RMESH 实例中 98 条被相同的模型
AABB 规则剔除，可见的 15 条产生 57 个 draw，数值/材质/透明暂缓均为零。23 个不同 RMESH 资产
在 Scene registry 复用；离屏 WORLD 合计 979 个 draw、524,824 个有效像素，次帧 979 次 cache hit。
此路径有显式 readback，不作为正常呈现或性能收益证据。正常帧审计还从真实 session 冻结八名正式模块化队员各自的 finalized body palette、被动装备、AK 武器变换和 V2 actor 光照值，复用 fixture 的
GPU skinning、资源 pin/backing 与 draw preflight，在同一 Scene WORLD color/depth 提交 120 个角色 draw。
Campaign near 0 总计 1099 draw，其中地图与静态实例为 979 draw；`actor-rifleman 0`、`actor-standard 0` 和 `actor-assault 0` 镜头可检查
角色、装备、武器与地图遮挡。旧镜头胸口 RGB 差异来自缺少 CHEST 装备；补入后目标像素与 mixed 一致。
武器的 RMESH 原始坐标按冻结的 geometry scale 变换，不复用被动装备的 position-scale 换算。
正常呈现还未选择 Scene WORLD draw；非正式角色、敌人和其他动态 WORLD 内容也未覆盖。完整角色画面合同仍需验证。
角色 pose 与 Scene GPU 打包按角色配方选择 body、装备资源、socket 和衣裤颜色；八种模块化角色配方通过逻辑回归，原生生命周期在第 101 帧由 rifleman 切换到 Medic 后仍完成绘制与退休。session reset 现在为八名正式队员分别登记身份，冻结时分别推进动作时钟，并按 source slot 顺序提取 pose 与 GPU draw；两组定向镜头的六个无遮挡角色像素与 mixed RGB 一致。
光照 bake 代际已进入资源复用键并通过逻辑与原生回归；这仍不能作为阶段 2 退出证据。
来源接线见[Runtime Map 来源现场](../archive/gpu-scene-world-source-20260924.md)；值帧、网格提取及
normal-native 审计见[地图渲染值现场](../archive/gpu-scene-world-render-values-20260924.md)。
Scene world handle、复用和 pinned 退休见[地图资源代际现场](../archive/gpu-scene-world-registry-20260924.md)。
RTX 3050 GPU cache、离屏 WORLD 与同步验证见[真实地图 GPU 现场](../archive/gpu-scene-world-gpu-cache-20260924.md)。
正常帧审计的同帧 draw、缓存复用和 validation/sync 见[正常帧地图 Scene 诊断现场](../archive/gpu-scene-normal-world-probe-20260924.md)。
光照代际回归见[地图光照代际现场](../archive/gpu-scene-world-light-generation-20260924.md)。
冻结地面来源、共用分区算法及 RTX 3050 同步验证见[分区地面现场](../archive/gpu-scene-floor-world-20260924.md)。
静态 object 值帧、boundary wall 网格及 RTX 3050 同步验证见[边界墙现场](../archive/gpu-scene-boundary-world-20260924.md)。
静态 RMESH 资产/实例、可见性及 RTX 3050 同步验证见[RMESH 现场](../archive/gpu-scene-static-rmesh-20260924.md)。
普通地图 `MODEL` 盒体、冻结 V1 诊断光照及 RTX 3050 同步验证见[模型盒体现场](../archive/gpu-scene-model-box-world-20260924.md)。
SIGN 牌体、双面文字、fixture 画面核对及 RTX 3050 同步验证见[SIGN 现场](../archive/gpu-scene-sign-world-20260924.md)。
legacy 展示形体、顶点光照修正、可见画面核对及 RTX 3050 同步验证见[展示模型现场](../archive/gpu-scene-legacy-display-world-20260924.md)。
特殊感染体静态 rig 展示、可见画面核对及 RTX 3050 同步验证见[特殊模型现场](../archive/gpu-scene-special-display-world-20260924.md)。
六种导入感染体静态展示、Scene 像素及缓存复用见[导入感染体展示现场](../archive/gpu-scene-infected-display-world-20260924.md)。
正常 session 角色 body/HEAD、冻结光照及 RTX 3050 同步验证见[正常角色现场](../archive/gpu-scene-normal-actor-world-20260924.md)。
被动装备、AK 武器坐标与正常帧审计回归见[角色装备现场](../archive/gpu-scene-normal-actor-gear-weapon-20260924.md)。
模块化角色配方、Medic 原生切换与单 actor 限制见[角色配方现场](../archive/gpu-scene-modular-recipe-20260924.md)。
两支正式小队的八名 actor 来源、逐人 pose、Scene 同帧提交与像素对照见[正式小队现场](../archive/gpu-scene-formal-squads-world-20260924.md)。

阶段 0/1 建立预算表：fixed tick 更新、snapshot、pose/IK/socket、extraction、上传/提交、GPU pass、
等待/present 分开记录；注明计时边界、frame ID、旧工作是否消失或保留，以及尚缺的计数器。
near 60 从既有 42.092 ms 到 16.67 ms 所需约 2.5 倍整体加速只是目标差距，不直接减去 bridge 时间。
先用定向样本检查预算假设；正式收益仍只接受完整帧低扰动 A/B。

### 阶段 0：合同、基线与迁移接口

第一步的[现行画面覆盖矩阵](gpu-scene-coverage.md)记录源码可达内容及未确认的运行证据。
第二步的[迁移接口合同](gpu-scene-interface.md)定义 snapshot、scene、稳定身份、顺序与所有权；
尚待核对来源 ID 和实际设备能力。
第三步的[迁移基线状态](gpu-scene-baseline.md)记录已有 Windows native 结构审计和阶段退出时需补的冻结证据。
第四步的[画面差异合同](gpu-scene-visual-contract.md)列出精确项、容差统计和固定审阅集合；
阈值与新基线仍待正式候选验收前审批，隔离原型可先提供原始证据。

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
退休为唯一可回收依据。首个端到端交付物固定为静态地图、一个蒙皮角色及一个 rigid 附件，使用独立
fixture 入口贯通 native present；它不构成完整正常帧。

先核对并复用现有 registry/cache、GPU skinning、slot backing 和 Windows presenter；列出复用接口、
所有者和缺口。Scene 解析、pass 编排和资源引用由新入口拥有，不将 mixed frame 或 RasterCmd 作为 Scene 主格式。
新 upload/skinning/draw 依赖链首次运行即在主设备做定向 validation/sync。
缺少 layer 时记录未完成门禁，不宣称通过。

1B 复用与缺口（源码核对 2026-09-23）：

| 设施 | 可复用入口 | Scene 必须补齐 |
| --- | --- | --- |
| 资源身份与 pin | `rasterfall_render_resources.h` 的 frame/pin/submitted/complete | 三件套已接入独立 registry；完整 Scene 表待扩展 |
| 不可变 mesh/texture | `rf_gpu_resource_cache_prepare/bind/resource` | preflight 先 pin/prepare，执行只 lookup；保留 generation 检查 |
| 蒙皮 | `rf_gpu_graphics.h` 的 skinned create/update/bind | 三件套已消费 finalized palette 与法线策略，输出归独立 slot；更多角色仍待接入 |
| slot | mixed executor 的已完成 slot 容量复用模式 | fixture 已有单个独立 slot，CPU pack/upload backing 已在退休后复用；多帧流水仍待实现 |
| graphics 提交 | 底层 graphics 资源与编码逻辑 | `rf_gpu_graphics_scene_present/retire` 已独立持有 command/fence，不借用 Raster |
| native present | Vulkan backend 内部 swapchain、审计和退休逻辑 | Scene color 直接 blit 到唯一 swapchain；RTX 3050 validation/sync、专项故障、退休已验证 |

资源缓存不能替代材质支持判定；每项新材质仍须全帧 preflight。不能仅包装现有 Raster present 接口，
就宣称 graphics-only Scene 已贯通；首个最小场景须证明没有 RasterCmd 回放及深度 bridge。

退出条件：同一 frozen 输入可重复生成等价实例/层序；world-cycle、resize、资源增长/退休和故障边界
可审计；没有正常热路径资源重建或 queue-idle。运行最近的逻辑、资源与离屏验证。

### 阶段 2：统一 WORLD 不透明路径

将地图、ground、static RMESH、普通与特殊敌人 body、队友 body、opaque gear/weapon 作为一个
graphics 深度域提交。pose/IK/socket 仍由 CPU 展示层拥有，GPU skinning 消费已冻结 palette；
rigid 附件消费相同 finalized placement。保持保守 near clipping、lighting、alpha/depth-write 和资源 pin。
允许按材质/mesh 批处理不透明实例，但必须证明不会改变可见顺序和材质结果。

退出条件：从固定 near/mid/thin-far、30/60 敌人和 Campaign 输入提取的离屏 WORLD 不透明内容
不再触发 Raster↔Draw depth bridge；通过画面合同、CPU/reference differential、角色 vertex diff 和固定 capture。
固定离屏 WORLD fixture 报告 GPU pass、CPU extraction、上传、draw/instance 与重复采样范围；
这些结果用于调整组织方式，不用于产品 FPS 结论。其余层未接齐时，不以缺内容的帧比较 whole-loop，
也不在帧内接回 mixed renderer。首次产品场景五轮 A/B 移到阶段 3。

### 阶段 3：完整 GPU 正常帧

把有序透明、effects、VIEWMODEL 独立 depth/coverage、screen OVERLAY 与 native present 接入
同一帧依赖图；POST 只保留当前正常画面所需的语义边界。特殊效果按阶段 0/1 已确定的 GPU 表示实现；
仍有缺口则明确阻断，不能静默掉帧、隐藏内容或借 CPU framebuffer copy 完成 present。

退出条件：四个固定场景和受影响 capture 在新路径完整运行；required native、零 fallback/readback/
CPU framebuffer copy、零 invalid transition、零热路径 queue-idle；Quick/Full、resize、world-cycle、
fault 和 10,000 帧 soak 按新生命周期覆盖。首次执行完整新路径 RTX 3050 五轮 A/B，保留旧路径独立 executable。

### 阶段 4：CPU producer 与可见性

在完整新路径上按 near 60 的独立阶段计时，依次评估 presentation snapshot 生成、保守
visibility/LOD、静态实例模板、pose/socket/gear cache、重复扫描与分配。每次只改一个所有权边界，
LOD 必须以冻结的距离/投影规则和视觉合同证明可见内容符合要求。线程取舍依据关键路径、可并行比例、
调度/同步成本和尾延迟，不设固定 4 ms 门槛；worker 不能改变提交序列、玩法真值或资源 pin 生命周期。

退出条件：固定 workload 下 CPU extraction/pack 的 P95/P99 可重复改善，画面与 gameplay 状态不变；
RTX 3050 低扰动五轮 whole-loop 的变化和范围足以支持保留该阶段结果。

### 阶段 5：性能与最终签收

RTX 3050 按[性能标准](../reference/gpu-performance-standards.md)完成 near 0/30/60、Campaign 的
median/P95/P99 阶段或最终门槛；保留 GPU pass 和 CPU producer 的可归因数据，不能把分位数相加。
若目标未达，先定位新架构中最大的完整帧瓶颈，再决定单独的后续计划，不退回逐个 bridge 补丁链。

在 RTX 3050 上用同一候选 package 完成 required-native、适用 Full、
resize/world-cycle/thin-far 和零 fallback/readback/copy；在装有
`VK_LAYER_KHRONOS_validation` 的环境完成 validation/sync，并证明 layer 实际加载与同步验证启用。
受影响的 fault 和 soak 结果一并保留。缺少设备或 layer 时只标记该门禁未完成，不宣称最终签收。

## 阶段门禁与决策规则

- **结构门禁：** 每个阶段先证明目标层序、target 所有权、资源 generation、失败语义和实际 bridge/run
  变化；禁止用 RasterCmd、draw call 或单次 audit 墙钟推断 FPS。
- **画面门禁：** 输入、语义与统计方法在阶段 0 冻结；容差在正式候选验收前批准。
  失败时修正新路径或公开修订合同，不把不符合合同的结果写成性能收益。
- **性能门禁：** 阶段 0 退出时冻结旧路径基线；此后只在阶段 3、4 的完整候选及最终签收跑正式低扰动五轮 A/B；同 package、
  独立 executable、交流电、固定电源方案、固定 tick/workload，保存各轮和中位轮。
- **回滚门禁：** 新路径用显式启动选择隔离，整帧选择旧或新 renderer。任何 GPU required 执行失败
  非零退出；不能在同一帧做局部 CPU replay。撤销的阶段结果归档，不长期维护两个“当前”设计。
- **文档门禁：** 所有权或数据流实际落地时，同一改动更新 architecture 与
  [`../README.md`](../README.md) 任务路由；命令变化更新 guide；旧测量和失败候选归档。

## 范围边界与完成定义

首版不引入新 PBR 材质、OIT、全局 GPU-driven pipeline、光追、跨平台 presenter 重构或新的玩法字段。
若 shader/material 覆盖矩阵证明其中一项不可避免，先在本计划记录阻断原因、成本与替代方案，再决策。

两项结果独立记录，归因不能代替达标：

| 结果 | 完成条件 |
| --- | --- |
| 架构迁移 | 完整 Scene 正常帧、零 bridge、画面合同、Windows native 生命周期与 validation/sync 通过 |
| RTX 3050 性能 | 固定场景达到性能标准的最终 median/P95/P99 门槛 |

整体完成要求两项均通过。架构完成而性能未达时保留未完成状态与归因；若转入后续计划，明确以
“被替代、性能未达标”归档，不能记为性能完成。活动入口始终只指向一个计划。
