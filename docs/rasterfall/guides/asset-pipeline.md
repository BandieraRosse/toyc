# Rasterfall 资产导入与诊断

> 状态：当前操作指南

本文记录可执行的模型、纹理和动画工具链。运行时模块边界与动画求值见
[动画架构](../architecture/animation-architecture.md)，Core 资源读取边界见[运行时架构](../architecture/runtime.md)，
资源是否允许发布见[资源来源台账](../reference/asset-sources.md)。

## 模型与动画求值回归

涉及 resource/instance、动作组合、IK 或重定向时运行：

```sh
build/rfchar_runtime_test <rfchar.rmesh>
build/rasterfall --character-acceptance <rfchar.rmesh> <output>
build/rasterfall --action-composition-capture <model.rmesh> <lower.rfanim> <lower-ms> <upper.rfanim> <upper-ms> <additive.rfanim> <additive-ms> <output.bmp>
make app-vmd-inspect app-glb-inspect rasterfall
build/vmd_inspect <walk.vmd> <model.rmesh> --vmd-leg-trace
build/vmd_inspect <walk.vmd> <model.rmesh> --vmd-walk-final-flips
```

检查一 resource / 两 instance 的隔离，以及完整 walk 连续性。修改 RFM2 格式、公共重定向数学或构建目标时，再覆盖相关转换工具、Windows 构建和自托管应用构建。实际参数以各程序 `--help` 为准。

## Enemy Visual V2

六份公开 infected RFM2 使用现有 character manifest → RFCHAR importer → runtime 门禁；
`tools/enemy_visual_round.py --generate --capture --deterministic` 复用 Blender V2 导出，生成并验证
Block/Humanoid 的 Common/Fast/Heavy。公开产物不依赖私有模型；重建命令、来源、预算与
真实 enemy renderer 截图入口见 [enemy-visuals.md](enemy-visuals.md)。

## RFANIM V1

动作文本字段和验证规则见 [RFANIM V1 格式](../reference/rfanim-format.md)。

当前 fixture 为 `rifle_idle.rfanim`。用 `build/rf_anim_info` 检查结构，用 Rasterfall 的
`--action-preview` 固定输出 BMP；相同模型、动作与毫秒输入必须逐字节一致。

## 统一导入入口与最终契约

私有 RF-C01 在 GLB 之前使用[分部件创作工作流](character-parts.md)：独立保存脸、眼口、头发、身体，
按冻结清单组装，并只在导出副本中合批；不改变下述 importer 和 RFCHAR 运行时合同。

新资产从 manifest 经统一入口导入。入口只负责编排，不把 GLB/PMX 解析合并成通用 Asset IR：

```sh
tools/assets/import_asset.py path/to/foo.asset.json
tools/assets/import_asset.py --force path/to/foo.asset.json
tools/assets/import_asset.py --validate-only path/to/foo.asset.json
make test-asset-pipeline
```

Windows 先构建原生 asset-tools，其中包含图片转换所需的 `toyasset.exe`，随后显式传入
`--no-build --tool-dir build-windows`：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 asset-tools
python tools/assets/import_asset.py --no-build --tool-dir build-windows --character-surface --output-root rasterfall/assets/models/characters --force tools/assets/manifests/characters/rf_clothing_field_jacket.asset.json
```

`--character-surface` 仅为 RFCHAR GLB 显式启用 RFM2 v15 的 opaque 基础色和
roughness/metallic 常量；未指定时保持 v14。它不开启 MASK、normal map 或 RF shader extras。
导入过程依次执行 RFCHAR validator、MAT1 layout/constant 检查、PNG/JPEG→TTEX 转换与
尺寸/引用验证，任一步失败都保留旧产物。v15 单资源最多八个 ≤1024² 基础色纹理，必须是
clamp/linear/mip 的 UV0 配置；格式字段由[角色资产合同](../reference/character-assets.md#rfm2-v15-opaque-surface-扩展)拥有。
刚性附件继续走普通 GLB importer 和已有 RFM2 v2 PBR 常量，不传该选项。

默认安装到 `rasterfall/private-assets/models`；公开工业 prop 可显式传
`--output-root rasterfall/assets/models/props/industrial`。输出名称全部由 asset ID 推导：

```text
foo.rmesh
foo.textures/texture_000.ttex
foo.textures/texture_001.ttex
foo_lod1.rmesh                 # manifest 要求时
```

输出命名和 asset ID 规则见 [asset manifest 契约](../reference/asset-manifest.md)。临时目录建立在输出根内，以保证最终 rename 不跨文件
系统。转换、TTEX 验证、RMESH 布局/纹理引用和全部 LOD 验证成功后才安装；`--force` 替换时先把旧
asset family 移到同文件系统备份，安装失败会回滚。不要把 `build/`、`tmp/` 或 importer 临时目录提交。
Windows 安装时会对移入的 RMESH、LOD 和纹理目录恢复目标目录的 ACL 继承，避免隔离账号生成的临时文件
保留仅限该账号读取的权限；导入器必须能够修改自己生成文件的 ACL，否则安装失败并回滚。

## Manifest

[Asset manifest schema 1](../reference/asset-manifest.md) 拥有字段和验证规则。

## 新角色接入前的能力清点

```powershell
python tools/assets/rfchar_audit.py path/to/character.glb
python tools/assets/test_rfchar_audit.py
```

审计只读输出 JSON，记录源 SHA-256、mesh node 实际引用数量对应的顶点/索引总量、
权重数量、材质与当前 importer/Scene 的已知缺口。退出 0 只代表清点成功；非零表示输入无法清点。
它不是 GLB 合同验证器，缺口列表为空也不代表能渲染；仍需原生 glb-inspect contract、
runtime 与材质/变形验收。公开回归探针不依赖私有内容，也不代表完整变形小样。
新字段、能力矩阵及待决策项见[角色包与材质草案](../reference/character-package-v1.md)。

新材质元数据的独立校验与公开回归：

```powershell
python tools/assets/rfchar_material_contract.py path/to/character.glb
python tools/assets/test_rfchar_material_contract.py
```

此入口要求每个材质显式携带 `extras.rf_material`，输出展开默认值后的材质表及实际所需能力。
它仅核对材质元数据、纹理/图片引用声明和 sampler profile，不检查 UV、图片字节、骨架或
依赖路径，不安装资产。当前 v14/v15 importer 仍拒绝新 RF extras；不可将此 JSON 当成运行时包。

公开原创材质小样复用 canonical fixture 骨架，并加入 UV0、内嵌 RGBA PNG、OPAQUE/MASK、
单双面及 toon/outline/face_light 元数据：

```powershell
& 'E:/Blender 5.2/blender.exe' --background --factory-startup --python-exit-code 1 --python tools/blender/generate_rfchar_material_fixture.py -- --output tmp/rfchar-material/fixture.glb --preview tmp/rfchar-material/blender-reference.png
python tools/assets/rfchar_material_contract.py tmp/rfchar-material/fixture.glb
```

Blender 路径按本机安装选择；GLB 和 PNG 均留在 tmp，不加入资源目录或安装到实验场。
预览使用固定相机与灯光，只供几何、基础色与遮罩参考；自定义 toon、描边和脸部光照只写入
GLB extras，Blender 预览未实现这些 RF 着色语义，不作为游戏 GPU 或专用头发高光取舍的证据。

材质导入边界的公开原生回归（先生成 fixture，所有生成物保留在 tmp）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 asset-tools
New-Item -ItemType Directory -Force tmp/rfchar-p0 | Out-Null
& 'E:/Blender 5.2/blender.exe' --background --factory-startup --python-exit-code 1 --python tools/blender/generate_rfchar_fixture.py -- --output tmp/rfchar-p0/fixture.glb
python tools/assets/test_rfchar_materials.py tmp/rfchar-p0/fixture.glb --validator build-windows/glb-inspect.exe --runtime build-windows/rfchar-runtime-test.exe
```

Blender 路径按本机安装选择。该回归覆盖 OPAQUE alpha、单/双面、默认白材质、无效字段和
不支持能力的拒绝、失败保留旧产物及原生实例隔离。它不进行 shader 视觉验收。
RFCHAR importer 在调用原生 validator 前解析源、产物和工具的绝对路径，避免原生工具工作目录影响。

## 空间规范与 Blender 边界

- `static_prop`：Blender 源场景为真实米制、Z-up、-Y forward；标准化 GLB 为 Y-up、+Z forward，
  pivot 位于底面中心；导出前应用对象变换。
  importer 只接受标准化 GLB，不用末端展示缩放修补源资产空间。
- `character`：RFCHAR V1 GLB 由 `tools/assets/rfchar_import.py` 转 RFM2 v14，显式 surface 样板转 v15；PMX 仅为
  compatibility path。manifest `type=character` 同时接受严格 GLB 与历史 PMX。
- `weapon`：保留 grip/attachment 语义；刚性 GLB 以标准化 origin/manifest attachment 描述表达，
  带骨架武器走 PMX 路径。不要把握持补偿偷偷烘焙成角色专属末端偏移。

Blender 只负责 FBX、复杂场景和源坐标的预处理，按上述类型规范应用/保留变换后导出 GLB 或 PMX
转换链可消费的输入。`glb2rmesh`、`pmx2rmesh` 仍分别拥有各自格式解析；importer 不取代它们。

## GLB 转 RMESH 与纹理

首批十件工业/军事环境组件使用 `tools/blender/generate_rasterfall_props.py` 生成，
完整规格见[工业组件资产规格](../reference/industrial-props.md)，CLI 见[工业组件指南](industrial-props.md)。默认产物在 `tmp/`，
不自动加入公开资源、内嵌依赖或 Windows package。
其 V2 light upgrade 的几何、albedo、palette、预算与验收约束见
[环境资产艺术约束](../reference/environment-art.md)；艺术升级不改变下述导入和运行时契约。
整套 V2 Hybrid 的选择生成与安装命令见[工业组件指南](industrial-props.md)。每件局部标识内嵌 PNG
导入为 `<id>.textures/texture_000.ttex`；根 Makefile 递归资产依赖和 Windows package
递归复制包含这些纹理。manifest、registry、展示比例与地图均沿用既有定义。

```sh
tools/assets/import_asset.py prop.asset.json
```

`app/linux/glb2rmesh.c` 读取 GLB mesh primitive 的 POSITION、可选 NORMAL/TEXCOORD_0、基础 PBR 因子、
baseColorTexture 索引和常见三角形索引，合并 primitive 并修正索引基址。importer 从 GLB bufferView、
base64 data URI 或受源目录约束的相对 URI 提取所引用的 PNG/JPEG，再调用 `toyasset` 解码并转换为
TTEX；`glb2rmesh` 本身不实现图片解码。运行时仍只读 RMESH/TTEX，不解析 glTF JSON。
静态转换路径不导入 GLB 骨架和动画，不能替代 GLB 动画预览路径。
当前仅处理第一个 mesh，忽略 node transform；每个组件必须单独导出、应用变换。
位置默认按 232 量化，极小模型另有自动放大，不等于米到 512 RFU 的玩法换算；
米制环境源资产保持真实尺寸，展示绑定时按已有目标尺寸规则换算。

## 静态 prop asset registry

Temporary Campus Kit V0安装到公开`props/campus/`；IDs 24–35的零碰撞尺寸明确拒绝
隐式gameplay AABB。`tools/campus_kit_round.py --generate --capture --deterministic --audit`
复用importer与Visual CLI完成完整性、已有库存和独立校园组图；
见[校园套件资产与拼接规格](../reference/temporary-campus-kit-v0.md)；原始 audit 见[阶段记录](../archive/temporary-campus-kit-v0.md)。

建筑套件 `rf_arch_*` 同样安装到公开 industrial 目录并追加稳定 ID；两种 flat 材质、零纹理。
使用 `python3 tools/architecture_round.py --generate --capture --deterministic` 重现资产与两个隔离原型；
套件、源保留、端口和 Surface V1 规范见[建筑套件合同](../reference/architectural-environment-v1.md)；
生成与截图步骤见[建筑套件指南](architectural-environment-v1.md)。
脚本先构建转换器一次，再调用 importer `--no-build`，避免重复构建；不改 importer 格式。

`include/rasterfall_prop.h` / `src/rasterfall_prop.c` 保存静态组件的 presentation 资产 profile。
当前注册首批工业组件（`crate`、`barrier`、`short_wall`、`railing`、`lamp_post`、
`vent_unit`、`workbench`、`ammo_container`、`industrial_pillar`、`pipe_module`）及正式地图装配
新增的 `power_unit`、`gate_frame`、`control_cabinet`，每项包含稳定 asset ID、名称、RMESH 路径、默认
展示缩放和 RFU 碰撞尺寸，运行时产物位于 `rasterfall/assets/models/props/industrial/`。
对应 manifest 位于 `tools/assets/manifests/props/industrial/`，源 GLB/Blend 位于本地
`rasterfall/private-assets/source/props/industrial/`。profile 使用 `512 RFU/m ÷ 232 RMESH units/m` 的 milli-scale；该换算
不进入 RMESH 或地图 visual mesh，碰撞尺寸以 RFU 元数据供地图 parser 生成 gameplay primitive。

此 registry 提供查找、单位契约和默认 RFU 碰撞盒；地图 parser 负责实例化碰撞 primitive，renderer
负责模型加载。新增组件时应先在 profile 中分配不复用的 ID，并同步检查
`rasterfall_prop_asset_logic_test()`。

## PMX 转 RFM2/TTEX

```sh
tools/assets/import_asset.py character.asset.json
```

`app/linux/pmx2rmesh.c` 负责 PMX 网格、材质、骨骼、BDEF1/BDEF2 蒙皮及已支持 IK/grant metadata，
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

`tools/rmesh_lod.py` 简化索引后建立 old→new vertex remap，只保留实际被 index 引用的
position/normal/UV/edge 数据和对应 SKN1 BDEF1/BDEF2 记录，支持现有 RFM2 v2-v14 输出。
骨架、bone hierarchy、IK、SKN1 names/metadata、材质和 primitive 不重排；v14 CHR1 原样保留。
`--keep-unused-vertices` 仅供旧 index-only LOD 的诊断 A/B。LOD 与完整模型共享纹理；缺少
LOD 文件时运行时应回退完整模型。修改选择阈值或布局假设时同时检查 `rasterfall_render.c`。

Gameplay Hybrid LOD 不要求全身统一压缩比。`--region-profile <json>` 接受离线 schema 1 descriptor：
`high_bones` / `medium_bones` 通过实际 SKN1 bone name 解析区域，`joint_zones` 用一对骨骼和相邻
骨长比例定义可缩放关节邻域，primitive 列表补充 face/hair 等材质边界。受保护 vertex 使用更细
空间格；所有 hybrid merge key 均包含 dominant bone、完整 BDEF2 bone pair 和可配置 weight bucket，
避免跨 skin-weight discontinuity 选取代表点。Eula pilot 入口为：

```sh
make lod-eula-gameplay
```

profile 位于 `tools/assets/lod_profiles/eula_gameplay.json`，输出 `eula_lod3.rmesh`；数字 `_lod`
后缀是现有共享 `eula.textures/` 路径契约，不表示 runtime 需要新的 LOD 类型。正常 Eula world/展示
在 near/mid 距离档使用该 Hybrid，FAR（4096 RFU，约 8 米起）使用 compact `eula_lod2.rmesh`；
Hybrid 缺失时回退完整模型。该路径不接入 Maid，也不修改动画求值或 CPU skinning。

## GLB 与 VMD 检查

Windows 原生角色诊断先运行
`powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 asset-tools`。
该入口在 `build-windows/` 生成 `glb-inspect.exe`、`rfchar-runtime-test.exe` 和
`rf-anim-info.exe`，并保留既有静态资产与地图工具。runtime 诊断复用原生 resource 测试的
链接集合，需要与游戏构建相同的 SDL2 依赖，但运行这些 CLI 不打开游戏窗口。
Windows runtime 的相对读取路径以 exe 所在目录为基准；从仓库根诊断私有文件时传绝对路径。
`rfchar_import.py` 用 `--validator <绝对路径>/build-windows/glb-inspect.exe` 选择此 lane，
源 GLB 与输出 RFM2 也显式传绝对路径。诊断不安装资产或更新单人默认 renderer。

```sh
make app-glb-inspect app-vmd-inspect
build/glb-inspect animation.glb
build/glb-inspect animation.glb humanoid
build/glb-inspect animation.glb basis
build/glb-inspect character.glb contract
build/glb-inspect --self-test
build/vmd_inspect motion.vmd model.rmesh
```

`glb_inspect` 检查 node、skin、accessor、animation 和 humanoid/rest basis，不生成运行时模型。
`vmd_inspect` 检查骨骼名、映射、关键帧、IK 和运动诊断。游戏运行时的 VMD/GLB 诊断参数以
`build/rasterfall --help` 为准。

RFANIM 调试使用 `build/rf_anim_info` 查看 action、role track 与关键帧；游戏的 action preview、
pose debug、composition capture 和 runtime debug 可观察 finalized pose、stable socket、武器握点与
叠加结果。完整参数以 `build/rasterfall --help` 为准；这些诊断只观察姿态，不改变求值所有权。

## 离屏与性能回归

```sh
build/rasterfall --model-views model.rmesh tmp/model-views
build/rasterfall --model-material-regression model.rmesh tmp/material-regression
build/rasterfall --model-performance model.rmesh 5 8
build/rasterfall --actor-performance 30 5 8
build/rasterfall --character-performance model.rmesh 3 20 3 8
build/rasterfall --character-performance-suite 3 20 3 8
python3 tools/eula_animation_acceptance_sheet.py
```

模型视图和材质回归走游戏内相同的材质与光栅路径。并行度、优化或渲染路径修改后，应比较确定性
帧缓冲哈希和各阶段统计；`--model-performance` 的 `full` / `lighting_off` 直接消融统一 RMESH
form-lighting，Character Acceptance 与 `--visual-capture lighting-props` 提供固定 OFF/V1 画面。
其他诊断消融模式不代表默认画质。生成物放在 `tmp/` 或 `build/`，不提交。

## 角色观察组图

V2.1 与 Profession Visual System V1 的整轮入口为
`python3 tools/rf_profession_round.py --generate --capture --world --deterministic`。
脚本复用 Blender 生成器、六份职业 manifest、统一 importer、RFCHAR contract/runtime、
Character Lab 和 world CLI；新增 lineup 也由游戏 renderer 输出，Python 只拼接 PNG。
默认产物在 `tmp/rf-v21-professions/`，包括每份资产日志、individual lab/world PNG、
`lineup-front.png`、`lineup-three-quarter.png`、`lineup-side.png` 和原始七张 BMP。
`--deterministic` 需要已有本轮 `lineup/` 或同时指定 `--capture`，并与第二次绘制逐字节比较。
装备和 body 合并为完整 RFCHAR carrier；生成源在导出前 join mesh，使 primitive 数受材质数
约束，遵守既有 runtime 上限。这些资产仍为可选私有资源，不进入公开 embedded 清单；
现有 Windows package 会递归复制本机 `private-assets`，所以本地打包会携带已生成的模型，
本轮无需新增复制规则，也未执行打包或发布。

两个通用脚本负责调用离屏入口并将 BMP 拼成单张 PNG；渲染仍由
`build/rasterfall` 完成，脚本不生成 Lighting OFF/V1 对比图。角色组图为三行
`bind`、`rifle-idle`、`rifle-aim`，四列 `front`、`side`、`back`、`three-quarter`：

```sh
python3 tools/character_lab_sheet.py \
  --model rasterfall/private-assets/models/rf_humanoid_v2.rmesh \
  --output tmp/rf-humanoid-v2/humanoid-v2-lab.png
```

实景组图匹配仓库根目录的 `real.png` 构图，为 near/mid/far 三行和
old/idle/aim/motion 四列，使用正式地图、灯光、深度和 Character Test Strip：

```sh
python3 tools/character_world_sheet.py \
  --model rasterfall/private-assets/models/rf_humanoid_v2.rmesh \
  --output tmp/rf-humanoid-v2/character-world.png
```

默认会在输出文件旁的 `captures/` 保存原始 BMP；使用 `--capture-dir` 可跳过重新
渲染、直接拼接已有 capture。`--cell-width` 控制每个单元的输出宽度，默认 400px。
脚本只依赖 Python 标准库，读取 24/32-bit BMP 并写 RGB PNG。

RF Humanoid V2 的头部覆盖探索使用：

```sh
python3 tools/rf_humanoid_headgear_sheet.py \
  --output tmp/rf-headgear-v1/headgear-lab.png --world
```

它对 `bare`、`headset`、`patrol-cap`、`goggles`、`respirator`、`tactical-helmet`、
`engineering-helmet` 使用同一 Character Acceptance 相机、姿态和标准 AK，生成每个变体的
individual lab/world sheet，并生成跨变体 comparison。world 默认只捕获 bare、goggles、
respirator、tactical-helmet、engineering-helmet 五个代表样本；可用 `--world-variants` 调整。
这些变体仍是完整 RFCHAR carrier，模块几何绑定 `RF_HEAD`，不改变 importer 或 runtime 的
stable attachment 表。

刚性角色附件仍使用统一入口；manifest 的 `rigid_attachment` 类型要求 authored mount origin、
canonical character orientation 和 meter units，转换结果是无 SKN1/CHR1 的普通 RMESH：

```sh
blender --background --factory-startup --python tools/blender/generate_rasterfall_humanoid_v2.py -- \
  --output rasterfall/private-assets/source/attachments/rf_tactical_helmet.glb \
  --rigid-attachment tactical-helmet
python3 tools/assets/import_asset.py --force \
  tools/assets/manifests/attachments/rf_tactical_helmet.asset.json
build/rasterfall --rigid-attachment-acceptance rasterfall/private-assets/models tmp/rigid-attachment
```

## 单位与边界

玩法世界使用 RFU，`512 RFU = 1 m`。PMX、GLB、VMD 的局部单位只在 presentation/导入边界
换算，不能直接进入碰撞、AI 或网络规则。坐标系或 bind pose 异常应从转换器输出一路定位到加载、
姿态求值和渲染，不要用角色专属末端偏移掩盖通用资产错误。
