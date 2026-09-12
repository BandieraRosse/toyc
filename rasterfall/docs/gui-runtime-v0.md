# RF GUI Runtime Prototype V0

> 文档更新：2026-09-12
> 源码核对基线：工作区（GUI context、desktop icon hit testing、window drag/close/text presentation；Application API Stabilization V0 Phase 1 ownership audit）

本原型是 Game Presentation 上层的最小屏幕空间 GUI，不是 HUD、地图实体或通用 UI framework。
`rf_game_runtime` 持有 `struct rf_gui_context`，GUI 只消费 Core 的输入 view 和当前 surface，
不读写 `toy_game`、地图、world renderer state 或资产管线。

## V0 范围

- `F12` 仅用于打开 debug desktop；Station Terminal 使用 `ESC` 退出 desktop。
- 三个固定 icon：`CORE STATUS`、`PERSONNEL`、`TERMINAL`。
- Station Terminal 进入时切换为单应用 desktop，仅显示 `PERSONNEL` icon；左键命中 icon 打开人员窗口。
- 左键命中窗口标题栏右侧红色方块 `X` 关闭窗口；窗口关闭后回到 desktop。
- GUI 打开时暂停玩法并释放 pointer lock，关闭时恢复原暂停状态。

## 状态所有权

`rasterfall_gui.c` 拥有 cursor、icon hover、窗口几何、open/drag 状态；
`rf_game_runtime_run()` 拥有 ESC/F12、暂停和 pointer lock；`rf_game_render()` 在 world/viewmodel
之后调用 `rf_gui_render()`。V0 不包含布局系统、UI editor、前哨站实体、Terminal query
adapter 或 IPC。

GUI icon 的 application dispatch 已迁移到 `rf_app_manager`；窗口系统仍由本模块拥有。
application lifecycle 和三个默认入口见 [app-runtime-v0.md](app-runtime-v0.md)。
GUI window 仅拥有几何、拖动和 open 状态；application 标识用于 dispatch，正文由 app manager
在 render callback 中提交，避免 GUI 持有 application state。

`rf_gui_logic_test()` 由 `build/rasterfall --logic-test` 聚合执行。
