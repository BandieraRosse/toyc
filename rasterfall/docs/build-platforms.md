# 构建、平台与验证

> 文档更新：2026-09-20
> 源码核对基线补充：2026-09-20 HG-2C4 最新 Windows package 通过 strict native 120 帧、专用 mixed gate 与四 extent 140 帧 resize gate。`hardware_graphics_resize.ps1` 的资源检查适配双帧在途：允许 fence 完成前的非零 pin，但要求 `retired=0`、`failed=0` 且 pinned resources 不超过 live resources；loads 在 resize 全程稳定。
> 源码核对基线补充：2026-09-19 `toy_window_open_native()` 在 Windows 为 native Vulkan 窗口创建 SDL software renderer；普通 `toy_window_open()` 仍使用原 SDL renderer。Core config 根据 `native_present` 选择入口，Wayland 共用原窗口实现。RTX 3050 strict native 10 帧零回退、零读回、零 CPU framebuffer copy；Fog 10 帧、三 extent native gate 与 Windows `--logic-test` 通过。
> 源码核对基线补充：2026-09-19 GPU 最终帧诊断沿用 Windows normal player 与现有 Vulkan backend，不新增编译单元或资源。Windows `gpu-mixed-executor-test`、`gpu-raster-test` 和 `hardware_graphics_proof.ps1 -ExecutorGate/-MixedGate/-NativeGate` 覆盖批量 Draw 交错和 native 呈现；`hardware_graphics_resize.ps1 -NoRedirect` 在 PowerShell 重定向停滞时仍用 runtime log 验证 140 帧四 extent。Linux freestanding 路径不调用 hosted mixed executor。
> 源码核对基线补充：2026-09-19 Windows normal player 已链接 `rf_gpu_mixed_executor.c` 与 `rf_gpu_resource_cache.c`，仅 strict native GPU 模式启用正常 mixed 帧；独立测试目标复用这些对象。`tools/hardware_graphics_resize.ps1` 等待窗口 140 帧上限已放宽为 180 秒，Intel 四种 extent 与 pin 稳态通过。Linux freestanding/self 未增加 hosted GPU 编译单元。
> 源码核对基线补充：2026-09-19 Windows `gpu-mixed-executor-test` 增加 `--native-window` 模式，复用 SDL/Win32 native handle、Vulkan swapchain 与现有测试目标；`tools/hardware_graphics_proof.ps1 -NativeGate` 是三帧 resize/native 呈现实机 smoke。未增加玩家 CLI、编译单元或 package 资源。
> 源码核对基线补充：2026-09-19 [HG-2B Core GPU executor](hardware-graphics-hg2b.md#core-真实离屏执行器)：`gpu/include/rf_gpu_mixed_executor.h` / `gpu/src/rf_gpu_mixed_executor.c` 已接通 frozen plan、registry cache 和 Raster ABI/indexed draw；整帧 preflight 先于 CLEAR，VIEWMODEL/Post 仅在尾段执行。`gpu-mixed-executor-test` / `-ExecutorGate` 为真实离屏门禁。混合 overlay/native/strict 和 normal producer 仍待实现；下方旧增量记录中的待实现项以本条及新 checkpoint 节为准。
> 源码核对基线补充：2026-09-19 [HG-2B registry GPU cache](hardware-graphics-hg2b.md#registry-gpu-cache) 新增 hosted `gpu/src/rf_gpu_resource_cache.c`，仅由 Windows `gpu-resource-cache-test` 链接真实 registry/model runtime；根 `win-gpu-resource-cache-test` 转发此目标。未加入 normal player、Linux freestanding 或 self；无新玩家参数、shader 或资源。graphics 资源拆分仍由既有 backend `.inc` 和依赖规则编译。
> 源码核对基线补充：2026-09-19 HG-2B [Core 混合帧计划](hardware-graphics-hg2b.md#core-混合帧计划基础) 由既有 `rf_core_host.c` 包含 `.inc`，无新编译单元、CLI 或资产；根 Makefile 显式依赖头文件/实现/fixture，Windows `-MMD` 自动依赖覆盖；self 无独立 Core 规则需要扩充。`--logic-test` 进入同一 Core 实现。
> 源码核对基线补充：2026-09-19 [HG-2B Raster ABI 分段基础](hardware-graphics-hg2b.md#raster-abi-分段基础hg-2b-进行中)：`rf_gpu_vulkan_raster_segment()` 使用独立范围/CLEAR/LOAD 参数，验证完整 stream；中间段不读回，VIEWMODEL/Post 留在末段。真实 graphics 交错与 Core/native 接入仍待实现。
> 源码核对基线补充：2026-09-19 [HG-2B 整数深度与 target bridge](hardware-graphics-hg2b.md) 已实现 GPU 整数裁剪/投影/深度、GPU color/depth 往返转换及 attachment LOAD；Intel 前置门禁通过。Raster ABI CLEAR/LOAD 分段基础已在 Intel 验证；compute/graphics 桥接、Core 混合顺序与 strict native 门禁仍待实现，正常帧不变。
> 源码核对基线补充：2026-09-19 [HG-2A](hardware-graphics-hg2a.md)：根 Makefile 的 `gpu-graphics-test` / `win-gpu-graphics-test` 与 Windows `gpu-graphics-test` 构建独立 hosted proof；graphics ABI/shader 已纳入 backend 依赖。无新增 freestanding/self 编译单元或玩家 CLI/资产；Windows package 与原 compute 基线通过。
> 源码核对基线补充：2026-09-19 [HG-1B](hardware-graphics-hg1b.md) 新增资源编译单元，根 Makefile 正常/self 与 Windows 列表同步；Windows 独立 `rasterfall-resource-test.exe` 验证真实 world switch、CPU renderer resize 和释放，不进入玩家 package；`tools/hardware_graphics_resize.ps1` 检查 native/Fog resize 与 registry 稳态。
> 源码核对基线补充：2026-09-19 [HG-1A Draw/reference](hardware-graphics-hg1a.md) 的 header/inc 已加入根 Makefile 正常/self 依赖，Windows 自动依赖覆盖；`--logic-test` 增加无外部资产 Draw 对照，Windows package 与 Intel 基线验收通过。
> 源码核对基线补充：2026-09-19 HG-1A 前置修复沿用共享 renderer 与现有 Windows package/differential 编译列表；基线脚本先运行完整 differential suite，`-Checkpoint` 标注 manifest，详见 [修复记录](hardware-graphics-hg1-preflight.md)。
> 源码核对基线补充：2026-09-19 HG-0 冻结 [Hardware Graphics 架构与基线](hardware-graphics-architecture.md)；显式 `--frame-audit` 改为逐帧输出，测量脚本记录各入口独立口径与原始证据。
> 源码核对基线补充：Windows `--logic-test` 聚合测试的大型局部 fixture 曾超过默认主线程栈并以 0xC00000FD 退出；`windows/Makefile` 将链接栈 reserve 设为 16 MiB，正式构建现可完整通过逻辑测试。栈按需提交，不改变玩法或 GPU 帧逻辑。
> 源码核对基线补充：2026-09-19 Windows strict GPU 老地图全向扫视覆盖 Texture V1 高命令量 pack；纹理 handle 改为本帧唯一视图表查找，避免方向相关的 watchdog 退出。
> 源码核对基线补充：2026-09-19 `rf_core_host.c` retained WORLD partition 同步实际分配容量；跨帧缩小/增长回归覆盖缓存复用。
> 源码核对基线：Windows normal binary 已链接共享 Vulkan backend、Raster V1、Texture V1、Post-Raster V1、overlay composite 和 Win32 swapchain presentation；Core 在 viewmodel barrier 之前按层保留 pre-post command。纯 Raster V1 effects command 与 VIEWMODEL span marker 可随 retained stream 消费；transparent、effects direct pixels 或 generic unsupported command 会记录原因并使整帧按原批次 CPU replay。默认仍为 CPU，GPU 由命令行显式选择。
> 当前平台边界：Intel Iris Xe 的 strict/Fog smoke、正式地图 320 帧零回退波次复现与窗口拉伸已确认，GPU 功能阶段结束；最近性能快照见 [GPU 当前状态](gpu-current-state.md)。Windows window/input/audio 仍由 SDL2 提供，SDL-free Native Platform 尚未实现。
> 源码核对基线补充：Windows 启动地图加载的容量型 Map IR 改为临时堆分配，成功与失败均释放；不依赖扩大线程栈，详见 map-format.md 的 Runtime Bridge。
> 源码核对基线补充：Static World Lighting V2 Phase D Linux GCC freestanding / Windows MinGW 构建通过；Linux headless capture 验收，Windows仅build，Wayland交互环境不可用，见 [Phase D](static-world-lighting-phase-d.md)。
> 源码核对基线补充：Static World Lighting Phase B 复用现有编译单元与顶点亮度 rasterizer；Linux/self world-light 规则补 Runtime Map header 依赖，Windows 既有 GAME_SRCS/-MMD 覆盖；ray slab 的 double 仅用于 bake，不引入宿主 libc。
> 源码核对基线补充：Static World Lighting Phase A 的 `rasterfall_world_light.c` 已接入 Linux Game 对象、适用 self 规则与 Windows GAME_SRCS；无宿主 libc 或新资源依赖。
> 源码核对基线补充：Campaign Continuous Wall / Floor 与 Component Collision：`boundary_wall` 为长度参数化 RFU 墙体；`attr.collision=component|boundary|none` 在 Runtime Map 展开独立碰撞，保留 object owner ID；布局导出调用 C inspector 获取实际碰撞。
> 源码核对基线：工作区（Enemy Visual V2 六份公开 RFM2 / renderer-only family；`make win-rasterfall` 显式进入 Windows `all`；正式 squad roster 编译单元已纳入 Linux/Windows；Rasterfall 对象无条件重建规则；GB2312 字库进入 Linux embedded 与 Windows 资产包）

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
