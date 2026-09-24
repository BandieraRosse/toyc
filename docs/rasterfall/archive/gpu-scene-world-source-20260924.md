# GPU Scene Runtime Map 来源接线现场

> 状态：历史现场归档；只读元数据，不是 WORLD opaque 执行证据
> 日期：2026-09-24
> 当前入口：[活动计划](../plans/README.md)、[迁移接口](../plans/gpu-scene-interface.md)

`rf_gpu_scene_world_capture` 从实际 Runtime Map render 取得 authored ID，并复用正式
`level_map.draw` 的投影顺序。正常帧的非 client `--frame-audit` 将这些 world 值与 session local actor
一起冻结到 V2 snapshot，再做有序 Scene 元数据提取。源数组没有 GPU handle 或 Runtime Map 指针。

Windows native `NativeCodex.ps1 test` 通过；逻辑回归加载正式 Campaign 与独立渲染地图，检查
ID/ordinal、air gate 开关时的可见性以及 box 48 / platform 96 alpha。
`NativeCodex.ps1 run --renderer gpu-compute --gpu-required --gpu-native-present --gpu-normal-scene near 0
--gpu-normal-fixed-tick --frame-audit --frames 3` 退出 0，首帧记录
`SCENE-LOCAL ... items=84 world_items=83` 与 `FRAME-AUDIT ... path=gpu-native`；
`GPU-FRAME` 记录 3 帧、零 fallback。该产品帧的 WORLD 仍由 mixed executor 提交，
本次值 snapshot 不含地图几何/材质，也未消除 bridge。
