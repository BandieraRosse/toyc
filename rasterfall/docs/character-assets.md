# Rasterfall Character Asset Contract V1 / RF Humanoid V1

> 文档更新：2026-09-10
> 源码核对基线：工作区（RF Humanoid V2 final convergence、CHR1 bind 基底烘焙与双手 socket 握持门禁；RFCHAR V1 / RFM2 v14 不变）

本文是所有新 Rasterfall 人形角色资产的第一入口。V1 冻结 Blender 到离线 importer 的输入门；
它不承诺任意 glTF 的兼容性，也不要求 runtime 直接读取 GLB。主线固定为：

```text
Blender source → Character GLB Contract V1 → offline importer/validator
               → RFM2（或后续 runtime character format）→ humanoid animation/rendering
```

Blender 是离线创作与生成环境，GLB 是主要交换格式，RFM2 是当前运行时格式。runtime 不依赖
Blender；PMX/VMD 仅是兼容输入，不定义 canonical 名称、空间或附件。玩法只认识 actor、character、
animation 与稳定枚举，不认识 GLB node name。

## RF Humanoid V1

GLB joint 名必须是下表的精确 ASCII 名。角色 importer 直接映射这些名字；旧 PMX/MMD、Mixamo、
Quaternius 等名字由各自 compatibility mapper 处理，不得加入本表别名。

| role / GLB joint | 必需 | 直接父 role |
| --- | --- | --- |
| `RF_ROOT` | 是 | 无 |
| `RF_HIPS` | 是 | `RF_ROOT` |
| `RF_SPINE` | 是 | `RF_HIPS` |
| `RF_CHEST` | 是 | `RF_SPINE` |
| `RF_UPPER_CHEST` | 否 | `RF_CHEST` |
| `RF_NECK` | 是 | `RF_UPPER_CHEST`（存在时），否则 `RF_CHEST` |
| `RF_HEAD` | 是 | `RF_NECK` |
| `RF_L_SHOULDER` / `RF_R_SHOULDER` | 是 | `RF_UPPER_CHEST`（存在时），否则 `RF_CHEST` |
| `RF_L_UPPER_ARM` / `RF_R_UPPER_ARM` | 是 | 同侧 shoulder |
| `RF_L_FOREARM` / `RF_R_FOREARM` | 是 | 同侧 upper arm |
| `RF_L_HAND` / `RF_R_HAND` | 是 | 同侧 forearm |
| `RF_L_UPPER_LEG` / `RF_R_UPPER_LEG` | 是 | `RF_HIPS` |
| `RF_L_LOWER_LEG` / `RF_R_LOWER_LEG` | 是 | 同侧 upper leg |
| `RF_L_FOOT` / `RF_R_FOOT` | 是 | 同侧 lower leg |

这 21 个 role 与现有 `rasterfall_humanoid_bone` 一一对应；V1 不新增第二套人形抽象。额外 twist、
toe、finger、face、hair 和 cloth bones 可存在，但不是 humanoid role，不得插入上表要求的直接父子链。

### 空间与静止姿态

- Blender 源为米制（Unit Scale 1.0）、右手系、Z-up、-Y forward；GLB 按 glTF 标准导出为米制、
  Y-up、+Z forward。Rasterfall importer 在离线边界换算到 `512 RFU = 1 m`，源资产不写 RFU。
- canonical rest pose 为直立 T-pose：头顶 +Y，面朝 +Z；左右从角色自身视角定义，左侧位于 +X，
  右侧位于 -X。手臂水平展开，掌心朝 -Y，手指指向外侧；双腿伸直，脚尖朝 +Z。
- `RF_ROOT` 位于两脚接触平面的中心 `(0,0,0)`，无父节点；`RF_HIPS` 位于骨盆中心。mesh 最低
  鞋底/脚底应在 `Y=0`，不得靠运行时展示 offset 修正地面。
- canonical joint 的 head→主要 child 方向定义 bone 的局部 +Y；局部 +Z 尽量朝角色 forward，
  局部 +X 由右手系确定。左右骨骼的位置关于 X=0 镜像，orientation 也按空间反射后重新建立右手
  基，而不是复制同一个 quaternion。
- canonical joints 的 rest local scale 必须 `(1,1,1)`，禁止 shear、负 scale、非均匀 scale；
  动画也不得写 scale channel。零长度阈值为 `0.001 m`；`RF_ROOT` 不参与长度检查。
- Blender Armature object 与所有 skinned Mesh object 必须应用 location/rotation/scale，导出 node 的
  object-level TRS 为 identity。骨骼 rest local translation/rotation保留；不能 Apply Pose as Rest 后让
  mesh 与 inverse bind matrices 失配。一个资产只包含一个角色，原点、地面和朝向均在角色空间表达。

## Character GLB Contract V1

只接受 glTF 2.0 binary `.glb`。一个资产允许一个或多个 mesh node；每个 mesh 可有多个 triangle
primitive。所有 skinned mesh node 必须引用同一个 skin，skin 必须以 `RF_ROOT` 为 `skeleton`，其
`joints` 包含全部 RF Humanoid joints 及确有顶点或附件用途的辅助骨骼。多个角色、多个 skin、多个
armature、未绑定 mesh 与同一 node 同时承担多个角色均拒绝。

每个 primitive 必须有：

- `POSITION`（FLOAT VEC3）、`NORMAL`（FLOAT VEC3）、`JOINTS_0`（unsigned byte/short VEC4）和
  `WEIGHTS_0`（FLOAT 或 normalized unsigned byte/short VEC4），且四者 count 相同；
- 显式三角形 `indices`（unsigned byte/short/int SCALAR），`mode` 缺省或为 TRIANGLES；
- 可选 `TEXCOORD_0`。V1 不接受第二套 joints/weights、morph target、Draco/meshopt、sparse accessor、
  line/point primitive、顶点色驱动材质或运行时 subdivision。

skin 必须提供 FLOAT MAT4 `inverseBindMatrices`，数量与 joints 相同。其定义遵循 glTF：bind pose 下
`joint_global * inverse_bind` 把 mesh bind-space 顶点保持不变；validator 以该关系检查 bind 一致性。
node 可用 TRS 或 matrix，但 canonical skeleton 和 skinned mesh 的 object-level 变换必须满足上一节；
importer 会完整求值 node hierarchy，绝不沿用旧 `glb2rmesh` 的“只取第一个 mesh、忽略 node transform”。

材质限定为 metallic-roughness 的 `baseColorFactor` 与一个 `baseColorTexture`（PNG/JPEG，UV0）；alpha
第一版仅允许 OPAQUE 或 MASK。不接受 KHR 材质扩展、运行时 shader graph、嵌套外部 URI 或依赖
Blender scene/camera/light。动画可与角色同包，但 skeletal importer 第一阶段可明确忽略；动画进入
格式无关 `rasterfall_animation_clip` 的规则仍见 `animation-architecture.md`。

## Attachment Contract V1

稳定 ID 为 `WEAPON_R`、`WEAPON_L`、`FOREGRIP`、`BACK`、`CHEST`、`HEAD`、`HIP_L`、`HIP_R`，
对应 `rasterfall_character_attachment`。`WEAPON_R` 是 V1 角色必需 attachment，其余可选。

Blender 中用 Armature 下的非 deform bone 表达，导出 joint/node 精确命名为
`RF_ATTACH_<ID>`；禁止用 loose Empty 作为最终契约，因为 Empty 可能脱离 skin hierarchy。attachment
bone 不参与蒙皮，直接父骨必须分别为：右手、左手、左手（FOREGRIP）、胸、胸、头、左上腿、
右上腿。`BACK`/`CHEST` 在有 upper chest 时也仍直接挂 `RF_CHEST`，以保持版本稳定。

importer 将其烘焙为 `{stable attachment id, parent humanoid role/bone index, local translation,
local rotation}`；scale 必须为 identity，不进入 runtime 记录。之后 gameplay/render 只能按稳定 ID
查询，不能使用 Blender 名称或 `find_bone("右手首")`。同一 ID 缺失（仅必需项）、重复、父骨错误或
参与 vertex weights 均为验证错误。

## Skinning Contract V1

- 每顶点最多两个非零 influence，以对齐 RFM2/SKN1 的 BDEF1/BDEF2。Blender 导出前须裁剪并重新
  归一化；importer 不静默丢弃第三、第四权重。
- 有效权重有限、非负，非零和须在 `1 ± 0.001`；近零分量可在量化前清零并再次归一化。
- joint 是 skin `joints` 数组的局部索引，必须有效；零权重槽的 joint 值忽略。无有效权重 vertex、
  attachment bone influence、无效 joint 都是硬错误，不自动绑 root。
- rest/bind 是 GLB 默认 node TRS 与 inverse bind matrices 共同定义的姿态。导入后的 RFM2 绝对 rest
  位置和 BDEF 记录必须复现该姿态；展示补偿不属于 bind pose。
- skin/joint/mesh node 禁止非均匀、负或动画 scale；V1 只允许一个 skin。刚性配件要么并入同一
  skinned mesh 并使用一个骨骼权重，要么作为单独 weapon/prop 资产走 attachment，不在角色 GLB 中
  留未绑定 mesh。

## 验证门与诊断格式

```sh
make app-glb-inspect
build/glb-inspect character.glb contract
build/glb-inspect --self-test
```

`contract` 模式是 importer 前置门，成功返回 0，任何 contract error 返回非零。诊断每行使用
`RFCHAR V1 ERROR <CODE>: ...` 或 `RFCHAR V1 WARN <CODE>: ...`，末行给出 error/warning 计数，便于
人和 agent 稳定解析。最小实现覆盖 canonical role/父链/重复与长度、单 skin 与 inverse bind、
primitive 属性、joint/weight/count/归一化、attachment 及 TRS/scale；未来 importer 增加能力时必须先
更新本契约、validator fixture 和诊断，再扩 runtime。

## Character Importer V1 与 reference fixture

正式主链已经可执行：

```sh
blender --background --factory-startup --python tools/blender/generate_rfchar_fixture.py -- --output tmp/rfchar_fixture.glb
build/glb-inspect tmp/rfchar_fixture.glb contract
python3 tools/assets/rfchar_import.py tmp/rfchar_fixture.glb tmp/rfchar_fixture.rmesh
build/rfchar_runtime_test tmp/rfchar_fixture.rmesh
build/rasterfall --model-pose-views tmp/rfchar_fixture.rmesh tmp/rfchar-pose rfchar-test
```

Importer 总是先运行 validator，再合并同一 skin 的全部 mesh/triangle primitive，写入 512 RFU/m
顶点、法线、UV、材质、SKN1 骨架/BDEF 与 CHR1 stable tables。reference fixture 包含完整 21-role
T-pose、三个 mesh node、两个 material、BDEF1/BDEF2、`WEAPON_R` 与可选 `BACK`。

RFM2 v14 在完整 SKN1 后追加 CHR1：32-byte header、21 个 role→bone `uint32`，以及每项 40-byte
attachment `{id,parent bone,local position RFU,local quaternion}`。v2-v13 继续读取；无 CHR1 的 PMX
仍使用名称 mapper。runtime API 是 `rasterfall_model_humanoid_bone()` 与
`rasterfall_model_character_attachment_transform()`。固定 `rfchar-test` pose 同时旋转右上臂、
右前臂和左上腿；model views 额外输出 `three-quarter.bmp`。

## 当前缺口与下一阶段边界

当前闭环不导入 animation clip，不实现通用 glTF、IK 自动生成、玩法角色注册或 model
resource/instance 重构。PMX mapper 与静态 `glb2rmesh` 继续作为兼容路径，但不定义新角色契约。

## RF Humanoid Art Acceptance Character V1.1

第一份非 fixture 的正式候选由 `tools/blender/generate_rasterfall_humanoid.py` 参数化生成，
manifest 为 `tools/assets/manifests/characters/rf_humanoid_acceptance.asset.json`。它沿用本页
冻结的 21-role skeleton 与 8 stable attachments，不修改 RFCHAR contract；几何按 pelvis/waist/chest、
独立 neck/skull/hair shell、tapered upper/lower limbs、放大的 hands/feet 拆分，使用 5 个大色块
材质。源资产与导入产物在 `rasterfall/private-assets/`（可选本地资产）中生成，不由 runtime 直接读取 GLB。

可重复生成和验收：

```sh
mkdir -p rasterfall/private-assets/source/characters
blender --background --factory-startup --python tools/blender/generate_rasterfall_humanoid.py -- \
  --output rasterfall/private-assets/source/characters/rf_humanoid_acceptance.glb
build/glb-inspect rasterfall/private-assets/source/characters/rf_humanoid_acceptance.glb contract
tools/assets/import_asset.py --force tools/assets/manifests/characters/rf_humanoid_acceptance.asset.json
build/rfchar_runtime_test rasterfall/private-assets/models/rf_humanoid_acceptance.rmesh
build/rasterfall --model-pose-views rasterfall/private-assets/models/rf_humanoid_acceptance.rmesh tmp/rf-humanoid-acceptance/bind bind
build/rasterfall --model-pose-views rasterfall/private-assets/models/rf_humanoid_acceptance.rmesh tmp/rf-humanoid-acceptance/posed rfchar-test
```

当前 acceptance 基线为 1,619 vertices / 1,011 triangles / 18 mesh nodes / 5 materials；
导入后为 29 bones（21 humanoid + 8 attachments），其中 1,499 个顶点为 BDEF1、120 个为
BDEF2。`--character-acceptance` 使用稳定 CHEST/WEAPON_R/WEAPON_L/FOREGRIP 接入现有 AK，
输出 bind、rifle-idle、rifle-aim 的 front/side/back/three-quarter，并输出 near/mid/far 的
固定 front A/B sheet。它是离屏验收入口，不是新的 runtime character path：

```sh
build/rasterfall --character-acceptance rasterfall/private-assets/models/rf_humanoid_acceptance.rmesh tmp/rf-humanoid-v11
```

步枪动作通过稳定 humanoid role composition 旋转肩、大臂、前臂和双手，再由现有 rifle hand
solver 求解握持；capture 根据姿态 bounds 使用稳定 margin framing。当前已验证 rifle motion、
RFM2 v14 runtime load 与 CPU skinning，仍需在真实 gameplay 镜头中继续观察远距离附件空间。

## RF Humanoid Art Acceptance Character V2

V2 是利用同一 RFCHAR V1 contract 对 V1.1 原型做的大尺度 body rebuild。V1.1 的 GLB/RMESH
继续保留作并排对照；V2 不新增骨骼角色抽象，也不改变 `RFCHAR`、RFM2 v14、stable role 或
attachment ID。新的源生成器为 `tools/blender/generate_rasterfall_humanoid_v2.py`，manifest
为 `tools/assets/manifests/characters/rf_humanoid_v2.asset.json`。

造型策略是长腿、较短的视觉躯干、明确的 pelvis second volume、连续 ribcage → waist → pelvis
收缩、独立 deltoid/elbow/knee/calf 轮廓，以及 jaw/cheek/temple/crown 分层的头部和独立 hair
silhouette mass。基础外观只使用 skin、hair、shirt、pants、boots 五个大色块；没有用战术附件
掩盖人体比例。所有 deform 顶点保持最多两项影响，按现有 BDEF1/BDEF2 输入门导出。

可重复生成、导入和验收：

```sh
mkdir -p rasterfall/private-assets/source/characters
blender --background --factory-startup --python tools/blender/generate_rasterfall_humanoid_v2.py -- \
  --output rasterfall/private-assets/source/characters/rf_humanoid_v2.glb
build/glb-inspect rasterfall/private-assets/source/characters/rf_humanoid_v2.glb contract
python3 tools/assets/import_asset.py --force tools/assets/manifests/characters/rf_humanoid_v2.asset.json
build/rfchar_runtime_test rasterfall/private-assets/models/rf_humanoid_v2.rmesh
build/rasterfall --character-acceptance \
  rasterfall/private-assets/models/rf_humanoid_v2.rmesh tmp/rf-humanoid-v2/acceptance
build/rasterfall --character-world-capture tmp/rf-world-v2 \
  --character-world-model rasterfall/private-assets/models/rf_humanoid_v2.rmesh
```

Final convergence 保留参数化生成源和五大材质色块。骨盆最大半宽 0.275m、腰半宽
0.235m；肩峰采用更小且偏向上臂权重的渐缩体，收窄胸侧以留出腋下空间。大腿根收窄，
左右 knee 网格位于各自的 X=±0.15m，calf 峰值上移、脚踝收窄、鞋底共面于地面；hair crown
闭合并与前缘相接，避免原先开口露出头皮。手部 socket 位于实际掌部，保持现有 stable IDs。
源空间从鞋底到 crown 约 2.080m；bind pose 中
hip 约在总高 42%、shoulder 约在 72%，视觉上把更多高度交给腿和小腿，而不是 V1.1 的短直筒腿。

`--character-acceptance` 继续输出 bind、rifle-idle、rifle-aim 的 front/side/back/three-quarter
及 near/mid/far A/B；`--character-world-model` 只替换开发者区 Character Test Strip 的 body，
仍使用正式地图、真实 world renderer、标准 AK 和深度路径。开发者区默认加载 V2；相机从展示带
斜正面观察，避开工业 prop 与边界墙对中远景的遮挡，并补充每个角色独立的三档距离截图。

观察模型时应优先采用组图方式，减少逐张打开和切换截图的开销。使用
`tools/character_lab_sheet.py` 将当前 Humanoid V2 的三种姿态和四个角度拼成一张 lab sheet；
需要检查真实地图中的距离、场景遮挡或角色姿态时，使用 `tools/character_world_sheet.py`
生成类似 `real.png` 的 near/mid/far × old/idle/aim/motion 实景合集。两个脚本都会先调用当前
离屏 CLI，再拼接正常光照结果；单张 BMP 仅作为原始证据保留，不作为主要观察交付物。

附件 importer 必须将 GLB 的 parent-local TRS 烘焙到 SKN1 的 identity-rest 基底：position 为
socket 与 parent 的全局 bind 位置差，rotation 为 socket 的全局 bind rotation。不能直接复制
GLB local TRS；`test_rfchar_pipeline.py` 对导入附件与 GLB bind 变换做交叉检查。

RFCHAR 持枪使用 CHEST 提供稳定枪架、WEAPON_R 和 FOREGRIP（缺失时 WEAPON_L）提供双手接触。
`rifle_solve_hands()` 按 stable role 获取骨骼，独立求解两臂并迭代扣除 socket 到 wrist 的偏移；
旧 PMX 分支继续使用原有校准。验收的 `visual_rf_calibration()` 区分低持枪 idle 与平持 aim，
不套用 Maid 体型姿态；`visual_rf_check_grips()` 超过 4 RFU 误差即返回失败。
Character Lab 的 front/three-quarter 对应 canonical +Z 正面，所有方向共享相同姿态与 framing。

这套 V2 是后续 AI/NPC 身体与 headgear/职业附件扩展的基线候选。完整运行时资产仍为本地可选
资源，生成器与 manifest 才是可复现源；本轮不自动替换全部 gameplay actor。最终冻结判断和
具体截图观察记录见 `archive/rf-humanoid-v2-final-convergence.md`，导航不记录阶段测试数量。
