# Rasterfall

Rasterfall 的代码、专属游戏引擎和运行时资源集中在本目录：

- `src/`：游戏主程序及各功能模块
- `include/`：Rasterfall 模块头文件，以及专属 `toy_game`/地图引擎接口
- `lib/`：Rasterfall 专属游戏规则、SFX 和地图加载实现
- `assets/audio/`：音效资源
- `assets/maps/`：地图资源
- `assets/textures/`：游戏纹理资源

地图文件使用 `assets/maps/*.map` 的文本格式。地图几何的可见性和碰撞是
独立属性：`box ... visible collision` 表示可见且阻挡，`box ... hidden
collision` 表示只参与碰撞；未写选项的旧 `box` 默认两者都开启。空气墙应
使用 `role=air_gate_*` 标识，例如：

```text
box -12000 -1800 -5700 -5680 1800 000000 hidden collision role=air_gate_left
```

`safe ... start/goal` 声明起点和终点安全室；`base id minx maxx minz maxz`
声明据点；`ai_spawn name base_id level1|level2|level3 x z downed` 声明 AI
出生点。旧的 `air` box 语法仍兼容，但新地图应使用显式的
`visible/collision/role` 选项。

从仓库根目录执行 `make rasterfall` 或 `make app-rasterfall` 构建游戏。
