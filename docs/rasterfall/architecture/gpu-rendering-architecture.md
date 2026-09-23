# GPU 渲染架构

> 状态：当前
> 所有者：Rasterfall GPU renderer、Core Host 与 Windows presenter
> 最近核对：2026-09-23

本文只定义 GPU 渲染的稳定所有权、数据流和失败边界。验收命令见
[GPU 验收与诊断](../guides/gpu-validation.md)，设备档位和性能门槛见
[GPU 性能标准](../reference/gpu-performance-standards.md)，尚未完成的工作只见
[当前活动计划](../plans/README.md)。阶段调查、单次设备现场和已撤销实验位于
[GPU 2026-09-22 归档](../archive/gpu-2026-09-22/README.md)。

## 所有权

| 职责 | 所有者 |
| --- | --- |
| world、角色、特效 producer 与层顺序 | `rasterfall/src/rasterfall_render.c`、`rasterfall/include/rasterfall_render_frontend.h` |
| 帧冻结、资源 pin、整帧 preflight 与执行编排 | `rasterfall/src/rf_core_host.c` |
| GPU service、mixed executor 与资源 cache | `gpu/src/rf_gpu_vulkan_backend.c`、`gpu/src/rf_gpu_mixed_executor.c`、`gpu/src/rf_gpu_resource_cache.c` |
| normal runtime 与 presenter 生命周期 | `rasterfall/src/rf_game_runtime.c`、`windows/src/window_sdl.c` |
| CPU reference 与 Raster ABI | `rasterfall/src/render/rasterfall_draw_reference.inc`、`rasterfall/include/rf_gpu_raster_abi.h` |

`rasterfall.c` 只负责进程、输入、固定步长主循环和顶层编排。renderer 读取玩法或展示投影，
不修改 `toy_game` 的权威结果。地图文本、Runtime Map、玩法碰撞和可见几何保持分层。

## 混合帧数据流

```text
game/session presentation snapshot
    -> renderer producers
    -> retained frame (Draw + RasterCmd)
    -> Core freeze / pin / whole-frame preflight
    -> mixed executor on shared color/depth targets
    -> native presenter
```

producer 按 WORLD、EFFECTS、VIEWMODEL、POST、OVERLAY 的固定层序生成 retained frame。普通 opaque
static RMESH、持久 ground/map/boundary geometry 和角色 body 可进入 hardware Draw；动态、透明、特效、
viewmodel、overlay 及尚未满足数值合同的内容保留为 RasterCmd。

Core 必须在任何 target 写入前冻结执行计划并完成整帧 preflight。required 模式下，unsupported、编码失败、
资源 generation 不匹配或 presenter 失败都会使整帧失败；禁止先写入部分 GPU target 再回放 CPU 整帧。
Draw 与 RasterCmd 共用 color/depth target，命令顺序、CLEAR/LOAD、透明、viewmodel 和 overlay 语义由统一
recording 保持。

producer 身份只用于诊断，不自动形成 target 可见性边界。相邻 WORLD Draw spans 之间没有 Raster span 时，
executor 可按原顺序合并为一个 graphics batch；Raster span 是硬边界。任何进一步合并或迁移必须同时
保持画面合同并减少实际 Draw/Raster run、bridge 或 whole-loop 成本，不能只以 RasterCmd 数量下降签收。

normal AI world 对模块化角色先按 actor 顺序求值 pose/IK 并冻结全部可见 body Draw，再按相同顺序提交
opaque gear/weapon RasterCmd。附件继续读取对应 actor 的 finalized pose、placement 和 scene-light override；
该编排不得跨越 transparent、effects、viewmodel 或 overlay 层。

## Raster 与诊断合同

Raster binning 按原 command index 将命令写入 tile 列表。binned shader 可用有序索引跳过当前 segment 外
命令，但必须在 `segment.end` 停止；独立 full-scan shader 保留为 differential 对照。帧审计按 producer
记录 RasterCmd/span，并按实际 bridge 记录方向、color/depth traffic、层、相邻 producer 和 target generation。

GPU timestamp 属于完成的旧 frame slot，必须按其 frame ID 回填，不能直接归到当前 CPU frame。
`valid` 要求 requested 与 recorded 相等且 dropped 为零；退出时未回收的尾部样本不进入分位数。
graphics submit/wait 是队列关系证据，不等于某个 producer 的 GPU 时间。

正常游戏画面使用中性 fog。RasterCmd fog 字段、CPU/GPU consumer 和底层 Post Fog 测试合同仍可保留，
但 normal runtime 不把它们接入画面。

## 资源生命周期

- 模型 registry 拥有不可变 CPU bundle 与 stable handle/generation；GPU cache 拥有 device resource。
- Core 在 begin-frame pin 本帧引用；frame slot 完成前不得释放。world 切换后的旧 generation 只有 pin 清零
  后才能回收。
- 双帧 slot 分别持有 extent target、command/fence/query 和动态 Draw backing；不可变 mesh/texture cache
  由 executor/device 统一持有。
- raw Raster 分段重传必须保留先前录制引用的 backing，直到相关录制完成或销毁。
- resize 只重建 extent 相关 target、slot binding 与 swapchain，不得重复上传稳定 world mesh/texture。
- swapchain 由 backend 唯一拥有；acquire、render fence 与 present completion 分开跟踪。正常热路径禁止
  queue-idle，只有明确的 recreate/teardown 边界可以排空队列。

## 角色 GPU skinning

CPU 继续拥有 pose、IK、socket、gear 和 weapon placement。mixed frame 冻结 finalized palette、bind
position/normal、BDEF influence 和索引；compute skinning 输出写入 frame-slot device-local vertex buffer，
body Draw 直接消费。

normal GPU skinning 不生成 CPU reference。`--gpu-character-vertex-diff` 只为目标帧建立对照；
`--gpu-character-skinning-off` 是正式回滚边界，只恢复 CPU-skinned vertex upload，不改变上游所有权。

## Presenter 与失败边界

Windows native presenter 使用同一 Vulkan device/queue 和唯一 swapchain generation。逐帧审计必须保持
fallback、readback、CPU framebuffer copy、hot queue-idle、非法层转换和 poisoned presenter 为零。
`--gpu-required` 下任何 unsupported、preflight、submit、present、readback 或 CPU copy 都必须非零退出。

省略 `--gpu-native-present` 只用于显式的软件呈现 A/B，不是 required runtime 的降级路径。CPU renderer
仍是独立完整实现，用于 reference 和不启用 GPU renderer 的正常运行。

## 支持边界

- Windows 原生 PowerShell、物理 GPU 和 native present 是主开发与签收环境。
- Linux hosted Vulkan、WSL 与 llvmpipe 只用于编译、ABI 或 correctness 辅助诊断，不能替代驱动、窗口、
  resize、presenter 生命周期或性能结论。
- 设备丢失恢复、跨厂商完整矩阵、validation/sync、fault injection 和长时 soak 属于按风险触发的专项，
  不由日常 Quick 自动替代。
- 运行参数以 package 中 `rasterfall.exe --help` 为准；验收范围与证据要求由 GPU 验收指南拥有。
