# GPU 渲染架构

> 文档更新：2026-09-22
> 源码核对基线：RB-0 Intel 最终签收、SKY 轴向朝向合同与 shared-color 生命周期修复工作区

本文只描述当前 GPU 渲染数据流与所有权。历史阶段、性能数字和故障排查过程见
[Hardware Graphics 归档](archive/hardware-graphics-2026-09/README.md)。

## 所有权与入口

| 职责 | 当前所有者 |
| --- | --- |
| world/角色/特效 producer 与层顺序 | `rasterfall/src/rasterfall_render.c`、`rasterfall/include/rasterfall_render_frontend.h` |
| 帧冻结、资源 pin、整帧 preflight 与执行编排 | `rasterfall/src/rf_core_host.c` |
| GPU service、mixed executor 与资源 cache | `gpu/src/rf_gpu_vulkan_backend.c`、`gpu/src/rf_gpu_mixed_executor.c`、`gpu/src/rf_gpu_resource_cache.c` |
| normal runtime 与 presenter 生命周期 | `rasterfall/src/rf_game_runtime.c`、`windows/src/window_sdl.c` |
| CPU reference 与 Raster ABI | `rasterfall/src/render/rasterfall_draw_reference.inc`、`rasterfall/include/rf_gpu_raster_abi.h` |

`rasterfall.c` 只负责进程、输入、固定步长主循环和顶层编排。渲染读取玩法/展示状态，
不修改 `toy_game` 的权威结果。

## Producer 与混合帧

producer 按 WORLD、EFFECTS、VIEWMODEL、POST、OVERLAY 的既定层序生成 retained frame。帧可混合
`Draw` 与 `RasterCmd`：前者由 graphics pipeline 执行，后者保留给动态、透明、特效及尚未迁移的几何。

Core 在任何 target 写入前冻结计划并完成整帧 preflight。required 模式下，unsupported、编码失败、
资源 generation 不匹配或 presenter 失败都会使整帧失败；不允许在已写入部分 GPU target 后切回 CPU。
不同命令段共享同一 color/depth target，并由统一 command recording 保持 Draw/Raster 的深度、顺序和
Post/overlay 语义。

RB-0 诊断在每个 mixed span 上保存 producer 身份。当前分类为 world/map、enemy body、enemy rigid
special、gear、weapon、transparent、effects、viewmodel 和 overlay；身份变化前只将 renderer 中已有
RasterCmd 送入 retained frame，从而形成可审计边界，不触发 GPU 执行，也不改变原命令顺序。每个
Raster/Draw 交替点另外记录 import/export、layer、color/depth traffic、前后 producer 与 frame-slot
target generation。该信息只用于测量，不扩展 Graphics 类型或改变深度/画面合同。

actor 裁剪现在用 renderer 的 command_filter 在 flush observer/consumer 之前处理本段，随后从新缓冲
零位置继续，actor 结束时处理尾段并解除作用域。原缺陷见 [RB-0 排查报告](gpu-rb0-investigation-20260922.md)。
mixed preflight 在 recycle 后按执行计划预留 query pool；requested/recorded/dropped 随 GPU frame 返回，
截断不得置 valid。代码与验证边界见 [RB-0 修复与续接](gpu-rb0-repair-20260922.md)。

## 资源生命周期

raw Raster 分段重传使用 `input_versions` 保留先前录制引用的 command/bin/texture backing，直至完成后
的新录制或销毁；冻结帧 preflight/upload reuse 仍共用一次上传。Graphics 的 `shared_rasters` 追踪
color attachment 借用方，resize 在借用帧完成后废弃旧录制并重绑定匹配 extent，下次必须 CLEAR；
任一方销毁都解除借用。专项合同与验证见 [RB-0 专项续接](gpu-rb0-special-20260922.md)。

模型 registry 拥有不可变 CPU bundle 和 stable handle/generation；GPU cache 拥有对应 device resource。
Core 在 begin-frame 固定本帧引用，frame slot 完成前不得释放。world 切换时旧 generation 进入 retired，
只有 pin 清零后才释放。resize 只重建 extent 相关 target/presenter 资源，不得重建稳定 world mesh。

双帧 slot 分别持有 extent target、command/fence/query 和动态 Draw backing；不可变 mesh/texture cache
由 executor/device 统一持有，同一设备上的 graphics slot 只建立各自 descriptor binding，不重复上传。
swapchain 由 backend 统一拥有；
acquire、render fence、present wait completion 分开跟踪。正常热路径禁止 queue-idle，recreate/teardown
才允许排空 queue。

RB-0 低扰动统计在 frame-slot recycle 调用外记录 render-fence wait，并与 graphics submit fence wait、
swapchain acquire/present 以及由 image reacquire 证明的 presenter completion 分开保存。GPU timestamp
属于完成的旧 slot，必须使用其 frame ID 回填对应 CPU frame；不能把当前 CPU frame 与刚返回的旧 slot
timestamp 直接比较。退出前尚未回收的末尾 timestamp 不进入 GPU 分位数。

graphics submit fence 统计聚合所有 `gfx_submit()` 调用者；正常 mixed bridge 只录制命令，
preflight 中每帧创建 skinned resource 的独立同步提交会计入该字段，并可能等待前一帧的队列工作。
CPU/GPU frame ID 对齐不能替代 wait 因果。gfx_submit 已按七类调用者统计，并记录提交时尚未确认完成的
最近 Raster frame；成功 wait/recycle 更新已确认完成水位。这只是队列先后关系，不是 GPU 时间分摊。
timestamp valid 现在要求 recorded=requested 且 dropped=0；末尾未回收样本仍不进入 GPU 分位数。

## 持久地图几何

partition ground、wall、opaque box、ramp、opaque platform 和 procedural boundary wall 按 world
generation 建立不可变 mesh，并使用局部坐标、顶点光照和资源 pin 生命周期。动态或透明 air-gate、
透明 platform、texture wall 继续使用 RasterCmd。可见 mesh 不替代玩法碰撞；地图文本、Runtime Map、
碰撞绑定和渲染几何仍是独立层。

## 角色 GPU skinning

CPU 继续拥有 pose、IK、socket、gear 和 weapon placement。mixed frame 按角色实例冻结 finalized
palette，以及 bind position/normal、BDEF influence 和索引数据。compute shader 将 skinning 输出写入
frame-slot device-local vertex buffer，普通 body Draw 直接绑定该输出。

正常 GPU skin 帧不生成 CPU-skinned reference。`--gpu-character-vertex-diff` 只在目标帧建立 reference
并精确比较实际 Draw backing；`--gpu-character-skinning-off` 是正式回滚边界，恢复 CPU-skinned vertex
upload，且不改变 pose/IK/socket 所有权。

## Presenter 与失败边界

Windows native presenter 使用同一 Vulkan device/queue 和唯一 swapchain generation。逐帧审计必须
保持 fallback、readback、CPU framebuffer copy、hot queue-idle、非法层转换和 poisoned presenter 为零。
设备/交换链重建发生在明确边界；required runtime 不把失败静默降级为软件呈现。

## 长期验证入口

- `tools/gpu_acceptance.ps1 -Quick`：日常 GPU 修改门禁。
- `tools/gpu_acceptance.ps1 -Full`：合并/发布前完整门禁。
- `tools/gpu_metrics.ps1`：从 frame audit 计算预热后 CPU 墙钟与 GPU timestamp 的 median/P95/P99/max，
  核对固定 tick 与关键计数范围，汇总 producer/bridge 范围和 whole-loop 相关性，并分类最慢 5% 帧。
- `tools/gpu_rb0_special.ps1`：串行执行 raw 分段/timestamp、真实 layer/sync validation、故障注入和 soak；
  schema 5 metrics 补充七类 submit/wait、互斥 CPU phase 及前序 GPU frame 关联。
- `rf-gpu-graphics-test`、`rf-gpu-raster-test`、`rf-gpu-raster-diff-test`、resource-cache test、
  mixed-executor test：底层合同的独立 hosted 门禁。

运行参数的完整清单始终以 package 中 `rasterfall.exe --help` 为准；当前硬件覆盖、限制与命令见
[GPU 当前状态](gpu-current-state.md)。
