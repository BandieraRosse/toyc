# 运行时与主循环

> 文档更新：2026-09-09
> 源码核对基线：工作区（Hurd 四职业 presentation profile 与固定 hurd-squad capture；Visual CLI V1 固定 procedural-humanoid 离屏 BMP capture；控制器命令经 actor API 进入玩法；`game_state.actors[]` 是玩家/AI gameplay truth；world step 独立推进投射物、敌人、波次和地图规则；camera、HUD、网络展示只读取 actor/world 或 derived presentation cache；本地 body 位置驱动 camera）

> 源码核对补充：session reset 在原 flag 1 和原坐标恢复 Maid 四人旗卫，并创建使用 flag 2 的正式 Hurd squad/outpost；Hurd 控制状态保持派生。

## 状态所有者

`src/rasterfall.c` 是可执行程序入口和最高层编排器。它拥有窗口、输入、摄像机、音频、网络、
暂停/启动菜单、固定步长累积器及渲染提交顺序，但具体玩法规则应下沉到 session/game。

关键配套文件：

- `src/rasterfall_options.c` / `include/rasterfall_options.h`：命令行默认值、解析和 usage。
- `include/rasterfall_camera.h`：共享摄像机数据结构。
- `include/rasterfall_units.h`：网络和玩法共用的单位换算。
- `src/rasterfall_console.c`、`src/rasterfall_calibration.c`：开发控制台与持枪姿态校准。
- `src/rasterfall_logic_test.inc`：由主编译单元包含的聚合逻辑测试入口。

## 生命周期

`main()` 的顺序是：解析参数和诊断早退 → 初始化网络 → 加载 session/map → 绑定并准备渲染资源
→ 可选逻辑测试 → 创建窗口 → 启动菜单/建房连接 → 音频启动 → 主循环 → 释放资源。

`--visual-capture <scenario> --visual-output <path>` 必须成对提供。解析后立即进入
`rasterfall_render_visual_capture()` 并退出，先于字库、网络、session/map、窗口和音频初始化。
固定场景不读取时钟、不推进 simulation，也不受交互式画面选项影响；不要与其他诊断模式混用。
支持 `procedural-humanoid`、`hurd-squad`、`--character-acceptance <model.rmesh> <output-dir>` 和
`--character-world-capture <output-dir>`；前两者仍是独立的纯展示 fixture，不读取正式 world actor；
正式 Hurd 四人另由 session reset 创建，两条路径共享 character/profession profile 和程序化人物绘制入口。
场景与离屏输出契约见 [rendering.md](rendering.md) 的 Visual CLI。未知场景、缺少参数、
资源加载/渲染/文件写入失败均返回非零并输出错误。

旧的 PMX/VMD 预览参数（`--vmd-eula-walk`、`--vmd-freeze-*`、
`--vmd-disable-*`、`--vmd-legacy-*`、`--vmd-skin-trace`）仅保留为显式
兼容诊断入口。正常启动不再自动加载 Eula/VMD 私有资产；当前角色观察和验收应使用
`--model-pose-views`、`--character-acceptance` 和 `--character-world-capture`。

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

- 新增启动参数：options 头文件字段、`rasterfall_options_init/parse/usage`，再接入 `main()`。
- 改键位或鼠标：`build_game_command()`、`consume_game_command_edges()` 及主循环的菜单/控制台分流。
- 改启动或暂停界面：`run_startup_menu()`、`draw_pause_overlay()`；HUD 主界面在 `rasterfall_hud.c`。
- 改射击视听同步：`sync_*_fire_effects()`、`emit_ray_effects()`，并核对网络序列号与游戏事件。
- 改镜头晃动：`rasterfall_effect_event.h`、`rasterfall_effects.c` 的 `CAMERA_SHAKE`，以及主循环中
  `render_camera` 的展示态应用；受击 preset 宏也在 `rasterfall_effects.h`；不要修改权威 `camera`
  或网络快照。
- 改自动化/截图/性能参数：先查 options，再查 `main()` 中窗口创建前的诊断早退和帧尾输出。

平台 API 不在本目录：窗口与输入分别在 `lib/platform/window_wayland.c`、`lib/input/input.c`，
Windows 替换实现位于 `windows/src/`。只有跨应用的平台缺陷才应修改这些公共层。
