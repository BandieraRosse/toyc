# Outpost V1 扩建

> 状态：活动
> 所有者：前哨站地图
> 执行入口：`rasterfall/assets/maps/outpost.map`

## 目标

把已完成的大厅扩成可行走的单层基地核心：中央大厅四向连通，北侧服务走廊分到 Power 与 Control，西侧 Research 留白，东侧 Operations 放置工位与储物，南侧连接室外测试场。屋顶继续开放，使用同一套工业组件。空间和碰撞合同见 [Outpost V1](../reference/outpost-hall-v1.md)。

## 当前落地

- 地图边界约 42 × 45 m；97 个静态 object，13 片相邻地面，所有门使用组件洞口碰撞。
- 大厅原有 Command Table、Null 工位和家具保留，挡住侧门的家具移到墙边。
- Infrastructure 以服务走廊连 Power 和 Control；Operations 已有工作台和储物；Research 保持空白；测试场有障碍、目标位及动力机组。
- 区域设备和标识只作静态展示，不新增玩法交互或设施模拟。

## 本轮签收

1. 地图 parser/runtime 接受所有记录，新增房间、门洞与测试场可通行；大厅与外围墙仍阻挡。
2. 用布局 PNG/JSON 检查无地面重叠、房间拓扑及物件相对位置。
3. 用 `--environment-capture` 审阅大厅 FPS/RTS 视角，并在 Windows native GPU Scene 启动检查新静态场景。

## 后续

按真实画面修正翼楼构图、标识可读性和测试场目标；从实际 GPU Scene 采样决定建筑及道具密度。研究设备、交互终端、动态门、完整照明与生活家具留待后续明确需求。
