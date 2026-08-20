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

## PMX 模型导入与第一阶段骨骼蒙皮

对于 MMD/PMX 模型，可以不经过 Blender，直接提取 Rasterfall 的材质、网格和
第一阶段骨骼蒙皮数据：

```sh
tools/import-pmx-model.sh path/to/character-folder character
```

目录中应只有一个 `.pmx`；脚本会构建转换器、复制 PMX 引用的纹理并自动生成
TTEX。重新导入已有名称时显式添加 `--force`。

`pmx2rmesh` 读取 PMX 2.0/2.1 的顶点位置、法线、UV、三角形索引和材质漫
反射色、基础纹理、sphere 和 toon 纹理引用，输出现有 RFM2 格式，并将 PMX 引用的
PNG/BMP 纹理复制到指定目录。再使用 `build/toyasset convert png1024|bmp`
转成 TTEX 后，Rasterfall 会按 RFM2 材质索引加载基础色、sphere 和 toon 纹理；sphere
贴图的乘算（mode 1）与加算（mode 2）模式会按顶点法线生成的 sphere UV
进行混合；mode 3 使用第一组 PMX 附加 UV 作为 SubTexture 乘算。独立 toon
纹理按模型法线生成的光照色阶采样；共享 toon 使用内置色阶。RFM2 v11 在旧网格
尾部追加可选 `SKN1` 段，保存 bone name、parent、rest position、flags 和每顶点
BDEF1/BDEF2；旧 v2～v10 模型没有该段时继续按静态网格渲染。当前不会求值
BDEF4/SDEF/QDEF、IK、append transform、Morph、刚体或关节。PNG 转换为 RGBA TTEX
时会保留 alpha；
全透明像素不写入颜色和深度，纹理 alpha 与 PMX 材质 alpha 相乘。包含透明度
的三角形在不透明命令之后按相机深度由远到近进行 source-over 混合。PMX 材质
alpha、toon 引用和第一组附加 UV 从 RFM2 v6 起保存；v7 还保存材质 drawing
flags，并按 bit 0 区分双面绘制与背面剔除；v8 将材质记录扩展为 24 字节，保存
edge RGBA 和宽度，并用外扩背面壳绘制轮廓；v9 保存环境色、镜面色和镜面指数，
在纹理合成后加入低强度环境光与随指数收窄的镜面高光；v10 在顶点记录中保存
PMX Edge Scale，使材质轮廓宽度可按顶点缩放；v11 增加可选骨骼/蒙皮尾段。加载器
仍兼容 v2 到 v5 的 24 字节旧
顶点记录及 v2 到 v7 的 16 字节旧材质记录，旧格式继续按双面材质渲染。

转换时会输出导入诊断：逐材质列出中英文名称、基础/sphere/toon 纹理、模式、
透明度、drawing flags 及各标志位语义、edge、环境光和镜面参数，并输出模型级
feature summary，汇总几何、纹理、BDEF1/BDEF2 数量、root 数、最大层级深度和异常
bone reference，并列出每根骨骼的 index、name、parent、rest position、flags。高级
骨骼 flags 会解析并报告，但不执行其语义；多于一组的附加 UV 会明确报告为未保留。

可以通过无窗口的离屏渲染输出模型正面、侧面和背面验证图。输出目录会自动
创建，图片采用无需额外编码库的 BMP 格式；该命令走与游戏内相同的材质、
基础纹理和 sphere 混合路径：

```sh
build/rasterfall --model-views \
    rasterfall/private-assets/models/yola.rmesh tmp/yola-views
```

输出为 `front.bmp`、`side.bmp`、`back.bmp` 及对应的 PPM。模型会按包围盒自动居中和缩放，
因此以后可以直接替换 RFM2 路径验证其他动漫模型。

骨骼诊断和程序化 pose 回归可使用：

```sh
build/rasterfall --model-bones rasterfall/private-assets/models/yola.rmesh 腕
build/rasterfall --model-humanoid rasterfall/private-assets/models/yola.rmesh
build/rasterfall --model-humanoid-basis rasterfall/private-assets/models/yola.rmesh
build/rasterfall --model-retarget-test rasterfall/private-assets/models/yola.rmesh right-arm
build/rasterfall --model-glb-animation rasterfall/private-assets/models/yola.rmesh rasterfall/private-assets/models/UAL1_Standard.glb Idle_Loop
build/rasterfall --model-static-views rasterfall/private-assets/models/yola.rmesh tmp/yola-static
build/rasterfall --model-pose-views rasterfall/private-assets/models/yola.rmesh tmp/yola-bind bind
build/rasterfall --model-pose-views rasterfall/private-assets/models/yola.rmesh tmp/yola-arm right-arm
```

`--model-humanoid` 按骨名建立第一版通用人体语义映射，并报告缺失核心骨、重复映射和
异常父链。该映射只保存到现有 skeleton bone index 的对应关系，不改变 RFM2 骨架、
BDEF 蒙皮或 AnimationClip 的运行方式。

离线检查 GLB 骨架和动画元数据可使用：

```sh
make app-glb-inspect
build/glb-inspect animation.glb
build/glb-inspect animation.glb humanoid
build/glb-inspect animation.glb basis
build/glb-inspect --self-test
```

工具只解析 GLB 容器及 glTF 的 node、skin、accessor 和 animation 元数据，不导入
mesh/material，也不生成 Rasterfall 动画。`humanoid` 模式将 Quaternius 的明确骨名
映射到通用 Humanoid 语义，并省略手指等非核心 joint 和逐 channel 明细。
`basis` 模式使用与 RFM2 相同的 Humanoid 规则输出 canonical global rest basis；
它只进行诊断，不转换或播放动画。

`--model-retarget-test` 只把固定角度的 canonical Humanoid rotation delta
换基为目标骨的 parent-local quaternion，用于验证 rotation retarget 数学顺序；它不读取
GLB animation keyframe，也不改变 AnimationClip、root motion 或游戏动画状态。

游戏地图在优菈前方提供 RESET、RIGHT ARM、ARMS 和 BODY TURN 四个 E 互动按钮；
默认单人出生点位于模型正面并朝向模型。这些 pose 仅为本地程序化演示，不进入网络同步。

需要严格对比 sphere 效果时，使用：

```sh
build/rasterfall --model-views-compare \
    rasterfall/private-assets/models/yola.rmesh tmp/yola-compare
```

该命令分别在 `with-sphere/` 和 `without-sphere/` 中生成三张同机位图片；
禁用版本只关闭 sphere 辅助纹理，基础纹理、缩放、相机和光照保持不变。

Toon 对照使用：

```sh
build/rasterfall --model-views-toon-compare \
    rasterfall/private-assets/models/yola.rmesh tmp/yola-toon-compare
```

该命令分别在 `with-toon/` 和 `without-toon/` 中生成三张同机位图片；两组都
保留基础纹理、透明混合和 sphere，只切换 toon 色阶。

Edge 轮廓对照使用：

```sh
build/rasterfall --model-views-edge-compare \
    rasterfall/private-assets/models/yola.rmesh tmp/yola-edge-compare
```

该命令分别在 `with-edge/` 和 `without-edge/` 中生成三张同机位图片，其余材质、
相机和缩放设置保持一致。

环境光与镜面高光对照使用：

```sh
build/rasterfall --model-views-lighting-compare \
    rasterfall/private-assets/models/yola.rmesh tmp/yola-lighting-compare
```

该命令分别输出 `with-lighting/` 和 `without-lighting/`，只切换 RFM2 v9 保存的
ambient/specular 材质参数。

完整材质回归使用：

```sh
build/rasterfall --model-material-regression \
    rasterfall/private-assets/models/yola.rmesh tmp/yola-material-regression
```

输出目录包含 `base/`、`sphere/`、`toon/`、`full/` 四组三视图和
`manifest.txt`。清单记录每张图片的确定性像素哈希、非背景像素数、平均亮度
与近黑像素数，可用于提交前比较材质或光栅器修改是否造成视觉回退。
模型三视图命令还会逐视角输出主体与 Edge 的三角形统计，包括 near reject、
near clip、背面剔除、实际输出和延迟渲染命令溢出数量。

模型功能性能基准会预加载一次资源，并在固定的 800×800 正/侧/后三视图下比较
Full、Edge off、Toon off、Sphere off、lighting off 和 model off：

```sh
build/rasterfall --model-performance \
    rasterfall/private-assets/models/yola.rmesh 5 8
```

第二个参数是每个视角的迭代次数，可选第三个参数固定 worker 数量（1–8，省略时
自动取可用 CPU 数并限制为 8）；输出将墙钟时间拆为 clear、triangle setup、
透明命令准备和 pixel raster；透明阶段进一步拆出分类、归并/复制、实际排序，
并输出 opaque、transparent、Edge 命令数和真实排序元素数。纯色/纹理光栅的
工作线程累计 CPU 时间、三角形数和像素漏斗也会一并输出；不包含模型加载、
纹理解码或图片写盘。Full 模式还会逐 worker 输出活跃/CPU 时间、检查命令数、
相交三角形数以及 bbox、inside、depth-pass、shaded/written、flat/texture 像素。
基准同时包含 base-texture-only、强制 opaque、仿射 UV 和简化纹理寻址等诊断
消融；其中标记 `intentionally_changed=yes` 的模式只用于归因，不代表默认画质。

版权受限的本地测试模型应放在 `private-assets/` 下；该目录已被 Git 忽略，
不会参与公开资源或嵌入式发布构建。当前本地角色样本位于
`private-assets/models/yola.rmesh`，游戏会将其绘制在 `(-13000, -900, -10000)`。
同目录存在 `UAL1_Standard.glb` 时，游戏会在旁边绘制一个直接使用 GLB 原骨架
和 skin 的 Quaternius 模型。第二排 GLB Idle/Walk/Jog 按钮播放原始 local TRS
动画，不经过 Humanoid retarget，用于和第一排优菈 retarget 结果同场对照。
