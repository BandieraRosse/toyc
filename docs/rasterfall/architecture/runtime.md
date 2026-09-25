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

## 生命周期

默认 Game policy 加载 `RASTERFALL_WORLD_OUTPOST`（`assets/maps/outpost.map`），不让 Core 选择或解析
Rasterfall world。Outpost 的 Operations Terminal 请求 `RASTERFALL_WORLD_CAMPAIGN_01`，新增 Return-to-WHU 入口请求
`RASTERFALL_WORLD_RETURN_TO_WHU_V0`；战役返回设备请求 Outpost；`rf_game_request_world()` 按 unload → Runtime Map load → projection → session reset → lightmap
rebuild 顺序完成一次完整重建。普通离线启动直接落地前哨站，显式网络/诊断路径仍可使用旧启动菜单。

`main()` 的顺序是：解析参数并组装 `rf_game_config` → 初始化唯一 `rf_core` context（window、renderer、
surface、filesystem、audio、input、clock）→ `rf_game_init(core, ...)` 加载 session/map → 绑定并准备渲染资源
→ 可选逻辑测试 → 启动菜单/建房连接 → 音频 presentation 启动 → 主循环 → 释放资源。

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
并在每轮通过 `rf_core_begin_tick()` 读取 Core 单调时钟；随后轮询网络并保留按键边沿。固定 16 ms 逻辑步中构造
`rasterfall_command`，交给 `rf_game_update()`；该 facade 按原顺序推进 session/client prediction、
host remote apply/rescue、gameplay timers、effects sync 和 authoritative snapshot/command bookkeeping，
之后由 `rf_game_render()` 派生 render camera，并按原顺序提交 world、entities、interactables、
world effects、viewmodel、HUD、pause、scoreboard、debug overlay、labels 和 console；Core startup
与 connection bootstrap UI 仍保持独立路径。排查“偶发吞键”
时查看 `pending_key_edges`，排查帧率相关玩法差异时查看 accumulator 和逻辑步，而不是只看渲染帧。

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
Game Runtime 使用该 view 构造命令。离线 RTS 由 `M` 切换，runtime 保持北朝屏幕上方的独立高位
render camera；`WASD` 平移该镜头，鼠标点击按相同透视投影反求地面坐标。选取结果仅为 runtime UI 状态，
移动命令和旗帜位置交给 session；切回 FPS 后恢复原来的视角俯仰。

`struct camera` 现以 `body` 和 `view` 两个命名空间表达该边界；旧的扁平字段暂保留为布局兼容
别名。新代码应使用 `camera.body` 读写派生位置，使用 `camera.view` 读写方向和展示高度。

## 常见任务落点

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
