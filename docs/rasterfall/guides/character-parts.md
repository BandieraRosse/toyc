# 私有角色分部件工作流

> 状态：当前操作指南

所有权见[分部件创作架构](../architecture/character-authoring.md)，当前 RF-C01 造型与限制见
[设计稿](../reference/rf-c01-design-study.md)。在仓库根执行；Blender 路径按本机安装调整。

## 入口与组装

```powershell
$blenderExe = 'E:/Blender 5.2/blender.exe'
$partsRoot = 'rasterfall/private-assets/source/characters/rf_c01/authoring'
$partsTool = 'tools/blender/rf_character_parts.py'
& $blenderExe --background --python-exit-code 1 --python $partsTool -- --help
& $blenderExe --background --python-exit-code 1 --python $partsTool -- assemble --manifest "$partsRoot/assembly.json" --output "$partsRoot/build/review-next.blend"
```

初始完整角色可直接打开 `authoring/build/rf_c01.blend`。每个 `parts/<part>/v001.blend` 也包含可打开
编辑的独立部件场景。骨架和审阅场景分别在 `rig/`、`review/`；私有源、预览、GLB 和 RFM2 均不加入 Git。
新输出使用未占用路径；工具拒绝覆盖已有部件版本及组装结果。

## 手工修改脸部或前发

打开部件源或完整组装文件，先另存到工作副本。修改目标部件；保留对象的 `part_id` 和 `object_id`，
不要覆盖清单锁定的源文件。编辑脸部 Basis 后，检查旧表情是否需要同步处理。

```powershell
& $blenderExe --background --python-exit-code 1 --python $partsTool -- publish --manifest "$partsRoot/assembly.json" --part head --revision v002 --source "$partsRoot/work/head-edited.blend"
```

成功后产生 `parts/head/v002.blend` 和 `assembly-head-v002.json`；其他部件继续引用原版本。
从完整组装文件发布时，误改身体 UV、材质或审阅场景会拒绝。换成 `hair_front` 可独立发布前发。
当前普通修订保持对象 ID 集合；新增或删除对象属于显式部件清单迁移。

## 程序修改侧后发

复制 `hair-v001.json` 为新的参数文件，只修改所需发束 ID 下的角度、宽度、发尾或弯曲参数。
不要改变共享空间接口来达到单束局部修形。

```powershell
& $blenderExe --background --python-exit-code 1 --python $partsTool -- rebuild --manifest "$partsRoot/assembly.json" --part hair_back --revision v002 --parameters "$partsRoot/hair-v002.json"
```

该命令只加载侧后发源并生成新版本，不加载或重算脸部、身体及表情。`hair_base` 使用同一独立生成器
的另一入口；`hair_front` 当前通过手工网格发布。省略 `--parameters` 使用所选清单的参数入口。

## 表情与验收

在修改过脸部的候选上，显式生成表情修订：

```powershell
& $blenderExe --background --python-exit-code 1 --python $partsTool -- expressions --manifest "$partsRoot/assembly-head-v002.json" --part head --revision v003
```

这一步读取清单锁定的眼球表面，仅修改头部表情 key；眼部装饰或口腔需要变化时，分别修订 `eyes`
或 `mouth` 并串联新的候选清单。当前表情仍是 Blender 离线诊断，不是游戏表情系统。

以侧后发候选为例，组装、限定变更范围并固定视角审阅：

```powershell
& $blenderExe --background --python-exit-code 1 --python $partsTool -- assemble --manifest "$partsRoot/assembly-hair_back-v002.json" --output "$partsRoot/build/hair-v002.blend"
& $blenderExe --background --python-exit-code 1 --python $partsTool -- verify --manifest "$partsRoot/assembly-hair_back-v002.json" --blend "$partsRoot/build/hair-v002.blend" --baseline "$partsRoot/baseline-v001.json" --allow hair_back --report "$partsRoot/build/hair-v002-validation.json"
& $blenderExe --background --python-exit-code 1 --python $partsTool -- render --blend "$partsRoot/build/hair-v002.blend" --output "$partsRoot/build/hair-v002-review"
```

`render --structure` 隐藏头发；`--expression Blink` 或 `MouthOpen` 检查表情。渲染操作不保存回源文件。
相机/灯光固定，但更改发型引起的真实投影或遮挡变化仍需人工判断。

## 导出与回归

```powershell
& $blenderExe --background --python-exit-code 1 --python $partsTool -- export --blend "$partsRoot/build/hair-v002.blend" --output "$partsRoot/build/hair-v002.glb"
$validatorExe = (Resolve-Path 'build-windows/glb-inspect.exe').Path
$glbFile = (Resolve-Path "$partsRoot/build/hair-v002.glb").Path
$rmeshFile = [System.IO.Path]::ChangeExtension($glbFile, '.rmesh')
python tools/assets/rfchar_import.py $glbFile $rmeshFile --validator $validatorExe
& build-windows/rfchar-runtime-test.exe $rmeshFile
```

导出仅修改一次性副本：清空表情、还原 bind、按材质签名合并。原生工具构建和路径规则见
[资产导入指南](asset-pipeline.md#glb-与-vmd-检查)。每一步核对非零退出码，不以输出文件存在代替成功。

修改创作工具时运行独立副本上的集成检查；输出目录必须不存在：

```powershell
& $blenderExe --background --python-exit-code 1 --python tools/blender/test_rf_character_parts.py -- --manifest "$partsRoot/assembly.json" --output "$partsRoot/build/isolation-next"
```

检查覆盖无改动头发/表情重建、一束发宽变化的范围、手工脸部发布、越界身体 UV 修改拒绝、
同色不同 shader 不合并、保存重开以及源文件保护。它需要本机私有部件源，不属于无私有资产的默认测试。
