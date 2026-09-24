# Scene WORLD 旗面字形切片（2026-09-24）

> 状态：历史现场归档；正常帧离屏 Scene WORLD 诊断，尚非统一正常呈现

本切片将旗帜短标签按值加入来源冻结帧，并按旧路径的 8×16 bitmap 字形、连续像素 run 合并和双面方向生成 GPU 网格。每面有标签的活动旗帜增加一项 WORLD opaque draw，字形与旗布共用 Scene 深度。资源按旗帜槽位缓存；标签变化时只替换该槽的字形网格。

Windows 原生 `test` 通过，逻辑回归覆盖标签按值隔离。`actor-standard` 固定镜头为 1091 draw，其中旗帜 15 draw、字形 5 draw；次帧地图 GPU cache 为 956 hit，字形资源保持复用。字形内部 `(653,263)`、`(663,264)` Scene/mixed RGB 完全相同；边缘像素仍受两条光栅路径覆盖规则影响，不作为精确门禁。原生脚本将这两个像素加入固定门禁。`tmp/scene-flag-text-sync-20260924/manifest.json` 记录 17 项 Windows 原生运行、RTX 3050（vendor 10de）及 validation/sync 全部 PASS。

当前正常呈现仍走 mixed；投射物、交互物、其他角色类别以及后续透明、特效、Viewmodel 和 Overlay 尚未接入完整 Scene。
