# Rasterfall Character Asset Contract V1

> 状态：当前
> 所有者：RFCHAR、附件与蒙皮资产合同

本文是所有新 Rasterfall 人形角色资产的第一入口。V1 冻结 Blender 到离线 importer 的输入门；
它不承诺任意 glTF 的兼容性，也不要求 runtime 直接读取 GLB。主线固定为：

```text
Blender source → Character GLB Contract V1 → offline importer/validator
               → RFM2（或后续 runtime character format）→ humanoid animation/rendering
```

新角色包、材质载体、能力矩阵与版本迁移设计见[角色包与材质 V1 草案](character-package-v1.md)。
草案尚未实现，不扩大本文 V1 的已支持范围。当前 RFCHAR converter 保存双面标志，
对 MASK 和未实现的 RF 材质元数据明确报错；显式 v15 MAT1 已贯通不透明基础色图、clamp/mip
采样与粗糙度/金属度常量，见下文表面扩展。完整角色包与动漫材质能力接入前仍须运行能力审计。

Blender 是离线创作与生成环境，GLB 是主要交换格式，RFM2 是当前运行时格式。runtime 不依赖
Blender；PMX/VMD 仅是兼容输入，不定义 canonical 名称、空间或附件。玩法只认识 actor、character、
animation 与稳定枚举，不认识 GLB node name。

## Infected carriers

Enemy Visual V2 的两套三类型 carrier 使用同一 RFCHAR V1 / RFM2 v14 契约。Humanoid 复用
V2 身体，Block 使用方块身体；源比例修改同时作用于 mesh、rest skeleton 和八个 attachment。
基础感染姿态按 stable roles 在独立 instance 中求值，不更改 character recipe 或 humanoid
contract。公开资源与完整生成/验收入口见 [enemy-visuals.md](enemy-visuals.md)。

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

持枪样板可选用 `RF_<L|R>_FINGER_<0|1|2|3|thumb>_<1|2>`，0–3 为食指至小指。
第一节直接挂同侧 HAND，第二节直接挂第一节；rest 仍为掌心朝下、手指向外的 canonical 空间。
共享 Humanoid 与 RF-C01 使用这些链，RFANIM composition 在持枪上身层后应用左右镜像的弯指，
扳机食指单独保留伸出姿态，拇指做对握。缺失链的旧资产保留原手型；不增加 humanoid role 或二进制格式。
同骨架衣物须随 body 一起导出完整 49 骨（原 29 骨加 20 根手指骨），不能混用旧 29 骨衣物。

### 空间与静止姿态

- Blender 源为米制（Unit Scale 1.0）、右手系、Z-up、-Y forward；GLB 按 glTF 标准导出为米制、
  Y-up、+Z forward。玩法单位为 `512 RFU = 1 m`，源资产不写 RFU；RFM2 局部存储单位由
  `position_scale` 定义，RFCHAR importer 默认 512，可显式选 8192/65536。高精度 Scene body
  在投影前换到 RFU，顶点、骨骼 rest 和 socket 平移必须使用同一局部单位，见[保真诊断](../guides/character-fidelity.md)。
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
格式无关 `rasterfall_animation_clip` 的规则仍见 [animation architecture](../architecture/animation-architecture.md)。

输入门与产物能力分开：RFM2 v14/v15 RFCHAR converter 只导入 OPAQUE，按其语义忽略
baseColorFactor alpha，写 byte 4=255；byte 5 为旧 toon index，不能写成 16 位 alpha 的高字节。
`doubleSided` 写入 byte 7 的 bit 0；无材质引用的 primitive 使用独立默认白材质，不借用材质 0。
MASK/BLEND 和 `extras.rf_material` 在写产物前以 `MATERIAL_CAPABILITY` 拒绝，直到新格式和消费者
实现，不能再静默丢掉 cutoff 或 RF 参数。无效颜色/布尔值报 `MATERIAL_FIELD`，无效索引报
`MATERIAL_INDEX`；验证失败保留已有输出。已导入的历史产物不会自动修正，需要重新构建。

### RFM2 v15 opaque surface 扩展

默认 importer 继续生成 v14；`--character-surface` 显式选择 v15。v15 完整保留原 header、
40-byte material、vertex/index、SKN1 与 CHR1 布局，只在 CHR1 之后追加唯一 MAT1 block，
不改骨架、权重或 attachment 表。runtime 同时可读历史 v14，不要求旧资源批量升级。

MAT1 的 32-byte little-endian 头为八个 `uint32`：magic=`0x3154414d`（`MAT1`）、
total bytes、version=1、material count、stride=16，以及三个 reserved=0。随后每材质一项
16-byte `{uint32 flags=1, float32 roughness, float32 metallic, uint32 reserved=0}`，顺序与
原 material 表一致；常量必须有限且在 `[0,1]`，material count、stride、尾块长度必须精确匹配。
旧 material byte 8 的 texture index 和 byte 7 的 double-sided 位继续生效。

flags=1 固定表示 opaque、sRGB 基础色、UV0、clamp、bilinear 和 mip 采样配置，不是任意
shader feature bitset。每资源最多八个基础色 texture slot（0–7），每图最大 1024×1024；
图像仍由统一 importer 提取成同目录 `.textures/*.ttex`，runtime 消费者准备 mip chain。
有贴图的 primitive 必须带有限且落在 `[0,1]` 的 UV0；GLB sampler 明确使用
wrapS/wrapT=CLAMP_TO_EDGE、mag=LINEAR、min=LINEAR_MIPMAP_LINEAR。
当前拒绝 normal/occlusion/emissive/metallicRoughness 贴图、材质扩展及新的 RF extras；
该窄扩展不等于[角色包与材质草案](character-package-v1.md)已经实现。

刚性 GLB 装备保留原 RFM2 v2 材质表中既有 metallic/roughness 的 u16 常量，不为其添加
SKN1/CHR1 或 MAT1。CPU/Scene 角色换装与 PBR 常量的读取按资产版本区分，不能把 v2 的
metallic 字节误读成后续版本的 alpha/toon 字段。

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

### Rigid Attachment Asset Contract V1

真正刚性的外置附件复用普通 GLB→RMESH，不引入附件专用二进制格式。manifest 使用
`type: rigid_attachment`，并固定 `attachment_space` 为 `origin=mount_origin`、
`orientation=canonical_character`、`units=meters`。因此 GLB/RMESH 只含附件 mesh/material/texture：
无 body、skin、host skeleton 或 CHR1；局部 `(0,0,0)` 是预期 mount origin，局部轴与本文 canonical
角色轴一致，大小是 asset-native metric presentation size。当前生成器的
`--rigid-attachment tactical-helmet|backpack` 分别复用 tactical helmet 与 Rifleman short-pack
geometry，产物 ID 为 `rf_tactical_helmet`、`rf_backpack`。

runtime recipe 是 `{stable host socket, shared model_resource, full rigid mount correction, flags}`。
mount correction 只校准“资产 authored origin/basis → socket contract”，其 translation 使用 host
model RFU、rotation 为 row-major 3×3、uniform scale 以 milli 表示；不得用于 body/bind pose、actor
world position或武器握持补偿。V1 assembly 只正式支持 HEAD/BACK 的被动 follower，武器仍属于
preliminary pose→weapon placement→hand targets→IK→final pose 链路。

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

runtime test 同时加载一份 immutable model resource 并创建两份 model instance，验证骨骼 pose/global
transform 存储不共享、A/B socket 随不同姿态独立变化、互相 finalize 不污染且 reset 恢复 bind。Character
Acceptance 也走同一 resource/instance 正式路径，并额外输出 `two-instance-isolation.bmp`（左 bind、右 aim）。

Importer 总是先运行 validator，再合并同一 skin 的全部 mesh/triangle primitive，写入 512 RFU/m
顶点、法线、UV、材质、SKN1 骨架/BDEF 与 CHR1 stable tables。reference fixture 包含完整 21-role
T-pose、三个 mesh node、两个 material、BDEF1/BDEF2、`WEAPON_R` 与可选 `BACK`。

RFM2 v14 在完整 SKN1 后追加 CHR1：32-byte header、21 个 role→bone `uint32`，以及每项 40-byte
attachment `{id,parent bone,local position RFU,local quaternion}`。v2-v13 继续读取；无 CHR1 的 PMX
仍使用名称 mapper。runtime API 是 `rasterfall_model_humanoid_bone()` 与
`rasterfall_model_character_attachment_transform()`。固定 `rfchar-test` pose 同时旋转右上臂、
右前臂和左上腿；model views 额外输出 `three-quarter.bmp`。

## Profession Modularization V1

六个 `rf_profession_*` 继续作为 legacy acceptance carrier。正式 modular 路径只加载一次
`rf_humanoid_v2` body resource，为六人建立独立 model instance，并由
`rasterfall_character_visual_recipe()` 解析 shirt/pants presentation palette 与稳定 gear resource ID。
recipe 和加载后的资源/instance 都不进入 `toy_game_actor` 或网络快照。

当前共享身体的衣袖和裤腿使用跨肘/膝的连续表面；源生成器只沿纵向平均衣物法线，保留径向
低模折面与装备硬边。该源几何修正沿用同一 RFCHAR/RFM2 输入门，不改变 stable roles、sockets、
inverse bind、身高和材质合同，也不为敌方创建身体分叉。局部重建与同镜头验收见
[美术验收指南](../guides/character-art-acceptance.md#共享身体连续关节表面)。

生成器的 `--rigid-attachment=<profession>-<slot>` 直接复用 carrier builder 已验收几何，按 HEAD、
CHEST、BACK、HIP_L、HIP_R socket authored origin 重定位后导出无 body、skin、skeleton、CHR1 的
metric rigid GLB/RMESH。职业产物为每职业 HEAD/CHEST/BACK，另有 Engineer HIP_L 和 Heavy
HIP_L/HIP_R；普通/精英枪手各增加 HEAD/CHEST/BACK，另有共享护目镜与左右大腿外裤模块。
Breacher 的前侧 lower armor 与 CHEST 壳构成同一刚体；
它没有伪造新的 HIPS socket。

`--profession-lineup` 输出 modular 六职业 front/three-quarter near/mid/far、side mid，并输出每职业
legacy carrier（左）/modular（右）A/B。该入口同时执行 CHEST/HIP_L/HIP_R bind、rifle idle、
rifle aim、turned 数值回归并报告 body 实际加载份数。`tools/rf_profession_round.py --generate`
生成并经统一 rigid importer 验证全部 gear；`--deterministic` 比较七张 lineup 的逐字节结果。
生成器中的 palette 赋值必须同时更新 Blender `diffuse_color` 与 Principled BSDF `Base Color`：前者只供
viewport，后者才是 glTF 导出的事实来源。`--generate` 会在导入前核对 GLB 中每个职业的
`RF_Headgear` / `RF_HeadgearLight`，并在导入后核对 carrier 与 21 个 rigid gear 的 RFM2 材质色；
这些门禁用于区分资产导出丢色与 raster command 消费问题。

### 可组装轮廓装备

`tools/blender/rf_humanoid_equipment.py` 拥有独立装备几何，body 生成器只传入已冻结的 canonical
空间和材质。胸甲、弹匣袋、肩带、腰封、头盔壳、导轨、耳罩、背包与压缩带均为实几何，沿用 opaque
材质与普通 rigid importer。战术头盔不含镜片；`rf_gear_ballistic_goggles` 可与其独立组合。
`rf_gear_cargo_thigh_l/r` 是可拆大腿外裤与立体侧袋，分别跟随 `HIP_L/R`。外裤上层止于膝盖上方，
不跨关节；它们继续服务原有职业。Heavy 另外装配下述独立跨关节蒙皮衣物。

recipe 最多含 `RASTERFALL_CHARACTER_RECIPE_ATTACHMENTS` 个独立 follower（当前为八个），同 socket
可以挂多个资源。CPU cache、Scene 冻结载荷和 native mesh 列表都使用该容量；不能按 socket 去重，
否则头盔/镜片或侧箱/裤层会丢失。已有稳定 gear ID 不变，新 ID 追加。换装仍只属于 presentation，
不进入权威 actor、命中体或网络协议。RFCHAR 新角色可复用相同 socket/recipe 边界；各体型仍需在
源空间制作与验收自己的版型，不能以现有 Humanoid 模块自动适配任意动漫身体。

`--gpu-scene-pose-test` 的装备回归填满 recipe，检查重复 socket 的独立记录、左右大腿随 WALK、
CPU/Scene transform 相同、查询不修改最终姿态及超容量拒绝。完整 carrier 也组装同一共享模块，
用于 legacy/modular A/B；正常玩法只保留共享身体和独立 gear。

### 可换蒙皮衣物样板

`rf_clothing_field_jacket` 与 `rf_clothing_combat_trousers` 是独立 RFCHAR skin resource，
由 `tools/blender/generate_rasterfall_clothing.py` 基于原创 Humanoid 版型生成，包含真实衣服厚度、
衣摆、领口、上臂袋、跨膝补强与裤侧袋。生成器沿用身体含手指链的完整 49 骨以及其两骨权重，不新增
cloth 骨、动画或刚体跨膝补偿。opaque UV0 基础色使用 `rf_tactical_textures.py` 生成的原创
256² ripstop 织物 PNG，保留原 baseColorFactor；导出器显式修正 factor，防止 Blender 节点导出丢色。

本体 `rf_humanoid_surface.py` 只增加 UV0 和三张原创基础色图：面部/手部与短发/靴面各 512²，
shirt/pants 共用 256² 中性织物。五材质顺序仍是 pants/shirt/skin/hair/boots，颜色 factor、
几何、法线、蒙皮权重与骨架不变；纹理不烘焙职业染色或方向光，避免 CPU/Scene palette override
后二次染色。该样板使用 opaque PNG、clamp、bilinear 和 mip，面部细节不进入 shader 特例。

`RASTERFALL_CHARACTER_RECIPE_CLOTHING` 限制每个 recipe 最多两件 skin follower；
`rasterfall_model_shared_skin_compatible()` 必须验证骨名、顺序、父子、absolute rest、grant、
position scale 与 humanoid role 映射一致。衣物消费 body finalized palette，不能重新求值
IK 或拥有独立动作时钟。CPU scoped palette 与 Scene 同一 pose payload 使用相同 bind-normal 策略。

覆盖区域是显式版型合同：`TORSO_ARMS` 对应 Humanoid body 的 shirt 材质 1，`HIPS_LEGS`
对应 pants 材质 0。producer 冻结 mask，消费者只对本次 body primitive 提交生效，不隐藏手、
脸、头发、靴子，也不修改共享 resource。Heavy recipe 移除原 rigid 大腿裤层以免叠穿，保留
六件 rigid 装备。换装不改变碰撞、护甲数值或网络快照；不自动适配 RF-C01，不包含布料模拟。

`--gpu-scene-pose-test` 验证共享 palette 的只读重放、骨架/单位不兼容拒绝、覆盖映射、容量错误
拒绝及左右膝两骨顶点随 WALK 运动。真实衣摆与护甲穿插仍须用 native 多角度动作截图核查。
