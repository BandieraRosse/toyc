# GPU 当前状态

> 文档更新：2026-09-20
> 源码核对基线补充：2026-09-20 P0 性能事实门禁已完成并接入 `tools/hardware_graphics_metrics.ps1`：固定丢弃前 16 帧，分开汇总 CPU whole-loop/frame interval 与按历史 frame ID 对齐的 GPU timestamp，并校验 Campaign `world=1`/敌人命令。Intel 实机固定 near/0 120 帧与显式 Campaign 320 帧均为全 native；Campaign 279 帧含敌人命令，活敌输出通过。既有默认 Outpost 320 帧样本不再称为正式波次。
> 源码核对基线补充：2026-09-20 HG-2C5 已签收。`done=7/8` 已定位为 Windows condition-variable futex 仿真的丢失唤醒，并改用按地址 `WaitOnAddress`。最终代码在 Intel 上固定 near 300/300、动态 `--auto` 10000/10000（7分40秒）及五种 fault injection 均通过预期合同；全程 hot queue-idle、fallback、readback、CPU copy 与 renderer watchdog 为零。Khronos validation + sync validation 覆盖 300 帧、五种 fault injection 与 teardown/recreate，零 VUID/SYNC-HAZARD；期间发现并修复 render-pass compatibility、录制中 descriptor set 更新与 acquire/layout transition 同步问题。详见 [HG-2C5](hardware-graphics-hg2c5.md)。
> 源码核对基线补充：2026-09-20 修复 HG-2C4 双 slot 呈现所有权：Vulkan backend 只保留一个 swapchain，两个 slot 分别持有 acquire/render-complete semaphore 和离屏资源。Intel 长时验证表明 render fence 不覆盖 present 完成，当前恢复 present 后 queue-idle；300/300 strict native 正常退出、零 fallback/readback/CPU copy，最终 timestamp frame 298。此前“正常帧 queue-idle 为零”已撤销。
> 源码核对基线补充：2026-09-20 HG-2C4 已签收：两个完整 mixed frame slot 使 normal native submit 不再立即等待本帧 render fence；slot 复用时回收 fence/timestamp，并按引用计数延迟释放 Core resource pin。Intel strict native 120/120、专用 mixed gate 与四 extent 140 帧 resize gate 通过；`mixed-gpu frame=` 从当前第 3 帧关联历史帧 1，最终报告帧 118，零 fallback/readback/CPU framebuffer copy。
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
- HG-2C5 的 `PRESENT-AUDIT` 是软件 ownership/state 审计；正常热路径的 `completion_source=IMAGE_REACQUIRED` 只证明该 image 的 present wait 已可安全复用，不声称显示扫描完成。

## 已验证范围和性能快照

### P0 口径校正

- 固定场景 120 帧旧平均包含首两帧约 289/128 ms 的 present cold-start。第 17--120 帧稳态观察为：
  whole-loop 平均约 33.6 ms、中位数约 32.6 ms、P95 约 40.3 ms；实际 frame interval 平均约
  41.2 ms。两者约 7.6 ms 的差值主要包含逐帧审计写日志和未采样调度，不能归入 renderer。
- 同一窗口 GPU timestamp 第 17--120 帧：Raster 平均约 26.6 ms，depth bridge 合计约 2.2 ms，
  Draw 约 0.76 ms，Post/overlay/copy 均为次要项。CPU scene 平均约 15.9 ms，其中 static 是主要来源；
  AI 提交约 6.7 ms。
- 已检查的 320 帧低负载日志为 `world=0`、无敌人命令，因此不是正式 Campaign 波次。其约 16--19 ms
  Acquire 等待表示 swapchain/显示背压；`vkQueuePresentKHR` 调用自身约 0.02 ms，不支持“GPU 提交或
  present API 是主瓶颈”的结论。
- 后续性能数字由 `tools/hardware_graphics_metrics.ps1` 生成；正式波次必须显式加载 Campaign 并通过
  world、敌人命令及活敌输出三重门禁。

### P0 最新实机基线

同一 Windows package、Intel Iris Xe、1280×720，丢弃前 16 帧：

| 场景 | whole-loop median/P95 | scene median/P95 | AI median/P95 | GPU Raster median/P95 | acquire / present median |
| --- | --- | --- | --- | --- | --- |
| 固定 near/0，120 帧 | 29.956 / 32.975 ms | 13.881 / 15.686 ms | 6.283 / 6.760 ms | 15.608 / 31.452 ms | 0.005 / 0.021 ms |
| Campaign 波次，320 帧 | 60.847 / 71.792 ms | 34.950 / 42.834 ms | 7.714 / 8.431 ms | 19.691 / 27.894 ms | 0.005 / 0.021 ms |

两轮分别为 120/120、320/320 native，零 fallback/readback/CPU framebuffer copy/hot queue-idle。
Campaign 全部 `world=1`，279 帧有敌人命令，标准输出观察到活敌 1--7。其 CPU scene 平均约
35.90 ms，明显高于 GPU Raster 平均约 20.08 ms；当前正式波次首先是 CPU scene/frontend 瓶颈，
其次才是 GPU Raster。bridge 合计中位数约 0.91 ms，Draw 约 0.55 ms，Post/overlay/copy 均不足
0.21 ms，不应排在 HG-3 前。

逐帧审计与未采样调度间隙两轮中位数均约 8.47 ms。Campaign 有审计和无审计的完整进程墙钟分别约
25.40 秒和 21.36 秒；该数字包含启动/退出，只用于确认审计扰动，不替代预热后逐帧统计。

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
