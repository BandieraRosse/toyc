# 运行时与主循环

> 文档更新：2026-09-11
> 源码核对基线：工作区（Humanoid Action Composition V1 CLI；双正式四人 squad runtime；Lighting V1；`game_state.actors[]` 是 gameplay truth；RF Core Runtime V0.2 `rf_game_runtime` facade、Core status query、service access cleanup 与 Input view；Core/Game startup config split；renderer frame ownership cleanup；Core filesystem service V0；唯一 `rf_core` context 与 Core clock service；Phase 3A `rf_game_update()` gameplay update authority；Phase 3B-1 world presentation migration；Phase 3B-2 steady-state Game UI presentation authority）

> 源码核对补充：session reset 在原 flag 1 和原坐标恢复 Maid 四人旗卫，并创建使用 flag 2 的正式 Hurd squad/outpost；Hurd 控制状态保持派生。

## 状态所有者

`src/rasterfall.c` 是可执行程序入口，负责命令行解析并组装 `rf_game_config`；
`src/rf_game_runtime.c` 负责 Game facade 的运行调度，Core Host 由 `rf_core_config` 接收启动参数。
`struct rf_game_runtime` 集中拥有
session 外的网络、摄像机、输入边沿、特效、HUD/菜单控制、perf/debug、音频和 fixed-step
运行态；`rf_game_update()` 与 `rf_game_render()` 是 Game facade 的更新/渲染入口，具体玩法规则
仍下沉到 session/game。

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
- `src/rasterfall_console.c`、`src/rasterfall_calibration.c`：开发控制台与持枪姿态校准。
- `src/rasterfall_logic_test.inc`：由主编译单元包含的聚合逻辑测试入口。

## 生命周期

`main()` 的顺序是：解析参数并组装 `rf_game_config` → 初始化唯一 `rf_core` context（window、renderer、
surface、filesystem、audio、input、clock）→ `rf_game_init(core, ...)` 加载 session/map → 绑定并准备渲染资源
→ 可选逻辑测试 → 启动菜单/建房连接 → 音频 presentation 启动 → 主循环 → 释放资源。

`--visual-capture <scenario> --visual-output <path>` 必须成对提供。解析后立即进入
`rasterfall_render_visual_capture()` 并退出，先于字库、网络、session/map、窗口和音频初始化。
固定场景不读取时钟、不推进 simulation，也不受交互式画面选项影响；不要与其他诊断模式混用。
支持 `procedural-humanoid`、`hurd-squad`、`lighting-props`、`--character-acceptance <model.rmesh> <output-dir>`、
`--squad-acceptance <model-dir> <output-dir>` 和
`--character-world-capture <output-dir> [--character-world-model <model.rmesh>]`；`procedural-humanoid` 与
`hurd-squad` 仍是独立的纯展示 fixture，不读取正式 world actor；
正式 Hurd 四人另由 session reset 创建，两条路径共享 character/profession profile 和程序化人物绘制入口。
场景与离屏输出契约见 [rendering.md](rendering.md) 的 Visual CLI。未知场景、缺少参数、
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

主循环先轮询平台事件和网络，再保留按键边沿；固定 16 ms 逻辑步中构造
`rasterfall_command`，交给 `rf_game_update()`；该 facade 按原顺序推进 session/client prediction、
host remote apply/rescue、gameplay timers、effects sync 和 authoritative snapshot/command bookkeeping，
之后由 `rf_game_render()` 派生 render camera，并按原顺序提交 world、entities、interactables、
world effects、viewmodel、HUD、pause、scoreboard、debug overlay、labels 和 console；Core startup
与 connection bootstrap UI 仍保持独立路径。排查“偶发吞键”
时查看 `pending_key_edges`，排查帧率相关玩法差异时查看 accumulator 和逻辑步，而不是只看渲染帧。

本地 session/client prediction 把控制器命令交给 actor API；actor 先更新 gameplay body，随后
world step 推进共享世界规则，再由 `session_sync_special_motion()` 派生 camera 的位置和高度。
camera 的方向仍作为输入视角供移动与瞄准使用。渲染阶段可复制 camera 叠加纯展示效果，但不得
回写 gameplay 位置。

窗口运行时的一帧由 Core Host 完整包住：`rf_core_begin_frame()` 获取 surface 并调用
`toy_renderer_begin()`；`rf_game_render()` 提交世界、交互物、第一人称模型、world effects 和
steady-state Game UI，
并通过 Core 提供的 `rf_core_flush()` 保留内部 ordering barrier。现有画面依赖世界深度层、
直接 framebuffer overlay 与 viewmodel 的既定顺序。最后
`rf_core_end_frame()` 执行最终 flush，再由 Core present。Core 同时拥有 window、surface、renderer
和 audio 的生命周期；Game 不销毁这些资源，也不管理 framebuffer。

`struct rf_core` 是当前唯一的 Core context 和所有 Core service owner；`rf_core_context` 仅是兼容命名别名，
不创建第二份 service container。`rf_game_runtime` 只保存一个非 owning 的 Core 指针，并通过 Core 提供的服务
使用 platform resource。正常 Runtime 的 fixed-step、菜单节流、连接等待和帧统计统一使用
`rf_core_time_us()`；离屏模型/benchmark 诊断仍可使用自己的临时计时路径，不属于正常 Host lifecycle。

模型视图、visual capture、benchmark 和 logic-test 是窗口 Core 初始化前的独立离屏 fixture，仍可
自建临时 renderer/surface；它们不是交互式 runtime 的 frame ownership 路径。

Core 还拥有独立的 `rf_core_filesystem` service。V0 只提供 `logical path -> owned blob`，内部复用
现有 `toy_asset_load_file()` 的 embedded lookup、磁盘 fallback、Linux/Windows 相对路径语义和大小
限制；它不识别 `.map`、`.rmesh`、`.rfanim` 或其他资源格式。现有格式 loader 暂不迁移，继续通过
兼容 wrapper 工作；未来 parser 可逐步改为消费 blob。

Core status query V0.2 由 `rf_core_get_status()` 提供。调用方得到一份不含内部指针或服务对象的
只读快照，包含 `RF_CORE_VERSION`/`RF_CORE_BUILD` 以及 window、renderer、filesystem、audio 和
clock 的 ready 状态；它不创建窗口、不启动终端，也不参与生命周期管理。

Game Runtime 不直接包含或调用 toy window implementation，也不直接调用平台 clock。正常帧循环
使用 `rf_core_time_us()`；窗口事件、输入和音频对象通过 Core accessors 借用。模型/性能离屏诊断
在 Core Host 初始化前运行，因此使用同一 Core clock service 的无实例入口
`rf_core_clock_now_us()`；资源格式 loader 仍保留现有兼容路径，未在本阶段迁移。

未来前哨站 / Terminal 的查询基础见 [core-runtime-v0.2.md](core-runtime-v0.2.md)。Core 与 Game
Runtime 分别提供独立快照；查询调用方不取得生命周期所有权，也不应维护第二份玩法状态。

Input Boundary V0 由 `rf_core_get_input_frame()` 提供。Core 在每次成功事件轮询后生成可复制的
`rf_input_frame`，包含 key down、pressed/released edge、指针位置/相对移动、锁定状态和鼠标按键；
Game Runtime 使用该 view 构造原有命令，未引入 action mapping，也未改变键位或 gameplay。

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
