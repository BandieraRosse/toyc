# Rasterfall 渲染架构

> 状态：当前
> 所有者：Rasterfall renderer frontend、Core Host 分层编排
> 最近核对：2026-09-23

本文描述 CPU frontend 以及共享展示语义和帧分层。GPU 有独立来源与提交路径，不消费 CPU 光栅命令或静态烘焙；高级光照仅由 GPU 实现，见[GPU 光照](gpu-lighting.md)。GPU 资源与 presenter 合同见
[GPU 渲染架构](gpu-rendering-architecture.md)；角色表现见 [角色表现](character-presentation.md)；HUD、
特效和 viewmodel 见 [HUD 与特效](hud-effects.md)。

## 所有权

网格编织机的资产解码、稳定三角面成型次序和机械姿态由
`rf_mesh_weaver_presentation.c` 共享；CPU/Scene 分别消费 Runtime 冻结的
`rf_mesh_weaver_frame`。固定机架走静态道具，托盘、八个转向架/内芯和三瓣光学头
走独立刚性变换，制造物使用完整枪械模型及同一物理适配。
Game/session 拥有任务与成品，渲染仅绘制阶段与进度；待取枪体由该表现统一绘制，
普通拾取模型抑制匹配槽位但保留交互。完整单位、网格和资源规则见
[普通机合同](../reference/mesh-weaver.md)。

实验板、道路板和填充板由 `rasterfall_render.c` 的共享 `draw_partitioned_floor` 生成，
分别消费 floor style 10/11/12。板缝、道路边带和原地板颜色在同一平面内分区；
CPU 与 Scene floor mesh 共用该来源。未指定新样式的通用地板维持原行为。
四角信标由共享 `rasterfall_lab_terminal.h` 的 sign style 7 发射底座与正交开放菱形，
底座沿用静态 WORLD 缓存；投影以共享只读时间采样绕竖轴旋转并上下浮动，CPU 逐帧发射，
Scene 消费 layer input 冻结的 `host_time_ms` 进入 WORLD。动画不使静态缓存失效。
亮色几何表示投影，不依赖实际动态照明或透明面板；底座碰撞由地图独立声明。

投影样式 2/3/4/7 共用 `rf_lab_projection_light_emit`：从光学槽展开的淡色光束壳、
细射线和缓慢上行的亮度波模拟空气散射；信标光束上端跟随浮动高度。
样式 2/3/4 另发射深色、区域色调的半透明背景，按观察侧放到字形后方，避免共面叠色。
CPU 使用透明命令；Scene 从冻结地图与时间逐帧发射到 TRANSPARENT，自发光、深度测试但不写深度。
背景与光束没有碰撞，不创建照明灯，不改变静态文字/机壳缓存或 gameplay。

实验区投影终端由 `render/rasterfall_lab_terminal.h` 共用程序几何定义。Runtime 通过显示 channel
更新内容，`render/rf_lab_terminal.inc` 保存展示文本，CPU 在绘制前取值，Scene 在 world freeze 时
按值复制。可更新面由 `rf_gpu_scene_layers.inc` 进入 WORLD；静态网格构建跳过该面，静态资源比较
忽略其文本变化。标题和试样小投影的机壳、图案仍走静态 SIGN 网格；样式 2/3/4 字体统一
按原色向白色混合 45%，Scene 在 WORLD 自发光段逐帧发射，避免环境阴影压暗文字。
CPU 共用提亮字形；背景与光束仍消费原始颜色。内容、控制 object、碰撞和 gameplay 相互
独立；显示更新不修改地图源或玩法真值。样式、图形和默认交互见[实验区合同](../reference/experiment-labs.md)。

机器屏幕 style 5 使用 `rasterfall_machine_screen.h` 的固定逻辑像素网格，共用字体、裁切和 span 发射器；
每个 span 在屏幕同一平面分区着色，不叠加背景/字形面。文本及样式控制均在冻结值内，CPU 和 Scene
复用相同几何。屏幕内容变化不触发静态世界重建；style 6 检修玻璃单独进入有序透明段，
alpha=42/255、深度测试且不写深度。机壳、内部板卡、散热器、显示器、键盘和支架为独立静态 RMESH，
组合与摆放由 Map Parser 降级，渲染器不解释装配文本。

RF 电子产品的扇框、轴心和机壳仍为静态 RMESH；叶片与前/侧状态灯由
`render/rf_electronics_geometry.inc` 共享几何发射器生成。Runtime 实验区控制器积分风扇相位、
发布 `rasterfall_electronics_frame`；CPU 消费当前帧，Scene layer input 按值冻结同一帧，均进入 WORLD 深度域。
转速切换保留相位，暂停/性能隔离冻结时钟；关闭后保留停止位置。状态灯仅为展示活动，
不伪装为 Host 实测负载，不创建 GPU 照明灯，不改变 `toy_game` 或网络状态。动态部分不使静态网格缓存失效。

| 职责 | 所有者 |
| --- | --- |
| world/角色/地图图元、投影、近裁剪与 draw command | `rasterfall/src/rasterfall_render.c` |
| frontend state、默认纹理与 worker binding | `rasterfall/src/render/rasterfall_render_frontend.c` |
| 底层 command、光栅化和 recording context | `lib/graphics/renderer.c`、`include/toy_renderer.h` |
| renderer/window/surface 生命周期、层 barrier、flush 与 present | `rasterfall/src/rf_core_host.c` |
| normal frame 的 camera 与 presentation state | `rasterfall/src/rf_game_runtime.c` |
| 天空、HUD、viewmodel、effects、性能统计 | 各自 `rasterfall_sky.c`、`rasterfall_hud.c`、`rasterfall_viewmodel.c`、`rasterfall_effects.c`、`rasterfall_perf.c` |

renderer 只读玩法或派生展示状态，不修改 `toy_game`。正常交互帧从前后两个逻辑 tick 提取 actor、enemy、projectile 的轻量运动记录，生成独立的展示数组；位置、朝向与同一动作内的动画时间可插值。HP、武器、downed、动作身份和统计仍从当前 actor 真值投影；HUD、第一人称武器和受击效果不得回读旧的顶层玩家副本。客户端已有的远端 presentation cache 继续负责网络展示插值。相机使用本帧输入，避免世界插值带来的一个 tick 视角延迟；时间节拍见[运行时架构](runtime.md)。

CPU watchdog 的绝对单调时间、帧截止时间及 worker/task 起点统一使用 `int64_t`；
Windows 的 32 位 `long` 不可承载系统运行时长的微秒值。耗时、像素与工作量统计维持原合同，
正常帧预算仍为 200ms，未增加取钟或调度。取消的同步工作先 drain，再由 Core 退休帧资源，
不得呈现半帧；每个实际 world generation 的首个成功 CPU 呈现与预热边界见[运行时架构](runtime.md)。

GPU Scene 迁移中的 `rf_gpu_scene_extract.c` 只从冻结的 V2 snapshot 生成有序 Scene 元数据；
它不拥有资源、pose、pass 或正常帧提交。现行渲染仍由下述 mixed frame 数据流负责。
隔离 snapshot 的 actor 输入由调用者提供非零来源生命周期 `source_epoch`；身份 tracker 将来源 ID
与 epoch 共同用于 generation 延续判断，避免两次冻结之间 ID 复用被当成同一对象。epoch 不进入
玩法或网络真值，也不进入当前 snapshot 输出。`rf_gpu_scene_local.c` 适配 session 创建的八名非 hired
正式模块化队员：reset 完成时逐名登记创建，reset/unload 结束各自来源生命周期；计数器在 session load 清空
其他状态时保留。freeze 按来源 ID 查找当前 slot，仅复制展示值。非 client 的 `--frame-audit`
将其送入 snapshot 和 Scene 元数据提取；正常提交仍走 mixed。逐 actor local presentation sidecar 冻结
角色配置、动作时间、武器、状态、地面/腾空高度及各自的 lower-body 展示时钟。
`rf_gpu_scene_world.c` 通过 Runtime Map 的 authored render ID 和与 `level_map.draw` 共用的投影顺序，
在正常帧审计时冻结 world 元数据。air gate 开关与 platform 的可见性、alpha 在来源处求值；
snapshot 不持有 Runtime Map 指针。当前 V2 world 值仍只有身份、位置、alpha 和可见性；配套的
版本化 world render 值帧按值冻结 `toy_map_draw` 并与 V2 核对，首批 wall/box/ramp/style 2 platform
不透明网格的几何从该值帧提取，顶点光照仍查询当前 world-light 状态。独立 Scene world registry
按 world/map、V2 light bake 代际、完整冻结绘制值、地面输入与 object 值持有十一类网格，同代复用 handle；变化时重建，
已 pin 的旧代在帧退休后释放。静态 RMESH 按冻结 object 值和实例 V2 光照加载资产 profile，
复用 mixed 的模型可见性及材质判定，再由 Scene GPU cache 编码可见实例。数值预检区分整数兼容投影与
硬件裁剪：超出旧屏幕范围的静态实例只有在顶点整数变换安全时才进入硬件裁剪管线，mixed 的数值门禁保持不变。
独立正常帧审计已接 GPU cache 与离屏 Scene WORLD，
正常呈现仍为 mixed；地图 LABEL 与透明项尚未接入 Scene。
普通地图 `MODEL` 盒体的旧绘制使用独立 V1 诊断光照；Scene floor 值帧冻结该场，
第七类模型盒体网格按旧四边形/三角形结构采样它。其余 normal WORLD 仍以 V2 为光照来源。
SIGN 牌柱和带内嵌字色的单层牌面从同一冻结 render 值生成第八类网格，按旧路径逐三角形中心采样 V2。`facing` 只控制字形在牌面 X 轴上的方向。
展示模型 style 1、2、6、9、12 共用旧方块人/圆柱人形体目录，生成第九类网格；
特殊感染体 style 3–5 按旧 rig 的静态姿态、变换和面光照生成第十类网格。导入感染体展示模型 style 7–8、10–11、13–14 按旧 idle rig 姿态、CPU skinning、材质与逐三角形面光照生成第十一类网格。
三类展示模型逐三角形采样 V1 光照，与普通盒体共用冻结场。
隔离的 `rf_gpu_scene_pose_extract` 已从这些冻结值按角色配方生成 finalized palette、衣裤颜色与 gear/weapon placement；
其所有权和限制见[角色表现](character-presentation.md)。正常帧审计已将八名正式模块化队员的
body、被动装备及 AK 武器 pose 值接入各自独立的 Scene GPU 资源、skinning 和同一离屏 WORLD submit；
活动投射物也按玩法 slot 冻结位置、时间、闪烁与光照，并以共享 bomb/molotov 模型资源加入该 WORLD submit。
交互物按 session 槽位冻结类型、武器、位置、效果实例高亮与 V2 光照；冻结时沿用 PLAYING 和暂停的显隐条件。七类拾取模型与按钮、药瓶、弹药盒程序几何从该值帧进入同一离屏 Scene WORLD；静态 GPU 资源跨帧复用，特殊按钮底座按来源槽位和高度复用。交互物模型的固定采样点与 mixed 一致；程序几何当前按实例中心光照和共享形体绘制，其视觉容差仍待阶段 0 基线审批。
程序角色、网络玩家和补充模块化角色的同帧 WORLD 诊断及独立来源见 [角色表现](character-presentation.md)。武器消费冻结的 RMESH 原始坐标到世界变换，不使用被动装备的
position-scale 换算。

动态敌人身体及其阴影、舌头、死亡变换的审计值由 `render/rasterfall_enemy_rig.inc` 在旧 producer 的姿态求值后冻结，
Scene 只消费这些值生成刚性网格；共享几何枚举不再读取 gameplay、observer 或时钟。
来源、暂缓范围及诊断限制见[角色表现](character-presentation.md)。
普通感染体在同一值帧冻结 recipe 与已采样步态，独立 instance 提取身体几何；
材质双面标志与光照参数随三角形进入同一 WORLD 深度域。独立预览另走直接来源；动态资源复用仍待优化。

`rasterfall_render_bind()` 是既有串行 presentation context，只向旧 helper 提供 session/effects/net、
纹理和 world light；它不拥有 window、surface、present 或 Core 资源。并行模型录制优先使用
`toy_renderer.recording_context` 隔离 frontend state。

## 独立 Scene 来源

`--gpu-scene-independent-preview` 跳过本页下述旧整帧 producer 与录制流程。
runtime 共用 camera 的展示求值顺序，随后直接调用 Scene 值冻结、pose、资源准备与 native 提交。
Core 独立 begin 只获取窗口 extent，不创建 RasterCmd/mixed recording。
本入口覆盖地图、正式模块化队员、旗帜、投射物和交互物；敌人及其余玩法角色由独立只读 adapter
在同一冻结边界收集，所有权见[角色表现](character-presentation.md)。独立非 WORLD 来源由
`render/rf_gpu_scene_layers.inc` 提取，地图透明与死亡 alpha 进入有序透明段；天空、特效、
第一人称和 HUD 共享一次目标提交。完整性边界见活动计划，分层合同见 GPU 架构。
旧 `--gpu-scene-world-preview` 的 producer 捕获诊断保留，不能视作新路径来源合同。
资源和执行边界见 [GPU 架构](gpu-rendering-architecture.md)。

## 一帧的数据流

`--gpu-scene-world-preview` 是单独的开发路径：复用下面的 producer 求值以冻结展示输入，
Core 丢弃未提交的 mixed recording，WORLD owner 独立硬件绘制并 native present。
它只显示不透明 WORLD，完整帧其他层按活动计划接入；不使用 mixed 补画或 CPU framebuffer copy。

```text
Core begin / clear
  -> sky
  -> WORLD: scene, map, actors, world labels
  -> world/transparent ordering barrier
  -> EFFECTS
  -> VIEWMODEL (独立 depth/coverage domain)
  -> optional Post semantic boundary
  -> Core begin screen overlay
  -> OVERLAY: prompt, names, crosshair, HUD, menus, console, desktop
  -> Core final flush / present
```

层序固定为 SKY、WORLD、TRANSPARENT、EFFECTS、VIEWMODEL、POST、OVERLAY。调用位置不能隐式改变层；
`rf_core_render_frame_enter_layer_v1()` 的 cursor 拒绝跳层和回退，审计必须保持
`invalid_layer_transitions=0`。

Core 在 pre-post 各层收集原始 command，完成整帧可表达性判断后才允许 GPU native 路径。任何 unsupported
material/texture/edge/overlay、direct-pixel debt 或 consumer failure 都触发完整 CPU replay；禁止 world
已写 GPU、后续层却只写不会呈现的 CPU surface。screen overlay 开始后，Core 同时切换返回 surface 与
`renderer->surface`；GPU native 使用独立 XRGB8888 color + 8-bit coverage，再 source-over composite。

## Command 与透明语义

WORLD 可跨多次 flush 收集，再稳定分为 opaque、transparent 两段。透明保持原提交顺序，使用 source-over、
depth-test/no-depth-write；有效 alpha 为 `texel_alpha * material_alpha / 255` 向下取整。当前不做 OIT 或
自动深度排序。VIEWMODEL 使用相同透明像素合同，但拥有独立逆深度域和 coverage。

actor 屏幕裁剪由 `ai_actor_command_scope_*()` 拥有。command filter 必须在每次 flush 消费前处理当前段，
随后从新缓冲零位置继续；actor 结束处理尾段并解除 callback。producer 身份、层和透明 state 是不同概念，
不能互相替代。

Texture V1 每帧维护唯一 texture-view/descriptor/texel 表，以 pointer identity 去重并使用 1-based handle；
CPU pointer 不进入 ABI。禁止为每个 textured command 回扫全部历史 command。surface stride 在 Core 边界
从字节显式换算为 GPU API 使用的 32-bit 元素数。

## 世界几何与光照

地图文本、Runtime Map、碰撞体和可见几何彼此独立。`boundary_wall`、partitioned floor、地图 wall/box/
ramp/platform 和 static RMESH 在各自 record 入口做保守 frustum/AABB 剔除；穿越 near plane 的几何仍交给
逐三角形裁剪。地图 visual mesh 不替代 gameplay collision。

Static World Lighting V2 是 normal runtime 唯一 world-light 来源。renderer 只消费其 Q8 查询结果，按
`world light × form lighting × material policy` 形成最终提交颜色；normal runtime 的 fog 输入固定为 0。
field bake、固定参数和诊断例外由 [Static World Lighting V2](static-world-lighting.md) 拥有。

持久地图 mesh 的顶点 Y 已是世界高度，`persistent_map_instance` 以 `min_y` 为实例 Y，抵消通用
模型 Draw 的 foot-origin 归一化；floor mesh 的局部原点合同另行保留，不改通用 shader。
Scene 地面资源从冻结的地图范围、出生区和 map render 值重建分区地面，调用与 mixed
相同的分区、颜色覆盖及 1024 RFU 光照细分算法；审计路径目前只向独立 Scene target 绘制。

地图 LABEL 是无深度屏幕注记，`rasterfall_render_map_labels` 在 Core 进入 OVERLAY 后、HUD 前
输出到 overlay color/coverage；不再在未 flush 的 WORLD 表面直接写字。SIGN 牌面与文字仍是
WORLD 几何，参与遮挡。

static RMESH 使用实例 world origin 查询一次 scene light，再通过既有 override 传给模型提交；不在逐顶点
热循环重复查询。角色材质的 FACE/SKIN/EYES/HAIR visibility floor 属于 character render policy，非角色
RMESH 保持通用 form-lighting。资产颜色、材质 role 与导入合同不由 renderer 重新定义。

## Post 与 CPU reference

normal runtime 保持 Post disabled，presentation color 直接选择 raster color。底层 identity/fog fixture
可使用独立 `post_color` 验证 ABI；它随 extent 重建，不能与 Raster target 原位读写。保留的 fog command
和 Post Fog 只用于兼容与 differential，不是正常画面策略。

CPU renderer 是独立完整 reference。GPU partial replay、world stream capture 或 readback 只用于诊断，
不能宣称为完整 normal frame 或性能结果。可执行验证入口见
[视觉验收](../guides/visual-validation.md)和[渲染性能诊断](../guides/rendering-performance.md)。

## 修改落点

- 世界物体缺失或遮挡：场景对应 `render_*`、近裁剪、depth 与层 barrier。
- 地图几何或剔除：map projection/record 入口；不要修改碰撞真值。
- 光照、材质或纹理：world light consumer、model submission、Texture V1；资产解码在 `lib/assets.c`。
- GPU 执行、bridge、resource 或 present：转到 GPU 架构与 GPU 验收指南。
- 可见结果改变：补固定 capture 或像素 differential；并行路径同时比较单 worker 与多 worker。
