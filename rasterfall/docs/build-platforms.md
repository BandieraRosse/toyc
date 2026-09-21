# 构建、平台与验证

> 文档更新：2026-09-21
> 源码核对基线：`fc75009`；Windows PowerShell 主开发 lane 与 WSL 支持边界

## 当前开发平台优先级

Rasterfall 当前处于 GPU 渲染持续开发阶段。Windows 原生 PowerShell 是主要开发、构建编排、物理 GPU
验证和签收环境；`windows/NativeCodex.ps1`、package root、帧审计和实际适配器输出构成当前事实入口。
共享源码和 freestanding Linux 路径继续保留，但 WSL 仅作为辅助/历史兼容路径，不保证随主线同步更新、
可构建或运行正确。WSL/llvmpipe/hosted Vulkan 结果不能替代 Windows native present、驱动、窗口生命周期
和性能验收。

## 当前 Windows GPU 验收状态

Intel Iris Xe 已通过 strict native present、Fog/Post smoke、正式地图 320 帧 zero-fallback audit 和窗口拉伸；最近固定视角的命令、frontend、GPU fence 与 native present 记录见 [GPU 当前状态](gpu-current-state.md)。
RTX 3050 上曾在 `vkCreateSwapchainKHR` 首次调用时访问冲突；native Vulkan 窗口改用 SDL software renderer 后，strict native mixed 帧已通过 10 帧 smoke。该软件 renderer 只负责窗口侧 SDL 兼容，world 与最终帧仍由 GPU mixed 和 Vulkan present 完成。
复现过程、排除项、窗口 API 的职责及实机验证边界见 [RTX 3050 swapchain 兼容修复](gpu-nvidia-swapchain-compat.md)。

## Linux

根 `Makefile` 的 Rasterfall 区域定义全部独立编译单元、依赖和链接对象。Rasterfall 与其共享的
Tinylibc/app 对象统一依赖 `rasterfall-rebuild`，每次目标构建都会重新编译对象，以避免头文件依赖
文件缺失、不完整或切换工作区状态时复用不一致的旧对象。推荐使用 `make rasterfall` 构建；该目标内部自动按 `nproc` 并行，不需要额外传递 `-j` 参数。
兼容入口 `app-rasterfall` 也会在目标内部按 `nproc` 自动并行；新脚本和文档应优先使用正式名称
`make rasterfall`。
构建 freestanding Linux 程序，窗口/输入/渲染/音频来自仓库 Tinylibc 与公共库。默认运行时读取
`rasterfall/assets`；`rasterfall-embedded` 才嵌入公开资源。
GB2312 字库位于 `rasterfall/assets/fonts/`，普通运行缺少 `gb2312-16.rfh` 时会明确报错并停止；
embedded 目标通过公开资产扫描自动纳入该文件及其许可/来源。

Linux 原生或 freestanding 修改仍应尽量保持构建正确；但不要把 WSL 当作 Rasterfall 当前主要开发环境。
若 WSL 构建、Wayland、音频或 Vulkan 路径落后，应明确记录为未维护/未覆盖，而不是据此否定 Windows
主线结果，也不要求 GPU 功能开发等待 WSL 修复。

平台相关实现主要是：

- `lib/platform/window_wayland.c`、`lib/graphics/wayland_min.c`：Wayland 窗口与协议。
- `lib/graphics/renderer.c`、`lib/input/input.c`：共享软件渲染器和输入状态。
- `lib/audio/audio.c`、`lib/audio/alsa.c`、`lib/audio/pulse_min.c`：Linux 音频抽象及 ALSA/Pulse 后端。
- `compiler/toyc_rt.c`、`lib/`、`include/tlibc/`：freestanding 运行时和 libc。

## Windows

Windows Native Codex 的统一入口是 `windows/NativeCodex.ps1`。它固定将 MSYS2
`mingw64` 与 `usr/bin` 放在该 lane 的 PATH 前端，继续使用现有
`windows/Makefile` 和静态 SDL2，不引入新的构建系统。Windows 对象、exe 和
package 默认位于 `build-windows/`，Linux `build/` 保持独立；`package` 后的真实
运行 root 是 `build-windows/rasterfall-windows`，日常闭环与 WIN-DEV-1 标准见
[Windows Native Codex](windows-native-codex.md)。

`--frame-audit` 的三行记录同时写标准输出与 exe 同目录 `rasterfall.log`：第一行包含递增 frame ID、
`gpu-native`/`cpu-fallback`/`cpu` 最终路径、camera/pitch/extent 和主循环 timing；第二行包含 RenderFrame
层计数、最终 layer cursor、非法逆序次数、retained pre-post command 数、整帧 CPU replay 决策与 unsupported 分类；第三行包含 GPU/native timing、overlay upload、readback 与 CPU framebuffer
copy 字节数。因而 Windows 验收不再依赖控制台留存。

`windows/Makefile` 用 MinGW-w64 + SDL2 构建相同玩法/渲染源，并加入 `windows/src/` 的 runtime、
WinSock、SDL 窗口/音频、线程和 WinMain 适配。平台契约头在 `windows/include/`。资源定位和包结构见
`windows/README.md`；对象同样依赖无条件重建目标，确保共享头文件变化不会留下旧的 Windows 对象；
不要把 Windows 修复硬编码进共享玩法，优先修平台适配层。根目标 `make win-rasterfall` 在内部按
本机 `nproc` 并行调用 Windows Makefile；打包目标 `make win-rasterfall-package` 也沿用该并行入口，
调用方不需要额外传递 `-j` 参数。
Windows package 复制整个 `rasterfall/assets`，因此会同时携带字库、BDF 源文件和许可。
normal GPU 实机必须从 package 目录启动，确保 exe-relative 的 `rasterfall/assets` 可见；直接运行
`build/rasterfall.exe` 会按其所在目录寻找 `build/rasterfall/assets`，不代表 GPU 初始化失败。

## GPU 探针

GPU-8A 不把 SDL 私有对象交给 Vulkan backend。`windows/src/window_sdl.c` 在平台层内部通过
`SDL_GetWindowWMInfo` 取得 HWND，并只通过 `toy_native_window_handle` 暴露类型、window 与 module
三个整数句柄。backend 在明确请求 native present 时启用 `VK_KHR_surface`、
`VK_KHR_win32_surface` 与 device `VK_KHR_swapchain`；能力失败只关闭 Native Presentation V1，
不改变 compute/Raster V1 service READY。swapchain 只接受 query 返回的
`B8G8R8A8_UNORM + SRGB_NONLINEAR` 与 transfer-destination usage，使用 FIFO 和 single-frame-in-flight，
并保持 Raster V1 color/depth buffer 所有权不变。运行入口：

```sh
rasterfall.exe --renderer gpu-compute --gpu-native-present --gpu-normal-scene near 0 --frames 120
```

该入口是 world-only proof，不是完整 normal presentation；正常 software-present A/B 去掉
`--gpu-native-present`。shutdown 的 `GPU-NATIVE` 行报告 acquire、combined GPU raster wait、
buffer-copy command record、submit、present、total，以及必须为零的 color readback/CPU framebuffer copy。
Intel Iris Xe near/0 同机 10 帧最后观测为 native total 29.009 ms（raster wait 23.989 ms、present
1.990 ms、readback 0），software-present total 102.399 ms（execution 36.672 ms、readback 63.316 ms）。
窗口由 1280×720 resize 后，300/300 native frames、零 fallback，最终 swapchain extent 984×661。

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
该 probe 本身不创建 surface/swapchain；normal runtime 由同一 backend 的 Core-owned 路径另行管理
framebuffer、Raster V1 与 native presentation。CPU 仍为默认 renderer，loader/device 不可用时按 optional/required policy 处理。

GPU-2A 已将 fallback 语义固化在 `rf_gpu`：optional 对 unavailable/failed 返回成功并保留状态，
required 对两者返回失败，disabled 不调用 backend；只有 READY backend 会在 Core shutdown 时释放。
`rf_core_get_status()` 提供概要状态，`rf_core_get_gpu_status()` 提供 adapter/message snapshot，均不暴露
Vulkan handle。GPU-2B 的 `rf_gpu_vulkan_backend` 持久拥有 loader、instance、选中 physical device、
logical device 与 queue；init 继续执行 compute/readback 门禁，shutdown 逆序释放。默认 normal runtime
选择 disabled/CPU，显式 `--renderer gpu-compute` 选择 optional/required GPU 路径。`make gpu-service-test`
验证平台无关契约，`make gpu-probe && build/rf-gpu-probe` 验证 hosted backend 生命周期。

GPU-3 的无窗口入口为 `make gpu-framebuffer-test` / `build/rf-gpu-framebuffer-test`，Windows
交叉构建为 `make win-gpu-framebuffer-test`。它验证固定尺寸全像素/hash、非紧密 destination stride、
resize 及 framebuffer-before-backend shutdown。output 是 device-local storage/transfer-src buffer，
readback 是 host-visible transfer-dst buffer；优先 coherent，否则 invalidate。command buffer 固定记录
compute → barrier → copy，fence 最长等待 5 秒。resize 仅 replacement-first 重建 framebuffer 资源。
WSL llvmpipe correctness、Windows MinGW build 与 RTX 3050 实机 smoke 均已通过；Windows probe
确认选中 NVIDIA GeForce RTX 3050 Laptop GPU（discrete）。
该无窗口 fixture 与 normal frame resource 分离；normal Windows GPU 路径链接同一共享 backend。

GPU-4 的 ABI 定义在 `rasterfall/include/rf_gpu_raster_abi.h`，CPU adapter 定义在
`rasterfall/include/rf_gpu_raster_pack.h` / `gpu/src/rf_gpu_raster_pack.c`。`make gpu-raster-abi-test`
运行 layout、deterministic packing、validation/rejection 测试；`make win-gpu-raster-abi-test` 验证
同一固定布局可由 MinGW LLP64 编译。该 ABI 同时由 hosted differential 和显式 normal
`gpu-compute` 路径消费；默认 CPU renderer 不依赖它。

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
shader compiler。该无窗口入口作为 normal GPU raster 所用共享 kernel/resource 的独立门禁。

GPU-6 的无窗口入口为 `make gpu-raster-diff-test` / `build/rf-gpu-raster-diff-test`，Windows
交叉构建为 `make win-gpu-raster-diff-test`。它让同一 GPU-4 stream 同时进入正式软件 renderer
adapter 与 GPU-5 compute path，逐 RGB24 与完整 signed depth 比较，并支持
`--replay-raster-stream commands.bin`。mismatch 默认产生可重放 stream、CPU/GPU/diff BMP、depth
binary 和文本报告；normal GPU frame 复用同一 packed stream/oracle 契约，renderer selection 与 CPU fallback 由 Core 管理。

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
