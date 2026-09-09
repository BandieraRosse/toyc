# 渲染、HUD、特效与性能

> 文档更新：2026-09-09
> 源码核对基线：工作区（Hurd 四职业 presentation profile 与固定 hurd-squad capture；Visual CLI V1 固定 procedural-humanoid 离屏 BMP capture；低模 AI 使用显式 state/profile 的 procedural humanoid 入口；HUD 与启动菜单使用 UTF-8/GB2312 8×16/16×16 点阵文本；viewmodel、crosshair、effects、managed actor 和客户端远端玩家的 gameplay 展示查询直接读取 actor；远端位置/朝向继续使用纯 derived presentation cache；RAY tracer 短线段/定向线宽投影，通用 emitter preset table，CAMERA_SHAKE 含开火后座与受击摇晃；受击四角浅红边缘与八方向中心箭头；程序化敌人身体组件描述表；world-space 静态 RMESH prop 入口与十件组件不重叠开发场景）

> 源码核对补充：正式 Hurd actor 通过四个专用 character profile 进入职业外观；恢复的四名 Maid 旗卫以 Maid character profile 接入 actor，同时继续由 anime identity 选择骨骼模型；普通 player、Eula、佣兵解析为 NONE。

## 渲染边界

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

静态环境组件通过 `rasterfall_render_static_prop()` 提交 world-space RMESH。地图 parser 将
注册表 asset name/id 转为轻量 `toy_map.props` 实例，renderer 遍历该数组；入口消费 RFU
`x/y/z`、绕世界 Y 轴的 yaw 和实例缩放，按
“RMESH local → `512/232` profile scale → instance scale → yaw → world translation”求值。
注册表模型缓存按 asset id 懒加载一次，多个实例共享同一 `rasterfall_model_asset`；地图实例在
空地 `z=-17000` 一带按 1500 RFU 间距展示十件工业组件，用于检查底部 pivot、尺寸、yaw、材质和深度；
相邻实例的 profile 碰撞 AABB 保持正间隙，不以视觉网格孔洞替代玩法碰撞。
地图实例使用 `asset x z yaw scale` 五个字段，`y` 固定为地面锚点 `-900`，`scale=1000`
表示资产原始设计尺寸。visual mesh 是 presentation-only；碰撞由地图 parser 从 profile 独立生成 gameplay box。

程序化敌人模型采用统一的 `enemy_body_part` 描述：每个条目对应一个基本身体组件，类型包括局部朝向盒、世界盒、圆柱、椭球和面部矩形，尺寸与局部偏移仍使用现有 RFU 数值。通用解释器按描述顺序提交几何，因此可以在不改变玩法状态的前提下继续接入参数化配置。敌人位置以 `toy_game_enemy.x/z` 为水平锚点，垂直基准由地面 `Y=-900`、`ground_y` 和 `airborne_y` 组成；Charger 的水平放大和普通敌人的既有缩放语义保留在解释器中。Tank 的挥臂依赖蓄力时间，是动态组件，继续由专用函数求值后插入静态组件之间，以保持原有遮挡和绘制顺序。

`src/render/rasterfall_render_frontend.c` 是渲染器前端适配，管理默认纹理、覆盖配置和 worker
绑定；底层光栅器在仓库公共的 `lib/graphics/renderer.c` / `include/toy_renderer.h`。

其他视觉模块：

- `lib/graphics/fb_font.c`：加载 `assets/fonts/gb2312-16.rfh`，把 UTF-8 字符串映射到半宽 ASCII
  或全宽 GB2312 16×16 点阵；启动菜单中的“光栅坠落”是游戏内中文显示的常驻验证入口。该资产也供
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
底层 renderer 收集/光栅化几何；随后绘制 HUD、菜单和调试叠层并 present。客户端角色展示可能使用
网络插值状态，不应误读为权威 `toy_game` 状态。

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
```

输出为 24-bit BMP（单人 800×800，小队 1600×800），路径由调用者指定，父目录须已存在；成功后打印最终路径并退出。
已有文件会覆盖。可连续 capture 后使用 `cmp` 检查字节一致性，再用图片查看工具观察。
不依赖窗口、音频、私有角色资源、地图或 gameplay step。

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

## 常见任务落点

- 世界物体缺失或遮挡错误：`render_scene()`、对应 `render_*`，再查近裁剪和 depth 路径。
- 角色模型/LOD/并行渲染：character loading、`render_characters_parallel()`、骨骼角色入口。
- 第一人称枪械位置或枪口：`rasterfall_viewmodel.c`；第三人称持枪在 render/model/calibration。
- UI、记分板、伤害闪屏：`rasterfall_hud.c` 或 `rasterfall.c` 中独立 overlay。
- 光照、纹理、材质：lightmap 和 textured triangle 路径；资产解码在 `lib/assets.c`。
- 性能回归：先用 `rasterfall_perf` 的分阶段数据区分玩法、建模、提交和 raster，再改实现。

模型和动画的求值边界见 [assets-animation.md](assets-animation.md)。改可见结果时保留确定性截图/像素
测试的价值；改并行路径时还要比较单 worker 和多 worker 的画面与统计。
