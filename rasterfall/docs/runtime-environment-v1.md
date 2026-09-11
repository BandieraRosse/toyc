# RF Runtime Environment V1

> 文档更新：2026-09-12
> 源码核对基线：工作区（RF Core Runtime V0.2、Game Runtime Authority、Command Runtime V0、GUI Runtime Prototype V0、Application Runtime V0、Application Projection Layer V0、Map Runtime Rewrite Integration）

本文是 Rasterfall 当前运行时基础设施的稳定 checkpoint。V1 只整理已经存在的运行时边界，
不新增 gameplay、地图语义、GUI 控件或应用行为。

## 总体关系

```text
平台资源与服务
        ↓
Core Runtime
        ↓ 借用 Core service / input / clock
Game Runtime ─────→ Command Runtime
        ↓                 ↓
  session / game       Terminal Frontend
        ↓
Map Runtime

Game/Core query
        ↓
Projection Layer
        ↓
GUI Runtime ← Application Runtime
```

Core 和 Game Runtime 是事实来源；Command、GUI 和 Application 是运行时上层消费者；Projection
Layer 只把查询结果转换为独立 value snapshot。任何上层都不能复制 gameplay、session 或 Core
service 的所有权。

## 1. Core Runtime

`struct rf_core` 是唯一 Core context 和 service owner。它拥有 window、renderer、surface、input、
filesystem、audio 和 clock 的生命周期，并提供事件轮询、退出状态、tick 时间、frame begin/flush/end
以及只读 status query。

正常窗口路径由 Core 包住完整帧生命周期；Game 只借用服务并提交内容。headless logic-test 路径只
初始化 filesystem、input 和 renderer，不创建 window/audio。Core shutdown 负责释放自己拥有的资源。

入口：`include/rf_core_host.h`、`src/rf_core_host.c`。

## 2. Game Runtime

`struct rf_game_runtime` 是 Game facade。它拥有运行时调度所需的 presentation、网络、输入边沿、
摄像机、菜单、特效、HUD、音频和 fixed-step 状态；`rf_game_update()` 是唯一 gameplay update
调度入口，`rf_game_render()` 是 steady-state world/presentation/UI submission 入口。

玩法规则和权威状态仍由 `rasterfall_session` 与 `toy_game` 拥有。`rf_game_runtime_run()` 只负责
Core lifecycle、平台输入/网络轮询、fixed-step 节拍和 facade 编排。

入口：`include/rf_game_lifecycle.h`、`src/rf_game_lifecycle.c`、`src/rf_game_runtime.c`。

## 3. Command Runtime

Command Runtime 由 command registry、`rf_command_context`、`rf_command_output` 和 permission
level 组成。registry 保存命令描述、handler 和所需权限；context 是一次执行的借用上下文；output
是 frontend-neutral 的值结果；permission 检查归 Command Runtime。

`rf_terminal_session` 只保存 transient input、history 和最近一次 output。Developer Console 是
当前 frontend，命令 handler 可通过 Core/Game query 读取状态，但不应把内部指针交给 application
或长期保存。

入口：`include/rasterfall_console.h`、`src/rasterfall_console.c`。

## 4. GUI Runtime

`rf_gui_context` 拥有 screen-space cursor、icon hover、window geometry、drag 和 open 状态；它
通过 Core input view 接收输入并在既有 surface 上提交 presentation。GUI window 只保存几何、标题和
application identity，不拥有 gameplay、地图、renderer 或 application snapshot。

当前 GUI 是固定 desktop/icon/window 原型。GUI 不扩展为 widget、layout 或 UI framework。

入口：`include/rasterfall_gui.h`、`src/rasterfall_gui.c`。

## 5. Application Runtime

`rf_app_manager` 由 `rf_game_runtime` 持有，负责有限容量 application descriptor 的注册、open/close、
update 和 render dispatch。application descriptor 持有 identity、icon、lifecycle callback 和
presentation callback；GUI 仍拥有窗口生命周期的视觉状态。

当前默认 application 是 CORE STATUS、PERSONNEL 和 TERMINAL。它们不拥有 Core、Game、session、
地图或窗口资源；application 读取数据必须经过 Projection Layer。

入口：`include/rasterfall_app.h`、`src/rasterfall_app.c`。

## 6. Projection Layer

Application Projection Layer 是 Core/Game Runtime 到 GUI/Application/Terminal 的只读边界。
`rf_application_query_context` 只借用 Core 和 Game Runtime；projection 每次清零并重建调用方提供的
snapshot，输出不含 gameplay 指针，因此调用方可独立读取该 value。

当前包含 Core status、Game runtime status 和 Personnel snapshot。Projection 不创建 service、不修改
暂停或 session 生命周期，也不执行 mutation。未来新增 application 数据必须继续使用 snapshot 边界。

入口：`include/rf_application_projection.h`、`src/rf_application_projection.c`。

## 7. Map Runtime relationship

Map Runtime 属于 session 生命周期。`rasterfall_session` 负责 level/map 的 load、runtime binding、
reset 和 unload；Map Runtime 负责解析后的稳定 ID 查询与运行时视图，`src/rasterfall_map.c` 再将其
适配到现有 gameplay、collision 和 renderer 接口。

Game Runtime 可以编排 session，但不复制地图真值；GUI、Application 和 Projection 也不能直接持有
地图 parser、level 或 gameplay map pointer。地图格式和玩法绑定保持既有分层，本 checkpoint 不修改
`.map` 语法或地图数据。

入口：`rasterfall_session`、`lib/rasterfall_map_runtime.c`、`src/rasterfall_map.c`；地图契约见
[`map-format.md`](map-format.md)。

## 当前明确不包含

- world terminal entity
- operations system
- research system
- runtime patch
- module loader

这些项目不是 V1 的隐含扩展点，也不应通过增加 GUI application、地图记录或 command handler 的方式
提前接入。它们未来若启动，必须另行定义所有权、snapshot/mutation 边界和验证矩阵。

## 验证入口

- `make app-rasterfall`
- `build/rasterfall --logic-test`
- 适用的 `--visual-capture` / model acceptance CLI
- `git diff --check`

V1 checkpoint 不改变现有 CLI、玩法规则、地图格式、网络协议或资产契约。
