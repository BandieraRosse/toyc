# HG-2C5：Windows Native Present 基线收口

> 状态：历史
> 归档原因：阶段完成或已由当前文档取代
> 当前入口：[GPU 渲染架构](../../architecture/gpu-rendering-architecture.md)

> 文档更新：2026-09-20
> 源码核对基线：2026-09-20 HG-2C5 已签收。Phase 2 image-reacquire 热路径、完整故障注入矩阵与 Intel 动态 soak 已通过。Windows futex 仿真改用动态解析的 `WaitOnAddress`/`WakeByAddressAll`，修复全局 condition variable 偶发丢失 renderer job 唤醒导致的 `done=7/8`；修复后固定 near 300/300 与动态 `--auto` 10000/10000（5分09秒）均无 watchdog。slot 只拥有 acquire semaphore，presenter 的每个 swapchain image 独立拥有 `render_finished`；hot queue-idle、fallback、readback 与 CPU framebuffer copy 均为零。Khronos validation + sync validation 覆盖固定 300 帧、五种故障注入与 teardown/recreate，零 VUID/SYNC-HAZARD。

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
| Phase 0 | 完成 | 建立 generation、owner、semaphore state、outstanding presents、hot/recreate idle count、资源高水位与 fail-fast invariant；`--frame-audit` 输出 `PRESENT-AUDIT` |
| Phase 1 | 完成 | slot 仅保留 acquire/fence；每个 presenter image 拥有 `render_finished`；固定窗口 300/300 strict native 通过 |
| Phase 2 | 完成 | image-reacquire 热路径、固定窗口 300/300、五种 fault injection、5分09秒动态 soak 与 Khronos validation + sync validation 通过；hot-frame queue-idle 为零 |

## 固定窗口边界

HG-2C5 假设应用请求的窗口尺寸保持不变，不再执行窗口 resize、最小化/恢复或多 extent 门禁。Windows
高层可能对窗口内容做缩放，窗口外观变化不能作为 Vulkan swapchain extent 已变化的证据；因此本任务只以
固定创建尺寸验证 presenter/image/slot 生命周期。真实窗口尺寸变化、DPI 缩放与 swapchain extent 重建若需
验证，另立平台任务并使用可直接观察 client extent 与 swapchain extent 的证据。

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

Phase 1 最低验证为 Windows build/package、`--logic-test`、Intel 固定窗口 strict native 至少 300 帧、
delayed timestamp、零 fallback/readback/CPU framebuffer copy、资源无持续增长、无旧帧闪回，
并加入 acquire OUT_OF_DATE、record failure、reset 后 submit failure、submit 后 present failure、
SUBOPTIMAL、双 slot 在途 recreate 与未完全 retire teardown 的 deterministic fault injection。recreate 用例由
故障注入直接驱动，不以窗口 resize 驱动。

Phase 2 在 Intel Iris Xe 上执行至少 300 固定帧与至少五分钟动态 gameplay soak，覆盖移动、转向、跳跃、
静止与连续射击；要求 hot queue-idle count 为零，无永久 acquire/present 阻塞、无 semaphore
reuse assertion，且 fallback/readback/CPU framebuffer copy 始终为零。Windows validation layer gate 必须
覆盖 300 帧、fault injection 与 teardown/recreate；`validation unavailable` 不等于 PASS。

Phase 2 当前固定窗口检查点：Windows package 与 `--logic-test` 通过；Intel Iris Xe 1280×720 near/0
strict native 300/300，`hot_queue_idle_count=0`、`native_present_queue_idle_ms=0.000`，帧 4 起稳定报告
`completion_source=IMAGE_REACQUIRED`。三个 swapchain image 在帧末保持 `PRESENT_PENDING`，因此稳态
`outstanding_presents=3` 是预期的 presentation 在途数量；每个 image 再次 acquire 时先记录 retire generation，
再将其 `render_finished` 变回 reusable 并用于本次 submit。全程 presenter generation=1、零 poison、零
fallback/readback/CPU framebuffer copy。该检查点不替代剩余动态 soak、fault injection 与 validation。

故障注入使用 `--gpu-present-fault <name> [frame]`，frame 为从 1 开始的 native-present attempt，默认 1，
每次进程只触发一次。当前名称为 `acquire-out-of-date`、`record-failure`、`submit-failure`、
`present-out-of-date` 与 `present-suboptimal`。每次触发输出 `PRESENT-AUDIT fault-injection=...`；
record/submit 用例预期 strict 进程失败并由 teardown drain，acquire/present/SUBOPTIMAL 用例预期走保守
recreate 并继续。入口已通过 Windows package 编译与 `--logic-test`，尚不能记为实机 fault gate PASS。

2026-09-20 继续验证结果：Windows package 与 package 内 `--logic-test` 通过。固定窗口 300 帧复跑完成
193 帧审计后，下一帧 CPU raster worker 长期停在 `done=7/8`；已完成的 193 帧仍为 generation 1、
`hot_queue_idle_count=0`、`presenter_poisoned=0`、零 fallback/readback/CPU framebuffer copy。第 5 次
present attempt 注入 `acquire-out-of-date` 与 `present-out-of-date` 均完成 20/20，重建到 generation 2，
`recreate_queue_idle_count=1`，热路径 idle 仍为零；`submit-failure` 输出注入审计并按 GPU-required contract
失败。`present-suboptimal` 与 `record-failure` 的本轮进程被同一 `done=7/8` worker 卡死阻断，不能判定
PASS/FAIL。validation layer 尚未执行。原始本地日志位于 `tmp/hg2c5-phase2/`，不提交。

随后定位到 Windows `__futex()` 使用进程级 condition variable 模拟任意 futex 地址；renderer worker 的
generation 比较与休眠之间可丢失唤醒，使一个 idle worker 永久停放。Windows runtime 现动态解析
`WaitOnAddress`/`WakeByAddressAll` 并按实际地址等待。修复后的当前 package：固定 near 300/300，动态
`--auto` 10000/10000，墙钟 5分09秒；动态模式覆盖交替前后/横移、每 90 帧跳跃、持续转向与射击，保留
每 60 帧场景传送。两轮均零 watchdog、fallback、readback、CPU framebuffer copy 与 hot queue-idle。
`acquire-out-of-date`、`present-out-of-date`、`present-suboptimal` 均在第 5 次 attempt 重建到 generation 2
并完成 20/20；`record-failure`、`submit-failure` 均在第 5 次 attempt 按 GPU-required contract 以退出码 3
失败，teardown 正常结束。系统仍未列出 `VK_LAYER_KHRONOS_validation`，所以 validation 状态保持
UNAVAILABLE，而不是 PASS。原始日志继续位于 `tmp/hg2c5-phase2/`，不提交。

最终签收使用仓库本地保留、此前 HG-2A/HG-2B 已验证过的 Khronos validation layer，通过进程私有
`VK_LAYER_PATH` 与 `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` 加载，并设置
`VK_VALIDATION_VALIDATE_SYNC=1`。首轮 validation 暴露三项此前未被软件审计覆盖的问题：clear/load
render pass 的 external dependency 不兼容却共享 framebuffer/pipeline；统一 command recording 中后续
segment 更新已绑定 descriptor set，导致已录制命令失效；acquire semaphore 仅等待到 TRANSFER，未覆盖
更早的 swapchain image layout transition。当前实现统一两个 render pass 的保守 dependency，每个录制
segment 使用独立 descriptor set，并从 TOP_OF_PIPE 等待 acquire semaphore。

修复后的 Intel Iris Xe 最终矩阵：固定 near 300/300 退出码 0；动态 `--auto` 10000/10000 在
7分40秒完成，零 watchdog/fallback/readback/CPU framebuffer copy/hot queue-idle；`acquire-out-of-date`、
`present-out-of-date`、`present-suboptimal` 第 5 次 attempt 重建并完成 20/20，退出码 0；
`record-failure`、`submit-failure` 第 5 次 attempt 按 GPU-required contract 退出码 3。六项均确认实际插入
Khronos instance/device layer，零 Validation Error、VUID 与 SYNC-HAZARD；正常帧仍为零
fallback/readback/CPU framebuffer copy/hot queue-idle。最终原始日志与汇总位于
`tmp/hg2c5-phase2/validation-final-*`，不提交。

## 性能与厂商路径

正确性签收后才重建性能基线，同时记录 whole loop、render、acquire、slot fence wait、submit、present、
hot/recreate queue-idle，以及 Raster/depth import/Draw/depth export/Post/overlay/present copy 的 GPU 时间。
FIFO backpressure 可能迁移到 acquire 或 slot recycle，结论必须比较吞吐、CPU/GPU overlap、pipeline
depth、frame pacing 与 P95/P99，不能只比较 present wall time。

Intel 冻结后，RTX 3050 首先强制使用完全相同的 image-reacquire 基线路径并通过固定窗口 soak、fault
injection、validation。maintenance1、present fence、present id/wait 只作为后续可关闭增强；共享同一
presenter ownership，image-reacquire 永久保留为 reference path。

## 完成标准

仅当 Windows Native 达成 1 HWND、1 surface、1 active swapchain generation、2 frame slots、slot-owned
acquire、image-owned render-finished、hot queue-idle=0，且 Intel 固定窗口五分钟动态 soak、fault injection、
validation 与资源/画面/同步门禁全部通过，HG-2C5 才标记 COMPLETE。NVIDIA present-fence/present-wait
优化是后续独立阶段，不阻塞本 checkpoint。
