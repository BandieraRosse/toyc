# GPU 目录概览

本目录用于保存 GPU 相关的外部项目和实验代码。当前主要项目是
[`wgpu-native`](wgpu-native/)，用于参考其 C API、GPU 设备发现和基础 GPU
计算流程。

## wgpu-native

`wgpu-native` 是基于 Rust `wgpu-core` 的 native WebGPU 实现，对外提供 C
接口。它可以在不同平台后端上完成：

- GPU adapter 枚举和设备信息查询；
- adapter、device 和 queue 创建；
- buffer、texture、shader 和 pipeline 管理；
- render/compute command 提交；
- Wayland、Windows 等平台的 surface 渲染。

主要入口：

- [`README.md`](wgpu-native/README.md)：项目说明；
- [`ffi/wgpu.h`](wgpu-native/ffi/wgpu.h)：wgpu-native 扩展 C API；
- [`ffi/webgpu-headers/webgpu.h`](wgpu-native/ffi/webgpu-headers/webgpu.h)：WebGPU C API；
- [`examples/enumerate_adapters/main.c`](wgpu-native/examples/enumerate_adapters/main.c)：枚举 GPU；
- [`examples/compute/main.c`](wgpu-native/examples/compute/main.c)：最小 compute 示例；
- [`Cargo.toml`](wgpu-native/Cargo.toml)：Rust 构建和 backend 配置。

## 与 Rasterfall 的关系

当前 Rasterfall 使用自有 CPU 软件光栅器。`wgpu-native` 先作为学习和验证
参考，不直接替换现有 renderer。后续如接入 Rf，建议从 Core 层的可选 GPU
服务开始：

```text
GPU 枚举 → 设备信息 → device/queue → compute 冒烟测试
```

完整 GPU renderer 需要另外处理 GPU surface、资源上传、pipeline、同步和
现有 framebuffer 的迁移。

## 原生 C Vulkan 探针

`gpu/src/rf_gpu_probe.c` 是 Rasterfall GPU 路线的 hosted 验收前端。它不使用
`wgpu-native`，也不要求安装 Vulkan SDK：`gpu/include/rf_vulkan_min.h` 只保存当前
backend 实际使用的 Vulkan 1.0 ABI 声明，`gpu/src/rf_gpu_vulkan_backend.c` 在运行时加载
系统 `libvulkan.so.1` 或 `vulkan-1.dll`。

```sh
make gpu-probe
build/rf-gpu-probe
```

Vulkan backend 优先选择具有 compute queue 的 discrete GPU；没有独显时回退到其他 compute
adapter，并持久拥有 loader、instance、physical device、logical device 与 queue。初始化期间继续
执行 Phase 1 compute 门禁：创建
host-visible/coherent storage buffer、descriptor、内嵌 SPIR-V compute pipeline、command
pool/buffer 与 fence，执行 `x = x * 3 + 1`，并 readback 验证 `1 2 3 4` 得到
`4 7 10 13`。没有 Vulkan loader、没有 physical device、无法建立 queue/resource，或结果
不一致时均返回非零；冒烟资源在初始化结束前释放，持久对象在 `rf_gpu_shutdown()` 时按依赖逆序释放。

同一源码可交叉编译为 Windows 原生控制台探针：

```sh
make win-gpu-probe
# 在 Windows 中运行 build\\rf-gpu-probe.exe
```

平台差异仅在动态 loader 与单调计时边界：Linux 使用 `libvulkan.so.1`，Windows 使用
`vulkan-1.dll`；Vulkan ABI、SPIR-V、资源生命周期和结果验证完全共享。输出同时记录 upload、
submit、execution/fence wait、readback 与 total 的 wall-clock 观测值。它们用于 bring-up；
精确 GPU-only timing 后续应使用 Vulkan timestamp query。

Phase 1 已完成双平台验收：WSL llvmpipe correctness 通过；Windows 原生枚举 AMD integrated 与
NVIDIA RTX 3050 Laptop GPU，discrete-first 策略明确选择 NVIDIA，compute/readback PASS。
Windows 当次观测为 upload 0.005 ms、submit 0.260 ms、execution-wait 0.134 ms、readback
0.002 ms；total 233.853 ms 包含资源、descriptor 与 pipeline 首次创建，不能当作稳态 dispatch
耗时。探针尚未建立 surface 或 swapchain，也没有接入正常 Rasterfall 可执行文件。
GPU-2A 已新增 `rasterfall/include/rf_gpu.h` 与 `rasterfall/src/rf_gpu.c`：RF Core 现在持有
平台无关 GPU service，拥有 disabled/optional/required policy、明确的 unavailable/failed/ready
状态、只读 adapter snapshot 和 backend shutdown 生命周期。`make gpu-service-test` 验证 optional
fallback、required failure 与 READY-only shutdown。正常游戏仍显式 disabled，CPU renderer 不变。
GPU-2B 已将 Vulkan ownership 从 probe 拆入 `rf_gpu_vulkan_backend`；probe 现在通过正式 service
启动 required backend、读取无 Vulkan handle 的状态快照并验证 shutdown。GPU-2 至此完成；下一步
GPU-3 是 GPU framebuffer smoke，正常 renderer 仍保持 CPU。

## 注意事项

- `wgpu-native` 依赖 Rust 标准库和多个系统图形 backend；
- 它与 Rasterfall 当前的 freestanding/Tinylibc 构建路径不是直接兼容关系；
- `ffi/webgpu-headers` 和 `examples/vendor/glfw` 是项目子模块；
- GPU 试验优先使用 hosted 构建目标，不影响 Rasterfall 现有 CPU 路径。
