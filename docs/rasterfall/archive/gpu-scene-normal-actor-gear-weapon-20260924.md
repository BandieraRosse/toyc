# 正常帧角色装备与武器 Scene WORLD 现场

> 状态：历史现场归档；离屏 Scene WORLD 诊断，不是正常帧 Scene 呈现验收
>
> 日期：2026-09-24

前一轮只将真实 session rifleman 的 body 与 HEAD 接入离屏 Scene WORLD。
整图对照表明胸口 RGB 差异来自缺少 CHEST 装备；加入 CHEST/BACK 后，
原先抽样的胸口 mixed/Scene 都是 `(80,93,65)`，面部都是 `(93,67,53)`。
AK 武器随后消费 pose extractor 已冻结的 RMESH 原始坐标到世界变换。该变换包含
authored centering、basis、geometry scale 和 grip 对齐；GPU 刚性 palette 对武器直接使用
`geometry_scale/1000`，不套用被动装备的 `RFU/position_scale` 换算。

正常帧审计从同一 frozen pose 解析并 pin body、三件被动装备及 AK 资源，
在地图和静态实例的同一离屏 Scene WORLD color/depth 提交 14 个角色 draw。
Campaign near 0 首帧共 971 draw，其中角色 14、地图和静态实例 957；
地图 GPU cache 首帧上传 937、命中 20，次帧上传 0、命中 957。
`actor-rifleman 0` 镜头首帧共 963 draw，其中角色 14、地图和静态实例 949；
首帧地图上传 933、命中 16，次帧上传 0、命中 949。
该镜头仍有 4 个静态实例数值预检暂缓；不能据此声明完整 WORLD 覆盖。

Windows package、逻辑测试和 RTX 3050 原生专项通过。
`tmp/scene-actor-gear-weapon-sync-20260924/manifest.json` 报告
`PASS - available gates`、`validation_sync: PASS`，可执行文件 SHA-256 为
`D6DBA249724DE8645724E395AC50023CE2527E171689F19EDE37BCCB102A7AE8`。
定向 mixed BMP 与 Scene PPM 位于 `tmp/scene-actor-weapon-scale-20260924/`；
胸口、面部及腿部五个选定像素 RGB 精确一致。该局部对照不等于完整视觉合同。

审计仍在 mixed present 后显式读回；其他角色、完整 WORLD、透明/effects、
viewmodel、overlay、正常 Scene swapchain 呈现和完整帧性能门禁仍待接入。
