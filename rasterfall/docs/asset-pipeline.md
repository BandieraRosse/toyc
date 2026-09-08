# Rasterfall 资产转换与诊断

> 文档更新：2026-09-08
> 源码核对基线：工作区（`tools/assets/import_asset.py` 统一入口、GLB baseColor texture 闭环、manifest/LOD/原子安装验证、现有转换器）

本文记录可执行的模型、纹理和动画工具链。运行时模块边界见 `assets-animation.md`，动画求值契约
见 `animation-architecture.md`，资源是否允许发布见 `asset-sources.md`。

## 统一导入入口与最终契约

新资产从 manifest 经统一入口导入。入口只负责编排，不把 GLB/PMX 解析合并成通用 Asset IR：

```sh
tools/assets/import_asset.py path/to/foo.asset.json
tools/assets/import_asset.py --force path/to/foo.asset.json
tools/assets/import_asset.py --validate-only path/to/foo.asset.json
make test-asset-pipeline
```

默认安装到 `rasterfall/private-assets/models`；公开资产审核后可显式传
`--output-root rasterfall/assets/models`。输出名称全部由 asset ID 推导：

```text
foo.rmesh
foo.textures/texture_000.ttex
foo.textures/texture_001.ttex
foo_lod1.rmesh                 # manifest 要求时
```

asset ID 只允许小写 ASCII 字母开头以及小写字母、数字、下划线，且发布后不复用。纹理表索引直接
决定三位十进制文件名；LOD 使用正整数层级。临时目录建立在输出根内，以保证最终 rename 不跨文件
系统。转换、TTEX 验证、RMESH 布局/纹理引用和全部 LOD 验证成功后才安装；`--force` 替换时先把旧
asset family 移到同文件系统备份，安装失败会回滚。不要把 `build/`、`tmp/` 或 importer 临时目录提交。

## Manifest

`tools/assets/manifest.example.json` 是 schema 1 示例。必填字段只有 `schema`、`id`、`type`、`source`；
`source` 相对 manifest 定位。可选 `lods` 保存该资产的简化策略，static prop 可记录自身
`dimensions_m`，weapon 可记录自身 `attachments`。输出根、输出路径、RMESH 的 232 units/m 等全局
可推导规则不写进 manifest，未知字段会被拒绝。

manifest 仅供离线导入、完整性验证，以及后续生成/校验 `rasterfall_prop` 或 asset registry；游戏
runtime 不解析 JSON。当前 importer 不生成 runtime registry，避免在契约稳定前制造第二套主数据。

## 空间规范与 Blender 边界

- `static_prop`：源文件为真实米制，Y-up、-Z forward，pivot 位于底面中心；导出前应用对象变换。
  importer 只接受标准化 GLB，不用末端展示缩放修补源资产空间。
- `character`：保留 skeleton、root、bind pose、骨骼层级和蒙皮语义；当前统一运行时导入使用 PMX
  转换路径。不得为了 static prop 规范烘焙或重置这些语义。
- `weapon`：保留 grip/attachment 语义；刚性 GLB 以标准化 origin/manifest attachment 描述表达，
  带骨架武器走 PMX 路径。不要把握持补偿偷偷烘焙成角色专属末端偏移。

Blender 只负责 FBX、复杂场景和源坐标的预处理，按上述类型规范应用/保留变换后导出 GLB 或 PMX
转换链可消费的输入。`glb2rmesh`、`pmx2rmesh` 仍分别拥有各自格式解析；importer 不取代它们。

## GLB 转 RMESH 与纹理

首批十件工业/军事环境组件使用 `tools/blender/generate_rasterfall_props.py` 生成，
完整规格与 CLI 见 [industrial-props.md](industrial-props.md)。默认产物在 `tmp/`，
不自动加入公开资源、内嵌依赖或 Windows package。

```sh
tools/assets/import_asset.py prop.asset.json
```

`app/glb2rmesh.c` 读取 GLB mesh primitive 的 POSITION、可选 NORMAL/TEXCOORD_0、基础 PBR 因子、
baseColorTexture 索引和常见三角形索引，合并 primitive 并修正索引基址。importer 从 GLB bufferView、
base64 data URI 或受源目录约束的相对 URI 提取所引用的 PNG/JPEG，再调用 `toyasset` 解码并转换为
TTEX；`glb2rmesh` 本身不实现图片解码。运行时仍只读 RMESH/TTEX，不解析 glTF JSON。
静态转换路径不导入 GLB 骨架和动画，不能替代 GLB 动画预览路径。
当前仅处理第一个 mesh，忽略 node transform；每个组件必须单独导出、应用变换。
位置默认按 232 量化，极小模型另有自动放大，不等于米到 512 RFU 的玩法换算；
米制环境源资产保持真实尺寸，展示绑定时按已有目标尺寸规则换算。

## 静态 prop asset registry

`include/rasterfall_prop.h` / `src/rasterfall_prop.c` 保存静态组件的 presentation 资产 profile。
当前注册 `crate`、`barrier` 和 `lamp_post`，每项包含稳定 asset ID、名称、RMESH 路径、默认
展示缩放和 RFU 碰撞尺寸。profile 使用 `512 RFU/m ÷ 232 RMESH units/m` 的 milli-scale；该换算
不进入 RMESH 或地图 visual mesh，碰撞尺寸以 RFU 元数据供地图 parser 生成 gameplay primitive。

此 registry 提供查找、单位契约和默认 RFU 碰撞盒；地图 parser 负责实例化碰撞 primitive，renderer
负责模型加载。新增组件时应先在 profile 中分配不复用的 ID，并同步检查
`rasterfall_prop_asset_logic_test()`。

## PMX 转 RFM2/TTEX

```sh
tools/assets/import_asset.py character.asset.json
```

`app/pmx2rmesh.c` 负责 PMX 网格、材质、骨骼、BDEF1/BDEF2 蒙皮及已支持 IK/grant metadata，
并按纹理表索引复制源图片；统一入口调用 `toyasset convert` 转成规范 TTEX，最终目录不保留中间图片。
旧的 `tools/import-pmx-model.sh` 暂时保留给既有调用者，新接入和自动化使用统一入口。

RFM2 是演进中的运行时格式，加载器保留多个旧版本兼容分支。修改格式时必须同步：

- 转换器写入与 `rasterfall_model.c` 读取；
- 版本号、记录宽度、边界检查和旧版本兼容；
- 材质、纹理、蒙皮、IK/grant 诊断；
- 公开模型、私有模型、LOD 工具和 Windows 构建。

当前精确版本与字段布局以转换器和加载器为准，不在本导航复制逐版本字节表。

## LOD

LOD 参数写在 manifest 的 `lods` 中，由统一入口调用 `tools/rmesh_lod.py`。既有 `make lod-*` 目标继续
保留，不要求迁移已有私有资产。

`tools/rmesh_lod.py` 简化索引并保留顶点、骨骼、蒙皮与材质布局，支持现有 RFM2 v2-v13 输出。
LOD 与完整模型共享纹理；缺少
LOD 文件时运行时应回退完整模型。修改选择阈值或布局假设时同时检查 `rasterfall_render.c`。

## GLB 与 VMD 检查

```sh
make app-glb-inspect app-vmd-inspect
build/glb-inspect animation.glb
build/glb-inspect animation.glb humanoid
build/glb-inspect animation.glb basis
build/glb-inspect --self-test
build/vmd_inspect motion.vmd model.rmesh
```

`glb_inspect` 检查 node、skin、accessor、animation 和 humanoid/rest basis，不生成运行时模型。
`vmd_inspect` 检查骨骼名、映射、关键帧、IK 和运动诊断。游戏运行时的 VMD/GLB 诊断参数以
`build/rasterfall --help` 为准。

## 离屏与性能回归

```sh
build/rasterfall --model-views model.rmesh tmp/model-views
build/rasterfall --model-material-regression model.rmesh tmp/material-regression
build/rasterfall --model-performance model.rmesh 5 8
build/rasterfall --actor-performance 30 5 8
```

模型视图和材质回归走游戏内相同的材质与光栅路径。并行度、优化或渲染路径修改后，应比较确定性
帧缓冲哈希和各阶段统计；诊断消融模式不代表默认画质。生成物放在 `tmp/` 或 `build/`，不提交。

## 单位与边界

玩法世界使用 RFU，`512 RFU = 1 m`。PMX、GLB、VMD 的局部单位只在 presentation/导入边界
换算，不能直接进入碰撞、AI 或网络规则。坐标系或 bind pose 异常应从转换器输出一路定位到加载、
姿态求值和渲染，不要用角色专属末端偏移掩盖通用资产错误。
