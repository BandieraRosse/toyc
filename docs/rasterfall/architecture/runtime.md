# 运行时与主循环

> 状态：当前
> 所有者：Rasterfall Game Runtime 与 Core Host 生命周期
> 事实入口：`rasterfall/src/rf_game_runtime.c`、`rasterfall/src/rf_core_host.c`
> 最近核对：2026-09-23

GPU 的当前验收与生命周期门槛见 [GPU 验收与诊断](../guides/gpu-validation.md)；Desktop/Application 的当前冻结边界见 [Application Runtime](application-runtime.md)。

连接等待页与启动菜单也经过 Core 的逐帧层状态初始化、空 WORLD/EFFECTS/VIEWMODEL 和正式 OVERLAY，
使 GPU-required 在联机握手期间仍能准备 native target。菜单文字写入 Core 返回的 overlay/coverage surface。
显式 `--frame-audit` 包含冷资源和同步 Scene 提取，关闭 renderer 的交互式 200 ms watchdog；专项脚本
负责进程超时。普通运行预算不变，审计时间不作为低扰动性能结果。

暂停菜单的当前页、选中项及滑条拖动归 runtime 的展示输入状态；主菜单、控制、音频和开发者子页
共用布局与命中几何，绘制只读状态。子页返回恢复对应主菜单入口，玩法输入在关闭时沿既有抑制机制处理。
玩家操作和键盘视角灵敏度见[暂停菜单与控制设置](../guides/player-ui-v2.md#暂停菜单与控制设置)。

音频设备与混音、设置偏好、空间监听和暂停试音见[音频架构](audio.md)；玩家操作见[音频设置](../guides/audio.md)。

## 状态所有者

动态实验内容由 session 的 `rasterfall_experiments` 拥有。基础 Runtime Map 保持独立，
活动片段经 `rf_map_runtime_compose` 合成、投影并通过一次成功提交替换；失败保留原来的地图与实体。
组内 actor/enemy 只持稳定 ID 和 generation，创建、指定动画、正常 AI、碰撞、伤害沿正式 Game 接口运行。
冻结和指定动画是 Game 上的显式播放控制，渲染只读其时钟与姿态；组销毁不能依赖 visibility 开关。
仅角色变化通过显式 created/removed 事件更新实例 epoch，不改地图代际、不重建静态光照。
地图片段提交使用新 Scene world generation，使旧绘制与阴影资源进入既有退休路径。session reset/unload 负责清空组，
UI 只拥有待应用配置、下拉和输入状态。合同与场地映射见[动态实验场](../reference/experiment-labs.md)。

`src/rasterfall.c` 是可执行程序入口，负责命令行解析并组装 `rf_game_config`；
`src/rf_game_runtime.c` 负责 Game facade 的运行调度，Core Host 由 `rf_core_config` 接收启动参数。
`struct rf_game_runtime` 集中拥有
session 外的网络、摄像机、输入边沿、特效、HUD/菜单控制、perf/debug、音频和 fixed-step
运行态；`rf_game_update()` 与 `rf_game_render()` 是 Game facade 的更新/渲染入口，具体玩法规则
仍下沉到 session/game。

Runtime Environment 的上层边界保持分层：Core 拥有平台资源及 service 生命周期，Game Runtime
拥有 update/render 调度，Command Runtime 拥有 registry、context、output 与 permission，GUI Runtime
拥有 GUI context 和窗口生命周期，Application Runtime 由 runtime 持有 app manager，Projection Layer
只生成独立 snapshot。Map/level 的加载、绑定、reset 和 unload 仍由 `rasterfall_session` 持有；这些层
只能借用或读取对应接口，不复制 gameplay、session、Core service 或地图真值。

当前 normal runtime 的 Desktop/Console feature gate 为关闭：Game 初始化不创建 GUI/app manager，主循环不打开、更新或渲染它们。保留的 console/gui/app/projection 源码与逻辑测试仅用于隔离诊断，不属于 normal GPU frame 的语义集合；F12、反引号和 station terminal 交互统一通过现有 HUD banner 报告暂时不可用。

首个边缘站点由前哨指挥桌的正常部署请求进入，返回使用既有世界请求与完整 session load/reset/unload。
Runtime 只处理输入、场景切换、HUD 和通讯投影；阶段、敌人配额和设备权限归 session 的局部任务导演，
边界见[玩法架构](gameplay.md#边缘站点-01-的局部任务权威)。启动和运行中的世界请求均拒绝联机模式进入该站点。
FPS/RTS 共享任务，切模式不重启整备、刷怪或通讯。暂停不调用权威 fixed step，因此不消耗任务和字幕逻辑时间。
首图失败后 Esc 仍打开正常暂停菜单；暂停展示优先于结果面板，菜单导航不要求 Game 仍处于游玩状态。
返回前哨沿同一世界切换链清理任务；恢复只关闭菜单，不撤销失败或恢复角色生命。

换图与重试通过真实世界生命周期清除上一任务计时、待生成配额、实体代际、界面请求和通讯镜头请求。
正常入口不依赖开发终端或任务驱动器。只读审计与截图请求可以观察正常运行，不能代替玩家命令、
授予设施控制或改变任务结果；实机验证需区分正常输入、纯逻辑 fixture 与渲染诊断。

V1 checkpoint 和版本化原型设计见 [Runtime 历史设计](../archive/runtime-design-v0/README.md)，不作为当前执行顺序。

关键配套文件：

- `src/rasterfall_options.c` / `include/rasterfall_options.h`：命令行默认值、解析和 usage。
  玩家运动启动配置从磁盘读取，提交 Game 纯解析器验证，进程入口在进入 runtime 前拒绝非法配置；
  `--movement-config-check` 打印解析结果后退出。session 保存启动 policy，地图重建不会丢失，见
  [玩家移动配置](../guides/player-movement-config.md)。
  通用玩法配置 `gameplay.cfg` 由相同启动层读取，`--gameplay-config` 选择文件，
  `--gameplay-config-check` 输出全部解析值与范围后退出。Game 纯解析器验证整份配置，
  session 保存 policy，换图与重试在世界 actor 创建前应用；见[玩法配置](../guides/gameplay-config.md)。
- `include/rf_game_lifecycle.h` / `src/rf_game_lifecycle.c`：`rf_game_runtime` 状态上下文及
  `rf_game_init/update/render/shutdown` facade。
- `src/rf_game_runtime.c`：fixed-step facade 的 gameplay/session/network/effects 更新、world/HUD/debug
  渲染、菜单与输入边沿处理；其中 `rf_game_update()` 是唯一 gameplay update 入口，
  `rf_game_render()` 是 steady-state world/presentation/UI submission 入口，
  `rf_game_runtime_run()` 只编排 Core lifecycle、平台输入/网络轮询、fixed-step 节拍和 render。
  `rasterfall.c` 不再访问 Game 状态，也不再把 `argc/argv` 传入 runtime。
- `include/rasterfall_camera.h`：共享摄像机数据结构。
- `include/rasterfall_units.h`：网络和玩法共用的单位换算。
- `src/rasterfall_console.c`、`include/rasterfall_console.h`、`src/rasterfall_calibration.c`：RF Terminal session、Developer Console frontend、RF Command Runtime V0.1 output/metadata/permission 与持枪姿态校准。
- `src/rasterfall_gui.c`、`include/rasterfall_gui.h`：保留的 Desktop 原型、icon hit testing、window state 与文本 presentation；当前边界见 [Application Runtime](application-runtime.md)。
- `src/rasterfall_app.c`、`include/rasterfall_app.h`：Application Runtime 的注册、open/close、update/render 与默认 application。
- `src/rf_application_projection.c`、`include/rf_application_projection.h`：Application Projection 的只读 Core/Game 查询与 personnel snapshot。
- `src/rf_gpu_scene_identity.c`、`include/rf_gpu_scene_identity.h`：GPU Scene 迁移中的隔离 presentation 身份状态；
  按来源身份和 world generation 跟踪 actor 生命周期，允许连续存活的 actor 更换输入 slot；
  从调用者给出的展示值构建只读 snapshot，尚未接入正常帧或修改玩法/网络状态。
- `src/rf_gpu_scene_frame.c`、`include/rf_gpu_scene_frame.h`：隔离的 V2 snapshot 构建，冻结 world authored ID、
  本帧 transient 来源 ID、map generation 与展示开关；输入值由后续 adapter 提供。
- `src/rasterfall_session.c`、`include/rasterfall_session.h`：session-owned level/map 生命周期及 Map Runtime adapter 接入。
- `src/rasterfall_logic_test.inc`：由主编译单元包含的聚合逻辑测试入口。

## 实验区与性能测试

AI 策略对抗场和武器靶场由指挥桌沿正常 world load/reset/unload 进入，session 拥有独立
`rf_tactical_lab`。runtime 在固定更新入口把原生 16,667 微秒节拍累积为 20ms 战术步，
保留玩家射击边沿到下一个有效步；终端、RTS 摄像机和音效只负责交互及表现。
地图、权威与只读投影合同见[战术 AI 架构](tactical-ai.md#游戏内实验地图)。

`rf_combat_lab.inc` 是战斗固定预设、现场控制和结果的 runtime owner，复用前哨站空性能场和物理终端交互。
实验与性能场互斥；正常 FPS/RTS 命令仍交给 session，脚本观察仅为实验拥有的 actor 生成正式移动/武器命令。
所有伤害、回避、计时和统计真值在 Game，runtime 每固定步读取 owned 对象并在结束时写结构化日志。
实验不会重置整个世界；它恢复临时使用的本地玩家，按身份清理本轮 actor、保留槽中的感染者及自有投掷物，
移除临时 LAB 旗帜。终端 UI 只显示数据，不进行命中或回避结算。操作和复现见[战斗实验场](../guides/combat-lab.md)。

`rf_idle_rifle_lab.inc` 拥有真实 AI 持枪区的参与者身份和部署模式，普通 AI 固定步驱动静止、移动、
索敌及射击，正常角色来源决定表现。暂停保留参与者，关闭、隔离或 world 替换只清理自有身份；
不复写姿态或开设第二套 AI 更新路径。详见[实验区合同](../reference/experiment-labs.md)。

Game Runtime 的 `rf_experiment_labs.inc` 统一持有展示请求、后端能力和独立时钟，
从当前世界与性能独占状态派生有效开关。渲染只读这些值，不解析地图或更改玩法。
RF 电子产品控制台复用交互边沿，按 E 循环关闭和三个转速档；控制器积分连续转子相位，
发布只读电子设备帧，暂停/隔离冻结时间。几何与灯色由共享展示发射器消费，状态不进入 Game 或网络快照。
`rf_performance_lab.inc` 持有单轮测试、结果和返回状态；session 仍拥有基准地图加载、投影和 reset。
`rf_performance_live.inc` 为同一性能 owner 配置真实战斗、五人跨层与正式首图巡检。它只加载世界、
复用战斗预设初始化、提交普通 RTS 高度指令和改变观察相机；所有 AI、伤害、死亡与运动仍由正常
`rf_game_update()` / session 固定步结算。它不运行战斗实验的死亡槽位保留，不把观察相机写回参与者身体。
诊断 profile 在运行期间借用、完成/取消/退出时解除；实际存活范围、交战帧分布、每步峰值和路线完成
进入只读结果。LIVE 结束或取消重建前哨站并恢复玩家、相机、种子及展示请求；现场和 autorun 共用整个生命周期。
`rf_scene_performance.inc` 是显式启用的正常场景采样器，保留当前世界、展示请求、真实时钟及正常呈现策略，
预热后在内存记录完整帧间隔和 GPU/天空时间，结束时一次输出。它不借用性能实验场的隔离状态。
正常无界运行关闭逐帧 Scene 审计输出；`--frame-audit` 和有限帧诊断仍保留完整日志。
默认测试加载独立世界，结束/取消通过 `rf_game_request_world()` 返回前哨站；恢复本地玩家和展示请求，
其余前哨站 session 内容重建。环境实测保持原 world，暂停展示和动态灯；FULL SCENE 保留正常背景和展示更新。
全景巡检属于同一测试 owner，拥有分段相机、逐段预热/采样和结果；全开预设只临时修改 presentation 请求，
返回时恢复用户设置。逐段帧分布与准备成本在同一结果终端查看，渲染器不识别测试视角或实验区名称。
无主动节流仅在所选测试期间生效，不改变正常帧或逻辑固定步长。
120 FPS 节流以实际渲染帧所属的循环起点计算 8,333 微秒预算，包含输入、逻辑和渲染；
下一帧不在等待该预算后额外叠加整段逻辑耗时。调度唤醒和呈现仍可能使实际帧率略低于上限。
完整合同见[实验区合同](../reference/experiment-labs.md)，执行见[性能诊断](../guides/rendering-performance.md)。

## 生命周期

### 指挥桌预加载与出生点预览

`rf_table_panel.inc` 统一拥有下拉选图、悬停、部署按钮和右侧预览矩形；选择与部署分开。
`rf_table_preview.inc` 持有一个独立 session、地图资源集和 GPU 离屏目标。选择地图只更新
目的地并显示“点击预览画面框”提示，不启动预加载；点击右侧画面框或将键盘焦点移至
预览框后确认，才提交预览请求。主 GPU 提交退休后，工作线程独占场景 renderer、预览
session 和离屏目标，依次加载/reset 目标地图、准备资源、渲染出生点首帧。主线程不调用
大厅玩法或场景渲染，继续轮询输入并刷新加载界面；后台 session 也不推进玩法，不补偿
加载耗时到实时逻辑。Windows `toy_window_begin_loading/present_loading/end_loading` 使用
主线程拥有的 GDI 子窗口和软件 surface，独立于父窗口 Vulkan swapchain；鼠标消息转交
SDL 父窗口，光标位置另从 Win32 原生快照同步。窗口绘制和 Vulkan 队列不跨线程并发访问。
启动工作线程前，从已退休的主颜色目标一次读回大厅最后一帧；加载 UI 每次先复制这份静态
背景，再绘制面板和光标。窗口尺寸变化时缩放背景；join 后释放 CPU 图像并恢复实时场景。
这次读回不重新绘制大厅，不推进玩法，也不与后台 GPU 工作并发。
任务的阶段、完成、取消以原子 release/acquire 发布；只有 join 后才能读取或移交资源。
进度条标明估算，按阶段和停留时间单调增长，未完成最多 95%，成功才到 100% 并允许部署；
百分比不参与完成判断。失败可点击画面框重试；切换地图或关闭取消旧任务，不能中断的
驱动调用结束后 join 并释放，旧任务不得开启部署。取消时可为新选择排队点击预览请求，
但不会自动加载新地图；关闭等待期间继续显示返回提示并处理窗口事件。
成功后保留 GPU 图像并停止刷新，正常大厅仍由自己的 session 推进；切换地图、关闭界面或
退出时，先退休主 GPU 提交并解绑视频，再释放预览目标和 session。失败禁用预览部署，
重新选择可重试；CPU 后端保留普通部署并明确提示无 GPU 预览。

确认部署沿设备权限检查和 `rf_game_request_world()` 进入。匹配的预加载 session 通过
`rasterfall_session_adopt_map()` 移交 Runtime Map、level 和 content，重新绑定投影并在目标
session 正常 reset；不复制 Game 或 session 自引用指针，不继承预览摄像机状态。原生逻辑
回归覆盖五个目录地图的移交、源 owner 卸载、目标投影绑定及世界代际增长。
未命中预加载时仍使用原 preflight/load 路径。预览 GPU 图像不充当进入后的主渲染目标。

默认 Game policy 加载 `RASTERFALL_WORLD_OUTPOST`（`assets/maps/outpost.map`），不让 Core 选择或解析
Rasterfall world。Outpost 指挥桌地图屏幕可请求 Campaign 01、边缘站点、AI 策略对抗场与武器靶场；
战役返回设备请求 Outpost；`rf_game_request_world()` 按 unload → Runtime Map load → projection → session reset → lightmap
rebuild 顺序完成一次完整重建。普通离线启动经 RF Boot Manager 引导后落地前哨站，显式网络/诊断路径仍可使用旧启动菜单。

前哨站指挥桌的离线互动由 Game Runtime 管理临时输入与展示状态：桌前 E 进入固定站位、解锁指针，Esc 恢复 FPS；地图列表以现有 world ID 为准，预览从对应 `.map` 文件的 world bounds、surface 与 collision footprint 读取并逐帧绘制。点击列表调用 `rf_game_request_world()`。离线暂停菜单的 `RETURN TO OUTPOST` 也调用同一重载链，恢复出生点、初始 seed 和新局状态；联机时避免客户端单方面重载地图。

大厅南入口西侧 `main_terminal` 的离线 FPS 互动也由 Game Runtime 持有临时 UI 状态：按 E 打开、释放指针并暂停玩法命令，Esc 关闭并恢复视角。`rf_render_terminal.inc` 持有 baseline 展示清单、按后端查询的高级功能支持状态和统一的请求入口；当前只有 CPU 模型边线可切换，设置仅作用于 presentation。终端不创建玩法交互物，也不修改 `toy_game`。

`main()` 的顺序是：解析参数并组装 `rf_game_config` → 初始化唯一 `rf_core` context（window、renderer、
surface、filesystem、audio、input、clock）→ 普通离线启动进入 RF Boot Manager，默认 Start Rasterfall 自动选择 GPU Scene 或 CPU，也可进入 RF Workbench、RF Shell 或 Diagnostics
→ `rf_game_init(core, ...)` 加载 session/map → 绑定并准备渲染资源
→ 可选逻辑测试 → 启动菜单/建房连接 → 音频 presentation 启动 → 主循环 → 释放资源。
启动环境由 `rf_boot_ui` 持有临时输入、命令和展示状态，不创建 Game/session。Core 初始化在各服务真实调用处
报告结果及单调时钟耗时；窗口先于其他可见服务创建，Core 使用早期软件画布实时显示服务日志，
完成画面以 3、2、1 秒倒计时进入 RF Boot Manager，Enter 可立即继续。Boot Manager 五秒后自动执行 Start Rasterfall，任意输入取消自动启动。自动路径探测 Vulkan 图形设备，实际 GPU Scene 初始化失败则恢复 CPU；手动 Workbench 保留 CPU/GPU Scene 选择。Start、Workbench 与 Shell 的启动命令只选择后端，最终按 CPU/GPU Scene 汇入两种共享启动页；当前窗口先呈现工作状态，保留已显示画面并扫描进入所选后端的启动页。Diagnostics 查询硬件与 Core 服务状态，Esc 返回菜单。Game 地图、光照、可选纹理和 session reset 在所有者调用后报告实际耗时。
启动阶段进度表示已完成的任务数，不代表文件字节或 GPU 上传百分比。显式诊断、网络与自动化默认跳过交互；
`--skip-boot` 强制跳过，`--boot --frames N` 供有限帧窗口验证。GPU Scene 选择后平台层保留现有窗口句柄，将 SDL 硬件呈现器换为软件呈现器，再由 Core 在同一窗口上初始化 native present；
手动 GPU 选择失败时保留窗口并返回 CPU Boot Manager 显示错误，自动选择失败时直接用 CPU 继续。启动期软件画面由 Core 专门呈现，不计入 GPU 游戏帧契约。
终端的 `/assets` 是 package 中 `rasterfall/assets` 的只读视图，路径与链接检查在 `rf_boot_files` 服务内完成。
启动绘图元素统一由 `rf_boot_canvas` 提供：共享调色、等比画布、位图文字、矩形、分隔线和热区换算。
`rf_boot_ui` 持有页面、操作及布局，并在交互引导期间拥有一个临时 canvas；canvas 只保存上次目标画面、
已显示画面和扫描源像素，不拥有窗口、Core 或 Game 状态。页面绘制后声明可扫描区域，再以 Core 单调时钟
合成整屏切换或局部内容更新，最后仍由原有窗口/Core 呈现入口提交。启动选择结果临时持有一帧已显示像素，
供同一窗口扫描进入所选后端启动页；首个进度页呈现后释放。其他引导返回路径释放像素历史。
扩展页面复用绘图元素，在连续帧路径声明区域即可接入扫描；
交互、错误与同步任务边界的即时显示规则见[启动界面合同](../reference/boot-interface.md#光栅刷新)。
暂停菜单离线切换 CPU/GPU 时保留 Game/session，先停止音频并退休 Scene/渲染资源，再在现有 Core 窗口上切换 GPU 服务、恢复音频、
清除输入边沿并重置帧时钟；GPU 初始化失败则尝试恢复 CPU。联机期间不允许切换。

`--visual-capture <scenario> --visual-output <path>` 必须成对提供。解析后立即进入
`rasterfall_render_visual_capture()` 并退出，先于字库、网络、session/map、窗口和音频初始化。
固定场景不读取时钟、不推进 simulation，也不受交互式画面选项影响；不要与其他诊断模式混用。
支持 `--map <path>` 加载本地 V1 空间地图进行第一人称检查；session 按地图显式 identity 加载既有 World Content，无 identity 时沿用 Campaign policy。另支持 `procedural-humanoid`、`hurd-squad`、`lighting-props`、`--character-acceptance <model.rmesh> <output-dir>`、
`--squad-acceptance <model-dir> <output-dir>` 和
`--character-world-capture <output-dir> [--character-world-model <model.rmesh>]`；`procedural-humanoid` 与
`hurd-squad` 仍是独立的纯展示 fixture，不读取正式 world actor；
正式 Hurd 四人另由 session reset 创建，两条路径共享 character/profession profile 和程序化人物绘制入口。
场景与离屏输出契约见 [视觉验收](../guides/visual-validation.md)。未知场景、缺少参数、
资源加载/渲染/文件写入失败均返回非零并输出错误。

旧的 PMX/VMD 开发者预览参数（`--vmd-eula-walk`、`--vmd-freeze-*`、
`--vmd-disable-*`、`--vmd-legacy-*`、`--vmd-skin-trace`）仅保留为显式
兼容诊断入口。正常启动不自动显示 Eula/VMD 开发者预览；若正式 Eula gameplay
actor 存在，renderer 会按角色 profile 懒加载其 walk clip，供移动中的 actor 使用。
当前开发者角色观察和验收仍应使用 `--model-pose-views`、`--character-acceptance`
和 `--character-world-capture`。

RFANIM 诊断同样在窗口、音频和 session 初始化前早退：`--action-info` 检查动作结构，
`--action-preview <model> <action> <time-ms> <output.bmp>` 固定渲染一帧，`--pose-debug` 输出指定
humanoid role 的 finalized transform 及人体/AK socket。独立的 `build/rf_anim_info` 适合资产管线门禁；
完整参数与顺序仍以 `build/rasterfall --help` 为准。
`--pose-debug` 的六参数形式同时接收 lower/upper action 与各自时间，输出 composed result、weapon
transform 和左右 hand target；旧单 action 四参数形式继续可用。

主循环由 `rf_core_poll_events()` 捕获平台关闭请求；暂停菜单的 `EXIT GAME` 也通过
`rf_core_request_exit()` 写入同一个 Core-owned exit request，最终统一以
`rf_core_should_exit()` 形成退出边界，
并在每轮通过 `rf_core_begin_tick()` 读取 Core 单调时钟；随后轮询网络并保留按键边沿。固定约 16.67 ms 逻辑步中构造
`rasterfall_command`，交给 `rf_game_update()`；该 facade 按原顺序推进 session/client prediction、
host remote apply/rescue、gameplay timers、effects sync 和 authoritative snapshot/command bookkeeping，
之后由 `rf_game_render()` 派生 render camera，并按原顺序提交 world、entities、interactables、
world effects、viewmodel、HUD、pause、scoreboard、debug overlay、labels 和 console；Core startup
与 connection bootstrap UI 仍保持独立路径。排查“偶发吞键”
时查看 `pending_key_edges`，排查帧率相关玩法差异时查看 accumulator 和逻辑步，而不是只看渲染帧。

Windows 正常交互帧以 120 FPS 为提交上限，玩法仍以 16,667 微秒固定步长推进。每个渲染帧轮询并采样输入；按键边沿保留到下一逻辑步，鼠标位移在下一逻辑步进入命令，同时立即用于本帧的只读第一人称视角。渲染保存前后两个已完成逻辑状态，以 accumulator 比例对同一身份的 actor、enemy 和 projectile 的位置与高度，以及角色和敌人的朝向做展示插值；同一动作的动画时间、敌人倒地时间和弹体飞行时间也按已知状态插值。新生成、槽位复用、动作切换或大幅瞬移直接显示当前状态。插值展示约落后逻辑一个 tick，玩法、碰撞、网络权威状态不读取展示副本。相机不采用该延迟。固定帧审计保留原诊断时序，不使用交互帧节流或世界插值。HUD FPS 仍按实际完成的渲染帧计数；达到 120 取决于完整帧成本和呈现能力。

Windows 普通启动默认无边框桌面全屏，surface 与 Scene 主视图使用实际客户区像素；在 1920×1080
桌面上原生渲染 1080p，不把 720p framebuffer 拉伸。Game options 选择显示模式和恢复后的窗口尺寸，
Core 将配置交给 Windows 平台层；SDL 持有全屏状态和恢复位置，F11 在同一窗口句柄上切换。
每批事件后按最终客户区尺寸同步 surface，再由现有 GPU owner 处理 swapchain 重建；分配失败传播错误。
Windows DPI awareness 使指针、surface 与 native present 共用像素坐标。
Windows 每次轮询（包括超时无事件）还发布有效的当前窗口焦点快照。启动画面可以先消费焦点事件，
但 Core 输入初始化后的焦点不能依赖再收到一次激活事件；实际失焦仍按原规则取消拖框和清除按住状态。
显式窗口模式、有限帧、frame-audit 与 Scene 诊断默认保留 1280×720 窗口，可用 CLI 覆盖。
UI 保留 720p 设计基准，按客户区高度和用户百分比计算，绘制与点击共用布局；使用见[玩家界面](../guides/player-ui-v2.md)。
显示模式不选择渲染后端，普通 Boot Manager 仍自动探测 GPU 并允许回退 CPU。
freestanding Linux/WSL 默认窗口仍为 1280×720，不接入 Windows 全屏快捷键。
CPU 在所有平台按真实 world generation 暂停首帧的 renderer watchdog，完成懒加载并成功 present 后
恢复正常的 200 ms 预算；经 session 重载并递增 world generation 的同图重载及后端切换也重新预热。
仅重置 Game 而未改变 world generation 不新增预热。Runtime 只向 Core 提供只读 generation，
Core 持有已成功预热和当前帧的记录；取消或失败帧不得标记已预热。玩法单位、相机 FOV 和权威状态
不依赖该策略。WSL 的 GPU、音频与额外窗口集成仍不属于 CPU 最小可玩承诺。

CPU watchdog 取消在 clear、WORLD/后续 flush 或最终 flush 返回后均丢弃该帧，不呈现半帧、
不增加成功帧数。Renderer 的 dispatch/parallel producer 同步等待取消 worker 全部退出本轮工作；
Game render facade 的失败出口结束动态光照 scope，Core `rf_core_discard_frame()` 再核验 worker 已退休，
恢复 VIEWMODEL 借用的 depth/coverage、清命令并完成 registry frame，下一次 begin 才可复用资源。
非取消错误、无法安全 discard、begin 或真正 present 失败有明确错误日志并非零退出；正常窗口关闭
仍返回 0。GPU 独立 Scene 的 submit/retire 与 reader 生命周期保持独立。

本地 session/client prediction 把控制器命令交给 actor API；actor 先更新 gameplay body，随后
world step 推进共享世界规则，再由 `session_sync_special_motion()` 派生 camera 的位置和高度。
camera 的方向仍作为输入视角供移动与瞄准使用。渲染阶段可复制 camera 叠加纯展示效果，但不得
回写 gameplay 位置。

窗口运行时的一帧由 Core Host 完整包住：每个成功提交帧只调用一次 `rf_core_begin_frame()` 获取 surface 并调用
`toy_renderer_begin()`；`rf_game_render()` 提交世界、交互物、第一人称模型、world effects 和
steady-state Game UI，
并通过 Core 提供的 `rf_core_flush()` 保留显式 layer barrier。GPU 模式下 Core 保留这些
pre-post command，到 viewmodel barrier 后才统一提交；任一未迁移的 effects/viewmodel command
或 direct-pixel producer 都会触发按原批次顺序的整帧 CPU replay。effects 与 viewmodel 分别完成 pre-post
submission，随后 `rf_core_begin_screen_overlay()` 同时切换 surface 与 renderer target；HUD、Console、
Desktop 只能在该边界之后提交。最后
`rf_core_end_frame()` 执行最终 flush，再由 Core present。Core 同时拥有 window、surface、renderer
和 audio 的生命周期；Game 不销毁这些资源，也不管理 framebuffer。

GPU-7C renderer policy 为：`renderer=cpu` 对应 `GPU_DISABLED`；显式
`--renderer gpu-compute` 默认对应 `GPU_OPTIONAL`，初始化或 Raster V1 capability 不可用时启动为
CPU；再加 `--gpu-required` 对应 `GPU_REQUIRED`，且必须同时启用 `--gpu-native-present`。required 是完整 runtime contract：初始化、逐帧 retained submission、Post、native present 或零 readback/copy 任一门禁失败即非零退出，禁止 CPU replay/software present。`services`
输出当前 renderer 及 world attempted/rendered/fallback 累计值。GPU frame resource 全归 Core，包含
Raster V1 stream、backend tile lists、GPU color/depth、readback 和 timing；Game/session 不持有 Vulkan
handle。world flush 只有完整 batch 全部支持时才在 GPU 同时生成 color 与 signed inverse-depth；否则
整批 CPU fallback，不存在 GPU 支持部分后由 CPU 补画 unsupported world command 的路径。

Windows normal GPU batch 同时用同一 packed stream 与 Texture V1 table 执行正式软件 raster oracle。
oracle 在 GPU dispatch 前生成；若逐 RGB/depth 比较失败，Core 自动保存
`gpu-oracle-mismatch/commands.bin`、`.textures`、CPU/GPU/diff BMP、双方 depth 与报告，再逐行复制
完整 CPU oracle color/depth 回正式 surface 并消费该批。stream 可直接交给 hosted differential replay。
`--gpu-normal-scene <view> <0|30>` 只固定 seed、world、camera/enemy fixture，仍执行正常
window/Core/world/present 主循环。shutdown timing 覆盖 frontend、classification、texture measure、
ABI pack + texture table、binning/upload/submit/execution-wait/readback、CPU oracle、present 与 frame total；
presentation copy 在 V1 中标为与 readback 合并。

`struct rf_core` 是当前唯一的 Core context 和所有 Core service owner；`rf_core_context` 仅是兼容命名别名，
不创建第二份 service container。`rf_game_runtime` 只保存一个非 owning 的 Core 指针，并通过 Core 提供的服务
使用 platform resource。正常 Runtime 的 fixed-step、菜单节流、连接等待和帧统计统一使用
`rf_core_time_us()`；离屏模型/benchmark 诊断仍可使用自己的临时计时路径，不属于正常 Host lifecycle。

模型视图、visual capture、benchmark 和 logic-test 是窗口 Core 初始化前的独立离屏 fixture，仍可
自建临时 renderer/surface；`--logic-test` 使用 Core 的 headless 初始化，仅准备 filesystem、input
和 renderer service，不创建 window/audio。它们不是交互式 runtime 的 frame ownership 路径。
GPU world raster capture 同属此类 headless fixture：固定 seed/world/camera，只输出 selected stream
与 coverage/frontend/pack 数据；实际 Vulkan differential 由 hosted replay 工具执行。

Core 还拥有独立的 `rf_core_filesystem` service。V0 只提供 `logical path -> owned blob`，内部复用
现有 `toy_asset_load_file()` 的 embedded lookup、磁盘 fallback、Linux/Windows 相对路径语义和大小
限制；它不识别 `.map`、`.rmesh`、`.rfanim` 或其他资源格式。现有格式 loader 暂不迁移，继续通过
兼容 wrapper 工作；未来 parser 可逐步改为消费 blob。

Core status query V0.2 由 `rf_core_get_status()` 提供。调用方得到一份不含内部指针或服务对象的
只读快照，包含 `RF_CORE_VERSION`/`RF_CORE_BUILD` 以及 window、renderer、filesystem、audio 和
clock 的 ready 状态；它不创建窗口、不启动终端，也不参与生命周期管理。

RF Command Runtime V0 由 `rasterfall_console_command` 注册表和 `rf_command_context` 组成，当前由
Developer Console 这个 frontend 使用。`rf_terminal_session` 持有输入 buffer、history 和最近一次
`rf_command_output`；session 只负责把命令交给 registry/handler，并不复制 command registry、handler
或 permission logic。当前 `killall`、`pose`、`help`、`clear`、`status`、`runtime` 和
`services` 均走同一注册/分发路径。`status` 组合 Core/Game snapshot，`runtime` 只读 Game snapshot，
`services` 只读 Core snapshot；命令层不读取内部 service、session、actor 或 renderer state。

RF Terminal Frontend Prototype V0 的链路是：

```text
Frontend UI (当前 Developer Console modal)
        ↓
rf_terminal_session
        ↓
RF Command Runtime (registry/context/output/permission)
        ↓
Core/Game Runtime query 与 command state
```

session 是 frontend 可复用的 transient interaction layer；UI 只负责打开/关闭、按键编辑和把
`rf_command_output` 映射为视觉日志。未来 F12 Terminal 可以复用同一 session API；World Terminal
Device 只需替换输入/显示适配并提供合适的 `rf_command_context`；Super Terminal 可在更高权限
context 下复用同一 registry。它们不需要复制命令实现，也不需要修改 `toy_game`、地图实体或
renderer。Prototype V0 不实现 World Terminal Device、GUI framework、IPC、exec、process manager
或 module loader。

Game Runtime 不直接包含或调用 toy window implementation，也不直接调用平台 clock。正常帧循环
使用 `rf_core_time_us()`；窗口事件、输入和音频对象通过 Core accessors 借用。模型/性能离屏诊断
在 Core Host 初始化前运行，因此使用同一 Core clock service 的无实例入口
`rf_core_clock_now_us()`；资源格式 loader 仍保留现有兼容路径，未在本阶段迁移。

Core 与 Game
Runtime 分别提供独立快照；查询调用方不取得生命周期所有权，也不应维护第二份玩法状态。

Input Boundary V0 由 `rf_core_get_input_frame()` 提供。Core 在每次成功事件轮询后生成可复制的
`rf_input_frame`，包含 key down、pressed/released edge、指针位置/相对移动、锁定状态和鼠标按键；
Game Runtime 使用该 view 构造命令。Windows SDL 将游戏使用的键、鼠标左/右/中/侧键和滚轮归一到公共输入帧；
滚轮步数逐次轮询累加、逐帧清零。离线 RTS 由 `M` 切换，runtime 保持北朝屏幕上方的独立
render camera；`WASD` 平移该镜头，滚轮调节相对当地可见地面的高度（3 至 130 米），
鼠标点击沿当前镜头的画面像素透视射线与可见地面求交。选取结果仅为 runtime UI 状态，
求交使用 renderer 所消费的地图绘制记录（基础地板、坡道、平台和盒体顶面），按最近命中点确定位置；
不以碰撞 primitive 的玩法脚底高度替代像素里的可见表面。
玩家移动目标由 session 持有，AI 目标归 Game actor；runtime 投影选中单位的脚底圈、目的地与意图连线。
`rf_rts_runtime.inc` 负责点选、拖框、十个数字编组和 UI 热区消费，`rf_rts.c` 按世界及 actor 代际验证成员。
指挥底栏、健康网格、卡片与实时单位镜头见 [RTS 核心指挥](rts-command.md)。旗帜不参与单位指挥。
切回 FPS 后恢复原来的视角俯仰，AI 已接受的移动继续执行。

进入 RTS 默认选中本地玩家。Y 将独立 render camera 锁定到玩家，按固定俯视倾角和缩放距离
补偿水平偏移，使玩家脚底投影在屏幕中央；WASD 平移从当前跟随位置接续并解除锁定，缩放保持跟随。
跟随和 T 传送选点都属于 runtime UI 状态，物理动作绑定分别为 `RF_ACTION_RTS_FOLLOW` 和
`RF_ACTION_RTS_TELEPORT`。T 切换选点，下一次左键地面命中交给 session；session 将可见高度
转换为玩法脚底高度，核对真实支撑和 body collision，仅在存活且未受特殊控制时更新本地 actor，
清除空中动量和旧 RTS 移动目标，再同步 camera。无效地点不改 actor；成功后关闭选点并选中玩家。
该功能沿 RTS 的离线边界使用，不向客户端开放本地权威位置修改。

### 物理键与动作绑定

平台事件同时提供原有的字符/菜单键码和独立物理键码。Windows 物理键码是 SDL 扫描码，按键布局
变化不改变其物理身份；Wayland 物理键码沿用 evdev 编号。两者分别记录 down、pressed 和 released，
有效键码为 1 至 511（0 表示未知）；失去键盘焦点时清除按住状态。Game 的 `rf_input_bindings` 将逻辑动作映射到物理键，
默认值按平台选择；`rf_game_bind_action()` 可将运行中任一动作改绑到范围内的物理键，
底层 `rf_input_bind()` 负责更新绑定表。各动作可共享默认物理键，
由当前界面决定语义，例如 Enter 在正常游玩中打开聊天、在菜单中确认。聊天优先消费共享动作边沿，
不会同时开火或提交回答。文本输入继续读取原有键码，不把
扫描码当作字符。鼠标按钮及滚轮继续使用原有输入通道。

Windows 每批鼠标事件保留第一次按下及其 `button_x/button_y`，松开不覆盖该边沿；
`pointer_x/pointer_y` 和 `mouse_buttons` 仍表示批次结束状态。RTS 用按下坐标进行 UI 命中与框选起点判断，
用最终指针完成拖动，避免高分辨率慢帧中短点击或整段拖动被同批松开事件吞掉。
`keyboard_focus_valid` 表示平台提供当前焦点快照，`keyboard_focus_changed` 只表示真实焦点事件。
公共输入接受任一种更新；未提供快照的事件型后端在无焦点事件时保留先前状态。

默认游戏操作为 WASD 移动、方向键看向、鼠标左键开火、Enter 打开/收起聊天、Space 跳跃、斜杠推开、R 换弹、
1 至 4 切换槽位、E 互动、F 放旗、M 切换 RTS；Esc 暂停或返回，F1 打开渲染终端，
F2 与反引号共用玩家终端开关，F3 无默认功能，F12 请求 Desktop。暂停/启动菜单使用方向键、
Enter、Esc；Tab 显示记分板或翻动姿态编辑页。姿态编辑的逗号/句号、J/L、R/P、U/O/I/V、
X/Y/Z 和骨骼调试的 N/B、减号/等号保留原有功能。具体默认物理码以
`src/rf_input_bindings.c` 为准。平台输入边沿先进入 Core，Game 在未跑固定逻辑步时保留物理键边沿，
首个逻辑步消费后清除，避免短按在低帧率下丢失或多次触发。

`struct camera` 现以 `body` 和 `view` 两个命名空间表达该边界；旧的扁平字段暂保留为布局兼容
别名。新代码应使用 `camera.body` 读写派生位置，使用 `camera.view` 读写方向和展示高度。

## 常见任务落点

前哨站性能实验场由 `rf_performance_lab.inc` 持有测试、相机、采样和只读结果。基础四项固定十秒、前两秒预热，固定玩家观察站位，结束清理敌人并恢复玩家和相机。全景巡检拥有分段相机；LIVE 则由 `rf_performance_live.inc` 配置真实战斗、五人跨层或正式首图，沿正常 session 规则更新，观察相机不写参与者身体。操作与返回合同见[实验区合同](../reference/experiment-labs.md#性能独占与基准世界)。测试只允许前哨站离线单人且当前没有敌人。结果不进入 `toy_game`、session 权威状态或网络快照；GPU 时间与 CPU 阶段重叠，不能相加。

- 新增启动参数：options 头文件字段、`rasterfall_options_init/parse/usage`，在 `main()` 解析后
  通过 `rf_game_config` 传给 runtime。
- 改键位或鼠标：`build_game_command()`、`consume_game_command_edges()` 及主循环的菜单/控制台分流。
- 改启动或暂停界面：`run_startup_menu()`、`draw_pause_overlay()`；HUD 主界面在 `rasterfall_hud.c`。
- 改射击视听同步：`sync_*_fire_effects()`、`emit_ray_effects()`，并核对网络序列号与游戏事件。
- 改镜头晃动：`rasterfall_effect_event.h`、`rasterfall_effects.c` 的 `CAMERA_SHAKE`，以及主循环中
  `render_camera` 的展示态应用；受击 preset 宏也在 `rasterfall_effects.h`；不要修改权威 `camera`
  或网络快照。
- 改自动化/截图/性能参数：先查 options，再查 `main()` 中窗口创建前的诊断早退和帧尾输出。
- 正式 squad content/acceptance：查 `rasterfall_roster.c`、session reset 的 roster adapter 和
  `--squad-acceptance` 早退；不要把验收 fixture 当作 gameplay actor 的额外状态源。

平台 API 不在本目录：窗口与输入分别在 `lib/platform/window_wayland.c`、`lib/input/input.c`，
Windows 替换实现位于 `windows/src/`。只有跨应用的平台缺陷才应修改这些公共层。
