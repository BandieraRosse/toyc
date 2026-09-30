# 角色包与材质 V1 合同草案

> 状态：提案；P0 第一轮决策，尚未由 importer/runtime 实现
> 所有者：角色包构建、RF 材质语义与能力协商

本文定义新体系的生产者和消费者边界，不改变当前 [RFCHAR V1](character-assets.md)、
[manifest schema 1](asset-manifest.md) 或已支持的二进制格式。执行进度只在
[活动计划](../plans/private-anime-character-gpu.md)维护。下面的版本和字段用于共同实现，
不得把这些字段交给旧 importer 后声称新能力已经保留。

## 身份和唯一事实来源

| 身份 | 生命周期与所有者 |
| --- | --- |
| gameplay character ID | Game/session 的稳定枚举；不携带文件路径、包修订或 GPU handle |
| presentation recipe ID | 只读表现目录选择 body、动作集、材质绑定和附件；实验场仅持有此身份 |
| asset ID | 发布后不复用的小写 ASCII ID，沿用 schema 1 命名规则；不等于数组下标 |
| revision | 包内容的 SHA-256；同 asset ID 可有多修订，运行时明确锁定一份 |
| GPU generation | 当前 device 上一次成功创建的资源代数；不持久化到包，不等于内容哈希 |

主链为 Blender 源 → GLB 与包 manifest → 离线校验/构建 → 不可变运行时包 → 目录与实例。
runtime 不解析 Blender 节点图或 GLB。禁止通过角色名称、材质排列或骨骼编号补足丢失的语义。

包 manifest 采用现有统一入口的 **schema 2、type=character** 扩展，不另建 RF-C01 加载器。
schema 1 保持原义，旧工具继续拒绝 schema 2。字段草案如下；除明确可选项外均必填，未知字段拒绝：

| 字段 | 含义 |
| --- | --- |
| `schema`, `type`, `id`, `source` | `2`、`character`、稳定包 ID、主 GLB 相对路径 |
| `requires` | 非空、无重复的版本化能力名列表；未知必需能力失败 |
| `lods` | `{level, source}` 数组；level 为唯一正整数，主 source 是 level 0；正式内容需要近中远三档 |
| `actions` | 稳定动作语义到 `{source, layer}` 的映射；source 为 RFANIM，layer 沿用 lower/upper/additive |
| `provenance` | `{source_record, license, distribution}`；记录文件也是依赖，distribution 为 public/private |
| `expressions` | 可选的表情源描述；仅在 morph 合同另行冻结并实现后接受，当前必须缺省 |

所有路径相对 manifest，只允许包源根内的普通相对路径，解析符号链接后仍须在根内；
不允许绝对路径、网络 URI、隐式“最新候选”或从工作目录找资源。依赖必须存在且不重复冲突。
作者 manifest 不手填 revision、输出路径、索引计数或 GPU generation。

构建器输出 lock/report：schema、asset ID、producer 版本、目标格式版本、完整依赖的相对路径与
SHA-256、产物路径与 SHA-256、实际使用能力、审计诊断。revision 对这些规范化字段计算：UTF-8 JSON、
键排序、无多余空白，依赖和产物按路径排序，排除 revision 自身、绝对路径、时间戳和日志。
工具版本或任何传递依赖变化都使构建失效。同字节内容重新构建必须得到相同 revision。

模型/全部 LOD、纹理、动作、lock/report 作为一组在同文件系统 staging 验证后安装；失败保留上一修订。
公开包拒绝依赖 private 记录，私有内容只在 private-assets 和显式本地 package 中分发。
当前 Windows package 会复制本地私有目录，不能把该产物直接视为公开包。

## 材质数据载体

选定 **GLB `materials[i].extras.rf_material`** 作为 RF 自定义材质元数据的唯一载体。
旁车 manifest 不再重复材质参数；Blender exporter 负责从作者属性导出该对象。
每个材质必填 `version=1` 和 `id`，ID 在包内唯一，遵循 asset ID 字符规则；
跨 LOD 的同一语义材质保持同 ID，不以显示色或 primitive 顺序推测等价。

glTF 原生的 baseColorFactor、baseColorTexture、alphaMode、alphaCutoff、doubleSided 仍由
对应标准字段唯一拥有。RF extras 不允许覆盖它们。只使用 UV0；有纹理时必须显式存在 UV0，
首版 UV 有限且在 [0,1] 内，越界拒绝，不能沿用当前 q16 的静默夹断。
图片首版 PNG/JPEG；MASK 必须保留 alpha，JPEG 不能承担遮罩。

| 字段/语义 | 默认值、范围和坐标 |
| --- | --- |
| baseColorFactor | `[1,1,1,1]`，各分量有限且在 [0,1]，RGB 为线性因子 |
| baseColorTexture | 可选；RGB 按 sRGB 解码，alpha 线性；缺省为白色，声明后缺文件是错误 |
| alphaMode / alphaCutoff | OPAQUE / 0.5；cutoff 在 [0,1]，MASK 以 texture alpha × factor alpha 比较，小于 cutoff 丢弃 |
| doubleSided | false；true 不剔除背面，并将背面着色法线朝向可见侧 |
| `toon` | 可选对象：`threshold=0.5` [0,1]、`softness=0.05` (0,1]、`shade_color=[0.65,0.65,0.65]` 线性 [0,1] RGB |
| `outline` | 可选对象：`width_px=0` [0,8]、`color=[0,0,0]` 线性 [0,1] RGB；0 禁用，以输出像素为单位 |
| `face_light` | 可选对象：`role=RF_HEAD`、`normal=[0,0,1]` 非零单位向量、`strength=0` [0,1]；向量位于 canonical role 局部空间 |
| `hair_highlight` | 首版只能缺省；专用语义/贴图仍待材质小样决定，不映射 PMX sphere 字段 |

toon 缺省使用基础光照；启用时以 `n=max(dot(N,L),0)`，
`t=smoothstep(threshold-softness/2, threshold+softness/2,n)`，
`base_rgb * mix(shade_color,vec3(1),t)` 定义首版直接光分档。
face_light 的向量须随 finalized role 姿态变换，按 strength 与几何法线混合后归一化；
禁止使用固定世界朝向或随镜头改脸的特判。灯光和阴影的场景边界仍由 GPU 架构定义。

描边按材质选择背面扩张轮廓通道，与 WORLD 共享深度；不能跨材质补画全部内部边。
像素宽度不随角色距离放大，近裁剪、MASK 和材质交界的联合行为必须在公开小样验证。
这里不承诺已消除全部描边交界伪影。

首版纹理使用 clamp-to-edge、双线性放大与三线性 mip 缩小；GLB 显式 sampler 必须匹配该 profile，
缺省 sampler 由 RF profile 固定，其他 sampler 请求明确拒绝。mip 在解码后的线性 RGB 中生成，
alpha 保持线性；MASK mip 需要检查覆盖率与远景闪烁。GPU 光照在线性空间，输出仅编码一次 sRGB。
TTEX 需要版本化表达 RGBA、颜色空间与完整 mip 链，当前 RGB 最近邻路径不算满足此合同。
BLEND、金属度/粗糙度贴图、法线贴图、任意节点图不属于首版；不能静默当作基础色处理。

RF 基础光照/toon 替代 glTF metallic-roughness 光照；`metallicFactor` 与
`roughnessFactor` 只校验有限 [0,1] 输入，不作为 RF 着色参数。非零 emissiveFactor、
emissive/occlusion 贴图及材质/纹理扩展在首版明确拒绝。显式 sampler 必须完整声明上述
四项 wrap/filter；未写 sampler 才使用 RF profile，不能将 glTF 显式 sampler 的默认 repeat
误认为 clamp。脸部 normal 的平方长度容差为 `1e-5`，超限拒绝，不自动修复作者方向。

`tools/assets/rfchar_material_contract.py` 是本页材质元数据的可执行校验与默认值展开入口。
它按源材质顺序返回稳定 ID 和所需材质能力，拒绝未知 RF 字段、重复 ID 与非法引用；
不检查 mesh/UV 数据、图片解码、依赖路径或包完整性，也不生成 RFM2。
通过此入口不表示当前 v14 importer 或 GPU 已支持这些材质。

## 能力矩阵与缺失行为

“首版必需”指消费者必须支持，单份内容可不使用所有能力。作者 requires 必须覆盖实际使用集合；
构建器从内容推导并核对，不能凭作者漏写来降级。可选能力只允许内容未使用时缺失。

| 能力名 | 当前端到端状态 | 首版决策 |
| --- | --- | --- |
| `rfchar.canonical.v1` / `rfchar.socket.v1` | RFCHAR importer/resource/instance 已有 | 必需，保留原合同 |
| `rfchar.skin2.v1` | 当前 GLB/RFM2/GPU 路径已有 | 现有基线，是否足够仍需最小变形样本 |
| `rfchar.skin4.v1` / `rfchar.morph.v1` | 不支持 | 待样本决策，不得静默裁剪或中性化 |
| `rfchar.material.v1` | 新 RF 元数据未消费 | 必需，包含本页基础色、toon、描边及脸部控制 |
| `rfchar.texture_rgba_mip.v1` | 当前角色 Scene 只接受平色 | 必需，由 P2 实现 |
| `rfchar.alpha_mask.v1` | GLB 门允许；RFCHAR converter 明确拒绝，未实现 cutoff | 必需，由 P1/P2 贯通 |
| `rfchar.double_sided.v1` | RFCHAR converter 已保存 GLB doubleSided，Scene 已有标志消费 | 必需，完整材质联合验收仍待 P2 |
| `rfchar.index32.v1` | RFM2 已写 uint32；Scene 当前最多 65,536 索引 | 必需，资源化提交，不能只扩大临时数组 |
| `rfchar.action_roles.v1` | RFANIM stable role 组合已有；GLB 动画不由 RFCHAR converter 导入 | 必需，manifest 显式引用 RFANIM |
| `rfchar.action_aux.v1` | RFANIM 尚无通用辅助骨骼通道 | 待内容需求，不得硬编码指骨/发骨编号 |
| `rfchar.lod.v1` / `rfchar.package.v1` | 完整角色组装包未实现 | 必需，由 P1/P3 接入 |

无效字段/范围、重复 ID、缺依赖、未知格式、缺能力、哈希不符都是硬错误，报告
`RFCHAR PACKAGE ERROR <CODE>: <asset>/<field>: <reason>`，退出非零。
能力错误使用 `CAPABILITY_MISSING`；其余分别为 `FIELD_INVALID`、`ID_DUPLICATE`、
`DEPENDENCY_MISSING`、`VERSION_UNSUPPORTED`、`HASH_MISMATCH`。
实验场显示 unavailable/failed 及诊断，不用另一角色代替成功。

## 二进制迁移与资源边界

继续演进 RFM2，下一目标保留为 v15；v2–v14 读取路径与 Humanoid 适配保持原行为。
v15 保留 uint32 索引、SKN1/CHR1 的已验证语义，通过有长度、有版本的扩展目录关联新材质表；
不能复用旧 PMX toon/sphere 字节猜测 RF 语义。目录项必须检查越界、重叠、重复及未知必需块。
具体字节偏移、四权重扩展和 TTEX 版本尚未冻结，P0 不在没有读写器验证时宣称 ABI 完成。
旧 loader 对 v15 必须拒绝；新 loader 对未来未知主版本同样拒绝，禁止降版本号蒙混加载。

包资源共享不可变 mesh/index/skin/material/texture/action；实例独占 pose、palette、动作时钟、
IK 历史和蒙皮输出。缓存键至少包含内容 revision、布局版本和 device generation。
逐帧只更新变化的实例数据；帧槽 pin 住实际提交的 generation，完成后才退休/释放。
切换 revision 在全组加载成功后发布，失败保留旧资源并显式报错；初次失败没有可用实例。
实验场不创建玩法 actor、不进入网络状态；默认 GPU 入口仍等待 P5 单独签收。

## 公开小样与尚待决策的证据

复用 `tools/blender/generate_rfchar_fixture.py` 的独立原创 canonical 骨架，扩展公开小样验证
肩袖抬臂、膝屈曲、手指握持与口眼表情。两权重/四权重必须使用相同 bind mesh、动作和镜头对比，
检查体积塌陷、关节轮廓、穿插及 socket 一致性；morph 与中性脸比较需保留可重放时间和多视角。
没有这些结果前，不把“V22h 恰好两权重且无 morph”当成首版取舍依据。
公开材质小样分别覆盖颜色、UV、MASK、双面、toon、脸部局部空间、描边及 mip，随后再联合审阅。

`tools/blender/generate_rfchar_material_fixture.py` 已提供公开的源数据小样，携带基础纹理、
遮罩、双面及 RF 自定义字段。它的 Blender 预览只显示基础材质；toon、描边、脸部控制与 mip
仍须由后续 GPU consumer 验证，不能以 extras 导出成功代替材质视觉决策。

`tools/assets/rfchar_audit.py` 当前只读清点 GLB 与已知链路缺口，输出源 SHA-256；
它不替代 glb-inspect contract、runtime test、变形样本或 GPU 验收。
`tools/assets/test_rfchar_audit.py` 使用独立生成的公开最小 GLB 探针，完全不依赖 RF-C01；
这些探针不是完整角色或变形样本。审计发现零缺口也不代表可显示或合同通过。
