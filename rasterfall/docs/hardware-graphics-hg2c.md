# HG-2C：mixed 帧架构与性能基础设施

> 文档更新：2026-09-19
> 源码核对基线：2026-09-19；HG-2C1 已完成 mixed CPU 分项与 Vulkan GPU timestamp。Windows package 和 40 帧 strict native 审计通过；共享 target、统一 command recording 与多帧在途尚未实现。

HG-2C 位于 HG-2B 与 HG-3A 之间。它不扩大 hardware Draw 的内容 allowlist，而是先消除当前 mixed
帧的固定全屏搬运与单帧同步成本，避免 HG-3 至 HG-5 建立在双向 bridge 架构上。

## 当前问题

1280×720 的一个正常 mixed 热帧仍执行两次 color/depth bridge，传输 29,491,200 bytes：

```text
Raster storage buffer
→ color/depth graphics attachment
→ indexed Draw
→ Raster storage buffer
→ transparent/effects/viewmodel/Post/overlay/present
```

bridge 包含全屏 copy、compute 格式转换、barrier 和资源状态往返。正常呈现仍在帧末调用
`vkQueueWaitIdle`，因此当前单帧资源不能安全跨帧复用，CPU 与 GPU 也不能并行处理相邻帧。

## Checkpoint

| 阶段 | 状态 | 交付 |
| --- | --- | --- |
| HG-2C1 | 完成 | mixed CPU 分项，以及 Raster、bridge import、Draw、bridge export、Post、overlay、swapchain copy 的 Vulkan timestamp |
| HG-2C2 | 待开发 | compute Raster 与 graphics Draw 共享 color target，先取消 color 回程 bridge |
| HG-2C3 | 待开发 | 前段 Raster、Draw、后段 Raster、Post、overlay 与 present copy 使用统一 frame command context |
| HG-2C4 | 待开发 | 2–3 个 frame context；正常帧删除 `vkQueueWaitIdle` |

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
