# RF-C01 发束边界与底发参考

> 状态：造型研究与离线候选依据，不代表美术冻结
> 所有者：Rasterfall character art
> 核对日期：2026-09-29

本页补充[头型与短发研究](rf-c01-head-hair-references.md)，专门处理 V21f 的宽主束叶状边界和
底发壳体感。当前产物与验收范围以[设计稿](rf-c01-design-study.md)为准。

## 资料与图片

本轮实际下载并查看下列五张作者公开图片；本地研究副本位于 `tmp/hair-v022-references/`，
不加入角色模型、纹理或公开资源包。没有购买源模型，也没有复制参考几何。

| 来源 | 实际核验 | 对本角色的用途与限制 |
| --- | --- | --- |
| Ascalon / Aversion of Reality：[Flat Modeling Anime/Toon Hair](https://blenderartists.org/t/flat-modeling-anime-toon-hair/1204349) | 正文；[完整发型线框](https://blenderartists.org/uploads/default/original/4X/5/7/9/579c62fe2314e98375f86f9a2e04e68d715d7384.jpeg)与[展开的发体和末端拓扑](https://blenderartists.org/uploads/default/original/4X/f/e/f/fefd72343f6880f1961900eb4ebe6df951cf356a.jpeg) | 图中上部发体连续，下部才形成长短不同的分叉。借鉴连接关系，不照搬圆齐刘海、幼态比例或具体拓扑。没有逐帧观看其视频。 |
| Boehmy：[How to: Sculpted Hair](https://www.zbrushcentral.com/t/how-to-sculpted-hair/373187) | [Step 3 底体](https://www.zbrushcentral.com/uploads/default/original/4X/0/0/8/008e51269423fb4124ccf1496a785cc7c3a95e03.png)、[Step 4 体块](https://www.zbrushcentral.com/uploads/default/original/4X/0/e/1/0e173351c79570ef82dcbb166045799310a31ad7.png)、[Tips 曲线](https://www.zbrushcentral.com/uploads/default/original/4X/8/9/b/89bf22f227492bda30379b6b2282c341e3f5b219.png) | 底体是继续塑形的起点；后续体块承担流向变化，短发曲线可参考 C 形走势。图示为较长发型，不直接采用长度、厚度与夸张量。 |
| UmityStudio：[Blender 与 Stylized Hair PRO：前提知识和准备](https://note.com/umitystudio/n/n078b6fe5ef56) | 已读正文；图片未另行下载审阅 | 作者将用于生发的基础物体埋入头部，并说明通过埋根减少缝隙。其基础物体是工具的生发支撑，不等于本项目可见的 `hair_base`；不能把“埋球”直接当本项目的修形方案。 |
| Antoine Dupuis：[Stylized Hair Tutorial](https://antoinedupuis.artstation.com/projects/rRqEAa) | 核对作者页面和 PDF 入口；未下载完整 PDF、未购买项目文件 | 保留为后续雕刻层次和表面细节的学习入口，不声称已检查隐藏拓扑或全部教学内容。 |

## 对 V21f 的诊断

以下是本项目灰模、参数和网格的检查结论，不是参考作者的原话。

- 宽束在根部窄、中段鼓、末端收尖，两侧边界同时露出，形成独立闭合叶片。
- 底发与侧后发曾分别修形，实际外表面不再一致；单纯压边不能修复两者的曲率差。
- 底发下缘先于外层束尾成为一整圈可见边界；大量平滑顶部与局部贴片形成帽壳观感。
- 只埋掉上半束会产生突然露出的三角片；只把底体膨胀到旧束外侧又会抹掉层次。

## 修形与审阅原则

1. 头壳作为只读空间依据，底发与侧后发使用一致的贴合包络。发量不能靠独立壳体整体加厚。
2. 宽束保持顺流向的长面，弱化对称鼓肚；束根与承托面连续，下部才显露搭接和尖尾。
3. 底发承担上部连续体积，外层束承担下缘分叉。保留覆盖，避免以露头皮换取薄感。
4. 同时检查两侧、背面、顶视、剪影和固定几何转光；原材质检查阴影接线，灰模检查形体。
5. 改变整束走向时，旧法线与新法线的夹角不能单独判定翻面。需另查真实外／内表面朝向、
   退化面和同一束自身相交；部件间设计上的搭接与几何自身相交分开报告。

私有候选必须锁定生成器、只读头部和依赖部件，另存版本；原头脸、刘海、表情、身体、骨架、
审阅场景及空间接口通过语义隔离核对。离线结果不代表已完成游戏 GPU 材质或动作验收。
