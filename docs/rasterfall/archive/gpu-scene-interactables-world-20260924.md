# Scene WORLD 交互物切片（2026-09-24）

> 状态：历史现场归档；正常帧离屏 Scene WORLD 诊断，尚非统一正常呈现

正常帧按 session 来源槽位冻结交互物 kind、weapon、位置、效果实例高亮及 V2 光照，并沿用 PLAYING、暂停、商店的旧显隐条件。Scene 在同一 WORLD color/depth 中绘制七类模型拾取物以及按钮、药瓶、弹药盒程序几何。模型按 primitive 复用静态 GPU 资源；程序形体共享资源，特殊按钮底座按来源槽位和高度缓存。

Windows 原生构建与逻辑回归通过。Campaign `pickup` 固定镜头的值帧有 45 个交互物；7 个模型项提交 16 个 draw，38 个程序项提交 111 个 draw，延后计数 0。第二帧地图 GPU cache 上传数为 0。模型拾取物四个无遮挡采样点在 mixed BMP 与 Scene PPM 中 RGB 精确一致。`tmp/scene-pickup-sync-20260924/manifest.json` 记录 RTX 3050（vendor 10de）19 项运行、validation/sync 通过；其中包含生命周期、增长、resize、世界退休及五类故障注入。

程序几何当前使用共享形体与实例中心 V2 光照，旧路径的逐面颜色和逐顶点光照尚未完成定向容差审批。正常呈现仍使用 mixed；地图透明、其他角色类别、特效、Viewmodel、Overlay 和完整 Scene present 仍待推进。
