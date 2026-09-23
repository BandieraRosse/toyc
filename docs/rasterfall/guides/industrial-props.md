# 工业组件生成与导入

> 状态：当前操作指南

组件尺寸、坐标和碰撞建议见[工业组件资产规格](../reference/industrial-props.md)。

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
和验收标准以 [环境资产艺术约束](../reference/environment-art.md) 为准。V2 不改变本页记录的尺寸、pivot、用途、
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

registry 的资源身份和导入边界见[资产导入与诊断](asset-pipeline.md)；
组件的展示比例、尺寸和碰撞建议见[工业组件资产规格](../reference/industrial-props.md)。

## 设施组件

在相同生成器上用 `--assets rf_power_unit rf_gate_frame rf_control_cabinet` 选择三件设施组件；
尺寸与用途见[工业组件资产规格](../reference/industrial-props.md)。已完成的 Campaign 装配见
[环境整合归档](../archive/2026-09-13-environment-integration.md)。
