# 角色表现

> 状态：当前
> 所有者：Rasterfall character/enemy presentation adapters
> 最近核对：2026-10-03

实验角色也属于正式 actor/enemy 池。`animation_control` 为普通 AI、指定动画、冻结采样三种状态，
指定或冻结时跳过 AI 控制，保留身体碰撞；姿态与持枪输入由同一表现 adapter 消费实例时钟。
RF-C01 通过稳定角色 ID 接入正常 Scene actor 提取和既有身体采样器，不另造展示用身体。
其当前可选动作为待机、行走、射击，CPU 终端不创建该 GPU 专属预设。
实验组的 ID/generation 与销毁责任见[动态实验场](../reference/experiment-labs.md)。

本文描述玩法 actor 到可见角色的渲染侧适配。模型格式、bind pose、动画求值和 attachment 格式分别由
[动画架构](animation-architecture.md)和
[角色资产合同](../reference/character-assets.md)拥有；固定截图流程见 [视觉验收](../guides/visual-validation.md)。

## 所有权边界

Block 与普通感染体通过[公共角色加速入口](gpu-rendering-architecture.md#统一角色加速)分离固定几何
和最终 palette；各自保留动作和骨架适配。特感 rigid rig 仍沿原几何生产者，不假定已经迁移。

Game/session 拥有 actor 身份、动作语义和权威位置。presentation adapter 将稳定 character ID 解析为
profile/recipe，并向 renderer 提交 finalized pose、transform、材质 override 和附件 placement。renderer
不得从 actor 读取资源路径、gear list 或临时资产参数，也不得从骨骼姿态反推动作语义。

持续维护表现状态的 actor 持有独立 `rasterfall_model_instance`；不可变 body、gear、weapon resource 可以共享。
Scene 从只读冻结值重新求值时，允许串行复用重置后的临时 instance；输出必须是独立值，不能把临时 pose 或历史交给下一个 actor。
单次 shirt/pants 或 scene-light override 只影响本次 submission，不修改 resource material table。
资产坐标、profile basis、bind pose、动画求值和末端渲染补偿保持分层。

## 展示方向与旋转保护

`rf_numeric_display.h` 只消费展示副本：先检查有限性与原始方向长度，再使用浮点归一化。
有限非零方向先按最大分量缩放，再求长度，避免极大或极小输入平方溢出/下溢。
零方向默认使用 +Z；NaN/Inf 明确失败，严格数值诊断也拒绝原始长度异常和意外零值。
异常记录保留原值、处理结果与可用实体身份，不因展示纠正而隐藏权威入口错误。

共享 `modular_rigid_facing` 从单位水平朝向构造旋转基。CPU modular、Scene frozen body、
Block、主/AUX 消费及从身体合成的武器/装备复用该规则。CPU 的 profile 轴向组合也先归一化，
避免在修正前进行可能溢出的整数乘法。`scale_milli` 保留模型合法缩放；
不对完整装备/骨骼矩阵任意归一化，不将 pitch 斜率当成二维单位方向。

展示纠正不回写 Game/session、网络输入或动画权威值，不增加玩法随机数。
Scene 继续保留原有 `±1.01` 矩阵元素校验与 palette/translation/scale 范围检查。
该阈值是最后的输入保护，不是浮点运算精度标准，也不能代替单位长度和正交性检查。

## Modular teammate 与敌方枪手

正式 RF 小队按 character ID → profile → modular recipe 选择共享 V2 body 与 rigid gear。正常 world 中每个
actor 先完成 pose、IK、bounds 和 body Draw 冻结，再按相同 actor 顺序提交 opaque gear/weapon RasterCmd；
延迟记录保存 transform、weapon placement 和 scene-light override。该两阶段编排只减少 mixed bridge，
不改变动画、附件或 actor 顺序，也不跨越 transparent/effects/viewmodel/overlay。

动作适配固定为 lower/upper/additive layers：IDLE/MOVE 保持 lower idle/walk，FIRE 只替换 upper 为
rifle fire；共享 rifle sampler 在 AK 移动时保持低位持枪，停止移动后的目标获取与 FIRE 驱动肩托瞄准。
两种姿态都跟踪射击方向，共享双臂握点与反冲。RFANIM authored 时间独立于 gameplay
回卷值，instance 累积 presentation time 以保持完整周期和短时 upper action 下的 lower phase。

真实 AK AI 静止、无目标时，由同一 rifle sampler 在肩托警戒与三种闲置持枪之间随机停留和过渡。
竖持时右手握枪、左臂放下；胸前斜持保持枪体与胸部间隙；低持放松在肋侧。目标、移动和特殊动作
优先中断待机。历史、随机数与姿态权重归 presentation，CPU cache 与 Scene sidecar 冻结同一输入，
不增加 Game/session 或网络字段。动作与中断合同见[动画求值](animation-architecture.md)。
位置插值副本同时复制权威 `combat_time_ms`；新增实验 AI、补充模块角色和 Block 的采样时钟
必须与正式 roster 一致，不能因未复制标量而停在初始低头姿态。

成功回避的上身避让与射击反冲可同时存在：lower/upper → recoil → 500ms evade → 瞄准/胸部避让/双臂 IK → 附件。
CPU modular pose cache 将该次方向、采样时间和权重纳入键，并按 combat generation 区分槽位复用。
Scene local source 用相同的只读采样器冻结这些值到逐 actor sidecar；pose extraction 只消费冻结值，
不读取玩法计时或推进历史。受控、腾空、死亡、倒地、复活中断当前事件后，即使状态提前解除也不恢复
残留侧倾。HUD 与回避粒子仍沿各自表现链消费权威结果，骨骼避让不产生额外成功命中或伤害。

`--combat-character-capture` 复用实际 modular adapter，除外观与行走射击矩阵外，输出同视角
12 帧、间隔 60ms 的 `evade-00.bmp` 至 `evade-11.bmp`。三个独立角色覆盖左右避让、行走、连续射击、
延迟触发和控制中断；每帧对比 CPU cache 与 Scene extraction 的最终 palette，资源必须保持只读。
动作逻辑另验证 lower 不变、反冲保留、末帧归零、错误 mask 拒绝、冻结重放和最终 IK。

`rasterfall_rifle_pose` 先求肩托、目标方向和双臂，weapon 再从 finalized `WEAPON_R` 对齐
authored `PRIMARY_GRIP`。物理尺寸、源中心和轴向通过统一 adapter 转换；详见
[动画求值](animation-architecture.md)与[武器模型适配](../reference/weapon-model-adapter.md)。
passive gear 只读取 finalized HEAD/CHEST/BACK/HIP 等 socket。所有修改只作用于当前 mutable
instance，不回写共享 resource；rifle 输入与 evasion 一起纳入 CPU cache 和 Scene 冻结 sidecar。

衣服外轮廓与装备按 recipe 条目组合；一个 socket 可以同时承载头盔与护目镜，或髋侧装备与大腿
外裤层。CPU cache、Scene pose payload 与 native mesh 列表统一使用 recipe 容量，每项保留自己的
resource ID 与 transform，不按 socket 合并身份。独立夹克和跨膝外裤作为 skin follower 共用身体
完成动作、握点修正后的最终 palette，不独立采样动画或求值 IK。Heavy recipe 装配两件衣物，
其他职业保留原 rigid 大腿层；头盔、护目镜与硬甲仍跟随 sockets。

skin follower 必须通过身体与衣物的骨名/顺序、父子层级、absolute rest、位置单位和 humanoid
role 映射完全一致检查。recipe 仅携带最多两件衣物的稳定 ID，pose producer 同时冻结 clothing
IDs 和 body hidden-material mask。CPU/Scene 都隐藏夹克覆盖的 shirt 或外裤覆盖的 pants，
不会修改 body resource 材质表。该区域映射只属于 canonical Humanoid 五材质版型；RF-C01 或
其他体型必须显式提供自己的兼容版型与区域映射。当前是蒙皮衣物，不包含布料物理模拟。

RFCHAR body、rigid gear、socket 与 weapon 统一采用 profile 定义的 `+Z` forward，不在枪械 helper
额外加 180° 修正。失败时可以回退既有 procedural actor，但不能产生另一套权威状态。

敌方普通/精英枪手使用稳定 `RASTERFALL_CHARACTER_GUNNER` / `GUNNER_ELITE` 身份，经同一目录解析
到共享 `rf_humanoid_v2` 身体、独立 shirt/pants 调色和 HEAD/CHEST/BACK rigid 组件。普通为红色轻胸挂、
帽檐/护目与窄背包；精英为红黑厚胸甲、全覆盖头盔/呼吸器和较宽背包。胸、背及俯视顶面都保留大块红色，
识别不依赖 HUD 或微小标记；这些视觉配置不授予护甲或技能。新增 recipe 不扩充六职业 legacy carrier 清单。

原创 V2 身体和全部职业/枪手组件的公开运行资源位于 `assets/models/characters/`；身体路径由角色目录拥有，
CPU modular、Scene pose 与 Scene native consumer 读取同一资源。私有 RF-C01 body 仍保留独立目录边界。
生成源、manifest 和真实 adapter 的固定视角/连续帧入口见
[枪手与共享身体重建](../guides/character-art-acceptance.md#战斗-v0-公开共享身体与枪手组件)。

## Procedural 与职业表现

低模 AI 入口只消费调用者提供的 actor transform、ground/airborne height、当前武器、downed 和动画采样；
它不查询 actor 数组、地面或 HUD，也不拥有整人裁剪。职业身份由 character profile 的稳定
`profession_id` 提供，与骨骼模型选择字段独立。

Gunsmith、Logistics、Medic、Guard 的颜色和附件由 presentation-only profile 组合；downed/death/revive
沿用既有整体变换，不新增玩法或网络字段。portrait 可以复用同一 renderer，但自行拥有 camera 与展示状态。

### Block 标准骨架

GPU Scene 的标准 Block 来源通过 `rf_gpu_scene_block_bind` 和 `rf_gpu_scene_block_sample`
分别输出固定几何与值类型 palette。两者只消费程序角色冻结输入；身体动作、握持与倒地复用
CPU sampler，职业装备共用 box 枚举，武器使用同一挂点和坐标适配。动态几何入口继续作为
CPU 路径和对照依据。常驻资源、AUX 借用及退休由[GPU 架构](gpu-rendering-architecture.md#程序角色常驻几何)拥有。

Block 队友与网络玩家使用公开 `rf_block.rmesh`，由 `generate_rasterfall_block.py` 生成硬边方块身体。
它复用 Humanoid V2 的完整 49 骨、rest 和八个挂点；身体分块以单骨权重保持棱角，领口使用双骨过渡。
上臂、前臂、手掌、指节、大腿、小腿和脚都由标准 RFCHAR 骨架驱动，持枪不再按手部目标拉长方块。
调色仍消费 character profile 的衣服、裤子、皮肤和头发颜色；四种源材质默认色是该资产的调色标识。

`render/rasterfall_block_character.inc` 拥有不可变公开资源与串行 scratch instance。每次求值从 resource
重置全部姿态及 IK 历史，消费冻结输入后生成蒙皮顶点；CPU 与 Scene 几何枚举共用该入口，实例不携带
跨 actor 历史，也不把可变 pose 指针放进冻结帧。现有 procedural carrier 名称保留为兼容接口，不表示
Block 仍无骨架。角色身份、职业、网络和玩法碰撞不随美术骨架改变。

下身使用 RFANIM idle/walk，上身使用共享 rifle composition 与双臂握点求解；单手竖持释放左臂，
双手握持保持固定骨长。换弹、推击、近战、投掷及受击保留为局部骨骼旋转叠加；倒地、死亡和复活
作用于身体整体展示变换。职业装备跟随最终头、胸或髋骨。五种枪械均从 finalized `WEAPON_R` 放置，
使用各自的完整双手握点、支点和物理单位 adapter；非枪械道具保留兼容尺寸和轴向，并绑定最终右手。
武器按资源路径独立懒加载，不依赖有数量上限的展示目录；没有模型的药品不发射手持几何。
Scene 单角色三角形预算包含身体、武器和职业装备，完整组合由姿态回归覆盖，防止旧地图
狙击手等非 AK 装备超过缓冲上限而使场景提交失败。

CPU 与独立 Scene 分别拥有 rifle sampler 历史；提取器只读已冻结权重，不推进随机序列。
真实角色步行相位使用只读 simulation 毫秒，台位和循环区使用所属展示时钟，避免 400ms 玩法动作
回卷截断 800ms RFANIM 行走。固定骨长、挂点、实例隔离与冻结重放由 `--gpu-scene-pose-test` 验证。

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
这条审计链仍依赖 mixed 完成姿态求值；独立预览使用下述独立来源，不经过该 draw 收集链。

普通感染体在同一值帧冻结六种 recipe ID、已采样 swing、bind 标志、变换与反馈颜色；
步态由 `rasterfall_infected_sample_motion` 单独求值：调用者传入只读历史、active、位置和
64 位微秒时间，返回下一历史与 swing；函数不读取时钟、玩法数组或 mutable pose，也不生成绘制命令。
历史归 presentation owner，在来源或 world 替换时清零；slot 不是生命周期身份。
非存活、瞬移和时间回退清除旧运动，停步保留 100 ms 展示窗口。旧 draw 入口已消费该接口，
独立 Scene owner 也消费此接口，在单次冻结后提交自己的历史，提取重放不得再次推进它。
Scene 复用不可变资源，以独立 scratch instance 重建姿态和 CPU skinning 几何，不读取或推进旧 motion cache，
也不复用旧 producer 的 mutable pose。
Scene 按 recipe、bind 模式及采样步态缓存局部 skinning 位置与法线；不可变资源可跨帧复用这些值。
世界位置和旋转法线另按顶点索引只缓存单次身体提取，重复索引复用同一结果，新身体即使姿态相同也重新变换。
世界光照、死亡变换、反馈和动作历史不进入局部姿态缓存。诊断开关可恢复逐身体蒙皮或逐角点变换。
颜色保留原路径的 form-light 处理；V2 使用逐顶点世界光照，
非 V2 路径保留材质亮度范围。普通感染体、特感和显式 LEGACY 身体共用 Scene WORLD 深度。死亡缩放、旋转中心和旋转值在来源处冻结；不透明死亡身体参与 WORLD，渐隐身体单列 transparent 计数，等待有序透明层。
旧接线仍是同步离屏审计，独立来源则可 native present；动态 GPU 资源在同步退休后按容量复用，
每帧重新提取几何并更新活动顶点，生命周期合同见 GPU 架构。

### 独立动态来源

`render/rf_gpu_scene_enemy_source.inc` 拥有独立敌人展示历史，在显式帧时间求普通感染体步态和
特感姿态，并冻结反馈位置、死亡旋转/缩放、阴影及舌头端点。world generation 或外观策略变化
清空该历史；同一帧禁止重复采样。输入只读，不调用旧 draw 入口，不共享旧 observer 或 mutable pose。
敌人 source slot 仍只表示帧内顺序，当前没有跨帧异步敌人资源身份；渐隐身体仅标记 transparent，
待有序透明层消费。

`render/rf_gpu_scene_actor_source.inc` 直接遍历只读 AI 和网络展示值，冻结程序身体及 downed 状态。
正常正式 roster 继续由 session local source 处理，adapter 按 actor ID 排除重复身体；client 的
模块化 AI 和补充模块化 AI 通过独立 local 值帧及 pose 提取器求值。补充项只使用帧内身份，
保留的 lower-body 时间是展示历史，不能作为 actor 生命周期证据。网络玩家复用只读插值 camera
及高度求值，host 排除断线项，client 排除本地玩家，均不打开 socket 或改写网络/玩法状态。
各 adapter 与地图、旗帜等共用冻结帧 ID，GPU 提取只消费冻结值；未覆盖展示站/编辑器专用角色。

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

旧 draw 捕获输入进入同一个同步离屏 WORLD；诊断动态资源在退休后按容量复用。独立预览由上述直接来源
替代捕获链，两者都不是异步多帧资源方案。复现与固定输入见 [Scene fixture](../guides/gpu-scene-fixture.md)。

## Rigid attachment 与 static prop

rigid attachment 的求值链为 `actor/world × finalized socket × mount correction × authored local`。完整
rotation/translation/uniform scale 同时作用于顶点，rotation 作用于法线；attachment bounds 不参与角色
缩放。renderer 只查询 finalized socket 并提交共享 resource，不修改 host pose。

static prop 使用 `RMESH local → profile scale → instance scale → yaw → world translation`。地图实例只保存
asset identity 与 transform；registry/cache 共享模型，碰撞由 map profile 独立生成。

## 开发 fixture

`RF_MODEL_LAB` 的目录 body 预览由 `rf_outpost_actor_showcase.inc` 拥有可见性与独立时钟，
`rf_gpu_scene_actor_source.inc` 按动作台位冻结角色 palette；`rf_gpu_scene_pose_body` 按 body resource ID
解析不可变 RFCHAR，并在串行临时 instance 中重置、采样已有 RFANIM。预览没有 gameplay character ID、
玩法 actor 或网络状态，不修改原材质颜色。隐藏和退出释放预览 CPU resource，GPU 资源沿 Scene
owner 的既有退休规则管理。缺失资源显示安装错误，重新关闭/开启后重试，不用其他角色替代。
`rf_gpu_scene_pose_body_action` 另外提供站立持枪、瞄准、低位移动持枪与行走射击，复用正式动作 composition、
共享肩托瞄准、双臂握点 IK 与武器 placement；`rf_gpu_scene_pose_body` 保留原站立/步行兼容入口。
目录预览与游戏实例使用同一握持求值，不额外补偿枪械位置或手腕方向。目录预览仍不代表换弹、
近战、倒地等全部玩法动作已经适配。
持枪闲置台位由展示 owner 用独立历史调用正式 `rasterfall_rifle_sample`，再显式传入冻结的 rifle 输入；
按展示时钟随机停留和切换，pose 提取器不保存历史。指定 `RF_GPU_CHARACTER_IDLE_POSE` 时才固定单一姿态。

`RIFLE CYCLE` 是 RF 模型区东侧的三模型循环展示，控制与时钟仍归统一实验区 runtime。
`render/rf_outpost_rifle_cycle.inc` 用显式时间生成三条同步路线、朝向、仰俯/左右角度、瞄准/低位权重和反冲；
采样不依赖前一帧，不生成玩法射击、目标或伤害。停止、暂停和性能独占沿用[实验区合同](../reference/experiment-labs.md)。
Block 使用程序角色冻结的共享 rifle 输入和下身时间；Humanoid 使用现有 rifleman recipe 与只读 game 副本；
RF-C01 使用 `rf_gpu_scene_pose_body_sample`，显式传入完整朝向、上下身时间和 rifle 输入，不读取诊断环境变量。
两种骨骼角色共用正式 composition、握点求解和武器 placement。提取与重放不推进循环时钟；移动射击保留下身步态。
枪口位置从 finalized weapon socket 求值并随 pose 冻结，闪光由 Scene EFFECTS 消费，不写入玩法特效事件或动态灯。
`WEAPON CYCLE 3 X 5` 在原循环区东侧，三排角色与五列枪械显式采样相同的循环时钟。
`render/rf_outpost_weapon_cycle.inc` 生成待机、角度、移动、反冲和武器 ID；三个目录身体均调用
`rf_gpu_scene_pose_body_sample`，复用正式 RFANIM、双臂 IK、武器 adapter 和 palette 冻结。
武器射击间隔只读 `toy_game_weapon_info`，不生成玩法 actor、射线或伤害；正常角色也消费相同握点配置。

目录资源由 RF 模型区与两个循环区共用，最后一个使用区关闭才释放 CPU resource；缺失 RF-C01 时另外两种仍可展示。

共享 Humanoid 和 RF-C01 的持枪指节来自 action composition，可换衣物继续使用同一最终骨架；
肘部平面同时考虑目标掌骨朝向和身体外侧方向，胸前斜持及单手举枪增加外展，极端仰俯减少外展。
姿态正确性包含腕部与前臂的关系及指节包握，不能仅由挂点误差签收。
RF-C01 的单表面眼球由私有 eyes 部件与不透明基础色图拥有，renderer 不增加眼部深度偏移或材质排序特例；
诊断方法见[角色保真](../guides/character-fidelity.md)。

前哨站南侧两个角色实验场使用只读展示状态。各区按钮由单人 runtime 消费 E 输入并独立控制可见性；`render/rf_outpost_showcase.inc` 拥有六类普通感染体的 12 个静止/原地移动台位，以及三个 Humanoid 往返步行实例、展示时钟和运动历史。台位与步行实例都通过实机 `rasterfall_infected_sample_motion` 采样；原地移动台按类型速度中值产生虚拟位移，步行实例按相同速度实际沿线段移动并在端点折返。隐藏、重新显示、时间回退或超过一秒的采样间隔重置历史；同一时间重复提交不推进动画。

CPU 入口显式接收已采样结果，独立 Scene 来源冻结同一结果；两者复用实机资源与 `enemy_visual_apply_pose`，不通过 capture 全局覆盖量切换实验场姿态。GPU 几何提取只读冻结值，不推进展示时钟。台位不占玩法敌人槽，不进入 gameplay snapshot 或网络；Scene 值帧与动态资源池通过统一容量声明预留额外 12 个 source slot，静态断言核对台位数。场地、道路、标线和终端属于 `.map`，台位属于展示模块。

东侧两个队友实验区用独立开关和展示描述提供 Block 全动作固定台位、Humanoid 站立/行走/射击固定台位，
并在后两排展示其余职业与敌方枪手的装备组合；所有 Humanoid 台位只采样已有动作。
另有两种外观乘三级 AI 的六条往返线。CPU 在角色展示阶段消费临时 actor 值；独立 Scene 把程序角色冻结为
procedural item，并在只读的 game 副本上用现有 local pose 提取模块角色。真实 `game` 不增加展示 actor。
所有身体都消费展示 actor 经实验区转换后的世界坐标；程序 Block 不得直接提交台位局部坐标。
展示身份不占 gameplay 槽位。不同区域的可见性和时钟由各自的 presentation owner 管理。
Scene 在冻结后为本次实际插入展示副本的槽位写入同一展示时钟，不从已取模的玩法动作时间推算
完整步态周期；慢帧、首次晚采样与后端切换仍与 CPU 使用相同相位。未标记的真实角色保留自身时钟。
动作台位使用 AK；往返线保留对应等级装备。持枪历史按台位独立保存，CPU 与 Scene 消费同一采样值；
静止台使用实机随机闲置逻辑，关闭或时间回退重置历史。Scene 只覆盖明确标记的展示槽位，
不借用其他台位的历史，也不让游戏时钟替代展示时钟。

GPU Scene 的模块化队员 pose 求值由 `render/rf_gpu_scene_pose.inc` 拥有，公开入口为
`rf_gpu_scene_pose_extract_all`（整批）、`rf_gpu_scene_pose_extract_at`（单 actor；fixture 保留 `rf_gpu_scene_pose_extract`）。它只读 `rf_gpu_scene_local_frame` 的指定 actor sidecar，复用共享 body/gear resource、
RFANIM composition、共享 rifle pose 和 finalized socket 求值，输出值类型的 body palette、
body-to-world、finalized pose 的 bind-normal 策略、被动 rigid gear 及主动武器的 model-to-world。武器变换包含 authored centering、
basis 和 PRIMARY_GRIP 对齐；consumer 不得再次补偿。资源标识是 character/weapon catalog ID，
尚非 GPU handle；consumer 必须解析、pin 后才能提交。独立 Scene fixture 的解析及 slot 所有权见
[GPU 渲染架构](gpu-rendering-architecture.md)。passive gear 的变换仍需要资源 `position_scale` 的 RFU
换算；主动武器输出已包含 authored geometry scale，不得重复使用 passive gear 的换算。

local source 为每名 actor 在成功 freeze 时分别累计 lower-body 展示时间，并把该时间复制进各自 sidecar；MOVE 回卷只累加
delta，FIRE 保留 lower phase。来源销毁、world 切换及角色配置变化重置时间，slot 移动保留同一来源
时间。提取不推进时钟，不读 session/game，不使用旧 renderer 的 slot pose cache；串行复用重置后的
临时 instance，以完整冻结值确保历史输入可重复求值。单 actor 输出成功后才整体替换，空 actor 产生
空 payload，unsupported/downed 或缺失资源显式失败，不做 procedural fallback。正常 runtime 使用整批
入口，只验证一次 roster，跳过 downed；批次失败不发布 count，调用者丢弃整批。
定向资源验证入口见 [GPU Scene fixture](../guides/gpu-scene-fixture.md)。

Character Test Strip、action debug station、MODEL_DISPLAY 和 lineup 都是 renderer-only fixture，不进入
actor、AI、碰撞或网络。正常 world 是否显示这些内容由 World Content policy 控制；Campaign fixture
不得泄漏到 Outpost。诊断 CLI 可以使用隔离 fixture，但必须复用正常 renderer、pose 和 attachment 路径。

相关的固定截图矩阵与输出由[视觉验收指南](../guides/visual-validation.md)维护。
