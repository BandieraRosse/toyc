# Rasterfall GPU 与 Windows Native Platform 总体计划

> 状态：执行中（GPU-0 至 GPU-7B 及 GPU Capability Contract V1 已完成并冻结）
> 进展同步：2026-09-16
> 源码核对基线：GPU-4 Raster Command ABI V1 / deterministic pack-validation
> 源码核对基线：GPU-5 前 portability gate 已建立无 Vulkan handle capability snapshot、独立 Raster V1 gate 与 limit-driven 16x16/8x8 workgroup policy；WSL llvmpipe / Windows Intel Iris Xe 实测通过。
> 源码核对基线补充：GPU-5 已完成 GPU-4 word-stream 显式解码、逐像素 64-bit integer triangle/depth/light/fog 与 deterministic color/depth readback；WSL llvmpipe / Windows Intel Iris Xe fixed fixtures 的 color/depth hash 一致并通过。
> 源码核对基线补充：GPU-6 建立正式 `toy_renderer` CPU oracle、逐 RGB24/signed-depth differential、mismatch artifact、ABI replay 与三组 deterministic stress；WSL llvmpipe / Windows Intel Iris Xe 实测均 0 mismatch，GPU hashes 继续 bit-exact。
> 方向：Vulkan GPU Runtime / Compute Rasterizer / Windows Native Platform
> 原则：保持 CPU renderer 与现有 Linux 路径稳定，以渐进方式引入 GPU 算力，并逐步收回 Windows 平台层所有权。

## 当前进展

| Checkpoint | 状态 | 可复核事实 |
| --- | --- | --- |
| GPU-0 Vulkan Probe | 已完成 | 自有最小 Vulkan 1.0 ABI、动态 loader、adapter / device / queue 生命周期 |
| GPU-0.5 Windows Hardware Bring-up | 已完成 | Windows 原生枚举 AMD integrated 与 NVIDIA RTX 3050 Laptop GPU，discrete-first 选中 NVIDIA |
| GPU-1 Compute Ownership | 已完成 | WSL llvmpipe 与 Windows RTX 3050 均通过 storage-buffer compute/readback：`1 2 3 4 -> 4 7 10 13` |
| GPU-2 RF GPU Core Service | 已完成 | Core-owned service contract 与持久 Vulkan loader/instance/device/queue backend；hosted probe 经正式 service 完成 compute/readback 和逆序 shutdown |
| GPU-3 Frame Ownership | 已完成 | hosted smoke 复用持久 backend；device-local XRGB8888 output 经 compute、barrier、host-visible readback 进入 `toy_surface`，覆盖 stride、hash、resize 与逆序 shutdown |
| GPU-4 Raster Command ABI V1 | 已完成 | 32-byte versioned stream header + 96-byte pointer-free commands；CPU `toy_raster_cmd` 显式 pack/validation，覆盖 clear color/depth 与 opaque flat triangle 的 depth/fog 输入；独立 layout/packing 双平台构建门禁 |
| GPU Capability Contract V1 | 已完成 | GPU-5 前 portability gate；service/renderer capability 分层、无 handle snapshot、`shaderInt64` Raster V1 requirement、limit-driven workgroup 与通用 memory property selection |
| GPU-5 Compute Rasterizer V1 | 已完成 | GPU 直接消费 GPU-4 binary stream；WSL llvmpipe / Windows Intel Iris Xe 的 color/depth fixed fixtures、resize/growth/shutdown 已通过且 hash 一致 |
| GPU-6 Differential Authority | 已完成 / FROZEN | 同一 Raster ABI V1 stream 经正式 CPU renderer 与 Vulkan Raster V1；fixed/stress/replay 在 llvmpipe / Intel Iris Xe 均为 color/depth 0 mismatch |
| GPU-6.5 Tile Command Binning | 已完成 / FROZEN | CPU bbox 两遍保序 binning，workgroup=tile，full-scan A/B；WSL/Intel Iris Xe 全部 differential 0 mismatch，stress execution-wait 明显下降 |
| GPU-7A Normal World Flat-Opaque Slice | 已完成 / FROZEN | WSL llvmpipe、Windows Iris Xe、Windows RTX 3050 三平台 replay 均 0 mismatch；RTX execution-wait 3.851--4.519 ms，GPU total 含约 49--51 ms readback |
| GPU-7B Vertex-Lit Planar Extension | 已完成 / FROZEN | V1 96-byte 新 command kind；CPU/GPU 共享 raster truth，WSL/RTX fixed、stress、combined-world 全部 0 mismatch，world coverage 99.85--99.91% |
| Windows Native Platform | 未开始 | 正常 Windows Rasterfall 仍使用 MinGW + SDL2，本阶段未改窗口、输入、音频或 presentation |

当前边界：

* GPU probe 是 hosted service frontend；Vulkan loader、instance、physical/logical device 与 queue 由持久 backend 拥有，正常 runtime 尚未选择该 hosted backend；
* CPU renderer 仍是唯一正常游戏渲染路径；
* 已有 hosted GPU framebuffer、GPU raster command ABI V1 与 Compute Rasterizer V1；正常 runtime 尚未消费 GPU raster，也尚无 Vulkan surface 或 swapchain；
* Windows `total` 首次观测包含 resource / descriptor / pipeline 创建，不作为稳态 GPU 性能结论。

当前复核入口：

```sh
make gpu-probe
build/rf-gpu-probe
make gpu-framebuffer-test
build/rf-gpu-framebuffer-test
make gpu-raster-abi-test
build/rf-gpu-raster-pack-test
make win-gpu-probe
make win-gpu-framebuffer-test
make win-gpu-raster-abi-test
# Windows PowerShell: .\build\rf-gpu-framebuffer-test.exe
```

---

## 1. 背景

Rasterfall 当前以自研 C 软件栅格器为主要渲染后端。

现有 CPU renderer 已能够支持：

* world geometry；
* static props；
* skeletal / modular characters；
* infected enemy models；
* Static World Lighting V2；
* texture / material；
* HUD、GUI、Desktop、Terminal；
* 离屏 capture、logic test 和固定性能 benchmark。

随着场景复杂度、角色数量和视觉系统增加，CPU 光栅化已经逐渐接近当前目标帧预算。

GPU 因此进入 Rasterfall 的正式演进路线。

本计划的目标不是一次性用 Vulkan 重写 renderer，而是建立一条可验证、可回退、可以逐步理解和控制的 GPU 路线：

```text
发现 GPU
    ↓
创建 device / queue
    ↓
执行 compute program
    ↓
管理 GPU memory
    ↓
生成 framebuffer
    ↓
消费 RF raster command
    ↓
接管 world rasterization
    ↓
原生 GPU presentation
    ↓
GPU-native visual features
```

同时，Windows 将从目前的 MinGW + SDL2 平台逐步演进为由 RF Core 自己管理的 Win32 平台后端。

---

# 2. 总体目标

最终希望形成以下结构：

```text
                    Rasterfall Game
                          │
                    RF Game Runtime
                          │
                       RF Core
          ┌───────────────┴────────────────┐
          │                                │
      RF Graphics                      RF Platform
   ┌──────┴──────┐                 ┌───────┴───────┐
CPU Raster    GPU Raster          Linux           Windows
   │              │                │                │
Reference        rf_gpu         existing        RF Win32
Renderer           │            platform         platform
                   │
                Vulkan
          ┌────────┴────────┐
          │                 │
        WSL              Windows
      llvmpipe          physical GPU
```

核心原则：

1. CPU renderer 长期保留。
2. GPU 是 RF Core 的可选能力，而不是 Game 状态。
3. GPU service 与 GPU renderer 分离。
4. Vulkan backend 不直接污染 gameplay / session。
5. WSL 继续作为主要开发环境。
6. Windows 成为当前机器上的 GPU hardware / performance truth。
7. Windows 平台逐步移除 SDL2，而不是为 GPU 一次性重写。
8. 原生 Vulkan surface / swapchain 不作为早期 GPU 开发前置条件。

---

# 3. CPU Renderer 的长期定位

GPU 接入不意味着淘汰软件渲染器。

CPU renderer 后续承担三个角色：

```text
Compatibility Renderer
Reference Renderer
GPU Differential Oracle
```

## Compatibility Renderer

在以下情况继续运行：

* GPU 不存在；
* Vulkan 不可用；
* GPU backend 初始化失败；
* 用户显式选择 CPU；
* GPU command 尚未覆盖某些诊断路径。

## Reference Renderer

CPU renderer 已经定义了一套经过长期验证的 Rasterfall raster semantics。

GPU V1 应优先复刻这些规则，而不是立即建立完全不同的视觉系统。

## Differential Oracle

固定 command stream 可以分别交给 CPU 与 GPU：

```text
same render input
      │
 ┌────┴────┐
 CPU      GPU
 │          │
color     color
depth     depth
 └────┬────┘
      ↓
 differential test
```

这将成为 GPU backend 后续扩展的重要验证基础。

---

# 4. 开发环境职责

## 4.1 WSL：Primary Development Environment

当前 WSL：

```text
/dev/dxg available
NVIDIA CUDA/NVML available
Vulkan → llvmpipe
```

Vulkan 当前只能通过 Mesa software implementation 执行。

因此 WSL 的定位是：

> Vulkan correctness environment，而不是 GPU performance environment。

WSL 负责：

* 日常 Codex / C 开发；
* Vulkan API 生命周期；
* ABI 验证；
* resource ownership；
* buffer / memory；
* descriptor；
* pipeline；
* command buffer；
* synchronization；
* shader correctness；
* CPU/GPU differential tests；
* error path；
* deterministic tests；
* normal Linux Rasterfall regression。

llvmpipe 的性能数据不用于判断 RF GPU renderer 是否有效。

---

## 4.2 Windows：GPU Hardware Truth

当前机器真实 NVIDIA GPU 从 Windows 原生 Vulkan 路径访问。

Windows 负责：

* physical GPU detection；
* NVIDIA Vulkan driver 验证；
* compute correctness；
* GPU execution timing；
* upload / download cost；
* workgroup / tile tuning；
* memory bandwidth behavior；
* renderer performance；
* native presentation；
* GPU stress testing。

因此：

```text
WSL asks:
    Is it correct?

Windows asks:
    Is it really running on GPU?
    Is it fast?
```

---

## 4.3 Native Linux GPU

原生 Linux + hardware Vulkan 是未来发布验证环境。

它不是当前 GPU bring-up 的前置条件。

未来形成：

```text
WSL llvmpipe
    development / correctness

Windows NVIDIA
    hardware / performance

Native Linux GPU
    Linux release validation
```

---

# 5. GPU Phase 0 — Vulkan Probe

第一阶段已经建立最小 Vulkan hosted probe。

当前已完成：

* minimal Vulkan ABI；
* dynamic Vulkan loader；
* VkInstance；
* physical-device enumeration；
* adapter information；
* queue-family enumeration；
* compute-capable adapter selection；
* VkDevice；
* VkQueue；
* clean destruction；
* no Vulkan SDK dependency；
* no VMA / Volk / wgpu-native；
* existing freestanding Rasterfall build unaffected。

WSL 当前结果为：

```text
Vulkan implementation:
    Mesa llvmpipe

device type:
    CPU

device / queue lifecycle:
    PASS
```

该结果证明 Vulkan API 链路正确，但不代表真实 GPU 性能。

---

# 6. GPU Phase 0.5 — Windows Hardware Bring-up

> 实现状态（2026-09-16）：**已完成。** Windows 原生 loader 枚举 RTX 3050
> Laptop GPU，device type 为 discrete，compute-capable queue、VkDevice、VkQueue 和清理
> 生命周期均已由同一份共用 probe 验证。

该阶段要求现有 probe 在 Windows 原生运行。

平台差异只允许存在于 Vulkan loader 边界。

```text
Linux:
    libvulkan.so.1

Windows:
    vulkan-1.dll
```

其余 Vulkan runtime 代码应尽可能共享。

目标：

```text
Windows
   ↓
vulkan-1.dll
   ↓
NVIDIA Vulkan ICD
   ↓
physical NVIDIA GPU
```

验收：

* 正确枚举 NVIDIA GPU；
* device type 为 discrete / hardware GPU；
* 找到 compute-capable queue；
* VkDevice 创建成功；
* VkQueue 获取成功；
* clean destruction；
* 与 WSL 使用相同 Vulkan ABI。

此 checkpoint 完成后，RF 才拥有第一个真实 GPU Vulkan execution environment。

---

# 7. GPU Phase 1 — Compute Ownership

> 实现状态（2026-09-16）：**GPU-0.5 / GPU-1 双平台验收完成。** 共用 hosted probe 已完成 storage buffer、host-visible/
> coherent memory、descriptor、内嵌 SPIR-V pipeline、command buffer、dispatch、fence 和
> readback verification；WSL llvmpipe 已通过。Windows 原生枚举 AMD integrated 与 NVIDIA
> RTX 3050 Laptop GPU，discrete-first 策略明确选择 NVIDIA，compute/readback PASS。
> 当次 Windows 观测：upload 0.005 ms、submit 0.260 ms、execution-wait 0.134 ms、
> readback 0.002 ms；total 233.853 ms 包含首次资源与 pipeline 创建，不作为稳态
> dispatch 性能结论。

这一阶段首次真正让 Rasterfall 提交 GPU 工作。

需要建立：

```text
buffer
device memory

descriptor set layout
descriptor pool
descriptor set

shader module
compute pipeline

command pool
command buffer

dispatch
fence

readback
verification
```

测试应保持极简。

例如：

```text
input:
    1 2 3 4

compute:
    x = x * 3 + 1

expected:
    4 7 10 13
```

必须同时在：

```text
WSL / llvmpipe
Windows / NVIDIA
```

通过。

这个 checkpoint 的意义是：

> Rasterfall 已经可以自行创建 GPU resources、运行自己的 GPU program，并读取和验证结果。

Windows 同时开始记录：

```text
upload
GPU execution
wait
readback
total
```

从这一阶段开始，Windows 性能数据具有正式意义。

---

# 8. GPU Phase 2 — RF GPU Core Service

> 实现状态（2026-09-16）：**GPU-2A、GPU-2B 已完成。** `rf_gpu` 已由
> `struct rf_core` 持有，定义 disabled / optional / required policy、disabled /
> unavailable / ready / failed 状态、无 Vulkan 类型的 adapter/status snapshot、backend
> 生命周期与逆序 shutdown。optional backend 缺失或失败时继续 CPU renderer；required
> 返回启动失败。`rf_gpu_vulkan_backend` 持久拥有动态 loader、instance、选中 adapter、device
> 与 queue；初始化沿用 GPU-1 compute/readback 门禁，shutdown 按 device → instance → loader 逆序释放。
> `make gpu-service-test` 覆盖 policy 与 READY-only shutdown，`make gpu-probe && build/rf-gpu-probe`
> 通过正式 service/backend 覆盖真实 Vulkan bring-up。正常 Rasterfall 仍显式使用 disabled，CPU
> renderer 不变；surface、swapchain 与 GPU framebuffer 从 GPU-3 开始。

经过 probe 和 compute smoke 验证后，再把实验代码收敛为正式 Core service。

推荐边界：

```text
RF Core
   │
   └── rf_gpu
        │
        └── Vulkan backend
```

`rf_gpu` 负责：

* GPU availability；
* adapter selection；
* device；
* queue；
* memory；
* buffer；
* command submission；
* synchronization；
* status / error reporting。

Game 不允许看到：

```text
VkInstance
VkPhysicalDevice
VkDevice
VkQueue
VkBuffer
VkDeviceMemory
VkFence
```

GPU 此时仍不等于 renderer。

允许出现：

```text
GPU:
    READY

Renderer:
    CPU
```

这是一种正常、重要的运行状态。

---

# 9. GPU Phase 3 — GPU Framebuffer Smoke

> 实现状态（2026-09-16）：**已完成。** `rf_gpu_framebuffer` 是 service 层无 Vulkan
> handle 的 resource。Vulkan backend 复用 GPU-2 持久 instance/device/queue，为 framebuffer
> 独占 device-local storage/transfer-src output、host-visible transfer-dst readback、compute
> pipeline、descriptor、command pool/buffer。固定 compute 图案为 XRGB8888 线性渐变，提交后以
> compute-write → transfer-read barrier 和 buffer copy 回读，再按目标 stride 复制至 `toy_surface`。
> readback 优先 coherent memory，否则 map 后显式 invalidate；fence timeout 固定为 5 秒。
> resize 先建立 replacement，成功后释放旧资源，不重建 persistent backend；partial failure
> 统一逆序清理。WSL llvmpipe 与 Windows RTX 3050 均通过 64×48 → 400×240 完整像素/hash、
> stride padding、resize 与 shutdown；Windows probe 同时确认 discrete adapter 为 RTX 3050。

下一步让 GPU 第一次参与 Rasterfall frame lifecycle。

compute shader 输出固定测试图案：

```text
GPU framebuffer
      ↓
readback
      ↓
toy_surface
      ↓
existing window present
```

暂时不渲染 world。

此阶段确定：

* framebuffer format；
* width / height；
* stride；
* resize；
* staging buffer；
* device-local memory；
* host-visible memory；
* coherent / non-coherent rules；
* fence timeout；
* multi-frame resource lifetime。

此阶段仍不要求：

* Vulkan surface；
* swapchain；
* graphics pipeline；
* normal Rasterfall scene。

---

# 10. GPU Phase 4 — Raster Command ABI V1（已完成）

GPU renderer 不直接依赖 `toy_renderer` 的内部 C command layout。

建立独立、定宽、无指针 GPU command representation，例如：

```text
rf_gpu_raster_cmd_v1
```

要求：

* fixed-width fields；
* no CPU pointer；
* explicit resource handle；
* explicit command kind；
* versioned layout；
* deterministic packing。

初期允许：

```text
existing CPU frontend
        ↓
toy raster semantics
        ↓ pack
rf_gpu_raster_cmd_v1
```

而不是立即重写完整 render frontend。

第一批支持：

* framebuffer clear；
* depth clear；
* opaque flat triangle；
* depth test；
* depth write；
* fixed color；
* fog。

当前 V1 实现位于 `rasterfall/include/rf_gpu_raster_abi.h` 与
`gpu/src/rf_gpu_raster_pack.c`。stream 固定为 little-endian，header 为 32 bytes，command 为
96 bytes；所有 reserved bytes 在 pack 时清零并在 validation 时检查。V1 resource handle 字段
显式存在，但 flat triangle 不依赖外部资源，因此必须为零。

pack 输入是 CPU renderer 已裁剪并记录的 command pool，加上该 frame 的 clear color 和固定
depth clear 0。它只接受 opaque、constant-light、non-overlay flat triangle，保存三顶点屏幕坐标、
Q16 inverse depth、负 winding area、clamped bbox、fixed color、Q8 light/fog，并声明 depth test
与 depth write；纹理、透明、逐顶点光、overlay 均明确返回 unsupported。GPU-4 不执行任何
rasterization，也不接正常 runtime。`make gpu-raster-abi-test` 验证 deterministic byte packing、
拒绝路径、stream corruption 与静态 layout；`make win-gpu-raster-abi-test` 是 LLP64 layout 构建门禁。

---

# 11. GPU Phase 5 — Compute Rasterizer V1（已完成）

第一版 GPU rasterizer 使用 compute，而不是立即转为传统 Vulkan graphics pipeline。

主要原因：

* 更容易复刻 CPU integer raster semantics；
* 更容易验证 exact output；
* 更容易观察内部算法；
* 不需要过早建立完整 graphics pipeline architecture。

推荐初始模型：

```text
screen
  ↓
tiles

one workgroup
  =
one tile

one invocation
  =
one pixel
```

最初每个 tile 可以直接扫描 command list：

```text
for command:
    bbox reject

    coverage test

    depth test

    shade
```

每个 invocation 独占一个 pixel 的 color/depth ownership。

这样第一版避免复杂的：

* color write races；
* depth atomics；
* triangle ordering races。

后续性能优化可以增加：

```text
tile binning
command compaction
GPU preprocessing
```

但不能成为 V1 正确性的前置条件。

当前实现使用独立 `rf_gpu_raster` owner。shader 将 GPU-4 stream 作为
little-endian `uint32 words[]` 并按冻结 word offset 解码，不依赖 GLSL/C struct
layout。16×16 与 8×8 两个固定 SPIR-V 变体由 capability 选择；每个
invocation 按 stream order 在局部持有 color/depth，最后各写一次
device-local buffer。color 存储 canonical `0xffRRGGBB`，depth 为 signed 32-bit
inverse depth。

upload 支持 coherent 与 non-coherent host-visible memory；后者按 allocation range
flush，readback 对 non-coherent memory invalidate。command buffer 显式记录 host-write →
compute-read 与 shader-write → transfer-read barrier，fence 有 5 秒上限。
`build/rf-gpu-raster-test` 覆盖 fixed color/depth hash、coverage/shared edge、depth
interpolation/overlap/equality order、offscreen/thin/degenerate、light/fog、stride、resize、
upload growth 与 shutdown。GPU-6 仍拥有通用 CPU↔GPU differential authority。

---

# 12. GPU Phase 6 — Differential Raster Testing

建立正式 CPU ↔ GPU raster differential test。

输入：

```text
same command stream
same framebuffer
same camera
same lighting
same fog
```

输出至少包括：

```text
color mismatch count
depth mismatch count

first mismatch coordinate

maximum RGB difference
maximum depth difference

CPU raster time

GPU upload time
GPU execution time
GPU wait time
GPU readback time
```

之后每增加一种 GPU raster capability，都必须经过该入口。

这一阶段把 CPU renderer 正式确立为 GPU V1 的 reference implementation。

状态：**DONE / FROZEN（2026-09-16）**。实现不建立第二套 raster truth：fixture 使用正式 renderer
记录并 pack，CPU 比较路径仅把 ABI 字段适配回正式 flat triangle API。报告同时给出逐像素定位与
hash；mismatch 自动保存 ABI stream、颜色/depth 与报告，可由 `--replay-raster-stream` 重放。
WSL llvmpipe 与 Windows Intel Iris Xe 的 fixed、组合、三组固定 seed stress 和 replay 均为 0
mismatch。计时为 wall-clock 分段，当前仅能报告 combined execution-wait，GPU total 包含 readback。

---

# 13. GPU Phase 7 — World Raster Migration

正常 world 按价值逐项迁移。

推荐顺序：

```text
flat opaque
    ↓
planar vertex lighting
    ↓
fog
    ↓
nearest texture
    ↓
bilinear texture
    ↓
static props
    ↓
infected
    ↓
RF humanoid
    ↓
anime characters
    ↓
advanced materials
    ↓
transparent / edge / special passes
```

第一版正常 GPU world 不需要立即 GPU 化 HUD。

允许：

```text
GPU:
    world
    props
    characters
    enemies
    depth effects

readback

CPU:
    HUD
    GUI
    Desktop
    Terminal
    screen-space overlays

present
```

这样 GPU 可以更早接管真正昂贵的 world rasterization。

---

# 14. Windows Platform Phase 1 — Native Window Layer

GPU compute 与基础 raster 跑通之后，再开始正式替换 SDL。

不要与 GPU bring-up 同时大规模改平台层。

第一阶段优先替换：

```text
SDL window
SDL input
SDL framebuffer presentation
```

为：

```text
Win32 window
Win32 input
Win32 CPU framebuffer present
```

建立：

```text
RF Core
   ↓
RF Windows Platform
   ↓
Win32
```

目标包括：

* native window creation；
* message loop；
* keyboard / mouse；
* resize；
* focus；
* framebuffer present；
* native HWND ownership。

获得 HWND 后，也为未来：

```text
VK_KHR_win32_surface
```

建立正式平台基础。

---

# 15. Windows Platform Phase 2 — SDL Removal

之后逐项收回平台能力。

推荐顺序：

```text
Window
    ↓
Input
    ↓
Clock
    ↓
Dynamic library
    ↓
Filesystem
    ↓
Threads / synchronization
    ↓
Audio
```

目标 API：

```text
Win32 Window API
Raw Input / Windows Messages
QueryPerformanceCounter
LoadLibrary / GetProcAddress
CreateFile / ReadFile
Win32 thread / event / SRW
WASAPI
Winsock
```

网络已有 Windows 平台实现时应尽量复用，不做无意义重写。

Audio 放在最后，因为其迁移复杂度明显高于窗口、输入、时钟和文件系统。

---

# 16. Windows Platform Phase 3 — SDL-free Rasterfall

该阶段建立一个独立 checkpoint：

> Rasterfall Windows runtime no longer requires SDL2.

目标结构：

```text
Rasterfall.exe
    │
    ├── RF Game
    ├── RF Core
    ├── RF Win32 Platform
    ├── CPU Renderer
    └── RF GPU Vulkan
```

主要外部边界仅保留操作系统和设备 ABI，例如：

```text
Windows system libraries
Winsock
Windows audio
Vulkan loader
GPU driver
```

目标不是消灭操作系统依赖，而是：

> 除正式 OS / driver ABI 外，Rasterfall 的用户态运行基础尽可能由自己的 C 代码拥有。

---

# 17. GPU Phase 8 — Native Vulkan Presentation

Windows Native Platform 建立后，才能自然进入 Vulkan surface。

```text
HWND
 ↓
VK_KHR_win32_surface
 ↓
VkSurfaceKHR
 ↓
VkSwapchainKHR
```

最终：

```text
GPU raster
    ↓
GPU image
    ↓
swapchain
    ↓
present
```

删除：

```text
GPU
 ↓
CPU readback
 ↓
software window present
```

这是 Windows GPU renderer 性能真正释放的重要节点。

Linux Vulkan native surface 留到 Linux GPU/backend 条件成熟后单独处理。

---

# 18. GPU Phase 9 — GPU-native Visual Renderer

GPU V1 的目标是：

> 复刻并加速现有 Rasterfall rendering semantics。

当 V1 稳定后，可以开始 GPU V2。

V2 不再要求所有效果都能由 CPU renderer 完整复制。

候选方向：

* GPU skinning；
* GPU vertex transform；
* tile binning；
* dynamic lighting；
* shadow；
* large particle systems；
* GPU light field；
* post-processing；
* screen-space effects；
* higher internal resolution；
* advanced material model。

届时：

```text
CPU Renderer
    =
compatibility + reference

GPU Renderer
    =
primary high-end visual backend
```

---

# 19. Vulkan Loader 边界

当前动态加载系统 Vulkan loader 是可接受的正式设计。

Linux：

```text
libvulkan.so.1
```

Windows：

```text
vulkan-1.dll
```

RF 自己负责：

* Vulkan ABI declarations；
* dispatch table；
* device ownership；
* resource ownership；
* synchronization；
* renderer implementation。

不把重新实现完整 Vulkan loader、ICD discovery 或 ELF dynamic linker 作为 GPU 项目前置任务。

这些可以作为未来独立的 Tinylibc / RF Core 低层研究方向。

---

# 20. 非目标

当前路线明确不要求：

* 一次性删除 CPU renderer；
* 一次性 Vulkan 化整个 renderer；
* 立即建立 Vulkan swapchain；
* 立即 GPU 化 HUD / Desktop / Terminal；
* WSL 获得真实 NVIDIA Vulkan；
* 为 WSL 自编 Mesa / Dozen；
* 在 WSL 安装普通 Linux NVIDIA display driver；
* 开始就使用 bindless；
* VMA；
* Volk；
* wgpu；
* Rust GPU runtime；
* runtime shader compiler；
* 自己实现完整 GLSL compiler；
* 自己实现完整 Vulkan loader；
* 同时重写 Windows platform 和 GPU renderer。

---

# 21. 近期关键路径

截至 2026-09-16，GPU-0 至 GPU-4 与 GPU Capability Contract V1 已完成，当前优先级为：

```text
Completed:
GPU-0 / GPU-0.5 / GPU-1 / GPU-2 / GPU-3 / GPU-4
WSL llvmpipe framebuffer smoke
            │
            ▼
Completed portability gate:
GPU Capability Contract V1
            │
            ▼
Completed:
GPU-5 Flat Compute Rasterizer
WSL llvmpipe / Windows Iris Xe matching hashes
            │
            ▼
GPU-6
CPU / GPU Differential Test
            │
            ▼
GPU-7
Normal World Migration
            │
            ├───────────────┐
            │               │
            ▼               ▼
      GPU World         WIN-1
                       Native Win32 Platform
                            │
                            ▼
                       SDL-free Runtime
                            │
                            ▼
                       Vulkan Swapchain
                            │
                            ▼
                   GPU-native Rasterfall
```

---

# 22. 关键 checkpoint 定义

## GPU-0.5 — Hardware Bring-up

Windows 原生 Vulkan 成功枚举并创建真实 NVIDIA GPU device。

## GPU-1 — Compute Ownership

RF 自己创建 GPU memory、pipeline 和 command buffer，并在物理 GPU 上执行并验证 compute result。

## GPU-3 — Frame Ownership

GPU 第一次真正进入 Rasterfall frame lifecycle，并生成可显示 framebuffer。

## GPU-5 — Raster Ownership

GPU 第一次消费 RF raster commands 并正确执行 depth-tested triangle rasterization。

## GPU-6 — Differential Authority

CPU renderer 成为自动化 GPU raster correctness reference。

## GPU-6.5 — Raster Scalability / Tile Command Binning

CPU 按 Raster ABI V1 bbox 建立保序 tile offsets/command indices，tile 尺寸等于 capability contract
选择的 workgroup。正常 hosted raster 只扫描本 tile list；保留同 shader semantics 的 full-scan
diagnostic A/B。WSL llvmpipe 与 Windows Intel Iris Xe 已完成全部 differential、stress 与 replay，
color/depth 均 0 mismatch；Intel 最大 stress execution-wait 从 230.272 ms 降至 163.568 ms，CPU
binning 为 8.343 ms。GPU-6.5 DONE / FROZEN，下一阶段可进入 GPU-7，但本阶段未迁移正常 world。

## GPU-7 — Playable GPU World

正常 Rasterfall world 的主要 raster workload 从 CPU 转移到 GPU。

GPU-7A 先完成 diagnostic-only flat opaque slice：正常 frontend → existing `toy_raster_cmd` → 分类 →
GPU-4 packer → 同一 selected Raster V1 stream 的 CPU/GPU differential。固定 near/mid 与 0/30 enemy
场景由 `--gpu-world-raster-test` 录制；hosted replay 默认使用 tile binning。它不做完整 CPU/GPU
hybrid depth、不扩 Raster ABI、不启用 normal GPU renderer。开发和验收以当前机器实际可用 adapter
为准，不再把某台 Intel Iris Xe 作为固定基线；缺少物理 GPU 时明确保留硬件 timing 门禁。

GPU-7A 于 Windows RTX 3050 Laptop GPU 完成最终门禁：near/0、near/30、mid/30 的
color/depth mismatch 与 max delta 全为 0；execution-wait 分别 3.851、4.519、4.517 ms，GPU total
分别 57.094、58.949、56.055 ms，其中包含 49.829、50.887、49.112 ms readback，不代表未来 native
frame time。hardware matrix 为 WSL llvmpipe PASS、Windows Iris Xe PASS、Windows RTX 3050 PASS，
GPU-7A DONE / FROZEN。

迁移前（Linux normal default 为 textures disabled）：near/0 为
16658/26832 supported（62.08%），near/30 为 24817/34991（70.92%），mid/30 为
25061/36213（69.20%），均包含两条 clear。三组 llvmpipe tile-binned replay 均 color/depth 0 mismatch；refs 分别为
41293、83833、72763，CPU reference 为 34.051、49.100、45.286 ms，llvmpipe GPU total 为
260.200、360.305、365.134 ms。该软件 Vulkan timing 不代表物理 GPU 收益。最大 unsupported family
是 vertex-lit planar（10144--11100 commands），远高于 texture（28--30），因此 GPU-7B 数据驱动
优先项为 vertex-lit planar，而不是 texture。MinGW normal game 与 differential tool 构建通过；当前
GPU-7B 在不改变 V1 header、96-byte record 或 flat command 的前提下新增 vertex-lit command kind；
原 flat payload 的最后三个 Q8 words 在新 kind 中表达 a/b/c light。shader 复用相同 coverage、edge、
depth、order、fog 与 framebuffer write，仅按 kind 选择 constant light 或 signed 64-bit edge-weighted、
toward-zero vertex-light interpolation。WSL fixed/stress/replay 与三组 combined-world differential 均为
0 mismatch。combined coverage 为 near/0 26802/26832（99.88%）、near/30 34961/34991（99.91%）、
mid/30 36161/36213（99.85%）；剩余为 texture 30/30/28，另有 mid transparent 24。RTX 3050
三组 combined replay 的 refs 为 90575/133115/134701，平均 25.160/36.976/37.417，最大
2585/2709/2277；CPU raster 为 15.609/28.657/38.716 ms，CPU binning 为
1.072/1.778/1.489 ms，tile upload 为 0.358/0.429/0.338 ms，command upload 为
0.490/0.551/0.474 ms，submit 为 0.095/0.108/0.080 ms，execution-wait 为
5.192/5.494/5.046 ms，readback 为 48.990/49.584/49.472 ms，GPU total 为
56.621/58.697/57.472 ms。GPU total 包含 readback，不是完整 normal frame 或未来 native frame time。
Windows normal frontend capture 的 frontend/classification/pack 分别为 near/0
13.629/5.481/1.388 ms、near/30 19.415/8.866/1.747 ms、mid/30 21.555/6.453/2.082 ms；
三份 Windows stream 与 Linux stream 逐字节一致。Windows fixed/stress/vertex-lit/replay 与 WSL
同为 0 mismatch；GPU-7B DONE / FROZEN。正常 renderer 继续 disabled/CPU。

## WIN-1 — Native Windows Platform

Windows Rasterfall 的窗口、输入与 framebuffer presentation 不再依赖 SDL。

## WIN-2 — SDL-free Runtime

正常 Windows Rasterfall 不再要求 SDL runtime。

## GPU-8 — Native Presentation

Vulkan renderer 直接向 Windows swapchain present，不再执行 GPU → CPU framebuffer readback。

## GPU-9 — Native Visual Renderer

GPU backend 开始支持超出现有 CPU renderer 能力范围的高级视觉效果。

---

# 23. 最终方向

这条路线不是简单的：

> “把 Rasterfall 从 CPU renderer 换成 Vulkan。”

最终目标是：

> Rasterfall 同时拥有一个可验证的软件参考 renderer、一个由 RF Core 管理的 GPU runtime、一套自己的 GPU raster architecture，以及逐步脱离第三方平台封装的 Windows native runtime。

开发环境也不再要求所有能力必须存在于同一系统中：

```text
WSL
    high-efficiency development
    correctness
    deterministic regression

Windows
    physical GPU
    hardware behavior
    performance
    native presentation

Native Linux
    future release validation
```

这样 GPU 算力的加入不会破坏 Rasterfall 已有的底层自主性。

相反，它把原来已经建立的：

```text
RF Core
CPU renderer
platform abstraction
render frontend
diagnostic infrastructure
```

继续向下一层扩展到：

```text
GPU device
GPU memory
GPU program
GPU rasterization
native presentation
```

最终形成一套真正属于 Rasterfall 的跨 CPU / GPU 图形运行环境。
