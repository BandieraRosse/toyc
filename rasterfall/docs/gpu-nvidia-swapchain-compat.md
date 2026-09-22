# Windows RTX 3050 native Vulkan 首帧兼容修复

> 文档更新：2026-09-19
> 源码核对基线：2026-09-19 工作区；`windows/src/window_sdl.c`、`include/toy_window.h`、`lib/platform/window_wayland.c`、`rasterfall/src/rf_core_host.c`、`tools/rasterfall_gpu_mixed_test.c`；Windows RTX 3050 Laptop GPU 实机复测。

## 症状与影响范围

同一份 Windows package 在 Intel Iris Xe 上可以执行 strict native GPU 帧，在本机 NVIDIA GeForce RTX 3050 Laptop GPU 上，首次使用 `--renderer gpu-compute --gpu-required --gpu-native-present --frames 1` 时以 Windows 异常 `0xC0000005` 退出。普通 `--renderer gpu-compute --frames 1` 可用。该问题发生在 native 窗口呈现链，不能据此判断 GPU compute 或 graphics Draw 不受支持。

## 定位证据

临时阶段日志与本机重复运行将首帧顺序收敛为：

1. Vulkan 初始化、GPU compute、mixed frame 收集和整帧 preflight 完成。4 个 static prop Draw 均完成资源 prepare、bind 与资格校验。
2. WORLD Raster 前段与 graphics Draw batch 均提交成功；最终 Raster 段进入 native present，overlay 拷贝完成。
3. `raster_swapchain_create()` 成功取得 surface capabilities、5 个 surface formats 和 5 个 present modes。使用 1280×720、BGRA8 UNORM、FIFO、3 张 image 调用 `vkCreateSwapchainKHR` 后进程访问冲突；调用尚未返回，也尚未进入 `vkAcquireNextImageKHR` 或 `vkQueuePresentKHR`。
4. 不启用 mixed Draw、仅使用 `--renderer gpu-compute --gpu-native-present --frames 1` 时，同样在首次 `vkCreateSwapchainKHR` 崩溃。因此早期“mixed draw/resource 编码阶段崩溃”的判断被排除。

这是调用位置和隔离实验给出的结论；没有驱动内部调用栈，不能断言 NVIDIA 驱动内部的具体空指针来源。

## 修复机制与所有权

原 Windows 窗口在 `SDL_CreateWindow()` 后立即用 `SDL_RENDERER_PRESENTVSYNC` 创建 SDL renderer。该 renderer 可在同一个 HWND 上建立自己的硬件呈现链，而 native GPU 路径随后又为该 HWND 创建 Vulkan swapchain。隔离试验中，将 SDL renderer 改为 `SDL_RENDERER_SOFTWARE` 后，RTX 3050 的 strict native 首帧正常完成。单独更改 Win32 `HINSTANCE` 来源、添加 `SDL_WINDOW_VULKAN` 标志或添加 color attachment image usage 均未消除崩溃；这些试验未保留在修复中。

正式实现仅在 native Vulkan 窗口使用软件 SDL renderer：

| 层 | 当前职责 |
| --- | --- |
| `include/toy_window.h` | 提供 `toy_window_open_native()` 平台入口。 |
| `rasterfall/src/rf_core_host.c` | `rf_core_init_config()` 根据 `native_present` 选择窗口入口；原 `rf_core_init()` 继续使用普通入口。 |
| `windows/src/window_sdl.c` | 普通窗口沿用 `SDL_RENDERER_PRESENTVSYNC`；native 窗口使用 `SDL_RENDERER_SOFTWARE`，避免 SDL 硬件呈现链与 Vulkan 争用 HWND。 |
| `lib/platform/window_wayland.c` | native 入口转到现有 Wayland 窗口实现；没有引入新的 Linux 呈现路径。 |
| `tools/rasterfall_gpu_mixed_test.c` | `--native-window` 诊断使用与正式 native 帧相同的窗口入口。 |

SDL software renderer 只负责窗口侧兼容与既有软件 surface 能力；strict native 帧的 world、mixed graphics/compute、Post、overlay composite 和最终 present 仍按原 GPU 路径执行。没有改变 Raster ABI、Vulkan swapchain 参数、GPU 资源生命周期或 `--gpu-required` 的零回退合同。将软件 renderer 限定到 native 窗口，也避免改变普通 CPU 与 compute readback 模式的 SDL 呈现方式。

## 本机验证与边界

从 package 运行根目录 `build-windows/rasterfall-windows` 执行；构建命令为 `make -f windows/Makefile all` 和 `make -f windows/Makefile gpu-mixed-executor-test`。在 RTX 3050 Laptop GPU 上：

- strict native normal scene 连续 10 帧退出码 0；`--frame-audit` 显示 10/10 GPU 帧、零 fallback、零 color readback 和零 CPU framebuffer copy。
- 当时尚存在的 `--gpu-post-fog` strict native 连续 10 帧退出码 0；该开关已在后续统一 fog-free runtime
  策略中移除，本条只保留为底层 Post ABI 的历史实机证据。
- `rasterfall-gpu-mixed-test.exe --native-window` 通过，覆盖 96×72、120×57、120×33 三个 extent，均为零 readback 与零 CPU framebuffer copy。
- Windows `--logic-test`、普通 GPU compute 单帧和 CPU 单帧均通过；`git diff --check` 通过。

本记录只证明上述机器和场景的首帧及短时运行。RTX 3050 的长时游玩、完整窗口 resize 矩阵、设备丢失恢复、其他 NVIDIA 型号与 Linux 图形环境未在本轮验收。包的完整 `package` 目标曾因本机 MSYS 环境缺少 `python3` 命令而停止；Windows exe 的 `all` 构建成功，并复制到现有 package 目录完成上述运行测试，未将该环境问题归因于 GPU 修复。
