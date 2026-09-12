# 渲染、HUD、特效与性能

> 文档更新：2026-09-12
> 源码核对基线补充：Enemy Procedural Rig V1；三特感 profile/pose 分离、posed tongue socket、真实命中 particles 与固定 world/silhouette capture。
> 源码核对基线：工作区（Enemy Visual V2 六份公开 RFM2 / renderer-only family；Character Material Lighting Policy V1；Enemy Presentation V1 1000ms ballistic body fade / rotating irregular fragments / directional trailing emitter / 10% legacy death；Humanoid Action Composition V1.1 additive recoil；modular RFANIM 独立 locomotion 时钟与双手持枪轨道；RFCHAR +Z forward basis；PRIMARY_GRIP weapon presentation；开发者 world strip 与战斗区共用 modular path；出生点 V2 action debug station；双正式四人 squad；Lighting V1；renderer frame ownership cleanup）

> 源码核对补充：正式 Hurd actor 通过四个专用 character profile 进入职业外观；恢复的四名 Maid 旗卫以 Maid character profile 接入 actor，同时继续由 anime identity 选择骨骼模型；普通 player、Eula、佣兵解析为 NONE。

正常 world render 的开发展示由 Game-owned World Content policy 控制：
`model_gallery` 与 `Character Test Strip` 只在 Campaign 01 启用，Outpost
不会调用这些 Campaign fixture（包括固定 Eula、developer strip 与 Humanoid debug）。明确的 visual/model diagnostic CLI 仍
使用各自的离屏 fixture，不受正常 world policy 影响。

Profession Modularization V1 的提交顺序是 finalized shared-body instance → passive rigid gear → active
weapon presentation；active weapon 从 finalized instance 的 `WEAPON_R` 对齐 authored `PRIMARY_GRIP`，不
读取 `pose_calibration_local`，绘制前执行左臂 FOREGRIP attachment IK。`rasterfall_render_character_instance()` 的 shirt/pants override
仅在单次 submission 生效，不修改 immutable resource material table。地图初始普通队友 Jesus 是首个
vertical slice：actor 的 stable character ID 解析到 Rifleman recipe，presentation runtime 按 actor index
持有独立 instance；`--visual-capture modular-teammate` 固定观察 idle/move/fire/reload/hit。

正式 RF 小队的每个 actor 先由 character ID 解析到 profile，再由 profile 的 modular recipe 选择
共享 V2 body 和共享 rigid gear resources；`render_modular_ai_teammate()` 按 actor index 保持独立
model instance。这个路径不从 actor 读取资源路径、gear list 或 palette override，失败时仍回退到
既有 procedural actor。

modular actor 在提交 body 前把玩法 animation semantic 适配为固定 action layers：IDLE/MOVE 更新并
保留 lower IDLE/WALK，FIRE 只替换 upper 为 RIFLE_FIRE，因此移动中开火不会清掉腿部动作；普通持枪
状态使用 RIFLE_IDLE。AIM clip 已可由 composition/CLI 使用，等待 gameplay 明确 aim semantic 后再由
adapter 接入，不能从骨骼姿态反推瞄准。组合完成并更新 bones 后，被动 gear 只读 HEAD/CHEST/BACK/HIP
等 finalized attachment，active weapon 则只读 finalized `WEAPON_R`，以 authored `PRIMARY_GRIP`
派生 weapon origin 和其他 weapon sockets；左臂 IK 只修改当前 actor 的 mutable instance，不回写共享
resource。

RFANIM walk 的 800ms authored 周期不直接复用 gameplay MOVE 的 400ms 回卷值。modular renderer
按 actor instance 累计跨回卷的展示时间，短暂 upper-body action 期间保留 lower-body 相位；因此动作
完整走完 800ms 后才循环。RFCHAR body、gear、socket 与 weapon 统一遵循契约的模型 `+Z` 前向，actor
yaw 不再附加 180 度修正。

出生点附近的 V2 action debug station 是 renderer-only fixture：它使用独立的
`rasterfall_model_instance`，不进入 `toy_game_actor`。按钮循环选择 `IDLE`、`WALK`、`RIFLE AIM`
和 `AIM + RECOIL`；最后一项按 lower locomotion → upper aim → additive recoil 的顺序组合，
并从同一 finalized pose 更新 AK socket、PRIMARY_GRIP、FOREGRIP 和 hand target。

RF Humanoid V2 的资产前向事实保存在 modular skeletal profile 中；body world rotation、
rigid attachments、character sockets 和 skeletal weapon 使用同一 profile basis，不在枪械
绘制函数内追加独立的 180 度修正。运行时可用 `--action-runtime-debug` 输出 actor、renderer
path 以及 lower/upper/additive action，确认 RF Humanoid 没有被 legacy anime path 抢占。

## Enemy Visual V2

普通敌人的可选 BLOCK_INFECTED / HUMANOID_INFECTED 家族由 renderer-only recipe 选择六份公开
RFM2；默认 AUTO 按 COMMON 70/20/10、FAST 40/40/20、HEAVY 30/40/30 混合。
Smoker / Charger / Tank 使用 `rasterfall_enemy_rig.h` / `render/rasterfall_enemy_rig.inc` 的
procedural rigid profile → truth adapter → pose → generic renderer；几何和计时不再混在专用绘制函数。
Charger 的命中边沿与 Tank 的 625ms 峰值只消费 gameplay；模型尺寸由 profile 拥有。
新增敌人的默认路线、固定关键帧、轮廓与 near/mid/far 验收见下方专题入口。
资源、串行 scratch pose 与逐槽位步幅均归 renderer，
不进入 gameplay 或 snapshot。新身体通过既有三角形入口保留受击、死亡旋转和渐隐；
入口、预算、远裁剪和验收见 [enemy-visuals.md](enemy-visuals.md)。

正式地图的 `MODEL_DISPLAY` 陈列台继续使用旧 style 1--5 的程序化敌人展示，并在右侧增加
style 6--14 的三组展示：COMMON、FAST、HEAVY 各自依次绘制 LEGACY、BLOCK_INFECTED、
HUMANOID_INFECTED。V2 两个家族由地图 draw record 触发同一感染模型 recipe，位置、姿态和
地面锚点属于 renderer；不会创建 enemy、碰撞体、AI 或网络状态。

## 渲染边界

Profession Visual System V1 的 `--profession-lineup <model-dir> <output-dir>` 由 options →
main 早退 → `rasterfall_render_profession_lineup()` 执行；实现位于
`dev-tests/rasterfall_visual_capture.inc`，复用 `visual_acceptance_model_frame()` 和标准 AK。
六人从左到右为 Rifleman、Breacher、Recon、Medic、Engineer、Heavy；同一深度缓冲、同一
Lighting V1、无名字标签，沿相机水平轴摆放，保证 side 也不重叠。输出 2400×900 BMP：
front/three-quarter 的 near/mid/far，以及 side-mid；距离为 2200/4400/8800 RFU。
正面与 3/4 站位间距 440 RFU，side mid 为避免枪管遮住相邻背包，间距为 660 RFU；
同一方向的角色与间距不随距离变化，远景不会自动放大来伪装可读性。

RFCHAR 的 Character Lab、world strip 与 lineup 统一采用 835 milli-scale（2.080m
源 body 对应约 1.736m 展示身高）。装备不再影响身体缩放；RFCHAR 使用 canonical 原点，
不会被非对称背包的包围盒偏移。旧非 RFCHAR 模型仍使用原有高度适配。此校准仅属于诊断展示，
不修改正式 gameplay actor 渲染或 RFCHAR/RFM2 数据。

`src/rasterfall_render.c` 是世界渲染和角色渲染主体：投影/近裁剪、三角形提交、地面与地图图元、
拾取物、敌人、玩家/队友、骨骼角色、弹道粒子及模型诊断。公开入口在
`include/rasterfall_render.h`，共享状态由 `rasterfall_render_context` 绑定。

低模 AI 链路为 `rasterfall_render_ai_teammate()` → `render_ai_teammate()` 的 actor
遍历/可见性/模型路径选择 → `rasterfall_render_procedural_humanoid()` → 现有 pose、身体部件、
武器 helpers → primitive 提交。公开入口和 `rasterfall_procedural_humanoid_state` 位于
`include/rasterfall_render.h`，实现保留在 `src/rasterfall_render.c`，不增加编译单元。
场景层提供 actor 的 x/z、sy/cy、ground_y + airborne_y、当前 slot 武器、downed、动画 ID/时间；
AI 的 muzzle_flash 参数仍为 0。入口只读这些瞬时参数，不查 actor 数组、不裁剪整个人物、
不绘制姓名/血条，也不查询地面；调用者可传入指定 camera。

`rasterfall_character_profile()` 提供 body/leg/skin/hair 基础外观：body 用于躯干及上臂，
leg 用于腿，skin 用于头部和脸部，hair 用于脸部矩形；武器 helper 的前臂固定肤色保持原样。
负 character ID 的旧 class/body tint 由调用适配层覆盖 profile 副本的 body_color，
其余外观继续使用默认 profile。实际骨骼路径选择仍取决于 `anime_character_id` 与模型是否加载，
不在本入口解释 profile 的 model_path/actions。

入口采样现有 `rasterfall_actor_animation_sample()`，保留 reload 武器时长、前移/抬升、
腿摆动、身体俯仰、death/revive 翻倒和 downed 简化身体的既有行为与绘制顺序。
内部保存/恢复 primitive helpers 的 lift/roll 临时状态；仍依赖已绑定 render context 和串行
helpers，不承诺并发重入。`render_player_avatar()` 仅保留其他现有调用者的参数适配。
职业身份枚举 `rasterfall_profession_id` 位于 character identity 头文件，与基础 character ID 独立；
稳定职业身份保存在四个 Hurd profile 和一个 Maid profile 的 `profession_id` 中，不作为重复字段进入 actor 或网络结构。
actor 展示适配器从 `character_id` 解析 profile，并通过
`rasterfall_procedural_humanoid_state.profession_id` 携带一次程序化绘制的身份；普通 player、地图佣兵、
hired AI 和远端普通玩家均使用负值 `RASTERFALL_CHARACTER_NONE` 并解析为 NONE。`anime_character_id`
继续独立选择原有 Eula/Maid 骨骼资产路径；正式 Maid 旗卫同时携带 Maid `character_id`，不再由模型选择器隐含 profession。`rasterfall_profession_visual_profile()` 在 character
模块解析静态 presentation-only 配置：accent/gear 颜色、head、badge、waist_bag、backpack、vest。
NONE/无效 ID 返回 NULL，完全跳过装备绘制，基础身体和既有绘制顺序保持原样。

正式 Hurd 四人是 `anime_character_id == 0` 的普通程序化 actor；renderer 与其他低模 AI 一样只读
actor 的 gameplay state，但从其稳定 `character_id` 解析四个 profession profile。即使工作区存在私有
骨骼角色资产，也不会把 Hurd actor 错切到 Eula/Maid 模型分支。Visual CLI 的 `hurd-squad` 仍仅是
离屏 fixture，不是正式 Hurd actor 的状态源。

renderer 内 `render_profession_visual()` 组合相同 actor-local box primitive：Gunsmith 橙色工具侧包、
露出扳手和护目镜；Logistics 卡其大背包、侧袋、胸袋和帽檐；Medic 灰白医疗箱、绿色十字和头带；
Guard 宽厚深绿背心、肩部护片、盾徽和简化头盔带。基础身体配色仍由 character profile 提供。
附件使用现有 lift/roll，躯干附件使用 body_pitch，头部使用原有 head lift；downed 简化代理不画
直立装备，death/revive 使用既有整体翻倒变换。未增加动画状态或附件资产系统。
portrait 可复用该入口和静态 profile，
仍需自行提供 camera 和展示状态。

RF Humanoid Headgear / Face Coverage V1 属于 asset-side presentation 扩展：变体模型沿用
同一 RFCHAR skeleton 和 stable `HEAD` attachment，头盔、goggles、respirator 等几何以
`RF_HEAD` 刚性权重随角色 pose 求值。renderer 不解释 headgear 名称，也不增加 actor 或网络
字段；Character Acceptance / world capture 只通过替换 `--character-world-model` 或输入模型
路径观察不同变体。当前阶段的完整 RFCHAR variant 是验证 carrier，未来若加入通用 rigid HEAD
assembly，仍应保持 renderer 只消费稳定 attachment transform。

静态环境组件通过 `rasterfall_render_static_prop()` 提交 world-space RMESH。地图 parser 将
注册表 asset name/id 转为轻量 `toy_map.props` 实例，renderer 遍历该数组；入口消费 RFU
`x/y/z`、绕世界 Y 轴的 yaw 和实例缩放，按
“RMESH local → `512/232` profile scale → instance scale → yaw → world translation”求值。
注册表模型缓存按 asset id 懒加载一次，多个实例共享同一 `rasterfall_model_asset`；地图实例在
空地 `z=-17000` 一带按 1500 RFU 间距展示十件工业组件，用于检查底部 pivot、尺寸、yaw、材质和深度；
相邻实例的 profile 碰撞 AABB 保持正间隙，不以视觉网格孔洞替代玩法碰撞。
地图实例使用 `asset x z yaw scale` 五个字段，`y` 固定为地面锚点 `-900`，`scale=1000`
表示资产原始设计尺寸。visual mesh 是 presentation-only；碰撞由地图 parser 从 profile 独立生成 gameplay box。

Generic Rigid Attachment V1 使用独立的 `rasterfall_render_rigid_resource()` full-transform submission，
不走 floor alignment。矩阵为 row-major，求值链为
`actor/world × finalized socket × mount correction × authored rigid local`；translation、完整 3×3
rotation 和 uniform scale 同时作用于顶点，rotation 同时作用于法线。RMESH header 的
`position_scale` 在 submission 边界把 asset meters 换成 512 RFU/m，附件 bounds 不参与角色或附件
scale。`rasterfall_render_rigid_attachment()` 只查询 instance finalized socket、组合矩阵并提交共享
resource，不修改 host pose，也不把 actor/world 状态写入 instance。

`--rigid-attachment-acceptance <model-dir> <output-dir>` 在同一深度缓冲绘制共享一份 Humanoid
resource 的 bind/turned 两个 instance，并让二者共享同一份 helmet/backpack resource；右侧额外旋转
chest 与 head，固定输出 socket 数值和 `rigid-attachment-acceptance.bmp`，可用重复 capture 做逐字节回归。

`--squad-acceptance` 额外以 Jesus WALK+FIRE、Engineer WALK、Heavy WALK+FIRE 压测组合后的 body、
backpack/hip/chest gear、socket attachment 与八 instance 隔离；三视角仍是固定输入确定性 BMP。

程序化敌人模型采用统一的 `enemy_body_part` 描述：每个条目对应一个基本身体组件，类型包括局部朝向盒、世界盒、圆柱、椭球和面部矩形，尺寸与局部偏移仍使用现有 RFU 数值。通用解释器按描述顺序提交几何，因此可以在不改变玩法状态的前提下继续接入参数化配置。敌人位置以 `toy_game_enemy.x/z` 为水平锚点，垂直基准由地面 `Y=-900`、`ground_y` 和 `airborne_y` 组成；Charger 的水平放大和普通敌人的既有缩放语义保留在解释器中。Tank 的挥臂依赖蓄力时间，是动态组件，继续由专用函数求值后插入静态组件之间，以保持原有遮挡和绘制顺序。

敌人死亡样式由 `rasterfall_effects` 按槽位持有，仅在首次观察到 `active == 2` 时选择：约 10%
保留原有整体压扁，其他情况在 1000ms `dying_ms` 窗口内沿最后一击方向加入确定性的随机侧偏，
以抛物线抬升并绕身体中心旋转；最后 380ms 通过透明三角形提交让全部 `enemy_body_part` 同步渐隐，
不再按部件顺序逐个消失。同时固定容量 emitter 跟随飞起的身体持续发射两层 raster fragments：
较大的主体碎片使用随时间旋转的不规则四面体，并按带重力的抛物线形成高速定向冲流；较小尘屑
使用透明 camera-facing 几何和更宽的侧向扩散；
两层生命周期分别为 1350ms 和 1550ms，因此可在 enemy slot 清空后继续消散。网络侧缺失最后一击
事件时才回退敌人朝向。样式、碎片和消隐阈值均不进入 snapshot。受击方向偏移也读取同一 effect
event payload，位移幅度按 damage 限幅缩放。

`src/render/rasterfall_render_frontend.c` 是渲染器前端适配，管理默认纹理、覆盖配置和 worker
绑定；底层光栅器在仓库公共的 `lib/graphics/renderer.c` / `include/toy_renderer.h`。

## Lighting V1：RMESH 形体光照

正式 RMESH 路径在 `render_gallery_model_range()` 统一应用低成本 ambient + directional
form-lighting，覆盖 RFCHAR/skeletal body、static prop 和通过同一模型入口绘制的第三人称 weapon。
变形或实例 yaw 后的三个顶点法线先求平均，每个提交三角形只计算一次整数点积；纯色材质在提交前
调制 base color，纹理材质把同一固定亮度交给已有 textured raster command。没有新增逐像素法线
计算，也不改变材质色相、饱和度或 gameplay 状态。

当前 Q8.8/Q15 参数集中在 `rasterfall_render.c`：世界主光方向为归一化
`(-0.408, 0.816, -0.408)`（表面指向高处西北主光），ambient 为 `136/256`，directional 为
`120/256`，因此 `form = max(136, 136 + max(dot(N,L),0) * 120) / 256`，最大为 1.0。
RFCHAR 或 skeletal 模型使用 `144/256` 的基础 presentation visibility floor；其他 RMESH 使用
`136/256`。Character Material Lighting Policy V1 在同一个 `character_render_policy()` 中复用
RFM2 material role，并在 form 与 scene/lightmap 相乘后对角色材质作最终亮度保护：FACE 与 SKIN
为 `224/256`，EYES 为 `240/256`，HAIR 为 `176/256`；上限当前统一为 `256/256`，rim 字段预留为
零且不执行额外 pass。CLOTHING、EQUIPMENT、无 role 材质及全部非角色 RMESH 保持 Lighting V1
原公式。纯色与纹理 submission 都应用相同策略，雾仍在原有阶段处理。当前没有 stylized
quantization、point light、shadow、probe 或动态局部光。

`rasterfall_render_set_model_lighting()` 仅供 presentation/诊断消融。`--model-performance` 的
`full` 与 `lighting_off` 保留相同材质功能，只切换上述 form-lighting，能够直接比较成本。
Character Acceptance 额外输出 `lighting-ab/{bind,rifle-idle,rifle-aim}/{front,side,back,three-quarter}.bmp`，
每张图左侧为 OFF、右侧为 V1；`--visual-capture lighting-props` 以相同方式固定输出 crate、
workbench、vent unit 和 industrial pillar。两条入口都不读取时钟，适合用 `cmp` 做确定性检查。
Character Acceptance 还输出 `lighting-policy/{normal-light,back-light,dark-environment}.bmp`：前两张
从固定 directional key 的正反方向观察，dark 使用固定 `96/256` scene brightness；该 override
只存在于进程内 capture fixture，不进入 runtime 参数、地图、gameplay 或网络状态。

其他视觉模块：

- `lib/graphics/fb_font.c`：加载 `assets/fonts/gb2312-16.rfh`，把 UTF-8 字符串映射到恢复的旧版
  VGA 半宽 ASCII 或全宽 GB2312 16×16 点阵；启动菜单中的“光栅坠落”是游戏内中文显示的常驻验证入口。该资产也供
  地图排布导出器读取，运行时不依赖 FreeType、系统 CJK 字体或宿主 libc 编码转换。

- `rasterfall_hud.c`：玩家、网络、波次、商店 HUD，交互提示和 BMP/帧导出。
- `rasterfall_viewmodel.c`：第一人称手臂、武器模型、后坐/摆动和枪口位置。
- `rasterfall_effects.c`：消费 `rasterfall_effect_event`，并从投掷物/燃烧区域展示状态同步枪口闪光、弹道、命中粒子、炸弹闪烁、Molotov 火焰和局部镜头晃动等短生命周期表现状态。
  事件消费现在还会登记到固定容量的 `rasterfall_effect_instance` runtime 池；instance 将底层
  组件类型（particle/ray/billboard/overlay/emitter/material/camera_shake）与语义 kind 分离。tracer、命中火花、分层 muzzle flash 和 Molotov 火焰已迁移到统一
  `RAY`/`PARTICLE`/`BILLBOARD` 组件；`ENTITY_HIT` 生成命中粒子但不再额外生成整条 hit ray，炸弹 fuse flash 使用 billboard；玩家伤害闪屏使用 `OVERLAY`，敌人受击颜色使用 `MATERIAL` feedback，交互高亮已登记为短生命周期 `INTERACTION_HIGHLIGHT` billboard 并驱动现有高亮绘制，屏幕空间效果通过 `render_effect_overlay()` 和
  `rasterfall_render_overlays()` 提供统一入口，因此本阶段不改变已有效果画面。`CAMERA_SHAKE`
  组件不修改权威摄像机，只在渲染阶段复制出的 `render_camera` 上叠加视空间平移、偏航和俯仰扰动；当前仅本地 `WEAPON_FIRE` 事件生成该组件，AI/远端开火事件通过 `LOCAL_VIEW` 标志隔离。多个组件先按轴叠加，再按每轴最大值限幅，并用短时插值追踪目标值。`EXPLOSION`
  现在由固定生命周期的 emitter 生成 16 个通用 `EXPLOSION_PARTICLE` 子实例；事件类型统一定义在
  `include/rasterfall_effect_event.h`，该模块不反写 gameplay。
- `rasterfall_sky.c`：天空背景。
- `rasterfall_perf.c`：阶段计时、场景统计和性能输出。
- `src/dev-tests/*.inc`：角色基准和蒙皮跟踪，直接包含进 render 编译单元。

## 一帧的数据流

主循环更新 session/net/effects 后，展示层从 `actors[TOY_GAME_PLAYER_ACTOR_INDEX]` 和其他 actor
读取玩家状态，再设置 `rasterfall_render_context`，调用场景及实体公开入口；客户端远端玩家的
HP、武器、downed、动画和统计也从对应 actor 读取。网络连接状态来自 `clients[]`，远端位置/朝向
插值来自 derived presentation cache，不作为远端 gameplay 展示源；不从 `toy_game` 顶层玩家字段
取 HUD、第一人称武器或受击效果数据。
底层 renderer 收集/光栅化几何；Core Host 负责 begin、分层 flush、最终 flush 和 present；随后绘制 HUD、菜单和调试叠层。客户端角色展示可能使用
网络插值状态，不应误读为权威 `toy_game` 状态。

Renderer frame ownership：Core 拥有 renderer/window/surface 的创建、初始化、生命周期和销毁，
并通过 `rf_core_begin_frame()`、`rf_core_flush()`、`rf_core_end_frame()` 管理一帧。Game runtime
只更新 camera、准备 presentation state 并提交 draw commands；Rasterfall renderer 只负责
rasterization、commands 和 render cache/state。renderer 内的 `rasterfall_render_bind()` 全局绑定
是现有串行 presentation context：它把 session/effects/net/纹理/lightmap 提供给旧的绘制 helper；
并行模型录制则优先使用 `toy_renderer.recording_context` 对应的 frontend state。该绑定不拥有
window、surface、present 或 Core 资源生命周期。

战斗事件链路为：规则结果/网络展示适配器 → `rasterfall_effect_event` →
`rasterfall_effects_consume()` → runtime instance pool。tracer、命中火花、
muzzle flash 和 Molotov 火焰的渲染已经直接消费 runtime `RAY`/`PARTICLE`/`BILLBOARD` instance；runtime instance 统一拥有组件类型、
语义 kind、位置、方向、速度、生命周期/年龄、尺寸和 alpha 等基础状态。更新阶段统一按固定 16ms
步进推进粒子运动和寿命；
射击同步器只搬运规则层射线与枪口坐标；
伤害、命中规则和网络快照不读取或写入这些视觉状态。镜头晃动实例在固定步中先按轴叠加并限幅，
再以 `RASTERFALL_CAMERA_SHAKE_SMOOTHING` 对聚合目标做短时插值；快速连射因此会累积到上限，
停火后平滑回零。

当前镜头后座平衡参考（玩家射速倍率 200%，实际间隔为基础 cooldown 的一半）如下：

| 武器 | 实际间隔 | 后座衰减 | 垂直单发振幅 | 理论连续峰值 | 垂直上限 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 手枪 | 100 ms | 110 ms | 8 | 约 9 | 18 |
| SMG | 50 ms | 150 ms | 8 | 约 16 | 20 |
| AK | 90 ms | 240 ms | 18 | 约 32（触顶） | 32 |
| 霰弹枪 | 400 ms | 170 ms | 16 | 约 16（单发） | 28 |
| AWP | 600 ms | 110 ms | 5 | 约 5（单发） | 10 |

理论连续峰值按“同向叠加、线性衰减”估算，实际画面还会经过短时插值和确定性噪声，因此会略低或有小幅波动。

爆炸由炸弹命中/结束位置产生 `RASTERFALL_EFFECT_EVENT_EXPLOSION`，在
`rasterfall_effects_consume()` 中登记一个固定容量 emitter，并由 emitter 按间隔把子组件写入统一
instance pool；当前爆炸配置生成冲击波 ray、短时 billboard 和固定数量的粒子子 instance，由
  `rasterfall_render_effects()` 的通用 world primitive 分支绘制。该接入不修改炸弹伤害和网络协议；联机事件仍应由展示适配器构造已有 event。当前 Molotov 火焰已经通过
`rasterfall_effects_sync_fire_zones()` 将每个燃烧区域同步为固定容量 emitter；emitter 按固定间隔批量生成
  `FIRE` 语义的 `PARTICLE` instance，并由通用粒子绘制入口消费。emitter 的子组件类型、生成间隔、
  数量上限、散布和 placement pattern 都是固定容量 runtime 描述；FIRE 与 EXPLOSION 都从统一
  preset table 复制 emitter 标量参数及 child descriptor 列表，事件只负责填充位置等动态字段，主循环现在通过
  `rasterfall_render_effects()` 统一提交 ray/billboard/particle，overlay 仍在屏幕空间阶段单独提交，
  以保证 HUD 和第一人称视图模型的层级顺序。
  受击 `DAMAGE_FLASH` overlay 以低透明度混合四角短 L 形红边，并在准星外围绘制快速消失的
  红色箭头。箭头以最近存活敌人为展示层伤害来源估计，并相对当前相机朝向归一到前、后、左、右
  及四个对角方向；该估计不进入 `toy_game` 或网络快照。
  镜头晃动不进入世界 primitive 绘制；`rasterfall_effects_apply_camera_shake()` 在世界渲染前对当前
  `render_camera` 做确定性衰减采样。受击摇晃使用独立的可配置 preset：默认随机左右偏航约 15°，
  先快速到峰值、短暂保持，再在 500ms 内连续衰减；重置时清除未完成的受击方向。最短接受间隔内
  的重复伤害不会生成新的摇晃，参数位于 `include/rasterfall_effects.h`。
  统一 instance 和 emitter 池均为固定容量环形池，满载时按写指针覆盖最旧槽位；该策略已由逻辑测试覆盖。后续可在不改变火焰语义的前提下替换粒子渲染细节。

tracer RAY instance 保留事件提供的枪口起点和命中/射程终点，但绘制时按 lifetime/age 在两点
之间截取短段并推进到终点，不再显示整条弹道。颜色在整条可见短段内保持统一，线宽、尾段比例和 68--86ms 的寿命
由 `toy_game_weapon_info` 的 presentation-only descriptor 提供；普通武器统一白色，AK/AWP
采用黄橙色。第一人称本地玩家 tracer 按相机空间深度抑制枪口近处亮度，使用平滑插值从约中
距离开始逐渐显现；AI/远端玩家也使用短线段快速移动，但采用更短的可见段和独立距离参数。
AI/远端 tracer 使用世界空间小方柱，避免沿射线方向观察时固定屏幕线宽盖过透视长度而显示为横线。
instance pool、事件和深度测试 flags 不变。

## Visual CLI V1：固定场景观察

从仓库根目录运行：

```sh
make app-rasterfall
build/rasterfall --visual-capture procedural-humanoid --visual-output /tmp/rf-humanoid.bmp
build/rasterfall --visual-capture lighting-props --visual-output /tmp/rf-lighting-props.bmp
build/rasterfall --character-acceptance rasterfall/private-assets/models/rf_humanoid_acceptance.rmesh /tmp/rf-humanoid-v11
build/rasterfall --squad-acceptance rasterfall/private-assets/models /tmp/rf-squad-acceptance
```

输出为 24-bit BMP（单人 800×800，小队 1600×800），路径由调用者指定，父目录须已存在；成功后打印最终路径并退出。
已有文件会覆盖。可连续 capture 后使用 `cmp` 检查字节一致性，再用图片查看工具观察。
`--character-acceptance` 是独立的正式角色验收场景，依赖指定私有角色 RMESH 和现有标准 AK，
输出固定三姿态四视角及 near/mid/far A/B；RFCHAR 正面为 canonical +Z。该入口保留旧的
CHEST `visual_rf_calibration()` 枪架和双手 `rifle_solve_hands()`，用于 legacy carrier/校准诊断，
不代表正式 modular teammate 的持枪来源。正式 modular path 从 finalized `WEAPON_R` 对齐
authored `PRIMARY_GRIP`，并在绘制前用同一枪的 `FOREGRIP` 解算左上臂/前臂；`visual_rf_check_grips()` 仍只检查 legacy acceptance。
普通 Visual CLI 仍不依赖窗口、音频、地图或 gameplay step。
该入口加载一个 `rasterfall_model_resource`，全部 pose、握持 IK、CPU skinning 和 socket 查询来自
`rasterfall_model_instance`；另输出 `two-instance-isolation.bmp`，在同一 depth buffer 中以共享 resource
绘制左侧 bind 与右侧 aim 两个独立 instance，作为 deterministic ownership 观察门。

真实地图验收使用：

```sh
build/rasterfall --character-world-capture /tmp/rf-world-v11
# 对照另一套 RFCHAR body，仍走同一个地图、灯光、深度和 Character Test Strip：
build/rasterfall --character-world-capture /tmp/rf-world-v2 \
  --character-world-model rasterfall/private-assets/models/rf_humanoid_v2.rmesh
```

该入口先加载正式地图并 reset session，再通过正常 `rasterfall_render_scene()` 输出
`near.bmp`、`mid.bmp`、`far.bmp`，并输出 `{near,mid,far}-{old,idle,aim,motion}.bmp`。
固定镜头取相对目标 (0.6d,0,0.8d) 的斜正面位置（d=2000/4000/8000 RFU），
避免原正前方工业 prop 与负 Z 边界墙挡住距离验收；场景深度和几何均照常绘制。
Character Test Strip 位于 `rasterfall.map` 的
`z=-20000` 展示带：旧 procedural AK，以及由 `render_modular_preview_frame()` 绘制的 RF
rifle idle、RF rifle aim、RF locomotion-like pose；后者与战斗区共享 RFANIM 分层组合、V2
`WEAPON_R + PRIMARY_GRIP` 武器呈现和左手动作轨道。它们是 renderer presentation-only
entities，不进入 actor、碰撞、AI 或网络状态。未提供
`--character-world-model` 时使用 `rf_humanoid_v2.rmesh`；提供时只替换 strip 的
skeletal body，不改变 camera、AK、地图或 world render path。

角色模型观察以组图为默认工作方式，以便一次比较姿态、角度和距离。V2 角色使用
`python3 tools/character_lab_sheet.py` 汇总 bind/rifle-idle/rifle-aim 的四视角；真实场景
使用 `python3 tools/character_world_sheet.py` 汇总 near/mid/far 与 old/idle/aim/motion。
组图脚本只是对当前 capture CLI 输出的离线拼接层，不改变渲染路径；需要像素级诊断时再打开
其保留的单张 BMP 原始文件。

数据流：options → main 诊断早退 → 命名场景检查/固定 setup →
`rasterfall_render_procedural_humanoid()` → 普通 primitive 与武器 helper →
`toy_renderer_flush()` → `rasterfall_hud_dump_bmp()`。setup 与进程级 capture 实现在
`src/dev-tests/rasterfall_visual_capture.inc`，由 render 编译单元包含，人体代码没有副本。

场景 `procedural-humanoid` 使用 Akari 基础 profile、手枪、idle 0ms、未倒地，
actor 展示锚点 x/z/lift 均为 0，朝向 sy=512/cy=-887；camera 位于 (x=0,y=-350,z=-1900)，
yaw sy=0/cy=1024、pitch sy=0/cy=1024。固定纯色背景与光照，串行光栅化并关闭交互 watchdog。
fixture 仅预载公开手枪到既有模型缓存，避免 gallery 扫描；该入口只供新进程诊断后立即退出，
不是运行中切换场景的 API。

```sh
build/rasterfall --visual-capture hurd-squad --visual-output /tmp/rf-hurd-squad.bmp
```

`hurd-squad` 从左到右固定 Gunsmith、Logistics、Medic、Guard，均使用 Akari 基础身体与 idle 0ms，
前三人显示 Pistol，Guard 显示 SMG（仅 fixture 的 weapon 展示字段）。camera z=-2600，其余相机参数
及角色朝向沿用单人场景；角色 x 为 -1320、-440、440、1320，z/lift=0。斜向正面构图同时展示胸口、
头部、武器和侧后附件。固定背景/光照，串行绘制，预载公开 Pistol/SMG，不初始化 gameplay。
职业装备与普通人物共用 `rasterfall_render_procedural_humanoid()`，没有人物绘制副本。
接口不提供 portrait、任意相机控制、回放或图片基线管理。

`--squad-acceptance <model-dir> <output-dir>` 是正式 RF roster 的固定离屏验收：同时构造两套
四人 roster，共八个 modular actor，输出 `front.bmp`、`three-quarter.bmp`、`side.bmp`。
三视角均为 2400×900、固定相机和固定横向间距；body 只加载一次，gear 按资源 ID 共享，instance
按 actor index 独立。该入口同时检查 palette isolation、RFCHAR attachment transform regression
和 instance resource ownership，重复运行可用 `cmp` 做字节级 deterministic capture 回归。

## 常见任务落点

- 世界物体缺失或遮挡错误：`render_scene()`、对应 `render_*`，再查近裁剪和 depth 路径。
- 角色模型/LOD/并行渲染：character loading、`render_characters_parallel()`、骨骼角色入口。
- 第一人称枪械位置或枪口：`rasterfall_viewmodel.c`；第三人称持枪在 render/model/calibration。
- UI、记分板、伤害闪屏：`rasterfall_hud.c` 或 `rasterfall.c` 中独立 overlay。
- 光照、纹理、材质：lightmap 和 textured triangle 路径；资产解码在 `lib/assets.c`。
- 性能回归：先用 `rasterfall_perf` 的分阶段数据区分玩法、建模、提交和 raster，再改实现。

模型和动画的求值边界见 [assets-animation.md](assets-animation.md)。改可见结果时保留确定性截图/像素
测试的价值；改并行路径时还要比较单 worker 和多 worker 的画面与统计。
