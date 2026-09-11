# RF Application Runtime V0

> 文档更新：2026-09-11
> 源码核对基线：工作区（`rf_app` / `rf_app_manager` registration、open/close/update/render；CORE STATUS、PERSONNEL、TERMINAL 迁移；Application API Stabilization V0 Phase 1 ownership audit）

RF Application Runtime V0 是 GUI Runtime 上层的最小 application model。它不是完整 UI
framework，不拥有窗口、输入、renderer、地图或玩法状态。

## 所有权

`struct rf_app_manager` 由 `rf_game_runtime` 持有，维护有限容量的 registered app descriptors
及其 open/window 状态。`struct rf_app` 提供 application identity、icon binding、lifecycle
callbacks 和 presentation text；manager 通过既有 `rf_gui_context` 创建/关闭窗口。

GUI 继续拥有 cursor、icon hit testing、窗口几何、拖动和 close button。点击 icon 后由 GUI
转发到 `rf_app_manager_open_icon()`；窗口内容由 `rf_app_manager_render_window()` 调用 app
的 render callback。每帧 active app 的 update callback 由 runtime 调用。

窗口只保存屏幕几何、标题和 application identity；正文及其它 application presentation state
由 app manager/application callback 保持，GUI 不保存 application-owned 文本或 snapshot。

## V0 API

- `rf_app_manager_register()`：注册 application。
- `rf_app_manager_open()` / `rf_app_manager_open_icon()`：创建对应 GUI window。
- `rf_app_manager_close()`：关闭 application 及其窗口。
- `rf_app_manager_update()`：更新已打开 application。
- `rf_app_manager_render_window()`：将 application 内容提交到既有 window。

当前默认注册三个 app：`CORE STATUS`、`PERSONNEL`、`TERMINAL`。它们保持固定文本展示；
Terminal command execution 仍归既有 Terminal/Console frontend，不在本阶段复制。

Application Projection Layer V0 已为 PERSONNEL 提供只读 snapshot。Application descriptor 仅借用
query context；GUI 和 Terminal 都通过 projection 读取人员数据，不直接暴露 `toy_game`。

## 边界与验证

不引入 widget、layout、UI editor、world terminal entity、IPC 或新的 renderer path。
`rf_app_manager_logic_test()` 由 `build/rasterfall --logic-test` 聚合执行。
