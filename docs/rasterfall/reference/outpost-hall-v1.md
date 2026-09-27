# Outpost V1 与设施家具

> 状态：当前
> 所有者：前哨站空间与设施家具
> 事实入口：`assets/maps/outpost.map`、`assets/worlds/outpost.content`、`tools/blender/generate_rasterfall_props.py`

## 单层基地合同

地图边界约 42 × 45 m，开放屋顶，所有楼层均为高度 0 的相邻 ground surface。大厅原点居中，北为正 Z；西翼 Research、东翼 Operations、北翼 Infrastructure、南侧测试场都从大厅直接到达。南门外前坪与测试场接壤；北门外原浅平台已并入基础设施地面。

西、东侧门使用 `arch_doorway` 组件的两肩与架空过梁碰撞；其余翼楼外墙和设施分隔使用 `boundary_wall` 组件。北侧服务走廊通过两扇横向门进入 Power 与 Control。各房间外墙、地面和对象均为地图事实；家具不声明玩法终端。

Research 只放少量维护件并标记未来空间；Operations 放工作台、箱体与储物；Infrastructure 放动力机组、控制柜、通风、线槽与检修件。测试场使用开放中轴、边界墙、门架、路障和动力机组，不声明可射击目标的玩法逻辑。

各区的 `render kind=sign` 是地图绘制的单层标识牌，不是玻璃或独立模型。牌面沿 X 轴展开，位于 Z bounds 中心；`height` 和 `attr.height2` 是世界 Y 轴上的牌底与牌顶，地面基准为 -900 RFU。字色直接嵌入牌面。`attr.facing` 指定从 `+z` 或 `-z` 一侧看文字为正向；北墙朝大厅或房间的一侧使用 `-z`，南墙朝内的一侧使用 `+z`。`attr.style=1` 表示无支杆的墙面牌；Power、Control、Research、Operations 和入口标识均使用此样式，尺寸控制为墙面导视，不覆盖整面墙。

## 大厅与家具合同

大厅墙线宽 18 m、深 16 m、墙高约 4.2 m，单层、无实心屋顶。南北墙顶各一条梁线，
每条由三段原尺寸 6 m 梁组成，梁底落在墙顶；中央指挥区上方留空。东西墙使用四块
4 m 墙壳，南北两端使用连续墙段与中央 doorway。门洞净宽约 4.8 m、净高 3.6 m，
碰撞使用两肩与架空过梁，不能以整块 AABB 封口。墙顶梁额外高出墙体 0.6 m。

南入口外有短前坪并通向测试场；北出口直接进入 Infrastructure 服务走廊。
Command Floor 为 8×7 m，中央桌面为 4×2.4 m；桌子至该区域边缘横向 2 m、纵向 2.3 m。
高柜、线槽、检修面板和灯全部靠墙，北侧设备带约占墙宽一半，门洞与外围空墙保持干净。

Null 工位在东侧，椅子向西看大厅，双屏向东面对操作位；Null 站在工位侧边，不穿过椅子或桌子。
西侧两张普通工作桌移到侧门两边，使用相同家具族。南侧终端偏离入口中轴，另一侧放长椅。
地面由相邻 surface/collision 与 ground paint 分区，沿用 4 m 低对比接缝；不叠地面模型。

## 七件家具

东北设备区使用 [Host Rack V2](host-rack-v2.md) 的两排四列候选机柜。东两列 Memory 靠角落，
西两列 CPU 靠大厅；机柜按主机硬件数量启用，朝向与墙线平行，两排之间留观察通道。
每柜最多六槽，CPU 一槽对应一个物理核心，Memory 一槽对应最多 4 GiB。
其资产预算和映射合同独立于下述普通家具。

尺寸顺序为 Blender X/Y/Z 米。原点为底部中心，Blender -Y 正面转换为 GLB +Z，
GLB 米制、RMESH 232 units/m、展示边界 512 RFU/m，实例默认 scale=1000。

| 资产 | 尺寸 m | 轮廓与使用 |
| --- | --- | --- |
| `rf_facility_desk` | 1.8×0.75×0.75 | 薄厚明确的桌面、双侧脚架、后挡板 |
| `rf_facility_chair` | 0.6×0.6×1.0 | 四脚、座面与低背，无坐姿或工作动画 |
| `rf_facility_monitor` | 0.65×0.15×0.45 | 单屏与底座；桌上放置 y=384 RFU，可重复组合 |
| `rf_facility_command_table` | 4.0×2.4×0.9 | 厚底座、凹入桌面、粗简图形，视觉核心 |
| `rf_facility_low_cabinet` | 1.2×0.45×0.8 | 暗色门板和粗把手 |
| `rf_facility_bench` | 1.8×0.55×0.5 | 无靠背长椅 |
| `rf_facility_terminal` | 1.0×0.6×1.5 | 通用站立终端，固定屏幕色块 |

每件三个无纹理 flat 材质：蓝灰主体、暗框、低饱和青色功能面。当前每件 48～192 个三角形，
400 为上限；允许低于旧工业道具的最低数量，不用细节填预算。不使用额外纹理、发光或高级灯光。
生成器、manifest 和注册表负责视觉合同；`rasterfall_map_components.c` 独立拥有简化实体碰撞。
桌、指挥桌、矮柜和长椅可站立，椅子与终端为普通阻挡；桌上屏幕实例不生成独立碰撞。

## 阶段边界

大厅中的普通终端仍为静态家具。中央指挥桌南侧（X ±1250、Z -1900 至 -850 RFU）现提供离线 FPS 互动：靠近显示 `E USE COMMAND TABLE`，按 E 将玩家放到桌前 (0, -1160)、朝北并解锁鼠标；屏幕显示地图列表与启动时从各地图文件的 surface/collision footprint 生成的俯视预览。鼠标悬停条目切换预览，点击条目直接部署，Esc 离开互动并恢复视角控制。桌上 `command_map_screen` 为无碰撞显示器家具；地图选择 UI 是展示状态，不写入 `toy_game`。
离线暂停菜单倒数第二项 `RETURN TO OUTPOST` 通过完整 world 重载回到前哨站出生点，并重置当前局的玩法与特效状态；联机时不执行本地单方面地图重载。
旧 Station / Operations / Super 和武大传送点从本大厅撤下，已有动作词汇仍保留供其他内容使用。
指挥桌地图屏幕提供 Campaign 与 WHU 部署入口；不提供 Station GUI，也不制作三套专用终端资产。
研究翼和 Infrastructure 房间已有静态空间。无动态门、多层、切顶、RTS 桌面交互、其他设备功能、武器测试交互或 NPC 工作行为。

运行时所有权沿用[地图与世界内容架构](../architecture/maps-and-world-content.md)。
生成、Windows 工具与截图见[设施家具指南](../guides/facility-assets.md)。
