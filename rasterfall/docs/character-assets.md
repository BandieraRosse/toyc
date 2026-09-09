# Rasterfall Character Asset Contract V1 / RF Humanoid V1

> 文档更新：2026-09-09
> 源码核对基线：工作区（RFM2 v14 / SKN1 + CHR1、正式 RFCHAR importer、runtime stable API 与 Blender reference fixture）

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
resource/instance 重构。正式 AI 低模制作前还需要角色造型/材质预算、Blender exporter 版本与依赖
锁定、动画 clip milestone，以及至少一份非 fixture 的 art acceptance asset。PMX mapper 与静态
`glb2rmesh` 继续作为兼容路径，但不定义新角色契约。
