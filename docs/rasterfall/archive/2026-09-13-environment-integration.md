# Campaign 环境组件整合 checkpoint

> 状态：历史
> 归档原因：阶段记录，当前设计与执行顺序已转入维护入口
> 当前入口：[Rasterfall 维护者入口](../README.md)

日期：2026-09-13。这是本轮现场验收记录；当前入口见[工业组件资产规格](../reference/industrial-props.md)、
[地图格式](../reference/map-format.md)、[渲染架构](../architecture/rendering-architecture.md)和[视觉验收](../guides/visual-validation.md)。

## 地图成果

直接修改正式 Campaign 的 rasterfall.map，未修改 rasterfall_legacy.map 或 Outpost。
原十件陈列实例保留，正式地图合计 68 个实例，新增 58 个；另有五块纯视觉设施地面色区。
既有十类组件均被使用，新增设施主体遵循原 V2 Hybrid 色彩和几何语言。

- BASE 外缘只放稀疏路障、灯柱；中央战斗与 RESP/ASLT 旗帜部署空间保持开阔。
- 西侧维护岛组合动力机组、双 vent/pipe 节奏、工位、弹药、货箱和控制柜。
- 东侧设备岛组合动力机组、通风设备、管组、短墙、立柱与控制柜，留出较宽设施前场。
- 北区动力端站提供中距离锚点，位于两翼刷怪区前方；北门/Hurd 用开放门架、灯柱和边缘补给形成入口与驻防区。
- 东西走廊复用门架和边缘控制柜；保留原坡道及通道宽度。
- 南侧维修区复用门架、工位、货箱和控制柜；删去重复工作台，避免把密集测试区继续堆满。
- 开发坡道旁以少量 railing/vent/lamp 表达服务边界；东南动力场组合主机、pipe、vent 和控制柜。

## 实际 kit gaps 与选择

首轮使用既有组件后，小设备在大院里缺少可辨认的设施主体；孤立立柱没有横向结构；
维修与补给组合缺少控制界面。由这些实景问题选择三件新组件：

| 组件 | 三角形 | 正式复用 | 选择理由 |
| --- | ---: | --- | --- |
| power_unit | 488 | 西、东、北、东南动力场，共 4 处 | 主机体提供体量层级，vent/pipe 成为从属设备 |
| gate_frame | 476 | 西走廊、东走廊、Hurd 入口、南维修区，共 4 处 | 开放门架把立柱与横梁组合成通道语法，无门扇和动态机制 |
| control_cabinet | 236 | 西、东、北、东南、南工位、Hurd、东西走廊，共 8 处 | 小型通用控制接口，低对比显示面和粗编号 |

三件均复用 generate_rasterfall_props.py 的 Builder、palette、flat 主体/暗框/功能色和
32×32 局部 sign，经 GLB → industrial manifest → unified import → 公开 RMESH/TTEX →
registry/profile → V1 object → 正常 renderer 完整集成。每件四个材质/primitive；米制、
底面中心 pivot、identity transform 与三角形预算通过生成器断言。两次独立 GLB 输出逐字节一致。

未生产的候选：独立 structural beam 被开放门架覆盖；pipe corner/junction 留待真正管线组合；
railing corner/end 在本轮直线服务边界中收益低；stairs/platform/catwalk 会扩大既有坡道碰撞与
高度整合范围；wall-mounted panel 暂以独立控制柜覆盖接口需求，不为它扩展高位 prop placement。

## 视觉闭环

经历既有组件首轮、资产整合、密度清理、设施朝向/间距复查。真实世界截图复用正常
scene/actor/flag/depth 路径，固定 seed，不推进玩法。最终十二视角覆盖整体空间对应的基地、
出生室、北区、北设施、东西设施、东西路径、Hurd、南维修区、坡道和东南动力场。
布局 PNG/JSON 与 World Content 合并布局均从当前源重新导出。

实景确认：设施主机与从属设备关系比首轮小 prop 集合清楚；门架提供方向辨识；
设施集中在边缘和岛状前场，中轴和基地角色可读。清理重复工位，修正南动力场设备正面朝向，
拉开北控制柜与主机、东南 vent/pipe 与主机间距。还调整了被既有坡道遮挡/落在坡面内的验收镜头。
先前最终布局的十二张 BMP 已做重复逐字节一致检查；最后间距微调重新截图并检查对应区域。

截图和组图保留在 tmp/environment/，不提交。复核命令：

```sh
make map-layout world-layout WORLD=campaign_01
build/rasterfall --environment-capture tmp/environment-review --textures
python3 tools/environment_sheet.py tmp/environment-review
```

## Gameplay / collision 与必要修复

原 map/world/region/collision/surface/actor_spawn/pickup/interaction、原 render 和原 object
记录逐条保留。World Content、AI、波次、武器、网络、角色和 RF Core 架构未改。
V1 object 当前只投影展示，不自动创建碰撞；本轮新增设施也不创建 collider，因此不能把它们
当新掩体或可站立平台。门架开放中心不使用整件 AABB 阻挡通道。

必要最小修复：

- projection count gate 增加 props/object 数量核对，防止缺少连续 legacy_index 的实例只出现在布局而未渲染。
- Builder 面朝向用法线正负而非整数截断判断，修复门架近似单位法线导致 sign 丢失。
- 原逻辑坡道用例在坡面用零高度 enemy placement，改从相邻平地创建；高度断层 fixture 再设置原预期坡面位置。
- 原 Tank 用例 40 tick 扣除 attack-start tick 只有 624ms，早于 625ms impact；改为有超时的真实命中观察。
- 原开发坡道与墙顶平台存在高端重叠，严格共享边缘规则令玩家卡在 z=-6392；补平台覆盖高端且继续向外的连续性。
  仍检查中心/坡面支撑和正常端点高差，原 Charger、侧攀、空气墙及断层回归继续通过。

## 验证与边界

Linux freestanding 主构建、--logic-test、map parser/runtime、world content/layout、map layout
export/query、asset pipeline、三个安装资产 validate-only 和 git diff --check 通过。
Blender 导出门禁和两次 GLB 确定性检查通过；正常世界 capture 无 command overflow。
Windows MinGW 主构建和 ZIP 打包通过，ZIP 检查包含三个新 RMESH 及对应 TTEX。
Windows 编译已有字符串截断与整数范围警告；未扩大到无关清理。

本轮使用离屏实景验收。尝试 `--frames 1 --dump-frame` 窗口启动时无法连接
`/run/user/1000/wayland-0`，未覆盖人工 FPS 试玩、窗口/音频体验或真实联机拓扑；
静态无敌人 fixture 的可读性不等于所有战斗密度下的人工验收。新增实例仍有 CPU 光栅化成本，
未声称已完成帧率/战斗性能基准。已有 aggregate logic 中禁用的旧 animation test 保持原状。

## 下一轮最高价值缺口

1. 可实际拼接的粗管线直段/弯头/端站连接，避免 pipe 永远只有独立端站轮廓。
2. 与旧坡道一致的少量承重梁/支撑语法；先解决真实高位 placement，而不是拿 crate 垫高。
3. 墙面控制/检修模块与高位安装表达，补长直墙的功能分区。
4. 设施明确碰撞策略与人工战斗检查，处理大型可见主机目前可穿行的 presentation 限制。
5. 大院外围墙的少量重复结构节奏及中远距离性能验收，避免只靠新增杂物增加密度。
