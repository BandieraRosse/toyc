# Architectural Environment V1 资产与表面规范

> 状态：当前
> 所有者：建筑套件、管线端口与表面语言
> 最近核对：2026-09-14

生成与截图见[建筑套件指南](../guides/architectural-environment-v1.md)，原型设计过程见[历史设计记录](../archive/architectural-environment-v1-design-record.md)。
当前连续墙、地面与组件碰撞语义由[地图格式](map-format.md)负责。

## 冻结范围与设计判断

V1 冻结可复用建筑资产、连接尺度和表面语言；关卡与碰撞语义见[地图格式](map-format.md)。
组合语法是：**独立玩法墙/地面 + 薄墙壳 + 粗结构跨度 + 贴边服务带**。
开放工业战场仍为主体，建筑只提供局部压缩和功能分区。

沿用 `generate_rasterfall_props.py` 的 Builder、材质创建、八边形管体、导出与检查器。
新增几何不使用倒角堆量、磨损纹理或黄色轮廓。常量 PBR 表面按
[环境材质约束](environment-art.md#材质创作与当前能力)区分涂漆、矿物与裸金属；主体使用现有蓝灰/暗灰方向，
墙壳适度提高灰色明度；功能颜色仍由既有机组、控制柜、灯柱和编号承担。
建筑大面没有纹理，环境设备仍使用既有局部 Hybrid sign。

## 标准房屋生成语法

`tools/building_kit.py` 提供离线 Python 编写语法，输出普通 V1 `collision`、`surface` 和
`render` 记录；地图解析器不读取 Python，也不从可见模型推断碰撞。前哨站的调用示例在
`tools/outpost_storeys.py`。墙、楼板和楼梯由同一参数同时生成可见实体、碰撞与可站立面。

- `slab(name, footprint, y, color, ceiling_color=None)`：`footprint=(min_x,max_x,min_z,max_z)`，Y 为板顶，底面为 Y 减统一厚度。可选关键字 `ceiling_color` 独立指定底面基础色，省略则沿用 `color`；`solid` 和 `switchback` 也接受此关键字。
- `ceiling_light(name, x, z, ceiling, yaw=0)`：透光面位于天花板底面 Y，支持四个正交朝向。完成建筑记录后调用 `finish()`，把同高灯体体积从楼板 render BOX 扣除；生成普通 V1 记录，不扩展运行时解析器。
- `wall(name, axis, at, start, end, bottom, top, color, openings=(), walk=False)`：轴向为 X 或 Z，`at` 是墙中心线；每个开口为 `(start,end,clear_height)`，自动生成两侧墙段及过梁。开口不得重叠或超出墙段。
- `window(name, axis, at, start, end, bottom, top, ...)`：填充已留出的矩形窗洞，输入高度与墙体一致，以地面为 0；生成细框、等分竖梃和淡灰蓝透明面。`frame`、`depth`、`max_pane` 控制框宽、框深和最大玻璃跨度；默认输出覆盖整个窗洞的独立碰撞，已有外围边界时可设 `collision=False`。该接口不负责开洞，先用 `wall` 留洞；仅在输出 SIGN 时转换到地面为 -900 的世界 Y，禁止调用方再次偏移。透明面与窗框分区，不叠在实体墙上。
- `flight(name, footprint, h0, h1, color, steps=12)`：沿 Z 的有限厚度连续碰撞坡面，附踏步表现。
- `switchback(name, footprint, storeys, ceiling, landing_depth=2048, spine_width=512, door_width=2458, door_height=1843)`：楼层为 `(名称,板顶Y)` 有序序列，生成各层南平台与入口门洞、北侧半层平台、双跑踏步、中间隔墙、外墙和顶盖。最底层南平台与底板共同铺满楼梯间，覆盖双跑踏步和半层平台下方；底板同步生成可见实体、有限厚度碰撞和可站立面，上层保留楼梯井开口。南入口墙由楼梯模块拥有，相邻房间的墙段接到模块边界。

标准层高由作者给定，墙顶必须等于下一层楼板底；不要独立指定一套不相符的视觉墙高。
相邻房间共享同一墙中心线，一条共享墙只生成一次，门口使用同一开口区间。
折返楼梯根据外墙内侧与中间隔墙计算踏步宽度，不预留无支撑窄槽；各跑的端点高度和平台高度一致。
护墙使用 `walk=True`，窄墙顶允许角色部分足迹支撑；外场空气墙继续使用独立的 boundary 规则。
楼层元数据、灯具与家具由调用方分别声明，不隐式生成角色、交互或楼层权限。

```python
from building_kit import BuildingKit

records = []
house = BuildingKit(records, thickness=154)
house.slab("home_1f", (-4096,4096,-3072,3072), 0, "758995")
house.wall("home_south", "x", -3072, -4096, 4096, 0, 2304,
           "526875", openings=((-614,614,1843),))
house.slab("home_roof", (-4096,4096,-3072,3072), 2458, "637A86", ceiling_color="E3E6E8")
house.ceiling_light("home_light", 0, 0, 2304)
house.finish()
```

其余三面墙按相同边界补齐；写入既有地图时保留 UTF-8 BOM 和换行，参照前哨站维护工具。
该语法负责建筑实体连接，既有资产套件仍可用于独立设备、服务带和有明确承重位置的梁。

`finish()` 按 `light_ceiling` 的安装包络生成浅凹槽，允许跨相邻楼板边界；凹槽顶部必须低于板顶，
否则报错，避免打穿上层地面造成漏光。灯体后有少量装配净空，凹槽之外的楼板体积相邻且不重叠。
碰撞和 surface 保持完整，灯具不增加碰撞。若底部锚点低于天花板（挂装），不自动切割。
生成器使用灯具 RFU 安装尺寸，而非 runtime 碰撞包围盒；尺寸变更须同步资产生成器与安装包络。
前哨站生成器每次从稳定碰撞 footprint 恢复原楼板再切割，移灯/删灯不会留下旧洞；重复运行输出不变。
凹槽裁分保留底面颜色。前哨站室内使用浅色天花板与较亮蓝灰墙面，地板仍有独立的较暗基础色；
颜色按 sRGB 解码后参与真实反弹，不使用只影响 GI 的反射率倍增。此为当前视觉校准，非实测照度认证。

## Canonical kit

以下 `rf_arch_*` 均为 **CANONICAL**；registry 名称省略 `rf_`。尺寸顺序为 Blender X/Y/Z 米。
精确规格的执行所有者是生成器 `SPECS`、manifest 和 registry；渲染统计以生成/截图日志为准。

| 资产 | 设计尺寸 m | 保留理由与使用边界 |
| --- | --- | --- |
| `rf_arch_beam` | 6 × .5 × .6 | 一件粗工字梁解释柱间跨度、边跨和檐下骨架；两端必须落在承重柱/墙顶，不做悬空装饰线 |
| `rf_arch_support` | 2.4 × .8 × 2.8 | 双脚和宽膝撑解释平台/维修台承重；重复到平台两端，平台面仍归地图 surface；不是供 Tank 通行的低门 |
| `rf_arch_wall` | 4 × .24 × 4.2 | 可重复薄墙壳；连续墙脚、大面和端筋直接分区，缓解 primitive 的整块纯色；只承担视觉围合 |
| `rf_arch_doorway` | 6 × .3 × 4.2 | 4.8 m 宽、3.6 m 高的真实几何开口；门洞两肩与墙壳续接；严禁拿整块 profile AABB 当门洞碰撞 |
| `rf_arch_pipe_straight` | 2 × .62 × .62 | 两米标准干线，使设备不再依靠相邻摆放暗示连接；八边形粗管与双端法兰 |
| `rf_arch_pipe_elbow` | 1.31 × 1.31 × .62 | 平面直角换向；服务带转入端站，无需增加大量弯管型号 |
| `rf_arch_pipe_tee` | 2 × 1.31 × .62 | 主干保留连续、侧支接控制/检修设备；替换对应 straight 而不叠加 |
| `rf_arch_service_panel` | 1.6 × .16 × 1.2 | 很薄的墙面检修门加三道宽服务口，提供设备与建筑的接口语义；不需要独立碰撞 |
| `rf_arch_cable_tray` | 4 × .24 × .32 | 一段覆盖一个墙壳模数的粗线槽；沿服务带连续，不能把每面墙都画成密集横线 |
| `rf_arch_floor_hatch` | 1.6 × 1.2 × .04 | 单个低矮维护盖，提供地面尺度和检修语义；放在设备带旁，不用它铺地 |

梁与墙模数承担不同任务：墙是 4 m 面板，结构为 6 m 跨度；按功能分区，而非每条墙缝都加柱。
现有 `industrial_pillar` 高 2.8 m，可直接承接梁底；`gate_frame` 继续提供较高的开放入口。
更长跨度应由同一梁 generator 的明确长度 variant 补充，不能无条件整体放大而改变梁厚。

## 连接契约

保留 Blender Z-up/-Y-forward、GLB Y-up/+Z-forward、bottom-center pivot 与 applied transforms。
GLB 真实米制，RMESH 232 units/m，registry 在展示边界换算到 512 RFU/m。
不要从模型预览 CLI 的 `front/back` 文件名推断源轴向；这些名称属于诊断相机。

现有 `pipe_module` 是**双立管设备**，并不是直干线；其管体半径 .24 m、法兰半径 .31 m、
厚 .12 m 被新管线复用。旧设备顶端有端盖，V1 不假装它已有可插拔的竖直连接口。
新件仅支持水平布管，地图 object 的 yaw 无法表达竖直转接。设备外壳上的连接是视觉服务接口，
没有流体、电力或交互模拟含义。

下表为 GLB 本地米制端口中心，法线向外。弯头/T 因 bottom-center pivot 与弯管几何中心不同，
**不能只把资产包围盒相贴**。按端口对接，m → RFU 仅在 placement 边界换算。

| 管件 | 端口中心 `(X,Y,Z)` 与出向 |
| --- | --- |
| straight | `(-1,.31,0)` → -X；`(1,.31,0)` → +X |
| elbow | `(-.655,.31,.345)` → -X；`(.345,.31,-.655)` → -Z |
| tee | `(-1,.31,.345)` → -X；`(1,.31,.345)` → +X；`(0,.31,-.655)` → -Z |

每个端口保留闭合低模端面；对接时端面互相接触，法兰不叠成第三个套环。支路末端应进入
设备外壳或明确的壁面接口，不能悬在通道中。不要将干线穿过门洞来追求“所有设备相连”。
电气线槽与机械管道是两种功能线；控制柜可以拥有管道控制接口，不把线槽说成流体连接。

墙面板正面朝 GLB +Z；落位时让正面朝维护通行带。墙体不能靠两片近乎重合的整面 shell
叠加出分区；V1 墙脚、竖筋和主体由相邻几何直接构成，减少斜视深度竞争。
连续外墙可利用背面的平色与贯通墙脚，不需背面复制完整检修装饰。

## Wall Surface V1

冻结两种用法，不另做一套纹理库：

- **Industrial shell**：中明度冷灰大面、低位维护带、稀疏竖向结构节奏；使用 wall/doorway。
- **Service wall**：同样的大面，在真实设备带加 service panel 和一条连续 cable tray。
  不是新 wall mesh，也不要求每个面板都有检修门。蓝灰检修件负责局部功能差异。

大面积连续 gameplay wall 使用地图层的参数化墙与表面记录；RMESH 墙壳适合局部建筑和边界转折；不要在全地图既有
实心墙前再满铺一层不可见背板。facility painted wall 作为配色方向保留，暂未冻结第三种模型。

## Floor Surface V1

保留模块化基因，取消高反差棋盘：约 4 m 大板、同色板面、很浅低对比接缝；维护区用一个
较浅区域色，外场采用较暗冷灰。板材节奏只提供尺度，不让每格轮流变色。
检修盖只放在设备侧，编号/警戒边界优先复用设备和门架上的现有信息，不铺黑黄边框。

正式地面分区由地图层表达，避免几百件 floor RMESH。近共面的整片底色 overlay 曾在软件深度路径出现闪线；
地表应直接分区/批处理，不复制多层覆盖的原型做法。

**Heavy-duty/ramp floor** 冻结为视觉规范：延续同色大板，接缝沿坡向投影；只在上下平台边界
保留少量粗防滑分段或安全提示。坡道 top 仍由原 surface 描述；不新增一片悬浮 ramp RMESH。
坡面 shader、材质绑定及正式地图效果需按[视觉验收指南](../guides/visual-validation.md)核对。
