# Rasterfall 地图格式

> 文档更新：2026-09-08
> 源码核对基线：工作区（地图排布通过 PNG/JSON 导出链路核验；地图布局 PNG/JSON 导出器与 JSON 查询器；北侧走廊入口边界碰撞；外围渲染墙与 gameplay 碰撞分离；静态 prop 实例及 profile 碰撞盒接入现有 primitive/nav）

正式地图位于 `rasterfall/assets/maps/*.map`。磁盘结构定义在 `include/toy_map.h`，文本解析在
`lib/map.c`，`src/rasterfall_map.c` 再把结果绑定到玩法盒体、图元、可交互物和安全区。修改语法时
必须同时检查解析、玩法绑定、碰撞/导航、渲染和逻辑测试。

## 几何与碰撞

地图几何的可见性和碰撞是独立属性：

```text
box minx maxx minz maxz height color visible collision
box minx maxx minz maxz height color hidden collision role=air_gate_left
```

未写选项的旧 `box` 默认可见且参与碰撞。需要允许站上顶部的高墙应追加 `walkable`；空气墙应
使用 `hidden collision` 并通过 `role=air_gate_*` 表达用途；旧 `air` 语法只用于兼容已有地图，
新内容不要继续使用。

走廊或区域边界应使用明确的 `box ... hidden collision` 碰撞体；仅用于背景围合的 `wall` 记录
只负责渲染，不会自动参与玩法碰撞。正式地图的外围渲染墙就是这种情况，实际可玩边界由中央
区域的内层墙、走廊侧墙和走廊端墙组成。边界有入口时，应拆成入口两侧的碰撞段，不能用一整
块墙再依赖渲染开口。

## 玩法声明

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

静态环境组件使用 registry 中的稳定名称或 ID，不直接引用模型路径：

```text
prop asset x z yaw scale
prop crate -14500 -17000 0 1000
```

当前开发地图的 `z=-17000` 陈列带使用同一 `prop` 记录接入十件工业组件；实例中心沿 X 轴每
1500 RFU 排列，renderer 与 gameplay primitive 共用各资产 profile 的尺寸契约，避免展示和
默认碰撞盒重叠。

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

## 修改地图排布的必经流程

地图排布以 `.map` 文本为唯一输入。调整区域、墙体、出生点、按钮或 `prop` 的位置后，必须使用
现有布局导出接口同时生成俯视图和 JSON 信息，再据此检查相对位置、边界和语义对象；不能只凭肉眼阅读
`.map`，也不能手工维护另一份坐标表或 JSON。

推荐流程如下：

1. 修改 `rasterfall/assets/maps/*.map`，保持玩法碰撞、可见几何和交互声明分别表达。
2. 运行 `make map-layout`（或对指定地图调用 `tools/map_layout_export.py`），生成配套的
   `output.png` 与 `output.json`。
3. 打开 PNG 检查整体排布，再用 `tools/map_layout_query.py` 查询对象的精确中心点、bounds、类型和邻近关系；
   PNG 用于空间理解，JSON/query 用于可复核的精确事实。
4. 若排布发生变化，重新导出两份结果并复查，不要继续使用旧的 PNG/JSON。导出的 JSON 是派生诊断资料，
   不能反向编辑来修改地图。

若现有导出器无法表达排布检查所需的信息，应先扩展导出器/schema 和查询接口，再修改地图；不要绕过接口
另写一次性脚本。完成地图排布修改时，至少核对 `safe`、`ai_spawn`、`button`、`prop` 以及相关碰撞体的
相对位置，并确认导出 JSON 的源文件指纹对应当前 `.map`。

## 俯视布局导出

零依赖离线工具 `tools/map_layout_export.py` 把现有 `.map` 导出为开发用俯视 PNG 和 JSON sidecar，
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
`make setup-map-layout`；它在 `.venv/map-layout` 创建独立 Python 环境，安装 Pillow，并检查/安装
`Noto Sans CJK SC`。导出器使用 Pillow 绘制中英双语图例，稳定对象 ID 和 JSON 字段仍保持 ASCII；也可用
`--font path/to/font.ttc` 指定其他 CJK 字体。

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
