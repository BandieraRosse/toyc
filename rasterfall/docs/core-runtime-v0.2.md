# RF Core Runtime V0.2 查询面设计

> 文档更新：2026-09-11
> 源码核对基线：工作区（Core status query、service access cleanup、Input view、runtime facade）

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

## 后续扩展规则

- 新增字段优先加入对应 snapshot，而不是暴露 `struct rf_core` 或扩大 runtime ownership。
- GUI/Terminal 需要的展示格式应在 presentation 层组装，不进入 `toy_game` 规则核心。
- 玩家、session 和网络字段如需跨进程或跨版本使用，未来另行定义显式 wire/query schema；本阶段不做 IPC。
- Core service failure 由 ready 状态表达；查询接口不尝试修复、创建或重启 service。
