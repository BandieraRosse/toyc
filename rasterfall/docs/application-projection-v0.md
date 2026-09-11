# RF Application Projection Layer V0

> 文档更新：2026-09-11
> 源码核对基线：Application Query Boundary V0；Core/Game query context 已建立，Application API 不暴露 `toy_game`。

本层是 Forward Station Application 与 Core/Game Runtime 之间的只读数据边界。它不拥有 Core、Game
Runtime、session 或 gameplay 状态，也不执行 command mutation。

## 数据来源

| Application 类型 | 唯一数据入口 | 允许内容 |
| --- | --- | --- |
| Core Application | `rf_core_status` | Core 版本、构建标识、window、renderer、filesystem、audio、clock readiness |
| Game Application | Game Runtime Query | runtime lifecycle、session active、network mode、本地玩家摘要及后续 application snapshots |

`rf_application_query_context` 只借用 Core/Game Runtime 和 Command Context。Application 只能通过 query
函数取得 snapshot，不能保存或转发 `struct toy_game`、`struct toy_game_actor`、session 或 Core service
指针。

## 责任边界

```text
Core / Game Runtime truth
          ↓ read-only query
Application Projection Layer
          ↓ value snapshot
GUI Application / Terminal Frontend
```

- Query 无副作用，不创建 service，不修复状态，不改变暂停或 session 生命周期。
- Projection model 按 application 语义组织字段，不复刻 gameplay struct 布局。
- Command Runtime 是未来 mutation 的唯一行为入口；本层 V0 不增加 mutation。
- GUI 与 Terminal 应消费同一个 projection，不能各自从 gameplay struct 拼装数据。

## 当前 API

- `rf_application_query_init()`：绑定借用的 Core/Game/Command context。
- `rf_application_query_core_status()`：只读取 `rf_core_status`。
- `rf_application_query_game_status()`：只读取 `rf_game_runtime_status`。

Personnel、Operations、Research snapshot 属于后续 projection；它们必须继续遵守上述边界。
