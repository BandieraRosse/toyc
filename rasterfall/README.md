# Rasterfall

Rasterfall 的代码、专属游戏引擎和运行时资源集中在本目录：

- `src/`：游戏主程序及各功能模块
- `include/`：Rasterfall 模块头文件，以及专属 `toy_game`/地图引擎接口
- `lib/`：Rasterfall 专属游戏规则、SFX 和地图加载实现
- `assets/audio/`：音效资源
- `assets/maps/`：地图资源
- `assets/textures/`：游戏纹理资源
- `assets/models/`：由外部 GLB 转换得到的 RFM1 静态网格资源

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

## 外部模型导入

Rasterfall 不在游戏进程中解析 glTF JSON、材质和图片，而是使用仓库内的
`RFM1` 紧凑网格格式。这样运行时只需读取定长顶点和索引，适合当前的
freestanding 软件光栅器。转换 `Zombie.glb` 的流程是：

```sh
mkdir -p rasterfall/assets/models
make app-glb2rmesh
build/glb2rmesh Zombie.glb rasterfall/assets/models/zombie.rmesh
```

`glb2rmesh` 当前支持 GLB 内单个静态 primitive 的 `POSITION`、可选
`NORMAL`/`TEXCOORD_0` 和 `UNSIGNED_BYTE/SHORT/INT` 三角形索引；它会将模型
坐标按 `232` 倍转换为 Rasterfall 世界单位。材质、贴图、骨骼和动画暂不在
转换器中展开，后续可在 `rasterfall_model.h` 的固定格式之上增加运行时材质
与骨骼表，而不改变外部模型导入入口。
