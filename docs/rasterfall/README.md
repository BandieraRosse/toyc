# Rasterfall 维护者入口

> 状态：当前
> 所有者：Rasterfall
> 事实入口：`windows/NativeCodex.ps1`、`build/rasterfall --help`
> 最近核对：2026-09-23

Rasterfall 是本仓库的长期开发主线。本页只负责把任务导向事实所有者；不记录阶段进度、性能数字或完整
参数表。用户构建和启动说明见 [`../../rasterfall/README.md`](../../rasterfall/README.md)，全仓库文档
规则见 [`../repository/documentation.md`](../repository/documentation.md)。

## 开始任务

整数方向接口与误差界见[整数方向数值合同](architecture/gameplay.md#整数方向数值合同)，
展示旋转保护见[角色表现](architecture/character-presentation.md#展示方向与旋转保护)，
异常缓冲、严格模式与日志关联见[数值异常定位](guides/gpu-scene-fixture.md#数值异常定位)。

一米网格地板、动画场西南扩展和中央敌人召唤终端见[网格玩法场](reference/experiment-labs.md#一米网格玩法场)；
地图由 `tools/grid_lab.py` 维护，session 拥有召唤身份与枪手推进意图，UI 位于 `rf_grid_terminal.inc`。
多枪手交战、出网格回看与角色 GPU 失败日志见[网格交战诊断](guides/gpu-scene-fixture.md#动态角色与设备实验)。

地图目录、校园地图退役与稳定 world ID 由[地图与世界内容](architecture/maps-and-world-content.md)维护；
旧地图和正式地图的首次缓存加载诊断从[光照指南](guides/gpu-lighting.md#固定光照烘焙与对照)进入。

策略对抗场的 96×72m 空间、出生遮挡和低效远射/受压转移见[战术 AI 架构](architecture/tactical-ai.md)；
武器全威力距离、射速和飞行弹迹表现见[共享战斗](architecture/combat.md)。

动态实验场的创建、暂停、替换、销毁、正式 actor 播放控制与可越过玻璃围栏，从
[动态实验场合同](reference/experiment-labs.md)进入；session 所有权见
[运行时架构](architecture/runtime.md)，固定负载与真实战斗采样见[性能诊断](guides/rendering-performance.md#前哨站游戏内性能实验场)。

角色加速公共合同、Block／普通感染体常驻网格、Humanoid 共用上传事务和多视图消费从
[统一角色加速](architecture/gpu-rendering-architecture.md#统一角色加速)进入；
求值缓存与实例隔离见[动画架构](architecture/animation-architecture.md#scene-批量求值与-ik-更新范围)。
RFCHAR／RF-C01 已接入的上传路径与 CPU、分块、主/AUX 纹理复用范围见
[RFCHAR 加速边界](architecture/gpu-rendering-architecture.md#rfchar-加速与纹理复用边界)；
跨实例 GPU 纹理共享与硬件采样评估留待[后续事项](plans/README.md#rfchar-加速后续)。

程序 Block 角色的常驻网格、骨骼与武器变换、主/AUX 资源复用从
[GPU 渲染架构](architecture/gpu-rendering-architecture.md#程序角色常驻几何)进入；
冻结姿态和职业装备来源归[角色表现](architecture/character-presentation.md#block-标准骨架)。

光照性能优化从[同包对照指南](guides/gpu-lighting.md#保持画面的光照性能对照)进入，探针权重复用与
深度分段灯表归[光照架构](architecture/gpu-lighting.md)，WORLD 深度预通道归[GPU 渲染架构](architecture/gpu-rendering-architecture.md#scene-资源与同步)。
固定太阳、天空与设施灯的烘焙、表面缓存和跨启动失效从[固定光照烘焙](architecture/gpu-lighting.md#固定光照烘焙)
进入；提前生成与实时对照见[烘焙操作](guides/gpu-lighting.md#固定光照烘焙与对照)。
接收缓存 FP16、静态/动态区域与三维灯表同由光照架构维护；蒙皮与主/AUX 消费者合并提交、
姿态缓存生效、当前姿态边界、主/AUX 独立可见性与取消退休由 GPU 渲染架构维护。
CPU 完整输入求值复用归[动画架构](architecture/animation-architecture.md#scene-批量求值与-ik-更新范围)，
片元单次不透明查询、固定灯整接收域遮挡掩码与失效键归光照架构；对照轴与 WORLD 缓存读取消融见同包对照指南。
加载变慢、跨启动管线缓存与辅助镜头首次 GI 准备从[入图预热与多视图所有权](architecture/gpu-rendering-architecture.md#入图预热与多视图资源所有权)
进入；计时范围见[启动界面合同](reference/boot-interface.md)，加载对照见[光照指南](guides/gpu-lighting.md#加载与首次辅助镜头准备)。
fast 楼梯缓存边界、地面遮挡合并与换图失败诊断由[光照架构](architecture/gpu-lighting.md)维护；
真实运动和整图灯具改色/关闭/恢复见[原生运动与固定灯变化](guides/gpu-lighting.md#原生运动与固定灯变化)。
低成本室内照明的默认 fast 与显式 reference 从[三模式对照](guides/gpu-lighting.md#低成本间接光候选对照)进入；
接收空间缓存、几何归属和失效由[光照架构](architecture/gpu-lighting.md#可选接收空间缓存)维护。

前哨站 B1、二层、屋顶和北侧楼梯改动先读 [前哨站合同](reference/outpost-hall-v1.md)，再读
[地图格式](reference/map-format.md)的楼层与有限厚度楼板约束；结构由 `tools/outpost_storeys.py` 维护。
标准房屋、天花板独立底面颜色、墙板接缝、门洞、玻璃窗坐标转换和折返楼梯编写见[建筑生成语法](reference/architectural-environment-v1.md#标准房屋生成语法)，
入口为 `tools/building_kit.py`；物体顶面、窄护墙、楼梯跳跃与跨层落地见[碰撞高度区间](architecture/gameplay.md#component-collision-高度区间)，玩法判定入口为 `lib/game.c`。
玩家速度、地面加减速、反向变向、斜向限速、跳跃惯性、离边/落地起跳容错及空中微调见[玩家地面移动与跳跃](architecture/gameplay.md#玩家地面移动与跳跃)；可编辑参数见[玩家移动配置](guides/player-movement-config.md)，由 Game actor 持有运动状态，session 只提交方向输入并保留启动配置。
单位互相阻挡、圆形接触滑行、高度区间与静态导航隔离见[常态单位体积碰撞](architecture/gameplay.md#常态单位体积碰撞)；入口为 `lib/game_unit_collision.inc`，普通游戏和策略实验共用 Game 身体规则。

| 任务 | 先读 | 主要代码或工具入口 |
| --- | --- | --- |
| 当前优先级与延期项 | [计划入口](plans/README.md) | 当前决策与活动计划；退役历史见[归档](archive/gpu-compute-retirement/README.md) |
| Windows 原生环境、package、实机验收 | [Windows Native](guides/windows-native.md)、[构建与平台](guides/build-platforms.md) | `windows/NativeCodex.ps1`、`windows/Makefile` |
| 按模块导出 AI 阅读源码集合 | [源码集合](guides/build-platforms.md#按模块导出源码集合) | `scripts/merge-rasterfall.sh`；总索引、七份源码集合与独立文档集合 |
| 原生分辨率、无边框全屏、窗口切换与 UI 缩放 | [运行时架构](architecture/runtime.md)、[玩家界面](guides/player-ui-v2.md) | `rasterfall_options.c`、`rf_core_host.c`、`windows/src/window_sdl.c`；显示尺寸与玩家 UI 布局分离 |
| 启动、参数、主循环、CPU 首帧预热与取消帧退休、120 FPS 展示节流与 60 Hz 逻辑插值、Core Host | [运行时架构](architecture/runtime.md)、[GPU 架构](architecture/gpu-rendering-architecture.md) | `src/rasterfall.c`、`src/rf_game_runtime.c`、`src/rasterfall_render.c`、`src/rf_core_host.c` |
| Firmware、RF Boot Manager、Workbench、Shell 与 Diagnostics | [启动界面合同](reference/boot-interface.md)、[运行时架构](architecture/runtime.md) | `src/rf_boot_ui.c` 页面与操作、`src/rf_boot_canvas.c` 共享绘图元素与扫描合成；Core 初始化事件、GPU 独立启动计时及 Scene 首帧事件、自动/手动启动、硬件诊断与渲染器选择 |
| 音量设置、枪声强度、简单空间音效与混音线程 | [音频架构](architecture/audio.md)、[设置与验证](guides/audio.md) | `rasterfall_audio.c`、`rasterfall_audio_engine.inc`、`rf_audio_menu.inc`、`rasterfall/lib/sfx.c`；统一分组、偏好保存、来源并发和 FPS/RTS 监听 |
| 物理键、动作默认绑定与输入边沿 | [运行时架构](architecture/runtime.md#物理键与动作绑定) | `include/rf_input_bindings.h`、`src/rf_input_bindings.c`、`windows/src/window_sdl.c` |
| Desktop、Application、GUI 与只读投影 | [Application Runtime](architecture/application-runtime.md) | feature gate、`rasterfall_app.c`、`rf_application_projection.c` |
| 玩法、session、AI、战斗与无金钱拾取规则 | [玩法架构](architecture/gameplay.md) | `lib/game.c`、`src/rasterfall_session.c`；普通武器直接装备，旧商店及付费操作已移除 |
| 策略算法接口、路线与暴露查询、简单/普通难度攻防 | [算法框架](architecture/tactical-ai.md#策略算法框架-api-3)、[路线暴露与计费](architecture/tactical-ai.md#路线暴露与工作计费)、[运行指南](guides/tactical-lab.md#新机械算法与攻防对打) | `include/rf_ai.h`、`lib/rf_ai.c`、`lib/rf_ai_mechanical.c`、`lib/rf_ai_normal.c`、`lib/rf_ai_tactical.inc`；低成本快照、完整路线、分级敌方射界采样、确定性工作预算、互斥动作 |
| 新枪战 AI 框架、种子对抗图、进攻/防守与联合动作 Beam Search | [战术 AI 架构](architecture/tactical-ai.md)、[实验指南](guides/tactical-lab.md)、[枪械基准](reference/tactical-weapons.md) | `lib/rf_tactical.c`、`lib/rf_tactical_solver.c`、`lib/rf_tactical_beam.c`、`include/rf_tactical_prediction.h`、`lib/rf_tactical_weapon.c`、`src/rf_tactical_cli.c`；正式 Game actor、只读信息层与隔离 Game 预测，动态算法语言暂缓 |
| 大厅部署 AI 对抗/靶场、下拉终端、RTS 观战、玩家试射与统计 | [游戏内实验所有权](architecture/tactical-ai.md#游戏内实验地图)、[操作指南](guides/tactical-lab.md#游戏内入口与终端) | `lib/rf_tactical_lab.c`、`src/rf_tactical_session.inc`、`src/rf_tactical_terminal.inc`、`src/rf_tactical_effects.inc`、`tools/tactical_maps.py`；Game 统一权威、session 实验编排、正式 actor 表现与事件统计 |
| 可编辑玩法参数、波次、基地、推搡/近战、投掷物与回避 | [玩法配置](guides/gameplay-config.md)、[玩法架构](architecture/gameplay.md) | `config/gameplay.cfg`、`include/toy_gameplay_fields.inc`、`lib/game_gameplay_config.inc`；启动解析，session 换图保留，Game 实例拥有规则 |
| 四至五人自动小队、窄门纵列、领队接替与 RTS 命令优先级 | [自动小队](architecture/gameplay.md#可选普通角色分队)、[RTS 指挥架构](architecture/rts-command.md) | `lib/game_actor_squad.inc`、`lib/game_actor_ai.inc`、`rf_frontier_session.inc`；Game 保存有界移动意图，session 绑定正式成员 |
| 首个边缘站点、三方战斗任务与设施回收 | [首图计划](plans/frontier-station-01.md)、[开发任务书](reference/frontier-station-01-task.md)、[操作与验证](guides/frontier-station-01.md)、[地图与世界内容](architecture/maps-and-world-content.md) | `assets/maps/frontier_station_01.map`、`assets/worlds/frontier_station_01.content`、`rf_frontier_mission.c`、`rf_frontier_session.inc`；任务权威与设施由 session 持有 |
| 网格编织机、真实蓝图成本、四柱八头机构、出料与机器音频 | [普通机合同](reference/mesh-weaver.md)、[制造实验指南](guides/mesh-weaver.md)、[玩法架构](architecture/gameplay.md#网格编织机制造权威) | `lib/game_mesh_weaver.inc`、`src/rf_mesh_weaver_presentation.c`、`src/rf_mesh_weaver_runtime.inc`、`src/rasterfall_audio_weaver.inc`、`render/rf_mesh_weaver_gpu.inc`、`tools/mesh_weaver_blueprints.py`、`tools/mesh_weaver_assets.py` |
| 战斗固定预设、现场 FPS/RTS 实验与种子结果 | [战斗实验场](guides/combat-lab.md)、[运行时架构](architecture/runtime.md) | `src/rf_combat_lab.inc`、`tools/combat_lab.ps1`；同一现场/脚本生命周期、owned 对象清理、真实伤害/回避统计与 native 规模采样 |
| 标准枪手、飞行子弹、距离衰减与简化回避 | [共享战斗能力](architecture/combat.md)、[本轮记录](archive/combat-ai-20261008.md) | `lib/game_combat.inc`、`lib/game_ballistics.inc`、`src/rasterfall_bullet_effects.inc`；Game 拥有弹丸、FPS/RTS 统一效果、主机确认命中 |
| 敌人共享导航场、复杂地形与逻辑帧时间 | [玩法架构](architecture/gameplay.md#敌人共享目标导航场)、[历史计划](archive/gpu-scene-renderer.md#共享目标导航场) | `lib/game_navigation.inc`、`lib/game.c`、`include/toy_game.h`；分层节点、共享指路记录、持续移动与低预算搜索纠错；旧集团仅作诊断对照 |
| FPS/RTS 切换、俯视相机、玩家跟随与地面传送 | [运行时架构](architecture/runtime.md)、[玩法架构](architecture/gameplay.md) | `src/rf_game_runtime.c`、`src/rasterfall_session.c`；runtime 拥有跟随/选点，session 验证地面并更新 actor |
| RTS 楼层、屋顶剖切、跨层指令与车间二层 | [楼层架构](architecture/rts-command.md#建筑与楼层-v1)、[地图格式](reference/map-format.md#楼层与有限厚度楼板)、[首图操作](guides/frontier-station-01.md#车间楼层-v1) | `rf_rts.h/.c`、`rf_rts_runtime.inc`、`game_navigation.inc`、`tools/frontier_station_map.py`；主视图楼层状态只读，完整车间外墙、内部折返楼梯与高处任务枪手 |
| RTS 核心指挥、全屏框选编组、双路镜头与独立队友 | [RTS 指挥架构](architecture/rts-command.md)、[操作与验收](guides/player-ui-v2.md#rts-核心指挥)、[辅助视图](architecture/gpu-rendering-architecture.md#通讯镜头单位镜头与设备预览) | `rf_rts.h/.c`、`rf_rts_runtime.inc`、`rf_rts_ui.inc`、`rf_rts_portrait.c`；正式 actor 个体命令、剧情/单位镜头独立，前哨道路三组测试队员，旗帜不控制部署 |
| 地图格式、Runtime Map、World Content | [地图与世界内容](architecture/maps-and-world-content.md)、[地图格式](reference/map-format.md)、[地图编辑](guides/map-authoring.md) | map parser/runtime、projection adapter、布局工具 |
| 大厅下拉选图、点击预览框后台加载、静态大厅背景、估算进度与部署移交 | [预加载生命周期](architecture/runtime.md#指挥桌预加载与出生点预览)、[GPU 辅助视图](architecture/gpu-rendering-architecture.md#通讯镜头单位镜头与设备预览) | `src/rf_table_panel.inc`、`src/rf_table_preview.inc`、`windows/src/window_sdl.c`、`rasterfall_session_adopt_map()`；独立 session 与离屏 owner，后台独占场景、主线程加载 UI |
| 世界渲染与帧分层 | [渲染架构](architecture/rendering-architecture.md) | `src/rasterfall_render.c`、Core layer/flush |
| GPU 天空、体积云与前哨站气氛实验区 | [GPU 天空架构](architecture/gpu-sky.md)、[天空验证](guides/gpu-sky.md)、[设计研究](reference/sky-v2-design-study.md) | `gpu/shaders/sky.glsl`、`graphics_sky.comp`、`render/rf_gpu_scene_lighting.inc`、`tools/gpu_sky_study.ps1`；`atmosphere-lab` 固定镜头与园区静态建筑，CPU 旧天空保留在 `src/rasterfall_sky.c` |
| CPU/Scene 默认功能、高级能力与大厅渲染控制终端 | [正常帧渲染 baseline](reference/rendering-baseline.md)、[GPU 架构](architecture/gpu-rendering-architecture.md)、[Outpost V1](reference/outpost-hall-v1.md) | `src/rf_render_terminal.inc`、`src/rf_game_runtime.c`、`src/rf_core_host.c` |
| 独立 GPU Scene 与直接来源预览 | [计划入口](plans/README.md)、[GPU 架构](architecture/gpu-rendering-architecture.md)、[Scene 工作流](guides/gpu-scene-fixture.md) | `--gpu-scene-independent-preview`、`rf_core_begin_scene_frame`、`render/rf_gpu_scene_enemy_source.inc`、`render/rf_gpu_scene_actor_source.inc`、`render/rf_gpu_scene_layers.inc`；直接冻结 WORLD 与分层几何 |
| GPU 实时光照、天空漫反射、太阳反弹、物理单位与 DDGI | [GPU 光照架构](architecture/gpu-lighting.md)、[实验与验证](guides/gpu-lighting.md)、[嵌灯生成语法](reference/architectural-environment-v1.md#标准房屋生成语法) | `gpu/src/rf_gpu_lighting.inc`、`rf_gpu_architecture_ray.inc`、`rf_gpu_architecture_light.inc`、`rf_gpu_indirect.inc`、`gpu/shaders/graphics_indirect.comp`、`graphics_environment.comp`、`graphics_light_tiles.comp`、`render/rf_gpu_scene_lighting.inc`、`tools/gpu_light_meter.py`、`tools/gpu_daylight_check.py`；独立自然光场、lm/cd/lux 与曝光分离、完整固定灯表、建筑查询与局部实体缓存、有限探针重定位、两次漫反射、分段/计数诊断 |
| CPU 静态世界光照遗产（停止维护） | [光照架构](architecture/static-world-lighting.md)、[验证指南](guides/static-world-lighting.md) | 保留旧实现，不作为新增光照或视觉验收目标 |
| 角色、敌人与附件表现、AI 随机静止持枪、肩托瞄准与低位移动持枪、双臂握点、反冲与回避叠加 | [角色表现](architecture/character-presentation.md)、[动画架构](architecture/animation-architecture.md) | character/enemy presentation adapters；`rasterfall_rifle_pose.c` 共享待机轮换/瞄准/移动持枪/避让/双臂 IK、modular additive、可选指节链、目录动作展示与 Scene 冻结姿态 |
| 敌人资源、姿态与固定截图 | [敌人视觉合同](reference/enemy-visuals.md)、[生成验收](guides/enemy-visuals.md) | 感染体家族、特感刚性 profile 与复现入口 |
| 动态敌人 Scene 身体、死亡与附属表现诊断 | [角色表现](architecture/character-presentation.md)、[Scene fixture](guides/gpu-scene-fixture.md) | `rf_gpu_scene_enemy.h`、`render/rasterfall_enemy_rig.inc`、`render/rasterfall_enemy_visual.inc`、`tools/gpu_scene_enemies.ps1`, `tools/gpu_scene_play.ps1 -Stage Combat`；特感及六种普通感染体同帧冻结、独立提取及共享 WORLD 深度 |
| 普通感染体步态采样与来源拆分 | [角色表现](architecture/character-presentation.md)、[活动计划](plans/README.md) | `rasterfall_enemy_visual.h`、`rasterfall_infected_sample_motion`；显式历史和时间的只读采样接口 |
| HUD、Viewmodel 与特效；独立 Scene 分层和单人入口 | [HUD 与特效](architecture/hud-effects.md)、[GPU 架构](architecture/gpu-rendering-architecture.md)、[Scene 工作流](guides/gpu-scene-fixture.md) | `--gpu-scene-play`、`tools/gpu_scene_play.ps1`、`rasterfall_canvas.h`、`rasterfall_actor_labels.h`、`render/rf_gpu_scene_layers.inc`；共享菜单与友军标签布局、独立几何与 native 生命周期 |
| 玩家 UI、暂停菜单与控制/开发者子页、FPS/RTS 地图、主题与可扩展窗口 | [HUD 与布局](architecture/hud-effects.md)、[玩家命令](architecture/player-commands.md)、[使用与验收](guides/player-ui-v2.md) | `rf_player_ui.h/.c` 的主题/布局与 FPS 紧凑透明卡片、武器侧视网格缓存；`rf_minimap.h/.c` 的坐标/缓存、`rf_player_panels.h/.c` 的窗口组件；业务不随页面重建 |
| NULL 行为、自动剧情、换图清理、顶部独立计时字幕、聊天历史、任务与通讯镜头 | [剧情与通讯](architecture/story-and-comms.md)、[GPU 子镜头](architecture/gpu-rendering-architecture.md)、[使用与验收](guides/player-ui-v2.md) | `rf_story.h/.c` 的触发策略/台词计时/状态/实体身份、`rf_game_lifecycle.c` 的换图演出清理、`rf_player_ui.c` 的字幕队列、`rf_player_runtime.inc` 的计时与输入、`rf_player_panels.c` 的视频及顶部四行字幕、`render/rf_gpu_scene_aux.inc` 的真实世界采样；会话与视角独立 |
| 视觉验收与渲染性能诊断 | [视觉验收](guides/visual-validation.md)、[性能诊断](guides/rendering-performance.md) | capture CLI、`rasterfall_perf`、有界 slow AUX 同帧归因与可选 COUNTER 稳态筛选、离屏 benchmark、`tools/gpu_scene_old_map_perf.ps1 -Stage Corridor` 旧地图按钮整波与真实时钟诊断；`tools/storey_performance.py` 另测前哨站跨层固定步长尾与规划等待 |
| 实验园区多模型、投影合批与正常帧性能 | [GPU 架构](architecture/gpu-rendering-architecture.md)、[园区采样](guides/rendering-performance.md#实验园区正常场景采样) | `tools/gpu_outpost_perf.ps1`、`src/rf_scene_performance.inc`、`render/rf_gpu_scene_layers.inc`；逐面颜色/透明度、独立主视图/阴影剔除、正常 native 呈现下五轮对照 |
| 实验区复合定义、RF 第一代电子组件与产品展区、像素屏幕、分区导视与展示独占 | [实验区合同](reference/experiment-labs.md)、[渲染架构](architecture/rendering-architecture.md)、[地图格式](reference/map-format.md)、[地图编辑](guides/map-authoring.md) | `tools/experiment_lab.py`、`tools/lab_computer.py`、`tools/rf_electronics_lab.py`、`tools/blender/generate_lab_computer.py`、`src/rf_experiment_labs.inc`、`src/render/rf_electronics_geometry.inc`、`src/render/rasterfall_machine_screen.h`、`src/render/rasterfall_lab_terminal.h`；B1/C1/M1/X1 装配、可调转速与闪灯、产品展台、透明侧板、后部接口、固定 RFU 像素 |
| 前哨站实验区总平面、道路、填充铺装与四角投影 | [实验园区 V3](reference/outpost-lab-layout-v3.md)、[标准地块铺装](reference/experiment-labs.md#标准地块铺装与角标) | `assets/maps/outpost.map`、`tools/experiment_lab.py`、`src/render/rasterfall_lab_terminal.h`；工作区与规划地块分离、三类铺装、信标底座碰撞与投影旋转浮动、道路并集与性能观察支带 |
| 前哨站性能实验、真实枪手对照、五人跨层与首图巡检 | [实验场操作](guides/rendering-performance.md#前哨站游戏内性能实验场)、[真实负载](guides/rendering-performance.md#live-真实负载预设)、[运行时架构](architecture/runtime.md) | `src/rf_performance_lab.inc`、`src/rf_performance_live.inc`、`tools/gpu_performance_lab.ps1`；独立/环境/完整背景、同一现场与脚本固定步、交战帧与路线完成结果、正式地图和 content |
| 通用 Scene 准备优化、多视图共享、入图预热与快照缓存 | [GPU 架构](architecture/gpu-rendering-architecture.md)、[性能诊断](guides/rendering-performance.md) | `src/render/rf_gpu_scene_layers.inc`、`src/rf_gpu_scene_world.c`、`src/rf_gpu_scene_world_gpu.c`、`src/rf_gpu_scene_native.c`、`gpu/src/rf_gpu_vulkan_graphics.inc`；多 reader 退休、地图代际预热、首图六份增援资源备用池、双镜头错峰、有界显示缓存、姿态/敌人几何复用、校验来源槽的敌人容量保留及其稀疏驻留取舍；`tools/gpu_player_ui_perf.ps1` 双镜头对照与预热记录 |
| GPU Scene/present 架构与退役边界 | [GPU 渲染架构](architecture/gpu-rendering-architecture.md)、[活动计划](plans/README.md) | `gpu/`、Scene graphics、Core Host |
| GPU Scene 只读 snapshot | [GPU 渲染架构](architecture/gpu-rendering-architecture.md)、[历史迁移接口](archive/gpu-scene-interface.md)、[角色表现](architecture/character-presentation.md) | `src/rf_gpu_scene_identity.c`、`src/rf_gpu_scene_frame.c`、`src/rf_gpu_scene_extract.c`、`src/rf_gpu_scene_local.c`、`src/rf_gpu_scene_world.c`、`src/rf_gpu_scene_world_gpu.c`、`src/render/rf_gpu_scene_pose.inc`、`src/rf_gpu_scene_native.c`；独立 Scene 冻结 world、角色与动态来源；旧正常帧审计记录见历史迁移接口 |
| 硬件 Scene WORLD 原生预览 | [Scene 工作流](guides/gpu-scene-fixture.md)、[GPU 架构](architecture/gpu-rendering-architecture.md) | `--gpu-scene-world-preview`、`tools/gpu_scene_preview.ps1`；真实 WORLD 独立 native 提交，供定向诊断 |
| 程序角色、网络玩家与阶段 2 WORLD 诊断 | [角色表现](architecture/character-presentation.md)、[Scene fixture](guides/gpu-scene-fixture.md) | `rf_gpu_scene_procedural_triangles`、`tools/gpu_scene_stage2.ps1`、`tools/gpu_scene_network.ps1`；共享 WORLD 深度、双面材质及帧内网络表现 |
| GPU 验收、诊断与性能采样 | [GPU 验收与诊断](guides/gpu-validation.md)、[Scene 同步与生命周期验证](guides/gpu-scene-fixture.md)、[GPU 性能标准](reference/gpu-performance-standards.md) | `tools/gpu_*.ps1`、Windows native package |
| 独立 Scene 运行成本、敌人几何、颜色合批及资源复用 | [历史计划](archive/gpu-scene-renderer.md)、[GPU 架构](architecture/gpu-rendering-architecture.md)、[Scene 工作流](guides/gpu-scene-fixture.md) | `tools/gpu_scene_cost.ps1`、`tools/gpu_scene_cost_report.py`、`rf_gpu_graphics_scene_color_resource_create`、`rf_gpu_graphics_scene_color_resource_update`；同包比较唯一顶点变换、20 字节颜色顶点、逐面颜色合批、上传、蒙皮与分层资源复用，拆分来源及 native 提交成本 |
| Scene 静态实例数值边界与近裁剪 | [GPU 渲染架构](architecture/gpu-rendering-architecture.md)、[Scene fixture](guides/gpu-scene-fixture.md) | `rasterfall_render_scene_static_prop_eligible`、`rf_gpu_scene_world_gpu_prepare`、`scene_static_prop_clip` |
| GPU Scene 专用渲染地图与定向复现 | [渲染 fixture](guides/gpu-scene-fixture.md) | `assets/maps/gpu_scene_render_fixture.map`；显式选择，不替换正式地图 |
| 模型、蒙皮、动画求值 | [动画架构](architecture/animation-architecture.md) | `src/rasterfall_model.c`、RFANIM/RFCHAR runtime |
| Block 方块身体、标准骨架与动作迁移 | [Block 标准骨架](architecture/character-presentation.md#block-标准骨架)、[资产导入](guides/asset-pipeline.md#block-队友身体) | `render/rasterfall_block_character.inc`、`tools/blender/generate_rasterfall_block.py`；完整人形骨架、固定骨长、CPU/Scene 共用求值 |
| 资产导入、LOD、检查器与静态道具 PBR 常量 | [资产导入与诊断](guides/asset-pipeline.md) | `tools/assets/`、inspect CLI、离屏诊断、`tools/blender/test_static_pbr.py`；`prop_surface_profiles.py` 与 `refresh_prop_surfaces.py` 持有现役套件表面绑定和刷新；RFM2 v2 金属度/粗糙度进入 Scene 静态道具 draw |
| Blender/RF 角色保真、单表面眼球、基础色贴图与材质对照 | [保真诊断](guides/character-fidelity.md)、[GPU 架构](architecture/gpu-rendering-architecture.md) | `tools/gpu_character_fidelity.ps1`、`rfchar_precision_audit.py`、RFM2 v15/MAT1 与 clamp/mip 纹理；目录动作台位及左右侧握持检查 |
| 武器真实尺寸、文件轴向与握点适配 | [武器模型适配](reference/weapon-model-adapter.md)、[动画架构](architecture/animation-architecture.md) | `rasterfall_calibration.c` 的物理长度、模型 adapter 与接触帧；CPU/Scene 共用转换 |
| 角色与附件资产合同 | [character-assets.md](reference/character-assets.md) | RFCHAR、RFM2、attachment、skinning validator |
| 新角色包合同、材质能力与接入缺口审计 | [合同草案](reference/character-package-v1.md)、[资产工作流](guides/asset-pipeline.md#新角色接入前的能力清点) | `tools/assets/rfchar_audit.py`、`tools/assets/rfchar_material_contract.py`；源清点与材质元数据校验，不替代完整资产合同或游戏验收 |
| 人形精修、可组装衣裤护甲与头部装备、职业外观验收 | [美术验收指南](guides/character-art-acceptance.md)、[角色表现](architecture/character-presentation.md) | `tools/rf_combat_character_round.py`、`tools/rf_clothing_round.py`；共享身体、独立蒙皮衣裤、覆盖区隐藏与刚性装备，AI 动作区组合台位 |
| 私有角色头脸、头发与身体的独立创作和组装 | [分部件创作架构](architecture/character-authoring.md)、[工作流](guides/character-parts.md)、[当前设计候选](reference/rf-c01-design-study.md)、[V25d 创作记录](archive/character-art-v025d-20261003.md) | `tools/blender/rf_character_parts.py`、`rf_parts/`；锁定部件、头壳接口、肩袖和发束拓扑迁移、动作/表情采样与固定视图审阅 |
| 新一代角色体系、私有动漫内容与 GPU 实验场接入 | [暂停的角色计划](plans/private-anime-character-gpu.md)、[实验场预览](guides/gpu-scene-fixture.md#rf_model_lab-角色预览)、[角色资产合同](reference/character-assets.md)、[GPU 架构](architecture/gpu-rendering-architecture.md) | `--gpu-normal-scene model-lab 0`；分块 GPU 蒙皮、不透明贴图和双手持枪已接通；完整角色包、morph、透明发片与 LOD 按活动计划控制 |
| 联机协议、快照、预测与测试 | [联机架构](architecture/network-architecture.md)、[网络测试](guides/network-testing.md) | `src/rasterfall_net.c` |
| 资源来源、许可、发布 | [资源来源台账](reference/asset-sources.md) | 资源台账和发布前检查 |
| 环境资产创作风格、材质与工业组件 | [环境资产艺术约束](reference/environment-art.md)、[工业组件规格](reference/industrial-props.md)、[生成指南](guides/industrial-props.md) | 调色、轮廓、纯色常量 PBR 与局部标牌、能力边界、预算和生成入口 |
| 建筑套件、管线端口和墙地表面 | [建筑套件规范](reference/architectural-environment-v1.md)、[生成指南](guides/architectural-environment-v1.md) | 建筑资产、连接合同与复现入口 |
| 前哨站多层基地与设施家具 | [Outpost V1](reference/outpost-hall-v1.md)、[活动计划](plans/README.md)、[生成与验证](guides/facility-assets.md) | `assets/maps/outpost.map`、`assets/worlds/outpost.content`、`tools/outpost_storeys.py`、`tools/building_kit.py`、`tools/facility_round.py`；墙板与门洞连接、封闭折返楼梯、护墙顶面、四向大厅与设施翼 |
| 前哨站指挥桌互动与地图屏幕 | [Outpost V1](reference/outpost-hall-v1.md)、[运行时架构](architecture/runtime.md) | `src/rf_outpost_table.inc`、`src/rf_game_runtime.c`；地图文件预览、鼠标部署与暂停菜单回站 |
| 南侧矩形露天实验区、展示开关与六类感染体 | [Outpost V1](reference/outpost-hall-v1.md)、[角色表现](architecture/character-presentation.md) | `assets/maps/outpost.map`、`src/rf_game_runtime.c`、`src/render/rf_outpost_showcase.inc`、`src/render/rf_gpu_scene_enemy_source.inc`；共享实机步态、12 台静止/原地移动展示及 3 条 Humanoid 往返步行线，CPU/Scene 共用冻结值 |
| 东侧 AI 队友动作台位、实机闲置切换与六条往返线 | [Outpost V1](reference/outpost-hall-v1.md)、[角色表现](architecture/character-presentation.md) | `assets/maps/outpost.map`、`src/render/rf_outpost_actor_showcase.inc`、`src/render/rf_gpu_scene_actor_source.inc`；Block 全动作、Humanoid 已有动作、逐台位持枪历史与两种外观乘三级 AI 的往返展示 |
| 三种 AI 模型的瞄准、射击与低位移动射击循环 | [循环区入口](guides/gpu-scene-fixture.md#三模型持枪循环区)、[实验区合同](reference/experiment-labs.md)、[角色表现](architecture/character-presentation.md) | `--gpu-normal-scene rifle-cycle-lab 0`、`src/render/rf_outpost_rifle_cycle.inc`、`tools/rifle_cycle_lab.py`；Block / Humanoid / RF-C01 同步角度、端点转身与往返路线 |
| 三种角色与五种枪械的握点、持枪动作和循环对照 | [五枪循环区](guides/gpu-scene-fixture.md#三角色五枪循环区)、[武器适配](reference/weapon-model-adapter.md)、[动画架构](architecture/animation-architecture.md) | `--gpu-normal-scene weapon-cycle-lab 0`、`render/rf_outpost_weapon_cycle.inc`、`tools/weapon_cycle_lab.py`、`tools/gpu_weapon_cycle.ps1`；15 组同步待机、瞄准、射击、低位移动与转身，支持逐组近景 |
| 实机 AI 随机静止持枪、移动与索敌中断实验 | [实机持枪实验](guides/gpu-scene-fixture.md#真实-ai-持枪实验区)、[实验区合同](reference/experiment-labs.md) | `--gpu-normal-scene idle-rifle-lab 0`、`src/rf_idle_rifle_lab.inc`、`tools/idle_rifle_lab.py`；真实 AI、正式部署与战斗、正常 CPU / Scene 表现 |
| Research BX18 环境、设备与物理终端 | [Research V1](reference/research-renovation-v1.md)、[生成验收](guides/research-assets.md) | `tools/blender/generate_research.py`、`tools/research_round.py`；独立静态 RMESH 族、粗碰撞与房间镜头 |
| Host 机柜、CPU / Memory 硬件展示 | [Host Rack V2](reference/host-rack-v2.md)、[地图与世界内容](architecture/maps-and-world-content.md) | `tools/host_rack_layout.py`、`rasterfall_prop.c`、`toy_platform.h`；八个候选柜、硬件选柜及逐槽实时展示 |
| 临时校园套件与合成场景 | [校园套件规格](reference/temporary-campus-kit-v0.md)、[生成验收](guides/temporary-campus-kit-v0.md) | 视觉资产、拼接与离屏复现；历史审阅见 [阶段记录](archive/temporary-campus-kit-v0.md) |
| 武大资料、真实空间依据 | [reference/return-to-whu-core/](reference/return-to-whu-core/) | 来源台账、调查报告、白盒计划 |
| 已完成或撤销的现场 | [archive/](archive/) | 只作历史证据，不作当前设计依据 |

## 可执行事实

优先以这些入口取得当前事实：

- `build/rasterfall --help`：运行参数的完整清单。
- `build/rasterfall --logic-test`：玩法、session、地图和网络逻辑回归。
- Visual capture、model views、character acceptance 和 world capture：角色、模型及真实 world render。
- `build/glb-inspect ... contract`、`build/rfchar_runtime_test <model.rmesh>`：资产与 runtime 契约。
- `make map-layout`、`tools/map_layout_query.py`：地图布局导出和精确空间查询。
- `windows/NativeCodex.ps1 gpu-test`、`tools/gpu_scene_play.ps1`、`tools/gpu_metrics.ps1`：Scene 正确性、生命周期和性能证据。

退出码、标准输出、日志和生成物属于可复核事实。文档与实际行为不一致时，先检查 Makefile、脚本、参数
解析和调用入口，再修正文档。

## 所有权主线

```text
平台事件、网络包
    → process / Core Host / Game runtime 编排
    → single-player、host、client session
    → actor API 与确定性 Game 状态
    → 只读 gameplay projection
    → renderer / HUD / audio / network presentation
```

权威玩法状态归 Game；模式编排归 session；渲染和 HUD 只读玩法或展示状态；客户端预测、校正和插值属于
网络展示链。地图文本、Runtime Map、玩法投影、碰撞和渲染不得互相替代。资产坐标、bind pose、动画求值
和末端渲染补偿也必须保持分层。

## 修改后的文档联动

| 改动 | 同步检查 |
| --- | --- |
| 模块职责、状态所有权或主数据流 | 本页和受影响的架构文档 |
| 构建、脚本、诊断或验收方式 | 相应 guide |
| 地图、资产、动画或协议格式 | 相应 reference、生产者与消费者 |
| 当前执行顺序 | 只更新 `plans/README.md` 和其活动计划 |
| 完成、撤销或被替代的阶段记录 | 移入 `archive/` 并写明当前替代入口 |

新增跨模块功能必须补充本页的任务路由。稳定导航中不维护测试数量、单次性能数字或完整参数表。
