# RF Core Runtime V0.2 查询面设计

> 文档更新：2026-09-11
> 源码核对基线：工作区（Core status query、service access cleanup、Input view、runtime facade、Runtime Facade Authority audit；RF Command Runtime V0 status command；RF Terminal Frontend Prototype V0 session/frontend）

本文只定义前哨站 GUI、游戏内 Terminal 和 Super Terminal 的后续读取边界，不实现任何 UI、
terminal、IPC 或额外进程。

## 查询分层

| 查询对象 | 入口 | 所有者 | 允许内容 |
| --- | --- | --- | --- |
| Core service | `rf_core_get_status()` | `struct rf_core` | 版本/构建标识、window、renderer、filesystem、audio、clock ready 状态 |
| Game runtime | `rf_game_runtime_get_status()` | `struct rf_game_runtime` | runtime initialized/running/paused、session active、network mode、本地玩家摘要 |
| Active session | `rf_game_runtime_status.session_active` | `rf_game_runtime` | 是否存在活动 session；不转移 session 所有权 |
| Local player | `rasterfall_session_local_player_const()` | `rasterfall_session` / `toy_game` | 只读 player actor access point；调用方不得写入或缓存为第二份真值 |

Core status 和 runtime status 应由上层分别查询后组合。Core service 状态不应复制到 Game，
玩家状态不应写入 Core。所有入口都是无副作用查询；返回的指针只借用当前 session 生命周期。

RF Command Runtime V0 的 `status` 命令是一个组合消费者：通过 `rf_command_context` 获取 Core 与 Game
runtime，再分别调用上述 snapshot API。命令层不暴露或缓存 Core service、runtime、session 或 actor 的内部指针。

Terminal frontend prototype 的 session 边界如下：`rf_terminal_session` 只拥有输入 buffer、有限
history 和最近一次 `rf_command_output`。它借用调用方提供的 `rf_command_context`，再调用既有
command registry/handler；registry、handler、permission 和 Core/Game snapshot 所有权都不下沉到
session。Developer Console 是当前 frontend，未来 F12 Terminal、World Terminal Device 和 Super
Terminal 可以替换 UI/输入输出适配，继续共享 `rf_command_context`、`rf_command_output` 与 registry。
World Terminal Device 属于未来 frontend/世界交互工作，不在本 prototype 中创建 map entity 或修改地图。

## 后续扩展规则

- 新增字段优先加入对应 snapshot，而不是暴露 `struct rf_core` 或扩大 runtime ownership。
- GUI/Terminal 需要的展示格式应在 presentation 层组装，不进入 `toy_game` 规则核心。
- 玩家、session 和网络字段如需跨进程或跨版本使用，未来另行定义显式 wire/query schema；本阶段不做 IPC。
- Core service failure 由 ready 状态表达；查询接口不尝试修复、创建或重启 service。

## Runtime Facade Authority audit（V0.2 收敛计划）

本审计以 `src/rf_game_runtime.c` 的 `rf_game_runtime_run()`、
`include/rf_game_lifecycle.h` 的 `struct rf_game_runtime` 以及
`rasterfall_session` / `toy_game` 的现有边界为准。此阶段只记录迁移边界，不改变玩法规则、
actor 布局或网络协议。

| 状态 | 当前现场 | 当前 owner | V0.2 迁移目标 |
| --- | --- | --- | --- |
| session | `run()` 使用文件级 `session`，同时 `rf_game_runtime.session` 指向它 | `rasterfall_session`（对象由 runtime run 的宿主静态存储提供） | runtime 持有 session reference；session 仍拥有玩法数据 |
| network | `run()` 的局部 `net` / `discovery`；facade 内另有 `runtime.net` / `runtime.discovery` | 两份容器，实际循环使用局部份 | `runtime.net` 和 `runtime.discovery` 为唯一运行时引用 |
| effects | 文件级 `effects`；`runtime.effects` 也存在，初始化和 render bind 仍混用 | 两份 presentation pool | `runtime.effects` 唯一 owner；render context 只借用它 |
| camera | `run()` 局部 `camera`；facade 有 `camera` / `render_camera` | 局部 camera 是真实 gameplay/view 输入 | runtime 的 body camera 为唯一运行时 camera；render camera 是每帧派生值 |
| running / paused | `run()` 局部变量；facade 有同名字段 | 局部变量控制主循环和暂停分支 | `runtime.running` / `runtime.paused` 是唯一 lifecycle/menu 状态 |
| menu | `pause_menu`、`managed_terminal`、startup locals、`menu_selected` 等局部状态 | `run()` 局部与启动菜单函数参数 | runtime 持有 pause/menu/terminal 的控制状态；startup fixture 保持局部直到后续边界完成 |
| input edges | `run()` 局部 `pending_key_edges`、pointer/fire/shove 边沿 | `run()` | runtime 字段唯一保存跨逻辑步的边沿；Core input frame 仍是只读输入源 |
| fixed-step clock | `run()` 局部 `last_time`、`accumulator`、`prev_begin` 等 | `run()`，时间值来自 Core | runtime 持有 simulation accumulator；Core 仍拥有 clock service |
| audio / perf / HUD controls | `run()` 局部 `audio`、stats、坐标轴/FPS/managed flags | `run()` | runtime 持有 presentation/perf 控制；Core audio 仍是 service owner |
| local player | 从 `session.game_state.actors[0]` 或 session const query 即时读取 | `toy_game` / `rasterfall_session` | 不复制 actor；runtime status 只生成 snapshot/query view |
| gameplay state | `session.game_state` 及其 actor/event 数据 | `toy_game` | 保持不变，不迁移进 runtime |

### 分阶段迁移顺序

1. Phase 1：保留现有行为，完成上述重复项和局部项清单，作为后续修改的 ownership contract。
2. Phase 2：把已存在的 runtime 字段接管真实 session/network/effects/camera/lifecycle/menu 状态；
   `rf_game_runtime_get_status()` 只从 runtime 真值和 session const query 生成快照。
3. Phase 3：将 fixed-step 更新与 render submission 提取到 `rf_game_update()` / `rf_game_render()`；
   `rf_game_runtime_run()` 只保留 Core host frame lifecycle、输入采集和两个 facade call。
4. Phase 4：在不暴露 `toy_game_actor` raw pointer 的前提下，补齐 runtime status、player summary
   和 personnel query 的最小只读 snapshot 接口。

迁移期间的硬约束：不引入第二份 session/network/effects 真值，不把 renderer global context 改成
新的 owner，不修改 `toy_game` 数据结构、AI、网络 wire format 或 loader。
