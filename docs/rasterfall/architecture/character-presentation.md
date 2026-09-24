# 角色表现

> 状态：当前
> 所有者：Rasterfall character/enemy presentation adapters
> 最近核对：2026-09-23

本文描述玩法 actor 到可见角色的渲染侧适配。模型格式、bind pose、动画求值和 attachment 格式分别由
[动画架构](animation-architecture.md)和
[角色资产合同](../reference/character-assets.md)拥有；固定截图流程见 [视觉验收](../guides/visual-validation.md)。

## 所有权边界

Game/session 拥有 actor 身份、动作语义和权威位置。presentation adapter 将稳定 character ID 解析为
profile/recipe，并向 renderer 提交 finalized pose、transform、材质 override 和附件 placement。renderer
不得从 actor 读取资源路径、gear list 或临时资产参数，也不得从骨骼姿态反推动作语义。

每个 actor 持有独立 `rasterfall_model_instance`；不可变 body、gear、weapon resource 可以共享。
单次 shirt/pants 或 scene-light override 只影响本次 submission，不修改 resource material table。
资产坐标、profile basis、bind pose、动画求值和末端渲染补偿保持分层。

## Modular teammate

正式 RF 小队按 character ID → profile → modular recipe 选择共享 V2 body 与 rigid gear。正常 world 中每个
actor 先完成 pose、IK、bounds 和 body Draw 冻结，再按相同 actor 顺序提交 opaque gear/weapon RasterCmd；
延迟记录保存 transform、weapon placement 和 scene-light override。该两阶段编排只减少 mixed bridge，
不改变动画、附件或 actor 顺序，也不跨越 transparent/effects/viewmodel/overlay。

动作适配固定为 lower/upper/additive layers：IDLE/MOVE 保持 lower idle/walk，FIRE 只替换 upper 为
rifle fire，aim 只有在 gameplay 提供明确 semantic 后才能接入。RFANIM authored 时间独立于 gameplay
回卷值，instance 累积 presentation time 以保持完整周期和短时 upper action 下的 lower phase。

weapon 从 finalized `WEAPON_R` 对齐 authored `PRIMARY_GRIP`；左手在绘制前用同一武器的 `FOREGRIP`
执行 attachment IK。passive gear 只读取 finalized HEAD/CHEST/BACK/HIP 等 socket。所有修改只作用于
当前 mutable instance，不回写共享 resource。

RFCHAR body、rigid gear、socket 与 weapon 统一采用 profile 定义的 `+Z` forward，不在枪械 helper
额外加 180° 修正。失败时可以回退既有 procedural actor，但不能产生另一套权威状态。

## Procedural 与职业表现

低模 AI 入口只消费调用者提供的 actor transform、ground/airborne height、当前武器、downed 和动画采样；
它不查询 actor 数组、地面或 HUD，也不拥有整人裁剪。职业身份由 character profile 的稳定
`profession_id` 提供，与骨骼模型选择字段独立。

Gunsmith、Logistics、Medic、Guard 的颜色和附件由 presentation-only profile 组合；downed/death/revive
沿用既有整体变换，不新增玩法或网络字段。portrait 可以复用同一 renderer，但自行拥有 camera 与展示状态。

## 敌人表现

普通感染体的 BLOCK_INFECTED/HUMANOID_INFECTED recipe，以及 Smoker/Charger/Tank 的 rigid profile、
truth adapter、pose 和 generic renderer 均属于 renderer presentation。玩法只提供 enemy 类型、状态、
命中与攻击真值；尺寸、轮廓、固定关键帧、renderer scratch pose 和资源选择不进入 snapshot。

具体家族比例、特殊敌人适配、预算和扩展流程由 [Enemy Visual](../reference/enemy-visuals.md) 拥有。死亡飞起、渐隐、
fragment/dust 和 knockback trail 是 presentation-only；slot 清空后仍可短时存活，但不得替代 enemy 真值。

正常帧审计通过 `rf_gpu_scene_enemy_begin` 开启单帧收集；mixed 的特感 producer 在求值后按值保存
Smoker、Charger、Tank 的身体 pose、受击后的世界位置、朝向、lift、反馈颜色与实际光照模式。
`rf_gpu_scene_enemy_freeze` 结束收集并输出只读值帧。每帧 begin 都清空旧内容，即使本帧没有 WORLD；
提取不再推进 observer 或读取时钟。source slot 只作帧内顺序，不作为跨帧生命周期身份。
`rf_gpu_scene_enemy_triangles` 与旧 renderer 共用刚性几何枚举，只有冻结值进入 Scene 资源预备。
不支持的身体和远距剔除分别计数；每个敌人项同时冻结 blob shadow、舌头端点与束缚圈输入。
这条审计链仍依赖 mixed 完成姿态求值；独立正常 Scene 来源及其生命周期身份尚未接入。

普通感染体在同一值帧冻结六种 recipe ID、已采样 swing、bind 标志、变换与反馈颜色；
Scene 复用不可变资源，以独立 scratch instance 重建姿态和 CPU skinning 几何，不读取或推进旧 motion cache，
也不复用旧 producer 的 mutable pose。颜色保留原路径的 form-light 处理；V2 使用逐顶点世界光照，
非 V2 路径保留材质亮度范围。普通感染体、特感和显式 LEGACY 身体共用 Scene WORLD 深度。死亡缩放、旋转中心和旋转值在来源处冻结；不透明死亡身体参与 WORLD，渐隐身体单列 transparent 计数，等待有序透明层。
该接线仍是同步离屏审计，动态 GPU 资源逐帧重建，不能作为正常 producer 或性能收益证据。

## 同帧程序角色与网络诊断

程序角色在 `rasterfall_render_procedural_humanoid` 的来源边界冻结已解析的位置、朝向、高度、武器、
职业、动画时间和四种外观颜色，同时冻结光照与双面标志；不复制 profile 的资源指针。
`rf_gpu_scene_procedural_triangles` 用这些值复用身体、职业装备、武器和死亡/复活的几何枚举。
串行枚举回调位于世界坐标投影之前，提取时不生成 RasterCmd，也不访问网络输入包或推进展示时钟。
回调失败会使整个提取失败；actor 变换和全局光照开关在返回前恢复。

host/guest 的网络 adapter 先解析插值 camera、actor 状态及高度，再经过同一程序角色冻结入口。
客户端模块化 AI 和非固定 roster 的模块化角色从 finalized instance 冻结 palette、装备及武器 placement，
与独立 local pose 提取共用 `scene_pose_from_instance`。补充项使用独立的帧内诊断命名空间，generation
只标识该审计帧，不声称提供跨帧 actor 生命周期。普通正式队员仍使用 session epoch 的独立提取链；
downed 队员由程序表现负责，不再要求不支持 downed 的模块化提取器处理它。

这些输入进入同一个同步离屏 WORLD；诊断动态资源仍按审计帧重建。它们不能直接充当正常 Scene
producer 或异步多帧资源方案。复现与固定输入见 [Scene fixture](../guides/gpu-scene-fixture.md)。

## Rigid attachment 与 static prop

rigid attachment 的求值链为 `actor/world × finalized socket × mount correction × authored local`。完整
rotation/translation/uniform scale 同时作用于顶点，rotation 作用于法线；attachment bounds 不参与角色
缩放。renderer 只查询 finalized socket 并提交共享 resource，不修改 host pose。

static prop 使用 `RMESH local → profile scale → instance scale → yaw → world translation`。地图实例只保存
asset identity 与 transform；registry/cache 共享模型，碰撞由 map profile 独立生成。

## 开发 fixture

GPU Scene 的模块化队员 pose 求值由 `render/rf_gpu_scene_pose.inc` 拥有，公开入口为
`rf_gpu_scene_pose_extract_at`（单 actor fixture 保留 `rf_gpu_scene_pose_extract`）。它只读 `rf_gpu_scene_local_frame` 的指定 actor sidecar，复用共享 body/gear resource、
RFANIM composition、左手 attachment IK 和 finalized socket 求值，输出值类型的 body palette、
body-to-world、finalized pose 的 bind-normal 策略、被动 rigid gear 及主动武器的 model-to-world。武器变换包含 authored centering、
basis 和 PRIMARY_GRIP 对齐；consumer 不得再次补偿。资源标识是 character/weapon catalog ID，
尚非 GPU handle；consumer 必须解析、pin 后才能提交。独立 Scene fixture 的解析及 slot 所有权见
[GPU 渲染架构](gpu-rendering-architecture.md)。passive gear 的变换仍需要资源 `position_scale` 的 RFU
换算；主动武器输出已包含 authored geometry scale，不得重复使用 passive gear 的换算。

local source 为每名 actor 在成功 freeze 时分别累计 lower-body 展示时间，并把该时间复制进各自 sidecar；MOVE 回卷只累加
delta，FIRE 保留 lower phase。来源销毁、world 切换及角色配置变化重置时间，slot 移动保留同一来源
时间。提取不推进时钟，不读 session/game，不使用旧 renderer 的 slot pose cache；当前为隔离诊断
每次创建独立 instance，以确保历史输入可重复求值，尚非正常帧分配策略。输出成功后才整体替换，
空 actor 产生空 payload，unsupported/downed 或缺失资源显式失败，不做 procedural fallback。
定向资源验证入口见 [GPU Scene fixture](../guides/gpu-scene-fixture.md)。

Character Test Strip、action debug station、MODEL_DISPLAY 和 lineup 都是 renderer-only fixture，不进入
actor、AI、碰撞或网络。正常 world 是否显示这些内容由 World Content policy 控制；Campaign fixture
不得泄漏到 Outpost。诊断 CLI 可以使用隔离 fixture，但必须复用正常 renderer、pose 和 attachment 路径。

相关的固定截图矩阵与输出由[视觉验收指南](../guides/visual-validation.md)维护。
