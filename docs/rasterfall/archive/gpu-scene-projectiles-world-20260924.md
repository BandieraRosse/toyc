# Scene WORLD 动态投射物切片（2026-09-24）

> 状态：历史现场归档；正常帧离屏 Scene WORLD 诊断，尚非统一正常呈现

本切片按玩法 projectile slot 冻结活动投射物的 kind、位置、高度、`age_ms`、闪烁状态及 V2 光照。Scene 仅在有投射物时加载 bomb/molotov 两份 RMESH GPU 资源；常规纹理态使用 `model_diffuse.ttex`，闪烁态使用模型材质纯色。旋转与缩放由每帧实例参数求值，和地图、角色、旗帜共用离屏 WORLD color/depth。

Windows 原生 `test` 通过，逻辑回归覆盖多 slot 顺序、冻结值隔离和非法 kind 的事务失败。显式 `projectile` 镜头固定放置有纹理 bomb、闪烁 bomb 和 molotov。Scene/mixed 截图中三个模型内部的 `(150,480)`、`(600,480)`、`(1040,440)` RGB 完全相同。该镜头为 1117 draw，其中 3 个投射物 draw；第二帧模型资源未重新上传。`tmp/scene-projectile-sync-20260924/manifest.json` 记录 18 项 Windows 原生运行、RTX 3050（vendor 10de）及 validation/sync 全部 PASS。

此证据仍只覆盖正常 mixed present 后的独立 Scene 审计。交互物、其余角色类别、地图透明、特效、Viewmodel 和 Overlay 未接入完整 Scene；正常呈现尚未切换。
