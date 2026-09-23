# Rasterfall GPU 目录

本目录保存 Rasterfall 的 Vulkan backend、Raster ABI pack/binning、compute shader 和诊断测试。维护者先读[GPU 渲染架构](../docs/rasterfall/architecture/gpu-rendering-architecture.md)，再按任务查看[GPU 验收与诊断](../docs/rasterfall/guides/gpu-validation.md)、[GPU 性能标准](../docs/rasterfall/reference/gpu-performance-standards.md)及[唯一活动计划](../docs/rasterfall/plans/README.md)。

| 范围 | 代码入口 |
| --- | --- |
| Vulkan ABI 与 backend | `include/rf_vulkan_min.h`、`include/rf_gpu_vulkan_backend.h`、`src/rf_gpu_vulkan_backend.c` |
| Raster ABI 与 CPU binning | `src/rf_gpu_raster_pack.c`、`src/rf_gpu_raster_bin.c` |
| GPU shader 与内嵌 SPIR-V | `shaders/`、`src/rf_gpu_*_spirv.inc` |
| 测试与 CPU/GPU differential | `src/rf_gpu_*_test.c`、`src/rf_gpu_raster_diff_test.c` |

`wgpu-native/` 是早期参考性外部项目，不进入当前 Rasterfall 构建。构建目标、诊断参数、实机 package 和验收范围以[构建与平台](../docs/rasterfall/guides/build-platforms.md)及[GPU 验收与诊断](../docs/rasterfall/guides/gpu-validation.md)为准。
