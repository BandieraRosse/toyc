# Rasterfall GPU 目录

> 文档更新：2026-09-19
> 源码核对基线：`gpu/src/rf_gpu_vulkan_backend.c`、`gpu/src/rf_gpu_raster_pack.c`、`gpu/src/rf_gpu_raster_bin.c`、`rasterfall/src/rf_core_host.c`；当前 Intel 实机状态见 [GPU 当前状态](../docs/rasterfall/gpu-current-state.md)。

本目录保存 Rasterfall 共享 Vulkan backend、Raster ABI pack/binning、compute shader、hosted 诊断前端和
参考性外部代码。本目录说明代码所有权和已实现的技术边界；实机验证与性能快照以
[GPU 当前状态](../docs/rasterfall/gpu-current-state.md) 为准。旧阶段计划保存在 `docs/rasterfall/archive/`。

## 当前所有权

```text
Rasterfall normal frontend
  → toy_raster_cmd
  → Raster ABI V1 pack + Texture V1 table
  → CPU tile binning
  → rf_gpu / rf_gpu_vulkan_backend
  → compute raster color + signed inverse-depth
  → optional Post-Raster V1
  → overlay composite
  → readback software present OR Win32 swapchain native present
```

- `rasterfall/src/rf_gpu.c` 是平台无关 service/resource facade。
- `gpu/src/rf_gpu_vulkan_backend.c` 拥有 Vulkan loader、instance、device、queue、resource、pipeline 和 synchronization。
- `rasterfall/src/rf_core_host.c` 拥有 normal frame 选择、fallback、pack/binning、oracle、overlay 和 present 编排。
- Game、session 与 gameplay 不持有 Vulkan object，不保存 GPU resource 状态。
- CPU renderer 是默认路径、兼容后端和 differential oracle。

## 主要文件

| 范围 | 入口 |
| --- | --- |
| 最小 Vulkan ABI | `include/rf_vulkan_min.h` |
| backend 公开边界 | `include/rf_gpu_vulkan_backend.h` |
| Vulkan backend | `src/rf_gpu_vulkan_backend.c` |
| Raster ABI pack | `src/rf_gpu_raster_pack.c` |
| CPU tile binning | `src/rf_gpu_raster_bin.c` |
| compute shaders | `shaders/*.comp` |
| 内嵌 SPIR-V | `src/rf_gpu_*_spirv.inc` |
| service/framebuffer/raster tests | `src/rf_gpu_*_test.c` |
| CPU/GPU differential | `src/rf_gpu_raster_diff_test.c` |

`rf_vulkan_min.h` 只声明 backend 实际使用的 Vulkan ABI。backend 运行时加载 Linux
`libvulkan.so.1` 或 Windows `vulkan-1.dll`，构建不要求 Vulkan SDK 或 runtime shader compiler。

## 已冻结的技术边界

- Raster ABI V1 是 little-endian、fixed-width、pointer-free、versioned word stream。
- Raster V1 要求 `shaderInt64`，workgroup 由 capability contract 在 16×16 和 8×8 中选择。
- CPU binning 按 command order 建立 tile offset/index list，不复制 payload，不改变 raster semantics。
- Texture V1 是 buffer-backed nearest RGB8/RGBA8；CPU pointer 不进入 ABI。
- normal world batch 必须整批可表达才使用 GPU；B2d-4a/4b/4c 的普通 RFM2 material alpha、RGBA texel alpha 与 `texel × material / 255` 组合 alpha 可由 flat/textured source-over command 表达，RGBA producer 在 CPU replay 侧也显式 no-depth-write，支持的 Transparent V1 不触发 fallback；高级 sphere/toon/specular、bilinear、edge、overlay 或其他 unsupported input 仍导致整批 CPU fallback。
- CPU/GPU differential 比较 canonical XRGB8888 color 和完整 signed inverse-depth；hash 不替代逐元素比较。
- Post-Raster V1 使用独立 device-local `post_color`，不对 raster color 原位读写。
- normal native present 使用 BGRA8 transfer-destination swapchain，不读回 color/depth，不复制 CPU framebuffer。

## 实机状态

normal gameplay 已在 Windows Intel Iris Xe 通过 strict native/Fog smoke、正式地图 320 帧零回退波次复现及窗口拉伸。该结果是功能范围验收；最近固定视角的命令和帧时见 [GPU 当前状态](../docs/rasterfall/gpu-current-state.md)。GPU-8B1/8B2/9A 等旧阶段编号仅用于定位实现历史，不再作为待执行计划。

## 验证入口

```sh
make gpu-probe
build/rf-gpu-probe

make gpu-service-test
make gpu-framebuffer-test
make gpu-raster-abi-test
make gpu-raster-test
make gpu-raster-binning-test
make gpu-raster-diff-test
make gpu-overlay-test

build/rf-gpu-raster-diff-test --replay-raster-stream commands.bin
build/rf-gpu-overlay-test
```

Windows 交叉构建对应 `win-gpu-*` 目标。`gpu-overlay-test` 同时覆盖 overlay composite、Post identity
和 Fog V0 differential。hosted test 允许有限 readback 用于自动判定；normal native-present 的契约仍是零
color/depth readback 和零 CPU framebuffer copy。

WSL llvmpipe 只是 correctness 环境，不提供物理 GPU 性能结论。性能、resize 和 native presentation
必须在 Windows 物理 GPU 上验收，并从 Windows package 目录运行 normal binary。

## `wgpu-native`

`wgpu-native/` 是早期参考性外部项目，不是 Rasterfall 当前 GPU backend，也不进入 normal build。
当前路线是仓库自有 C Vulkan backend；不引入 Rust GPU runtime、wgpu 运行时依赖或 runtime shader compiler。

## 修改规则

- 新增 Raster ABI command 或 shading semantics 时，同步扩展 pack validation、CPU oracle、GPU shader 和 differential fixture。
- 新增 shader 时，同步检查 SPIR-V 生成物、Makefile 依赖、Linux hosted 与 Windows 交叉构建。
- 不将 Vulkan handle/type 暴露给 Game、session、console 或 status snapshot。
- 不将 hosted readback timing 或 llvmpipe timing 宣称为 normal native GPU frame 性能。
- 实现边界或验证状态变化时同步更新 `docs/rasterfall/gpu-current-state.md` 及受影响的
  `rendering.md`、`runtime.md` 和 `build-platforms.md`。
