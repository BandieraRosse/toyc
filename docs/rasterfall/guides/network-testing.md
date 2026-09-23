# 网络测试与排查

> 状态：当前
> 所有者：Rasterfall 联机验证
> 事实入口：`rasterfall/src/rasterfall_logic_test.inc`、`rasterfall/src/rasterfall_net.c`、`rasterfall/include/rasterfall_public_protocol.h`
> 最近核对：2026-09-12

联机权威、输入、快照和展示状态的所有权见 [联机架构](../network-architecture.md)。验证先运行 `build/rasterfall --logic-test` 中的 packet/pipeline 用例，再根据改动范围做 Windows native 的真实拓扑测试。纯 codec 成功只证明编码、解码和本地逻辑，不能代替 host/guest 窗口观察。

## 选择排查入口

- 无法建房或加入：分别检查 LAN discovery、直接 UDP、公共房间打洞或 relay，再核对启动菜单和等待连接状态。
- 状态不同步：先判定字段属于权威玩法、客户端预测或纯展示，再检查 packet encode/decode、snapshot apply 和 actor 真值。
- 位置抖动或回弹：核对 reconcile、未确认输入重演和 presentation interpolation。
- 枪声或命中特效缺失：核对可靠事件、`fire_seq` 去重和 `sync_network_fire_effects()`。
- 新增同步字段：逐项检查主机采集、编码、版本和边界、解码、客户端应用、重置与回归用例。

## 人工拓扑验收

记录运行平台、host/guest 角色、房间号、操作顺序、预期 UI/日志，以及应观察的玩家或世界状态。按更改的传输模式分别测试 LAN、直接 UDP、打洞或 relay；不要用一个成功拓扑推断其他拓扑。

Enemy Visual V1 的观察场景为相同最新构建的 host + guest，在 Campaign 中观察 Charger windup/charge、Tank sweep，并分别让 host 和 guest 被击中。双方的低伏/挥击应跟随 snapshot timer；命中粒子只在实际命中时出现，重复快照不应重复产生粒子；本地受伤 shake 与玩法击飞一致。高延迟或丢包时，以可见快照到达判断表现，不以客户端表现帧推定主机伤害结算时间。

Windows GUI 进程等待和日志规则见 [Windows Native](windows-native.md)。
