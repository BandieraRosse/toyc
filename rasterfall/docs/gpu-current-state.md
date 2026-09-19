# GPU 当前状态

> 文档更新：2026-09-19
> 源码核对基线补充：2026-09-19 [HG-2B 真实交错桥接](hardware-graphics-hg2b.md#真实-raster-abi--graphics-交错桥接)：`rf_gpu_graphics_raster_draw()` 在同 device/extent 的未结束 Raster target 中插入整数 indexed draws，GPU 内双向传递 color/depth；`rf-gpu-raster-test --mixed-gate` 验证交错顺序。Core/native 混合接入仍待实现。
> 源码核对基线补充：2026-09-19 [HG-2B Raster ABI 分段基础](hardware-graphics-hg2b.md#raster-abi-分段基础hg-2b-进行中)：`rf_gpu_vulkan_raster_segment()` 使用独立范围/CLEAR/LOAD 参数，验证完整 stream；中间段不读回，VIEWMODEL/Post 留在末段。真实交错已由 `rf_gpu_graphics_raster_draw()` 接通；Core/native 接入仍待实现。
> 源码核对基线补充：2026-09-19 [HG-2B 整数深度与 target bridge](hardware-graphics-hg2b.md) 已实现 GPU 整数裁剪/投影/深度、GPU color/depth 往返转换及 attachment LOAD；Intel 前置门禁通过。Raster ABI CLEAR/LOAD 分段基础已在 Intel 验证；compute/graphics 桥接已通过 Intel 固定 fixture；Core 混合顺序与 strict native 门禁仍待实现，正常帧不变。
> 源码核对基线补充：2026-09-19 [HG-2A](hardware-graphics-hg2a.md) 提供独立 graphics indexed draw、持久 VB/IB/texels、RGBA8/D32 离屏 target；Intel 数值与资源复用门禁通过。下文 HG-1A/1B 的“尚无 hardware”仅指对应阶段与正常帧。
> 源码核对基线补充：2026-09-19 [HG-1B](hardware-graphics-hg1b.md) 已接入 CPU resource registry、generation、帧 pin 与延迟释放；Windows native/Fog resize 验证见该 checkpoint。尚无 GPU mesh cache、retained Draw 或 hardware indexed draw。
> 源码核对基线补充：2026-09-19 [HG-1A Draw/reference](hardware-graphics-hg1a.md) 已接入普通 opaque static RMESH；CPU/compute 输出保持精确一致，当前仍无 hardware indexed draw 或持久 GPU mesh cache。
> 源码核对基线补充：2026-09-19 HG-0 冻结 [Hardware Graphics 架构与基线](hardware-graphics-architecture.md)；显式 `--frame-audit` 改为逐帧输出，测量脚本记录各入口独立口径与原始证据。
> 源码核对基线：`windows/NativeCodex.ps1`、`rasterfall/src/rf_core_host.c`、`rasterfall/src/rf_gpu.c`、`gpu/src/rf_gpu_vulkan_backend.c`、`rasterfall/src/rasterfall_render.c`；Windows package 的 `--help`、`--logic-test` 与固定视角 `--frame-audit` 实测。

此文档只记录已实现的边界和最近可复核的状态，不设下一阶段目标或 GPU 硬件开发任务。旧 GPU V1 功能验收见 [归档](archive/gpu-v1-final-acceptance-2026-09-19.md)，上一轮性能工作顺序见 [历史计划](archive/gpu-performance-stage-2026-09-19.md)。新阶段计划和进度见 [Hardware Graphics](hardware-graphics-architecture.md)，HG-0 实测与统计边界见 [checkpoint 记录](hardware-graphics-hg0.md)。

## 当前实现

- CPU renderer 仍是默认路径。显式 `--renderer gpu-compute --gpu-native-present --gpu-required` 要求每个 normal frame 使用 native GPU；不支持的命令、回退、readback 或 CPU framebuffer copy 会使 strict 运行失败。
- normal frame 由 Core 编排：world frontend → Raster Command ABI V1/Texture V1 → CPU tile binning → Vulkan compute raster → 可选 Fog Post → CPU 生成的 overlay 上传与 GPU composite → Win32 swapchain present。窗口、输入、音频仍使用 SDL2；SDL-free Windows Native Platform 尚未实现。
- 共享 Vulkan backend、ABI pack/binning 和 shader 位于 `gpu/`；正常帧的命令与提交所有权位于 `rasterfall/src/rf_core_host.c`，渲染生产者位于 `rasterfall/src/rasterfall_render.c`。Gameplay/session 不持有 Vulkan 资源。
- `rf-gpu-graphics-test` 是独立 HG-2A hosted 诊断：选择 graphics+compute queue、上传单 mesh 与 opaque texture、提交 indexed draw 并读回离屏 color/depth。`rf_gpu_vulkan_graphics.inc` 拥有其 GPU 资源；HG-2B 已连接 Raster ABI 分段、GPU attachment/buffer 双向 bridge 与 LOAD indexed draws；尚无 Core 混合消费者、normal-frame hardware producer 或 registry generation adapter。
- 普通 opaque static RMESH 先按实例/submesh 生成 CPU-backed Draw，再同步 lowering 为原 RasterCmd；特殊/透明实例整实例保留旧 producer。`draw-reference` 审计显示实际实例、Draw、源三角形及拒绝原因。CPU registry 持有 mesh/material/texture bundle 和 generation，Core 帧 pin 保护 lowering 后的 RasterCmd 纹理引用；尚未建立 retained Draw，CPU 每帧仍执行几何处理。
- `--frame-audit` 在 Windows 同时写入 `rasterfall.log`；`fence_wait_ms`、`native_present_queue_idle_ms` 是 CPU 墙钟等待，不是 GPU timestamp。

## 已验证范围和性能快照

以下 28k 数据保留为前一轮历史快照，不能作为新阶段性能分母；HG-0 已采用逐帧审计重新记录基线。

- Intel Iris Xe 上 strict native/Fog smoke、正式地图 320/320 帧零回退波次运行、核心游玩和窗口拉伸已确认。该功能验收不等于性能目标或完整生命周期矩阵通过。
- 最近的 Windows Campaign 固定视角复测：1280×720、camera `(-13000,-12000)`、方向 `(0,1024)`、第 17–46 帧中位数。46/46 帧 `gpu-native`，零回退、零 readback、零 CPU framebuffer copy；包内 `--logic-test` 通过。
- 该视角中，world command 中位数 28,045.5，retained pre/post 29,128.5，最终 AI command 4,317.5。AI 提交耗时中位数 7.287 ms；render 37.261 ms、whole loop 101.533 ms、GPU frontend 33.058 ms、fence wait 25.792 ms、native present queue idle 30.488 ms。各阶段口径见 [渲染文档](rendering.md)，不能把等待墙钟时间解释为 shader 执行时间。
- AI 正常渲染已先做保守侧平面剔除，再压紧完全位于视口外的命令。同一视角仍有约 3 名 AI 通过前段检查，却在提交后被判为全屏外；模块化身体、装备和 socket 武器的逐姿态边界尚未用于前段剔除。

## 事实入口

| 需要确认 | 入口 |
| --- | --- |
| 当前参数 | package 内 `rasterfall.exe --help` |
| Windows 构建、打包与逻辑回归 | `windows/NativeCodex.ps1 package`、`test` |
| HG-2A hardware indexed draw 离屏 proof | `windows/Makefile gpu-graphics-test`、`tools/hardware_graphics_proof.ps1`；数值和平台边界见 [HG-2A](hardware-graphics-hg2a.md) |
| 实机 strict 路径 | `windows/NativeCodex.ps1 gpu-test`；固定场景使用 `--gpu-normal-scene near 0 --frame-audit --frames 46` |
| 帧阶段、命令语义和诊断 | [rendering.md](rendering.md)、[runtime.md](runtime.md) |
| 平台与构建边界 | [build-platforms.md](build-platforms.md)、[windows-native-codex.md](windows-native-codex.md) |

历史验收和上一轮性能数字仅描述各自测试场景；启动新硬件计划时应重新固定设备、驱动、package、场景、分辨率和预热区间。
