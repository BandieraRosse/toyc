# RF-C01 初步角色设计

> 状态：设计提案，待美术审阅；不是已交付的运行时角色
> 所有者：Rasterfall character art
> 日期：2026-09-28

> 最近候选：2026-09-29，V08 离线表情与明暗

本稿服务于[私有原创角色活动计划](../plans/private-anime-character-gpu.md)的造型与基模审阅。临时代号 `RF-C01`，角色身份、配色和外观尚可调整。当前成果包括原创多视角设计、各版 Blender 原型及 V08 离线表情与明暗候选；尚未完成计划中的游戏步行与持枪竖切样本。

## 查看成果

当前候选见下方 [V08 表情与明暗](#v08表情与明暗)；以下 V01 文件表保留为最初概念与体块入口。

私有成果保存在仓库本地 `rasterfall/private-assets/source/characters/rf_c01/design-v01/`，遵守现有 private-assets 边界，不加入 Git 或公开资源包。此文档可随代码仓库提交；私有审阅包独立交付，未执行 Git 提交。

| 文件 | 用途 |
| --- | --- |
| `01-character-sheet.png` | 正面、侧面、背面、三分之四全身设计，含脸部及服装细节 |
| `02-head-study.png` | 脸部正侧与三分之四、眼部、头壳和发束拆分设计 |
| `03-blockout-front.png` 至 `07-blockout-head.png` | Blender 实际渲染：正面、侧面、背面、三分之四、头部近景 |
| `rf_c01-blockout.blend` | 可打开、旋转查看及编辑的米制体块原型，包含摄影棚和相机 |
| `rf_c01-blockout.glb` | 供通用 3D 查看器使用的静态模型；不是 RFCHAR 合同输入 |
| `build_blockout.py` | 从零重建原型、GLB 和五个固定渲染视图 |
| `imagegen-prompts.json` | 内置 image_gen 的完整两次提示词和参考图关系 |
| `blockout-stats.json`、`review-manifest.json` | 原型统计、文件用途及 SHA-256 校验清单 |

设计图由内置 image_gen 生成，第二张以第一张为身份参考；它们不是 Blender 或游戏截图。体块原型由独立 Blender 几何脚本制作，没有复制 Eula 或其他已有角色的几何、纹理或动作。生成图中的零散英文装饰文字不定义角色背景、资产合同或游戏功能。

## 造型提案

- 成年女性外勤技术员，兼具持枪行动需求；约 1.70 m、约七头身，体型轻健。
- 深青黑短发，以偏分刘海、露出的双眼和两侧稍长发束构成识别特征。大体积发束优先，避免依赖大量透明发片。
- 暖琥珀色眼睛，柔和但清晰的下颌、可从侧面识别的鼻梁与鼻尖；表情中性、专注。
- 灰蓝短夹克、浅灰肩胸分区、炭灰内搭和完整工装长裤；平底短靴、贴合手套、小型工具袋与琥珀色身份条。
- 轮廓围绕肩、肘、手腕、髋和膝活动留空；肩部无大型护甲，背后无长披风，便于后续持枪和转身。

| 分区 | 建议 sRGB 色值 | 建模与材质意图 |
| --- | --- | --- |
| 头发 | `#263D45` | 分块实体轮廓、少量高光，优先不透明 |
| 夹克 | `#536478` | 哑光织物，褶皱在主形稳定后补充 |
| 肩胸浅色区 | `#C4C7C1` | 中距离职业与上身方向辨识 |
| 内搭、长裤 | `#363E46` | 降低细节对轮廓的干扰 |
| 接缝提示 | `#91B8A9` | 少量局部色，不假定发光能力 |
| 眼睛、身份条 | `#C58A43` | 小面积暖色焦点 |

色值是后续纹理制作建议；Blender 原型使用线性颜色和摄影棚灯光，不能直接把渲染像素当作纹理色值。当前没有制作可导入的纹理图集或 GPU toon/sphere 材质。

## 原型的用途与已知差距

原型包含完整头、发、身体、简化服装和手脚，源坐标为米制、Z-up、-Y forward，鞋底在 Z=0 附近。它用于判断整体比例、空间体积、服装分区和绕行可读性。GLB 为 Blender 标准导出的 Y-up 静态模型。

目前为放松站姿、分件网格，没有 RF canonical T-pose 骨架、蒙皮、attachment、手指关节、UV、动作或 LOD；不能送入 RFCHAR importer 作为正式角色。脸部仍为简化体块，眼睑与面部衔接、发束根部、手掌、肩袖及髋部分件接缝需要后续手工拓扑整理。概念图比原型完成度高，不以原型当前面数和对象数作为运行时预算。

已完成 Blender 后台生成、静态 GLB 导出和五视图渲染，并检查正面、侧面、背面、三分之四及头部视图。未执行 RFCHAR contract、RFM2 runtime、动画、游戏近中远镜头或 GPU 性能验收；这些属于后续接入工作。

重建命令（在仓库根执行，Blender 路径按本机安装调整）：

```powershell
& 'E:/Blender 5.2/blender.exe' --background --python-exit-code 1 --python rasterfall/private-assets/source/characters/rf_c01/design-v01/build_blockout.py
```

## 审阅重点与下一步

### V08：表情与明暗

2026-09-29 候选位于 `authoring/build/v008/`，组装清单为
`authoring/assembly-mouth-v008.json`，串联 `head`、`eyes`、`mouth` 三个 v008 修订。
沿用 V07 中性几何、现有眼口环线、头发、身体、UV、权重和骨架动作；本轮没有重新布线或增加环线，
重点是已有环线上的连续变形、独立半闭眼采样与固定几何明暗实验。

| 文件 | 用途 |
| --- | --- |
| `rf_c01-v008.blend` | 普通材质、默认归零的 `Blink`、`BlinkMid`、`BrowDown`、`SmileL`、`MouthOpen` 离线表情 |
| `expression-review.png`、`blink-progression.png`、`before-after.png` | 正面与三分之四表情、中间态及同设置 V07/V08 对照 |
| `structure-review.png` | 隐藏头发检查眉皮协同、眼睑与口周 |
| `rf_c01-v008-toon-smooth.blend`、`rf_c01-v008-toon-edited.blend` | 同几何、同配色的普通平滑法线与脸部编辑法线对照；仅离线审阅 |
| `normal-comparison.png`、`light-sweep.html` | 前侧与全身对照、可暂停和拖动的 25 档转光序列 |
| `rf_c01-v008.glb`、`rf_c01-v008.rmesh` | 普通材质中性脸导出，无 morph，不包含试验 toon 材质 |
| `validation.json`、`expression-validation.json`、`shading-validation.json` | 隔离、连续强度采样、明暗几何保持证据 |

半闭眼沿眼球表面独立求值，审阅器经 `BlinkMid` 分段插值；压眉对皮肤和眉毛应用同一平滑位移场。
单侧嘴角上提带动邻近唇颊，张口使用连续上下唇位移和圆滑嘴角衰减。与旧端点比较，小幅张口更窄，
完整张口从近矩形变为弧形。眼角仍有凹折，口腔只有暗面；不是完整口腔或任意表情组合验收。

两档明暗分别设置暖肤色阴影、深青头发暗部、灰蓝衣服阴影，脸部边界比头发柔和。
编辑法线减少眼下、鼻侧和嘴角的碎块，保留较小鼻部提示；侧颈边界仍偏硬，脸部体积仍需审阅。
转光使用解析方向光 N·L 隔离法线因素，不包含投影、自阴影、额外轮廓线或游戏 GPU consumer。
全身图只供缩小尺寸辨识，不代表真实 RTS 相机验收。随视角修脸与分部位轮廓线仍延期。

复现脚本位于 `authoring/scripts/`：`build_v008.py` 建立不可覆盖的新修订，
`review_v008.py -- expressions` 与 `-- toon` 生成真实 Blender 渲染，`validate_v008.py` 重开核对，
`package_v008.py` 调用 `sheets_v008.ps1` 组图并校验 ZIP CRC/SHA-256。
Blender 脚本使用工作流中的 `--background --python-exit-code 1 --python` 调用。
打开最终 blend 不依赖历史目录；重新建立同名候选需使用独立基线副本，工具拒绝覆盖已发布修订。
独立包为角色根目录的 `rf-c01-v008-review.zip`。原生 RFCHAR contract、bind、两权重导入和
RFM2 双实例隔离通过；表情与 toon 仍未接入运行时，活动计划阶段 1 未完成。

### V07 分部件创作基线

2026-09-29 从 V07 提取同级 `authoring/`，作为后续创作入口；所有权与使用方式见
[分部件创作架构](../architecture/character-authoring.md)和[工作流](../guides/character-parts.md)。
`assembly.json` 锁定头脸、眼部、口腔、头发底层、前发、侧后发和身体七组源；
`build/rf_c01.blend` 为完整组装预览，`parts/<part>/v001.blend` 可单独打开编辑。

迁移保持 V07 造型，正面、三分之四、侧面、背面和全身五张固定视图与原 V07 PNG 逐像素一致。
已核对逐部件坐标、拓扑、法线、UV、权重、材质、表情、骨架、动作及审阅场景。
独立导出继续为 18 个 primitive、单 skin、无 morph；原生 RFCHAR、RFM2 两权重与双实例隔离通过。
生成与隔离检查记录在 `authoring/build/`；旧 `design-v*` 文件保持。

头发和表情生成器已经独立，正常组装不再依赖旧版目录。新增候选采用部件修订清单，不自动覆盖
V07 或全局更新脸部/身体。未新增造型、RFANIM、握枪或游戏 GPU 表现；阶段 1 仍未完成。

### V07：短发衔接

2026-09-29 候选位于同级 `design-v07/`。侧后发束改为 11 束独立宽度、弯曲方向和长度，
根部和侧缘埋入底层，底层下缘收至发束覆盖区，减少外露壳边。保留 V06 的脸部和离线表情、
V05 身体、49 骨骨架及原有动作；本轮范围是短发几何优化。

`rf_c01-v07.blend` 为完整绑定审阅源，`rf_c01-v07-head-cages.blend` 为头部控制网格；
同名 `.glb` 与 `.rmesh` 是中性脸 bind 导出及导入产物。`compare-*.png` 左为 V06、右为 V07，
覆盖正面、30°、45°、侧面、后侧、背面及全身，沿用相同相机、灯光与 32 次 Cycles 采样。
`expression-review.png` 保留闭眼和张口检查。源仍以普通材质检查体积。

最终网格为 33,952 顶点、65,678 三角形，相对 V06 减少 3,664 三角形（约 5.3%），
不是游戏帧率结论。前侧刘海根部仍有板片感，后侧发尾仍偏规则；眼睑、嘴角、口腔、
真正握枪与 RFANIM 继续待做，阶段 1 未完成。

保存重开验证通过：211 个旧文件哈希保持，19 个脸部及表情对象逐点一致，13,501 个身体顶点
及权重、rest 骨架和动作采样保持。原生 RFCHAR contract、bind、两权重导入和 RFM2 双实例
隔离通过；导出为 18 个 primitive，无 morph。审阅包通过 CRC 与 SHA-256 校验。

复现入口为本目录的 `build_facehair.py -- --render`、`verify_facehair.py`、`make_sheets.ps1`
及 `package_review.py`，调用方式与下方 V06 相同。校验报告为 `validation.json`，
审阅包为同级 `rf-c01-design-v07-review.zip`。重新生成依赖旧版源；打开最终 `.blend` 可独立审阅。

### V06：脸部与分层短发

2026-09-29 独立候选位于 `design-v06/`，在 V05 副本上调整眼窝、眼角到颊面的转折，
补充鼻梁和鼻颊体积；保留偏分、深青短发与成年职业身份。侧后主体改为底层头壳和 13 束
有厚度、根部埋入、发尾错开的发束，刘海根部进一步贴合。不是造型冻结或游戏基模签收。

| 文件 | 用途 |
| --- | --- |
| `rf_c01-v06.blend` | 完整绑定审阅源，默认中性脸与 idle；沿用 V05 idle/walk/aim actions |
| `rf_c01-v06-head-cages.blend` | 独立头部控制网格；不含完整身体、最终蒙皮或表情 key |
| `rf_c01-v06.glb`、`rf_c01-v06.rmesh` | 中性脸 canonical bind 导出与原生导入产物；不含表情 morph |
| `compare-*.png` | 左 V05、右 V06；正面、30°、45°、侧面、后侧、背面和两个全身视图，相同相机、姿态、灯光、32 次 Cycles 采样 |
| `expression-review.png`、`V06-Blink-*.png`、`V06-MouthOpen-*.png` | 正面与 45° 的闭眼、张口离线诊断；不是游戏表情系统 |
| `V06-structure-thirty.png`、`V06-structure-profile.png` | 隐藏头发的结构检查 |
| `build_facehair.py`、`render_baseline.py`、`verify_facehair.py`、`make_sheets.ps1`、`package_review.py` | 重建、只读 V05 对照、保存重开与原生验证、组图、打包 |
| `source-hashes.json`、`candidate-stats.json`、`validation.json`、`review-manifest.json` | 源保护、网格、验证和审阅包哈希事实 |

本轮使用本地参考包的调查报告及 Blender Studio 面部拓扑字幕，按眼部整体、环线变形与发束
分层的顺序检查；没有复制商业角色资产。固定普通材质和灯光检查几何，没有新增作者法线、
两档明暗、随视角修形或运行时材质。局部闭眼试样根据实际眼球表面约束眼睑，修正发现的
眼白穿出；`Blink`、`MouthOpen` shape keys 默认归零，只保留在 Blender 源中。

网格为 35,788 顶点、69,342 三角形；增加的侧后发束使面数高于 V05，不作为最终 LOD 预算。
身体顶点与权重、49 骨 rest skeleton 沿用 V05；原有三个动作保留，未改善其步态或握枪。
保存重开检查核对 162 个旧版文件哈希、13,501 个身体顶点及其权重和骨架静止矩阵。
中性脸导出副本按材质合并为 18 个 primitive，以满足现有 runtime 上限；Blender 审阅源仍保留分件。
原生 RFCHAR contract、bind 一致性、RFM2 v14 导入、两权重蒙皮与双实例隔离检查通过。
发束仍有重复节奏和壳片感，眼睑与嘴角的完整表情拓扑、口腔与牙齿尚未完成；有限视图与
采样不保证任意角度、任意表情无穿插。形体审阅后仍需收敛这些局部，再推进真正的握枪、
RFANIM 和最小 GPU 材质竖切，活动计划阶段 1 保持未完成。

在仓库根复现（打开最终 `.blend` 不依赖旧目录；从脚本重建与哈希复核需要旧版源）：

```powershell
& 'E:/Blender 5.2/blender.exe' --background --python-exit-code 1 --python rasterfall/private-assets/source/characters/rf_c01/design-v06/build_facehair.py -- --render
& 'E:/Blender 5.2/blender.exe' --background --python-exit-code 1 --python rasterfall/private-assets/source/characters/rf_c01/design-v06/render_baseline.py
& 'E:/Blender 5.2/blender.exe' --background --python-exit-code 1 --python rasterfall/private-assets/source/characters/rf_c01/design-v06/verify_facehair.py
powershell -NoProfile -ExecutionPolicy Bypass -File rasterfall/private-assets/source/characters/rf_c01/design-v06/make_sheets.ps1
python rasterfall/private-assets/source/characters/rf_c01/design-v06/package_review.py
```

审阅包为同级 `rf-c01-design-v06-review.zip`。私有源、导出、对照图与包均不加入 Git 或正式 catalog。

### V05：全身重建与首个绑定候选

V05 位于同级 `design-v05/`，延续 A 的成年动漫方向，保留 V04 眼部与口周拓扑。
缩短下半脸并收敛静止嘴形；短发改为连续头顶、侧后主体与贴合的偏分刘海，清除本轮发现的
发根折皱及耳缘穿插。身体重新建立肩袖连通网格、共享裆桥与双腿环线、前臂、手套与分指，
调整腰带、短靴和护膝。仍有后脑整块感、面部转折偏软及肩袖简化，不声明造型已冻结。

| 文件 | 用途 |
| --- | --- |
| `rf_c01-v05-cages.blend` | 细分应用前的可编辑控制网格与骨架；没有最终蒙皮 |
| `rf_c01-v05.blend` | 已绑定候选；保留 canonical T-pose rest，默认显示 idle；Action Editor 可选 idle/walk/aim |
| `rf_c01-v05.glb`、`rf_c01-v05.rmesh` | 通过 RFCHAR 的 bind 资产及 RFM2 v14 验证产物；没有安装到正式 catalog |
| `compare-*.png` | 左 V04、右 V05 的真实 Cycles 对照；相机与灯光一致，全身姿势不同 |
| `motion-review.png`、`V05-walk-*.png`、`V05-aim-000.png` | 步行变形四相位与抬臂样本；aim 为抬臂候选，不含真实武器握持验收 |
| `V05-back.png`、`V05-full-profile.png`、`V05-walk-side.png`、`V05-flex-back.png` | 背面、侧面与关节补查；由 `render_checks.py` 重开绑定源生成 |
| `build_candidate.py`、`verify_candidate.py`、`make_sheets.ps1`、`package_review.py` | 独立生成、重开及原生验证、组图和打包入口 |
| `candidate-stats.json`、`validation.json`、`source-hashes.json`、`review-manifest.json` | 几何、动作采样、源保护和包校验事实 |

本版为 49 根骨骼：21 个 canonical role、20 根手指辅助骨和 8 个非 deform attachment。
每顶点最多两权重；求值网格 55,822 三角形、19 个材质，UV 仅为初步独立岛，尚无绘制图集。
19 个材质和当前网格不是正式运行时预算，后续仍需合并色区、规划 LOD 和满足 Scene 单资源限制。

Windows 原生 GLB contract、bind 一致性、RFM2 导入、BDEF1/BDEF2 蒙皮与双实例隔离通过。
保存重开后核对 127 个旧版文件哈希；三个 Blender action 的首尾采样闭合，walk 样本存在变形，
四相位脚底最低点接近地面。这里的 root 高度修正只属于离线动作样本，不是运行时补偿；
采样不证明完整周期无滑步、无穿插或全部关节合格。idle 与 aim 当前是静止姿态轨道。

Blender action 未转换为 RFANIM，手指、枪械 socket 和双手握持尚未完成游戏链路验证；
没有新 GPU toon、纹理、LOD、正常单人 Scene 或 `RF_MODEL_LAB` 接入。当前成果是已打通基础
资产合同的绑定候选，尚不是各方面合格的游戏基模，活动计划阶段 1 仍未完成。

在仓库根复现（Blender 路径按本机安装调整）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 asset-tools
& 'E:/Blender 5.2/blender.exe' --background --python-exit-code 1 --python rasterfall/private-assets/source/characters/rf_c01/design-v05/build_candidate.py -- --render
& 'E:/Blender 5.2/blender.exe' --background --python-exit-code 1 --python rasterfall/private-assets/source/characters/rf_c01/design-v05/verify_candidate.py
& 'E:/Blender 5.2/blender.exe' --background --python-exit-code 1 --python rasterfall/private-assets/source/characters/rf_c01/design-v05/render_checks.py
powershell -NoProfile -ExecutionPolicy Bypass -File rasterfall/private-assets/source/characters/rf_c01/design-v05/make_sheets.ps1
python rasterfall/private-assets/source/characters/rf_c01/design-v05/package_review.py
```

审阅包为同级 `rf-c01-design-v05-review.zip`。打开绑定源不依赖旧目录；重建仍需 V04 源与
源哈希清单涉及的旧版本。下一步优先收敛肩袖、膝部、手指握持及步态，再完成 RFANIM 与
最小 GPU 材质竖切；不要将本轮格式通过等同于美术、动作和正常游戏验收。

### V04：以 A 为方向的头部与短发重建

用户已选择成年比例、明确动漫五官和实用职业服装的路线，以 A 为继续设计的起点。
独立成果位于 `rasterfall/private-assets/source/characters/rf_c01/design-v04/`；
V01–V03 与 A/B 原文件保持不变。本版没有沿用可归零的 `style_mix`：头部和头发已重建拓扑，
旧 A 文件本身是完整回退与对照来源。

| 文件 | 用途 |
| --- | --- |
| `rf_c01-v04.blend` | 可编辑头部、眼部、口周和分层短发，使用普通材质与 Cycles 摄影棚 |
| `rf_c01-v04-toon.blend` | 同一几何的 EEVEE 两档明暗试样；脸部另用宽缓法线场，非游戏材质实现 |
| `rf_c01-v04.glb` | 普通材质版静态导出，无骨架、蒙皮和动作，不是 RFCHAR 输入 |
| `compare-front.png`、`compare-profile.png`、`compare-three-quarter.png` | 左 A、右 V04，相同相机、灯光及 Cycles 采样 |
| `compare-full.png`、`compare-full-three-quarter.png` | 全身比例对照；身体与装备沿用 A，仅作上下文 |
| `V04-structure-profile.png`、`V04-structure-three-quarter.png` | 临时隐藏头发的头颈结构检查 |
| `V04-toon-front.png`、`V04-toon-three-quarter.png`、`V04-toon-full.png` | 明暗分区与颜色设计参考；不与 Cycles 对照混作单变量实验 |
| `build_design.py`、`verify_design.py`、`make_sheets.ps1` | 生成、保存重开验证、原尺寸对照图拼接 |
| `source-hashes.json`、`validation.json` | 旧资源哈希、身体保持、几何与静态导出检查证据 |

本版重建连续头颈控制网格，保留接近 A 的眼裂横宽；补出口周环线、窄口缝与内侧遮挡面，
重排鼻底到上唇、下唇到下巴以及颌底到颈部的过渡。眼部区分较强上眼线与轻下眼睑，
虹膜采用原创琥珀色分区和高光。短发改为底层头壳、偏分刘海、侧后发束与覆盖根部的头顶发束，
使用不透明薄壳。本版未读取或复制 Eula、maid 的网格、贴图和动作。

普通材质版负责检查真实体积；两档明暗版另用脸部法线场抑制眼下零碎阴影，
不修改几何。该法线场目前定义在静态角色坐标中，不能直接当作带动画的 GPU 材质合同。
此次提前做材质样本是为了联合判断动漫辨识，不代表主形、UV、表情或最终渲染风格已冻结。

保存重开验证核对了 97 个旧文件哈希及 37 个沿用对象的几何与变换。头部控制网格为
1,702 顶点、1,613 面，其中 1,612 个四边面；边界为双眼、口部和颈口，共 4 环。
最终静态导出为 199,968 三角形，其中本版重建部分为 62,142；发束采样已在保留细分的前提下减密。
普通材质与两档明暗源的几何一致，GLB 三角形数与普通材质源的求值网格一致。

仍需人工审阅眼眶体积、嘴角与下唇转折、颌底、头顶及侧后发束交叠；两档明暗版脸部偏平，鼻部提示仍弱。
头发仍有壳片感，耳部和下身体块仍简化；有限坐标、边界和无退化面检查不证明无穿插或适合变形。
全身静态导出的面数包含沿用的下身体块与细分求值，不是运行时预算；尚无绑定、步行、持枪、LOD
或正常游戏近中远镜头验收，活动计划第 1 阶段继续保持未完成。

在仓库根复现：

```powershell
& 'E:/Blender 5.2/blender.exe' --background --python-exit-code 1 --python rasterfall/private-assets/source/characters/rf_c01/design-v04/build_design.py -- final
& 'E:/Blender 5.2/blender.exe' --background --python-exit-code 1 --python rasterfall/private-assets/source/characters/rf_c01/design-v04/verify_design.py
powershell -NoProfile -ExecutionPolicy Bypass -File rasterfall/private-assets/source/characters/rf_c01/design-v04/make_sheets.ps1
python rasterfall/private-assets/source/characters/rf_c01/design-v04/package_review.py
```

独立审阅包为同级 `rf-c01-design-v04-review.zip`，包含 Blender 源、静态 GLB、对照图、脚本及
`review-manifest.json`；打包时复核 ZIP CRC 和文件 SHA-256。打开源文件不依赖旧目录，重新生成则需保留旧源。

### V03 成熟动漫风格候选

基于 V03 的两个非破坏性候选及统一正面、侧面、45°、全身对照见
[成熟动漫比例研究](rf-c01-mature-anime-study.md)。私有成果位于同级 `style-study-v03/`；
A 为轻度 Mature Anime，B 为更明显 Anime，各自保存独立 Blender 文件与可归零 Shape Key。
原 V01–V03 保持不变，两个候选均保留成人身体与装备尺度。该研究提出人工审阅建议，不冻结造型。

### v02 修形审阅

下一版保存在同级 `design-v02/`，完整包为
`rasterfall/private-assets/source/characters/rf_c01/rf-c01-design-v02-review.zip`。
两张概念图沿用 v01；v02 的七张模型图片均为 Blender 实际渲染，新增
`08-head-front.png` 和 `09-head-profile.png`。源文件仍名为 `rf_c01-blockout.blend`，
对应静态 GLB、生成脚本、`refine_geometry.py`、验证脚本和独立 manifest 均在新版目录。

本版把鼻梁、鼻尖、脸颊和下颌改为连续面部表面，调整杏仁形眼白、眼睑和唇线；
重建弧形偏分刘海及沿头壳切线展开的侧后发束，合并肩袖与裤裆的体块交界，补充分指轮廓和衣襟接缝。
这些是造型修订，不代表造型已冻结。眼区仍有叠置表面，服装合并采用体素修形底模，
尚未完成眼眶、肩髋和指蹼的动画拓扑；仍无骨架、蒙皮、socket、UV 图集、动作或 LOD。
细分求值面数与静态 GLB 导出面数分别记录，不能作为正式运行时预算。

本地复核入口（仓库根目录执行）：

```powershell
& 'E:/Blender 5.2/blender.exe' --background --python-exit-code 1 --python rasterfall/private-assets/source/characters/rf_c01/design-v02/build_blockout.py
& 'E:/Blender 5.2/blender.exe' --background --python-exit-code 1 --python rasterfall/private-assets/source/characters/rf_c01/design-v02/verify_review.py
python rasterfall/private-assets/source/characters/rf_c01/design-v02/package_review.py
```

验证脚本核对保存后重新打开的 Blender 源、有限坐标、地面与身高范围、静态 GLB 结构，
并逐项比对 v01 原 manifest 的 SHA-256。打包脚本校验 ZIP CRC 与包内文件哈希。
具体结果保存在私有目录 `review-validation.json`；这些检查不覆盖 RFCHAR、RFM2、变形或游戏 GPU 验收。

### v03 头肩控制网格样本

最新样本位于同级 `design-v03/`，独立包为 `rf-c01-design-v03-review.zip`。
两张概念图继续沿用 v01；`03-head-front.png`、`04-head-profile.png`、
`05-head-three-quarter.png`、`06-bust-three-quarter.png`、`07-full-context.png`、
`08-bust-back.png` 和 `09-head-control-cage.png` 均来自 Blender 真实模型渲染。
最后一张显示关闭细分后的头部控制边，不是概念图描线。

本版重点是可继续编辑的头肩结构：

- 头颈共用控制网格，挖出两个真实眼孔，以三圈四边面连接眼睑与面部；眼球置于孔后，虹膜分区沿眼球曲面。
- 重建较薄的纵向发束控制网格，保留偏分短发轮廓；不是发根、发际线或层次已完成的声明。
- 夹克与袖子通过袖窿边界连通，浅色肩胸区使用网格材质分区；本版头肩不使用体素合并。
- 下身沿用 v02，仅作全身比例参照；没有完成全身重拓扑。

头颈控制网格为 1,022 顶点、961 面（960 个四边面），夹克为 484 顶点、436 个四边面。
头颈的两个眼孔及颈口共 3 个边界环，夹克领口、下摆和袖口共 4 个边界环；
检查未发现孤立顶点、退化面或超过两面共边的边。该检查不证明边流适合表情或肢体变形。
整套含旧下身体块的求值模型为 200,178 三角形，其中本版新建部分为 67,490；
这些数据仍不是正式角色预算，也不能当作 GPU 性能结果。

实际渲染尚未达到概念图的标准：眼角周围体积偏凸，眼睑厚度和脸部平面转折生硬；
下颌接颈过窄，缺少可信的颌底过渡；嘴部虽与连续面部同形，仍缺完整口周环线。
头发仍有头盔感，发根埋入头壳、侧发层次和耳部遮挡需重新调整；肩胸分区和服装褶皱也仍简化。
本版只能确认结构路线有所推进，不能宣称已经证明真实模型接近概念图，或适合进入绑定。

源为 `rf_c01-sample.blend`、静态审阅导出为 `rf_c01-sample.glb`。
`baseline-v02.blend` 是原 v02 源的逐字节副本，供独立重建使用；脚本不会覆盖旧版本。
在仓库根执行：

```powershell
& 'E:/Blender 5.2/blender.exe' --background --python-exit-code 1 --python rasterfall/private-assets/source/characters/rf_c01/design-v03/build_sample.py
& 'E:/Blender 5.2/blender.exe' --background --python-exit-code 1 --python rasterfall/private-assets/source/characters/rf_c01/design-v03/verify_review.py
python rasterfall/private-assets/source/characters/rf_c01/design-v03/package_review.py
```

验证覆盖保存后源重开、有限坐标、地面与身高、上述控制网格结构、静态 GLB 与源求值面数一致、
v01/v02 manifest 哈希保持及审阅包 CRC/文件 SHA-256。验证旧版哈希需要同级旧版本目录。
没有执行 RFCHAR、RFM2、动作、变形或 GPU 游戏验收；未修改运行时代码、实验区展示或默认入口。

下一步继续修正主形、口周和下颌边流，经真实多视图审阅后再铺开全身拓扑、canonical 骨架与稳定 socket，
完成 idle/walk/持枪样本；之后进入材质、资源复用、LOD 与前哨站 `RF_MODEL_LAB` 正常单人 Scene 展示。
造型尚未冻结，活动计划第 1 阶段仍未完成。
