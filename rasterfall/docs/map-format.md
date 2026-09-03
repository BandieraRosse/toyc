# Rasterfall 地图格式

> 文档更新：2026-09-03
> 源码核对基线：`75a10cd`（将项目协作说明转向 Rasterfall）

正式地图位于 `rasterfall/assets/maps/*.map`。磁盘结构定义在 `include/toy_map.h`，文本解析在
`lib/map.c`，`src/rasterfall_map.c` 再把结果绑定到玩法盒体、图元、可交互物和安全区。修改语法时
必须同时检查解析、玩法绑定、碰撞/导航、渲染和逻辑测试。

## 几何与碰撞

地图几何的可见性和碰撞是独立属性：

```text
box minx maxx minz maxz height color visible collision
box minx maxx minz maxz height color hidden collision role=air_gate_left
```

未写选项的旧 `box` 默认可见且参与碰撞。空气墙应使用 `hidden collision` 并通过
`role=air_gate_*` 表达用途；旧 `air` 语法只用于兼容已有地图，新内容不要继续使用。

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

其他受支持记录及参数应直接以 `lib/map.c` 的解析分支为准。新增记录时在本文记录用途和最小示例，
不要只修改关卡文件。可见几何不能代替玩法碰撞，渲染正确也不能证明导航和地面查询正确。

