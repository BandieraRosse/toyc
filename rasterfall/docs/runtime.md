# 运行时与主循环

> 文档更新：2026-09-11
> 源码核对基线：工作区（Humanoid Action Composition V1 CLI；双正式四人 squad runtime；Lighting V1；`game_state.actors[]` 是 gameplay truth；RF Core Runtime V0 `rf_game_runtime` facade；Core/Game startup config split）

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
- `src/rf_game_runtime.c`：实际 fixed-step gameplay/network/effects 更新、world/HUD/debug
  渲染、菜单与输入边沿处理；`rasterfall.c` 不再访问 Game 状态，也不再把 `argc/argv` 传入
  runtime。
- `include/rasterfall_camera.h`：共享摄像机数据结构。
- `include/rasterfall_units.h`：网络和玩法共用的单位换算。
- `src/rasterfall_console.c`、`src/rasterfall_calibration.c`：开发控制台与持枪姿态校准。
- `src/rasterfall_logic_test.inc`：由主编译单元包含的聚合逻辑测试入口。

## 生命周期

`main()` 的顺序是：解析参数并组装 `rf_game_config` → runtime 初始化网络 → 加载 session/map → 绑定并准备渲染资源
→ 可选逻辑测试 → 创建窗口 → 启动菜单/建房连接 → 音频启动 → 主循环 → 释放资源。

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
`rasterfall_command`，交给 session 或客户端预测路径；之后同步音频/特效并渲染。排查“偶发吞键”
时查看 `pending_key_edges`，排查帧率相关玩法差异时查看 accumulator 和逻辑步，而不是只看渲染帧。

本地 session/client prediction 把控制器命令交给 actor API；actor 先更新 gameplay body，随后
world step 推进共享世界规则，再由 `session_sync_special_motion()` 派生 camera 的位置和高度。
camera 的方向仍作为输入视角供移动与瞄准使用。渲染阶段可复制 camera 叠加纯展示效果，但不得
回写 gameplay 位置。

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
