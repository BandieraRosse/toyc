# Rasterfall 地图格式

> 状态：当前
> 所有者：Rasterfall 空间地图格式
> 事实入口：`rasterfall/lib/rasterfall_map_parser.c`、`rasterfall/include/rasterfall_map_parser.h`
> 最近核对：2026-09-21

本页维护空间地图的语法、字段与碰撞合同。地图运行所有权见[地图与世界内容架构](../architecture/maps-and-world-content.md)，编辑与验证见[地图指南](../guides/map-authoring.md)，开发地图的阶段布置见[历史记录](../archive/map-content-fixtures-2026-09.md)。

## World Definition V1

`.map` 只描述空间地图的空间事实；`assets/worlds/*.content` 由
Game-owned World Content parser 单独加载，描述 actor、terminal、flag、
formation 与 fixture。静态 world identity 将两者绑定，但 content 不进入
Runtime Map。对外统一称呼为“空间地图”和“内容地图”：`map-layout` 输出空间地图，
`world-layout` 将空间地图与 World Content 合并后输出内容地图。

## Map Compiler V1

Map Compiler V1 是新的 World Description Language 和运行时无关 Map IR 的独立链路。
正式地图的所有运行时记录都由 V1 source 提供，并通过 Runtime Map adapter 生成原有 gameplay
数组、`toy_map` primitive/draw records；renderer 保持原有输入结构，`rasterfall_legacy.map` 仅作为明确的
fallback 输入。

实现位于 `rasterfall/lib/rasterfall_map_parser.c` 与 `rasterfall/include/rasterfall_map_parser.h`；检查命令见[地图指南](../guides/map-authoring.md)。
语法使用单行 `key=value` record、全局唯一稳定 `id`，记录顺序没有语义。未知 record、未知字段、
重复 ID、非法数字、越界坐标和容量超限都会带源文件行号失败；`attr.<name>=<value>` 是为后续
扩展保留的显式属性命名空间。

最小示例：

```text
map version=1 units=rfu
world min_x=-1000 max_x=1000 min_z=-800 max_z=800 room_limit=1200
region id=start_area kind=safe min_x=-300 max_x=300 min_z=-700 max_z=-400 attr.role=start
collision id=north_wall shape=box min_x=-900 max_x=900 min_z=650 max_z=700 height=300 visible=false walkable=false blocks_airborne=true
surface id=arena_floor kind=ground min_x=-900 max_x=900 min_z=-700 max_z=700 height=0 material=294B45
render id=arena kind=box min_x=-900 max_x=900 min_z=-700 max_z=700 color=294B45 height=0
interaction id=wave_skip action=wave_skip x=0 y=0 z=-400
```

Map IR 包含 world、regions、collisions、surfaces、renders、interactions、actor_spawn、pickup 和 object，
不引用 `toy_game` 或 `rasterfall_session`。检查器输出 world bounds、各类数量和稳定 ID；parser 测试覆盖成功解析、未知 record/字段、非法数字、重复 ID、缺字段、世界越界和容量上限。

## Surface V1

`render kind=floor attr.style=13` 为一米网格地板。格长固定 512 RFU，相对 render 的最小 X/Z
边界对齐，以同一地面网格分割浅青格线及蓝灰交错格面；它只负责表现，不隐式生成占地、导航或碰撞。
通行仍需独立 surface/collision 声明；使用规范见[网格玩法场](experiment-labs.md#一米网格玩法场)。

### 楼层与有限厚度楼板

`region kind=building_floor` 使用 `attr.building` 稳定建筑 ID、`attr.level`（-16～-1 或 1～16）、
`attr.y` 支撑高度、`attr.ceiling` 层顶高度、可选 `attr.roof=0|1` 和 `attr.name` 显示名。
XZ bounds 必须非空，Y 支持 ±1000000 RFU，ceiling 大于 Y；同建筑不能重复 level 或 Y，每张地图最多 32 个楼层。
高度相对地面 0 表示；负 level 为地下层，RTS 显示 B1、B2 等标签。
这是供 RTS 选择、拾取和显示使用的元数据，本身不产生楼板、门或通行连接。
普通 region 的可选 `attr.y` 也支持高处任务出生点，未指定时保持地面 0。

`collision shape=box attr.base_y=<RFU>` 指定底面，省略时为 0；显式底面支持负值并要求 base_y≤height。
显式底面只允许 BOX。渲染用对应 `render kind=box attr.base_y`，`height` 为顶面。
楼板需显式 walkable，并以 platform surface 的 `attr.collision_id` 引用，投影保持有限 BOX。
楼梯使用既有 ramp surface；楼层 region 不替代物理楼梯。collision 的 height/height2 支持负值。
`collision shape=ramp_x|ramp_z attr.thickness=<正RFU>` 声明平行底面的有限厚度坡道，允许从其下方通过，
支撑、头顶碰撞和射线均消费该厚度。省略时保留旧坡道语义。
对应 `render kind=ramp attr.thickness` 可加 `attr.steps=1..64` 绘制踏步，碰撞仍为连续坡面；steps 要求显式厚度。
坡道 render 的 `attr.style=2` 沿 X 起伏，`attr.style=3` 沿 Z 起伏，须与关联 surface/collision 轴一致。

```text
region id=workshop_2f kind=building_floor min_x=0 max_x=8000 min_z=0 max_z=8000 attr.building=workshop attr.level=2 attr.y=2150 attr.ceiling=4198 attr.roof=0 attr.name=Workshop_2F
collision id=upper_slab shape=box min_x=0 max_x=8000 min_z=0 max_z=8000 height=2150 attr.base_y=2048 visible=false walkable=true
surface id=upper_surface kind=platform min_x=0 max_x=8000 min_z=0 max_z=8000 height=2150 material=87989F attr.collision_id=upper_slab
render id=upper_draw kind=box min_x=0 max_x=8000 min_z=0 max_z=8000 height=2150 attr.base_y=2048 color=87989F
```

### 实验区域复合定义

```text
lab id=sample_area x=16000 z=-32000 width=10240 depth=9216 category=model enclosure=open
object id=sample_button kind=facility_terminal x=4000 y=0 z=4000 yaw=0 scale=1000 attr.lab=sample_area attr.collision=component
render id=sample_info kind=sign min_x=-900 max_x=900 min_z=-800 max_z=-780 height=-700 color=9FB4FF attr.height2=-450 attr.style=3 attr.text=MODEL_IDLE attr.texture_u=1 attr.lab=sample_area
```

`lab` 原点为地面中心，仅作 X/Z 平移，宽深必须为偶数且至少 512 RFU；原点绝对值和宽深
上限为 1000000 RFU。用途为 `model|animation|lighting|performance`，围合为
`open|backdrop|walled`。Parser 将其降为 `kind=experiment` region，保存原点、用途与围合属性，
不创建隐式可见面或碰撞。可复用的完整组合由[区域生成工具](../../../tools/experiment_lab.py)输出。

region、surface、collision、render、object、interaction、actor_spawn、pickup 可声明
`attr.lab`。Parser 在读完整文件后解析引用并平移其 X/Z bounds/position，再校验世界边界。
支持先写子组件再写区域；ID 仍全局唯一，不自动加前缀。未知区域、重复 ID 和加法溢出失败并
报告子记录行号。Y/height、yaw、scale、length 不平移；不支持嵌套、旋转或区域缩放。
`attr.lab` 必须引用 `lab` 记录，不能把普通 region 当局部坐标原点。
终端样式和展示默认值见[实验区合同](experiment-labs.md)。

正式地图中的地面、平台和坡道使用独立的 V1 `surface` 记录，不再从 legacy 文本的
`ground`/`floor`/`platform`/`ramp` 行读取 surface 数据。`id`、`kind`、bounds、height/height2、
axis 和 material 原样保存在 Map IR/Runtime；parser 不解释 `kind` 或 `material`，`attr.*` 继续
作为扩展字段保存。坡道必须保存 `axis` 与起止高度；平台/地面使用单一 `height`。

`attr.collision_id=<collision稳定ID>` 供 adapter 把 surface 几何写入对应 `toy_map_primitive`，不改变
collision record 的碰撞标志和路径。例如 `attr.collision_id=ground_world`；引用必须存在且唯一。
正式地图的 surface/render 记录均由 V1 Runtime adapter
写入现有 primitive/draw 兼容结构；具体数量以 `map-inspect` 和 `map-runtime-test` 的当前输出为准。

Map IR、Runtime Map、玩法投影与 World Content 的所有权见 [地图与世界内容](../architecture/maps-and-world-content.md)。

### 机器组件组合

```text
assembly id=console x=4000 y=0 z=4000 attr.lab=sample_area
object id=sample_button kind=lab_computer_stand x=0 y=0 z=0 yaw=0 scale=1000 attr.assembly=console attr.collision=component
object id=console_display kind=lab_computer_display x=130 y=628 z=-64 yaw=0 scale=1000 attr.assembly=console
render id=console_screen kind=sign min_x=-126 max_x=386 min_z=-29 max_z=-29 height=676 color=79E8C5 attr.height2=996 attr.style=5 attr.texture_u=256 attr.texture_v=160 attr.channel=sample_computer attr.assembly=console
```

`assembly` 是独立组件的平移坐标系，必须有 id 和 x/y/z，可由 `attr.lab` 放入实验区；
不支持 assembly 嵌套、旋转或整体缩放。最多 64 个，原点各轴范围 ±1000000 RFU。
子项仅支持 object 和 sign render，使用 `attr.assembly`，不能同时写 `attr.lab`。
对象 x/y/z 加组合原点；sign 的 X/Z bounds 加原点，两个高度均为相对组合地面的高度，
降级时加 `assembly.y - 900` 得到世界 Y，两个高度必须显式声明。未知引用、重复 ID、
混用坐标系、溢出与不支持的子记录均报错。支持前向引用，组件 ID 不隐式加前缀。
组合在 Parser 层降为普通记录，Runtime Map 不依赖 assembly，碰撞仍由根 object 显式声明。
当前 IR/玩法投影物件容量同步为 320；超限拒绝加载，不截断。

完整机器由 `tools/lab_computer.py` 生成，标准实验区生成器复用它。屏幕分辨率、字体控制、透明窗
及重建命令见[实验区合同](experiment-labs.md#模块化控制计算机)。布局导出的 `assemblies` 保存组合世界原点，
子项仍导出世界坐标，碰撞由 C Runtime Map 提供。

## 几何与碰撞

### 离线一米格规划属性

建筑模块可用 `region kind=building_module` 表达墙线 bounds；设备组合根 object 可声明
`attr.grid_min_x`、`attr.grid_min_z`、`attr.grid_width`、`attr.grid_depth`，
前两项跟随 object 的局部平移坐标系，后两项为正整数格数。世界角点须在 512 RFU 格线上。
Parser/Runtime 保存扩展字段，不生成碰撞或导航；完整性和对齐由离线工具校验。
组合、奇数格中心和通行边界见[一米格合同](map-grid-v1.md)。

地图几何的可见性和碰撞是独立属性：

```text
box minx maxx minz maxz height color visible collision
box minx maxx minz maxz height color hidden collision role=air_gate_left
box minx maxx minz maxz height color hidden collision blocks_airborne
```

未写选项的旧 `box` 默认可见且参与碰撞。需要允许站上顶部的高墙应追加 `walkable`；空气墙应
使用 `hidden collision` 并通过 `role=air_gate_*` 表达用途；旧 `air` 语法只用于兼容已有地图，
新内容不要继续使用。

`blocks_airborne` 是独立碰撞属性：水平移动在角色高于 box 顶面时仍会被它阻挡，适用于不可越过的
关卡边界。普通 box 默认不带该属性，crate、platform 和矮墙仍可从足够高度越过；该属性不从
`role` 推导。可用 `allows_airborne` 显式清除属性。

走廊或区域边界应使用明确的 `box ... hidden collision` 碰撞体；仅用于背景围合的 `wall` 记录
只负责渲染，不会自动参与玩法碰撞。正式地图的外围渲染墙就是这种情况，实际可玩边界由中央
区域的内层墙、走廊侧墙和走廊端墙组成。边界有入口时，应拆成入口两侧的碰撞段，不能用一整
块墙再依赖渲染开口。

`platform` 可在颜色和显示模式后追加 `role=`。空气墙的可站立顶面必须和对应竖直墙使用同一
`air_gate_*` role 前缀，例如 `platform ... 3B5550 transparent role=air_gate_left_top`；空气墙开关
会同时移除整组的渲染、碰撞和导航阻挡，不能留下无形顶面。

## 玩法声明

V1 的玩法与世界对象记录使用单行 `key=value` 字段，稳定 `id` 不承载文本顺序语义：

```text
actor_spawn id=Jesus class=level2 base_id=2 x=1000 y=0 z=0 downed=0
pickup id=pickup_smg kind=smg x=-250 y=-235 z=-7450
object id=object_crate kind=crate x=-14500 y=0 z=-17000 yaw=0 scale=1000
```

`actor_spawn` 的 `class`/`type` 二选一，另外需要 `base_id`、三轴坐标和 `downed`；`weapon` 可选。
`pickup` 描述真实可拾取物，不描述按钮；按钮继续使用 `interaction action=...`。`object` 只描述
静态对象的稳定 kind 和 placement，不能把 renderer 类型、模型路径或碰撞 primitive 塞入该记录。
兼容数组顺序由 `attr.legacy_index` 显式保存，仅供 adapter 使用，不是 V1 的语义顺序。

```text
safe minx maxx minz maxz start
safe minx maxx minz maxz goal
base id minx maxx minz maxz
ai_spawn name base_id level1|level2|level3 x z downed
```

- `safe` 声明起点或终点安全室。
- `base` 声明带稳定 ID 的据点区域。
- `ai_spawn` 声明 AI 名称、所属据点、等级、位置和初始倒地状态。

墙上按钮使用 `button_<用途> x z y` 记录并绑定到对应玩法交互。动作调试按钮只驱动 session
presentation 状态，不创建 gameplay actor；敌人死亡测试按钮通过正式 gameplay 命中入口驱动真实敌人。
具体开发地图坐标和当前按钮配置见[历史记录](../archive/map-content-fixtures-2026-09.md)。

静态环境组件使用 registry 中的稳定名称或 ID，不直接引用模型路径：

```text
prop asset x z yaw scale
prop crate -14500 -17000 0 1000
```

模型陈列台的 `model` 记录只属于 renderer 展示，不创建玩法实体或碰撞。现有 style 1--5 是旧
敌人/特感展示；style 6--8、9--11、12--14 分别按 COMMON、FAST、HEAVY 展示
LEGACY / BLOCK_INFECTED / HUMANOID_INFECTED。
布局导出器会输出 `type=model`，保留 style、颜色、高度和源行号，供
`map_layout_query.py ... type model` 复核。

Character Test Strip 是渲染器拥有的 presentation-only 开发测试带。地图只声明可见 label；旧 procedural 与 RF Humanoid 的位置、姿态和
AK attachment 由 `render_character_test_strip()` 固定配置，避免把测试角色写入 `toy_game_actor`
或地图碰撞。截图命令见[视觉验收指南](../guides/visual-validation.md)。

## V1 render 记录

地图视觉描述使用独立的 `render` 记录，由 `rasterfall_map_parser` 解析为 Map IR，再由
`rasterfall_map_runtime` 暴露给 runtime。运行时 adapter 将它转换为既有 `toy_map_draw`，因此
renderer 不读取 parser 内部结构，也不拥有地图文本解析逻辑：

```text
render id=crate_display kind=model min_x=-3200 max_x=-2000 min_z=-8700 max_z=-8100 height=-900 color=B66A35 asset=rf_crate attr.style=1
render id=outer_wall kind=wall min_x=-45000 max_x=33000 min_z=-45000 max_z=-45000 height=5400 color=555B68 attr.legacy_index=91
```

基础字段是稳定 `id`、`kind`、bounds、可选 `height` 和 `color`；位置由 bounds 的中心表达。
`render kind=floor` 的 `attr.style=10/11/12` 分别选择实验检修板、带浅色边带的道路板、低对比填充板；
省略时保留通用地板。三个样式仅在共享地面分区中改变接缝及边带颜色，不改变 surface 或 collision。
道路按矩形较长轴确定方向，交叉口仍由总平面道路并集拥有。地板与标识数量上限为 640 条 render，
Parser、Runtime、玩法投影和 Scene 按同一容量合同接收。

嵌入天花板的灯具由[建筑生成语法](architectural-environment-v1.md#标准房屋生成语法)离线降级为
普通 `object kind=light_ceiling` 和凹槽周围的有限厚度 `render kind=box`；runtime 不做隐式模型布尔运算。
楼板保留封闭背板、原 collision 和 surface，Scene BOX 必须绘制完整底面。

`render kind=box attr.bottom_color=E3E6E8` 可独立指定底面（世界 -Y）的六位 sRGB 基础色；
省略时沿用 `color`，`000000` 表示真实黑色，不等于省略。其他面仍用 `color`。
仅 BOX 接受此扩展，非法十六进制值在绘制投影时拒绝。该颜色同时进入 Scene 可见网格与 DDGI
反射率，不能只改显示而保留旧反弹颜色；它不改变几何、collision 或 surface。

`render kind=sign attr.style=7` 为固定尺寸四角投影信标，bounds 中心决定 X/Z，
底座占地 360×240 RFU，含动画投影的水平包络为 360×280 RFU，高度范围为世界 Y=-896 到 -52；
`color` 为光学槽与交叉菱形框颜色。
记录 bounds、height/height2 应匹配上述几何包络。信标没有文字、交互或隐式碰撞；
模板用独立 walkable box 声明底座粗碰撞，投影无碰撞。动画合同见[实验区合同](experiment-labs.md#标准地块铺装与角标)。

`attr.*` 只承载地图层扩展，例如迁移期的 `legacy_index`、style、文字和附加高度。模型记录只
引用 registry asset ID，不放 mesh、texture、material 或 rasterizer 状态。视觉装饰可以超出
gameplay `world` bounds（正式地图外围墙保留了这一旧行为）；collision/surface 仍必须位于 world 内。

`kind=sign` 的牌面沿 X 展开，Z bounds 的中心是单层牌面，`height` 和 `attr.height2` 分别是世界 Y 轴上的牌底、牌顶；地面世界 Y 为 -900 RFU。默认样式保留路牌支杆，`attr.style=1` 画无支杆的墙面牌。字体区域是牌面本身的另一种颜色，没有前后叠加的文字平面。`attr.facing=+z` 或 `attr.facing=-z` 指定观察者位于哪一侧时文字正向可读；另一侧看到自然镜像。旧记录省略时默认 `+z`。文字宽度由 X 跨度决定，墙面牌应贴在南北向墙面并给出显式 `attr.height2`。

正式 `rasterfall.map` 的 render records 由 V1 source 完整提供；`rasterfall_legacy.map` 仍保留
作为 fallback，但正式启动路径的 draw data 来自 V1 render → runtime → draw adapter。

投影 sign 样式 2/3/4 自动带半透明背景和从底座到牌面的动态散射光束；样式 7 带光束，
不为开放菱形添加实体面板。背景和光束仅为展示几何，不新增地图记录或碰撞。
`attr.projection_beams=0` 显式关闭该 sign 的散射光锥和细射线，保留文字、背景、图案与机壳；
省略或 `1` 保持原外观，其他取值拒绝投影到渲染数据。CPU 和 Scene 共用此只读表现选项。

透明机器窗 `kind=sign attr.style=6` 允许两种轴向：`min_z=max_z` 且 `min_x<max_x` 为正面窗；
`min_x=max_x` 且 `min_z<max_z` 为侧面窗。两种窗均使用 `height` / `attr.height2` 表示底顶高度，
不带文字；双水平跨度非零或同时为零均无效。其他 sign 样式继续沿 X 展开。
建筑窗同样可以使用此透明面，但 SIGN 高度是地面为 -900 的世界 Y，BOX 和 collision
高度是地面为 0 的建筑高度，两者不能直接照抄。离线编写优先使用
`BuildingKit.window` 完成坐标转换、窗框和独立碰撞，见[建筑生成语法](architectural-environment-v1.md#标准房屋生成语法)。

地图 IR 与玩法绘制投影的 render 容量均为 640，authored collision 容量为 512，object/prop 容量均为 384；GPU Scene world snapshot
直接沿用绘制投影容量。各边界必须同步，避免新展区通过解析后在投影时截断或无法冻结。
显式碰撞与组件展开碰撞合计仍不得超过 Runtime Map 和 Game 的 512 条总预算，超过时拒绝加载。
RF 电子组件的 `attr.length` 展示组定义见[实验区合同](experiment-labs.md#产品展区风扇与状态灯)。

`x/z` 使用 RFU，实例落在地面锚点 `y=-900`；`yaw` 为绕世界 Y 轴的角度；`scale=1000`
表示资产原始设计尺寸。默认根据资产 profile 的 RFU 碰撞盒生成普通 gameplay box；视觉网格
与该盒体独立。仅在确有需要时可追加 `collision=none`，例如：

```text
prop lamp_post 0 -17000 0 1000 collision=none
```

解析结果位于 `toy_map.props`，生成的碰撞结果位于同一地图的 `primitives`；默认 primitive
同时是碰撞体和可站立顶面，玩家可从边缘离开。二者都不引用 RMESH 的 232 单位。

其他受支持记录及参数应直接以 `lib/map.c` 的解析分支为准。新增记录时在本文记录用途和最小示例，
不要只修改关卡文件。可见几何不能代替玩法碰撞，渲染正确也不能证明导航和地面查询正确。

Hurd 旗帜与固定 actor 由 session 生成，不是地图记录。内容地图的 `flag` 可使用 `facing=+z/-z` 指定单层旗布文字的正确阅读侧，省略时默认 `+z`。阶段地图中的 control region 和刷怪区坐标见[历史记录](../archive/map-content-fixtures-2026-09.md)；若以后正式数据化这些内容，需同时扩展 parser、绑定、布局导出和 query schema。

旧商店 pickup、`money`/`clear_hired` interaction 及其 legacy 按钮记录已移除，不属于当前地图动作词汇。

地图修改、布局导出与精确查询流程见 [地图编辑与查询](../guides/map-authoring.md)。

## Continuous Wall / Floor 与 Component Collision

Host 候选机柜的 `attr.length` 用百位区分 CPU/Memory，十位区分柜号，个位 1–6 表示从底部开始的槽号；
个位 0 用于机架附属件。它不是空间长度，也不生成模块碰撞；未启用柜的机架碰撞在投影时关闭。
合同与示例见 [Host Rack V2](host-rack-v2.md)。

Campaign 长墙使用 `object kind=boundary_wall`，高度 2150 RFU（约 4.2 m）、厚度
124 RFU，墙脚/主体/压顶相邻分区，扶壁约每 4096 RFU 一处。长度按墙段生成，
只支持 cardinal yaw 与 scale=1000；不新增 RMESH 或纹理资产。旧墙替换和位置调整见[历史记录](../archive/map-content-fixtures-2026-09.md)。

```text
object id=yard_wall kind=boundary_wall x=0 y=0 z=6000 yaw=0 scale=1000 attr.length=8192 attr.collision=boundary
object id=yard_gate kind=gate_frame x=0 y=0 z=2000 yaw=0 scale=1000 attr.collision=component
object id=overhead_beam kind=arch_beam x=0 y=1434 z=0 yaw=0 scale=1000 attr.collision=component
```

`attr.collision` 缺省/none 不生成碰撞；component 使用独立的 RFU 模板，boundary
额外阻止 airborne 越界。模板拥有简化实体形状、multipart 门洞/管件、底部高度和 walkable
策略，不读取视觉网格或 presentation registry 的 AABB。生成结果保存 `owner_id` 与源行号；
ID 为 `<object_id>_col_<part_index>`，冲突、越界、非法尺寸与容量超限加载失败。
未知 component kind 加载失败。新增模板主要修改 `rasterfall_map_components.c`；
新增视觉资产身份仍需检查 prop registry 与平台构建/资源规则。

新 collision/object/render 无需 legacy_index，按稳定 ID 排在已有迁移槽之后；
collision/render/object 的迁移槽要求连续且唯一，pickup/interaction 共用槽并允许保留空位。
Surface 通过 `attr.collision_id` 关联碰撞稳定 ID，不能再填写 surface `attr.legacy_index`。
Runtime 允许独立未绑定 Surface；当前 Gameplay Projection 要求每个 Surface 绑定一个 collision。
不存在的引用、非 collision ID 与多个 Surface 绑定同一 collision 均带源行号加载失败。

地面继续由 surface 与 floor paint 提供，非 authored-ground 世界使用约 4 m 同色大板和
很浅接缝，直接在同一平面分区，不增加 floor RMESH 或叠层。WHU 保留已有 authored paint。

碰撞 JSON 输出 Runtime Map 实际碰撞，包括 owner、bounds、base_y、height 和 flags。
组件地图的 layout exporter 调用检查器，JSON 保留生成碰撞与组件 collision_bounds，
source_file 增加 SHA256；命令和验证入口见[地图指南](../guides/map-authoring.md)。
