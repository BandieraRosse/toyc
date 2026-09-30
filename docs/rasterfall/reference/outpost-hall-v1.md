# Outpost V1 与设施家具

> 状态：当前
> 所有者：前哨站空间与设施家具
> 事实入口：`assets/maps/outpost.map`、`assets/worlds/outpost.content`、`tools/blender/generate_rasterfall_props.py`

## 单层基地合同

地图边界约 89 × 93 m，开放屋顶，所有楼层均为高度 0 的相邻 ground surface。大厅原点居中，北为正 Z；西翼 Research、东翼 Operations、北翼 Infrastructure、南侧测试场都从大厅直接到达。南门外前坪与测试场接壤；测试场南门经东西向通路到达露天角色实验场，再由南侧通路到达步行实验区和原创 RF 模型观察区；北门外原浅平台已并入基础设施地面。

西、东侧门使用 `arch_doorway` 组件的两肩与架空过梁碰撞；其余翼楼外墙和设施分隔使用 `boundary_wall` 组件。北侧服务走廊通过两扇横向门进入 Power 与 Control。各房间外墙、地面和对象均为地图事实；家具不声明玩法终端。

Research 使用独立的 [Research V1](research-renovation-v1.md) 设备族，包含实验算力集群、Core Analysis、原型测试台与物理终端，保持开放中央通道。Operations 放工作台、箱体与储物；Infrastructure 放动力机组、控制柜、通风、线槽与检修件。测试场使用开放中轴、边界墙、门架、路障和动力机组，不声明可射击目标的玩法逻辑。

标准实验区为 20 × 18 m 矩形：X ±5120、Z -24064 至 -14848 RFU，四边用 64 RFU 宽的地面标线界定，无墙、围栏或门。北侧 Z -14848 至 -11776 留出 6 m 宽的东西向通路，通路延伸到地图东西边缘，供后续相邻实验区接入。地面标线与内部地面绘制范围互不重叠。一个区域使用一个开关；当前东北侧 `lab_showcase_button` 位于 (4000, -15400)，靠近后按 E 显示或隐藏全部测试角色，初始及离开前哨站后为隐藏。按钮不创建玩法 actor，不影响联机权威状态。

当前包含 Block 与 Humanoid 两个感染体家族，每族 Common、Fast、Heavy 三种，共六列；每列北排 idle、南排 move，共 12 台。西三列为 Block，东三列为 Humanoid。移动台原地展示，以各类型实际速度范围的中值、16 ms 步长产生虚拟位移，调用实机 `rasterfall_infected_sample_motion`；静止台使用同一采样器的零位移输入。两种渲染器消费同一份已采样结果，并复用实机六份不可变 RFCHAR 资源及 `enemy_visual_apply_pose`，不维护实验场专用步态公式。展示属于外观和动画检视，不模拟追击、碰撞或攻击行为。

原实验区南侧通过 6 m 通路连接第二个同规格的 20 × 18 m 无围栏实验区（X ±5120、Z -36352 至 -27136）。这里的三条纵向直线分别展示 Humanoid Common、Fast、Heavy；角色在 Z -28700 至 -34700 间往返，端点折返并转身。每种类型按实机速度范围中值和 16 ms 步长推进世界位置，同时把真实位移送入共享步态采样器。第二个区域的东北侧有独立按钮，初始及离开前哨站后隐藏；两个区域各自开关，不创建玩法 actor。地面、64 RFU 边线、标牌和按钮沿用第一个区域的设置。

GPU 实机复现可运行 `powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 run --map rasterfall/assets/maps/outpost.map --renderer gpu-scene --gpu-normal-scene character-lab 0 --frames 5 --frame-audit`；此显式诊断镜头预先显示 12 台角色，正常进入地图仍默认隐藏。

第二个区域使用同一命令并将 `character-lab` 改为 `walk-lab`；该镜头预先显示三名往返行走者。

北侧角色实验区的东面新增两块同规格的 20 × 18 m 无围栏实验区，分别为 X 11264 至 21504 和
X 23552 至 33792 RFU，Z 均为 -24064 至 -14848 RFU。北侧原东西通路延伸到东边界，
两块区域各有独立开关，初始及离开前哨站后隐藏。地图标线、地面、按钮和标牌沿用原实验区。

`AI_ACTION_LAB` 固定台位按 11 种现有玩法动作展示 Block 程序队友。Humanoid 模块队友只展示
现有的 idle、move、fire 姿态，其他对应台位保持空缺并标记 `EMPTY`；两种角色都不改变世界位置，
动作时间独立循环。`AI_WALK_LAB` 使用 Block 与 Humanoid 两种队友外观，各按 AI 1、2、3 级
组成六条纵向往返线；每名角色使用对应等级的玩法移动速度，沿 Z -16600 至 -22400 RFU
按 16 ms 步长移动并在端点转身。展示角色不占玩法 actor 槽位，不进入网络或存档；CPU 与独立
GPU Scene 都从展示值读取位置、朝向和动作。

可将上述 GPU 复现命令的镜头名分别替换为 `actor-actions-lab` 和 `actor-walk-lab`；
显式镜头预先开启对应展区，正常进入地图仍默认隐藏。

### 原创 RF 模型观察区

`RF_MODEL_LAB` 位于 AI 动作实验区南侧，X 11264 至 21504、Z -36352 至 -27136 RFU，沿用 20 × 18 m 的开放地面和 64 RFU 边线。北侧 6 m 通路向西连接感染体步行区入口，向北连接 AI 动作区。东北侧终端独立控制 GPU 角色预览，普通入口初始隐藏，切离前哨站后隐藏。当前展示 RF-C01 V22h 的站立与原地步行采样，可绕行观察；只支持已有不透明分色材质，完整持枪、纹理与动漫着色尚未签收。启动和捕获见 [Scene 工作流](../guides/gpu-scene-fixture.md#rf_model_lab-角色预览)，后续接入顺序由[私有角色活动计划](../plans/private-anime-character-gpu.md)拥有。

### 后续动漫角色的 GPU 接入门槛

动漫角色先按 [角色资产合同](character-assets.md)导入、验证骨架与材质，并提供可循环的独立 authored clip；展示时间与玩法动作时钟分离。当前感染体只有静态 idle 和程序步行，不构成动漫动作集。旧 PMX/VMD 正常帧开关处于关闭状态，不能仅打开旧宏复用为独立 Scene 角色。

Scene 当前的感染体动态源由 CPU 计算姿态和蒙皮几何，再按帧更新 GPU 资源，且单体提取上限为 2048 三角形。高面数动漫角色若直接走此路径，CPU 蒙皮、三角形提取及每帧上传会随展示实例数增长，也可能触及上限。正式接入应走角色专用来源，复用不可变 mesh/材质/纹理，按角色实例上传骨骼 palette 并在 GPU 蒙皮；展示站增加独立的资源身份、容量上限与退休规则。当前 RFCHAR 合同每顶点最多两个非零骨骼权重，不支持 morph target；需要更多权重或面部表情时先扩展资产、导入和 GPU skinning 合同。透明头发和衣物需明确有序透明、深度写入和双面材质策略；描边、toon 光照与阴影属于可选高级画质，不能借用 CPU `--edge-pass` 声称 Scene 已支持。按当前活动计划，先用一名原创 RF 角色的 idle/walk 在 Windows 单人 GPU Scene 核对遮挡、层序、姿态、附件和帧成本，再扩大角色或动作数量；CPU 动漫绘制和双后端画面对照不作为接入门槛。正式资源还需核对来源和发布许可。

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

大厅南入口西侧的 `main_terminal`（-1946, -3712 RFU）提供离线渲染控制：FPS 靠近时按 E 打开，离线游玩时也可从任意位置按 F1 打开；再次按 F1 或按 Esc 退出。鼠标或 1/2 切换 baseline 与高级功能页，点击功能或按 Enter 请求切换。baseline 页说明 CPU/Scene 共同默认功能；高级页显示当前后端、支持状态及各可用开关的请求/生效值。尚未实现或当前后端不支持的功能会说明原因并保留配置。终端只修改 presentation 状态，不写入 `toy_game`。

中央指挥桌南侧（X ±1250、Z -1900 至 -850 RFU）现提供离线 FPS 互动：靠近显示 `E USE COMMAND TABLE`，按 E 将玩家放到桌前 (0, -1160)、朝北并解锁鼠标；屏幕显示地图列表与启动时从各地图文件的 surface/collision footprint 生成的俯视预览。鼠标悬停条目切换预览，点击条目直接部署，Esc 离开互动并恢复视角控制。桌上 `command_map_screen` 为无碰撞显示器家具；地图选择 UI 是展示状态，不写入 `toy_game`。
离线暂停菜单倒数第二项 `RETURN TO OUTPOST` 通过完整 world 重载回到前哨站出生点，并重置当前局的玩法与特效状态；联机时不执行本地单方面地图重载。
旧 Station / Operations / Super 和武大传送点从本大厅撤下，已有动作词汇仍保留供其他内容使用。
指挥桌地图屏幕提供 Campaign 与 WHU 部署入口；不提供 Station GUI，也不制作三套专用终端资产。
研究翼和 Infrastructure 房间已有静态空间。无动态门、多层、切顶、RTS 桌面交互、其他设备功能、武器测试交互或 NPC 工作行为。

运行时所有权沿用[地图与世界内容架构](../architecture/maps-and-world-content.md)。
生成、Windows 工具与截图见[设施家具指南](../guides/facility-assets.md)。
