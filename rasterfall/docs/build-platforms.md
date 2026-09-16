# 构建、平台与验证

> 文档更新：2026-09-16
> 源码核对基线补充：GPU-7A 增加正常 world selected-stream capture；Linux/Windows 游戏编译 GPU-4 packer，但 Vulkan backend 仍只在 hosted test 中。real-world replay 默认 tile-binned，跳过大 stream 的 full-scan。
> 源码核对基线补充：GPU-7B 的 Linux/MinGW ABI 与 differential 构建已通过；WSL/Windows RTX 3050 vertex-lit fixtures、stress、combined-world replay 全部 0 mismatch，GPU-7B DONE / FROZEN。
> 源码核对基线补充：Windows package-layout console diagnostic 仅用于本次 frontend timing；三份 Windows capture stream 与 Linux stream 逐字节一致，不改变正常 GUI subsystem 或发布内容。
> 源码核对基线补充：GPU-6.5 CPU tile binning 已通过 WSL llvmpipe 与 Windows Intel Iris Xe 的 CPU/full-scan/binned 0 mismatch 及 stress A/B，MinGW differential 构建通过；GPU-6.5 DONE / FROZEN。
> 源码核对基线补充：GPU-6 Differential Authority 已完成；hosted CPU `toy_renderer` reference、Vulkan Raster V1、artifact/replay 与 deterministic stress 已建立，WSL llvmpipe / Windows Intel Iris Xe 均为 color/depth 0 mismatch。
> 源码核对基线补充：GPU-4 已完成 pointer-free、fixed-width、versioned Raster Command ABI V1；现有 CPU command pool 通过显式 deterministic pack/validation 生成 clear color/depth 与 opaque flat triangle command，独立 Linux runtime test 与 Windows LLP64 layout build gate 已接入，尚不执行 GPU rasterization。
> 源码核对基线补充：GPU-3 已完成；Core-owned `rf_gpu_framebuffer` 复用持久 Vulkan backend，以 compute 生成 device-local XRGB8888 framebuffer，经有限 fence、readback 与 stride-aware copy 进入 `toy_surface`；正常 runtime 仍显式 disabled/CPU renderer。
> 源码核对基线补充：GPU Phase 1 hosted probe 已覆盖 Linux/Windows 共用的 storage-buffer compute、descriptor/pipeline、command/fence 与 readback 校验，并采用 discrete-first adapter selection；WSL llvmpipe 与 Windows RTX 3050 compute/readback 均已通过；正常 freestanding Rasterfall 和 Windows 游戏构建未接入 GPU。
> 源码核对基线补充：Windows 启动地图加载的容量型 Map IR 改为临时堆分配，成功与失败均释放；不依赖扩大线程栈，详见 map-format.md 的 Runtime Bridge。
> 源码核对基线补充：Static World Lighting V2 Phase D Linux GCC freestanding / Windows MinGW 构建通过；Linux headless capture 验收，Windows仅build，Wayland交互环境不可用，见 [Phase D](static-world-lighting-phase-d.md)。
> 源码核对基线补充：Static World Lighting Phase B 复用现有编译单元与顶点亮度 rasterizer；Linux/self world-light 规则补 Runtime Map header 依赖，Windows 既有 GAME_SRCS/-MMD 覆盖；ray slab 的 double 仅用于 bake，不引入宿主 libc。
> 源码核对基线补充：Static World Lighting Phase A 的 `rasterfall_world_light.c` 已接入 Linux Game 对象、适用 self 规则与 Windows GAME_SRCS；无宿主 libc 或新资源依赖。
> 源码核对基线补充：Campaign Continuous Wall / Floor 与 Component Collision：`boundary_wall` 为长度参数化 RFU 墙体；`attr.collision=component|boundary|none` 在 Runtime Map 展开独立碰撞，保留 object owner ID；布局导出调用 C inspector 获取实际碰撞。
> 源码核对基线：工作区（Enemy Visual V2 六份公开 RFM2 / renderer-only family；`make win-rasterfall` 显式进入 Windows `all`；正式 squad roster 编译单元已纳入 Linux/Windows；Rasterfall 对象无条件重建规则；GB2312 字库进入 Linux embedded 与 Windows 资产包）

## Linux

根 `Makefile` 的 Rasterfall 区域定义全部独立编译单元、依赖和链接对象。Rasterfall 与其共享的
Tinylibc/app 对象统一依赖 `rasterfall-rebuild`，每次目标构建都会重新编译对象，以避免头文件依赖
文件缺失、不完整或切换工作区状态时复用不一致的旧对象。推荐使用 `make rasterfall` 构建；该目标内部自动按 `nproc` 并行，不需要额外传递 `-j` 参数。
底层 `app-rasterfall` 目标仍可直接使用；如需手动控制并行度，可调用
`make -j12 app-rasterfall`。
构建 freestanding Linux 程序，窗口/输入/渲染/音频来自仓库 Tinylibc 与公共库。默认运行时读取
`rasterfall/assets`；`rasterfall-embedded` 才嵌入公开资源。
GB2312 字库位于 `rasterfall/assets/fonts/`，普通运行缺少 `gb2312-16.rfh` 时会明确报错并停止；
embedded 目标通过公开资产扫描自动纳入该文件及其许可/来源。

平台相关实现主要是：

- `lib/platform/window_wayland.c`、`lib/graphics/wayland_min.c`：Wayland 窗口与协议。
- `lib/graphics/renderer.c`、`lib/input/input.c`：共享软件渲染器和输入状态。
- `lib/audio/audio.c`、`lib/audio/alsa.c`、`lib/audio/pulse_min.c`：Linux 音频抽象及 ALSA/Pulse 后端。
- `compiler/toyc_rt.c`、`lib/`、`include/tlibc/`：freestanding 运行时和 libc。

## Windows

`windows/Makefile` 用 MinGW-w64 + SDL2 构建相同玩法/渲染源，并加入 `windows/src/` 的 runtime、
WinSock、SDL 窗口/音频、线程和 WinMain 适配。平台契约头在 `windows/include/`。资源定位和包结构见
`windows/README.md`；对象同样依赖无条件重建目标，确保共享头文件变化不会留下旧的 Windows 对象；
不要把 Windows 修复硬编码进共享玩法，优先修平台适配层。
Windows package 复制整个 `rasterfall/assets`，因此会同时携带字库、BDF 源文件和许可。

## GPU 探针

`make gpu-probe` 构建 Linux `build/rf-gpu-probe`；`make win-gpu-probe` 使用 MinGW 构建
Windows 控制台程序 `build/rf-gpu-probe.exe`。两者都是 hosted 开发工具，不进入 `LIBC_A`、
`APP_EXTRA_OBJS_rasterfall`、embedded 目标、正常 Windows 游戏或 package。它们通过仓库内最小
Vulkan ABI 声明分别加载系统 `libvulkan.so.1` / `vulkan-1.dll`，不需要 Vulkan SDK，但运行机器
仍必须提供 Vulkan loader 与可用 ICD/驱动。

当前 probe 通过 `rf_gpu` 正式 service 启动 hosted Vulkan backend。backend 枚举 adapter/queue
family 后持久创建 device/queue，并完成 storage buffer、host-visible
memory、descriptor、内嵌 SPIR-V compute pipeline、command buffer、dispatch、fence wait 和
readback verification 的完整最小闭环。它没有 surface 或 swapchain。WSL llvmpipe correctness
已验收；Windows 原生枚举 AMD integrated 与 NVIDIA RTX 3050 Laptop GPU，discrete-first
策略明确选择 NVIDIA，compute/readback 结果通过。探针的 `total` 从 upload 前计至 readback 后，
包含首次 descriptor/pipeline/command resource 创建，不代表稳态 GPU dispatch 时间。
后续 GPU service 接入 Core 前必须继续保持正常 CPU renderer 为默认路径，并为 loader/device
不可用定义可复核的 fallback。

GPU-2A 已将 fallback 语义固化在 `rf_gpu`：optional 对 unavailable/failed 返回成功并保留状态，
required 对两者返回失败，disabled 不调用 backend；只有 READY backend 会在 Core shutdown 时释放。
`rf_core_get_status()` 提供概要状态，`rf_core_get_gpu_status()` 提供 adapter/message snapshot，均不暴露
Vulkan handle。GPU-2B 的 `rf_gpu_vulkan_backend` 持久拥有 loader、instance、选中 physical device、
logical device 与 queue；init 继续执行 compute/readback 门禁，shutdown 逆序释放。当前正常 runtime
显式 disabled；`make gpu-service-test` 验证平台无关契约，`make gpu-probe && build/rf-gpu-probe`
验证真实 backend 生命周期。GPU framebuffer、surface 与 swapchain 不属于 GPU-2。

GPU-3 的无窗口入口为 `make gpu-framebuffer-test` / `build/rf-gpu-framebuffer-test`，Windows
交叉构建为 `make win-gpu-framebuffer-test`。它验证固定尺寸全像素/hash、非紧密 destination stride、
resize 及 framebuffer-before-backend shutdown。output 是 device-local storage/transfer-src buffer，
readback 是 host-visible transfer-dst buffer；优先 coherent，否则 invalidate。command buffer 固定记录
compute → barrier → copy，fence 最长等待 5 秒。resize 仅 replacement-first 重建 framebuffer 资源。
WSL llvmpipe correctness、Windows MinGW build 与 RTX 3050 实机 smoke 均已通过；Windows probe
确认选中 NVIDIA GeForce RTX 3050 Laptop GPU（discrete）。
正常 Rasterfall 未链接 hosted backend，CPU renderer 与 optional/required 启动语义不变。

GPU-4 的 ABI 定义在 `rasterfall/include/rf_gpu_raster_abi.h`，CPU adapter 定义在
`rasterfall/include/rf_gpu_raster_pack.h` / `gpu/src/rf_gpu_raster_pack.c`。`make gpu-raster-abi-test`
运行 layout、deterministic packing、validation/rejection 测试；`make win-gpu-raster-abi-test` 验证
同一固定布局可由 MinGW LLP64 编译。该路径是 hosted GPU 开发设施，不进入正常 Rasterfall link；
GPU framebuffer smoke 与 CPU renderer 行为均未改变。GPU-5 通过独立
`rf_gpu_raster` owner 消费该 stream，不进入正常 Rasterfall link。

GPU Capability Contract V1 将 Vulkan properties/features/memory properties 复制为无 handle 的
`rf_gpu_capabilities`，并在 status 中分开 service READY 与 compute/framebuffer/raster_v1。
`make gpu-service-test` 覆盖 READY+支持/不支持 Raster V1、16x16/8x8 选择、coherent/
non-coherent readback capability、integrated/discrete/CPU adapter snapshot 和 CPU fallback 边界。
probe 输出 adapter index、API/compute/storage limits、`shaderInt64`、heap/type flags 和派生能力。
Linux 与 Windows 仍共用 backend，选 memory type 只依据 `memoryTypeBits` 与 property flags。

GPU-5 的无窗口入口为 `make gpu-raster-test` / `build/rf-gpu-raster-test`，Windows
交叉构建为 `make win-gpu-raster-test`。它输出 canonical XRGB8888 color 和
signed-32 depth，覆盖固定 hash、stride、resize、upload growth、capability rejection
和 cleanup。shader 是两个固定 16×16/8×8 SPIR-V 变体，不需要 runtime
shader compiler。正常 Rasterfall 仍为 disabled/CPU renderer。

GPU-6 的无窗口入口为 `make gpu-raster-diff-test` / `build/rf-gpu-raster-diff-test`，Windows
交叉构建为 `make win-gpu-raster-diff-test`。它让同一 GPU-4 stream 同时进入正式软件 renderer
adapter 与 GPU-5 compute path，逐 RGB24 与完整 signed depth 比较，并支持
`--replay-raster-stream commands.bin`。mismatch 默认产生可重放 stream、CPU/GPU/diff BMP、depth
binary 和文本报告；正常 Rasterfall 链接、renderer selection 与 CPU fallback 均未改变。

GPU-6.5 新增 `make gpu-raster-binning-test` 的纯 CPU acceleration-structure 测试。GPU differential
默认对同一 stream 执行 CPU、diagnostic full-scan 与 tile-binned pipeline；输出 CPU binning、
tile-list/command upload、submit、两种 execution-wait、readback 和 tile ref 统计。Windows 通过
`make win-gpu-raster-diff-test` 构建同一测试；Intel Iris Xe 已完成全部 fixture/stress/replay，最大
1279×719 / 1026-command stress 的 full-scan / binned execution-wait 为 230.272 / 163.568 ms，
CPU binning 8.343 ms，color/depth 均 0 mismatch。

GPU-7A 使用两段式无窗口入口，避免把 hosted Vulkan/宿主 libc 引入 freestanding normal game：

```sh
build/rasterfall --gpu-world-raster-test near 30 build/world-near-30.bin
build/rf-gpu-raster-diff-test --replay-raster-stream build/world-near-30.bin
```

前者复用正常 Campaign frontend，报告 composition、supported ratio、bbox 与 frontend/classification/
pack timing；后者复用 GPU-6 oracle 和 GPU-6.5 binning，报告 mismatch/hash、tile refs、upload、
execution-wait 与 readback。结果是 partial selected stream，不与完整 CPU normal frame 比较。硬件性能
结论以当前实际可用 physical adapter 为准；只有 llvmpipe 时仅作 correctness 观测。

## 改文件列表时

新增 Rasterfall `.c` 文件通常必须同时加入根 Makefile 的对象/规则及 `windows/Makefile` 的源列表；
若该文件要求 Toyc 自托管，还要核对 self 对象规则。新增公开运行时资源要检查默认文件加载、内嵌资源
依赖和 Windows package 的复制规则；模型展示扫描必须覆盖 `rasterfall/assets/models/` 下的分类子目录。

Enemy Visual V2 的六份公开 RFM2 自动进入现有递归 embedded 依赖及 Windows assets 复制；
其 renderer `.inc` 已加入 Linux/self 显式依赖，Windows 使用 `-MMD` 跟踪，不新增平台编译单元。
详见 [enemy-visuals.md](enemy-visuals.md)。

## 最小验证矩阵

- 纯玩法/session/map：`make rasterfall`，再运行 `build/rasterfall --logic-test`。
- 渲染或模型：构建 + logic test，并使用相关 dump/benchmark/诊断参数；涉及画面时做实际启动检查。
- 网络：先跑 logic test 中的 packet/pipeline 用例，再按 `network-architecture.md` 做所需人工拓扑。
- Linux 平台：实际 Wayland/ALSA 启动；无图形/音频环境时明确报告未覆盖项。
- Windows 平台或共享平台契约：依赖已准备时运行 `make win-rasterfall`；打包变化再跑 package。

具体可用命令以 `rasterfall/README.md`、根 README 和 `--help` 为准。不要在本导航记录会变化的测试
通过数量。Rasterfall 不要求由 Toyc 编译；除非改动触及工具链或公共自托管路径，不必扩大到编译器全套测试。

Component collision 新编译单元 `rasterfall/lib/rasterfall_map_components.c` 纳入 Linux Game、
map inspector/runtime test、Windows GAME_LIB_SRCS 与适用 self 规则。没有新增公开资源，
既有 embedded/package 地图复制继续适用；map-layout 依赖 C inspector。
