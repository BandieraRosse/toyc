# HG-2C：mixed 帧架构与性能基础设施

> 文档更新：2026-09-20
> 源码核对基线补充：2026-09-20 长时实机修复：frame slot 不再各自持有 swapchain；唯一 swapchain/图像属于 Vulkan backend presenter，slot 仅持有自己的 acquire/render-complete semaphore 与离屏资源。Intel 上按 image 复用完成 semaphore 且不等待 present 的实现会在 30--60 秒内停滞；当前 present 后恢复 queue-idle 作为正确性门槛。300/300 strict native、零 fallback/readback/CPU copy，历史 timestamp 到 frame 298。
> 源码核对基线：2026-09-20；HG-2C1 至 HG-2C4 已完成。两个完整 mixed frame slot 分别拥有 Raster/Graphics target、上传 buffer、command buffer、render fence 与 timestamp query pool；normal native submit 后不等待本帧 fence，复用 slot 时才回收。资源 pin 使用跨帧引用计数并由 slot fence 完成后释放，`mixed-gpu frame=` 报告被回收的历史帧编号。Intel strict native 120 帧、专用 mixed gate 与四 extent 140 帧 resize gate 均通过。

HG-2C 位于 HG-2B 与 HG-3A 之间。它不扩大 hardware Draw 的内容 allowlist，而是先消除当前 mixed
帧的固定全屏搬运与单帧同步成本，避免 HG-3 至 HG-5 建立在双向 bridge 架构上。

## 当前问题

1280×720 的一个正常 mixed 热帧仍执行两次 depth bridge，传输 14,745,600 bytes：

```text
shared RGBA8 storage/color attachment + Raster depth buffer
→ graphics color/depth attachment
→ indexed Draw
→ shared RGBA8 storage/color attachment + Raster depth buffer
→ transparent/effects/viewmodel/Post/overlay/present
```

depth bridge 包含 D32/inverse-Z 转换、全屏 depth copy、barrier 和资源状态往返。正常呈现仍在帧末调用
`vkQueueWaitIdle`，因此当前单帧资源不能安全跨帧复用，CPU 与 GPU 也不能并行处理相邻帧。

## Checkpoint

| 阶段 | 状态 | 交付 |
| --- | --- | --- |
| HG-2C1 | 完成 | mixed CPU 分项，以及 Raster、bridge import、Draw、bridge export、Post、overlay、swapchain copy 的 Vulkan timestamp |
| HG-2C2 | 完成 | compute Raster、graphics Draw 与 Post 共享 RGBA8 storage/color attachment；双向整屏 color copy 已取消，diagnostic readback 独立 |
| HG-2C3 | 完成 | 前段 Raster、Draw、后段 Raster、Post、overlay 与 present copy 使用统一 frame command context |
| HG-2C4 | 完成 | 两个完整 frame slot、延迟 fence/timestamp 回收和延迟资源 unpin；strict 120 帧、mixed gate 与四 extent resize gate 通过 |

HG-2C 完成后才进入 HG-3A。最低门槛是正常帧不再双向搬运完整 color、正常 present 后不调用
`vkQueueWaitIdle`、至少双帧在途，并能用 GPU timestamp 区分 Raster、bridge、Draw、Post、overlay
与 swapchain copy 的设备执行时间。

## HG-2C1 计时合同

`--frame-audit` 的 `mixed-cpu` 行报告：

- `freeze_ms`：Core 冻结有序 mixed span。
- `cache_collect_ms`：GPU resource cache 回收检查。
- `preflight_ms`：完整 mixed 预检总墙钟；它包含下面的纹理、打包与 Draw 编码子项，不能与子项相加。
- `texture_measure_ms`：RasterCmd 唯一纹理测量。
- `pack_ms`：Raster ABI stream 与纹理表内容打包。
- `draw_encode_ms`：Draw 编码、cache prepare/bind 与 graphics 数值预检。
- `draw_batch_prepare_ms`：Draw span 的 resource/batch 表准备。
- `graphics_draw_ms`：graphics bridge import、indexed Draw、bridge export 与 queue submit 的 CPU 调用墙钟。
- `raster_segment_ms`：所有 Raster segment 调用的 CPU 墙钟总和，包括最终 fence/present 等待。

这些字段是 CPU 墙钟而不是 GPU shader 时间。`graphics_draw_ms` 与 `raster_segment_ms` 互不嵌套；
`preflight_ms` 是总项，其三个子项用于解释组成。后续 GPU timestamp 使用独立的 `mixed-gpu` 行，
不复用这些字段名。

## 实现入口

- mixed 计划、pack 与分项累计：`gpu/src/rf_gpu_mixed_executor.c`
- Core freeze 与逐帧 delta：`rasterfall/src/rf_core_host.c`
- `--frame-audit` 输出：`rasterfall/src/rf_game_runtime.c`
- graphics bridge/Draw：`gpu/src/rf_gpu_vulkan_graphics.inc`
- Raster/Post/overlay/present：`gpu/src/rf_gpu_vulkan_backend.c`

`--frame-audit` 的 `mixed-gpu` 行是设备 timestamp：

- `supported`：物理设备、graphics+compute queue 和 Vulkan 入口共同支持 timestamp。
- `valid`：本帧 query 已在最终 fence 后成功读取。
- `raster_ms`：帧内所有 compute Raster segment 的设备执行时间总和。
- `bridge_import_ms` / `bridge_export_ms`：Raster storage buffer 与 graphics attachment
  之间两向 bridge 的设备执行时间。
- `draw_ms`：indexed Draw render pass 的设备执行时间。
- `post_ms`、`overlay_ms`、`present_copy_ms`：尾段 Post、overlay composite 和 buffer 到
  swapchain image copy 的设备执行时间。

query pool 属于 Raster target；CLEAR segment 重置本帧 query，跨 Raster 与 graphics command
buffer 写入成对时间戳，最终 fence 后读取并按类别聚合。不支持 timestamp 时正常 GPU 帧仍可运行，
审计输出 `supported=0 valid=0`。这些字段不能用现有 CPU 墙钟 `bridge_ms`、
`graphics_draw_ms`、`fence_wait_ms` 或 `native_total_ms` 替代。

Windows Intel 实机 1280×720 near/0、40 帧 strict native 验证为 40/40 GPU 帧、零 fallback、
零普通读回/CPU framebuffer copy；`mixed-gpu` 每帧均为 `supported=1 valid=1`。热帧观察到
Raster 约 10–16 ms、bridge import 约 1.0–2.3 ms、Draw 约 0.3–0.5 ms、bridge export
约 0.9–1.5 ms，说明下一步 HG-2C2 应优先消除完整 color bridge，并继续保留 depth 契约审计。

## HG-2C2 direct-color 首步

graphics color attachment 改用 `B8G8R8A8_UNORM`。在 little-endian 主机上，它与 Raster
`0xAARRGGBB` storage buffer 的字节布局一致，因此 mixed import/export 的 color 不再经过
`bridge_raster`、`bridge_transfer` 和 `swap_rb` compute；depth 仍通过 D32 ↔ inverse-Z bridge，
保持原遮挡合同。独立 HG-2A diagnostic roundtrip 继续保留完整 RGBA/depth 转换模式。

1280×720 normal mixed 帧的两次 bridge 总统计先由 29,491,200 bytes 降至 22,118,400 bytes，
即每个方向由 16 B/px 降为 12 B/px。Windows Intel near/0 40 帧 strict native 为 40/40 GPU、
零 fallback/readback/CPU framebuffer copy；热帧观察到 bridge import 约 0.8–1.2 ms、export
约 0.54–0.93 ms。该轮次未固定与 HG-2C1 完全相同的系统调度条件，因此只把结构性字节下降
作为严格结论，timestamp 区间作为观察值。

`--mixed-gate`、`HG-2B-core-executor`、Windows package、package `--logic-test` 均通过。此时 color
仍在 Raster buffer 与 graphics attachment 之间各复制一次，因此这里只是 HG-2C2 的历史中间状态；
最终共享 target 完成状态见下文。

bridge descriptor 的两个 storage buffer 在 target 生命周期内稳定，normal mixed import/export 也使用
相同 range。实现现在只在 buffer handle 或 range 改变时更新 descriptor set，避免每个 Draw span 两次
重复的 `vkUpdateDescriptorSets` CPU 调用。resize、独立 roundtrip 的双平面 range 与后续 mixed range
切换仍会触发更新；bridge copy、depth 转换和 22,118,400 bytes 统计均不变。

BGRA8 切换后重新核对了两条颜色路径。standalone HG-2A 的公开读回仍输出原 diagnostic RGBA 字节
合同：attachment 先复制到双平面 bridge，mode 0 compute 用 `swap_rb` 生成颜色读回，depth 保留 D32
float。normal mixed 则继续在 Raster `0xAARRGGBB` buffer 与 BGRA attachment 间直接复制 color，仅转换
depth；两条路径不能共用同一份“无需转换”的推断。

同时确认，先前让 mixed export 无 fence 提交、依赖后续 Raster/present fence 的做法并不安全：下一次
graphics span 会调用 `reset_command_pool`，可能重置仍在执行的同一 command buffer，连续 Draw 因而保留
前一次颜色。当前恢复 export fence wait；这不会改变 bridge bytes，但会恢复 CPU 同步成本。正确删除该
wait 的前提是 HG-2C3 统一 frame command context，或为并行提交提供独立且受 fence 保护的 command buffer。

## HG-2C2 shared color 完成状态

normal mixed 最终使用 `R8G8B8A8_UNORM` storage/color attachment。mixed preflight 在 resize 后把
image/view 绑定给 Raster target；Raster 与 Post 使用 image-color shader 变体，graphics load pass 以
`GENERAL` layout 接入同一 image。设备必须同时支持该格式的 storage image 与 color attachment；不满足时
strict mixed 初始化失败，不静默回退。

color 不再进入 import/export bridge。depth 仍按原合同执行 Raster inverse-Z buffer ↔ bridge buffer ↔
D32 attachment。1280×720、每帧两个方向的统计因此由 22,118,400 降为 14,745,600 bytes，即
8 B/px/方向；减少 7,372,800 bytes（33.3%）。descriptor 缓存不计入这一下降。

Post 直接从共享 image 读取；即使 Post mode 关闭，终段也通过 image Post 变体把共享 target 编码到既有
presentation buffer，overlay、capture 与 swapchain copy 的下游合同不变。resize 会重建 graphics image，
旧共享绑定随即失效；后续 Raster segment 前必须重新绑定，不能跨 resize 沿用旧 image。

standalone HG-2A 继续使用独立 diagnostic bridge/readback。attachment 改为 RGBA8 后，diagnostic bridge
按 RGBA attachment 字节合同读写，公开颜色与原 oracle 保持一致，不修改 oracle。

Windows Intel strict native near/0 46 帧验证为 46/46 `gpu-native`、零 fallback、普通 readback 与 CPU
framebuffer copy；每帧 2 次 bridge、14,745,600 bytes、`mixed-gpu supported=1 valid=1`。swapchain
仍报告 format 44；该值不是共享 color attachment 的格式。`graphics_waits=1` 仍是当前正确性同步。

## HG-2C3 统一 frame command recording

Raster target 现在拥有 normal mixed 单帧命令录制状态。首个非 final Raster segment 重置并开始主命令
缓冲，但不结束、不提交；graphics batch 临时借用同一个命令缓冲，把 depth import、indexed Draw 和
depth export 依次追加；尾段 Raster 再追加 Post、overlay 与 present copy，最后统一结束并提交。这样
不再重置仍在执行的 graphics/Raster command pool，也不需要 mixed 中间 graphics fence。

独立 HG-2A graphics/readback 诊断仍使用 graphics 自己的 command pool、submit 与 fence，不受 normal
mixed 借用路径影响。当前最终 Raster submit 仍同步等待 fence，native present 后仍调用
`vkQueueWaitIdle`；资源也仍只有一套，因此这一步不是多帧在途实现。

Windows Intel Iris Xe strict native near/0 120 帧通过，零 fallback、普通 readback 与 CPU framebuffer
copy；热帧 `graphics_submits=0 graphics_waits=0`，每帧仍为 2 次 depth bridge、14,745,600 bytes，
`mixed-gpu supported=1 valid=1`。专用 `rasterfall-gpu-mixed-test` 多 extent/双 Draw span 回归通过，并
断言稳态 graphics queue submit 不增长。

## HG-2C4 present queue-idle 移除增量

normal native present 不再复用单个 render-complete semaphore。swapchain 创建时按 image 数量分配
完成 semaphore；本帧 acquire 到的 image 决定 submit signal 与 present wait 使用哪一个。再次 acquire
同一 image 时，前一次 presentation 已释放该 image，因此对应 semaphore 可以安全复用。acquire
semaphore 仍由本帧 render fence 保护；swapchain 重建、surface 重建和 teardown 才显式
`vkQueueWaitIdle`，不进入正常帧热路径。

Windows Intel Iris Xe near/0 strict native 120 帧通过：120/120 `gpu-native`，零 fallback、普通
readback 与 CPU framebuffer copy；swapchain 为 3 images，所有帧
`native_present_queue_idle_ms=0.000`，热帧 `graphics_submits=0 graphics_waits=0`，bridge 仍为每帧
2 次、14,745,600 bytes。该增量只删除 present 完成等待；最终 render submit 仍同步等待 fence，
timestamp 也仍在同帧 fence 后收集，因此尚未形成双帧在途，HG-2C4 不标记完成。

## HG-2C4 双 frame slot 增量

mixed executor 轮换两个完整 Raster/Graphics slot。每个 slot 独立持有共享 color/depth target、Raster
上传与 presentation buffer、graphics bridge target、command pool/buffer、持久 render fence 和 timestamp
query pool。final native submit 后立即进入 present，不等待本帧 fence；CPU 继续构建下一帧，只在再次
选择同一 slot 时等待其旧 fence。resize 和销毁也先回收所有涉及的 slot。

timestamp 在 slot 回收时读取并归档，`FRAME-AUDIT mixed-gpu frame=N` 中的 `N` 是对应的已完成历史
帧，而不是当前正在提交的帧。前两个预热帧没有可回收结果，因此输出 `frame=0 valid=0`。

Core resource pin 改为引用计数。成功提交后，当前 CPU recording frame 结束，但 Draw mesh/material/texture
pin 转交给 GPU slot；slot fence 完成后按本帧 Draw handle 逐项释放。相邻在途帧引用同一资源时，前一帧
完成不会提前解除后一帧的保护。

Intel Iris Xe 最终 near/0、1280×720、Fog strict native 为 120/120 GPU 帧、零
fallback/readback/CPU framebuffer copy。前两帧为 timestamp 预热，从第 3 帧开始回收帧 1，最终第 120
帧报告帧 118；118 个历史帧结果全部 `supported=1 valid=1`。热帧
`graphics_submits=0 graphics_waits=0`，所有帧 `native_present_queue_idle_ms=0.000`。

同一最新 package 的 `hardware_graphics_proof.ps1 -MixedGate` 与
`hardware_graphics_resize.ps1 -NoRedirect` 均通过；resize 覆盖 140 帧和四个 extent，资源 loads 稳定，
在途 pin 不超过 live resources。resize 门禁不再要求每帧 `pinned=0`，因为双帧在途时 fence 未完成的
Draw 资源应保持 pin；销毁、resize 与 slot 复用负责在安全点释放。

固定视角热帧 17--120 的观测值：`whole_loop_ms` 中位数/P95 为 22.925/25.835，
`render_ms` 为 16.293/18.855，`present_wall_ms` 为 4.549/5.521。GPU timestamp 中位数/P95：
Raster 10.835/13.838 ms、depth import 0.522/0.602 ms、Draw 0.313/0.365 ms、depth export
0.442/0.517 ms、Post 0.315/1.275 ms、overlay 0.075/0.096 ms、present copy 0.106/0.152 ms。
这些是单次 Intel 实机观测值，不替代跨机器性能基线。

## HG-2C4 长时稳定性修复

最初的双 slot 实现错误地把 swapchain、swapchain images 和 present completion semaphore 复制进每个
Raster target，导致两个 slot 针对同一窗口 surface 交替 acquire/present。移动画面因此可能在两条
呈现时间线之间切换旧帧；长时运行还会停在呈现同步链。修复后 swapchain 与 images 由 Vulkan backend
唯一拥有，双 slot 只保留离屏资源、command/fence/query，以及各自的 acquire/render-complete semaphore。

Intel Iris Xe 实测还确认，render fence 只覆盖 graphics queue submit，不能作为 present 完成证明。
按 swapchain image 复用完成 semaphore、移除 queue-idle 的版本分别在第 39 帧和第 123 帧停滞；恢复
present 后 queue-idle，并按 slot 复用完成 semaphore 后，near/0 strict native 300/300 正常退出，零
fallback、普通 readback 与 CPU framebuffer copy，最终回收 frame 298。该修复优先保证正确性，当前
`native_present_queue_idle_ms` 重新成为非零成本；后续若再次异步化 present，必须引入可证明的 present
完成机制并通过至少 300 帧长时门禁，不能只依赖 render fence 或短 smoke。当前虽然保留两个资源 slot，
但 queue-idle 使 normal present 边界重新串行，不应再把该状态描述为两帧真实并发在途。
