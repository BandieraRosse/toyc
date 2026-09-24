# Scene WORLD 旗帜几何切片（2026-09-24）

> 状态：历史现场归档；正常帧离屏 Scene WORLD 诊断，尚非统一正常呈现

本记录保存本次切片的证据；当前架构与计划仍以 [GPU 渲染架构](../architecture/gpu-rendering-architecture.md) 和 [活动计划](../plans/gpu-scene-renderer.md) 为准。

正常帧审计从 session 复制五面旗帜的 active、位置、颜色与选中状态。独立 Scene target 复用两份旗杆/旗布 GPU 几何，每面活动旗帜提交两项 WORLD opaque draw。旗面文字仍由 mixed 绘制；正常呈现没有切换到 Scene。

Windows 原生 RTX 3050：`NativeCodex.ps1 build`、`test` 通过，逻辑回归覆盖冻结值隔离、选中状态和非法数量的事务失败。`tools/gpu_scene_native.ps1` 的 17 项运行及 validation/sync 通过，原始证据在 `tmp/scene-flags-sync-20260924/manifest.json`。`actor-standard` 固定画面的旗杆 `(639,100)` 与旗布 `(650,275)` Scene/mixed RGB 完全相同。静态场景五面旗帜增加 10 项 draw；连续两帧资源上传增量为零。

已知剩余范围：旗面字形、投射物、交互物及其他 WORLD 动态来源尚未接入统一 Scene；Scene 当前只在 mixed 呈现之后作离屏审计，不能作为正常帧完成证据。
