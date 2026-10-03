# RF-C01 V26f 贴图、头脸与发根创作记录

> 状态：历史，本轮私有内容创作与导入记录；不是完整角色包签收
> 日期：2026-10-03
> 当前造型入口：[RF-C01 设计稿](../reference/rf-c01-design-study.md)

本轮从 V25d 的锁定部件继续，最终默认候选为
`rasterfall/private-assets/source/characters/rf_c01/authoring/assembly-eyes-v026f.json`。
完整源、图片、原始导出、运行 GLB 和验证报告位于同一创作根的 `build/v026f/`。
V25d 及全部中间候选保留；私有几何、贴图、脚本与产物不加入 Git。

## 实际内容改动

- 轻调鼻根、鼻翼、口周及下颌支撑，保留成熟动漫的成人比例；空间变换同时应用于 Basis 和全部已有离线表情。
- 收短耳周长束、减弱后发中段的叶腹，平顺公共发根体积；发束根端进入公共底发，后部底壳收进可见发束之下，
  中央主束建立更明确的叠压。沿用 V25 的闭合网格、对象身份、权重与拓扑，未再次重建头发拓扑。
- 针对原生中性脸略显瞪眼的反馈，将头部/眼睑的 Basis 向原 `BlinkMid` 移动 24%，最大位移约 1.39 mm。
  原 `BlinkMid` 和 `Blink` 终点逐点保留，其他表情目标加入相同 Basis 位移；眼球、虹膜位置不变。
- 身体保留 V25d 的顶点坐标、连接关系和权重，仅重做 UV 与材质归属。脸和身体皮肤有独立材质身份，
  不再因共享皮肤签名而合并；骨架、静止变换、attachment、既有离线动作均不变。

头发粗糙度为 0.58，减少大面积塑料般的反光；虹膜与眼白、皮肤、织物、靴与金属小件使用不同粗糙度常量。
没有加入透明发片、法线贴图、MR 贴图或逐根几何毛发。近看仍能辨别部分实体发束交叠，服装织纹保持克制，
本轮不等于商业角色最终完成度。

## 五张基础色贴图

| 图片 | 尺寸 | 内容与 UV |
| --- | --- | --- |
| `rf_c01_face_basecolor.png` | 1024² | 正面居中的柱面 UV，面颊/耳周暖色、鼻部与唇部色彩，微弱肤色变化 |
| `rf_c01_skin_basecolor.png` | 512² | 身体皮肤的低对比色彩变化，与脸部独立材质 |
| `rf_c01_eyes_basecolor.png` | 512² | 琥珀虹膜纤维、瞳孔与外圈，逐眼局部 UV |
| `rf_c01_hair_basecolor.png` | 1024² | 八个纵向发束色彩变体，保留发束 UV 流向 |
| `rf_c01_garment_basecolor.png` | 1024² | 四区中性色织纹/皮革/装备表面，材质线性因子负责最终配色 |

贴图由本项目私有脚本从零生成，内容表示色素和织纹，没有烘焙摄影棚明暗；没有下载或复制参考作品的像素。
RGB 以线性值创作，写 PNG 时编码为 sRGB，alpha 恒为 255。PNG 内嵌于 GLB，UV0 落在 [0,1]，
采样器为 clamp-to-edge / linear / trilinear。粗糙度和金属度是 glTF 常量，服装 tint 在纹理采样后按线性值相乘。

Blender 5.2 的旧 `MixRGB` 节点没有被当前 glTF 导出器识别为颜色因子；V26b 改用 `Mix` RGBA Multiply，
逐材质核对实际 `baseColorFactor` 后继续。原始 GLB 含五张图片及十个纹理别名；私有规范化步骤按
`(source image, sampler)` 去重，并同步全部材质索引，输出独立 `-runtime.glb`。原始导出保留，BIN 字节、
几何、图片、采样器和各材质实际解析结果保持不变；对应哈希和映射见 `texture-normalization.json`。

## 独立动漫头部对照

`authoring/assembly-mouth-v026_anime_f.json` 只改变头、眼和嘴，扩展眼眶开口、轻缩中庭并减少鼻部前突。
其头发、身体、材质、纹理、摄影棚、曝光与相机和成熟主线一致；文件和四视图在 `build/v026_anime_f/`。
这是可回退的独立风格试样，未安装为默认角色；不把“大眼”本身视作质量提升，也不据此替代成熟主线。

## 验证与范围

默认源通过保存重开、部件隔离、旧源哈希和接口检查；逐项核对身体坐标/权重/连接记录与 V25d 相等。
检查网格无松散顶点、三面共边及退化面，前发和后发保持闭合。idle、walk、aim 共 51 个离线骨骼采样
坐标有限且三角形非退化；Blink、MouthOpen、BrowDown、SmileL 各 0 / 0.5 / 1 共 12 组离线表情数值检查。
另有实际半闭眼、闭眼、张嘴、眉部和去发结构图；这些不是新增游戏表情，运行 GLB 仍排除 morph。

统一导入目标为 RFM2 v15，43,743 个导出顶点、78,260 个三角形、49 根骨骼、8 个 attachment、13 个材质及
5 个 TTEX。源 RFCHAR 检查无错误或警告；安装后逐材质核对角色字节、纹理槽、粗糙度及金属度，五张 TTEX
分别通过 `toyasset validate`。具体文件哈希见 `installed-validation.json`。

本记录的范围是内容创作、离线形变、源合同与统一导入；游戏持枪/服装能力和最终 GPU 验收由本轮系统工作记录。
Blender 固定灯光图不能代替正常游戏光照，离线非退化检查也不证明所有姿态没有衣物或部件穿插。

## 复现入口

先读[分部件工作流](../guides/character-parts.md)，从清单指向的冻结部件继续；不重跑旧程序头发配方覆盖本轮造型。
私有主线脚本顺序为 `build_v026b.py`、`build_v026c.py`、`finalize_v026d.py`、`finalize_v026e.py`、
`finalize_v026f.py`，每步使用不同版本目录；已存在的版本不得覆盖。纹理模块为 `texture_v026b.py`。
更动漫对照由 `build_v026_anime_f.py` 从 V26e 生成，保留成人身体尺度。

`validate_v026.py -- v026f` 核对源与离线采样，`review_head_v026.py -- v026f`、
`review_expressions_v026.py -- v026f` 使用固定摄影棚。`normalize_textures_v026.py v026f` 输出运行 GLB，
随后以同目录 `rf_c01_v026f.asset.json` 运行统一 `import_asset.py`，使用 `--character-surface`、
`--position-scale 65536` 和明确的 `visual-roles.json`；`audit_installed_v026.py v026f` 核对安装产物。
