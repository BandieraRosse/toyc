# 网络代码导航

> 文档更新：2026-09-08
> 源码核对基线：工作区（客户端权威移动为长期协议模型；客户端按可信端处理；输入条目只编码 command、sequence/tick、选中槽位意图、airborne prediction report 和 fire validation rays；不编码 inventory/reload/cooldown/muzzle gameplay 镜像；输入协议版本 42；旧玩家快照已删除；actor snapshot 是玩家/AI/远端玩家 gameplay truth，world snapshot 只承载世界级状态；远端插值缓存只保存 derived render state；投射物/燃烧区显式携带 owner；本地预测位置驱动 camera）

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

主机处理远端玩家时，普通枪械的武器、命中和动画状态直接归入
`session->game_state.actors[remote_actor_index]`；不要再把远端玩家临时覆盖到
`toy_game` 的本地玩家字段。炸弹和 Molotov 通过
`toy_game_actor_throwable()` 直接作用于远端 actor；投射物和燃烧区保存稳定
`owner_actor_id`，爆炸/燃烧造成的伤害、击杀和 throwable 统计归属投掷者。
远端 shove 不再临时覆盖本地 actor 的位置，而是通过
`toy_game_shove_from_position()` 将远端位置作为只读规则输入。

客户端权威移动是长期架构，不是迁移期间的临时兼容路径。客户端负责根据本地输入推进自己的 body
position、airborne 状态和视角，并在输入包中冗余发送预测后的运动报告；主机将该报告写入对应 remote
actor，负责玩法结果、伤害、特殊控制、世界实体和快照发布。该边界意味着主机不重新模拟远端平移；
主机不重新模拟远端平移，也不以此边界承担反作弊职责；不要在 renderer 或旧 player 状态中建立第二个
位置源。

远端 reload、切槽和普通枪械开火均先经过 `toy_game_execute_actor_command()` 的 actor 规则边界；主机用
输入意图推进 actor 的换弹/切枪计时和库存，fire report 只提供客户端瞄准结果；主机负责验证武器、弹药、冷却、弹丸数量、伤害上限、
射程和基础命中几何后才写入伤害与统计。该校验用于阻止 malformed packet、非法状态和重复结算，
不重做完整墙体 raycast、不重建客户端散射；客户端长期按可信端处理。

主机侧 `rasterfall_net_client` 是 connection/input/protocol 状态容器，不是玩家 gameplay
镜像：`actor = gameplay truth`，而 `remote presentation cache = derived render state`；远端 actor
生命周期由 `toy_game_set_remote_actor()` 管理，网络客户端槽位不保存第二份 HP、武器或统计。
`latest_input` 只表示最新解码的输入/客户端报告；HP/down、animation/stats、airborne、武器计时器
和 inventory 均由对应 `game_state.actors[]` 持有。玩家和 AI 均通过 actor snapshot
表达，不再存在独立玩家 gameplay 镜像。

`rasterfall_effect_event` 是接收端的 presentation-only 扩展接口。纯视觉事件不加入 snapshot，
也不把 event 的原始 C 布局直接发送到网络；未来网络驱动表现必须增加明确的协议编码。

网络主机和客户端的远端开火展示由 `sync_network_fire_effects()` 统一适配为 muzzle billboard
和 tracer ray runtime instance；`fire_seq` 是去重边界。该路径只读取网络展示状态并写入
`rasterfall_effects`，不回写玩法状态，也不改变协议字段。

协议使用显式整数编码和单位换算；不要发送原始 C struct。Windows socket 适配位于
`windows/src/socket_winsock.c`，Linux 使用 Tinylibc 网络接口。网络测试可受沙箱和本机端口环境影响，
应把纯 packet/logic 测试与真实回环或公网验收分开报告。

本地玩家输入、快照和可靠事件的状态源是 `actors[TOY_GAME_PLAYER_ACTOR_INDEX]`；远端命令执行直接
绑定对应 actor。actor snapshot 同时表达玩家、AI 和远端玩家 gameplay 状态；输入条目在协议版本 42 中收敛为输入意图、
预测 metadata 与开火验证数据，旧的输入状态镜像字段不再编码。
