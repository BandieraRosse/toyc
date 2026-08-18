# Rasterfall

Rasterfall 的代码、专属游戏引擎和运行时资源集中在本目录：

- `src/`：游戏主程序及各功能模块
- `include/`：Rasterfall 模块头文件，以及专属 `toy_game`/地图引擎接口
- `lib/`：Rasterfall 专属游戏规则、SFX 和地图加载实现
- `assets/audio/`：音效资源
- `assets/maps/`：地图资源
- `assets/textures/`：游戏纹理资源
- `assets/models/`：由外部 GLB 转换得到的 RFM2 静态网格资源

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

从仓库根目录执行 `make rasterfall` 或 `make app-rasterfall` 构建游戏。Linux 默认从
本目录读取资源；如需旧的单文件方式，使用 `make rasterfall-embedded`，生成
`build/rasterfall-embedded`。Windows 使用 `make win-rasterfall`，生成自包含的
`build/rasterfall.exe`。

## 外部模型导入

Rasterfall 不在游戏进程中解析 glTF JSON，而是使用仓库内的 `RFM2` 紧凑
网格格式。这样运行时只需读取定长顶点、索引和材质表，适合当前的
freestanding 软件光栅器。转换 `Zombie.glb` 的流程是：

```sh
mkdir -p rasterfall/assets/models
make app-glb2rmesh
build/glb2rmesh Zombie.glb rasterfall/assets/models/zombie.rmesh
```

`glb2rmesh` 支持一个 GLB Mesh 内全部静态 primitive 的 `POSITION`、可选
`NORMAL`/`TEXCOORD_0` 和 `UNSIGNED_BYTE/SHORT/INT` 三角形索引，并会自动
合并顶点和修正索引基址。它会将模型坐标按 `232` 倍转换为 Rasterfall 世界
单位。RFM2 会保存 primitive 到材质的映射，以及 GLB 的
`baseColorFactor`、metallic 和 roughness；当前仍不展开图片贴图、骨骼和
动画。

`.claude/glb/` 中的武器和弹药箱资源已经转换到 `assets/models/*.rmesh`，
文件名使用小写下划线命名，并保留同名变体的来源后缀。

## PMX 静态模型导入

对于 MMD/PMX 模型，可以不经过 Blender，直接提取 Rasterfall 当前静态
渲染所需的数据：

```sh
make app-pmx2rmesh
build/pmx2rmesh character.pmx rasterfall/assets/models/character.rmesh
```

`pmx2rmesh` 读取 PMX 2.0/2.1 的顶点位置、法线、UV、三角形索引和材质漫
反射色，输出现有 RFM2 格式。骨骼、Morph、刚体、关节、toon/sphere 贴图
和原始图片纹理当前只跳过，不会写入 RFM2；因此输出模型是固定静态网格，
使用 Rasterfall 当前的纯色材质路径渲染。

版权受限的本地测试模型应放在 `private-assets/` 下；该目录已被 Git 忽略，
不会参与公开资源或嵌入式发布构建。当前本地角色样本位于
`private-assets/models/yola.rmesh`，游戏会将其绘制在 `(-13000, -900, -10000)`。
