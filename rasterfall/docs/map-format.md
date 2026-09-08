# Rasterfall 地图格式

> 文档更新：2026-09-08
> 源码核对基线：工作区（地图布局 PNG/JSON 导出器；静态 prop 实例及 profile 碰撞盒接入现有 primitive/nav）

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

## 俯视布局导出

零依赖离线工具 `tools/map_layout_export.py` 把现有 `.map` 导出为开发用俯视 PNG 和 JSON sidecar，
不引入新地图语法，也不进入游戏运行时：

```sh
make map-layout
# 或导出任意地图，固定生成 output.png / output.json
python3 tools/map_layout_export.py path/to/level.map --output-dir tmp/level-layout
make test-map-layout-export
```

PNG 使用 x/z 平面、RFU 网格、色块/线框和紧凑 ID；隐藏碰撞以交叉线框 `AWn` 显示，普通 box
只挑 role box 和面积最大的少量碰撞 box 标为 `BXn`。JSON schema 为
`rasterfall-map-layout-v1`，每个对象保留源记录名、原始字段和行号，并补充导出 ID、中心点及 bounds；
顶层明确记录 `512 RFU = 1 m`。当前范围只覆盖布局理解，不做 chokepoint、路径分析或高程渲染。

最小人工验收是在正式地图上运行 `make map-layout`，打开 `tmp/map-layout/output.png`，抽查 safe、
spawn、button、prop 的相对位置，再用相同导出 ID 对照 `output.json` 的中心点与 bounds。当前正式地图
没有 `base` 或 `safe goal` 记录；这两类由自动化覆盖用例验证，待正式地图实际声明后再加入人工抽查。
