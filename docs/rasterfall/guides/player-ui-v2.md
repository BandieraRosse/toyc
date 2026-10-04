# 玩家界面 V2 使用与验收

> 状态：当前开发版本
> 事实入口：`rf_input_bindings.c`、终端 `help` 与各命令组

从 Windows native 启动：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 run --skip-boot --renderer gpu-scene
```

默认玩家界面。默认 F3 循环玩家/终端/实验，反引号临时打开或关闭玩家终端，M 切 FPS/RTS，
G 按住展开地图。Enter 打开/收起聊天记录，Esc 收起，上下键或滚轮查看历史，左右键或 Tab 选择，Z 回答，H 收起/恢复视频；
数字武器槽仍保留。提示由当前物理动作绑定生成，运行中改绑后不依赖这些默认字母。
普通通讯允许继续移动和射击；主动进入回答焦点才占用指针。设备和终端关闭后，已按住的键鼠需先释放，
再重新向玩法产生输入，避免关闭窗口时误开火或移动。

网格编织机附近按交互键打开窗口，Tab/上下键移动可见焦点，确认键执行；左右键旋转真实模型。
选择不制造，开始按钮明确执行。关闭或收起后制造继续；领取会说明被替换武器，确认时再次校验。
工程详情保留精确几何和消耗。实验模式保留原工程面板。

## HUD、聊天与共享地图

FPS/RTS 地图均在左下角，上方保留区域名称。FPS 显示附近的小尺度地图，RTS 切换到较大的全图地图。
FPS 玩家状态位于右下角，武器 HUD 在其上方；任务卡在地图与区域名称上方。
剧情视频在屏幕左侧，台词及玩家回答以「说话者: 内容」显示在屏幕中间偏下的聊天区域。
被动聊天背景几乎透明，十秒没有新内容后消失；Enter 展开时背景加深，可查看本进程的聊天历史，
没有进行中的剧情也能打开。关闭聊天恢复正常操作，视频和等待回答的剧情不因聊天超时而结束。
当前两段介绍每次进入对应区域都触发，重启也会再播放；停留在区域内不会循环。一次性触发策略仍保留供后续内容使用。

RTS 右侧“收起底栏 / 展开底栏”只改变展示，不清除单位选择、移动目标、制造或通讯。
收起时隐藏底栏选择与命令区域，保留地图、区域名称、任务与展开入口；按住地图展开动作仍可临时查看整张地图。
实际点击、截图及验证范围见[本轮验收记录](../archive/2026-10-04-player-ui-v2-native.md)。

FPS 默认小地图显示附近区域，北向上、玩家箭头随朝向变化；RTS 与展开地图显示全图边界。
两者共用地形缓存、目标和可见性规则，不扫描显示全部敌人。FPS 展开地图用于查看；RTS 地图内左键把
观察相机移到对应地点并取消跟随，右键向当前选择提交移动请求，目标仍由 session 校验。
地图及底栏面板内的点击由 UI 消费，不继续落到后面的世界选择。普通玩家底栏不提供实验传送。

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
