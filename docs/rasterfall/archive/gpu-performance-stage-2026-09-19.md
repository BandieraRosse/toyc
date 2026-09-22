# Rasterfall GPU 性能阶段与 Windows Platform 路线（历史计划）

> 历史归档：此处保留 2026-09-19 上一轮性能工作顺序，不再作为当前开发任务或验收门槛。当前实现与最近实测见 [GPU 当前状态](../gpu-current-state.md)。

> 文档更新：2026-09-19
> 源码核对基线补充：逐帧审计已区分 `native_present_ms`（`vkQueuePresentKHR` 调用）与 `native_present_queue_idle_ms`（随后 `vkQueueWaitIdle`）；同时输出 classification、texture measure、binning 及各上传阶段。两者均为 CPU 墙钟计时，尚未提供 GPU timestamp。
> 源码核对基线：Windows Intel Iris Xe 上，正式地图 `--gpu-wave-repro --frames 320 --renderer gpu-compute --gpu-required --gpu-native-present --frame-audit` 产生 320/320 帧 `gpu-native`，零 fallback、readback、CPU framebuffer copy 和无效层切换；用户确认地图核心游玩与窗口拉伸正常。Windows build、logic-test 和重新打包后的 3 帧 strict smoke 通过。

## 当前阶段

GPU normal gameplay 的功能阶段已结束。现在的主任务是提高 Windows Intel 实机帧率。旧的 GPU V1 冻结矩阵不再作为当前开发门槛；当时的范围与证据保存在[历史验收记录](gpu-v1-final-acceptance-2026-09-19.md)。长期 soak、完整窗口生命周期组合和故障注入不是本阶段的前置条件，后续遇到相关故障再据实处理。

现有 normal frame 链路为：world frontend → Raster Command ABI V1 → CPU tile binning → Vulkan compute raster → optional Fog Post → CPU 生成的 HUD/overlay 上传 → GPU composite → Win32 swapchain present。CPU renderer 保留作为兼容路径和正确性参考。Windows 窗口、输入和音频仍由 SDL2 提供；SDL-free Windows Native Platform 是独立后续工作。

## 已知性能基线

原正式地图 320 帧 strict 运行的 `whole_loop_ms` 平均约 136 ms、最高约 270 ms，只有一个 world 且没有窗口交互。重新打包后同一地图的 60 帧 strict 复测为 60/60 帧 `gpu-native`、零 fallback：整循环 median 116 ms、p95 144.465 ms；frontend median 43.163 ms、p95 61.833 ms；pack median 2.069 ms；fence wait median 26.848 ms；native present median 30.786 ms；present wall median 64.691 ms。这是低帧率问题的实机证据，不代表全部玩法场景或独立 GPU shader 成本。首帧与稳定帧应分开统计，不能将 fence wait 或 present wall time 直接等同于 GPU shader 时间。现有数据提示 frontend 与 presentation 都需进一步拆分；pack 不是当前最大已知耗时。

## 性能工作顺序

1. **固定可复现基线。** 在同一 package、地图、分辨率和 GPU 上保存 `--frame-audit` 的逐帧日志；至少分别记录静止场景、波次交战、转向高命令量视野及 Fog 开/关。汇总预热后 median、p95、最大值，以及 frontend、classification、texture measure、pack/binning、GPU fence、overlay 和 native present 各阶段。记录命令数、三角形数、纹理上传字节与分辨率。
2. **先解释 CPU frontend 成本。** 依据 `rasterfall_perf` 和 frame audit 定位地图、敌人、角色、静态物件或裁剪中哪一类在生成约 4 万条命令。优先减少无贡献的提交和重复求值；每次改动对照相同场景的 coverage、命令量和画面。
3. **分离 GPU 与 presentation 成本。** 核对 CPU tile binning、stream/texture upload、compute dispatch、fence wait、overlay upload/composite、swapchain acquire/present 的单独耗时。以 GPU timestamp 或等效实机证据区分排队等待与 shader 执行；避免仅凭 `fence_wait_ms` 决定 shader 优化方向。
4. **按最大确定瓶颈优化。** 保持 strict native、零回退/回读/CPU framebuffer copy 和正确层顺序。修改 Raster ABI、纹理或透明语义时运行对应 differential/fixture；修改 frontend 时核对真实 world 画面与 command coverage。
5. **以同一基线复测。** 首个可用性目标是 1280×720 Intel Iris Xe 的常见正常游玩场景预热后 p95 帧时不高于 33.3 ms（约 30 FPS）；达到后再评估 16.7 ms（约 60 FPS）目标。任何优化结论都同时报告画质、命令量和 strict audit，不用单帧最优值替代整体结果。

## 入口与边界

| 工作 | 入口 |
| --- | --- |
| 实机复现、帧审计、参数 | `docs/rasterfall/runtime.md`、`rendering.md`；`build-windows/rasterfall-windows/rasterfall.exe --help` |
| normal frontend 与分阶段统计 | `rasterfall/src/rasterfall_render.c`、`rasterfall/src/rasterfall_perf.c`、`rasterfall/src/render/` |
| Core command、pack、retained frame | `rasterfall/src/rf_core_host.c`、`gpu/src/rf_gpu_raster_pack.c` |
| GPU raster、Post 与 native present | `gpu/src/rf_gpu_vulkan_backend.c`、`gpu/shaders/`、`rasterfall/src/rf_gpu.c` |
| Windows package 与实机验证 | `docs/rasterfall/build-platforms.md`、`windows/NativeCodex.ps1` |

不要把性能优化写成新增玩法功能，也不要为旧 diagnostic/legacy 路径扩大 normal Raster ABI。Linux freestanding CPU 路径、Windows 共享玩法源码和 `--gpu-required` 的逐帧失败契约继续保留。后续 Windows Native Platform 自有 Win32 window/input/audio 与 SDL2 移除另立阶段；当前 HWND native present 不代表该阶段完成。
