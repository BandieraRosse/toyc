# Rasterfall 地图格式

> 文档更新：2026-09-12
> 源码核对基线：工作区（Runtime Map V1 projection ownership cleanup；正式 `rasterfall.map` 为完整 V1 source；`rasterfall_legacy.map` 仅保留显式 fallback；layout exporter 默认读取 V1 source）

> 源码核对补充：北侧通道扩宽为 Hurd 防区，原中央北侧刷怪区拆到左右两翼。

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

`attr.legacy_index` 只供迁移 adapter 把 surface 几何写入现有 `toy_map_primitive`，不改变
collision record 的碰撞标志和路径。当前正式地图 surface 为 32/32，render 为 98/98；两者都由
V1 Runtime adapter 写入现有 primitive/draw 兼容结构。

## Map IR Runtime Bridge

`rasterfall/lib/rasterfall_map_runtime.c` 将 V1 parser 的结果复制为不暴露 parser 内部结构的运行时视图，
公开 region、interaction、actor spawn、pickup、object 和 collision 的稳定 ID 查询；各类记录按稳定 ID 规范化，
调用方不依赖文本行顺序。
interaction 的 `action` 仍是字符串，runtime registry 再把它解析为 action ID；parser 不包含 gameplay callback。

`src/rasterfall_map.c` 提供 Gameplay Projection Adapter，把 V1 region、interaction、actor_spawn、pickup、object、
collision、surface 和 render 转换到 gameplay/renderer 现有数组。因此 collision primitive、collision engine、spawn 算法、
AI、prop 和 renderer 行为保持不变；Runtime Map 是 authoritative world representation，projection 只是迁移期接口，
默认流程只加载 V1 Runtime Map。
`legacy_index` 只用于兼容数组的稳定排列，不是 V1 record 的顺序语义。

`build/map-runtime-test` 与 `make test-map-runtime` 覆盖 V1 runtime 加载、稳定 ID 查询和 action registry。

正式地图源位于 `rasterfall/assets/maps/rasterfall.map`；旧兼容源为同目录的
`rasterfall_legacy.map`。旧源的磁盘结构定义在 `include/toy_map.h`，文本解析在 `lib/map.c`，仅供显式 fallback/reference
使用。`--legacy-map` 和 `rasterfall_session_load_legacy()` 是当前保留的 legacy compatibility entry。V1 的 parser、IR、
Runtime Map 和 projection adapter 是默认输入链路；修改语法时必须同时检查 parser、runtime、玩法绑定、
碰撞/导航、渲染和逻辑测试。

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

正式 `rasterfall.map` 当前包含完整的 98 个 render records；`rasterfall_legacy.map` 仍保留
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

## 修改地图排布的必经流程

地图排布以 `.map` 文本为唯一输入。布局导出器默认读取正式 V1 source，旧语法解析仅保留为离线兼容能力，不参与运行时。
调整区域、墙体、出生点、按钮或 `prop` 的位置后，必须使用
现有布局导出接口同时生成俯视图和 JSON 信息，再据此检查相对位置、边界和语义对象。

推荐流程如下：

1. 修改 `rasterfall/assets/maps/rasterfall.map` V1 source，保持玩法碰撞、可见几何和交互声明分别表达。
2. 运行 `make map-layout`（或对指定地图调用 `tools/map_layout_export.py`），生成配套的
   `output.png` 与 `output.json`。
3. 打开 PNG 检查整体排布，再用 `tools/map_layout_query.py` 查询对象的精确中心点、bounds、类型和邻近关系；
   PNG 用于空间理解，JSON/query 用于可复核的精确事实。
4. 若排布发生变化，重新导出结果并复查，不要继续使用旧的 PNG/JSON。导出的 JSON 是派生诊断资料，
   不能反向编辑来修改地图。

若现有导出器无法表达排布检查所需的信息，应先扩展导出器/schema 和查询接口，再修改地图；不要绕过接口
另写一次性脚本。完成地图排布修改时，至少核对 `safe`、`ai_spawn`、`button`、`prop` 以及相关碰撞体的
相对位置，并确认导出 JSON 的源文件指纹对应当前 `.map`。

## 俯视布局导出

零依赖离线工具 `tools/map_layout_export.py` 把 V1 `.map`（并兼容旧语法）导出为开发用俯视 PNG 和 JSON sidecar，
不引入新地图语法，也不进入游戏运行时：

```sh
make map-layout
# 或导出任意地图，固定生成 output.png / output.json
.venv/map-layout/bin/python tools/map_layout_export.py path/to/level.map --output-dir tmp/level-layout
make test-map-layout-export
# 已有 output.json 的精确事实查询（不重新解析 .map）
python3 tools/map_layout_query.py tmp/map-layout/output.json summary
python3 tools/map_layout_query.py tmp/map-layout/output.json get PR1
python3 tools/map_layout_query.py tmp/map-layout/output.json near-pos -14500 -17000 1000
python3 tools/map_layout_query.py tmp/map-layout/output.json rect -15000 -13000 -18000 -16000
```

地图布局导出是 Linux 开发工具，不属于 C 核心的零依赖边界。首次使用先运行
`make setup-map-layout`；它只在 `.venv/map-layout` 创建独立 Python 环境并安装 Pillow。导出器直接读取
`assets/fonts/gb2312-16.rfh` 绘制中英双语图例；不探测系统字体，也不再提供 TrueType 字体覆盖入口。
稳定对象 ID 和 JSON 字段仍保持 ASCII。字库重建和许可见 `assets/fonts/README.md`。

PNG 使用 x/z 平面、RFU 网格、色块/线框和紧凑 ID；右侧图例按区域、角色、通行、世界和交互分组并显示对象数量，`prop`
在图例中称为“组件 COMPONENT”；绘制顺序为底图几何、语义区域、碰撞叠加和关键点/标签，避免平台或组件遮挡边界信息。
`ai_spawn BASE` 和显式 `base` 区域使用黄色五角星作为高优先级基地锚点。隐藏碰撞以交叉线框 `AWn` 显示，普通 box
最后以白色空心交叉线框显示，普通 box 只挑 role box 和面积最大的少量碰撞 box 标为 `BXn`。JSON schema 为
`rasterfall-map-layout-v1`，每个对象保留源记录名、原始字段和行号，并补充导出 ID、中心点及 bounds；
顶层明确记录 `512 RFU = 1 m`。当前范围只覆盖布局理解，不做 chokepoint、路径分析或高程渲染。

`tools/map_layout_query.py` 只消费上述 `output.json`，支持 `summary`、`get ID`、`type TYPE`、`near ID radius`、
`near-pos x z radius` 和 `rect minx maxx minz maxz`；追加 `--json` 可得到机器可读结果。空间查询只使用已有
center/bounds：图片负责整体空间感，query 负责精确局部事实；不做路径规划、chokepoint 或自然语言区域识别。
Exporter 会记录源 `.map` 文件指纹，源文件变化时 query 在 stderr 提醒。

最小人工验收是在正式地图上运行 `make map-layout`，打开 `tmp/map-layout/output.png`，抽查 safe、
spawn、button、prop 的相对位置，再用相同导出 ID 对照 `output.json` 的中心点与 bounds。当前正式地图
没有 `base` 或 `safe goal` 记录；这两类由自动化覆盖用例验证，待正式地图实际声明后再加入人工抽查。
