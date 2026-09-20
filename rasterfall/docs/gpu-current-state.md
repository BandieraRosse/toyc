# GPU 当前状态

> 文档更新：2026-09-20
> 源码核对基线补充：2026-09-20 HG-2C4 的首个增量已删除 normal present 后的 `vkQueueWaitIdle`：完成 semaphore 按 swapchain image 分配，重建/销毁边界才排空 queue。Intel strict native near/0 120/120、零 fallback/readback/CPU framebuffer copy，所有帧 `native_present_queue_idle_ms=0.000`。帧末 render fence、单帧资源与多帧在途仍待完成。
> 源码核对基线补充：2026-09-20 HG-2C3 已完成统一 frame command recording：normal mixed 的 Raster、depth bridge、Draw、Post、overlay 与 present copy 录入同一 command buffer，热帧 graphics submit/fence wait 均为 0；共享 RGBA8 color target 保持 1280×720 两次 depth bridge 共 14,745,600 bytes。
> 源码核对基线补充：2026-09-19 HG-2C1 已开始：`--frame-audit` 新增 `mixed-cpu`，区分 freeze、cache collect、preflight、texture measure、pack、Draw encode、batch prepare、graphics Draw/bridge 调用与 Raster segment 墙钟。它们不是 GPU timestamp；设备执行分项仍待 query pool 接入。计划与口径见 [HG-2C](hardware-graphics-hg2c.md)。
> 源码核对基线补充：2026-09-19 normal mixed 热路径已移除连续 overlay 的逐帧整屏 CPU copy，并复用 mixed pack/batch 容量。当时取消的中间 graphics CPU fence wait 后续确认会在连续 Draw span 间重置仍在执行的 command pool，2026-09-20 已为正确性恢复；历史性能数字不能作为当前同步版本的基线。剩余首要工作仍是共享 target、统一 command recording 和多帧在途。详见 [HG-2B 后续交接](hardware-graphics-post-hg2b-handoff.md)。
> 源码核对基线补充：2026-09-19 修复 static RMESH 在特定近面/侧向视角下过晚触发 mixed graphics 数值预检、导致 strict 帧退出的问题：producer 现在先按 graphics 整数投影范围判定，不合格实例留在 GPU compute RasterCmd 路径。Windows Outpost 与 legacy map 的 `--auto --frames 300` strict native 自动旋转/传送均通过，零 fallback/readback/CPU framebuffer copy。
> 源码核对基线补充：2026-09-19 RTX 3050 Windows 首帧 native swapchain 兼容修复：Core config 为 native present 选择 SDL software renderer 窗口，避免 SDL 硬件 renderer 与 Vulkan 在同一 HWND 上同时建立呈现链。strict native 10/10 帧、零回退、零读回及零 CPU framebuffer copy；Fog 10 帧及三 extent native gate 通过，长时运行未验收。详见 [兼容修复记录](gpu-nvidia-swapchain-compat.md)。

> 源码核对基线补充：2026-09-19 HG-2B 已按 Windows Intel Iris Xe 修订口径签收：strict native 正常混合帧、混合遮挡/层顺序 fixture、近/中距离窗口帧及四 extent 的 140 帧 resize 通过；正常帧逐像素对照与设备丢失恢复未验证且不属本 checkpoint 门禁。Linux/其他 GPU 未验收，HG-3A 尚未开始。详见 [HG-2B](hardware-graphics-hg2b.md)。
> 源码核对基线补充：2026-09-19 [HG-2A](hardware-graphics-hg2a.md) 提供独立 graphics indexed draw、持久 VB/IB/texels、RGBA8/D32 离屏 target；Intel 数值与资源复用门禁通过。下文 HG-1A/1B 的“尚无 hardware”仅指对应阶段与正常帧。
> 源码核对基线补充：2026-09-19 [HG-1B](hardware-graphics-hg1b.md) 已接入 CPU resource registry、generation、帧 pin 与延迟释放；Windows native/Fog resize 验证见该 checkpoint。尚无 GPU mesh cache、retained Draw 或 hardware indexed draw。
> 源码核对基线补充：2026-09-19 [HG-1A Draw/reference](hardware-graphics-hg1a.md) 已接入普通 opaque static RMESH；CPU/compute 输出保持精确一致，当前仍无 hardware indexed draw 或持久 GPU mesh cache。
> 源码核对基线补充：2026-09-19 HG-0 冻结 [Hardware Graphics 架构与基线](hardware-graphics-architecture.md)；显式 `--frame-audit` 改为逐帧输出，测量脚本记录各入口独立口径与原始证据。
> 源码核对基线：`windows/NativeCodex.ps1`、`rasterfall/src/rf_core_host.c`、`rasterfall/src/rf_gpu.c`、`gpu/src/rf_gpu_vulkan_backend.c`、`rasterfall/src/rasterfall_render.c`；Windows package 的 `--help`、`--logic-test` 与固定视角 `--frame-audit` 实测。

此文档只记录已实现的边界和最近可复核的状态，不设下一阶段目标或 GPU 硬件开发任务。旧 GPU V1 功能验收见 [归档](archive/gpu-v1-final-acceptance-2026-09-19.md)，上一轮性能工作顺序见 [历史计划](archive/gpu-performance-stage-2026-09-19.md)。新阶段计划和进度见 [Hardware Graphics](hardware-graphics-architecture.md)，HG-0 实测与统计边界见 [checkpoint 记录](hardware-graphics-hg0.md)。

## 当前实现

- CPU renderer 仍是默认路径。显式 `--renderer gpu-compute --gpu-native-present --gpu-required` 要求每个 normal frame 使用 native GPU；不支持的命令、回退、readback 或 CPU framebuffer copy 会使 strict 运行失败。
- Windows strict native normal frame 由 Core 编排：world RasterCmd 与 static prop indexed Draw 按顺序进入 mixed plan → Vulkan compute/graphics target 交错 → 可选 Fog Post → CPU 生成的 overlay 上传与 GPU composite → Win32 swapchain present。其他 GPU 模式仍走原 compute raster 路径。窗口、输入、音频仍使用 SDL2；SDL-free Windows Native Platform 尚未实现。
- 共享 Vulkan backend、ABI pack/binning 和 shader 位于 `gpu/`；正常帧的命令与提交所有权位于 `rasterfall/src/rf_core_host.c`，渲染生产者位于 `rasterfall/src/rasterfall_render.c`。Gameplay/session 不持有 Vulkan 资源。
- `rf-gpu-graphics-test` 是独立 HG-2A hosted 诊断：选择 graphics+compute queue、上传 mesh 与 opaque texture、提交 indexed draw 并读回离屏 color/depth。`rf_gpu_vulkan_graphics.inc` 已将持久资源与共享 target/pipeline 分离；HG-2B 连接 Raster ABI、GPU attachment/buffer bridge、Core mixed consumer 和 normal-frame static prop hardware producer。
- 普通 opaque static RMESH 按实例/submesh 生成 Draw；Windows strict native 的 eligible 实例提交 hardware Draw 并跳过同步 CPU lowering，其他模式仍同步 lowering 为 RasterCmd。特殊/透明实例保留原 producer。CPU registry 持有 mesh/material/texture bundle 和 generation，Core 帧 pin 保护混合帧资源引用。
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
