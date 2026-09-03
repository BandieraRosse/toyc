# 运行时与主循环

> 文档更新：2026-09-03
> 源码核对基线：`75a10cd`（将项目协作说明转向 Rasterfall）

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

主循环先轮询平台事件和网络，再保留按键边沿；固定 16 ms 逻辑步中构造
`rasterfall_command`，交给 session 或客户端预测路径；之后同步音频/特效并渲染。排查“偶发吞键”
时查看 `pending_key_edges`，排查帧率相关玩法差异时查看 accumulator 和逻辑步，而不是只看渲染帧。

## 常见任务落点

- 新增启动参数：options 头文件字段、`rasterfall_options_init/parse/usage`，再接入 `main()`。
- 改键位或鼠标：`build_game_command()`、`consume_game_command_edges()` 及主循环的菜单/控制台分流。
- 改启动或暂停界面：`run_startup_menu()`、`draw_pause_overlay()`；HUD 主界面在 `rasterfall_hud.c`。
- 改射击视听同步：`sync_*_fire_effects()`、`emit_ray_effects()`，并核对网络序列号与游戏事件。
- 改自动化/截图/性能参数：先查 options，再查 `main()` 中窗口创建前的诊断早退和帧尾输出。

平台 API 不在本目录：窗口与输入分别在 `lib/platform/window_wayland.c`、`lib/input/input.c`，
Windows 替换实现位于 `windows/src/`。只有跨应用的平台缺陷才应修改这些公共层。
