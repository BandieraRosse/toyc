# Scene CPU backing 与 GPU 时间戳现场

> 状态：历史现场归档；独立三件套 fixture，不代表完整正常帧
> 日期：2026-09-24
> 当前入口：[活动计划](../plans/README.md)、[复现指南](../guides/gpu-scene-fixture.md)

Windows native package executable SHA256：`9D7648CEA2061B2CFA3851CB05588ECE25E38B7EA450D366AE52A0244E556201`。
设备为 NVIDIA GeForce RTX 3050 Laptop GPU，vendor/device `10de/25e2`。原始日志、manifest、
capture 保存在 `tmp/scene-timestamp-sync-fixed-20260924/`。

独立 Scene 专项完成 120 帧、增长、两次 resize、world 退休、五类 present 故障及 pose、logic、旧 normal-native
回归。CPU pack/upload 数组在普通帧、增长后及 world 退休后保持复用。119 条 `SCENE gpu-time` 均按冻结
frame ID 从 2 到 120 归属，`supported=1 valid=1`；区间仅包含 WORLD draw 和 swapchain blit，
不包含先前独立提交的 upload/skinning，也不能当作完整帧性能结论。

Khronos layer 由 loader 实际插入，脚本确认 Synchronization Validation 开启，manifest 的
`validation_sync` 为 `PASS`，没有 Validation Error、SYNC-HAZARD 或 VUID。第一次用相对
`-ValidationLayerDirectory` 运行时，子进程以 package 为工作目录，loader 找不到 layer manifest；
脚本现先解析为绝对路径，再运行相同专项通过。正常帧仍使用 mixed renderer，阶段 2/3 尚未验收。
