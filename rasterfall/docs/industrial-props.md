# 第一套程序化工业 / 军事组件

> 文档更新：2026-09-08
> 源码核对基线：工作区（十件 V2 Hybrid 的程序化几何、flat surface role、局部 32×32 sign 与统一 importer；空间与运行时契约不变）

## V2 Hybrid 生成

十件组件默认采用 geometry + flat materials + limited decal texture。`PILOT` 保存工业
主体 palette 和标识身份，`ACCENTS` 保存少量功能色；`Builder.box()` 用面 UV 的 tile role
标记主体/框架/点缀/标牌。`hybrid_prop()` 将大面转为无纹理 flat 材质，只有局部标牌正面
保留纹理；crate 沿用已确认的 `hybrid_crate()`。色值从 sRGB palette 转为线性 GLB factor。
每件使用三个或四个材质/primitive、一张 32×32 标牌 PNG。原生 exporter 与无 numpy 的
静态 writer 均支持 UV/内嵌 PNG；不存在大面积纹理磨损、油迹或面板边线。

```sh
blender -b --python-exit-code 1 --python tools/blender/generate_rasterfall_props.py -- \
    --output tmp/props-v2-hybrid-set
for id in rf_crate rf_barrier rf_short_wall rf_railing rf_lamp_post rf_vent_unit \
          rf_workbench rf_ammo_container rf_industrial_pillar rf_pipe_module; do
    cp "tmp/props-v2-hybrid-set/$id.glb" rasterfall/private-assets/source/props/industrial/
    tools/assets/import_asset.py --force --output-root rasterfall/assets/models/props/industrial \
        "tools/assets/manifests/props/industrial/$id.asset.json"
done
```

可用 `--assets ID ...` 只生成所选资产；输出含独立 GLB、标牌 PNG 和已 pack 图像的 blend。
已有输出需显式 `--overwrite`，可用 `cmp` 比较两次独立 GLB/PNG。安装后由既有展示区
直接加载；资源身份、路径、展示比例、地图、碰撞及 runtime 均不变。

### Crate 对照

生成时加 `--assets rf_crate --crate-material hybrid`。`hybrid_crate()` 在原 V2 几何完成后
按现有面 UV 分区重分配材质，不改变位置、法线、三角形或空间契约。主体和凹盖使用灰绿
flat 材质，框架使用暗灰 flat 材质，只有铭牌正面使用纹理；共三个材质/primitive。
编号 tile 独立裁为 32×32，保持原 texel 密度和留边。取消全表面纹理面板边线、底缘磨损、
积尘与盖板油迹；保留几何凹盖、粗箍、护角和编号。没有新增警示图案或几何。
`--crate-material full` 仍可生成 full-texture 对照，默认是 `hybrid`；其他资产不受该选项影响。
两种模式均沿用 `rf_crate` ID，经既有 importer 安装；同场 A/B 需分别安装并重启游戏，
保持同一地图、相机和实例数量，不为试验新增 registry ID 或地图实例。

## 生成与检查

在仓库根目录执行（Blender 3.6+，使用随 Blender 提供的 glTF 导出器）：

```sh
blender -b --python-exit-code 1 --python tools/blender/generate_rasterfall_props.py
```

默认输出 `tmp/rasterfall-props/`：一个 `rasterfall_props.blend` 和十个独立 GLB。
`--python-exit-code 1` 让自动化任务能够检测脚本断言或导出失败。
可选参数位于 Blender 的 `--` 之后：

```sh
blender -b --python-exit-code 1 --python tools/blender/generate_rasterfall_props.py -- --output tmp/props-review --all-glb
```

已有同名输出默认拒绝覆盖；明确需要重新生成时加 `--overwrite`。
脚本会清空当前场景，适合在单独的 background 进程中运行。
所有部件合并为独立 mesh，倒角固定一段并应用，显式三角化、flat normals，
无灯光、相机、动画、骨骼或压缩扩展。按用途分配低饱和主体色与暗框、局部功能色；
使用不透明 Base Color、metallic=0、roughness=0.9，不依赖 PBR 效果。
大面颜色形成分区，倒角贡献轮廓与明暗变化，纹理仅提供粗编号、箭头或单个安全符号。
不做螺丝、细字或密集格栅。V2 light upgrade 的颜色、几何、纹理预算、逐件改造要点
和验收标准以 [environment-art.md](environment-art.md) 为准。V2 不改变本页记录的尺寸、pivot、用途、
碰撞建议或导入契约。

终端先报告 Blender mesh 顶点、实际三角形、预算和边界，再报告 GLB 因平面法线拆分后的
实际顶点、三角形和 primitive 数。脚本检查尺寸、地面 pivot、非退化面、200～预算上限、
GLB 单 mesh、identity node transform、材质 primitive 数和导出后的 Y-up 边界。
这些检查失败会终止；已经完成的输出可能保留，下次应换输出目录或显式覆盖。

总 blend 中十件资产保持地面原点重合、各自一个 collection；默认仅显示 crate，
在 Outliner 切换眼睛图标查看其他组件。这样库中每件资产的 transform 均为 identity。
可选总 GLB 同样原点重合，用于资产库交换，不是陈列场景。
现有转换器只读取第一个 mesh，**仅将独立 GLB 交给转换器**：

```sh
make app-glb2rmesh
build/glb2rmesh tmp/rasterfall-props/rf_crate.glb tmp/rasterfall-props/rf_crate.rmesh
```

导出选项依据 [Blender glTF API](https://docs.blender.org/api/3.0/bpy.ops.export_scene.html)。
生成器不需要第三方插件，也不更改构建、地图、碰撞或运行时格式。

生成后的十个 manifest 位于 `tools/assets/manifests/props/industrial/`，公开运行时产物统一安装到
分类目录：

```sh
for manifest in tools/assets/manifests/props/industrial/*.asset.json; do
    tools/assets/import_asset.py --force \
        --output-root rasterfall/assets/models/props/industrial "$manifest"
done
```

首批十件组件均已进入静态 prop asset registry，profile 位于
`rasterfall/include/rasterfall_prop.h` / `rasterfall/src/rasterfall_prop.c`；对应 manifest 位于
`tools/assets/manifests/props/industrial/`；运行时 RMESH 位于
`rasterfall/assets/models/props/industrial/`，本地源文件位于
`rasterfall/private-assets/source/props/industrial/`。registry 只登记稳定资产 ID、RMESH
路径、`512/232` 展示缩放和默认 RFU 尺寸，不改变地图格式，也不负责实例化或碰撞。

地图开发者区的 `z=-17000` 陈列带已接入十件组件，x 坐标从 `-14500` 到 `-1000`，相邻实例
中心间距为 1500 RFU；按各 profile 的旋转后碰撞 AABB，最小水平间隙为 271 RFU，因此展示
区不会发生实例或默认碰撞盒重叠。

## 统一空间约定

以下尺寸均为米，顺序为 **宽 X × 深 Y × 高 Z（Blender）**。
全部 object 的 pivot 为占地包围盒底面中心 `(0,0,0)`，不是几何重心。
Blender 为 Z-up、-Y-forward；GLB 为 Y-up、+Z-forward，映射 `(x,y,z) → (x,z,-y)`。
对称组件仍沿用同一 forward 约定。transform 应用后 location/rotation 为零、scale 为一。

GLB 保留真实米制尺寸。现有 `glb2rmesh` 将位置乘以 232 存储（极小模型还有自动放大分支，
本套尺寸不会触发），这不等于 `512 RFU = 1 m` 的玩法世界尺度。
例如 1 m 在 RFM2 中约为 232 个整数单位。未来绑定展示时应使用已有目标尺寸换算，
比例约为 `512/232`；不要把 GLB 米、转换器量化尺度或文件整数直接当地图 RFU。
本次不添加运行时绑定，也不将比例烘焙进 GLB 而破坏源资产实际尺寸。

## 组件规格

预算是建议上限，不是要求填满；实际统计以运行输出为准。各组件为 3–4 materials，
pivot 和 forward 均使用上述统一约定。

| object name | 建议 triangles | 实际尺寸 m | 主要 silhouette | 地图典型用途（设计建议） |
| --- | --- | --- | --- | --- |
| `rf_crate` | 200–600 | 1.2 × 1.0 × 1.0 | 宽顶底框、两道粗箍、正面宽铭牌块 | 仓库货垛、路口遮挡、补给区标记 |
| `rf_barrier` | 200–600 | 2.4 × 0.8 × 1.0 | 宽脚窄肩梯形、三块肩标、正面换装板 | 检查站、道路分流、低掩体 |
| `rf_short_wall` | 200–600 | 2.4 × 0.5 × 1.4 | 粗压顶、底座、三条竖向筋 | 院区分隔、走廊转角、阵地边缘 |
| `rf_railing` | 200–600 | 2.4 × 0.3 × 1.1 | 三根立柱、双横杆、宽脚板，保留大空隙 | 平台边缘、危险区域、通道引导 |
| `rf_lamp_post` | 200–600 | 0.8 × 0.8 × 3.2 | 高细杆、前伸大灯头、宽底座 | 路口与入口视觉地标；灯面仅色块，无实际光源 |
| `rf_vent_unit` | 200–800 | 1.4 × 0.9 × 1.2 | 厚柜体、四片宽百叶、顶底框 | 机房、屋顶设备、墙边遮挡 |
| `rf_workbench` | 200–600 | 1.8 × 0.8 × 0.9 | 土黄台面、四粗腿、底架、单侧双柜面 | 工坊、维修点、补给区 |
| `rf_ammo_container` | 200–600 | 0.9 × 0.5 × 0.6 | 扁长箱、厚盖、双锁扣、低矮提手 | 弹药存放视觉提示；不自动成为可交互拾取物 |
| `rf_industrial_pillar` | 200–600 | 0.8 × 0.8 × 2.8 | 窄倒角柱身、宽柱头柱脚、双套环 | 走廊节奏、厂房结构视觉提示 |
| `rf_pipe_module` | 200–800 | 1.6 × 0.8 × 1.4 | 双八棱立管、粗法兰、共同底座 | 设备区、管线端站、机房轮廓变化 |

## 复用与碰撞建议

下表碰撞盒为 Blender 局部米制 **中心 C / 尺寸 S**，纯设计建议，不导出碰撞 mesh。
除灯柱外均取整件外包围盒，栏杆空隙、工作台下方因此也会挡人；这适合当前简单盒体玩法，
但不表示视觉孔洞可穿行。若未来使用已有地图 box，需要先按轴向映射并乘 512、取整；
当前 box 为地面起的水平轴对齐盒，任意角度旋转要重新计算保守 AABB。

| object | 推荐盒 C / S（m） | 重复 / 旋转 / 缩放 |
| --- | --- | --- |
| crate | (0,0,0.5) / (1.2,1.0,1.0) | 可重复、堆叠；绕 Z 90°；建议等比 0.8–1.25 |
| barrier | (0,0,0.5) / (2.4,0.8,1.0) | 2.4 m 模数串联；90°；可沿 X 0.75–1.5 |
| short_wall | (0,0,0.7) / (2.4,0.5,1.4) | 2.4 m 串联及直角转角；可沿 X 0.5–1.5 |
| railing | (0,0,0.55) / (2.4,0.3,1.1) | 2.4 m 串联；90°；可沿 X 0.75–1.5，避免缩细横杆 |
| lamp_post | (0,0,1.6) / (0.8,0.8,3.2) | 稀疏重复；灯头朝通道；建议等比 0.9–1.1；盒较保守 |
| vent_unit | (0,0,0.6) / (1.4,0.9,1.2) | 可成排；前面朝通道；建议等比 0.8–1.25 |
| workbench | (0,0,0.45) / (1.8,0.8,0.9) | 靠墙重复；90°；可沿 X 0.8–1.2，保持台高 |
| ammo_container | (0,0,0.3) / (0.9,0.5,0.6) | 可重复堆放，按完整高度留提手空间；90°；等比 0.8–1.2 |
| industrial_pillar | (0,0,1.4) / (0.8,0.8,2.8) | 规则间距重复；90°；沿 Z 0.8–1.5 |
| pipe_module | (0,0,0.7) / (1.6,0.8,1.4) | 成排设备；90°；建议等比 0.8–1.2 |

为碰撞与对齐优先使用 90° 摆放；纯装饰可任意绕竖轴旋转。避免负缩放。
表中缩放指将来的场景摆放或制作派生版本；生成的基础资产均为 scale=1。
组合件允许封闭子网格相交，不做昂贵布尔并集；少量内部面换取稳定生成和低面数。
它们不适用于需要整个资产水密连通的物理体积计算。

现有地图 `model` 记录不是任意文件路径实例接口，生成 GLB/RFM2 不会自动把这些组件加入关卡。
典型用途、碰撞建议和灯面颜色均不产生玩法或光照效果。资产能进入现有转换流程，
实际地图装配需要另外明确授权的运行时工作。
在目标低分辨率下，先检查整件轮廓与遮挡效果，再增加摆放数量；低三角数不能替代
CPU 光栅化的覆盖像素、过度绘制和可见实例数预算。新增变体只扩展 `SPECS` 与 `build()`，
沿用同一材质、空间和导出断言，不新增资产格式。
