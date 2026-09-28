# 正常帧渲染 baseline 清点

> 状态：当前清点；默认观感仍待统一验收
> 所有者：Rasterfall presentation
> 最近核对：2026-09-27

本文清点正常游玩帧在 CPU 软件渲染与独立 GPU Scene 中都已接入的内容，并定义默认功能的**对外描述**。表中“已接入”不表示实现方式相同，也不表示整体观感已经逐项核对；统一工作和验收门槛见[活动计划](../plans/rendering-baseline-unification.md)。后端与执行方式见[GPU 渲染架构](../architecture/gpu-rendering-architecture.md)。

## 共同功能

| 范围 | baseline 语义 | 当前来源与边界 |
| --- | --- | --- |
| 相机和层 | FPS/RTS 展示相机；天空、世界、透明、特效、第一人称、屏幕 UI 的画面层序 | CPU 由 `rf_game_render_profiled` 提交；Scene 独立冻结并提交，不录制 RasterCmd。 |
| 地图与静态环境 | Runtime Map 的地面、墙、盒体、坡道、平台、边界墙、静态 RMESH、SIGN、MODEL 展示物及屏幕 LABEL；可见性、近裁剪和遮挡 | CPU 从 `level_map.draw` 绘制；Scene 从冻结的 map/world 值准备资源。玩法碰撞另有所有者。 |
| 世界光照与材质 | 正常世界使用 Static World Lighting V2；常规颜色、贴图、材质亮度与深度遮挡 | `MODEL` 展示物等 V1 诊断例外按[光照架构](../architecture/static-world-lighting.md)处理。此项不承诺两个光栅器逐像素相同。 |
| 透明 | 地图透明盒体/平台、死亡渐隐和有序透明特效，保留来源顺序及相应深度规则 | 两后端各自执行透明提交；是否覆盖具体内容仍需按正常帧镜头验收。 |
| 世界动态物 | 旗帜、投射物、可见交互物、拾取物和高亮 | Scene 使用只读冻结值；两后端各自提交几何，玩法真值仍由 Game/session 持有。 |
| 角色 | 正式模块化队员、其他程序角色、武器及附件；第三人称/RTS 可见本地角色 | CPU 使用现有角色 producer；Scene 使用独立 actor adapter 与 pose/几何提取。GPU Scene 正常入口当前仅限单人。 |
| 敌人 | 普通感染体与特感身体、动作、死亡展示及其附属表现 | CPU 与 Scene 使用各自的只读展示来源；具体阴影、舌头、渐隐和碎片以正常帧验证为准。 |
| 天空与特效 | 天空、弹道、枪口、粒子、爆炸、受击反馈及屏幕伤害效果 | SKY/HUD 有共享 canvas；特效读取同一 instance 语义，几何仍分别生成。 |
| 第一人称 | 手臂、武器、摆动/后座及本地枪口；RTS 或倒地时按现有规则隐藏 | 两后端消费共享 `rasterfall_viewmodel_geometry` 求值，使用各自的深度/覆盖执行。 |
| 屏幕 UI | HUD、准星、交互提示、AI 名称/状态、暂停、商店、结算、计分板、RTS 控件及前哨站指挥桌 | HUD 与正常状态布局已有共享入口；actor 名称、地图标签等局部投影仍分别实现。Desktop/Console 正常 gate 关闭。 |

上表冻结的是默认功能的**描述和语义边界**：切换后端不应改变默认角色身份、玩法位置、地图可见性、层序、透明遮挡、光照来源和 HUD 真值。CPU RasterCmd 与 Scene mesh/batch 是各自的执行格式，不定义共同 baseline。两后端不要求共用每一段几何实现；性能策略、资源缓存与像素采样也不是共同合同。同输入 capture 只用于发现明显缺项和整体观感问题，不要求逐像素一致。

## 未纳入共同 baseline

| 项目 | 当前状态 | 后续处理 |
| --- | --- | --- |
| 联机画面 | CPU runtime 支持 host/client；`--renderer gpu-scene` 参数检查限定单人 Runtime Map。 | Scene 联机覆盖完成前，不宣称跨后端共同功能。 |
| 旧动漫高模、PMX/VMD 正常帧表现 | `RASTERFALL_LEGACY_ANIME_RENDERING_ENABLED=0`；CPU 旧实现和离屏诊断仍在，Scene 独立来源将无模块化 recipe 的 AI 当程序角色。 | 如恢复，作为显式可选角色表现，先明确资产来源、pose/动画/武器/LOD 与两后端的输出合同，再纳入共同功能。不能仅打开旧宏。 |
| Desktop/Console | `RASTERFALL_DESKTOP_RUNTIME_ENABLED=0`；正常帧只显示暂不可用提示。 | 与渲染 baseline 分开恢复。 |
| 额外画质 | 实时阴影、PBR、GI、SSAO、normal map、toon/anime outline 等不属于当前 Static World Lighting V2。 | 逐项定义可选能力、默认值与后端支持；缺失能力不得静默改变 baseline。 |
| 开发入口 | WORLD-only preview、frame audit、模型/角色验收与离屏 fixture 有专用来源或画面范围。 | 只用来验证明确的局部合同，不当作正常帧功能清单。 |

## 当前实现关系与能力差异

| 功能 | 当前共享边界 | 独立实现或能力差异 |
| --- | --- | --- |
| UI、天空 | SKY/HUD 使用同一 canvas 布局；暂停、结算、准星、计分板和 RTS 状态使用 `rf_game_shared_ui_layout`。 | actor 名称与地图 LABEL 的位置/提交仍由两条路径分别处理。 |
| Viewmodel | `rasterfall_viewmodel_geometry` 统一手臂、武器和本地枪口的几何求值。 | CPU RasterCmd 与 Scene 三角形、深度/coverage 执行不同。 |
| 世界地图 | 两条路径读取同一 Runtime Map 绘制值；Scene 将其按帧冻结。 | 地面/物体网格、裁剪、材质和透明提交仍有独立实现。 |
| 角色、敌人 | 程序角色及部分敌人共享几何枚举；正式队员共用角色 profile、动作与附件合同。 | CPU 与 Scene 的 source/pose 历史、提取及资源提交仍分开。 |
| 特效 | 消费相同的展示 instance 池与效果语义。 | CPU 的 `rasterfall_render_effects` 与 Scene 的 `scene_layer_effects` 分别生成几何。 |
| 边线/高级材质 | CPU 提供 `--edge-pass`，正常游玩默认关闭。 | Scene 没有对应的正常帧边线能力；CPU 模型边线是显式实验高级项，不属于共同 baseline。 |

## 功能分级与后端能力原则

1. **基线功能**是正常游戏默认开启且 CPU、GPU Scene 都承诺提供的功能描述。两后端可以用不同绘制原语和优化方式；验收关注内容、动作、层序与整体观感大致相同，不要求同一几何实现或逐像素相等。
2. **高级功能**逐项定义效果、默认值、资源要求和支持状态，由用户显式启用。GPU Scene 可以先完成正式支持；CPU 可选择实验支持或不支持，不因 GPU 有该功能而承担同等实现与性能保证。
3. 支持状态至少区分 **正式支持、实验支持、不支持、尚未实现**。请求不支持或尚未实现的功能时，报告原因并保持既有配置；不得静默切换后端、替换成另一种效果，或把诊断实现当作正常功能。实验支持必须在界面/日志中可辨认。
4. 功能选择只影响 renderer/presentation，不修改 Game/session 真值。切换时在帧边界应用；涉及资源重建的功能先准备成功，再替换活动配置。默认基线不因一次高级功能失败而变成部分帧。
5. 大厅南入口西侧 `main_terminal` 已提供游戏内渲染控制面板。当前登记的可切换项只有 CPU 模型边线；面板从一份功能表读取描述和双后端支持状态，通过同一请求函数修改展示配置。后续带资源重建的功能仍须先准备成功、再在帧边界替换活动配置；不得在终端内复制资源生命周期或玩法状态。

### 现有开关核对

| 入口 | 当前事实 | 分级处理 |
| --- | --- | --- |
| `--renderer cpu|gpu-scene`、`--gpu-scene-play` | 选择后端；Scene 限单人 Runtime Map，并自动要求原生 GPU present。 | 后端选择，不是画质功能。 |
| `--textures` / `--no-textures` | 现有 CLI 纹理开关；Windows 默认开启，其他平台默认关闭。它主要传入旧 renderer context，不能据参数名认定 Scene 全部纹理都可按相同规则关闭。 | 先明确作用对象并验证双后端，再考虑进入终端；不能直接当作完整高级功能。 |
| `--edge-pass` / `--no-edge-pass` | CPU 模型边线正常游玩默认关闭；Scene 正常帧没有对应能力。 | 已移出共同 baseline。终端在 CPU 模式标为实验支持并可切换；Scene 模式显示不支持且不改配置。 |
| `--enemy-visual-family` | 显式覆盖敌人外观家族；CPU 与 Scene 独立敌人来源都读取该策略。 | 内容/外观策略，不等于画质等级；若进入终端，需单独描述视觉和资源变化，不修改玩法敌人身份。 |
| `--gpu-character-skinning-off`、`--gpu-character-vertex-diff`、`--frame-audit` | GPU 回退/差分/审计入口。 | 诊断或实现选择，不作为玩家可见的高级功能。 |
| `--input-test`、`--no-stats`、坐标轴/FPS 调试显示 | 输入、性能或开发者观察入口；坐标轴当前在 CPU 旧画面层单独绘制。 | 调试功能，排除在默认画质和高级画质清单之外；未来若要跨后端显示需另定合同。 |
| PMX/VMD、完整 toon/anime outline、实时阴影、PBR、GI 等 | 旧动漫正常帧关闭；CPU 有限的模型边线由上面的 `--edge-pass` 表示，完整效果及后列项目尚未构成当前正常帧功能。 | 候选项，逐项实现和验收后才登记为可选高级功能。 |

## 选择与验收

当前无参数启动选择 CPU；`--renderer gpu-scene`（或 `--gpu-scene-play`）选择独立单人 Scene 并要求 GPU 原生呈现。两后端默认关闭 CPU 专有模型边线；共同内容和观感仍按活动计划验收。Scene 初始化或提交失败应报错，不能把半帧当作成功呈现。

冻结后的改动按风险抽查：相同 world、相机、游戏状态和资源下，两条正常路径的内容有无、遮挡/层序、透明、角色动作、HUD 与世界切换；先运行最近的验证，涉及 native 提交时按[Windows Native](../guides/windows-native.md)检查窗口及 present。像素差分只用于定位具体缺陷。
