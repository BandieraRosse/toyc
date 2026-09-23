# Application、GUI 与查询投影

> 状态：当前
> 所有者：Rasterfall Game presentation
> 事实入口：`rasterfall/include/rasterfall_feature_freeze.h`、`rasterfall/src/rf_game_lifecycle.c`、`rasterfall/src/rf_application_projection.c`
> 最近核对：2026-09-23

本页描述保留在源码中的 Desktop、Application 与 Projection 边界，以及正常运行路径的冻结状态。进程、Core 和固定步长调度见 [运行时与主循环](runtime.md)。

## 当前运行边界

`RASTERFALL_DESKTOP_RUNTIME_ENABLED` 当前为 `0`。正常启动不初始化 GUI 或 app manager；F12、反引号和前哨站 terminal 请求只显示暂不可用的 HUD 提示。`desktop-v1` 离屏视觉场景和 GUI、app manager、projection 逻辑测试仍作为隔离验证入口。原型源码的存在不表示 Desktop 已进入 normal frame。

## 所有权和数据流

| 层 | 拥有 | 不拥有 |
| --- | --- | --- |
| Core Host | window、surface、input 和 service 生命周期 | application 或 gameplay 状态 |
| Game Runtime | 正常运行调度；启用原型时持有 GUI 与 app manager context | Core service 或玩法真值 |
| GUI Runtime | cursor、icon hit testing、窗口几何、焦点、拖动与开关状态 | application 正文、session、地图 |
| Application Runtime | 有限容量 descriptor、单实例 open/close、update/render dispatch | window 几何、renderer、玩法状态 |
| Projection Layer | 调用方提供的独立 value snapshot | Core/Game/session 指针的长期所有权 |

GUI 的 icon 操作交给 app manager；窗口仍由 GUI 管理，内容通过 application callback 提交。当前原型仅注册 `PERSONNEL` 和 `TERMINAL` 两个默认 application。前哨站原型共用同一 desktop，并默认打开 `TERMINAL`；这些行为只在隔离原型中有效。

`rf_application_query_context` 只临时借用 Core/Game Runtime。projection 每次清零并重建调用方提供的 snapshot，不暴露或缓存 `toy_game`、actor、session、Core service 或 Command Runtime 指针。PERSONNEL 从 session roster/actor/squad 关系生成只读人员 value；GUI 和 Terminal 共享这条投影链。命令执行仍归 Command Runtime，application projection 不执行 mutation。

## 扩展边界与验证

启用 Desktop 前需重新核对 feature gate、暂停和 pointer lock、窗口生命周期、application dispatch、projection 数据来源与 normal frame 视觉语义。不得仅解除宏开关就把隔离原型视为已验收功能。

现有隔离用例由 `build/rasterfall --logic-test` 聚合；`--visual-capture desktop-v1 --visual-output <path>` 验证固定视觉场景。实际参数以 `build/rasterfall --help` 为准。原型形成过程见 [Runtime 历史设计](archive/runtime-design-v0/README.md)。
