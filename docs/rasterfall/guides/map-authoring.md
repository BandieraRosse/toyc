# 地图编辑与查询

> 状态：当前
> 所有者：Rasterfall 地图编辑与布局诊断
> 事实入口：`Makefile`、`tools/map_layout_export.py`、`tools/map_layout_query.py`
> 最近核对：2026-09-23

空间地图的字段与约束见 [地图格式](../reference/map-format.md)；Runtime Map、World Content 和投影所有权见 [地图与世界内容](../architecture/maps-and-world-content.md)。

V1 语法可用 `build/map-inspect <map-v1-file>` 检查，`make test-map-parser` 验证错误输入和容量边界。
组件碰撞用 `build/map-inspect --collision-json <map>` 查看 owner、bounds、高度和 flags；
`make map-layout` 会自动构建检查器。修改组件投影时再运行 `make test-map-components test-map-runtime` 和
`build/rasterfall --logic-test`，结合正式画面检查可见与碰撞结果。

用 `build/rasterfall --map path/to/experiment.map` 只覆盖本次进程的启动地图。修改 V1 输入链路时运行
`make test-map-runtime`，覆盖 runtime 加载、稳定 ID 查询和 action registry；也可直接运行
`build/map-runtime-test`。`--legacy-map` 仅用于显式兼容检查，实际参数以程序 `--help` 为准。

## 修改地图排布的必经流程

正式 Campaign 设施组合记录位于 `rasterfall.map` 的 `env_*` object 段，沿用既有
空间和 World Content。当前 adapter 投影全部 object；显式 `attr.legacy_index` 保留迁移排列，无索引 object 按稳定 ID 追加。新增实例
必须检查 runtime/projection 数量和真实 render，不能只看布局导出。V1 object placement
进入 `level.props`；显式 `attr.collision` 选择独立的 Map component collision contract，原有
collision records 与生成结果都由 Runtime Map 拥有。

V1 object 的 `y` 是相对地面基准的 RFU 高度，经 `toy_map_prop.y` 传给 static prop renderer，
最终 pivot 为 `-900 + y`；它不查询或自动吸附 gameplay surface。Campaign 的 `env_arch_*`
用于西侧维修巷、东侧双入口设施和南侧动力区，梁、面板、管线及线槽必须显式填写高度。
Campaign 现已显式启用组件碰撞；门洞使用两肩和架空上梁，不能用整块 AABB 封门。

`--environment-capture <dir>` 以固定 seed 加载 Campaign，输出基地、北区、东西设施、
东西路线、Hurd、南侧、坡道、出生室与南侧动力场视角；`tools/environment_sheet.py <dir>` 拼接原始 BMP。
该入口使用正常 scene/actor/flag renderer，不进行 gameplay tick。

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
