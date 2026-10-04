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

## 状态所有者

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

V1 checkpoint 和版本化原型设计见 [Runtime 历史设计](../archive/runtime-design-v0/README.md)，不作为当前执行顺序。

关键配套文件：

- `src/rasterfall_options.c` / `include/rasterfall_options.h`：命令行默认值、解析和 usage。
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

默认 Game policy 加载 `RASTERFALL_WORLD_OUTPOST`（`assets/maps/outpost.map`），不让 Core 选择或解析
Rasterfall world。Outpost 指挥桌地图屏幕可请求 `RASTERFALL_WORLD_CAMPAIGN_01` 或
`RASTERFALL_WORLD_RETURN_TO_WHU_V0`；战役返回设备请求 Outpost；`rf_game_request_world()` 按 unload → Runtime Map load → projection → session reset → lightmap
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

正常窗口默认使用 CPU renderer，Windows 与 freestanding Linux/WSL 的默认 framebuffer 均为
1280×720。WSL CPU 在每个 world 的首帧完成懒加载预热并成功 present 后恢复正常的 200 ms renderer watchdog；玩法单位、相机 FOV
和权威状态不依赖该策略。WSL 的 GPU、音频与额外窗口集成仍不属于 CPU 最小可玩承诺。

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
或 permission logic。当前 `killall`、`give+N`、`pose`、`help`、`clear`、`status`、`runtime` 和
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
玩家移动目标由 session 持有，runtime 将其投影为随镜头移动的 `MOVE` 标记；旗帜位置也交给 session。
切回 FPS 后恢复原来的视角俯仰。

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

默认游戏操作为 WASD 移动、方向键看向、鼠标左键开火、Enter 打开/收起聊天、Space 跳跃、斜杠推开、R 换弹、
1 至 4 切换槽位、E 互动、F 放旗、M 切换 RTS；Esc 暂停或返回，F1 打开渲染终端，
F2 打开受管终端，反引号打开开发者控制台，F12 请求 Desktop。暂停/商店/启动菜单使用方向键、
Enter、Esc；Tab 显示记分板或翻动姿态编辑页。姿态编辑的逗号/句号、J/L、R/P、U/O/I/V、
X/Y/Z 和骨骼调试的 N/B、减号/等号保留原有功能。具体默认物理码以
`src/rf_input_bindings.c` 为准。平台输入边沿先进入 Core，Game 在未跑固定逻辑步时保留物理键边沿，
首个逻辑步消费后清除，避免短按在低帧率下丢失或多次触发。

`struct camera` 现以 `body` 和 `view` 两个命名空间表达该边界；旧的扁平字段暂保留为布局兼容
别名。新代码应使用 `camera.body` 读写派生位置，使用 `camera.view` 读写方向和展示高度。

## 常见任务落点

前哨站性能实验场由 `rf_game_runtime.c` 中的 `rf_performance_lab.inc` 持有一次测试的状态、固定视角、采样和单面板结果。开始时清空上一结果、固定随机种子和玩家站位，关闭选择菜单；10 秒墙钟测试的前 2 秒预热，不计入结果。运行期间继续推进固定步长玩法，但忽略玩家操作并将玩家固定在观察道路上；结束时清除全部测试敌人，恢复玩家和相机并解除控制锁。地图仅提供终端、地面、矮墙和静态组件；结果是运行时展示状态，不进入 `toy_game`、session 权威状态或网络快照。测试只允许前哨站离线单人且当前没有敌人。采样字段来自 GPU Scene 正常帧 probe，GPU 绘制时间与 CPU 阶段重叠，不能相加。

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
