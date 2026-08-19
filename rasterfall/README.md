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
build/pmx2rmesh character.pmx \
    rasterfall/private-assets/models/character.rmesh \
    rasterfall/private-assets/models/character.textures
```

`pmx2rmesh` 读取 PMX 2.0/2.1 的顶点位置、法线、UV、三角形索引和材质漫
反射色、基础纹理和 sphere 纹理引用，输出现有 RFM2 格式，并将 PMX 引用的
PNG/BMP 纹理复制到指定目录。再使用 `build/toyasset convert png1024|bmp`
转成 TTEX 后，Rasterfall 会按 RFM2 材质索引加载基础色和 sphere 纹理；sphere
贴图的乘算（mode 1）与加算（mode 2）模式会按顶点法线生成的 sphere UV
进行混合。依赖附加 UV 的 mode 3 暂不应用；骨骼、Morph、刚体、toon 贴图和
alpha 混合当前只跳过。

可以通过无窗口的离屏渲染输出模型正面、侧面和背面验证图。输出目录会自动
创建，图片采用无需额外编码库的 BMP 格式；该命令走与游戏内相同的材质、
基础纹理和 sphere 混合路径：

```sh
build/rasterfall --model-views \
    rasterfall/private-assets/models/yola.rmesh tmp/yola-views
```

输出为 `front.bmp`、`side.bmp` 和 `back.bmp`。模型会按包围盒自动居中和缩放，
因此以后可以直接替换 RFM2 路径验证其他动漫模型。

需要严格对比 sphere 效果时，使用：

```sh
build/rasterfall --model-views-compare \
    rasterfall/private-assets/models/yola.rmesh tmp/yola-compare
```

该命令分别在 `with-sphere/` 和 `without-sphere/` 中生成三张同机位图片；
禁用版本只关闭 sphere 辅助纹理，基础纹理、缩放、相机和光照保持不变。

版权受限的本地测试模型应放在 `private-assets/` 下；该目录已被 Git 忽略，
不会参与公开资源或嵌入式发布构建。当前本地角色样本位于
`private-assets/models/yola.rmesh`，游戏会将其绘制在 `(-13000, -900, -10000)`。
