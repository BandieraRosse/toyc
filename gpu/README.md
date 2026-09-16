# GPU 目录概览

> 文档更新：2026-09-16
> 源码核对基线补充：GPU-7A normal-world capture 由正常 Rasterfall binary 输出 selected Raster V1 stream；本目录 hosted differential 对 replay 默认执行 tile-binned CPU/GPU authority，大型 real-world replay 不执行 full-scan。
> 源码核对基线补充：GPU-7B 以 V1-compatible 96-byte command kind 表达无纹理 vertex-lit planar；flat/vertex-lit 共用 raster kernel，WSL/RTX fixed、stress、combined-world differential 均 0 mismatch，DONE / FROZEN。
> 源码核对基线：GPU-6.5 Tile Command Binning 已完成并冻结；WSL llvmpipe 与 Windows Intel Iris Xe 的 CPU/full-scan/tile-binned differential 均为 color/depth 0 mismatch，GPU-6 authority 持续约束 Raster V1。

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
启动 required backend、读取无 Vulkan handle 的状态快照并验证 shutdown。

GPU-3 新增平台无关 `rf_gpu_framebuffer` resource/API 与 Vulkan framebuffer 实现：每个 framebuffer
拥有 device-local XRGB8888 output、host-visible readback、compute pipeline、descriptor 和 command
buffer；固定 shader 写线性渐变，barrier 后 copy/readback，再按 stride 复制进现有 `toy_surface`。
coherent readback 直接读取，non-coherent readback 显式 invalidate；fence 使用 5 秒有限 timeout。
resize 以 replacement-first 方式只重建 framebuffer 资源，resource 必须先于 backend shutdown 释放。

```sh
make gpu-framebuffer-test
build/rf-gpu-framebuffer-test
make win-gpu-framebuffer-test
# Windows PowerShell: .\build\rf-gpu-framebuffer-test.exe
```

WSL llvmpipe 与 Windows RTX 3050 均已通过全像素/hash、非紧密 stride、resize 与 shutdown 检查；
Windows probe 确认 discrete adapter 为 NVIDIA GeForce RTX 3050 Laptop GPU。正常 renderer 仍为 CPU；triangle execution/depth buffer/world、
surface/swapchain 均未开始。GPU-3 不输出性能统计，避免把临时 readback 当成 native renderer 结论。

GPU-4 已建立独立 Raster Command ABI V1：32-byte little-endian versioned header 后接固定 96-byte、
无指针 command。CPU adapter 只 pack clear color、depth clear 0 与 opaque flat triangle，显式携带
screen vertex/Q16 inverse-depth、area/bbox、fixed color、Q8 light/fog 和 depth-test/write flags；所有
reserved bytes 清零。纹理、透明、overlay 与逐顶点光命令明确拒绝，不会静默降级。

```sh
make gpu-raster-abi-test
build/rf-gpu-raster-pack-test
make win-gpu-raster-abi-test
```

测试覆盖静态 layout、确定性重复 pack、容量/unsupported 拒绝与 corruption validation。ABI 尚未由
Vulkan framebuffer 消费；GPU-5 已在独立 hosted raster owner 中消费该 ABI。

## GPU-5 Compute Rasterizer V1

`rf_gpu_raster` 独立于 GPU-3 gradient framebuffer smoke。Vulkan backend 直接上传并
验证 GPU-4 binary stream，compute shader 以 `uint32 words[]` 和固定 word offset 解码，
不建立 shadow command struct。每个 invocation 对应一个 pixel，按 command order
在局部执行 clear/triangle/depth/light/fog，最后写出 canonical `0xffRRGGBB`
color 和 signed-32 inverse depth。两个内嵌 SPIR-V 变体分别使用 16×16 和
8×8 local size，与 capability contract 选择一致。

```sh
make gpu-raster-test
build/rf-gpu-raster-test
make win-gpu-raster-test
# Windows PowerShell: .\\build\\rf-gpu-raster-test.exe
```

upload/readback 覆盖 coherent 与 non-coherent memory，显式 flush/invalidate，并有
host→compute、compute→transfer barrier 与 5 秒 fence timeout。测试使用固定
color/depth hash，包含 stride、resize、command upload growth、capability rejection 和
cleanup。WSL llvmpipe 与 Windows Intel Iris Xe 实机均已通过，五组固定
color/depth hash 完全一致。

## GPU Capability Contract V1

GPU-5 前的 portability gate 已收敛到 `rf_gpu_status`：GPU service READY 与
compute/framebuffer/raster_v1 独立 capability 分开。snapshot 仅含定宽整数、数组和
定长字符，不暴露 Vulkan handle/type。它记录 API version、adapter index/type/ID、
compute queue/workgroup limits、storage-buffer range/alignment、`shaderInt64`、
`nonCoherentAtomSize` 以及 memory heap/type size/flags，并派生 device-local output、
host-visible readback 与 coherent/non-coherent 可用性。

Raster V1 要求 compute、GPU-3 framebuffer memory path、非零 storage-buffer range、可用
workgroup 与 `shaderInt64`。CPU reference 的 edge/area 与 `edge * inv_z` 重心插值使用
64-bit integer，GPU-4 ABI 也以 `int64_t area` 冻结；在已允许的 framebuffer/坐标范围
不能将中间值证明为 signed 32-bit，因此不以 float 或溢出语义代替。
workgroup 按 limit 选择：满足 256 invocations 且 X/Y >= 16 时用 16x16；否则
满足 64 且 X/Y >= 8 时用 8x8；再不满足则仅 `raster_v1 unsupported`。

2026-09-16 实测：WSL llvmpipe（CPU）为 Vulkan 1.4.335、1024 invocations、
1024x1024x1024、128 MiB storage range/alignment 16、atom 64、单个
device-local+host-visible+coherent+cached type；Windows Intel Iris Xe（integrated）为
Vulkan 1.3.297、1024 invocations、1024x1024x64、1073741820-byte storage range/alignment 64、
atom 1，一个 device-local heap 与三个 type（包含 device-local+host-visible+coherent 组合）。
两者 `shaderInt64`、16x16、compute/readback 和 framebuffer smoke 均通过。RTX 3050 保留
GPU-1/GPU-3 历史验收，新 snapshot 待回到该机器复测。memory selection 仍为
`memoryTypeBits + required/preferred flags`；GPU-3 继续 device-local output → host-visible readback，
未引入 UMA zero-copy 或 vendor-specific path。

## GPU-6 CPU/GPU Differential Authority

`rf-gpu-raster-diff-test` 的 fixture 先由正式 `toy_renderer_triangle_lit()` 记录，再由 GPU-4
packer 产生唯一 Raster Command ABI V1 stream。比较阶段同一 stream 一路经薄 adapter 解码回
`toy_renderer` 的 flat triangle API 并执行正式 CPU raster，另一路由 Vulkan Compute Raster V1
执行并 readback；CPU X byte canonicalize 为 `0xff`，RGB24 与完整 signed 32-bit inverse depth
逐像素比较。hash 只用于快速确认，pixel/depth compare 是 correctness authority。

```sh
make gpu-raster-diff-test
build/rf-gpu-raster-diff-test
build/rf-gpu-raster-diff-test --replay-raster-stream commands.bin
make win-gpu-raster-diff-test
```

输出包含 mismatch count、首个坐标及 CPU/GPU 值、最大 RGB/depth delta、双方 color/depth hash，
以及 CPU raster 与 GPU validation/upload/submit/execution-wait/readback/total wall time。当前 fence
只能诚实报告 combined `execution-wait`；`GPU total` 包含 readback，不能解释为未来 native frame
time。发生 mismatch 时默认写入 `build/gpu-raster-diff-mismatch/`：完整 ABI `commands.bin`、
CPU/GPU/diff BMP、双方 depth binary 与 `report.txt`。fixture 覆盖 GPU-5 fixed cases、shared-edge
grid、equal-depth/near-far、offscreen/frame edges/thin/mixed size、light/fog、resize/command growth，
以及固定 seed `1`、`2`、`0x5246` 的 deterministic stress。

GPU-6 冻结规则：Raster V1 correctness 持续由 CPU reference differential test 约束；以后每新增
Raster ABI command 或 shading semantics，必须同时增加对应 differential fixture。

## GPU-6.5 CPU Tile Command Binning

正常 hosted GPU raster 仍为一个 workgroup 对应一个 tile、一个 invocation 对应一个 pixel，tile 尺寸
直接使用 capability contract 选出的 16×16 或 8×8。CPU 从冻结的 Raster ABI V1 bbox 构造
`tile_offsets[tile_count+1]` 和 `tile_command_indices[total_refs]`；indices 指向原 command stream，
不复制 payload。两遍 count/prefix/fill 按原 command index 填充，因此每个 tile 严格保持 stream order。
V1 的两个前导 clear 被加入所有 tile；当前 ABI validator 不允许中途或额外 clear，因此不存在提升
clear 的状态变换。

offset/index 的 CPU 和 Vulkan buffer 分别持有 capacity，增长采用 replacement-first，所有 tile/ref
算术检查溢出和 storage-buffer limit，失败不截断 command。`make gpu-raster-binning-test` 覆盖 tile
边界、单/多/full/thin 覆盖、8×8/16×16、顺序、增长和非法 stream；differential test 对同一 stream
依次运行 CPU、保留的 diagnostic full-scan pipeline 与正常 tile-binned pipeline，并输出 binning、两类
upload、submit、两种 execution-wait、readback 以及 command/tile/ref 统计。

WSL llvmpipe 的 1279×719 / 1026 commands stress 实测为 3600 tiles、1,540,976 refs、平均
428.049、最大 591；CPU binning 7.073 ms，full-scan / binned execution-wait 为 1321.763 /
635.771 ms，CPU/full-scan/binned 三方 color/depth 均 0 mismatch。该数值只代表当次软件 Vulkan
运行。Windows Intel Iris Xe 同一 workload 为 CPU raster 457.160 ms、CPU binning 8.343 ms、
tile-list upload 1.280 ms、full-scan / binned execution-wait 230.272 / 163.568 ms、readback
35.739 ms；全部 fixture、stress 与 replay 的 CPU/full-scan/binned color/depth 均 0 mismatch。
因此 GPU-6.5 标记 DONE / FROZEN，并具备进入 GPU-7 Normal World Migration 的性能基础。

## 注意事项

- `wgpu-native` 依赖 Rust 标准库和多个系统图形 backend；
- 它与 Rasterfall 当前的 freestanding/Tinylibc 构建路径不是直接兼容关系；
- `ffi/webgpu-headers` 和 `examples/vendor/glfw` 是项目子模块；
- GPU 试验优先使用 hosted 构建目标，不影响 Rasterfall 现有 CPU 路径。
