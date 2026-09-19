# GPU 当前状态

> 文档更新：2026-09-19
> 源码核对基线：`windows/NativeCodex.ps1`、`rasterfall/src/rf_core_host.c`、`rasterfall/src/rf_gpu.c`、`gpu/src/rf_gpu_vulkan_backend.c`、`rasterfall/src/rasterfall_render.c`；Windows package 的 `--help`、`--logic-test` 与固定视角 `--frame-audit` 实测。

此文档只记录已实现的边界和最近可复核的状态，不设下一阶段目标或 GPU 硬件开发任务。旧 GPU V1 功能验收见 [归档](archive/gpu-v1-final-acceptance-2026-09-19.md)，上一轮性能工作顺序见 [历史计划](archive/gpu-performance-stage-2026-09-19.md)。新硬件开发计划应以此状态重新立项。

## 当前实现

- CPU renderer 仍是默认路径。显式 `--renderer gpu-compute --gpu-native-present --gpu-required` 要求每个 normal frame 使用 native GPU；不支持的命令、回退、readback 或 CPU framebuffer copy 会使 strict 运行失败。
- normal frame 由 Core 编排：world frontend → Raster Command ABI V1/Texture V1 → CPU tile binning → Vulkan compute raster → 可选 Fog Post → CPU 生成的 overlay 上传与 GPU composite → Win32 swapchain present。窗口、输入、音频仍使用 SDL2；SDL-free Windows Native Platform 尚未实现。
- 共享 Vulkan backend、ABI pack/binning 和 shader 位于 `gpu/`；正常帧的命令与提交所有权位于 `rasterfall/src/rf_core_host.c`，渲染生产者位于 `rasterfall/src/rasterfall_render.c`。Gameplay/session 不持有 Vulkan 资源。
- `--frame-audit` 在 Windows 同时写入 `rasterfall.log`；`fence_wait_ms`、`native_present_queue_idle_ms` 是 CPU 墙钟等待，不是 GPU timestamp。

## 已验证范围和性能快照

- Intel Iris Xe 上 strict native/Fog smoke、正式地图 320/320 帧零回退波次运行、核心游玩和窗口拉伸已确认。该功能验收不等于性能目标或完整生命周期矩阵通过。
- 最近的 Windows Campaign 固定视角复测：1280×720、camera `(-13000,-12000)`、方向 `(0,1024)`、第 17–46 帧中位数。46/46 帧 `gpu-native`，零回退、零 readback、零 CPU framebuffer copy；包内 `--logic-test` 通过。
- 该视角中，world command 中位数 28,045.5，retained pre/post 29,128.5，最终 AI command 4,317.5。AI 提交耗时中位数 7.287 ms；render 37.261 ms、whole loop 101.533 ms、GPU frontend 33.058 ms、fence wait 25.792 ms、native present queue idle 30.488 ms。各阶段口径见 [渲染文档](rendering.md)，不能把等待墙钟时间解释为 shader 执行时间。
- AI 正常渲染已先做保守侧平面剔除，再压紧完全位于视口外的命令。同一视角仍有约 3 名 AI 通过前段检查，却在提交后被判为全屏外；模块化身体、装备和 socket 武器的逐姿态边界尚未用于前段剔除。

## 事实入口

| 需要确认 | 入口 |
| --- | --- |
| 当前参数 | package 内 `rasterfall.exe --help` |
| Windows 构建、打包与逻辑回归 | `windows/NativeCodex.ps1 package`、`test` |
| 实机 strict 路径 | `windows/NativeCodex.ps1 gpu-test`；固定场景使用 `--gpu-normal-scene near 0 --frame-audit --frames 46` |
| 帧阶段、命令语义和诊断 | [rendering.md](rendering.md)、[runtime.md](runtime.md) |
| 平台与构建边界 | [build-platforms.md](build-platforms.md)、[windows-native-codex.md](windows-native-codex.md) |

历史验收和上一轮性能数字仅描述各自测试场景；启动新硬件计划时应重新固定设备、驱动、package、场景、分辨率和预热区间。
