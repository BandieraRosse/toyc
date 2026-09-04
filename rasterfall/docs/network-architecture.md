# Rasterfall 联机架构与扩展边界

> 文档更新：2026-09-04
> 源码核对基线：`92aab4e`（effect event / presentation cue 基础层）

本文记录联机实现必须保持的内部边界。产品入口和平台范围见 `../README.md`。

## 模块职责

- `include/rasterfall_public_protocol.h`：游戏和公网协调服务共享的房间范围、传输模式、消息类型、
  角色和错误码。房间号是打洞或 relay 模式的唯一事实来源。
- `src/rasterfall_net_transport.c`：Linux/Windows UDP socket 的打开、关闭、收发和单调时钟；
  不解释公网协议或 gameplay 数据。
- `app/net/rasterfall_punch_server.c`：一个房间号对应一个房间、一个 host 和三个等价 guest
  槽位；负责注册、匹配、租约和 relay 转发。
- `src/rasterfall_net.c`：公网连接阶段和 gameplay replication。后续拆分 codec 时不得把 socket
  平台分支重新放回该文件。

## 房间生命周期

- host 注册不存在的房间时创建房间；已有不同 host 时返回 `ROOM_EXISTS`。
- guest 只能加入已有且 host 活跃的房间；三个槽位等价，按首个空闲槽分配玩家 ID 1～3。
- guest 满员返回 `ROOM_FULL`；服务器无空闲房间记录时返回 `SERVER_FULL`。
- guest 超时只释放自己的槽位；host 超时销毁整个房间。
- `0000`～`4999` 只打洞，`5000`～`9999` 只 relay；失败不得切换模式。

## 新玩法状态分类

新增字段前必须先选择一种所有权，不能因为现有快照中有空位就随意塞入：

| 类别 | 所有者与传输 | 典型内容 |
|---|---|---|
| Input | 客户端权威，冗余发送 | 移动、视角、跳跃运动状态、按键命令 |
| Snapshot | 主机权威，周期发送，可被后续状态覆盖 | 玩家生命与库存、Actor、Enemy、世界阶段、投射物 |
| Reliable event | 主机分配事件 ID，确认前重发 | 购买结果、拾取、复活、特殊控制开始/结束 |
| Presentation | 接收端从规则状态推导，不上网 | 动画混合时钟、模型 LOD、粒子、镜头平滑 |
| Local only | 永不进入协议 | Pose Editor、模型预览、开发者标定状态 |

战斗表现事件遵循 Presentation 类别：由权威射击/命中结果或接收端已有展示数据生成，交给
`rasterfall_effects` 消费；不能把粒子、tracer、镜头抖动等视觉状态反向写入 gameplay。

持续状态不能只发送事件：中途加入或事件丢失后，下一份完整快照必须能够恢复它。纯表现字段不能
反向成为 gameplay 判定依据。改变任何现有 codec 的字段顺序、宽度或语义时直接提高
`RASTERFALL_NET_PROTOCOL_VERSION`，不保留旧布局分支。

## 人工联机验收

网络修改完成后向维护者列出：运行平台与角色、房间号、操作顺序、预期 UI/日志、应观察的玩家
或世界状态。最终正确性由 Windows 为主的真实联机验证确认；局部自动检查不替代该结论。
