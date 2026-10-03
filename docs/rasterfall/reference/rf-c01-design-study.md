# RF-C01 初步角色设计

> 状态：设计候选，已接入 GPU 实验场；正式角色包尚未签收
> 所有者：Rasterfall character art
> 日期：2026-09-28

> 最近候选：2026-10-03，V23a 身体服装、关节细节与几何清理

本稿服务于[私有原创角色计划](../plans/private-anime-character-gpu.md)的造型与基模审阅。临时代号 `RF-C01`，角色身份、配色和外观尚可调整。当前成果包括原创多视角设计、各版 Blender 原型及 V23a 身体候选；实验场已有静止/步行展示，正式持枪、贴图与 LOD 尚未完成。

## 查看成果

头型与短发的下一轮结构依据见[参考研究](rf-c01-head-hair-references.md)：四组来源、核验范围与分区修形建议；不代表已实施或已验收。

当前候选见下方 [V23a 身体修订](#v23a身体服装与关节细节)；头发沿用 [V22 底发与侧后发连续塑形](#v22底发与侧后发连续塑形)，其依据见[发束边界与底发参考](rf-c01-hair-surface-study.md)。以下 V01 文件表保留为最初概念与体块入口。

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

### V23a：身体服装与关节细节

从 V22h 仅修订 `body`，候选清单为 `authoring/assembly-body-v023a.json`；完整源、GLB、
RFM2、固定视图与前后对照在 `authoring/build/v023a/`。模型目录安装独立的 `rf_c01_v023a.rmesh`，
`RF_MODEL_LAB` 经共享角色目录读取这一明确版本；V22h 源与旧产物保留。

肩袖增加连续的袖山体积、肩缝与袖口，夹克加入胸袋开口、裁片接缝和腰带环；工装裤保留连通的
裆部与腿部表面，细化膝/裤脚褶皱、侧袋盖和开口。两块厚矩形膝片替换为贴合腿部的轻量护膝，
手套增加腕口与指节细节、收敛指尖，靴子调整鞋头并补靴口/鞋面分区。保持原有成年职业比例与配色。
这些细节复用已有不透明材质，不增加纹理、材质数、骨骼或渲染专用路径。

清理身体原有孤立顶点与手套面板重合倒角面。身体三角形减少，同时保持头脸、眼部、口腔、全部头发、
表情、骨架、动作、空间接口和审阅场景的语义记录不变。部件隔离、保存重开、无退化面、51 个离线
动作采样与原生 RFCHAR/双实例合同通过；固定视图已检查。数据与范围见
[V23a 身体修订记录](../archive/character-body-v023a-20261003.md)。本轮没有完成武器握持、角色贴图、
LOD 或完整动作签收；头部沿用已锁定版本，不能把身体修订记作头部改善。

### V22：底发与侧后发连续塑形

从 v021f 修订 `hair_back` 与 `hair_base`，候选清单为 `authoring/assembly-hair_base-v022h.json`；
审阅入口为 `authoring/build/v022h/review.html`，上排 V21f、下排 V22h，附 Blender、GLB 与 RFM2。
本轮检索建模资料并实际查看五张作者公开参考图，出处、使用边界和结构推导见
[发束边界与底发参考](rf-c01-hair-surface-study.md)。

侧后发重新采样到同一只读头壳包络，束宽以顺流长面为主，横截面不对称，末段再收尖。
底发根据新侧后束的外表面塑形，消解完整露出的圆钝束根；上部加入连续的浅发流转面，
下缘退到长短不一的束尾内侧。两部件共享空间变形，避免底体与发束各自塑形造成曲率脱节。
保持原对象集合、拓扑、UV、权重与材质；头脸、刘海、表情、身体、骨架、审阅场景和 V20c 接口保持。
外轮廓更贴合头壳，原来宽叶片贴在大底壳上的组合明显减弱；这也改变了侧后发量和束尾排列，
需要按完整发型审阅，不能只比某个局部接缝。近看仍有个别束肩接线，尚未表示美术冻结。

保存重开、部件隔离、确定性重复生成、表情往返、RFCHAR 导入与原生实例隔离通过。
几何检查覆盖外／内表面朝向、退化面和单网格内非相邻三角形穿越；部件搭接不是该检查的失败项，
也不能据此断言所有姿态无穿插。由于整束走向重采样，不能沿用旧版法线夹角作为唯一翻面判据。

复现使用私有 `scripts/run_v022h.py` 的 `build`、`surface_validate`、`validate`、`diagnostic`、
`studio`，再执行 `native_validate_v022h.py`、`package_v022h.py`。V22–V22g 的试验保留：
单独埋根会留下突兀三角尾，直接膨胀底体会抹掉层次；V22g 的顶点扇一片朝向异常已在 V22h 修正。
未替换初始组装或游戏默认角色，未执行游戏 GPU 签收，私有产物不加入 Git。

### V21：侧后发束转向与收尾

从 v020c 仅修订 `hair_back`，候选清单为 `authoring/assembly-hair_back-v021f.json`；
审阅入口为 `authoring/build/v021f/review.html`，同目录提供 Blender 源、GLB 与 RFM2。
上排 V20c、下排 V21f，提供两侧灰模、剪影、原材质、顶视、固定几何转光和表情对照。

逐束错开弯曲方向、宽度峰值与结束高度，局部横截面偏转；少数耳后及后脑次束从中段露出。
沿发尾走向压缩末端，保持闭合截面；束肩边缘向主体内收，减少抬边造成的深接缝。
保留原对象、拓扑、UV、权重与材质；头脸、原表情、刘海、底发、身体、骨架、审阅场景和
空间接口保持 V20c。配方显式锁定 V20c 冻结侧后发与头部，重复生成不累积修形。

局部三角面朝向、保存重开、部件隔离、四组表情往返、RFCHAR 与原生实例隔离检查通过。
宽主束仍有叶状边界，部分束根接线和底发壳体感尚未解决；该候选供美术对照，不表示造型冻结
或游戏 GPU 签收。过程候选保留：V21c 的中心线压缩出现局部翻面，V21d 改用正向截面压缩
但收尾偏短；V21f 在减小压缩幅度后继续收回抬起的束边。

复现使用私有 `scripts/run_v021f.py` 的 `build`、`local_validate`、`validate`、`diagnostic`、
`studio`，再执行 `native_validate_v021f.py`、`package_v021f.py`。旧版本不覆盖，私有产物不加入 Git。

### V20：鼻唇、额顶与发束遮挡

从 v019 继续，当前清单为 `authoring/assembly-hair_back-v020c.json`；审阅入口为
`authoring/build/v020c/review.html`，同目录提供 `rf_c01-v020c.blend`、GLB 与 RFM2。
对照图上排 V19，下排 V20c，沿用固定相机、灯光和材质。

鼻尖与上额至顶弧局部边网重采样，整理切向间距及法向曲率；鼻侧转面与后脑保持。
连续头网格从 6,872 增至 10,992 顶点，局部修形最大约 1.27 mm，头围极值与下颈保持。
薄唇增加轻微中央弧线，嘴角逐渐收紧并埋入承载面，没有整体加厚。
次发束下沉、缩短并提前藏进主体；细尖尾的末段保持刚性平移，避免近重合顶点翻面。
前发使用较小幅度以维持额前衔接，侧后保留更明显的收束。底层发壳几何保持。

局部细分插值 UV、权重和原有表情，中性修形同步到各表情 key；眼部组件、口腔、身体、
骨架和审阅场景保持。`interfaces-v020c.json` 记录局部头壳迁移，程序头发显式锁定新头部
及原冻结发束源，不隐式沿历史配方累积生成。

保存重开、部件隔离、局部三角面朝向、表情增量和四组表情往返通过；RFCHAR 零错误零警告，
原生实例隔离通过。提供鼻唇近景、裸头额顶、多方向发束、表情和固定几何转光对照。
初稿 v020 有局部翻面，v020b 的刘海缩短过多；当前 v020c 修正这些问题，过程候选保留。
侧后大块面仍偏整齐，嘴角仍是简化结构；有限采样不证明任意表情无穿插，不表示造型冻结或游戏 GPU 签收。

复现入口为私有 `scripts/run_v020c.py` 的 `build`、`local_validate`、`validate`、`studio`、
`detail`、`light`，以及 Blender 执行 `diagnostic_v020c.py`；随后执行
`native_validate_v020c.py` 与 `package_v020c.py`。源文件和审阅产物保留在私有目录，不加入 Git。

### V19：鼻部与口周结构

从 v018b 只修订头部，当前清单为 `authoring/assembly-head-v019.json`；审阅入口为
`authoring/build/v019/review.html`，同目录提供 `rf_c01-v019.blend`、GLB 与 RFM2。
对照图上排 V18b，下排 V19，固定相机、灯光与材质。

收回鼻尖投影、补充鼻梁与鼻侧支撑，并缓和鼻底折返；口周重点填缓下唇至下巴的深凹，
收回下唇突沿、补上唇承载面，并小幅调整中央唇弧与嘴角。头部最大位移约 3.39 mm。
拓扑、UV、权重及原有表情增量保持；头发、额顶、眼部组件、口腔、身体、骨架与审阅场景保持。
继续使用 v018b 接口及头发锁定的头壳依赖，本轮没有改变头皮采样区。

保存重开、部件隔离、局部面朝向、非目标区域和表情增量核对通过；四组表情往返、
RFCHAR 零错误零警告及原生实例隔离通过。正侧与 3/4 近景、唇下特写、小幅张口、
半闭眼、闭眼、皱眉、微笑及固定几何转光均提供离线审阅图。
侧面唇下凹折减弱，但嘴缝仍为简化结构，鼻部近景仍可见轮廓折线；这些结果不等于
任意组合表情无穿插、造型冻结或游戏 GPU 签收。

私有复现入口为 `scripts/run_v019.py` 的 `build`、`local_validate`、`validate`、`studio`、
`detail`、`light`，随后执行 `native_validate_v019.py` 和 `package_v019.py`。V18b 与旧版保留。

### V18：鼻部、口周、额顶与发束

从 v017a 继续，当前清单为 `authoring/assembly-hair_back-v018b.json`，审阅入口为
`authoring/build/v018b/review.html`；同目录提供 `rf_c01-v018b.blend`、GLB 与 RFM2。
对照图上排 V17a，下排 V18b，使用相同相机与灯光。

鼻尖横向收窄、轻抬，鼻底回收并补鼻侧小体积；口周增加宽缓承载面，整理下唇厚度、
唇下过渡与嘴角渐隐。上额至顶弧作局部曲率调整，头部最大位移约 1.65 mm。
后脑、头顶最高点、下颈、眼部组件、口腔、身体、骨架和固定审阅场景保持。
`interfaces-v018b.json` 显式记录额顶接口迁移，头发同步修订并锁定新头部依赖。
主次发束分别收束束根、调整搭接深度及发尾长短；保留现有拓扑、UV 和权重。

初稿 v018 和 v018a 的部分极细发尾未通过面朝向对照；v018b 对四边面的两种剖分均约束
位移幅度，保留这些未采用的试稿。最终版通过保存重开、部件隔离、局部面朝向及表情增量核对，
并检查 Blink、SmileL、MouthOpen、BrowDown 四组往返采样。九方向、原材质近景、口周特写、
表情及固定几何转光用于美术审阅。局部检查不证明任意组合表情均无穿插。

近景鼻尖和额顶仍能看到有限拓扑造成的轮廓折线，口缝与发束片状感尚未完全消除；
本轮属于局部改善候选，不表示造型冻结或游戏 GPU 签收。复现入口为私有
`scripts/run_v018b.py` 的 `build`、`local_validate`、`validate`、`diagnostic`、`studio`、
`detail`、`light`，随后运行 `native_validate_v018b.py` 与 `package_v018b.py`。
原生 RFCHAR 与实例隔离结果见审阅目录 `native-validation.json`。

### V17：眼眶、鼻侧与脸颊连接

从 v016 只修订 `head` 的眼眶外圈、鼻侧与上脸颊，当前清单为
`authoring/assembly-head-v017a.json`，审阅入口为 `authoring/build/v017a/review.html`。
同目录提供 `rf_c01-v017a.blend`、GLB、RFM2；对照图上排 V16，下排 V17a。

上眼眶增加轻微覆盖体积，内眼角旁的鼻侧和下眼睑外圈接入较宽缓的脸颊曲面；
局部只调整前后深度，保留正面眼形。锁定眼孔及相邻内圈的 240 个顶点，
头部最大位移约 2.29 mm，原有表情增量保持。头顶、后脑、口周、下颈、眼部组件、
口腔、头发、身体、骨架和审阅场景保持。继续使用 v016 空间接口；头皮采样区未改变，
头发保留其冻结 v016 头壳依赖，不隐式更新生成源。

九方向、原材质近景、半闭眼／闭眼／皱眉／微笑和固定几何转光用于美术对照。
保存重开、局部隔离、四组表情往返、RFCHAR 零错误零警告及原生实例隔离通过。
`local-validation.json` 另外检查边界顶点、非目标区域、表情增量和采样三角面朝向；
初稿 v017 在闭眼时出现眉毛遮挡，v017a 按各表情的眉毛运动范围收回附近增量；
`brow-validation.json` 检查各 key 五档强度的眉毛顶点正面射线间距，没有新增遮挡。
这些检查不等于任意组合表情无穿插，造型仍待用户审阅，未执行游戏 GPU 签收。

私有入口为 `scripts/run_v017a.py` 的 `build`、`local_validate`、`brow_validate`、`detail`、`studio`、
`validate`；全方向先运行 `diagnostic`，再运行 `light` 生成实际区域光对照。
最后执行 `native_validate_v017a.py` 和 `finalize_v017a.py` 整理审阅页。旧候选保留。

### V16：额顶与鼻梁衔接

从 v015 修订上额到头顶、额头到鼻梁的过渡。当前清单为
`authoring/assembly-hair_back-v016.json`，审阅入口为 `authoring/build/v016/review.html`；
同目录提供 `rf_c01-v016.blend`、GLB、RFM2 和上排 V15／下排 V16 的同相机对照。

上额提前向后转入顶弧，中额补充轻微弧度；鼻根浅收、下接鼻梁支撑，位移向鼻侧渐退。
头部最大位移约 4.05 mm。后脑、最高点、眼部部件、口周和下颈保持；
`interfaces-v016.json` 显式记录空间迁移，底发、前发和后发同步跟随。
拓扑、UV、权重、材质、表情增量、身体、骨架和固定审阅场景保持。

参考 [AnimeOutline 侧脸教程](https://www.animeoutline.com/how-to-draw-anime-face-side-view-with-proportions/)
的无发头部原图与正文，借鉴额鼻连续曲线；另阅读
[AnimSchool 头部建模教学](https://blog.animschool.edu/2024/04/27/3d-head-modeling-topology-techniques/)
的体块及轮廓布线说明。前者为二维结构参考，不照搬幼态比例；后者未完整观看视频。
第三方图仅作参考，未导入其模型或纹理。

已完成九方向裸头／底发／全发对照、原材质和固定几何转光；保存重开、接口隔离、
四组表情往返、RFCHAR 导入与原生实例隔离通过。证据见同目录验证 JSON 与日志。
私有复现入口为 `scripts/run_v016.py` 的 `build`、`interface_validate`、`diagnostic`、
`studio`、`light`、`detail`、`validate`，以及 `native_validate_v016.py`、`package_v016.py`。
构建拒绝覆盖旧候选；形体仍待用户审阅，未执行游戏 GPU 签收，阶段 1 保持未完成。

### V15：后脑体积收敛

针对用户指出的 3/4 视角后脑过度隆起，从 v014c 收回中段枕部深度及侧后宽度。
当前清单为 `authoring/assembly-hair_back-v015.json`，审阅入口为 `authoring/build/v015/review.html`；
同目录提供 `rf_c01-v015.blend`、GLB、RFM2 和同相机对照图（上排 V14c，下排 V15）。

裸头最后缘由 Y=0.11951 m 收至 0.10626 m，收回约 13.25 mm；侧后宽度同步渐退。
Z≥1.632 m 的顶区与 Z≤1.470 m 的下颈保持，最高点仍为 1.64800 m。
`interfaces-v015.json` 显式锁定空间修订；底发、后发同映射收拢，前发几何不变。
五官、表情增量、拓扑、UV、权重、材质、身体、骨架及审阅场景保持。

九方向裸头／底发／全发、原材质与固定几何转光用于对照；保存重开、接口隔离、四组表情往返、
RFCHAR 导入与原生实例隔离通过。证据在同目录各项 `*-validation.json` 和 `validation.json`。
旧候选保留，造型仍待用户审阅；本轮没有游戏 GPU 签收，阶段 1 继续未完成。

### V14：顶弧与侧后过渡

前轮候选为 `authoring/assembly-hair_back-v014c.json`，审阅入口为
`authoring/build/v014c/review.html`；同目录提供 `rf_c01-v014c.blend`、GLB 和 RFM2。
从用户已认可后脑体积的 v013b 修订，保留旧版与本轮试样。对照图上排 V13b、下排 V14c，
包括裸头、底发、全发九方向灰模与剪影、原材质正侧和三分之四、固定几何区域光旋转及表情。

本轮将上半颅体渐变到较宽缓的顶弧，前额采用单独渐退范围，避免压顶产生肩部折点或额部外鼓。
裸头最高点由 1.65644 m 降至 1.64800 m，最大位移约 8.44 mm；最大横向宽度与最后缘保持。
Z≤1.585 m 的几何保持，保留中段枕部、五官和下颈。`interfaces-v014c.json` 显式锁定空间修订；
依赖头发按冻结 v013b 头壳上的径向对应位移同步迁移，沿用原有发束组织和后颈贴合。

`shape-comparison.png` 与 `hair-comparison.png` 是本轮主要审阅图。逐对象核对拓扑、UV、权重、材质
及表情增量保持；眼部、口腔、身体、骨架和审阅场景保持。保存重开、四组表情各 17 档正反向采样、
RFCHAR 导入与原生实例隔离通过，证据分别见 `validation.json`、`interface-validation.json`、
`expression-validation.json` 和 `native-validation.json`。本轮只调整离线头型，未执行游戏 GPU 签收；
宽发束分区仍可继续审阅，造型未冻结，阶段 1 仍未完成。

### V13：裸头枕部与后颈发层

前轮候选为 `authoring/assembly-hair_back-v013b.json`，成果在 `authoring/build/v013b/`。
从 v012b 建立 V13 裸头与发束试样，再只修订底发、后发形成 v013b；旧候选和初始组装保留。

按用户反馈直接增加裸头中段枕部及侧后体积，不以头发替代头壳。`interfaces-v013.json` 锁定
增量空间映射、配方哈希和前版接口；头部与依赖头发共同迁移。裸头水平截面显示，Z=1.55/1.57 m
的最后缘分别后移约 14.4/15.8 mm，侧后宽度也小幅增加；顶区和枕下渐退，前脸、眼位、下颈保持。
头部最大顶点位移约 16.3 mm，既有表情增量保持。数值来自裸头网格截面，不含头发。

侧后发压低次级束鼓包、延长耳上过渡、减弱主束隆起并错开搭接。v013b 的头发参数显式锁定
`parts/head/v013.blend`，后颈底发和发束只读采样真实头壳，使下层逐渐贴合后颈，减轻悬空硬边。
前脸、唇缘、眼部与口腔沿用 v012b；本轮没有继续修改口周或重建表情。

| 文件 | 用途 |
| --- | --- |
| `review.html` | 本轮审阅入口，上排 V12b、下排 V13b；含裸头／底发／全发九方向原图 |
| `shape-comparison.png`、`hair-comparison.png`、`silhouette-comparison.png` | 同相机裸头体积、侧后发和黑色剪影对照 |
| `face-comparison.png`、`light-comparison.png` | 正侧与三分之四、固定几何的实际区域光旋转 |
| `mouth-comparison.png`、`blink-comparison.png` | 小幅张口及半闭眼／闭眼复核 |
| `rf_c01-v013b.blend`、`rf_c01-v013b.glb`、`rf_c01-v013b.rmesh` | 组装源、中性导出和原生导入产物 |
| `validation.json`、`interface-validation.json`、`expression-validation.json`、`native-validation.json` | 发层隔离、继承的裸头迁移与截面、表情往返和原生合同证据 |

最终候选保存重开、拓扑与四组表情各 17 档正反向采样通过；561 个先前源文件哈希保持。
身体、眼部、口腔、骨架动作和固定审阅场景保持。Windows 原生 RFCHAR 零错误零警告，
RFM2 双实例隔离通过；导出仍为 47,644 顶点、66,356 三角形、49 骨骼、8 附件。

对照可见裸头枕部更饱满、耳上凸起减弱、后颈底层硬边收敛；部分宽主束仍有封闭叶片边界，
长尖末端与束间遮挡仍待美术审阅。有限视角和表情往返不证明任意组合无穿插，造型仍未冻结，
未接入游戏、未替换默认角色，阶段 1 保持未完成。

私有复现入口为 `scripts/build_v013.py` → `interface_validate_v013.py` → `build_v013b.py`；
新修订只可在独立副本或未占用路径生成。最终审阅执行 `run_v013b.py diagnostic`、`studio`、
`validate`，再运行 `native_validate_v013b.py`、`finalize_v013b.py` 和 `package_v013b.py`。
Blender 脚本按[分部件工作流](../guides/character-parts.md)后台执行；`run_` 脚本用 Python 调度并保存退出码和 UTF-8 日志。

### V12：顶后弧、分层短发与唇缘

当前审阅候选为 `authoring/assembly-hair_back-v012b.json`，成果在 `authoring/build/v012b/`。
从 v011c 显式修订；v012 为首轮试样，旧版文件与初始组装保留。依据
[头型与短发参考](rf-c01-head-hair-references.md)调整裸头和依赖头发，不只加厚后发。

顶区向前后及两侧展开，最高区略降，后上部体积向侧后延展；枕下和领口接口保持。
`interfaces-v012b.json` 锁定空间迁移配方及哈希；同一空间映射应用到头部、底发、前发和后发。
后发错开起点、宽度峰值、转向和结束高度，截面采用偏心曲率；次级束收窄根部，底发延伸承接后颈。
前侧长束保持身份长度，微调中段向后弯曲。唇部锁住真实口缝边界，局部平滑外唇卷边并减弱材质色差；
各表情保留原形变增量，没有改眼位或重建口腔。

| 文件 | 用途 |
| --- | --- |
| `review.html` | 本轮集中审阅入口；上排 V11c、下排 V12b |
| `rf_c01-v012b.blend`、`rf_c01-v012b.glb`、`rf_c01-v012b.rmesh` | 可编辑源、中性绑定导出与原生导入 |
| `shape-comparison.png`、`silhouette-comparison.png` | 裸头体积及全发黑色剪影；另有裸头／底发／全发九方向原图 |
| `face-comparison.png`、`hair-comparison.png` | 同相机与照明的前后对照 |
| `mouth-comparison.png`、`blink-comparison.png` | 唇缘、小幅张口、半闭眼和闭眼 |
| `light-comparison.png`、`V12-mouth-gray-*.png` | 固定几何、普通灰材质的实际区域光旋转与唇部灰模 |
| `validation.json`、`expression-validation.json`、`native-validation.json` | 组装隔离、保存重开／表情往返和原生合同结果 |

组装、拓扑与四组表情各 17 档正反向采样通过，488 个既有源文件哈希保持；身体、眼部、口腔、
骨架动作和固定审阅场景不变。Windows 原生 RFCHAR 为零错误零警告，RFM2 双实例隔离通过；
导出仍为 47,644 顶点、66,356 三角形、49 骨骼、8 附件。

对照可见顶后弧更宽缓、唇外缘减弱、发尾高度错开；部分宽主束仍有封闭叶片边界，耳后短束的
凸起与搭接还需美术审阅。九方向与离线转光不证明任意角度、任意表情无穿插；未接入游戏，未替换默认角色。
造型仍未冻结，活动计划阶段 1 保持未完成。

重建入口为私有 `authoring/scripts/build_v012b.py`；诊断、摄影棚、实际转光分别使用
`review_v012b.py -- diagnostic`、`review_v012b.py -- studio`、`light_v012b.py`，最后运行
`validate_v012b.py`、`native_validate_v012b.py`、`package_v012b.py`。Blender 脚本遵循
[分部件工作流](../guides/character-parts.md)的后台执行方式，已有修订不得覆盖。

### V11：侧面体积与头颈关系

V11 候选为 `authoring/assembly-hair_back-v011c.json`，成果位于 `authoring/build/v011c/`。
从 V10 v010d 修订，保留 v011a/v011b 中间候选；用户反馈侧视有所好转但后脑仍扁后，v011c
再同步补充枕部与后发体积。初始组装、V10 和更早源保持，不替换默认运行时角色。

本轮强化侧颊、颏部与颌底的前后关系，调整上颈位置与下颌角过渡；鼻梁到鼻侧补充连续体积。
减轻唇色边界、增加下唇外侧厚度，并让张口形变依据真实唇缝曲线，修正初稿中央折口。
口腔仍为三环简化内壁，没有牙齿、舌头或夸张张口。头发以四组主束带动较浅次级束，
减少顶部凸起，错开发宽、长度及发尾方向；宽主束仍有片状感，极近景唇色边界仍可继续软化。

枕部新增约 11 mm 后向体积，后发同步跟随，并向头顶与后颈渐退。该变化显式保存为
`interfaces-v011c.json`，记录旧接口哈希和受影响部件；眼位、前脸与领口连接保持。
`assembly-v011c-interface-baseline.json` 仅为共享接口迁移的验证基线，不是另一个几何源。
`interface-validation.json` 逐点核对声明的变换，以及拓扑、UV、材质、权重、前脸和下颈保持。

| 文件 | 用途 |
| --- | --- |
| `rf_c01-v011.blend`、`rf_c01-v011.glb`、`rf_c01-v011.rmesh` | 可编辑中性源、无 morph 的绑定导出及原生导入 |
| `review.html`、`profile-comparison.png`、`occiput-comparison.png` | V10/V11 侧视、隐藏头发结构、头肩关系，以及 v011b/v011c 枕部对照 |
| `face-comparison.png`、`hair-comparison.png`、`lip-comparison.png` | 同相机、同灯光和同采样的前脸、顶部、侧后发与小幅说话 |
| `blink-review.png`、`normal-comparison.png`、`light-review.png` | 闭眼复核及普通／编辑法线的固定几何转光；正面、侧面、三分之四各 25 档 |
| `validation.json`、`interface-validation.json`、`expression-validation.json`、`native-validation.json` | 部件隔离、接口迁移、表情往返、源保护及原生资产验证 |

保存重开后网格检查与四组表情各 17 档正反向采样通过；372 个既有 Blender/JSON 源文件哈希保持。
普通／编辑法线试样保持几何、UV、权重与表情一致。两档明暗采用离线方向光 N·L，不含投影阴影；
鼻部在部分正面光向仍较含蓄，不以此宣称所有光向已通过美术验收。
Windows 原生 RFCHAR contract 零错误零警告，RFM2 双实例隔离通过；导出为 47,644 顶点、
66,356 三角形、49 骨骼、8 附件。以上不代表任意表情组合无穿插或正常游戏 GPU 验收。

复现入口位于 `authoring/scripts/`：`build_v011b.py -- v011b`、`build_occiput_v011.py`，
以及 `probe_v011.py -- v011c`、`review_v011c.py -- expressions`／`-- shading`、
`validate_v011c.py`、`validate_occiput_v011.py`、`native_validate_v011c.py` 和 `package_v011c.py`。
发布拒绝覆盖已有版本，重建需独立基线副本；冻结 `.blend` 可直接打开。
独立审阅包为角色根目录的 `rf-c01-v011-review.zip`，私有资产不加入 Git。
造型继续待美术审阅，阶段 1、持枪、RFANIM 与游戏 GPU 材质仍未完成。

### V10：局部修形与整体头发

当前候选为 `authoring/assembly-hair_back-v010d.json`，成果位于 `authoring/build/v010d/`。
它显式继承 V09 的 v009c；v010/v010a/v010b/v010c 是本轮诊断中间产物，初始组装、V09 和更早源仍保留。
本轮修订 `head`、`eyes`、`mouth`、`hair_base`、`hair_front`、`hair_back`；身体、骨架、动作、
空间接口与固定审阅场景保持。头部仍为 6,872 顶点、6,740 面，没有继续加密眼区。

眼角调整支撑行间距、内缘厚度和闭眼的位移衰减，固定眼孔与厚度行后平滑眼角支撑区位移，减轻全闭眼的竖向折痕；闭合线采用连续弧线，眼线与眼睑共用眼孔参考。
鼻梁与鼻侧增加宽缓的几何支撑，局部放松下颌到侧颈；另以固定几何的连续法线场审阅转光，
不增加鼻部描边。嘴部保留下唇外侧体积，扩大嘴角位移场，将口腔入口靠近唇缘并区分浅层内壁与深处暗面；
内壁张开幅度向深处递减，显露浅层入口；仍无牙齿、舌头或夸张张口。

头发共用顶部隆起与侧后流向，主次发束提前从底层显露，刘海的既有控制行沿头壳向顶部延伸，
保留薄壳厚度。正面、侧面、三分之四沿用固定相机；补充俯视、背面和后侧诊断，不写回公共审阅场景。
整体感仍需美术审阅，尤其是顶部发束汇合处与两档明暗下的分束节奏；没有据此冻结造型。

| 文件 | 用途 |
| --- | --- |
| `rf_c01-v010.blend` | 中性可编辑模型，离线表情归零 |
| `review.html`、`hair-comparison.png` | 闭眼往返、两视角转光，以及 V09/V10 头发与顶部、后侧审阅 |
| `blink-progression.png`、`mouth-review.png`、`eye-detail-comparison.png`、`lip-detail-comparison.png` | 50%、75%、全闭眼及微笑、小幅说话的三视角和局部近景 |
| `normal-comparison.png`、`light-review.png`、`rf_c01-v010-toon-*.blend` | 普通／编辑法线的固定几何转光，正面和三分之四各 25 档 |
| `validation.json`、`expression-validation.json`、`shading-validation.json` | 部件隔离、头发及脸部网格、表情往返与明暗几何保持证据 |

保存重开后，头脸、口腔和头发通过退化面、孤立顶点及超过两面共边检查；四组表情各 17 档正反向、
共 136 次采样有限且往返一致。293 个既有源文件的哈希保持，身体、骨架、动作和审阅场景隔离检查通过。
普通／编辑法线源的几何、UV、权重与表情保持。Windows 原生 RFCHAR contract 零错误零警告，
RFM2 双实例隔离通过；中性导出为 47,644 顶点、66,356 三角形、49 骨骼、8 附件。
这些检查不证明任意表情组合无穿插，也不是 GPU 性能结论。独立审阅包为角色根目录的
`rf-c01-v010-review.zip`，附文件 SHA-256 与 ZIP CRC 校验。

复现脚本位于 `authoring/scripts/`：`build_v010.py -- v010d`、`geometry_v010.py`、
`expressions_v010.py`、`hair_v010.py`、`normals_v010.py`，以及 `review_v010.py -- expressions`、
`-- shading`、`details_v010.py`、`validate_v010.py`、`native_validate_v010.py`、`package_v010.py`。
修订发布仍拒绝覆盖已有版本；重建需独立基线副本。冻结组装可直接打开，不需要执行历史生成器。
这轮仅为离线造型候选，不代表 RFANIM、游戏 GPU 材质或正常单人展示已完成。

### V09：局部拓扑与脸部体积

2026-09-29 的审阅候选位于 `authoring/build/v009c/`，清单为
`authoring/assembly-mouth-v009c.json`。v009/v009a/v009b 保留为本轮诊断中间产物，不是交付入口。
最终 `head`、`eyes`、`mouth` 修订通过部件发布与重新组装；旧版源、头发、身体、骨架、动作、
空间接口和固定审阅场景保持，初始 `assembly.json` 未替换。

眼孔径向边局部细分，插入四边形支撑环并放松眼角邻域间距，增加 0.65 mm 内缘厚度。
头部为 6,872 顶点、6,740 面，比 V08 增加 240 顶点与 240 面；没有重建整头。
闭眼读取实际眼孔上下边界，支撑行随边缘移动；独立眼线读取相同边界，保留 `BlinkMid` 分段采样。
正面终点图已消除首轮候选的虹膜残缝，仍需审阅眼角短折线和眼下局部过渡。

单侧微笑增加较宽的脸颊位移场，张口保留下唇向外的体积；口腔由单张暗面改成 132 顶点、
89 面、约 9 mm 深的三环内壁与背面。它仍是简化口腔，没有牙齿、舌头或夸张表情合同。
额头、眼下、鼻侧、下颌、颈部分区连续混合法线，曲面目标保留侧脸体积；抑制鼻部孤立暗斑，
鼻部提示仍较含蓄，侧颈阴影边界仍需结合转光序列审阅。

| 文件 | 用途 |
| --- | --- |
| `rf_c01-v009.blend` | 可编辑普通材质源，五个离线表情 key 默认归零 |
| `review.html` | 闭眼往返、普通／编辑法线 25 档转光、原尺寸小图和组图入口 |
| `blink-progression.png`、`mouth-review.png`、`before-after.png` | 中间态、三视角嘴角与小幅说话、同设置 V08/V09 对照 |
| `normal-comparison.png`、`rf_c01-v009-toon-*.blend` | 固定几何的分区法线两档明暗实验 |
| `outline-comparison.png`、`plain-small-*.png`、`outline-small-*.png` | 临时反壳局部描边与 96/192 像素宽全身渲染，眼鼻内部不加额外边线 |
| `rf_c01-v009.glb`、`rf_c01-v009.rmesh` | 中性脸普通材质导出，不带 morph 或实验 toon |
| `validation.json`、`expression-validation.json`、`shading-validation.json` | 部件隔离、网格、往返与明暗几何保持证据 |

保存重开检查通过非流形边、孤立顶点、退化面与两权重检查；四组表情各 17 档正反向、共 136 次
采样坐标有限且往返一致。普通／编辑法线源重开后的几何、UV、权重和表情保持。
Windows 原生 RFCHAR contract 零错误零警告，RFM2 双实例隔离通过；中性导出为 47,600 顶点、
66,356 三角形、49 骨骼、8 附件。这些数值不是 GPU 预算或性能结论。

复现脚本位于 `authoring/scripts/`：`build_v009.py`、`geometry_v009.py`、`expressions_v009.py`，
`review_v009.py -- expressions`、`-- shading`、`-- baseline`，以及 `details_v009.py`、
`validate_v009.py`、`package_v009.py`。先构建并生成表情、明暗与旧版对照，再生成细节图、重开验证、
按分部件工作流导入 RFM2，最后打包。使用已发布源时无需再次构建；重建同名修订需要独立基线副本。
单独重建眼部表情时，私有配方 `eyelids_v009.json` 提供本版冻结眼孔边界；再次修改眼孔需显式修订该配方。
独立包为角色根目录的 `rf-c01-v009-review.zip`，带文件 SHA-256 与 ZIP CRC 校验。

离线位置往返不证明任意组合无穿插；小图不是实际 RTS 镜头，解析转光不含真实投影。
本轮未推进持枪、动作、GPU 材质接入或随视角修脸，阶段 1 保持未完成，优先审阅上述三项目标。

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
