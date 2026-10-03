# RF-C01 V25d 肩袖、头脸与分层短发修订

> 状态：历史，本轮创作与验证记录；正式角色包仍未签收
> 日期：2026-10-03
> 当前造型入口：[RF-C01 设计稿](../reference/rf-c01-design-study.md)

本轮从 V23a 的锁定部件继续修订，保留成熟动漫、成年外勤技术员方向。最终候选清单为
`rasterfall/private-assets/source/characters/rf_c01/authoring/assembly-head-v025d.json`，完整源、GLB、
RFM2、固定视图与验证报告位于同一创作根下的 `build/v025d/`。V23a、V24 系列与 V25 中间试样保留，
未覆盖原始部件、骨架、动作或摄影棚。私有几何、脚本和产物不加入 Git。

## 本轮实际改动

- **肩袖**：移除像独立护肩边缘的环形黑缝，降低袖山鼓包，调整肩前后厚度和上臂布料余量，
  让浅色肩胸裁片与袖子连成连续轮廓；身体其他细节沿用 V23a。
- **头脸**：收敛下颌两侧、支撑面颊和鼻翼过渡，轻微调整头顶弧度，缩短视觉颈长；眼眶与眼部组件
  同步变形，增大虹膜并重新贴合眼球，强化眉毛、上睫毛和低饱和唇色。
- **头发**：显式重建前发与侧后发拓扑；前发包含五组主体和两条细分支，侧后发用长主束与短覆盖束
  错开结束高度、束宽和发尾方向。薄实体闭合，采用新 UV 和 `RF_HEAD` 单骨权重；底发继续承托根部。
  深青中间色经正常 GPU 光照反馈调亮，保持原材质数量。
- **拓扑**：发现旧头部 n-gon 在姿态求值时可产生共线三角形。微调三个顶点并显式固定头部三角化，
  保留全部离线 shape key 和逐点变形关系，使动作采样不再依赖变化的 n-gon 三角划分。

V24 的头壳/眼部/颈长变化通过新接口文件声明，并串联所有依赖部件。V25 的新发束网格属于明确的
拓扑迁移；对象 ID、部件所有权、骨架及 attachment 不变。头发从原 procedural 配方显式转为冻结网格
编辑源，后续必须从候选清单继续，不能重跑旧头发配方覆盖本轮造型。

## 验证事实与边界

最终导出为 RFM2 v14，47,723 个导出顶点、78,260 个三角形、49 根骨骼、8 个 attachment、19 个材质。
通过保存重开、部件隔离、锁定旧源哈希、RFCHAR 输入门（0 error / 0 warning）和 Windows 原生双实例
隔离测试。完整角色的 idle、walk、aim 共 51 个离线采样均为有限坐标、非退化三角形；新前发/后发
无边界边，网格检查未发现松散顶点或三面共边。

原生导入显式恢复保真诊断已有的 eyes / hair / skin / clothing / equipment 材质类别，并逐材质核对
RFM2 角色字节；此前本地 V23a 身体产物曾漏传这一映射。头脸和身体皮肤仍按相同材质签名合并，因此
继续使用 skin 类别，不声称已经能够单独驱动脸部材质。

头脸、眼部和口腔的 Basis 与所有已有 shape key 使用相同空间变换，最终三角化也保留 shape 层。
另采样 Blink、MouthOpen、BrowDown、SmileL 的 0 / 0.5 / 1，共 12 组数值检查，并渲染半闭眼、
闭眼、半张嘴、眉部与去发结构图。它们是既有离线表情的适配和诊断，**没有新增游戏运行时表情**；
导出继续排除 morph。数值检查也不等于全部表情组合或所有部件交叠已签收。

图检包括固定正面、三分之四、侧面、背面、肩袖、全身、walk 和 aim。头发的大中小束与肩袖连接
已有改善；近距离仍能看出部分发束根部转接，脸部没有皮肤/眼部纹理，发丝材质、完整持枪动作和 LOD
仍是后续工作。本记录不把本轮改动表述为已达到商业角色最终完成度。

## 参考与复现

本轮继续采用已有[头型与短发参考](../reference/rf-c01-head-hair-references.md)和
[发束表面研究](../reference/rf-c01-hair-surface-study.md)。新核验包括
[Proko 颈部解剖正文](https://www.proko.com/course-lesson/how-to-draw-neck-muscles-anatomy-and-motion/)，
用于复核下颌、耳后与颈部的连接；以及作者发布的
[Boehmy 雕刻头发步骤](https://www.zbrushcentral.com/t/how-to-sculpted-hair/373187)、
[Leti M. Vila 头发教程](https://letimvila.artstation.com/projects/rR6laG)和
[Antoine Dupuis 头发教程](https://antoinedupuis.artstation.com/projects/rRqEAa)入口。
后面三项核对了作者说明与教学入口，未购买项目源文件；不声称审阅了完整付费课程。
所有模型改动均为本项目几何创作，没有复制参考角色的几何、纹理或动作。

私有复现链为 `scripts/build_v024c.py` → `build_v025b.py` → `finalize_v025c.py` →
`finalize_v025d.py`，依次生成不同的不可覆盖候选。最终检查使用 `validate_v025.py -- v025d`、
`native_v025.py v025d`，视图使用 `review_v024.py -- v025d all` 和 `review_expressions_v025d.py`。
Blender 脚本以 `--background --python-exit-code 1 --python <script> -- ...` 执行；原生脚本通过 Python
执行。运行前阅读[分部件工作流](../guides/character-parts.md)，不要覆盖已存在的版本产物。
