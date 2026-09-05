# 网络代码导航

> 文档更新：2026-09-05
> 源码核对基线：工作区（敌人快照已移除普通 AI 状态字段，特感能力字段仍显式同步）

## 文件职责

- `rasterfall_net.c` / `rasterfall_net.h`：UDP 协议、握手、客户端槽位、输入/快照、可靠事件、
  客户端预测与校正、展示插值、公开房间打洞。
- `rasterfall_net_transport.c` / `.h`：可替换的发送入口和传输层钩子；丢包模拟在上层 net 状态配置。
- `rasterfall_net_discovery.c`：局域网房间广播与浏览。
- `include/rasterfall_public_protocol.h`：公共打洞/房间服务共享协议。
- `app/net/rasterfall_punch_server.c`：公共房间协调服务端。
- `rasterfall_session.c`：权威 step、客户端 step/replay，以及网络命令最终落到玩法的边界。

联机状态分类、房间生命周期和人工验收清单见
[`network-architecture.md`](network-architecture.md)。

## 排查顺序

- 无法建房/加入：先分 LAN discovery、直接 UDP、公共房间打洞，再查 `main()` 的启动菜单和等待连接。
- 状态不同步：确认字段属于权威玩法、客户端预测还是纯展示；查 packet encode/decode 和 snapshot apply。
- 抖动或回弹：查 reconcile 与 presentation interpolation，不要在 renderer 中修权威坐标。
- 枪声/命中特效丢失：查可靠事件队列、fire sequence、主循环 `sync_network_fire_effects()`。
- 新增同步字段：同时检查主机采集、编码、边界/版本、解码、客户端应用、重置和相关测试。

`rasterfall_effect_event` 是接收端的 presentation-only 扩展接口。纯视觉事件不加入 snapshot，
也不把 event 的原始 C 布局直接发送到网络；未来网络驱动表现必须增加明确的协议编码。

网络主机和客户端的远端开火展示由 `sync_network_fire_effects()` 统一适配为 muzzle billboard
和 tracer ray runtime instance；`fire_seq` 是去重边界。该路径只读取网络展示状态并写入
`rasterfall_effects`，不回写玩法状态，也不改变协议字段。

协议使用显式整数编码和单位换算；不要发送原始 C struct。Windows socket 适配位于
`windows/src/socket_winsock.c`，Linux 使用 Tinylibc 网络接口。网络测试可受沙箱和本机端口环境影响，
应把纯 packet/logic 测试与真实回环或公网验收分开报告。
