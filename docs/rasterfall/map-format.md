# Rasterfall 地图格式

> 状态：当前
> 所有者：Rasterfall 空间地图格式
> 事实入口：`rasterfall/lib/rasterfall_map_parser.c`、`rasterfall/include/rasterfall_map_parser.h`
> 最近核对：2026-09-21

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

入口为 `build/map-inspect <map-v1-file>`，实现位于
`rasterfall/lib/rasterfall_map_parser.c` 与 `rasterfall/include/rasterfall_map_parser.h`。
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
不引用 `toy_game` 或 `rasterfall_session`。`build/map-inspect` 输出 world bounds、各类数量和稳定 ID；
`make test-map-parser` 覆盖成功解析、未知 record/字段、非法数字、重复 ID、缺字段、世界越界和容量上限。

## Surface V1

正式地图中的地面、平台和坡道使用独立的 V1 `surface` 记录，不再从 legacy 文本的
`ground`/`floor`/`platform`/`ramp` 行读取 surface 数据。`id`、`kind`、bounds、height/height2、
axis 和 material 原样保存在 Map IR/Runtime；parser 不解释 `kind` 或 `material`，`attr.*` 继续
作为扩展字段保存。坡道必须保存 `axis` 与起止高度；平台/地面使用单一 `height`。

`attr.collision_id=<collision稳定ID>` 供 adapter 把 surface 几何写入对应 `toy_map_primitive`，不改变
collision record 的碰撞标志和路径。例如 `attr.collision_id=ground_world`；引用必须存在且唯一。
正式地图的 surface/render 记录均由 V1 Runtime adapter
写入现有 primitive/draw 兼容结构；具体数量以 `map-inspect` 和 `map-runtime-test` 的当前输出为准。

Map IR、Runtime Map、玩法投影与 World Content 的所有权见 [地图与世界内容](architecture/maps-and-world-content.md)。

## 几何与碰撞

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

墙上按钮使用 `button_<用途> x z y` 记录并绑定到对应玩法交互；例如
`button_west_corridor_no_tank -23940 2100 200` 会在西侧走廊出口旁的墙面放置一个按钮，
一次生成 16 个随机敌人但排除 Tank。

`button_humanoid_actions x z y` 是出生点附近的 RF Humanoid V2 动作调试按钮。它只驱动
session 的 presentation 状态，不创建 gameplay actor；当前地图将按钮放在
`(-12600,-12800)`，对应的 V2 Rifleman 由 renderer 固定展示在 `(-11800,-10200)`。
按钮按 `IDLE → WALK → RIFLE AIM → AIM + RECOIL` 循环。

`button_enemy_death_test 14000 -10500 -250` 位于开发者区东南空地。交互后在其正前方
`z=-13500` 生成一排六个真实 gameplay enemy（Common/Fast/Heavy 各两个），随即通过正式
`toy_game_apply_reported_hit()` 入口施加等于当前生命值的伤害。它不维护独立假人或动画时钟；击杀
统计、死亡状态、effects 同步、网络已有 enemy 状态和最终清槽均沿用正式链路。

静态环境组件使用 registry 中的稳定名称或 ID，不直接引用模型路径：

```text
prop asset x z yaw scale
prop crate -14500 -17000 0 1000
```

当前开发地图的 `z=-17000` 陈列带使用同一 `prop` 记录接入十件工业组件；实例中心沿 X 轴每
1500 RFU 排列，renderer 与 gameplay primitive 共用各资产 profile 的尺寸契约，避免展示和
默认碰撞盒重叠。

模型陈列台的 `model` 记录只属于 renderer 展示，不创建玩法实体或碰撞。现有 style 1--5 是旧
敌人/特感展示；style 6--8、9--11、12--14 分别按 COMMON、FAST、HEAVY 展示
LEGACY / BLOCK_INFECTED / HUMANOID_INFECTED，沿同一 `z=-8700` 展示线向右排列。
布局导出器会输出 `type=model`，保留 style、颜色、高度和源行号，供
`map_layout_query.py ... type model` 复核。

Character Test Strip 是渲染器拥有的 presentation-only 开发测试带，固定在 `z=-20000`、
工业 prop 陈列带后方。地图只声明可见 label；旧 procedural 与 RF Humanoid 的位置、姿态和
AK attachment 由 `render_character_test_strip()` 固定配置，避免把测试角色写入 `toy_game_actor`
或地图碰撞。`--character-world-capture` 使用该地图和正常 world render path 生成真实场景截图。

## V1 render 记录

地图视觉描述使用独立的 `render` 记录，由 `rasterfall_map_parser` 解析为 Map IR，再由
`rasterfall_map_runtime` 暴露给 runtime。运行时 adapter 将它转换为既有 `toy_map_draw`，因此
renderer 不读取 parser 内部结构，也不拥有地图文本解析逻辑：

```text
render id=crate_display kind=model min_x=-3200 max_x=-2000 min_z=-8700 max_z=-8100 height=-900 color=B66A35 asset=rf_crate attr.style=1
render id=outer_wall kind=wall min_x=-45000 max_x=33000 min_z=-45000 max_z=-45000 height=5400 color=555B68 attr.legacy_index=91
```

基础字段是稳定 `id`、`kind`、bounds、可选 `height` 和 `color`；位置由 bounds 的中心表达。
`attr.*` 只承载地图层扩展，例如迁移期的 `legacy_index`、style、文字和附加高度。模型记录只
引用 registry asset ID，不放 mesh、texture、material 或 rasterizer 状态。视觉装饰可以超出
gameplay `world` bounds（正式地图外围墙保留了这一旧行为）；collision/surface 仍必须位于 world 内。

正式 `rasterfall.map` 的 render records 由 V1 source 完整提供；`rasterfall_legacy.map` 仍保留
作为 fallback，但正式启动路径的 draw data 来自 V1 render → runtime → draw adapter。

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

当前 Hurd control region 按 V1 要求不扩展 `.map` 格式，而由 session 固定配置。地图只提供北侧空间：
北门和外侧防区为 `x=-6000..6000, z=24000..33000`，其墙体与 `blocks_airborne` 碰撞同步扩宽；控制区
内缩为 `x=-5000..5000, z=25500..31500`，避免旗帜靠墙仍判定部署。原北侧中央刷怪区被拆为
`x=-20000..-9000` 与 `x=9000..20000` 两翼，二者均为 `z=14000..22000`，不会与据点或入口重叠。
Hurd 旗帜和四名固定 actor 由 session 生成，不是地图记录；原 Maid 四人及其位于 `(-12000, 0)` 的
flag 1 也继续由 session 生成，Hurd 因此使用 flag 2。若以后正式数据化这些内容，再同时扩展 parser、
绑定、布局导出和 query schema。

地图修改、布局导出与精确查询流程见 [地图编辑与查询](guides/map-authoring.md)。

## Continuous Wall / Floor 与 Component Collision

Campaign 长墙使用 `object kind=boundary_wall`，高度 2150 RFU（约 4.2 m）、厚度
124 RFU，墙脚/主体/压顶相邻分区，扶壁约每 4096 RFU 一处。长度按墙段生成，
只支持 cardinal yaw 与 scale=1000；不新增 RMESH 或纹理资产。原 12.3 m 可见长墙已替换，
开发坡道和可站立 air gate 保留。旧南侧背景墙从世界外 z=-45000 移到 z=-33000，
world bounds 留出墙厚，外墙显式阻止 airborne 越界。

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

`build/map-inspect --collision-json <map>` 输出 Runtime Map 实际碰撞，包括 owner、bounds、
base_y、height 和 flags。组件地图的 layout exporter 调用它，JSON 保留生成碰撞与组件
collision_bounds，source_file 增加 SHA256；`make map-layout` 自动构建 inspector。
验证入口为 `make test-map-components`、`make test-map-runtime` 与 `--logic-test`。
