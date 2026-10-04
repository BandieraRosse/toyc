# 玩家界面 V2 使用与验收

> 状态：当前开发版本
> 事实入口：`rf_input_bindings.c`、终端 `help` 与各命令组

从 Windows native 启动：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 run --skip-boot --renderer gpu-scene
```

默认玩家界面。F2 或反引号打开或关闭玩家终端，F3 不再切换界面，M 切 FPS/RTS，
G 放大地图已取消。Enter 打开/收起聊天记录，Esc 收起，上下键或滚轮查看历史；剧情自动推进，不再用 Z 回答或 H 收起视频。
数字键在 FPS 选择武器槽，在 RTS 召回编组。提示由当前物理动作绑定生成，运行中改绑后不依赖这些默认字母。
普通通讯允许继续移动和射击；主动展开聊天记录才占用指针。设备和终端关闭后，已按住的键鼠需先释放，
再重新向玩法产生输入，避免关闭窗口时误开火或移动。

网格编织机附近按交互键打开窗口，Tab/上下键移动可见焦点，确认键执行；左右键旋转真实模型。
选择不制造，开始按钮明确执行。关闭或收起后制造继续；领取会说明被替换武器，确认时再次校验。
工程详情保留精确几何和消耗。实验模式保留原工程面板。

## 分辨率与显示模式

Windows 普通游玩默认无边框全屏，跟随当前显示器桌面分辨率；1920×1080 屏幕使用原生 1080p。
F11 随时切换普通窗口与无边框全屏，保留游戏会话和窗口位置。默认恢复窗口为 1280×720。
`--windowed` 显式选窗口；`--window-size 1600 900` 设置窗口客户区大小并选择窗口模式；
其后追加 `--fullscreen` 可让游戏全屏启动、恢复时使用该窗口尺寸。全屏不会切换显示器分辨率。
有限帧、`--frame-audit` 和 Scene 诊断默认保留窗口，验证全屏时显式加 `--fullscreen`。

玩家 HUD、地图和设备面板继续以 720p 为设计基准，1080p 默认按 1.5 倍绘制，命中和 GPU 子镜头
使用同一套布局。`--ui-scale 125` 在自动尺寸上再乘 125%，覆盖本次加载的 UI 缩放偏好；
运行中也可在玩家终端输入 `ui scale 100` 恢复默认比例，合法范围以 `--help` 和终端提示为准。
现有经典/实验界面仍保留自身布局，不使用玩家 UI 百分比。缩放不改变相机 FOV、玩法尺寸或逻辑步长。

本轮构建、全屏往返和命中验证范围见[1080p 原生记录](../archive/2026-10-04-display-1080p.md)。

## HUD、聊天与共享地图

FPS/RTS 地图均在左下角，上方保留区域名称。地图框大小一致，FPS 显示附近尺度，RTS 切换到全图尺度。
FPS 右下角使用高透明的紧凑状态卡与武器卡，沿用 RTS 配色和边框；常规宽度下贴底并排，窄窗或大字号下上下排列。
FPS 底部操作提示与 RTS 主信息控制栏等宽对齐，右侧两卡相应收窄，文字自适配且武器侧视图保持比例。
状态卡保留生命、回避与药品；武器卡显示透明背景的真实模型侧视网格、武器名和弹药，换弹时增加细进度条。
任务目标在区域名称上方，仅一行字，无背景。
剧情视频为固定 2:3 竖框，贴近屏幕左侧、与地图左边缘对齐，位于 RTS 地图上方并避开任务文字；
通讯镜头改为水平全身取景，让人物占据画面大部分高度，头顶与脚底留少量余量。
FPS/RTS 切换和战斗时位置尺寸不变，移除 `-`/`X` 按钮；默认关闭，演出期间打开，结束自动关闭。
台词以「说话者: 内容」显示在顶部居中，FPS/RTS 使用相同的半屏宽、四行字幕区，白字配深蓝灰字影，活动字幕不显示整块底板。
新台词从下方出现并把旧台词向上挤；活动台词持续显示，结束后独立快速淡出，不被新消息延长。
无语音时暂按约每秒四个中文字估算时长，当前两段没有选项或继续按钮。
信息栏下方不显示按键提示，Enter 打开信息栏的说明位于 FPS 底部提示。
Enter 展开后以淡蓝灰底提示焦点，可在同一区域查看本进程的聊天历史，
没有进行中的剧情也能打开。关闭聊天恢复正常操作，演出期间台词保持可见。
当前两段介绍每局各触发一次，同局离开再进入区域或重建世界不会重播；重新启动可再次播放。
台词按现实语速估时后自动推进，包含固定耗时和标点停顿；计算规则见[剧情与通讯](../architecture/story-and-comms.md)。

RTS 右侧“收起底栏 / 展开底栏”只改变展示，不清除单位选择、移动目标、制造或通讯。
收起时隐藏底栏选择与命令区域，保留地图、区域名称、任务与底栏展开入口。
实际点击、截图及验证范围见[本轮验收记录](../archive/2026-10-04-player-ui-v2-native.md)。

FPS 默认小地图显示附近区域，北向上、玩家箭头随朝向变化；RTS 地图显示全图边界。
两种模式地图框同尺寸；FPS 显示玩家视线锥，RTS 显示当前屏幕的地面覆盖范围框。
两者共用地形缓存、目标和可见性规则，不扫描显示全部敌人。RTS 地图内左键把
观察相机移到对应地点并取消跟随，右键向当前选择提交移动请求，目标仍由 session 校验。
地图及底栏面板内的点击由 UI 消费，不继续落到后面的世界选择。普通玩家底栏不提供实验传送。

## RTS 核心指挥

M 进入 RTS。左键点选或拖动框选友军，Shift 点选/框选追加；空地点选取消选择。
底栏上方有十个编组槽，`1…9、0` 召回，`Ctrl + 数字` 用当前选择完整重设对应组；没有选中单位时清空该组。
编组条也可点击，Ctrl 点击执行同样的重设/清空。一名队员可同时属于多个组，不额外限制编组人数。
十格从左到右对应 `1…9、0`，只显示组内人数。

单选左侧显示随健康状态变色的身体网格，右侧显示名称、武器、生命、回避和弹药。
多选显示单位卡片，悬停查看简要信息、点击改为单选；较多成员可滚轮或按钮翻页。
所选单位脚底有绿圈，右键移动显示各自终点及连线，X 停止整个选择；WASD 和滚轮仍控制主观察镜头。
信息与操作区之间的单位镜头实时拍摄当前主选队员，始终从队员正前方固定距离跟随取景，画面铺满卡片；GPU Scene 支持视频，CPU 显示不可用。
收起底栏停止该视频，保留选择与指令。镜头与多人编组均不改变 AI 的战斗、受伤或动画规则。
单位镜头与剧情镜头同时显示，切换或取消单位选择不影响剧情视频。

前哨站实验区北侧入口道路常驻三支正式 AI 小组，从西向东为步枪、狙击、霰弹枪，各三人。
它们按 `outpost.content` 的 `rts_*` formation 生成，正常游戏启动即可选择与编组，不需要打开展示台。
所有 AI 队友已独立于旗帜位置与寻路；旗帜仍可保留、购买和携带。

架构和身份失效规则见 [RTS 核心指挥](../architecture/rts-command.md)。完成 native test/stage 后运行：

```powershell
python tools/rts_command_check.py --output tmp/rts-command
python tools/rts_command_check.py --physical-mouse --fullscreen --story --gesture-ms 20 --output tmp/rts-fullscreen
python tools/rts_command_check.py --physical-mouse --toggle-fullscreen --gesture-ms 20 --output tmp/rts-f11
python tools/rts_command_check.py --boot --fullscreen --physical-mouse --story --output tmp/rts-normal-boot
```

该工具需要 Windows 桌面鼠标权限；只向它启动并取得前台的 SDL 窗口投递输入，失焦停止。
输出包括逐步审计、GPU 截图和真实退出码；它不代替长时间性能与全部窗口组合验收。
`--fullscreen` 使用游戏真正的桌面全屏；`--toggle-fullscreen` 从窗口进入全屏，并检查往返切换后的选择。
系统鼠标路线使用真实坐标与按钮事件，短手势检查同批按下/松开；`--story` 另验证双视频及取消选择后剧情保留。
`--boot` 让 Boot Manager 自动选择后端和默认地图，先确认首个游戏帧已有窗口焦点，再允许脚本操作。
该路线不能同时指定显式渲染器；否则运行时会跳过 Boot Manager，无法覆盖直接启动 exe 的输入生命周期。

## GUI 与终端映射

| 图形操作 | 结构化接口 / 终端 | 只读查询 | 权限与校验 | 验证 |
| --- | --- | --- | --- | --- |
| 模式/缩放/透明度 | `RF_PLAYER_SET_*`；`ui mode player|terminal|experiment`、`ui scale N`、`ui opacity N` | `ui` | USER；范围校验、独立保存 | 重启偏好、布局检查 |
| 蓝图选择 | `RF_PLAYER_WEAVER_SELECT`；`weaver select pistol` 等真实 ID | `weaver list`、`weaver` | USER；真实设备、距离、代际 | GUI/终端交替 |
| 制造 | `RF_PLAYER_WEAVER_START`；`weaver start` | `weaver` | USER；资源、尺寸、忙碌、serial | 重复提交拒绝 |
| 电力/CPU/X1 | `RF_PLAYER_WEAVER_POWER/CPU/X1`；`weaver power|cpu|x1 0|1` | `weaver` | USER；提交时校验距离，不补充能源 | 断供/恢复保持进度 |
| 领取 | `RF_PLAYER_WEAVER_COLLECT`；`weaver collect [confirm]` | `weaver` | USER；成品身份、旧武器确认、距离 | 无重复领取收益 |
| RTS 选择/移动/停止 | `RF_PLAYER_RTS_*`；`rts select N`、`rts move X Z`、`rts stop` | `rts` | USER；离线、目标地面、有效选择 | 视角切换保持目标 |
| 跟随/视角 | `RF_PLAYER_RTS_FOLLOW/VIEW`；`rts follow 1`、`rts view fps|rts` | `rts` | 只变展示；RTS 离线 | 不清空会话/制造 |
| 通讯回答 | `RF_PLAYER_DIALOG_ANSWER`；`comms answer N SESSION NODE_REV` | `comms` | USER；双版本校验，N=0确认结语 | 旧回答拒绝 |
| 收起/恢复/结束 | `RF_PLAYER_DIALOG_COLLAPSE/CLOSE`；`comms hide|resume|close` | `comms`、`comms history` | USER；保留/释放按会话规则 | 停止视频、释放控制 |
| 任务/通知 | 共享只读状态 | `task`、`notices` | USER；不泄漏未知敌人 | 真实事件驱动 |
| 渲染开关 | `RF_DEVICE_RENDER_SET`；`render set outline 0|1` 等能力 ID | `render status` | USER；按当前后端支持状态校验 | 显式设置幂等 |
| 指挥桌选择/部署 | `RF_DEVICE_TABLE_SELECT/DEPLOY`；`table select|deploy outpost|campaign|whu` | `table maps`、`devices status` | 部署复核桌旁距离/高度、离线与世界代际 | 队列单次消费 |
| 重播/重置 | `RF_PLAYER_STORY_REPLAY/RESET`；`comms replay 101|102`、`comms reset 0|101|102` | `comms` | ADMIN；显式实验模式 | 两段与全部回答分支 |

## 验收入口

以下原生交互/布局脚本仍包含旧回答和手动收起路线，尚未适配自动演出；不能据此声明新交互已通过实机验收。
当前自动推进、每局去重及结束释放由 `rf_story_logic_test` 覆盖，竖框取景和主题状态卡布局由原生截图及实际游玩确认。

先完成 native build/stage，再按改动选择最近的路线。以下 GPU 进程串行执行，性能期间停止构建、转码和其他 GPU 工作。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 test
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_player_ui.ps1 -Stage All
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_player_ui.ps1 -Stage Boundaries
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_player_ui.ps1 -Stage Remote
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_player_ui_layout.ps1 -Case All
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_player_ui_perf.ps1 -Rounds 2 -Samples 360 -OutputDirectory tmp/player-ui/performance-new
```

| 入口 | 实际范围与输出 |
| --- | --- |
| `gpu_player_ui.ps1 -Stage All` | Outpost、复用存档的 Resume、Weaver、Remote、三条 Boundaries；逐路线保存审计、真实输入、退出码与截图 |
| `-Stage Boundaries` | 终端、通讯回答焦点、编织机、渲染面板的持续键鼠跨关闭；普通通讯运动/射击、重复显隐、设备只读查询与权限/距离拒绝；不包含主动窗口失焦专项 |
| `-Stage RenderBoundaries` | 只跑渲染设备 GUI 设置、关闭输入边界与终端读取同一设置 |
| `-Stage Remote` | 真实实验区起点触发远端 B，若 A 在先则用正常回答结束；核对距离、NULL actor/generation、实时刷新、任务与结束释放 |
| `gpu_player_ui_layout.ps1 -Case All` | 1280×720、1920×1080、960×720 且字号 150%；FPS/RTS、折叠/恢复、通讯、终端、渲染设备与 Weaver，等待视频 live-ready；输出仍需人工看图 |
| `gpu_player_ui_perf.ps1` | 同 exe 的 player/experiment FPS/RTS、真实预览、近处通讯显示/隐藏/关闭和远处显示/隐藏；反转轮序、预热、哈希与分位数，capture 和逐帧审计关闭 |

布局可用 `-Case 720p`、`1080p`、`Narrow` 单独重跑，`-OutpostOnly -CommsOnly` 只复拍普通通讯。
`-OutpostOnly -HudOnly` 跳过需要前台鼠标的底栏折叠操作，保留 FPS/RTS、视频、聊天、终端和设备键盘路线。
聊天路线检查 Enter 不开火或回答、十秒无新内容隐藏、重新展开历史、剧情结束后仍可查看，以及窄窗长记录上下滚动。
脚本设置 SDL DPI awareness，并核对实际 PPM 宽高，
不能只看请求窗口尺寸。交互脚本的 `-Stage Resume` 需传入 `-ResumeDirectory`，指向此前已完成 Outpost 的
证据目录；`-NoScreenshots` 仅关闭截图，不把带审计的输入路线变成性能采样。
性能脚本要求新的输出目录；原始旧 exe 未保留时，experiment 只能称为同版本兼容基线。
CPU 线程记账粒度、同步辅助提交和分位数口径见[性能诊断](rendering-performance.md#玩家界面与辅助镜头)。

三个脚本均支持 `-CheckOnly` 检查脚本/辅助类型而不启动 GPU，例如：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_player_ui.ps1 -CheckOnly
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_player_ui_layout.ps1 -CheckOnly
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_player_ui_perf.ps1 -CheckOnly
```

交互和布局脚本发送 Windows 按键消息并经过正常 SDL 路由；边界路线用真实鼠标事件，布局路线用前台守卫、
真实光标定位与窗口鼠标消息。布局首次激活点击未进入 SDL 时只重试一次，审计仍需与截图对照。
脚本等待结构化只读状态并请求原生截图，不直接赋值制造阶段或对话节点。
性能脚本在预热阶段通过类型化命令设置场景，采样期间仅观察正常运行状态。
每次新建证据目录，等待进程真实退出并保存 stdout/stderr/操作记录。
`RF_UI_SAVE_PATH` / `RF_STORY_SAVE_PATH` 可把测试存档隔离到临时文件；`RF_UI_NO_SAVE` 禁止持久化。
固定场景诊断默认关闭剧情，显式 `RF_UI_STORY=1` 才开启；正常游玩自动检测真实区域。

视频属于 GPU Scene 功能，CPU 界面显示明确不可用状态并保留文本会话。通讯仅一个可见镜头，
设备真实预览优先占用辅助画面；关闭设备后恢复通讯采样。新增性能/视觉结果进入本轮现场记录，
不得把独立逻辑通过写成真实窗口验收通过。

当前构建、逻辑、输入、视觉与性能的实际范围统一查看[2026-10-04 原生验收记录](../archive/2026-10-04-player-ui-v2-native.md)，
后续安排查看[活动计划](../plans/player-ui-v2.md)。该记录保留未测项与失败尝试，不代表 V01–V16 全通过。

## 后续扩展入口

| 调整内容 | 所有者与维护入口 |
| --- | --- |
| 颜色、间距、缩放、HUD 排布和 RTS 折叠 | `rf_player_ui.h/.c` 的 theme、布局配置与 `rf_ui_layout_resolve`；[HUD 与布局](../architecture/hud-effects.md#玩家界面-v2-的展示契约) |
| 设备/通讯窗口、文字流与命中区域 | `rf_player_panels.h/.c` 的 layout/hit/draw；与 HUD 共用基础组件，视频内容区保留透明；[玩家命令与展示](../architecture/player-commands.md) |
| 地图图层、标记和交互坐标 | `rf_minimap.h/.c` 与 `rf_player_ui_map_view`；[共享地图服务](../architecture/hud-effects.md#共享地图服务) |
| 新设备操作或终端命令 | `rf_player_commands` / `rf_device_commands` 的类型化 request、只读 query 与执行时校验；[权限和所有权](../architecture/player-commands.md#请求与权限) |
| 剧情节点、触发、任务与镜头机位 | `rf_story.h/.c` 的稳定 ID、数据表和 camera profile；[剧情与通讯](../architecture/story-and-comms.md) |
| 真实镜头、模型预览、刷新频率与 GPU 缓存 | `render/rf_gpu_scene_aux.inc` 与 Scene 辅助请求；[通讯镜头与设备预览](../architecture/gpu-rendering-architecture.md#通讯镜头与设备预览) |

优先在这些展示入口调整布局和效果；新业务动作先扩展共享查询/执行，再连接按钮与终端。窗口开关、颜色和
资源缓存不进入 `toy_game`，也不复制剧情会话。当前身体朝向并非独立头颈/眼球 look-at；完整世界存档另属后续系统范围。
