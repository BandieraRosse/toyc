# Rasterfall GPU 目录

本目录保存 Rasterfall 的 Vulkan backend、独立 Scene graphics、GPU skinning 和诊断测试。维护者先读[GPU 渲染架构](../docs/rasterfall/architecture/gpu-rendering-architecture.md)，再按任务查看[GPU 验收与诊断](../docs/rasterfall/guides/gpu-validation.md)、[GPU 性能标准](../docs/rasterfall/reference/gpu-performance-standards.md)及[计划入口](../docs/rasterfall/plans/README.md)。

| 范围 | 代码入口 |
| --- | --- |
| Vulkan ABI 与 backend | `include/rf_vulkan_min.h`、`include/rf_gpu_vulkan_backend.h`、`src/rf_gpu_vulkan_backend.c` |
| Scene graphics 与 GPU skinning | `src/rf_gpu_vulkan_graphics.inc`、`src/rf_gpu_graphics_spirv.inc` |
| GPU shader 与内嵌 SPIR-V | `shaders/`、`src/rf_gpu_*_spirv.inc` |
| Scene 与 GPU service 测试 | `src/rf_gpu_graphics_test.c`、`src/rf_gpu_service_test.c` |

`wgpu-native/` 是早期参考性外部项目，不进入当前 Rasterfall 构建。旧 GPU Compute Raster 资料见[退役归档](../docs/rasterfall/archive/gpu-compute-retirement/README.md)。构建目标、诊断参数、实机 package 和验收范围以[构建与平台](../docs/rasterfall/guides/build-platforms.md)及[GPU 验收与诊断](../docs/rasterfall/guides/gpu-validation.md)为准。
