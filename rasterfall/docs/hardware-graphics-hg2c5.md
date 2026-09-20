# HG-2C5：Windows Native Present 基线收口

> 文档更新：2026-09-20
> 源码核对基线：2026-09-20 Phase 0 已建立 Intel Iris Xe 实机检查点；当前仍保留 hot-frame `vkQueueWaitIdle`，新增 presenter/image/slot generation、semaphore 软件状态、outstanding present、queue-idle 分类计数与 `PRESENT-AUDIT`，并补齐 acquire 后失败、submit 前 invariant、queue-idle failure 与 surface-lost 后重建的 poison 边界。Phase 1 ownership 搬迁和 Phase 2 hot wait 移除尚未开始。

HG-2C5 在 Windows Native Vulkan 下冻结唯一 presenter、双 frame slot 与可证明的呈现生命周期。
Intel Iris Xe 是最低能力与最终签收基线；NVIDIA/AMD 首先必须运行相同的 image-reacquire 基线路径。
WSL 与 llvmpipe 不属于 GPU backend 正确性、兼容性或性能验收范围。

## 冻结 ownership

一个 Win32 `HWND` 只允许一个 active presenter generation：它拥有 surface、swapchain、swapchain
images，以及 Phase 1 后每个 image 独立的 `render_finished` binary semaphore。两个 frame slot 只拥有
acquire semaphore、render fence、command/query/offscreen/upload resources、resource pins 与 submitted
frame/generation。禁止恢复 per-slot swapchain，也禁止让 slot 拥有 present timeline 或 swapchain image。

必须区分三种 completion：

- render submission completion：仅由 slot render fence 证明，可回收 command/query/upload/pins；
- swapchain image reacquire：同 generation 的 image 再次 acquire 后，基线路径才允许重新 signal 该
  image 的 `render_finished`；
- display/presentation completion：Intel 基线路径没有独立证明，不将 reacquire 命名为 display complete，
  也不据此计算真实 input-to-display latency。

状态名使用 `PRESENT_PENDING`、`PRESENT_RETIRED`、`IMAGE_REACQUIRED`，不使用含义过强的
`DISPLAY_COMPLETE`。

## 执行阶段

| 阶段 | 状态 | 交付与门禁 |
| --- | --- | --- |
| Phase 0 | 进行中 | 保留 hot queue-idle；建立 generation、owner、semaphore state、outstanding presents、hot/recreate idle count、资源高水位与 fail-fast invariant；`--frame-audit` 输出 `PRESENT-AUDIT` |
| Phase 1 | 未开始 | slot 仅保留 acquire/fence；每个 presenter image 拥有 `render_finished`；submit 成功立即把 command/query/upload/pins 交给 slot fence；补齐 acquire 后失败、submit 失败、present failure poison/drain/recreate；仍保留 hot queue-idle |
| Phase 2 | 未开始 | 同 generation 同 image 再次 acquire 作为 image semaphore 可复用证明；删除 hot-frame queue-idle，recreate/teardown 保留 slow-path drain |

Phase 0 当前 Intel Iris Xe 实机证据：固定 near 场景 strict native 120/120 帧通过，两个 slot 逐帧交替，
swapchain/image generation 始终配对；每帧 acquire 与 render-finished 软件状态回到 reusable，image 为
retired，`outstanding_presents=0`，hot queue-idle 从 1 递增到 120。四 extent resize gate 140/140 帧通过，
presenter generation 从 1 增至 4，recreate queue-idle 从 0 增至 3；全程无 invariant failure、poison、
fallback、readback 或 CPU framebuffer copy。该检查点不替代 Phase 1 的 300 帧、validation 与 fault
injection 门禁。

Phase 0 fail-fast invariant：同时只有一个 active swapchain generation；image index 必须与 generation
配对；binary semaphore 未 reusable 前不得再次 signal；slot acquire 必须被对应 submit wait 消费；
slot fence 完成前不得回收 command/query/pins；present failure 后 poison generation，不再复用其 image
synchronization。

## Failure-path transaction

`vkQueueSubmit == VK_SUCCESS` 是 ownership transaction 边界。成功后不依赖 present 结果，command
buffer、query、upload、resource pins 与 submitted frame 立即归 slot fence。acquire 成功但 submit 前失败
会 poison acquire semaphore，进入 drain/recreate；submit 后 present 返回 OUT_OF_DATE、SURFACE_LOST 或
无法证明已正常排队的错误时 poison presenter generation。SUBOPTIMAL 必须采用明确、保守的 generation
策略。

## 验证顺序

Phase 1 最低验证为 Windows build/package、`--logic-test`、Intel strict native 至少 300 帧、四 extent
resize、delayed timestamp、零 fallback/readback/CPU framebuffer copy、资源无持续增长、无旧帧闪回，
并加入 acquire OUT_OF_DATE、record failure、reset 后 submit failure、submit 后 present failure、
SUBOPTIMAL+resize、双 slot 在途 recreate 与未完全 retire teardown 的 deterministic fault injection。

Phase 2 在 Intel Iris Xe 上执行至少 300 固定帧与至少五分钟动态 gameplay soak，覆盖移动、转向、跳跃、
静止、连续射击、反复 resize；要求 hot queue-idle count 为零，无永久 acquire/present 阻塞、无 semaphore
reuse assertion，且 fallback/readback/CPU framebuffer copy 始终为零。Windows validation layer gate 必须
覆盖 300 帧、resize、fault injection 与 teardown/recreate；`validation unavailable` 不等于 PASS。

## 性能与厂商路径

正确性签收后才重建性能基线，同时记录 whole loop、render、acquire、slot fence wait、submit、present、
hot/recreate queue-idle，以及 Raster/depth import/Draw/depth export/Post/overlay/present copy 的 GPU 时间。
FIFO backpressure 可能迁移到 acquire 或 slot recycle，结论必须比较吞吐、CPU/GPU overlap、pipeline
depth、frame pacing 与 P95/P99，不能只比较 present wall time。

Intel 冻结后，RTX 3050 首先强制使用完全相同的 image-reacquire 基线路径并通过 soak、resize、fault
injection、validation。maintenance1、present fence、present id/wait 只作为后续可关闭增强；共享同一
presenter ownership，image-reacquire 永久保留为 reference path。

## 完成标准

仅当 Windows Native 达成 1 HWND、1 surface、1 active swapchain generation、2 frame slots、slot-owned
acquire、image-owned render-finished、hot queue-idle=0，且 Intel 五分钟动态 soak、resize、fault injection、
validation 与资源/画面/同步门禁全部通过，HG-2C5 才标记 COMPLETE。NVIDIA present-fence/present-wait
优化是后续独立阶段，不阻塞本 checkpoint。
