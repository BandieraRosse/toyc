# 角色表现

> 状态：当前
> 所有者：Rasterfall character/enemy presentation adapters
> 最近核对：2026-09-23

本文描述玩法 actor 到可见角色的渲染侧适配。模型格式、bind pose、动画求值和 attachment 格式分别由
[资源与动画](assets-animation.md)、[动画架构](animation-architecture.md)和
[角色资产合同](reference/character-assets.md)拥有；固定截图流程见 [视觉验收](guides/visual-validation.md)。

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

具体家族比例、特殊敌人适配、预算和扩展流程由 [Enemy Visual](enemy-visuals.md) 拥有。死亡飞起、渐隐、
fragment/dust 和 knockback trail 是 presentation-only；slot 清空后仍可短时存活，但不得替代 enemy 真值。

## Rigid attachment 与 static prop

rigid attachment 的求值链为 `actor/world × finalized socket × mount correction × authored local`。完整
rotation/translation/uniform scale 同时作用于顶点，rotation 作用于法线；attachment bounds 不参与角色
缩放。renderer 只查询 finalized socket 并提交共享 resource，不修改 host pose。

static prop 使用 `RMESH local → profile scale → instance scale → yaw → world translation`。地图实例只保存
asset identity 与 transform；registry/cache 共享模型，碰撞由 map profile 独立生成。

## 开发 fixture

Character Test Strip、action debug station、MODEL_DISPLAY 和 lineup 都是 renderer-only fixture，不进入
actor、AI、碰撞或网络。正常 world 是否显示这些内容由 World Content policy 控制；Campaign fixture
不得泄漏到 Outpost。诊断 CLI 可以使用隔离 fixture，但必须复用正常 renderer、pose 和 attachment 路径。

修改角色可见结果时，至少覆盖相关的 bind/action、多视角、near/mid/far、edge-entry/near-crossing 和
instance isolation；具体命令与输出由视觉验收指南维护。
